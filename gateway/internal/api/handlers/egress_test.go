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
	"github.com/gin-gonic/gin"
)

// VOID-135b red-green — the /egress endpoints are the gateway's side of
// the LoRa-egress contract with the bouncer:
//
//   GET  /api/v1/egress/pending      → JSON array of PENDING receipts
//                                      (bouncer drains, TXes each, acks)
//   POST /api/v1/egress/ack          → flips PENDING → DISPATCHED
//
// Semantics the tests below pin:
//   • GET returns up to page-size (10) records oldest-first, ONLY those
//     currently PENDING (DISPATCHED records are invisible).
//   • GET returns a bare JSON array — not wrapped — so the C++ bouncer
//     can parse with minimal JSON code.
//   • POST with a valid (payment_id, settlement_tx_hash) that exists
//     and is PENDING → 200 + status flip.
//   • POST with valid key that's already DISPATCHED → 200 (idempotent).
//   • POST with unknown key → 404 (not a retryable error; bouncer
//     should log + drop, not retry).
//   • POST with missing / malformed JSON → 400.
//   • GET / POST when EgressStore is nil (server not fully wired) →
//     503 Service Unavailable.

func sampleTestRecord(id, tx string) receipt.Record {
	return receipt.Record{
		PaymentID:        id,
		SettlementTxHash: tx,
		SatID:            0xCAFEBABE,
		Amount:           "420000000",
		AssetID:          1,
		Wallet:           "0x70997970C51812dc3A010C7d01b50e0d17dc79C8",
		PacketCHex:       "ab",
		BlockNumber:      1,
		TsMs:             2,
	}
}

// withEgressStore wires a fresh temp-path Store into the package-level
// handlers.EgressStore slot. t.Cleanup restores nil so other tests in
// this file don't leak state through the global.
func withEgressStore(t *testing.T) *receipt.Store {
	t.Helper()
	path := filepath.Join(t.TempDir(), "receipts.json")
	s, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	handlers.EgressStore = s
	t.Cleanup(func() {
		_ = s.Close()
		handlers.EgressStore = nil
	})
	return s
}

// newEgressRouter exposes the two VOID-135b endpoints via the same
// gin.Engine pattern cmd/server/main.go uses.
func newEgressRouter() *gin.Engine {
	gin.SetMode(gin.TestMode)
	r := gin.New()
	g := r.Group("/api/v1/egress")
	g.GET("/pending", handlers.HandleEgressPending)
	g.POST("/ack", handlers.HandleEgressAck)
	return r
}

func httpGET(t *testing.T, r *gin.Engine, path string) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(http.MethodGet, path, nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	return w
}

func httpPOST(t *testing.T, r *gin.Engine, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	var raw []byte
	switch v := body.(type) {
	case string:
		raw = []byte(v)
	case []byte:
		raw = v
	default:
		b, err := json.Marshal(v)
		if err != nil {
			t.Fatalf("marshal body: %v", err)
		}
		raw = b
	}
	req := httptest.NewRequest(http.MethodPost, path, bytes.NewReader(raw))
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	return w
}

// ---- GET /pending ---------------------------------------------------------

func TestEgress_PendingReturnsEmptyOnFreshStore(t *testing.T) {
	withEgressStore(t)
	r := newEgressRouter()

	w := httpGET(t, r, "/api/v1/egress/pending")
	if w.Code != http.StatusOK {
		t.Fatalf("status: got %d, want 200", w.Code)
	}
	body, _ := io.ReadAll(w.Body)
	if string(bytes.TrimSpace(body)) != "[]" {
		t.Fatalf("empty response: got %q, want []", string(body))
	}
}

func TestEgress_PendingReturnsQueuedRecords(t *testing.T) {
	store := withEgressStore(t)
	for _, id := range []string{"alpha", "bravo", "charlie"} {
		if err := store.Append(sampleTestRecord(id, "0x"+id)); err != nil {
			t.Fatalf("Append %s: %v", id, err)
		}
	}
	r := newEgressRouter()
	w := httpGET(t, r, "/api/v1/egress/pending")

	if w.Code != http.StatusOK {
		t.Fatalf("status: got %d, want 200", w.Code)
	}
	var got []receipt.Record
	if err := json.Unmarshal(w.Body.Bytes(), &got); err != nil {
		t.Fatalf("decode JSON: %v", err)
	}
	if len(got) != 3 {
		t.Fatalf("len: got %d, want 3", len(got))
	}
	wantOrder := []string{"alpha", "bravo", "charlie"}
	for i, w := range wantOrder {
		if got[i].PaymentID != w {
			t.Errorf("order[%d]: got %s, want %s", i, got[i].PaymentID, w)
		}
		if got[i].DispatchStatus != receipt.StatusPending {
			t.Errorf("status[%d]: got %q, want %q",
				i, got[i].DispatchStatus, receipt.StatusPending)
		}
	}
}

