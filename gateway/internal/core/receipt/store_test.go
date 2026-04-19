package receipt

import (
	"fmt"
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

// ---------------------------------------------------------------------------
// VOID-135b — PENDING/DISPATCHED state machine for the egress path.
// ---------------------------------------------------------------------------
//
// The LoRa egress to the bouncer is an at-least-once channel: the
// bouncer polls the gateway for pending receipts, TXes them, then ACKs.
// If the gateway crashes between building the PacketC and the bouncer's
// ACK, the record stays PENDING and is re-offered after restart. If the
// bouncer retries ACKing (first response lost), the gateway treats the
// second ACK idempotently.
//
// Semantics the tests below pin:
//   • Append defaults DispatchStatus to "PENDING".
//   • MarkDispatched flips "PENDING" → "DISPATCHED" via an append-only
//     update line (NOT an in-place rewrite; JSONL stays append-safe).
//   • MarkDispatched on an already-DISPATCHED record is idempotent (nil).
//   • MarkDispatched on an unknown key returns ErrUnknown.
//   • PendingRecords(limit) returns up to `limit` records in PENDING
//     state, in append order (oldest first — fairness).
//   • Store reload takes the LATEST status line per dedup key.
//   • Legacy records without dispatch_status (#15a era) default to
//     PENDING on reload so they get re-offered after the upgrade.

func TestStore_AppendDefaultsToPending(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	// Leave DispatchStatus blank on purpose — the store MUST default it.
	rec := sampleRecord("10", "0xabc")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}

	pending := s.PendingRecords(10)
	if len(pending) != 1 {
		t.Fatalf("PendingRecords: got %d, want 1", len(pending))
	}
	if pending[0].DispatchStatus != StatusPending {
		t.Fatalf("DispatchStatus: got %q, want %q",
			pending[0].DispatchStatus, StatusPending)
	}
}

func TestStore_MarkDispatchedFlipsStatus(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	rec := sampleRecord("20", "0xdef")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}
	if err := s.MarkDispatched(rec.PaymentID, rec.SettlementTxHash); err != nil {
		t.Fatalf("MarkDispatched: %v", err)
	}

	pending := s.PendingRecords(10)
	if len(pending) != 0 {
		t.Fatalf("PendingRecords after MarkDispatched: got %d, want 0", len(pending))
	}
	// But the record still EXISTS — Has must still be true (so a replay
	// ACK doesn't get treated as an unknown record).
	if !s.Has(rec.PaymentID, rec.SettlementTxHash) {
		t.Errorf("Has=false after MarkDispatched — record must persist")
	}
	if got := s.Count(); got != 1 {
		t.Errorf("Count=%d, want 1 (MarkDispatched preserves the record)", got)
	}
}

func TestStore_MarkDispatchedIdempotent(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	rec := sampleRecord("30", "0xfed")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}
	if err := s.MarkDispatched(rec.PaymentID, rec.SettlementTxHash); err != nil {
		t.Fatalf("MarkDispatched #1: %v", err)
	}
	// Second call MUST NOT error — the bouncer might retry its ACK if
	// its first response was lost.
	if err := s.MarkDispatched(rec.PaymentID, rec.SettlementTxHash); err != nil {
		t.Fatalf("MarkDispatched #2 (idempotent): got %v, want nil", err)
	}
}

func TestStore_MarkDispatchedUnknownReturnsErrUnknown(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	if err := s.MarkDispatched("no-such", "0xzero"); err != ErrUnknown {
		t.Fatalf("MarkDispatched(unknown): got %v, want ErrUnknown", err)
	}
}

func TestStore_PendingRecordsInOrderAndCapped(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	// Append 5, dispatch the 2nd, then query.
	for i := 1; i <= 5; i++ {
		if err := s.Append(sampleRecord(
			fmt.Sprintf("%d", i),
			fmt.Sprintf("0x%02x", i),
		)); err != nil {
			t.Fatalf("Append %d: %v", i, err)
		}
	}
	if err := s.MarkDispatched("2", "0x02"); err != nil {
		t.Fatalf("MarkDispatched: %v", err)
	}

	// PendingRecords(10) — should return 4 (all except #2), oldest first.
	all := s.PendingRecords(10)
	if len(all) != 4 {
		t.Fatalf("PendingRecords(10): got %d, want 4", len(all))
	}
	wantIDs := []string{"1", "3", "4", "5"}
	for i, want := range wantIDs {
		if all[i].PaymentID != want {
			t.Errorf("order[%d]: got %s, want %s", i, all[i].PaymentID, want)
		}
	}

	// PendingRecords(2) — should cap at 2 and return the oldest two.
	capped := s.PendingRecords(2)
	if len(capped) != 2 {
		t.Fatalf("PendingRecords(2): got %d, want 2", len(capped))
	}
	if capped[0].PaymentID != "1" || capped[1].PaymentID != "3" {
		t.Errorf("cap order: got %s,%s; want 1,3",
			capped[0].PaymentID, capped[1].PaymentID)
	}
}

func TestStore_ReloadTakesLatestStatus(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")

	// --- first session ---
	s1, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #1: %v", err)
	}
	a := sampleRecord("A", "0xA")
	b := sampleRecord("B", "0xB")
	if err := s1.Append(a); err != nil {
		t.Fatalf("Append a: %v", err)
	}
	if err := s1.Append(b); err != nil {
		t.Fatalf("Append b: %v", err)
	}
	// Dispatch only A. B stays PENDING.
	if err := s1.MarkDispatched(a.PaymentID, a.SettlementTxHash); err != nil {
		t.Fatalf("MarkDispatched a: %v", err)
	}
	if err := s1.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	// --- reload ---
	s2, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore #2: %v", err)
	}
	defer s2.Close()

	pending := s2.PendingRecords(10)
	if len(pending) != 1 {
		t.Fatalf("reloaded Pending: got %d, want 1 (only B)", len(pending))
	}
	if pending[0].PaymentID != b.PaymentID {
		t.Errorf("pending[0]: got %s, want %s", pending[0].PaymentID, b.PaymentID)
	}
	// Count still 2 — both records exist; A is just no longer pending.
	if got := s2.Count(); got != 2 {
		t.Errorf("Count: got %d, want 2", got)
	}
}

func TestStore_LegacyRecordWithoutStatusDefaultsPending(t *testing.T) {
	// A #15a-era receipts.json line has no "dispatch_status" field. On
	// reload it MUST be treated as PENDING so the egress path re-offers
	// it; otherwise a gateway upgrade would silently drop any
	// un-dispatched receipt from before this state machine landed.
	path := filepath.Join(t.TempDir(), "receipts.json")
	legacy := `{"payment_id":"LEGACY","settlement_tx_hash":"0x999","sat_id":3405691582,"amount":"1","asset_id":1,"wallet":"0x70997970C51812dc3A010C7d01b50e0d17dc79C8","packet_c_hex":"aa","block_number":1,"ts_ms":1}` + "\n"
	if err := os.WriteFile(path, []byte(legacy), 0o644); err != nil {
		t.Fatalf("seed legacy line: %v", err)
	}

	s, err := NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer s.Close()

	pending := s.PendingRecords(10)
	if len(pending) != 1 {
		t.Fatalf("PendingRecords: got %d, want 1 (legacy defaults to PENDING)", len(pending))
	}
	if pending[0].DispatchStatus != StatusPending {
		t.Errorf("DispatchStatus: got %q, want %q (reload-default)",
			pending[0].DispatchStatus, StatusPending)
	}
}
