package handlers_test

import (
	"bytes"
	"crypto/ed25519"
	"encoding/hex"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"
	"github.com/gin-gonic/gin"
)

// VOID-112: the ingest path must reject truncated or unsupported
// Packet B buffers with HTTP 400 and must only accept a full, correctly
// signed 184-byte Packet B with HTTP 200. These tests exercise the
// handler end-to-end via httptest — no mocking of the parser, the
// verifier, or the registry.
//
// The registry is mutated at test setup to register the deterministic
// test public key under sat_id 0xCAFEBABE so the committed golden
// Packet B vector verifies green. This is a test-only shim; production
// code never touches 0xCAFEBABE.
const (
	packetBBodyLen = 178
	detTestSatID   = uint32(0xCAFEBABE)
	detSeedHex     = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
)

func init() {
	gin.SetMode(gin.TestMode)
}

// repoVectorsDir walks up from this test file to the repo root, then
// into test/vectors/. ingest_test.go lives at
// gateway/internal/api/handlers/, so we climb four levels.
func repoVectorsDir(t *testing.T) string {
	t.Helper()
	_, thisFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatalf("runtime.Caller failed")
	}
	return filepath.Join(filepath.Dir(thisFile), "..", "..", "..", "..", "test", "vectors")
}

// registerTestPubKey pokes the deterministic test pubkey into the mock
// registry under sat_id 0xCAFEBABE so VerifyPacketSignature accepts
// signatures produced by the VOID-123 test seed. Idempotent — safe to
// call from multiple test functions.
func registerTestPubKey(t *testing.T) {
	t.Helper()
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		t.Fatalf("decode det seed: %v", err)
	}
	priv := ed25519.NewKeyFromSeed(seed)
	pub := priv.Public().(ed25519.PublicKey)
	registry.MockDB[detTestSatID] = registry.SatRecord{
		SatID:     detTestSatID,
		PubKeyHex: hex.EncodeToString(pub),
		Wallet:    "test-only",
		Role:      "Test",
	}
}

func newIngestRouter() *gin.Engine {
	r := gin.New()
	r.POST("/ingest", handlers.IngestPacket)
	return r
}

func postIngest(t *testing.T, r *gin.Engine, payload []byte) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest(http.MethodPost, "/ingest", bytes.NewReader(payload))
	req.Header.Set("Content-Type", "application/octet-stream")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)
	return w
}

func loadGoldenPacketB(t *testing.T, tier string) []byte {
	t.Helper()
	path := filepath.Join(repoVectorsDir(t), tier, "packet_b.bin")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	return data
}

// ---- Tests ----

// TestIngestRejects50ByteBuffer — VOID-112: a 50-byte buffer is far
// too short to contain a Packet B body (178B) and must be rejected
// with HTTP 400. The handler must NOT proceed to any struct cast or
// signature verification path.
func TestIngestRejects50ByteBuffer(t *testing.T) {
	registerTestPubKey(t)
	r := newIngestRouter()

	w := postIngest(t, r, make([]byte, 50))

	if w.Code != http.StatusBadRequest {
		body, _ := io.ReadAll(w.Body)
		t.Errorf("50-byte buffer: got %d, want %d. body=%s", w.Code, http.StatusBadRequest, string(body))
	}
}

// TestIngestAcceptsValidPacketB — VOID-112: the full 184-byte CCSDS
// golden Packet B with a valid signature (via the test registry) must
// be accepted with HTTP 200. This is the positive-path lock: if ever
// this test starts failing on a PR that did not intend to change the
// wire format or verifier, something has drifted.
func TestIngestAcceptsValidPacketB(t *testing.T) {
	registerTestPubKey(t)
	r := newIngestRouter()

	raw := loadGoldenPacketB(t, "ccsds")
	if len(raw) != 184 {
		t.Fatalf("expected 184-byte CCSDS packet_b, got %d", len(raw))
	}

	w := postIngest(t, r, raw)

	if w.Code != http.StatusOK {
		body, _ := io.ReadAll(w.Body)
		t.Errorf("valid 184B Packet B: got %d, want 200. body=%s", w.Code, string(body))
	}
}

// TestIngestRejects177ByteShortBuffer — VOID-112 off-by-one guard.
// 177 bytes is exactly one byte shy of the Packet B body length and
// must be rejected with HTTP 400. Asserts the boundary condition of
// the bounds check at packetBBodyLen = 178.
func TestIngestRejects177ByteShortBuffer(t *testing.T) {
	registerTestPubKey(t)
	r := newIngestRouter()

	w := postIngest(t, r, make([]byte, packetBBodyLen-1))

	if w.Code != http.StatusBadRequest {
		body, _ := io.ReadAll(w.Body)
		t.Errorf("177-byte buffer: got %d, want 400. body=%s", w.Code, string(body))
	}
}

// TestIngestRejectsValidPacketBWithUnknownSat — defence-in-depth:
// even a structurally valid and correctly signed 184-byte Packet B
// must be rejected if the sat_id is not registered. Proves the
// verifier is actually in the path and not shortcircuited.
func TestIngestRejectsValidPacketBWithUnknownSat(t *testing.T) {
	registerTestPubKey(t)
	// Deregister, ensuring the verifier will see an unknown sat.
	delete(registry.MockDB, detTestSatID)
	defer registerTestPubKey(t) // restore for other tests

	r := newIngestRouter()
	raw := loadGoldenPacketB(t, "ccsds")
	w := postIngest(t, r, raw)

	if w.Code == http.StatusOK {
		t.Errorf("unknown sat Packet B: got 200, want non-2xx rejection")
	}
}
