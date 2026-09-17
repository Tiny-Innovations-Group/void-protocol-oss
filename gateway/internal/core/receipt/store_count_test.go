package receipt

// VOID-142: Store.CountByStatus red-green. The status endpoint's
// receipts_pending / receipts_dispatched fields must reflect latest-
// state-per-dedup-key, never raw line counts (the file is append-only;
// a status flip appends a second line for the same key).

import (
	"path/filepath"
	"testing"
)

func TestCountByStatusEmptyStore(t *testing.T) {
	s, err := NewStore(filepath.Join(t.TempDir(), "receipts.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer func() { _ = s.Close() }()

	pending, dispatched := s.CountByStatus()
	if pending != 0 || dispatched != 0 {
		t.Errorf("got (%d, %d), want (0, 0)", pending, dispatched)
	}
}

func TestCountByStatusSplit(t *testing.T) {
	s, err := NewStore(filepath.Join(t.TempDir(), "receipts.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer func() { _ = s.Close() }()

	recs := []Record{
		{PaymentID: "p1", SettlementTxHash: "0xa1", SatID: 1, Amount: "100"},
		{PaymentID: "p2", SettlementTxHash: "0xb2", SatID: 1, Amount: "200"},
		{PaymentID: "p3", SettlementTxHash: "0xc3", SatID: 1, Amount: "300"},
	}
	for _, rec := range recs {
		if err := s.Append(rec); err != nil {
			t.Fatalf("Append %s: %v", rec.PaymentID, err)
		}
	}
	if err := s.MarkDispatched("p1", "0xa1"); err != nil {
		t.Fatalf("MarkDispatched p1: %v", err)
	}

	pending, dispatched := s.CountByStatus()
	if pending != 2 || dispatched != 1 {
		t.Errorf("got (%d, %d), want (2, 1)", pending, dispatched)
	}
}

// TestCountByStatusLegacyLines: records persisted without a
// dispatch_status field reload as PENDING and must count as such.
func TestCountByStatusLegacyLines(t *testing.T) {
	s, err := NewStore(filepath.Join(t.TempDir(), "receipts.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	defer func() { _ = s.Close() }()

	legacy := Record{PaymentID: "p9", SettlementTxHash: "0xd4", SatID: 1, Amount: "900"}
	if err := s.Append(legacy); err != nil {
		t.Fatalf("Append legacy: %v", err)
	}
	// Blank the in-memory status to simulate a pre-VOID-135b line
	// (loadRecords performs the same normalisation on boot).
	s.records[dedupKey(legacy.PaymentID, legacy.SettlementTxHash)] = legacy

	pending, dispatched := s.CountByStatus()
	if pending != 1 || dispatched != 0 {
		t.Errorf("got (%d, %d), want (1, 0)", pending, dispatched)
	}
}
