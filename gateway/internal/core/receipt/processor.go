package receipt

import (
	"encoding/hex"
	"fmt"
	"sync"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
)

// Processor ties a chain.Watcher event stream to the Builder and Store.
// Each SettlementEvent turns into a signed PacketC + a JSONL line in
// receipts.json — with dedup on (payment_id, settlement_tx_hash) so
// gateway restarts replay the on-chain event log without emitting
// duplicate receipts.
//
// AssetID is always 1 (USDC) in the record because the on-chain
// SettlementCreated event does NOT carry the assetId (the contract
// reverts any non-USDC intent up-front via InvalidAsset). Stamping 1
// here is correct for flat-sat; a future multi-asset rev lands
// alongside a schema extension.
//
// SellerAPID defaults to 100 (apidSatA) — the seller identity the
// flat-sat demo uses; matches the deterministic golden packet_c.bin.
// Per-seller APIDs are a Phase B hardening concern.

// SellerAPID100 is the deterministic demo seller APID. Matches
// gateway/test/utils/generate_packets.go::apidSatA.
const SellerAPID100 uint16 = 100

// AssetIDUSDC is the sole asset id the Escrow contract accepts.
const AssetIDUSDC uint16 = 1

// Processor is safe for concurrent Handle calls; the seq counter and
// store append both hold an internal mutex.
type Processor struct {
	store      *Store
	sellerAPID uint16
	seed       []byte

	mu       sync.Mutex
	seqCount uint16 // 14-bit, wraps per CCSDS spec
}

// NewProcessor binds a Store + signing seed + seller APID into a
// ready-to-use event handler.
func NewProcessor(store *Store, sellerAPID uint16, seed []byte) (*Processor, error) {
	if store == nil {
		return nil, fmt.Errorf("receipt.NewProcessor: store is nil")
	}
	if len(seed) == 0 {
		return nil, fmt.Errorf("receipt.NewProcessor: seed is empty")
	}
	return &Processor{
		store:      store,
		sellerAPID: sellerAPID,
		seed:       seed,
	}, nil
}

// Handle is the chain.Watcher callback: build + sign a PacketC for
// this event, append a receipts.json line. Idempotent on the dedup
// key, so a replay after a restart returns nil without touching the
// store or the seq counter.
//
// Returns an error only on genuine builder / I/O failure — duplicates
// are NOT errors, they're the normal path on replay.
func (p *Processor) Handle(ev chain.SettlementEvent) error {
	paymentID := ev.TxNonce.String()
	txHashHex := ev.TxHash.Hex()

	if p.store.Has(paymentID, txHashHex) {
		return nil // already recorded — replay noop
	}

	p.mu.Lock()
	seq := p.seqCount
	// CCSDS sequence count is 14-bit (0..0x3FFF). Increment
	// BEFORE unlocking so concurrent Handle calls get distinct
	// values; wrap per spec.
	p.seqCount = (p.seqCount + 1) & 0x3FFF
	p.mu.Unlock()

	// Seller APID is stable across flat-sat; Ed25519 seed is stable
	// across the gateway's lifetime. Both are captured at NewProcessor
	// and used verbatim here.
	packetC, err := Build(Inputs{
		ExecTimeMs:  ev.BlockTimeSec * 1000, // block timestamp is seconds; PacketC stores ms
		EncTxID:     ev.TxNonce.Uint64(),    // low 64 bits of payment_id (= epoch_ts)
		EncStatus:   1,                      // always success; contract reverts failures
		APID:        p.sellerAPID,
		SeqCount:    seq,
		SeedEd25519: p.seed,
	})
	if err != nil {
		return fmt.Errorf("receipt.Handle: build PacketC: %w", err)
	}

	rec := Record{
		PaymentID:        paymentID,
		SettlementTxHash: txHashHex,
		SatID:            ev.SatID,
		Amount:           ev.Amount.String(),
		AssetID:          AssetIDUSDC,
		Wallet:           ev.Wallet.Hex(),
		PacketCHex:       hex.EncodeToString(packetC),
		BlockNumber:      ev.BlockNumber,
		TsMs:             int64(ev.BlockTimeSec * 1000),
	}
	if err := p.store.Append(rec); err != nil {
		// ErrDuplicate here means another goroutine won the race
		// between the Has() check and Append(). Idempotent — treat
		// as success.
		if err == ErrDuplicate {
			return nil
		}
		return fmt.Errorf("receipt.Handle: append to store: %w", err)
	}
	return nil
}
