package main

import (
	"bytes"
	"crypto/ed25519"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"hash/crc32"
	"log"
	"os"
	"path/filepath"
)

/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Native Go Packet Generator
 * Desc:      Replaces gen_packet.py. Zero external dependencies.
 *-------------------------------------------------------------------------*/

const outputDir = "./generated_packets"

// Constants
const syncWord uint32 = 0x1D01A5A5 // SNLP Magic Word
const apidGround uint32 = 0
const apidSatA uint32 = 100 // Seller
const apidSatB uint32 = 101 // Mule/Buyer

// PUF Private Key Seeds (32 bytes / 64 hex chars)
const privHexA = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
const privHexB = "8994ec3b3d470df7432bd5b74783765822c756c7ff972942a5efdad61473605b"

var privKeyA ed25519.PrivateKey
var privKeyB ed25519.PrivateKey

func init() {
	// Derive full 64-byte Ed25519 Private Keys from the 32-byte seeds
	seedA, _ := hex.DecodeString(privHexA)
	privKeyA = ed25519.NewKeyFromSeed(seedA)

	seedB, _ := hex.DecodeString(privHexB)
	privKeyB = ed25519.NewKeyFromSeed(seedB)
}

// ==============================================================================
// HELPERS
// ==============================================================================

func writeLE(buf *bytes.Buffer, data interface{}) {
	if err := binary.Write(buf, binary.LittleEndian, data); err != nil {
		log.Fatalf("Failed to write LittleEndian: %v", err)
	}
}

func writeBE(buf *bytes.Buffer, data interface{}) {
	if err := binary.Write(buf, binary.BigEndian, data); err != nil {
		log.Fatalf("Failed to write BigEndian: %v", err)
	}
}

func getCRC(data []byte) uint32 {
	return crc32.ChecksumIEEE(data)
}

// buildHeader constructs either a 14-byte SNLP or 6-byte CCSDS header
func buildHeader(isSnlp bool, payloadLen int, apid uint32, isCmd bool) []byte {
	var buf bytes.Buffer

	if isSnlp {
		writeBE(&buf, syncWord)
	}

	var version uint16 = 0
	var pktType uint16 = 0
	if isCmd {
		pktType = 1
	}
	var secFlag uint16 = 1

	idFieldVal := (version << 13) | (pktType << 12) | (secFlag << 11) | uint16(apid&0x7FF)
	writeBE(&buf, idFieldVal)
	writeBE(&buf, uint16(0xC000))       // Seq Count
	writeBE(&buf, uint16(payloadLen-1)) // Packet Length

	if isSnlp {
		writeBE(&buf, uint32(0)) // 4-Byte Alignment Pad
	}

	return buf.Bytes()
}

// ==============================================================================
// BODY GENERATORS
// ==============================================================================

func genPacketA(isSnlp bool) []byte {
	// VOID-114B: Packet A body is 66 bytes (was 62). Added _pad_head[2] at
	// body offset 0 and _pre_crc[2] between asset_id and crc32 so every
	// critical field lands on its natural alignment boundary.
	var body bytes.Buffer
	body.Write([]byte{0x00, 0x00})                        // PadHead (VOID-114B)
	writeLE(&body, uint64(1710000000))                    // EpochTs
	writeLE(&body, []float64{7000.12, -12000.45, 550.78}) // PosVec
	writeLE(&body, []float32{7.5, -0.2, 0.01})            // VelVec
	writeLE(&body, apidSatA)                              // SatId
	writeLE(&body, uint64(420000000))                     // Amount
	writeLE(&body, uint16(1))                             // AssetId
	body.Write([]byte{0x00, 0x00})                        // PreCrc (VOID-114B)

	header := buildHeader(isSnlp, 66, apidSatA, false)
	crc := getCRC(append(header, body.Bytes()...))
	writeLE(&body, crc)

	return append(header, body.Bytes()...)
}

