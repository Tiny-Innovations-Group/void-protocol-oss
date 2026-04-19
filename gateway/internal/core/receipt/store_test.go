package receipt

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// VOID-135a store red-green.
// The JSONL file at gateway/data/receipts.json is the TRL-4 audit
// artefact. Every committed line proves "the gateway observed
// settlement tx_hash H for payment_id P and emitted PacketC C". The
// store is responsible for:
//
//   - atomic append (O_APPEND + fsync) so a crash between write and
//     acknowledge never leaves a torn record;
//   - on-boot reload into an in-memory dedup set, so a gateway restart
//     cannot double-settle the same (payment_id, tx_hash) pair;
//   - Has() cheap read path so the orchestrator can gate before the
//     builder runs.

func sampleRecord(paymentID, txHash string) Record {
	return Record{
		PaymentID:        paymentID,
		SettlementTxHash: txHash,
		SatID:            0xCAFEBABE,
		Amount:           "420000000",
		AssetID:          1,
		Wallet:           "0x70997970C51812dc3A010C7d01b50e0d17dc79C8",
		PacketCHex:       strings.Repeat("ab", 112),
		BlockNumber:      42,
		TsMs:             1710000100000,
	}
}

// TestStore_AppendPersistsAndDedups — Append writes a JSONL line, sets
// Has() to true, Count() goes up by one, and a second Append with the
// same (payment_id, tx_hash) returns ErrDuplicate.
func TestStore_AppendPersistsAndDedups(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	rec := sampleRecord("12345", "0xdead")
	if s.Has(rec.PaymentID, rec.SettlementTxHash) {
		t.Fatalf("Has true on empty store")
	}
	if got := s.Count(); got != 0 {
		t.Fatalf("Count=%d on empty store", got)
	}

	if err := s.Append(rec); err != nil {
		t.Fatalf("Append first: %v", err)
	}
	if !s.Has(rec.PaymentID, rec.SettlementTxHash) {
		t.Fatalf("Has false after first append")
	}
	if got := s.Count(); got != 1 {
		t.Fatalf("Count=%d after one append", got)
	}

	if err := s.Append(rec); err != ErrDuplicate {
		t.Fatalf("second Append: got %v, want ErrDuplicate", err)
	}
	// Count must NOT have advanced on the rejected duplicate.
	if got := s.Count(); got != 1 {
		t.Fatalf("Count=%d after duplicate attempt", got)
	}
}

// TestStore_ReloadBuildsDedupSet — a fresh Store pointed at a pre-
// existing file must reconstruct the dedup set from disk. This is
// the VOID-135 "survives gateway restart" primitive.
func TestStore_ReloadBuildsDedupSet(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	first, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #1: %v", err)
	}
	recs := []Record{
		sampleRecord("11", "0x1111"),
		sampleRecord("22", "0x2222"),
		sampleRecord("33", "0x3333"),
	}
	for _, r := range recs {
		if err := first.Append(r); err != nil {
			t.Fatalf("Append %s: %v", r.PaymentID, err)
		}
	}
	if err := first.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	second, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #2 (reload): %v", err)
	}
	defer second.Close()

	if got := second.Count(); got != len(recs) {
		t.Fatalf("reloaded Count=%d, want %d", got, len(recs))
	}
	for _, r := range recs {
		if !second.Has(r.PaymentID, r.SettlementTxHash) {
			t.Errorf("reload missed record: payment_id=%s tx=%s", r.PaymentID, r.SettlementTxHash)
		}
	}
	// And a write through the reopened store still rejects duplicates.
	if err := second.Append(recs[0]); err != ErrDuplicate {
		t.Fatalf("reloaded store: duplicate accepted, got %v", err)
	}
}

// TestStore_NewStoreFailsOnUnreadableDir — creating a store under a
// path that cannot be opened returns an error (don't silently swallow
// the config issue).
func TestStore_NewStoreFailsOnUnreadableDir(t *testing.T) {
	path := filepath.Join(t.TempDir(), "no-such-dir", "receipts.json")
	if _, err := NewStore(path); err == nil {
		t.Fatalf("expected error opening file under non-existent dir, got nil")
	}
}

// TestStore_JSONLFormatIsOneLinePerRecord — the on-disk artefact MUST
// be JSON Lines (one JSON object per line, no commas, no array
// wrapper) so downstream tooling (jq, grep, tail -f) works without
// extra parsing. Regression guard against someone swapping the
// encoder to a JSON array.
func TestStore_JSONLFormatIsOneLinePerRecord(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	if err := s.Append(sampleRecord("1", "0xa")); err != nil {
		t.Fatalf("Append: %v", err)
	}
	if err := s.Append(sampleRecord("2", "0xb")); err != nil {
		t.Fatalf("Append: %v", err)
	}
	if err := s.Append(sampleRecord("3", "0xc")); err != nil {
		t.Fatalf("Append: %v", err)
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	lines := strings.Split(strings.TrimRight(string(raw), "\n"), "\n")
	if len(lines) != 3 {
		t.Fatalf("expected 3 lines, got %d. raw=%q", len(lines), string(raw))
	}
	for i, l := range lines {
		if !strings.HasPrefix(l, "{") || !strings.HasSuffix(l, "}") {
			t.Errorf("line %d not a JSON object: %q", i, l)
		}
	}
}
