package void_protocol_test

import (
	"bytes"
	"encoding/binary"
	"reflect"
	"strings"
	"testing"

	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
)

// deriveChaCha20Nonce implements the VOID-110 nonce rule:
//
//	nonce = sat_id_le[4] || epoch_ts_le[8]      (12 bytes total)
//
// The nonce is derived on both ends from fields that are already on the
// wire — it is never transmitted as its own field. Any new field called
// "nonce" in PacketBBody would be a spec violation.
func deriveChaCha20Nonce(satID uint32, epochTsMs uint64) []byte {
	nonce := make([]byte, 12)
	binary.LittleEndian.PutUint32(nonce[0:4], satID)
	binary.LittleEndian.PutUint64(nonce[4:12], epochTsMs)
	return nonce
}

// TestNonceDerivation — VOID-110. Two distinct (sat_id, epoch_ts) tuples
// must yield distinct 12-byte nonces. This also locks the byte-level
// layout: sat_id first (4 LE bytes), epoch_ts second (8 LE bytes).
func TestNonceDerivation(t *testing.T) {
	cases := []struct {
		name    string
		satID   uint32
		epochTs uint64
	}{
		{"golden", 0xCAFEBABE, 1710000100000},
		{"alt-sat", 0xDEADBEEF, 1710000100000},
		{"alt-ts", 0xCAFEBABE, 1710000200000},
	}

	nonces := make(map[string][]byte)
	for _, c := range cases {
		n := deriveChaCha20Nonce(c.satID, c.epochTs)
		if got := len(n); got != 12 {
			t.Fatalf("%s: nonce length %d, want 12", c.name, got)
		}
		nonces[c.name] = n
	}

	if bytes.Equal(nonces["golden"], nonces["alt-sat"]) {
		t.Errorf("nonce collision: different sat_id must produce different nonce")
	}
	if bytes.Equal(nonces["golden"], nonces["alt-ts"]) {
		t.Errorf("nonce collision: different epoch_ts must produce different nonce")
	}
	if bytes.Equal(nonces["alt-sat"], nonces["alt-ts"]) {
		t.Errorf("nonce collision: alt-sat and alt-ts must differ")
	}

	// Byte-level pin: for the golden tuple, verify the exact 12 bytes.
	want := []byte{
		0xBE, 0xBA, 0xFE, 0xCA, // sat_id 0xCAFEBABE LE
		0xA0, 0xD2, 0xF2, 0x23, // epoch_ts 1710000100000 LE (low 4 of 0x18E23F2D2A0)
		0x8E, 0x01, 0x00, 0x00, // epoch_ts LE (high 4)
	}
	if !bytes.Equal(nonces["golden"], want) {
		t.Errorf("golden nonce bytes:\n  got  %x\n  want %x", nonces["golden"], want)
	}
}

// TestNonceMatchesGoldenVectorFields — parse the committed Packet B
// vectors and confirm that the nonce derived from their on-wire (SatId,
// EpochTs) fields is the deterministic test nonce. Any drift between
// the generator's fields and this derivation rule breaks ChaCha20
// interop — this test catches that before firmware ever runs.
func TestNonceMatchesGoldenVectorFields(t *testing.T) {
	for _, tier := range []string{"ccsds", "snlp"} {
		t.Run(tier, func(t *testing.T) {
			raw := readVector(t, tier, "packet_b.bin")
			p := parseVector(t, raw)

			body, ok := p.Body.(*void_protocol.VoidProtocol_PacketBBody)
			if !ok {
				t.Fatalf("expected *VoidProtocol_PacketBBody, got %T", p.Body)
			}

			gotNonce := deriveChaCha20Nonce(body.SatId, body.EpochTs)
			wantNonce := deriveChaCha20Nonce(0xCAFEBABE, 1710000100000)
			if !bytes.Equal(gotNonce, wantNonce) {
				t.Errorf("%s nonce mismatch:\n  got  %x (from sat_id=%#x, epoch=%d)\n  want %x",
					tier, gotNonce, body.SatId, body.EpochTs, wantNonce)
			}
		})
	}
}

// TestPacketBBodyHasNoWireNonceField — VOID-110 removed the 4-byte
// transmitted nonce. This test locks the struct shape: no field on
// VoidProtocol_PacketBBody may contain the substring "nonce" (case-
// insensitive). If someone ever adds one back, this fails loud.
func TestPacketBBodyHasNoWireNonceField(t *testing.T) {
	typ := reflect.TypeOf(void_protocol.VoidProtocol_PacketBBody{})
	for i := 0; i < typ.NumField(); i++ {
		name := typ.Field(i).Name
		if strings.Contains(strings.ToLower(name), "nonce") {
			t.Errorf("VoidProtocol_PacketBBody.%s reintroduces a wire nonce (forbidden by VOID-110)", name)
		}
	}
}