func genPacketB(isSnlp bool) []byte {
	// VOID-110 + VOID-114B: Packet B body is now 178 bytes (174 payload + 4-byte tail pad).
	//   • VOID-110 removed the 4-byte wire nonce (ChaCha20 nonce is derived
	//     from sat_id || epoch_ts at runtime).
	//   • VOID-114B added three alignment pads: _pad_head[2] at body 0,
	//     _pre_sat[2] between enc_payload and sat_id, _pre_sig[4] between
	//     sat_id and signature. _tail_pad[4] brings frame totals to 184
	//     CCSDS (÷8 ✅) and 192 SNLP (÷64 ✅ cache line).
	// The signature still covers (header + body[0..105]) = 106 body bytes.
	var body bytes.Buffer
	body.Write([]byte{0x00, 0x00})                     // PadHead (VOID-114B)
	writeLE(&body, uint64(1710000100))                 // EpochTs (ms)
	writeLE(&body, []float64{7010.0, -11990.0, 560.0}) // PosVec

	// Structured EncPayload (62 Bytes) — plaintext under SNLP per spec.
	var enc bytes.Buffer
	writeLE(&enc, uint64(1710000000))     // InvoiceTs
	writeLE(&enc, uint32(420000000))      // Amount
	writeLE(&enc, uint16(1))              // AssetId
	enc.Write([]byte("PAYMENT_INTENT"))   // Intent string
	enc.Write(make([]byte, 62-enc.Len())) // Pad to 62
	body.Write(enc.Bytes())

	body.Write([]byte{0x00, 0x00}) // PreSat (VOID-114B)
	writeLE(&body, apidSatB)       // SatId
	body.Write(make([]byte, 4))    // PreSig (VOID-114B)

	// Signature scope: header + everything before the signature field.
	header := buildHeader(isSnlp, 178, apidSatB, false)
	bodyBeforeSig := body.Bytes()
	if len(bodyBeforeSig) != 106 {
		log.Fatalf("FATAL: Packet B pre-sig body is %d bytes, expected 106", len(bodyBeforeSig))
	}
	signature := ed25519.Sign(privKeyB, append(append([]byte{}, header...), bodyBeforeSig...))
	body.Write(signature)

	crc := getCRC(append(header, body.Bytes()...))
	writeLE(&body, crc)
	body.Write(make([]byte, 4)) // TailPad (VOID-114B: frame ÷8 / ÷64)

	return append(header, body.Bytes()...)
}

func genPacketH(isSnlp bool) []byte {
	// Packet H: Handshake (106 Bytes)
	var msg bytes.Buffer
	writeLE(&msg, uint16(900))        // SessionTtl
	writeLE(&msg, uint64(1710000200)) // Timestamp

	pubKey := make([]byte, 32)
	for i := range pubKey {
		pubKey[i] = byte(0xA0 + i)
	}
	msg.Write(pubKey) // EphPubKey

	// Sign
	signature := ed25519.Sign(privKeyB, msg.Bytes())
	msg.Write(signature) // No CRC for Handshake

	header := buildHeader(isSnlp, 106, apidSatB, false)
	return append(header, msg.Bytes()...)
}

func genPacketC(isSnlp bool) []byte {
	// Packet C: Receipt (98 Bytes)
	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})             // PadHead
	writeLE(&msg, uint64(1710000300))         // ExecTime
	writeLE(&msg, uint64(0xDEADBEEFCAFEBABE)) // EncTxId
	writeLE(&msg, uint8(1))                   // EncStatus
	msg.Write(make([]byte, 7))                // PadSig

	signature := ed25519.Sign(privKeyA, msg.Bytes())

	var fullBody bytes.Buffer
	fullBody.Write(msg.Bytes())
	fullBody.Write(signature)

	header := buildHeader(isSnlp, 98, apidSatA, false)
	crc := getCRC(append(header, fullBody.Bytes()...))
	writeLE(&fullBody, crc)
	fullBody.Write(make([]byte, 4)) // TailPad

	return append(header, fullBody.Bytes()...)
}

func genPacketD(isSnlp bool) []byte {
	// Packet D: Delivery (122 Bytes)
	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})     // PadHead
	writeLE(&msg, uint64(1710000400)) // DownlinkTs
	writeLE(&msg, apidSatB)           // SatBId

	payload := make([]byte, 98)
	for i := range payload {
		payload[i] = byte(0xE0 + i)
	}
	msg.Write(payload)

	header := buildHeader(isSnlp, 122, apidSatB, false)
	crc := getCRC(append(header, msg.Bytes()...))
	writeLE(&msg, crc)
	msg.Write(make([]byte, 6)) // Tail

	return append(header, msg.Bytes()...)
}