func TestEgress_PendingSkipsDispatched(t *testing.T) {
	store := withEgressStore(t)
	_ = store.Append(sampleTestRecord("a", "0xA"))
	_ = store.Append(sampleTestRecord("b", "0xB"))
	if err := store.MarkDispatched("a", "0xA"); err != nil {
		t.Fatalf("MarkDispatched: %v", err)
	}

	r := newEgressRouter()
	w := httpGET(t, r, "/api/v1/egress/pending")
	var got []receipt.Record
	_ = json.Unmarshal(w.Body.Bytes(), &got)

	if len(got) != 1 {
		t.Fatalf("len: got %d, want 1", len(got))
	}
	if got[0].PaymentID != "b" {
		t.Errorf("only PENDING should surface; got %s, want b", got[0].PaymentID)
	}
}

func TestEgress_PendingCapsAtPageSize(t *testing.T) {
	store := withEgressStore(t)
	// Seed 15 pending; expect GET to return the first 10 (page size).
	for i := 0; i < 15; i++ {
		_ = store.Append(sampleTestRecord(
			string(rune('A'+i)),
			"0x"+string(rune('A'+i)),
		))
	}
	r := newEgressRouter()
	w := httpGET(t, r, "/api/v1/egress/pending")

	var got []receipt.Record
	_ = json.Unmarshal(w.Body.Bytes(), &got)
	if len(got) != 10 {
		t.Fatalf("len: got %d, want 10 (page size cap)", len(got))
	}
}

func TestEgress_NoStoreConfiguredReturns503(t *testing.T) {
	handlers.EgressStore = nil
	r := newEgressRouter()
	w := httpGET(t, r, "/api/v1/egress/pending")
	if w.Code != http.StatusServiceUnavailable {
		t.Fatalf("status: got %d, want 503", w.Code)
	}
}

// ---- POST /ack ------------------------------------------------------------

func TestEgress_AckFlipsPendingToDispatched(t *testing.T) {
	store := withEgressStore(t)
	_ = store.Append(sampleTestRecord("x", "0xX"))

	r := newEgressRouter()
	w := httpPOST(t, r, "/api/v1/egress/ack", map[string]string{
		"payment_id":         "x",
		"settlement_tx_hash": "0xX",
	})
	if w.Code != http.StatusOK {
		body, _ := io.ReadAll(w.Body)
		t.Fatalf("ack status: got %d, want 200. body=%s", w.Code, string(body))
	}
	// After ACK, pending list is empty.
	if n := len(store.PendingRecords(10)); n != 0 {
		t.Errorf("PendingRecords after ACK: got %d, want 0", n)
	}
}

func TestEgress_AckIdempotent(t *testing.T) {
	store := withEgressStore(t)
	_ = store.Append(sampleTestRecord("y", "0xY"))

	r := newEgressRouter()
	body := map[string]string{"payment_id": "y", "settlement_tx_hash": "0xY"}

	w1 := httpPOST(t, r, "/api/v1/egress/ack", body)
	if w1.Code != http.StatusOK {
		t.Fatalf("first ack: got %d, want 200", w1.Code)
	}
	// Second call (simulating bouncer ACK retry after a lost response).
	w2 := httpPOST(t, r, "/api/v1/egress/ack", body)
	if w2.Code != http.StatusOK {
		b, _ := io.ReadAll(w2.Body)
		t.Fatalf("repeat ack: got %d, want 200. body=%s", w2.Code, string(b))
	}
}

func TestEgress_AckUnknownKeyReturns404(t *testing.T) {
	withEgressStore(t)
	r := newEgressRouter()
	w := httpPOST(t, r, "/api/v1/egress/ack", map[string]string{
		"payment_id":         "nope",
		"settlement_tx_hash": "0xzero",
	})
	if w.Code != http.StatusNotFound {
		b, _ := io.ReadAll(w.Body)
		t.Fatalf("status: got %d, want 404. body=%s", w.Code, string(b))
	}
}

func TestEgress_AckMalformedJSONReturns400(t *testing.T) {
	withEgressStore(t)
	r := newEgressRouter()
	w := httpPOST(t, r, "/api/v1/egress/ack", "{not valid json")
	if w.Code != http.StatusBadRequest {
		t.Fatalf("status: got %d, want 400", w.Code)
	}
}

func TestEgress_AckMissingFieldsReturns400(t *testing.T) {
	withEgressStore(t)
	r := newEgressRouter()
	w := httpPOST(t, r, "/api/v1/egress/ack", map[string]string{
		"payment_id": "only-id-no-tx",
	})
	if w.Code != http.StatusBadRequest {
		t.Fatalf("status: got %d, want 400", w.Code)
	}
}