package chain

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"math/big"
	"time"

	"github.com/ethereum/go-ethereum"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
)

// VOID-135a — on-chain event watcher for Escrow.SettlementCreated.
//
// The watcher polls FilterLogs every `interval` from a stored cursor
// and hands each decoded event to a caller-supplied handler. Polling
// (vs WebSocket subscribe) was chosen because:
//
//   - it works on any JSON-RPC endpoint (Anvil, Sepolia, local nodes
//     that don't expose WS)
//   - no reconnect state machine
//   - catches up after a gateway restart without needing a separate
//     subscription resync path
//
// Tuning knobs:
//
//   - interval: flat-sat scope requires "within 2 s" of settlement, so
//     DefaultInterval = 2 * time.Second
//   - FromBlock: caller-supplied; the orchestrator picks a "genesis or
//     last processed" value and the watcher advances it after each poll.

// DefaultWatchInterval is 2 s — the flat-sat acceptance criterion for
// "settlement → receipts.json line".
const DefaultWatchInterval = 2 * time.Second

// EventFilterer is the minimum surface the watcher needs from a
// go-ethereum client. Both `*ethclient.Client` and
// `simulated.Backend.Client()` satisfy it, so tests can exercise the
// watcher in-process.
//
// BlockNumber is required (not just FilterLogs) because the simulated
// backend rejects FilterLogs with FromBlock > head as "invalid block
// range params" — production RPC returns empty results in that case,
// but we can't rely on that universally. Polling for the head block
// first lets us skip the FilterLogs call entirely when the cursor has
// already advanced past the chain head (no new settlements).
type EventFilterer interface {
	FilterLogs(ctx context.Context, q ethereum.FilterQuery) ([]types.Log, error)
	HeaderByNumber(ctx context.Context, number *big.Int) (*types.Header, error)
	BlockNumber(ctx context.Context) (uint64, error)
}

// SettlementEvent is the decoded form of an Escrow.SettlementCreated log.
// Fields:
//   - SatID / Amount / TxNonce / Wallet — unpacked from the event data
//     + indexed topics; match the on-chain Solidity struct.
//   - TxHash / BlockNumber / BlockTimeSec — chain coordinates of the
//     settling transaction, useful for the receipt's exec_time and for
//     receipts.json provenance.
type SettlementEvent struct {
	SatID          uint32
	Amount         *big.Int
	TxNonce        *big.Int
	Wallet         common.Address
	TxHash         common.Hash
	BlockNumber    uint64
	BlockTimeSec   uint64 // block header timestamp (Unix seconds)
}

// Watcher polls an EVM endpoint for SettlementCreated logs and forwards
// decoded events. Not safe for concurrent Run calls — one watcher runs
// in one goroutine.
type Watcher struct {
	client      EventFilterer
	contract    common.Address
	interval    time.Duration
	eventTopic  common.Hash
	nextBlock   uint64 // block from which to resume on the NEXT poll
}

// NewWatcher builds a watcher for the given Escrow deployment. `fromBlock`
// is inclusive — a fresh gateway should pass 0 (or the deployment
// block) and let the orchestrator's dedup guard collapse old events.
func NewWatcher(client EventFilterer, contractAddr common.Address, fromBlock uint64, interval time.Duration) (*Watcher, error) {
	if interval <= 0 {
		interval = DefaultWatchInterval
	}
	ev, ok := EscrowABI.Events["SettlementCreated"]
	if !ok {
		return nil, errors.New("chain.Watcher: EscrowABI missing SettlementCreated event")
	}
	return &Watcher{
		client:     client,
		contract:   contractAddr,
		interval:   interval,
		eventTopic: ev.ID,
		nextBlock:  fromBlock,
	}, nil
}

