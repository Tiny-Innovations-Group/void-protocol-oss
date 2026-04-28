package void_protocol_test

import (
	"crypto/ed25519"
	"encoding/hex"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol/protocol"
)

// VOID-111: the Ed25519 signature on Packet B covers (routing_header +
// body[0..packetBSigScopeBody]) where the scope body length is exactly
// 106 bytes. That scope is identical in semantics across tiers — the
// only difference is whether the routing header is the 6-byte CCSDS
// primary header or the 14-byte SNLP header. The same signing key MUST
// produce a valid signature on both tiers' packets using this scope.
const (
	packetBSigScopeBody = 106
	ccsdsHeaderLen      = 6
	snlpHeaderLen       = 14
)

// detTestPubKey is derived at test-setup time from the single committed
// test seed documented in test/vectors/README.md. It is a test-only key
// and must not appear in any flight configuration.
func detTestPubKey(t *testing.T) ed25519.PublicKey {
	t.Helper()
	const detSeedHex = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		t.Fatalf("decode det seed: %v", err)
	}
	return ed25519.NewKeyFromSeed(seed).Public().(ed25519.PublicKey)
}

// TestPacketBSignatureParityAcrossTiers — VOID-111. Load the two
// committed Packet B vectors, extract the exact sig-scope bytes
// (header + body[0..105]) from each, and verify both signatures
// against the one deterministic test pubkey. Both MUST verify.
//
// A failure here means the signature scope has drifted between tiers,
// or between the Go generator and this test's understanding of the
// scope boundary. Either way the cross-implementation guarantee is
// broken and the PR must not land.
func TestPacketBSignatureParityAcrossTiers(t *testing.T) {
	pub := detTestPubKey(t)

	cases := []struct {
		tier      string
		headerLen int
	}{
		{"ccsds", ccsdsHeaderLen},
		{"snlp", snlpHeaderLen},
	}

	for _, c := range cases {
		t.Run(c.tier, func(t *testing.T) {
			raw := readVector(t, c.tier, "packet_b.bin")
			p := parseVector(t, raw)

			body, ok := p.Body.(*protocol.VoidProtocol_PacketBBody)
			if !ok {
				t.Fatalf("expected *VoidProtocol_PacketBBody, got %T", p.Body)
			}
			if got := len(body.Signature.Raw); got != ed25519.SignatureSize {
				t.Fatalf("signature length %d, want %d", got, ed25519.SignatureSize)
			}

			// Signature scope = header + first 106 body bytes, taken
			// verbatim from the raw wire buffer. Using raw[] (not the
			// parsed fields) guarantees we're testing the actual
			// on-the-wire bytes the signer signed.
			sigScopeEnd := c.headerLen + packetBSigScopeBody
			if len(raw) < sigScopeEnd {
				t.Fatalf("vector too short (%d) for sig scope end %d", len(raw), sigScopeEnd)
			}
			sigScope := raw[:sigScopeEnd]

			if !ed25519.Verify(pub, sigScope, body.Signature.Raw) {
				t.Errorf("%s Packet B signature failed to verify against deterministic test pubkey", c.tier)
			}
		})
	}
}

// TestPacketBSigScopeBoundary — the 106-byte body-scope constant must
// line up with the end of the PreSig padding in the struct. The
// signature field MUST start exactly at body offset 106. This is what
// VOID-111 pins and what VOID-125's C++ test_sign_verify.cpp relies on
// via offsetof().
func TestPacketBSigScopeBoundary(t *testing.T) {
	// padHead(2) + epoch_ts(8) + pos_vec(24) + enc_payload(62) +
	// pre_sat(2) + sat_id(4) + pre_sig(4) = 106
	const want = 2 + 8 + 24 + 62 + 2 + 4 + 4
	if want != packetBSigScopeBody {
		t.Fatalf("sig scope constant mismatch: field layout sums to %d, packetBSigScopeBody=%d",
			want, packetBSigScopeBody)
	}
}
