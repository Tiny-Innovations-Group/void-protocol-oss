package handlers_test

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/receipt"
)

// VOID-135b crash-safety.
//
// AC (Notion): "Kill the gateway mid-run (between settle and receipt
// emit), restart → gateway re-emits the pending PacketC on boot and
// does not double-settle."
//
// These tests exercise that invariant through the HTTP surface the
// bouncer will actually use. We simulate a gateway restart by closing
// the store, reopening it at the SAME file path, re-wiring the
// package-level EgressStore, and checking GET /pending.
//
// What's explicitly covered:
//   • Record appended but never ACKed survives restart as PENDING.
//   • A record that was ACKed before the restart comes back as
//     DISPATCHED — no re-offer, no double-TX.
//   • An ACK that arrives AFTER a restart still works (bouncer's
//     retry semantics are preserved across gateway reboots).
//
// What's NOT here (and is out-of-scope for #15b): a real subprocess
// test that kills the gateway binary with SIGKILL. The file-level
// guarantees are established by the atomic O_APPEND + fsync in the
// store; exec-level tests would add flakiness without adding
// confidence beyond what these three scenarios already pin.

// simulateRestart closes the current store, reopens it at the same
// path, re-wires handlers.EgressStore, and returns the new *Store.
// Mirrors what cmd/server/main.go does at boot.
func simulateRestart(t *testing.T, old *receipt.Store, path string) *receipt.Store {
	t.Helper()
	if err := old.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}
	fresh, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore after restart: %v", err)
	}
	handlers.EgressStore = fresh
	return fresh
}

// getPending fires a GET /api/v1/egress/pending through the router
// wired to the current handlers.EgressStore.
func getPending(t *testing.T) []receipt.Record {
	t.Helper()
	r := newEgressRouter()
	req := httptest.NewRequest(http.MethodGet, "/api/v1/egress/pending", nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	if w.Code != http.StatusOK {
		body, _ := io.ReadAll(w.Body)
		t.Fatalf("GET /pending: %d, body=%s", w.Code, string(body))
	}
	var out []receipt.Record
	if err := json.Unmarshal(w.Body.Bytes(), &out); err != nil {
		t.Fatalf("decode pending: %v", err)
	}
	return out
}

// postAck fires a POST /api/v1/egress/ack with the given key.
func postAck(t *testing.T, paymentID, txHash string) int {
	t.Helper()
	r := newEgressRouter()
	body, _ := json.Marshal(map[string]string{
		"payment_id":         paymentID,
		"settlement_tx_hash": txHash,
	})
	req := httptest.NewRequest(http.MethodPost, "/api/v1/egress/ack",
		bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	return w.Code
}

// TestCrashSafety_PendingSurvivesRestart — the VOID-135 headline
// acceptance criterion. Append a record (gateway builds PacketC),
// close without ACK (simulated gateway kill), reopen, GET /pending
// must still return the record so the bouncer gets another chance.
func TestCrashSafety_PendingSurvivesRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	handlers.EgressStore = s
	t.Cleanup(func() { handlers.EgressStore = nil })

	rec := sampleTestRecord("recoverable", "0xR1")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}

	// Verify pre-restart state.
	before := getPending(t)
	if len(before) != 1 || before[0].PaymentID != rec.PaymentID {
		t.Fatalf("pre-restart pending: got %+v, want the one we just appended", before)
	}

	// 💥 Simulated gateway crash mid-dispatch: kill the process, come back.
	fresh := simulateRestart(t, s, path)
	t.Cleanup(func() { _ = fresh.Close() })

	after := getPending(t)
	if len(after) != 1 {
		t.Fatalf("post-restart pending: got %d, want 1 (re-offered)", len(after))
	}
	if after[0].PaymentID != rec.PaymentID {
		t.Errorf("post-restart PaymentID: got %s, want %s",
			after[0].PaymentID, rec.PaymentID)
	}
	if after[0].DispatchStatus != receipt.StatusPending {
		t.Errorf("post-restart status: got %q, want %q",
			after[0].DispatchStatus, receipt.StatusPending)
	}
}

// TestCrashSafety_DispatchedStaysDispatchedAcrossRestart — the flip
// side. If the bouncer ACKed before the crash, the record must NOT
// re-surface as PENDING after a restart. Otherwise the bouncer would
// re-TX the same PacketC on every gateway reboot.
func TestCrashSafety_DispatchedStaysDispatchedAcrossRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	handlers.EgressStore = s
	t.Cleanup(func() { handlers.EgressStore = nil })

	rec := sampleTestRecord("settled", "0xS1")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}
	if code := postAck(t, rec.PaymentID, rec.SettlementTxHash); code != http.StatusOK {
		t.Fatalf("initial ACK: got %d, want 200", code)
	}

	// 💥 Restart.
	fresh := simulateRestart(t, s, path)
	t.Cleanup(func() { _ = fresh.Close() })

	after := getPending(t)
	if len(after) != 0 {
		t.Fatalf("post-restart pending: got %d, want 0 (ACKed before crash)", len(after))
	}
}

// TestCrashSafety_AckAfterRestartStillWorks — bouncer semantics
// across a restart. Gateway boots, bouncer polls + TXes + ACKs, the
// gateway processes the ACK normally and persists the DISPATCHED
// status so a SECOND restart doesn't re-offer the record. Proves the
// egress path is genuinely stateless across reboots.
func TestCrashSafety_AckAfterRestartStillWorks(t *testing.T) {
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	handlers.EgressStore = s
	t.Cleanup(func() { handlers.EgressStore = nil })

	rec := sampleTestRecord("late-ack", "0xL1")
	if err := s.Append(rec); err != nil {
		t.Fatalf("Append: %v", err)
	}

	// 💥 First restart — no ACK happened yet.
	s = simulateRestart(t, s, path)

	// Bouncer's poll returns the pending record …
	after1 := getPending(t)
	if len(after1) != 1 {
		t.Fatalf("post-restart #1 pending: got %d, want 1", len(after1))
	}

	// … bouncer TXes + ACKs post-restart.
	if code := postAck(t, rec.PaymentID, rec.SettlementTxHash); code != http.StatusOK {
		t.Fatalf("post-restart ACK: got %d, want 200", code)
	}

	// 💥 Second restart — the ACK must persist.
	fresh := simulateRestart(t, s, path)
	t.Cleanup(func() { _ = fresh.Close() })

	after2 := getPending(t)
	if len(after2) != 0 {
		t.Fatalf("post-restart #2 pending: got %d, want 0 (ACK must survive reboot)", len(after2))
	}
}