// Poll runs a single FilterLogs round and returns every new
// SettlementEvent observed since the previous Poll (or NewWatcher).
// The caller controls the cadence; Run wraps Poll in a ticker loop.
// Exposed separately so tests can drive one poll at a time
// deterministically without sleeping.
func (w *Watcher) Poll(ctx context.Context) ([]SettlementEvent, error) {
	// Guard against FromBlock > head, which the simulated backend
	// treats as an error. If no new blocks have appeared since the
	// cursor's last advance, there's nothing to poll.
	head, err := w.client.BlockNumber(ctx)
	if err != nil {
		return nil, fmt.Errorf("chain.Watcher.Poll: BlockNumber: %w", err)
	}
	if w.nextBlock > head {
		return nil, nil
	}

	q := ethereum.FilterQuery{
		FromBlock: new(big.Int).SetUint64(w.nextBlock),
		ToBlock:   new(big.Int).SetUint64(head),
		Addresses: []common.Address{w.contract},
		Topics:    [][]common.Hash{{w.eventTopic}},
	}
	logs, err := w.client.FilterLogs(ctx, q)
	if err != nil {
		return nil, fmt.Errorf("chain.Watcher.Poll: FilterLogs: %w", err)
	}
	if len(logs) == 0 {
		return nil, nil
	}
	events := make([]SettlementEvent, 0, len(logs))
	highest := w.nextBlock
	for i := range logs {
		ev, err := decodeSettlementCreated(&logs[i])
		if err != nil {
			return events, fmt.Errorf(
				"chain.Watcher.Poll: decode log at %s/%d: %w",
				logs[i].TxHash.Hex(), logs[i].Index, err)
		}
		// Pull the block timestamp once per block. Most flat-sat
		// settlements share a block; this stays O(blocks) not O(logs).
		if ev.BlockNumber >= w.nextBlock {
			hdr, err := w.client.HeaderByNumber(ctx, new(big.Int).SetUint64(ev.BlockNumber))
			if err != nil {
				return events, fmt.Errorf(
					"chain.Watcher.Poll: HeaderByNumber(%d): %w",
					ev.BlockNumber, err)
			}
			ev.BlockTimeSec = hdr.Time
		}
		events = append(events, ev)
		if ev.BlockNumber > highest {
			highest = ev.BlockNumber
		}
	}
	// Advance the cursor PAST the highest-seen block so the next Poll
	// does not re-emit the same logs. A settlement that lands in the
	// same block as a later poll would be missed if we advanced only
	// to `highest` rather than highest+1.
	w.nextBlock = highest + 1
	return events, nil
}

// Run polls in a loop until ctx is cancelled. For each batch of
// events, handler is invoked in the same goroutine — handler is
// expected to be fast (the orchestrator's builder + store are both
// sub-millisecond).
func (w *Watcher) Run(ctx context.Context, handler func(SettlementEvent)) error {
	ticker := time.NewTicker(w.interval)
	defer ticker.Stop()
	for {
		events, err := w.Poll(ctx)
		if err != nil {
			// Surface the error to the caller — persistent RPC
			// failure is the operator's problem, not ours. Tests
			// that want resilience can wrap with their own retry.
			return err
		}
		for _, ev := range events {
			handler(ev)
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			// next iteration
		}
	}
}

// NextBlock returns the cursor the watcher will resume from on the
// next Poll. Exposed for tests + operational logs.
func (w *Watcher) NextBlock() uint64 { return w.nextBlock }

// decodeSettlementCreated unpacks one go-ethereum log entry into a
// SettlementEvent using the ABI. Layout:
//   topics[0] = event signature hash (fixed)
//   topics[1] = satId    (uint32, left-padded to 32 bytes)
//   topics[2] = txNonce  (uint256, indexed)
//   topics[3] = wallet   (address, indexed)
//   data      = amount   (uint256, unindexed)
func decodeSettlementCreated(l *types.Log) (SettlementEvent, error) {
	if len(l.Topics) != 4 {
		return SettlementEvent{}, fmt.Errorf(
			"expected 4 topics on SettlementCreated, got %d", len(l.Topics))
	}

	// satId is a uint32 — indexed topics are 32 bytes left-padded; the
	// low 4 bytes carry the value.
	satTopic := l.Topics[1]
	satID := binary.BigEndian.Uint32(satTopic[common.HashLength-4:])

	// txNonce + wallet from their topic slots.
	txNonce := new(big.Int).SetBytes(l.Topics[2].Bytes())
	wallet := common.BytesToAddress(l.Topics[3].Bytes())

	// amount lives in the data field as a single uint256.
	if len(l.Data) != 32 {
		return SettlementEvent{}, fmt.Errorf(
			"expected 32 bytes of event data (amount uint256), got %d", len(l.Data))
	}
	amount := new(big.Int).SetBytes(l.Data)

	return SettlementEvent{
		SatID:       satID,
		Amount:      amount,
		TxNonce:     txNonce,
		Wallet:      wallet,
		TxHash:      l.TxHash,
		BlockNumber: l.BlockNumber,
	}, nil
}
