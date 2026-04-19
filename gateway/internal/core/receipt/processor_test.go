package receipt

import (
	"encoding/hex"
	"math/big"
	"path/filepath"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/ethereum/go-ethereum/common"
)

func sampleEvent(txNonce uint64, blockTimeSec uint64) chain.SettlementEvent {
	return chain.SettlementEvent{
		SatID:        0xCAFEBABE,
		Amount:       big.NewInt(420_000_000),
		TxNonce:      big.NewInt(0).SetUint64(txNonce),
		Wallet:       common.HexToAddress("0x70997970C51812dc3A010C7d01b50e0d17dc79C8"),
		TxHash:       common.HexToHash("0xdeadbeef"),
		BlockNumber:  42,
		BlockTimeSec: blockTimeSec,
	}
}

func newTestProcessor(t *testing.T) (*Processor, *Store, string) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "receipts.json")
	store, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	t.Cleanup(func() { _ = store.Close() })
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		t.Fatalf("decode seed: %v", err)
	}
	p, err := NewProcessor(store, SellerAPID100, seed)
	if err != nil {
		t.Fatalf("NewProcessor: %v", err)
	}
	return p, store, path
}

// TestProcessor_HandlePersistsReceipt — a fresh event produces a
// receipts.json line with a valid signed PacketC and the event's full
// provenance.
func TestProcessor_HandlePersistsReceipt(t *testing.T) {
	p, store, _ := newTestProcessor(t)

	ev := sampleEvent(1710000100000, 1710000000)
	if err := p.Handle(ev); err != nil {
		t.Fatalf("Handle: %v", err)
	}
	if got := store.Count(); got != 1 {
		t.Fatalf("Count: got %d, want 1", got)
	}
	if !store.Has(ev.TxNonce.String(), ev.TxHash.Hex()) {
		t.Errorf("Has false after Handle")
	}
}

// TestProcessor_DuplicateEventIsIdempotent — replaying the same event
// (same payment_id + tx_hash) must not produce a second line and must
// not return an error. Simulates a gateway restart that re-scans the
// chain and sees the historical event.
func TestProcessor_DuplicateEventIsIdempotent(t *testing.T) {
	p, store, _ := newTestProcessor(t)

	ev := sampleEvent(1710000100000, 1710000000)

	if err := p.Handle(ev); err != nil {
		t.Fatalf("Handle #1: %v", err)
	}
	if err := p.Handle(ev); err != nil {
		t.Fatalf("Handle #2 (duplicate): got %v, want nil", err)
	}
	if got := store.Count(); got != 1 {
		t.Fatalf("Count: got %d, want 1 (duplicate should not append)", got)
	}
}

// TestProcessor_DistinctEventsAllPersist — n different events produce
// n distinct receipts. Covers the realistic flow: multiple settlements
// arrive over time, all need auditing.
func TestProcessor_DistinctEventsAllPersist(t *testing.T) {
	p, store, _ := newTestProcessor(t)

	for i := 0; i < 5; i++ {
		ev := sampleEvent(uint64(1710000100000+i), uint64(1710000000+i))
		ev.TxHash = common.BytesToHash([]byte{byte(i + 1)})
		if err := p.Handle(ev); err != nil {
			t.Fatalf("Handle #%d: %v", i, err)
		}
	}
	if got := store.Count(); got != 5 {
		t.Fatalf("Count: got %d, want 5", got)
	}
}

// TestProcessor_RestartReplaysWithoutDuplicates — simulate the full
// restart scenario: Handle once, Close, open a NEW Processor on the
// SAME path, Handle the SAME event. The second Handle must observe the
// existing row and noop. This is the "gateway restart → no double-
// settle" contract from the VOID-135 acceptance spec.
func TestProcessor_RestartReplaysWithoutDuplicates(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		t.Fatalf("decode seed: %v", err)
	}

	// --- first run ---
	store1, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #1: %v", err)
	}
	p1, err := NewProcessor(store1, SellerAPID100, seed)
	if err != nil {
		t.Fatalf("NewProcessor #1: %v", err)
	}
	ev := sampleEvent(1710000100000, 1710000000)
	if err := p1.Handle(ev); err != nil {
		t.Fatalf("Handle: %v", err)
	}
	if err := store1.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	// --- restart: reopen the SAME path, same event flows through ---
	store2, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #2: %v", err)
	}
	t.Cleanup(func() { _ = store2.Close() })
	p2, err := NewProcessor(store2, SellerAPID100, seed)
	if err != nil {
		t.Fatalf("NewProcessor #2: %v", err)
	}
	if got := store2.Count(); got != 1 {
		t.Fatalf("reloaded Count: got %d, want 1", got)
	}
	if err := p2.Handle(ev); err != nil {
		t.Fatalf("Handle (replay): %v", err)
	}
	if got := store2.Count(); got != 1 {
		t.Fatalf("Count after replay: got %d, want 1 (should NOT double-settle)", got)
	}
}
