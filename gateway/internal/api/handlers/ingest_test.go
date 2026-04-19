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
	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
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
	detTestSatID = uint32(0xCAFEBABE)
	detSeedHex   = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
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

	w := postIngest(t, r, make([]byte, void_protocol.PacketBBodyLen-1))

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

// ---------------------------------------------------------------------------
// VOID-129 — Ed25519 signature verification gate, both tiers.
// ---------------------------------------------------------------------------

// Packet B body layout (178 B total, offsets within body):
//
//   00-01  _pad_head        ← sig-scope byte candidate (in-scope)
//   02-09  epoch_ts
//   10-33  pos_vec
//   34-95  enc_payload      ← in-scope flip target (we use body offset 50)
//   96-97  _pre_sat
//   98-101 sat_id
//   102-105 _pre_sig        ← end of VOID-111 sig scope (body[0..105])
//   106-169 signature       ← flipped-sig target (we use body offset 110)
//   170-173 global_crc      ← out-of-scope flip target (body offset 170)
//   174-177 _tail_pad
//
// Absolute offsets add the tier header length: 6 (CCSDS) or 14 (SNLP).
func flipByte(raw []byte, offset int) []byte {
	mutated := append([]byte{}, raw...)
	mutated[offset] ^= 0xFF
	return mutated
}

// TestSigVerifyHandler — VOID-129: pin the signature gate behaviour across
// both tiers. Four assertions per tier: valid → 200, flipped signature →
// 400, flipped in-scope body byte → 400, flipped out-of-scope body byte
// → 200 (documented exception — VOID-111 sig scope stops at body[105];
// a future CRC gate would flip this to 400).
//
// Red-green: before VOID-129 the handler returned 401 on signature
// failure. This test pins 400 per CLAUDE.md VOID-111/112 ("length check
// first, cast second" — reject under-sized or signature-mismatched
// frames with 400, not 401 — the frame is malformed input, not an
// authorisation failure).
func TestSigVerifyHandler(t *testing.T) {
	registerTestPubKey(t)

	// All offsets are ABSOLUTE into the raw frame (header + body).
	const (
		bodyOffInScope    = 50  // inside enc_payload region
		bodyOffSignature  = 110 // inside the 64-byte signature
		bodyOffOutOfScope = 170 // inside global_crc (past VOID-111 scope)
	)

	cases := []struct {
		name       string
		tier       string
		headerLen  int
		flipBody   int // body-relative offset, -1 if no flip
		wantStatus int
	}{
		{"Valid_CCSDS", "ccsds", 6, -1, http.StatusOK},
		{"Valid_SNLP", "snlp", 14, -1, http.StatusOK},

		{"FlippedSig_CCSDS", "ccsds", 6, bodyOffSignature, http.StatusBadRequest},
		{"FlippedSig_SNLP", "snlp", 14, bodyOffSignature, http.StatusBadRequest},

		{"FlippedBodyInScope_CCSDS", "ccsds", 6, bodyOffInScope, http.StatusBadRequest},
		{"FlippedBodyInScope_SNLP", "snlp", 14, bodyOffInScope, http.StatusBadRequest},

		// Out-of-scope flip: VOID-111 sig only covers body[0..105]. A flip
		// at body offset 170 (global_crc region) is invisible to the
		// signature check. Returns 200 today — documented carve-out until
		// a CRC gate lands as a follow-up ticket.
		{"FlippedBodyOutOfScope_CCSDS", "ccsds", 6, bodyOffOutOfScope, http.StatusOK},
		{"FlippedBodyOutOfScope_SNLP", "snlp", 14, bodyOffOutOfScope, http.StatusOK},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			r := newIngestRouter()
			raw := loadGoldenPacketB(t, tc.tier)
			payload := raw
			if tc.flipBody >= 0 {
				payload = flipByte(raw, tc.headerLen+tc.flipBody)
			}
			w := postIngest(t, r, payload)
			if w.Code != tc.wantStatus {
				body, _ := io.ReadAll(w.Body)
				t.Errorf("%s: got %d, want %d. body=%s",
					tc.name, w.Code, tc.wantStatus, string(body))
			}
		})
	}
}
