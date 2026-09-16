package handlers_test

// VOID-142: GET /api/v1/status — read-only operator-console status.
// Red-green: the endpoint must report the monotonic ingest counters
// (packets_in, sig verify ok/fail, intents_queued) and the receipt
// store's PENDING/DISPATCHED split exactly as the ImGui console's
// Panel 2 readout consumes them.

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/receipt"
	"github.com/gin-gonic/gin"
)

func newStatusRouter() *gin.Engine {
	r := gin.New()
	r.POST("/ingest", handlers.IngestPacket)
	r.GET("/status", handlers.HandleStatus)
	return r
}

func getStatus(t *testing.T, r *gin.Engine) (*httptest.ResponseRecorder, map[string]interface{}) {
	t.Helper()
	req := httptest.NewRequest(http.MethodGet, "/status", nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	var body map[string]interface{}
	if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil {
		t.Fatalf("status response not JSON: %v (%q)", err, w.Body.String())
	}
	return w, body
}

// swapStatusDeps nils the chain-side package deps for the duration of
// a test and restores them after. Mirrors how ingest_chain_test.go
// treats handlers.Submitter.
func swapStatusDeps(t *testing.T) {
	t.Helper()
	oldStore, oldSub, oldAddr := handlers.EgressStore, handlers.Submitter, handlers.EscrowAddress
	handlers.EgressStore, handlers.Submitter, handlers.EscrowAddress = nil, nil, ""
	t.Cleanup(func() {
		handlers.EgressStore, handlers.Submitter, handlers.EscrowAddress = oldStore, oldSub, oldAddr
	})
}

func jsonInt(t *testing.T, body map[string]interface{}, key string) int64 {
	t.Helper()
	v, ok := body[key]
	if !ok {
		t.Fatalf("status body missing key %q (body=%v)", key, body)
	}
	f, ok := v.(float64)
	if !ok {
		t.Fatalf("status key %q not a number: %T (%v)", key, v, v)
	}
	return int64(f)
}

// TestStatusNoChainDefaults: with no on-chain pipeline and no traffic,
// the endpoint serves 200 with zeroed counters and chain_enabled=false.
// The console renders this as UNCONNECTED-safe defaults.
func TestStatusNoChainDefaults(t *testing.T) {
	handlers.ResetStatusCounters()
	swapStatusDeps(t)

	w, body := getStatus(t, newStatusRouter())
	if w.Code != http.StatusOK {
		t.Fatalf("got %d, want 200. body=%s", w.Code, w.Body.String())
	}
	if body["status"] != "ok" {
		t.Errorf("status = %v, want ok", body["status"])
	}
	for _, k := range []string{
		"packets_in", "sig_verify_ok", "sig_verify_fail",
		"intents_queued", "receipts_pending", "receipts_dispatched",
	} {
		if got := jsonInt(t, body, k); got != 0 {
			t.Errorf("%s = %d, want 0", k, got)
		}
	}
	if body["chain_enabled"] != false {
		t.Errorf("chain_enabled = %v, want false", body["chain_enabled"])
	}
	if body["escrow_address"] != "" {
		t.Errorf("escrow_address = %v, want empty", body["escrow_address"])
	}
	if got := jsonInt(t, body, "uptime_sec"); got < 0 {
		t.Errorf("uptime_sec = %d, want >= 0", got)
	}
}

// TestStatusCountersReflectIngest: a golden PacketB passing verify
// bumps packets_in + sig_verify_ok; the same frame with a flipped
// signature byte bumps packets_in + sig_verify_fail and stays out of
// sig_verify_ok.
func TestStatusCountersReflectIngest(t *testing.T) {
	handlers.ResetStatusCounters()
	swapStatusDeps(t)
	registerTestPubKey(t)

	r := newStatusRouter()

	// Good frame → 200, packets_in=1, sig_verify_ok=1.
	raw := loadGoldenPacketB(t, "snlp")
	if w := postIngest(t, r, raw); w.Code != http.StatusOK {
		t.Fatalf("golden PacketB: got %d, want 200. body=%s", w.Code, w.Body.String())
	}
	_, body := getStatus(t, r)
	if got := jsonInt(t, body, "packets_in"); got != 1 {
		t.Errorf("packets_in = %d, want 1", got)
	}
	if got := jsonInt(t, body, "sig_verify_ok"); got != 1 {
		t.Errorf("sig_verify_ok = %d, want 1", got)
	}
	if got := jsonInt(t, body, "sig_verify_fail"); got != 0 {
		t.Errorf("sig_verify_fail = %d, want 0", got)
	}

	// Tampered signature → 400, packets_in=2, sig_verify_fail=1,
	// sig_verify_ok unchanged. SNLP PacketB: 14-byte header + 106-byte
	// sig scope → signature starts at raw offset 120; flipping inside
	// it keeps the frame parseable but unverifiable.
	tampered := loadGoldenPacketB(t, "snlp")
	tampered[121] ^= 0xFF
	if w := postIngest(t, r, tampered); w.Code != http.StatusBadRequest {
		t.Fatalf("tampered PacketB: got %d, want 400. body=%s", w.Code, w.Body.String())
	}
	_, body = getStatus(t, r)
	if got := jsonInt(t, body, "packets_in"); got != 2 {
		t.Errorf("packets_in = %d, want 2", got)
	}
	if got := jsonInt(t, body, "sig_verify_ok"); got != 1 {
		t.Errorf("sig_verify_ok = %d, want 1 (unchanged)", got)
	}
	if got := jsonInt(t, body, "sig_verify_fail"); got != 1 {
		t.Errorf("sig_verify_fail = %d, want 1", got)
	}
}

// TestStatusReceiptCounts: with a wired store the endpoint reports the
// latest-state split (PENDING vs DISPATCHED), not raw line counts.
func TestStatusReceiptCounts(t *testing.T) {
	handlers.ResetStatusCounters()
	swapStatusDeps(t)

	store, err := receipt.NewStore(filepath.Join(t.TempDir(), "receipts.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	t.Cleanup(func() { _ = store.Close() })
	handlers.EgressStore = store

	rec1 := receipt.Record{PaymentID: "p1", SettlementTxHash: "0xaaa", SatID: 1, Amount: "100"}
	rec2 := receipt.Record{PaymentID: "p2", SettlementTxHash: "0xbbb", SatID: 1, Amount: "200"}
	if err := store.Append(rec1); err != nil {
		t.Fatalf("Append rec1: %v", err)
	}
	if err := store.Append(rec2); err != nil {
		t.Fatalf("Append rec2: %v", err)
	}
	if err := store.MarkDispatched(rec2.PaymentID, rec2.SettlementTxHash); err != nil {
		t.Fatalf("MarkDispatched rec2: %v", err)
	}

	_, body := getStatus(t, newStatusRouter())
	if got := jsonInt(t, body, "receipts_pending"); got != 1 {
		t.Errorf("receipts_pending = %d, want 1", got)
	}
	if got := jsonInt(t, body, "receipts_dispatched"); got != 1 {
		t.Errorf("receipts_dispatched = %d, want 1", got)
	}
}
