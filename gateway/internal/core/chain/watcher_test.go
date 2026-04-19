package chain

import (
	"context"
	"math/big"
	"testing"
	"time"

	"github.com/ethereum/go-ethereum/common"
)

// VOID-135a watcher tests — reuse the spinUpEscrow harness from
// client_test.go so the watcher runs against a real simulated EVM
// backend with the same Escrow bytecode we ship.
//
// Together with client_test.go:TestEscrowClient_SettleBatch_HappyPath
// these form the "gateway can round-trip an on-chain settlement"
// contract: settle → watch → decoded event.

// TestWatcher_PollReturnsSettlementEvent asserts the happy path — one
// settleBatch, one matching SettlementEvent with all fields decoded.
func TestWatcher_PollReturnsSettlementEvent(t *testing.T) {
	sim, client, _ := spinUpEscrow(t)

	w, err := NewWatcher(sim.Client(), client.Address(), 0, 0)
	if err != nil {
		t.Fatalf("NewWatcher: %v", err)
	}

	wallet := common.HexToAddress(anvilAcct1Addr)
	nonce := DeriveTxNonce(0xCAFEBABE, 1710000100000)
	intent := SettlementIntent{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(420_000_000),
		AssetId: 1,
		TxNonce: nonce,
		Wallet:  wallet,
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	txHash, err := client.SettleBatch(ctx, []SettlementIntent{intent})
	if err != nil {
		t.Fatalf("SettleBatch: %v", err)
	}
	sim.Commit()

	events, err := w.Poll(ctx)
	if err != nil {
		t.Fatalf("Poll: %v", err)
	}
	if len(events) != 1 {
		t.Fatalf("expected 1 event, got %d", len(events))
	}
	ev := events[0]
	if ev.SatID != 0xCAFEBABE {
		t.Errorf("SatID: got 0x%X, want 0xCAFEBABE", ev.SatID)
	}
	if ev.Amount.Uint64() != 420_000_000 {
		t.Errorf("Amount: got %s, want 420000000", ev.Amount.String())
	}
	if ev.TxNonce.Cmp(nonce) != 0 {
		t.Errorf("TxNonce: got %s, want %s", ev.TxNonce, nonce)
	}
	if ev.Wallet != wallet {
		t.Errorf("Wallet: got %s, want %s", ev.Wallet.Hex(), wallet.Hex())
	}
	if ev.TxHash != txHash {
		t.Errorf("TxHash: got %s, want %s", ev.TxHash.Hex(), txHash.Hex())
	}
	if ev.BlockNumber == 0 {
		t.Errorf("BlockNumber: got 0, expected non-zero after Commit")
	}
	if ev.BlockTimeSec == 0 {
		t.Errorf("BlockTimeSec: got 0, expected populated from header")
	}

	// A second Poll immediately after, without new settlements, must
	// return zero events — the cursor advanced past the block.
	events2, err := w.Poll(ctx)
	if err != nil {
		t.Fatalf("Poll #2: %v", err)
	}
	if len(events2) != 0 {
		t.Fatalf("expected 0 events on idle re-poll, got %d", len(events2))
	}
}

// TestWatcher_CursorAdvancesPastHighestBlock — after observing an
// event, the internal cursor must move past the event's block so a
// re-poll does not double-emit.
func TestWatcher_CursorAdvancesPastHighestBlock(t *testing.T) {
	sim, client, _ := spinUpEscrow(t)
	w, err := NewWatcher(sim.Client(), client.Address(), 0, 0)
	if err != nil {
		t.Fatalf("NewWatcher: %v", err)
	}

	if got := w.NextBlock(); got != 0 {
		t.Fatalf("fresh watcher NextBlock=%d, want 0", got)
	}

	ctx := context.Background()
	_, err = client.SettleBatch(ctx, []SettlementIntent{{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(1),
		AssetId: 1,
		TxNonce: DeriveTxNonce(0xCAFEBABE, 1000),
		Wallet:  common.HexToAddress(anvilAcct1Addr),
	}})
	if err != nil {
		t.Fatalf("SettleBatch: %v", err)
	}
	sim.Commit()

	events, err := w.Poll(ctx)
	if err != nil {
		t.Fatalf("Poll: %v", err)
	}
	if len(events) != 1 {
		t.Fatalf("expected 1 event, got %d", len(events))
	}
	if got := w.NextBlock(); got != events[0].BlockNumber+1 {
		t.Errorf("NextBlock: got %d, want %d (block+1)",
			got, events[0].BlockNumber+1)
	}
}

// TestWatcher_MultipleBatchesEachEmitOneEvent — two distinct
// settleBatch calls produce two events on a subsequent Poll (both
// observed, neither missed or duplicated).
func TestWatcher_MultipleBatchesEachEmitOneEvent(t *testing.T) {
	sim, client, _ := spinUpEscrow(t)
	w, err := NewWatcher(sim.Client(), client.Address(), 0, 0)
	if err != nil {
		t.Fatalf("NewWatcher: %v", err)
	}

	ctx := context.Background()
	for i := 0; i < 3; i++ {
		_, err := client.SettleBatch(ctx, []SettlementIntent{{
			SatId:   0xCAFEBABE,
			Amount:  big.NewInt(int64(100 + i)),
			AssetId: 1,
			TxNonce: DeriveTxNonce(0xCAFEBABE, uint64(2000+i)),
			Wallet:  common.HexToAddress(anvilAcct1Addr),
		}})
		if err != nil {
			t.Fatalf("SettleBatch #%d: %v", i, err)
		}
		sim.Commit()
	}

	events, err := w.Poll(ctx)
	if err != nil {
		t.Fatalf("Poll: %v", err)
	}
	if len(events) != 3 {
		t.Fatalf("expected 3 events, got %d", len(events))
	}
}
