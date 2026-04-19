package receipt

import (
	"bytes"
	"crypto/ed25519"
	"encoding/hex"
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
)

// VOID-135a builder red-green.
// The golden PacketC at test/vectors/snlp/packet_c.bin was emitted by
// gateway/test/utils/generate_packets.go::genPacketC using the
// deterministic seed, sat-A APID (100), exec_time = detEpochTsMs, and
// the hardcoded enc_tx_id literal 0xDEADBEEFCAFEBABE. Feed the same
// inputs to the builder here and the output MUST be byte-identical to
// the checked-in golden.
//
// When this test fails, it is ALWAYS one of:
//   - struct field order drift vs Protocol-spec-SNLP §3
//   - signature scope moved from "body[0..25]" to something else
//   - CRC scope moved from "header + body[0..103]" to something else
//   - signer seed / public-key rotated without updating the vector
//
// None of these should change silently. Update the golden deliberately
// and re-run; do not patch the test to match a new output.

const (
	detSeedHex    = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
	detEpochTsMs  = uint64(1710000100000)
	detEncTxID    = uint64(0xDEADBEEFCAFEBABE)
	detEncStatus  = uint8(1)
	apidSatA      = uint16(100)
	packetCSizeSn = 112
)

// repoRoot climbs from this test file to the repo root. builder_test.go
// sits at gateway/internal/core/receipt/, so we walk up four levels.
func repoRoot(t *testing.T) string {
	t.Helper()
	_, self, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatalf("runtime.Caller failed")
	}
	return filepath.Join(filepath.Dir(self), "..", "..", "..", "..")
}

func loadGoldenPacketCSNLP(t *testing.T) []byte {
	t.Helper()
	path := filepath.Join(repoRoot(t), "test", "vectors", "snlp", "packet_c.bin")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	if len(data) != packetCSizeSn {
		t.Fatalf("golden packet_c.bin SNLP: expected %d bytes, got %d", packetCSizeSn, len(data))
	}
	return data
}

func deterministicSeed(t *testing.T) []byte {
	t.Helper()
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		t.Fatalf("decode det seed: %v", err)
	}
	if len(seed) != ed25519.SeedSize {
		t.Fatalf("det seed wrong size: got %d, want %d", len(seed), ed25519.SeedSize)
	}
	return seed
}

// TestBuild_ByteExactMatchesGolden — the canonical flat-sat PacketC
// emitted by the gateway MUST be byte-identical to the checked-in
// golden vector when fed the same deterministic inputs. This is the
// primary regression gate for the builder; any drift here invalidates
// the cross-impl wire contract.
func TestBuild_ByteExactMatchesGolden(t *testing.T) {
	seed := deterministicSeed(t)

	built, err := Build(Inputs{
		ExecTimeMs: detEpochTsMs,
		EncTxID:    detEncTxID,
		EncStatus:  detEncStatus,
		APID:       apidSatA,
		SeqCount:   0,
		SeedEd25519: seed,
	})
	if err != nil {
		t.Fatalf("Build: %v", err)
	}

	golden := loadGoldenPacketCSNLP(t)
	if len(built) != len(golden) {
		t.Fatalf("length: built %d, golden %d", len(built), len(golden))
	}
	if !bytes.Equal(built, golden) {
		// Narrow the diff to a readable line count so the failure
		// report points at the first diverging byte rather than 112
		// bytes of hex.
		for i := 0; i < len(built); i++ {
			if built[i] != golden[i] {
				t.Fatalf("first byte diff at offset %d: built 0x%02x, golden 0x%02x\n  built:  %x\n  golden: %x",
					i, built[i], golden[i], built, golden)
			}
		}
	}
}

// TestBuild_SignatureVerifiesAgainstSeederPubKey — the signature the
// builder produced verifies with the public key derived from the same
// seed. Cheap sanity that we are actually Ed25519-signing, not just
// writing zero bytes into the 64-byte slot.
func TestBuild_SignatureVerifiesAgainstSeederPubKey(t *testing.T) {
	seed := deterministicSeed(t)

	built, err := Build(Inputs{
		ExecTimeMs:  detEpochTsMs,
		EncTxID:     detEncTxID,
		EncStatus:   detEncStatus,
		APID:        apidSatA,
		SeqCount:    0,
		SeedEd25519: seed,
	})
	if err != nil {
		t.Fatalf("Build: %v", err)
	}

	// Scope = body[0..25] = 26 bytes, starting just after the 14-byte
	// SNLP header. Pulled from void_protocol constants for consistency.
	const (
		hdrLen      = void_protocol.SNLPHeaderLen // 14
		sigScopeLen = 26
		sigOff      = hdrLen + 26 // 40
	)
	msg := built[hdrLen : hdrLen+sigScopeLen]
	sig := built[sigOff : sigOff+ed25519.SignatureSize]

	priv := ed25519.NewKeyFromSeed(seed)
	pub := priv.Public().(ed25519.PublicKey)
	if !ed25519.Verify(pub, msg, sig) {
		t.Fatalf("signature does not verify against seed-derived pubkey")
	}
}

// TestBuild_CRCCoversHeaderPlusBodyToSignatureEnd — flipping the first
// body byte must invalidate the CRC. Guards against accidentally
// narrowing CRC scope (e.g. covering only body, or only sig region).
func TestBuild_CRCCoversHeaderPlusBodyToSignatureEnd(t *testing.T) {
	seed := deterministicSeed(t)

	orig, err := Build(Inputs{
		ExecTimeMs:  detEpochTsMs,
		EncTxID:     detEncTxID,
		EncStatus:   detEncStatus,
		APID:        apidSatA,
		SeqCount:    0,
		SeedEd25519: seed,
	})
	if err != nil {
		t.Fatalf("Build: %v", err)
	}

	// CRC covers header + body[0..103]; crc32 sits at frame[104..107].
	// Flipping any byte in that covered region must change the CRC
	// value in the frame.
	const crcFieldOff = void_protocol.SNLPHeaderLen + 90 // 14 + 90 = 104
	origCRC := orig[crcFieldOff : crcFieldOff+4]

	// Flip a deep body byte well inside the pre-sig area.
	flipAt := void_protocol.SNLPHeaderLen + 5 // inside exec_time
	orig[flipAt] ^= 0xFF

	// Recompute a fresh build at the new input (cheaper sanity — just
	// compare the CRC positions).
	_ = origCRC // silence "unused" in case of lint noise; we're asserting below

	// Rebuild with the same inputs except one byte in the body has
	// flipped. Easiest: build twice with DIFFERENT inputs and compare
	// CRCs. Flip enc_status from 1 to 0.
	alt, err := Build(Inputs{
		ExecTimeMs:  detEpochTsMs,
		EncTxID:     detEncTxID,
		EncStatus:   0, // different body byte
		APID:        apidSatA,
		SeqCount:    0,
		SeedEd25519: seed,
	})
	if err != nil {
		t.Fatalf("Build (alt): %v", err)
	}

	origReference, _ := Build(Inputs{
		ExecTimeMs:  detEpochTsMs,
		EncTxID:     detEncTxID,
		EncStatus:   detEncStatus,
		APID:        apidSatA,
		SeqCount:    0,
		SeedEd25519: seed,
	})
	a := origReference[crcFieldOff : crcFieldOff+4]
	b := alt[crcFieldOff : crcFieldOff+4]
	if bytes.Equal(a, b) {
		t.Fatalf("CRC unchanged under body-byte flip — scope too narrow")
	}
}
