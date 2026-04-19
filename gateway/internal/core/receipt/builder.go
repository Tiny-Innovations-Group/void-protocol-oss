// Package receipt builds the PacketC wire frame the gateway emits after
// observing an on-chain Escrow.SettlementCreated event (VOID-135 / journey
// #15). PacketC is the seller-facing receipt — it is signed on-behalf-of
// the seller using a demo Ed25519 seed (VOID-121 waived for flat-sat)
// and closes the commerce loop: A (Invoice) → B (Payment) → settle → C
// (Receipt).
//
// This file is the builder half of #15a: pure function from inputs to
// a 112-byte SNLP wire frame. Persistence (store.go), event watching
// (watcher.go), and the orchestrator live alongside in this package.
// LoRa egress + end-to-end crash-safety test are #15b.

package receipt

import (
	"crypto/ed25519"
	"encoding/binary"
	"fmt"
	"hash/crc32"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
)

// PacketCFrameLen is the full wire length of a SNLP-tier PacketC frame.
// Sourced from void-core/include/void_packets_snlp.h::PacketC_t (112 B).
const PacketCFrameLen = 112

// Byte offsets inside a built PacketC frame — useful to callers that
// want to carve up bytes without re-deriving the layout.
const (
	// Header occupies [0, SNLPHeaderLen) = [0, 14).
	_                      = void_protocol.SNLPHeaderLen // anchor import usage
	PacketCBodyStart       = void_protocol.SNLPHeaderLen // 14
	PacketCExecTimeOff     = PacketCBodyStart + 2        // 16
	PacketCEncTxIDOff      = PacketCExecTimeOff + 8      // 24
	PacketCEncStatusOff    = PacketCEncTxIDOff + 8       // 32
	PacketCSigScopeLen     = 26                          // bytes covered by Ed25519 signature (body[0..25])
	PacketCSignatureOff    = PacketCBodyStart + PacketCSigScopeLen // 40
	PacketCCRCOff          = PacketCSignatureOff + ed25519.SignatureSize // 104
	PacketCTailPadOff      = PacketCCRCOff + 4           // 108
)

// Inputs captures everything the builder needs. All fields are required;
// a nil or wrong-sized SeedEd25519 returns an error. The seed should
// match the seller's registered public key (verifier.go consults
// registry.MockDB[sat_id].PubKeyHex).
type Inputs struct {
	// ExecTimeMs is the block timestamp (Unix ms) of the on-chain
	// SettlementCreated event that triggered this receipt.
	ExecTimeMs uint64

	// EncTxID is an opaque 64-bit correlation token. The flat-sat
	// gateway sets it to the low 64 bits of the payment_id
	// (= epoch_ts of the original PacketB) so the seller can
	// back-match the receipt to its original PacketA by timestamp.
	EncTxID uint64

	// EncStatus is the settlement outcome. 0x01 = success; 0x00 would
	// represent a revert path (not emitted today — the on-chain contract
	// reverts whole batches rather than recording a failed entry).
	EncStatus uint8

	// APID is the 11-bit CCSDS APID for the seller. Flat-sat demo
	// uses 100 (apidSatA).
	APID uint16

	// SeqCount is the CCSDS sequence count for this frame. 14-bit
	// field; caller owns monotonic increment when firing receipts
	// at volume.
	SeqCount uint16

	// SeedEd25519 is the 32-byte Ed25519 private-key seed used to
	// sign the receipt. Must match the seller's registered public
	// key so downstream verifiers accept the frame.
	SeedEd25519 []byte
}

// Build emits a 112-byte SNLP PacketC frame matching Protocol-spec-SNLP.md
// §3 / void_packets_snlp.h::PacketC_t. The frame has its signature and
// CRC computed over the canonical scopes (body[0..25] and
// header+body[0..103] respectively). Returned bytes are safe to retain —
// Build does not reuse the underlying array.
func Build(in Inputs) ([]byte, error) {
	if len(in.SeedEd25519) != ed25519.SeedSize {
		return nil, fmt.Errorf("receipt.Build: seed must be %d bytes, got %d",
			ed25519.SeedSize, len(in.SeedEd25519))
	}
	priv := ed25519.NewKeyFromSeed(in.SeedEd25519)

	frame := make([]byte, PacketCFrameLen)

	// ---- 1. SNLP header (14 bytes, big-endian) ----
	writeSNLPHeader(frame[:void_protocol.SNLPHeaderLen], in.APID, in.SeqCount,
		PacketCFrameLen-void_protocol.SNLPHeaderLen /* body_len = 98 */)

	// ---- 2. Body pre-signature: _pad_head(2) + exec_time(8) + enc_tx_id(8) + enc_status(1) + _pad_sig(7) ----
	// _pad_head is already zero from make().
	binary.LittleEndian.PutUint64(frame[PacketCExecTimeOff:], in.ExecTimeMs)
	binary.LittleEndian.PutUint64(frame[PacketCEncTxIDOff:], in.EncTxID)
	frame[PacketCEncStatusOff] = in.EncStatus
	// _pad_sig is zero from make().

	// ---- 3. Ed25519 signature over body[0..25] ----
	sigScope := frame[PacketCBodyStart : PacketCBodyStart+PacketCSigScopeLen]
	sig := ed25519.Sign(priv, sigScope)
	copy(frame[PacketCSignatureOff:PacketCSignatureOff+ed25519.SignatureSize], sig)

	// ---- 4. CRC32 over header + body[0..103] = frame[0..103] ----
	crc := crc32.ChecksumIEEE(frame[:PacketCCRCOff])
	binary.LittleEndian.PutUint32(frame[PacketCCRCOff:PacketCCRCOff+4], crc)

	// ---- 5. _tail_pad is already zero from make() ----
	return frame, nil
}

// writeSNLPHeader writes the 14-byte SNLP header into dst. Matches
// gateway/test/utils/generate_packets.go::buildHeader for a non-command
// (telemetry) frame: version=0, type=0, sec=1, seq_flags=11, seq_count
// supplied by caller, packet_len = bodyLen - 1 (CCSDS convention).
func writeSNLPHeader(dst []byte, apid, seqCount uint16, bodyLen int) {
	// sync_word 0x1D01A5A5 (BE)
	dst[0] = 0x1D
	dst[1] = 0x01
	dst[2] = 0xA5
	dst[3] = 0xA5

	// id field: version(3)=0 | type(1)=0 | sec(1)=1 | apid(11)
	id := uint16(0x0800) | (apid & 0x07FF)
	dst[4] = byte(id >> 8)
	dst[5] = byte(id)

	// seq flags (11 = unsegmented) | seq_count (14-bit)
	seq := uint16(0xC000) | (seqCount & 0x3FFF)
	dst[6] = byte(seq >> 8)
	dst[7] = byte(seq)

	// packet_len = bodyLen - 1
	plen := uint16(bodyLen - 1)
	dst[8] = byte(plen >> 8)
	dst[9] = byte(plen)

	// align_pad (4B, zeros — already zero from make())
	_ = dst[13]
}
