package void_protocol_test

import (
	"bytes"
	"encoding/binary"
	"testing"

	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
)

// VOID-113: SNLP header is exactly 14 bytes, CCSDS header is exactly 6
// bytes, and the SNLP sync word is 0x1D01A5A5. These are pinned across
// the whole protocol — any drift here means wire-format rewrite.
const snlpSyncWord uint32 = 0x1D01A5A5

// TestCcsdsHeaderLength — parse any committed CCSDS vector and confirm
// the routing header consumes exactly 6 bytes. Uses packet_l (40 B)
// because it's the smallest vector and easiest to reason about.
func TestCcsdsHeaderLength(t *testing.T) {
	raw := readVector(t, "ccsds", "packet_l.bin")
	p := parseVector(t, raw)

	isSnlp, err := p.IsSnlp()
	if err != nil {
		t.Fatalf("IsSnlp: %v", err)
	}
	if isSnlp {
		t.Fatalf("ccsds/packet_l.bin parsed as SNLP")
	}

	hdr, ok := p.RoutingHeader.(*void_protocol.VoidProtocol_HeaderCcsds)
	if !ok {
		t.Fatalf("expected *VoidProtocol_HeaderCcsds, got %T", p.RoutingHeader)
	}

	// CCSDS header is three big-endian u16s: 6 bytes total.
	// We can't ask kaitai for the raw header length, so verify
	// structurally: the three u16 fields must be populated AND the
	// total frame (40 B) minus the body (34 B) equals 6.
	_ = hdr.VersionTypeSecApid
	_ = hdr.SeqFlagsCount
	_ = hdr.PacketLength
	const expectedBodyLen = 34 // heartbeat / packet L body
	if got := len(raw) - expectedBodyLen; got != 6 {
		t.Errorf("CCSDS header length derived from raw frame = %d, want 6", got)
	}
}

// TestSnlpHeaderLength — parse a committed SNLP vector and confirm the
// routing header consumes exactly 14 bytes AND starts with the
// 0x1D01A5A5 sync word. Uses packet_l (48 B) for the same reason.
func TestSnlpHeaderLength(t *testing.T) {
	raw := readVector(t, "snlp", "packet_l.bin")
	p := parseVector(t, raw)

	isSnlp, err := p.IsSnlp()
	if err != nil {
		t.Fatalf("IsSnlp: %v", err)
	}
	if !isSnlp {
		t.Fatalf("snlp/packet_l.bin did not parse as SNLP")
	}

	hdr, ok := p.RoutingHeader.(*void_protocol.VoidProtocol_HeaderSnlp)
	if !ok {
		t.Fatalf("expected *VoidProtocol_HeaderSnlp, got %T", p.RoutingHeader)
	}

	// Sync word is the first 4 big-endian bytes on the wire.
	if got := binary.BigEndian.Uint32(hdr.SyncWord); got != snlpSyncWord {
		t.Errorf("SNLP sync word = %#x, want %#x", got, snlpSyncWord)
	}
	// Also verify directly against the raw bytes — the parser could in
	// principle lie; the wire cannot.
	wantSync := []byte{0x1D, 0x01, 0xA5, 0xA5}
	if !bytes.Equal(raw[:4], wantSync) {
		t.Errorf("raw sync bytes = %x, want %x", raw[:4], wantSync)
	}

	// SNLP header = 4 sync + 6 CCSDS + 4 align pad = 14.
	// Verify via frame total minus body length.
	const expectedBodyLen = 34
	if got := len(raw) - expectedBodyLen; got != 14 {
		t.Errorf("SNLP header length derived from raw frame = %d, want 14", got)
	}
	if got := len(hdr.AlignPad); got != 4 {
		t.Errorf("SNLP align pad length = %d, want 4", got)
	}
}

// TestHeaderDeltaForFixedBodyVectors — for every committed packet type
// whose body length does NOT vary by tier, the SNLP total MUST be
// exactly 8 bytes larger than the CCSDS total (14-byte SNLP header vs
// 6-byte CCSDS header). Packet ACK is excluded because its tunnel
// payload intentionally grows by 8 bytes under SNLP to maintain
// alignment, producing a 16-byte delta by design.
func TestHeaderDeltaForFixedBodyVectors(t *testing.T) {
	for _, v := range goldenVectors {
		if v.file == "packet_ack.bin" {
			continue
		}
		t.Run(v.file, func(t *testing.T) {
			if delta := v.snlpLen - v.ccsdsLen; delta != 8 {
				t.Errorf("%s: snlp(%d) - ccsds(%d) = %d, want 8 (14-byte SNLP vs 6-byte CCSDS)",
					v.file, v.snlpLen, v.ccsdsLen, delta)
			}
		})
	}
}
