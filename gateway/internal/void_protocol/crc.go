package void_protocol

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

// VOID-122 — CRC pre-validation gate.
//
// PacketD and PacketAck are the only two packet types on the wire that
// land in the "dispatch_122" collision zone (both have a 122-byte body
// in the SNLP tier; CCSDS PacketD also hits 122 B via its separate
// path). Neither carries an Ed25519 signature, so the trailing CRC32 is
// the only integrity gate available. The ingest handler must verify it
// BEFORE trusting any dispatched body fields — otherwise a single
// bit-flip on the RF link silently routes D as ACK (or vice versa),
// or leaks a wrong business field downstream.
//
// Layout of the CRC region differs between the two:
//
//	PacketD:   [..header + body before CRC..][CRC u32 LE (4)][_tail (6)]
//	           CRC field at frame_size - 10. _tail is NOT CRC-covered;
//	           it is frame-alignment filler (VOID-114B).
//	PacketAck: [..header + body before CRC..][CRC u32 LE (4)]
//	           CRC field at frame_size - 4. No tail.
//
// Both CRCs cover `frame[0:crc_offset]` (header + all body bytes before
// the CRC field itself). The hash function is IEEE 802.3 CRC32 — the
// same polynomial Go's hash/crc32.ChecksumIEEE produces, which matches
// the Go test vector generator and the C++ firmware implementation in
// void-core (see satellite-firmware/src/void_protocol.cpp).

// PacketDCrcOffsetFromEnd is the byte distance from the END of a
// PacketD frame to the START of its 4-byte CRC field. The 6 bytes
// AFTER the CRC are `_tail[6]` (frame-alignment pad, not CRC-covered).
const PacketDCrcOffsetFromEnd = 10

// PacketAckCrcOffsetFromEnd is the byte distance from the END of a
// PacketAck frame to the START of its 4-byte CRC field. No trailing
// pad follows the CRC.
const PacketAckCrcOffsetFromEnd = 4

// ValidateFrameCRC32 verifies the little-endian u32 CRC at
// frame[crcOffset:crcOffset+4] against crc32.ChecksumIEEE applied to
// frame[0:crcOffset]. Returns nil on a match, a descriptive error
// otherwise. The caller computes crcOffset from the packet type's
// known layout (see PacketDCrcOffsetFromEnd / PacketAckCrcOffsetFromEnd).
func ValidateFrameCRC32(frame []byte, crcOffset int) error {
	if crcOffset < 0 || crcOffset+4 > len(frame) {
		return fmt.Errorf(
			"frame-crc: crcOffset %d out of bounds for frame of len %d",
			crcOffset, len(frame))
	}
	wire := binary.LittleEndian.Uint32(frame[crcOffset : crcOffset+4])
	computed := crc32.ChecksumIEEE(frame[:crcOffset])
	if wire != computed {
		return fmt.Errorf(
			"frame-crc: computed 0x%08x, wire 0x%08x", computed, wire)
	}
	return nil
}