func genPacketAck(isSnlp bool) []byte {
	// Packet ACK: Command (114B CCSDS, 122B SNLP)
	tunnelSize := 88
	payloadLen := 114
	if isSnlp {
		tunnelSize = 96
		payloadLen = 122
	}

	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})     // PadA
	writeLE(&msg, uint32(0xCAFEBABE)) // TargetTxId
	writeLE(&msg, uint8(1))           // Status
	msg.Write([]byte{0x00})           // PadB

	writeLE(&msg, uint16(180))       // Azimuth
	writeLE(&msg, uint16(45))        // Elevation
	writeLE(&msg, uint32(437200000)) // Frequency
	writeLE(&msg, uint32(5000))      // DurationMs

	tunnel := make([]byte, tunnelSize)
	for i := range tunnel {
		tunnel[i] = byte(0xAA + i)
	}
	msg.Write(tunnel) // EncTunnel

	msg.Write([]byte{0x00, 0x00}) // PadC

	header := buildHeader(isSnlp, payloadLen, apidSatB, true)
	crc := getCRC(append(header, msg.Bytes()...))
	writeLE(&msg, crc)

	return append(header, msg.Bytes()...)
}

func genPacketL(isSnlp bool) []byte {
	// VOID-114B: Heartbeat body is still 34 bytes but field order has
	// changed so every critical field is naturally aligned. The legacy
	// `reserved[2]` field is dropped; its slot is now `pad_head`.
	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})     // PadHead (VOID-114B)
	writeLE(&msg, uint64(1710000500)) // EpochTs
	writeLE(&msg, uint32(100800))     // PressurePa
	writeLE(&msg, int32(515000000))   // LatFixed
	writeLE(&msg, int32(-128000))     // LonFixed
	writeLE(&msg, uint16(4100))       // VbattMv
	writeLE(&msg, int16(2300))        // TempC
	writeLE(&msg, uint16(600))        // GpsSpeedCms
	writeLE(&msg, uint8(3))           // SysState
	writeLE(&msg, uint8(8))           // SatLock

	header := buildHeader(isSnlp, 34, apidSatB, false)
	crc := getCRC(append(header, msg.Bytes()...))
	writeLE(&msg, crc)

	return append(header, msg.Bytes()...)
}

// ==============================================================================
// MAIN EXECUTION
// ==============================================================================

func main() {
	if err := os.MkdirAll(outputDir, 0755); err != nil {
		log.Fatalf("Failed to create output dir: %v", err)
	}

	generators := []struct {
		name string
		fn   func(bool) []byte
	}{
		{"packet_a_invoice", genPacketA},
		{"packet_b_payment", genPacketB},
		{"packet_h_handshake", genPacketH},
		{"packet_c_receipt", genPacketC},
		{"packet_d_delivery", genPacketD},
		{"packet_ack_command", genPacketAck},
		{"packet_l_heartbeat", genPacketL},
	}

	fmt.Printf("--- Generating Native Go Packets in '%s/' ---\n", outputDir)

	for _, g := range generators {
		// SNLP
		snlpData := g.fn(true)
		snlpPath := filepath.Join(outputDir, fmt.Sprintf("void_%s_snlp.bin", g.name))
		os.WriteFile(snlpPath, snlpData, 0644)
		fmt.Printf("[%s] SNLP: %d bytes -> %s\n", g.name, len(snlpData), snlpPath)

		// CCSDS
		ccsdsData := g.fn(false)
		ccsdsPath := filepath.Join(outputDir, fmt.Sprintf("void_%s_ccsds.bin", g.name))
		os.WriteFile(ccsdsPath, ccsdsData, 0644)
		fmt.Printf("[%s] CCSDS: %d bytes -> %s\n", g.name, len(ccsdsData), ccsdsPath)
	}

	fmt.Println("\n✅ Generation Complete. You may delete gen_packet.py.")
}
