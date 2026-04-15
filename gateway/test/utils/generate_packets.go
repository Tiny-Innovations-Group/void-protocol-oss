package main

import (
	"bytes"
	"crypto/ed25519"
	"encoding/binary"
	"encoding/hex"
	"flag"
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
 * Status:    Native Go Packet Generator (VOID-123 deterministic mode)
 * Desc:      Emits the 14 golden wire-format vectors consumed by the
 *            Go (VOID-124) and C++ (VOID-125) regression suites.
 *-------------------------------------------------------------------------*/

// ------------------------------------------------------------
// VOID-123 — deterministic golden vector inputs.
// Any change here invalidates every committed .bin under
// test/vectors/ and MUST be accompanied by a regenerated,
// reviewed vector set plus updated C++/Go test assertions.
// ------------------------------------------------------------
const (
	detEpochTsMs uint64 = 1710000100000
	detSatId     uint32 = 0xCAFEBABE
	detAmount    uint64 = 420000000
	detAssetId   uint16 = 1
	detSeedHex          = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
)

var (
	detPosVec = [3]float64{7010.0, -11990.0, 560.0}
	detVelVec = [3]float32{7.5, -0.2, 0.01}
	detPriv   ed25519.PrivateKey
)

// Wire-format constants
const (
	syncWord   uint32 = 0x1D01A5A5
	apidSatA   uint32 = 100
	apidSatB   uint32 = 101
	loraMaxLen        = 255

	// F-03 FIX (VOID-006): body-offset-0 magic discriminants for the
	// 122-byte collision zone. Hamming distance 4 between 0xD0 and
	// 0xAC — a single RF bit-flip cannot cross from one to the other.
	magicPacketD   uint8 = 0xD0
	magicPacketAck uint8 = 0xAC
)

func init() {
	seed, err := hex.DecodeString(detSeedHex)
	if err != nil {
		log.Fatalf("bad deterministic seed: %v", err)
	}
	detPriv = ed25519.NewKeyFromSeed(seed)
}

// ============================================================
// HELPERS
// ============================================================

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

// buildHeader constructs either a 14-byte SNLP or 6-byte CCSDS header.
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
	writeBE(&buf, uint16(0xC000))
	writeBE(&buf, uint16(payloadLen-1))

	if isSnlp {
		writeBE(&buf, uint32(0)) // 4-byte alignment pad
	}

	return buf.Bytes()
}

// ============================================================
// BODY GENERATORS — all deterministic (VOID-123)
// ============================================================

func genPacketA(isSnlp bool) []byte {
	// VOID-114B: Packet A body is 66 bytes (PadHead + fields + PreCrc + CRC).
	var body bytes.Buffer
	body.Write([]byte{0x00, 0x00})   // PadHead (VOID-114B)
	writeLE(&body, detEpochTsMs)     // EpochTs
	writeLE(&body, detPosVec[:])     // PosVec (24B)
	writeLE(&body, detVelVec[:])     // VelVec (12B)
	writeLE(&body, detSatId)         // SatId
	writeLE(&body, detAmount)        // Amount
	writeLE(&body, detAssetId)       // AssetId
	body.Write([]byte{0x00, 0x00})   // PreCrc (VOID-114B)

	header := buildHeader(isSnlp, 66, apidSatA, false)
	crc := getCRC(append(header, body.Bytes()...))
	writeLE(&body, crc)

	return append(header, body.Bytes()...)
}

func genPacketB(isSnlp bool) []byte {
	// VOID-110 + VOID-114B: body = 178 bytes. Signature covers
	// header + body[0..105]. Nonce is derived from (sat_id||epoch_ts),
	// never transmitted.
	var body bytes.Buffer
	body.Write([]byte{0x00, 0x00}) // PadHead
	writeLE(&body, detEpochTsMs)   // EpochTs
	writeLE(&body, detPosVec[:])   // PosVec (24B)

	// Structured enc_payload (62B) — plaintext under SNLP per spec.
	var enc bytes.Buffer
	writeLE(&enc, detEpochTsMs)         // InvoiceTs
	writeLE(&enc, uint32(detAmount))    // Amount (u32 slot)
	writeLE(&enc, detAssetId)           // AssetId
	enc.Write([]byte("PAYMENT_INTENT")) // Intent string
	enc.Write(make([]byte, 62-enc.Len()))
	body.Write(enc.Bytes())

	body.Write([]byte{0x00, 0x00}) // PreSat
	writeLE(&body, detSatId)       // SatId
	body.Write(make([]byte, 4))    // PreSig

	header := buildHeader(isSnlp, 178, apidSatB, false)
	bodyBeforeSig := body.Bytes()
	if len(bodyBeforeSig) != 106 {
		log.Fatalf("FATAL: Packet B pre-sig body is %d bytes, expected 106", len(bodyBeforeSig))
	}
	signature := ed25519.Sign(detPriv, append(append([]byte{}, header...), bodyBeforeSig...))
	body.Write(signature)

	crc := getCRC(append(header, body.Bytes()...))
	writeLE(&body, crc)
	body.Write(make([]byte, 4)) // TailPad (VOID-114B)

	return append(header, body.Bytes()...)
}

func genPacketH(isSnlp bool) []byte {
	// Packet H: Handshake (106B body).
	var msg bytes.Buffer
	writeLE(&msg, uint16(900))   // SessionTtl
	writeLE(&msg, detEpochTsMs)  // Timestamp

	pubKey := make([]byte, 32)
	for i := range pubKey {
		pubKey[i] = byte(0xA0 + i)
	}
	msg.Write(pubKey) // EphPubKey

	signature := ed25519.Sign(detPriv, msg.Bytes())
	msg.Write(signature)

	header := buildHeader(isSnlp, 106, apidSatB, false)
	return append(header, msg.Bytes()...)
}

func genPacketC(isSnlp bool) []byte {
	// Packet C: Receipt (98B body).
	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})             // PadHead
	writeLE(&msg, detEpochTsMs)               // ExecTime
	writeLE(&msg, uint64(0xDEADBEEFCAFEBABE)) // EncTxId
	writeLE(&msg, uint8(1))                   // EncStatus
	msg.Write(make([]byte, 7))                // PadSig

	signature := ed25519.Sign(detPriv, msg.Bytes())

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
	// Packet D: Delivery (122B body).
	// F-03: body[0] = 0xD0 magic, body[1] = 0x00 pad (absorbed from former pad_head[2]).
	var msg bytes.Buffer
	msg.Write([]byte{magicPacketD, 0x00}) // Magic + PadHead
	writeLE(&msg, detEpochTsMs)           // DownlinkTs
	writeLE(&msg, detSatId)       // SatBId

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
	// Packet ACK: 114B body (CCSDS) / 122B body (SNLP).
	tunnelSize := 88
	payloadLen := 114
	if isSnlp {
		tunnelSize = 96
		payloadLen = 122
	}

	// F-03: body[0] = 0xAC magic, body[1] = 0x00 pad (absorbed from former pad_a[2]).
	var msg bytes.Buffer
	msg.Write([]byte{magicPacketAck, 0x00}) // Magic + PadA
	writeLE(&msg, uint32(0xCAFEBABE))       // TargetTxId
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
	// Packet L (heartbeat / LoRa beacon): 34B body (VOID-114B).
	var msg bytes.Buffer
	msg.Write([]byte{0x00, 0x00})   // PadHead
	writeLE(&msg, detEpochTsMs)     // EpochTs
	writeLE(&msg, uint32(100800))   // PressurePa
	writeLE(&msg, int32(515000000)) // LatFixed
	writeLE(&msg, int32(-128000))   // LonFixed
	writeLE(&msg, uint16(4100))     // VbattMv
	writeLE(&msg, int16(2300))      // TempC
	writeLE(&msg, uint16(600))      // GpsSpeedCms
	writeLE(&msg, uint8(3))         // SysState
	writeLE(&msg, uint8(8))         // SatLock

	header := buildHeader(isSnlp, 34, apidSatB, false)
	crc := getCRC(append(header, msg.Bytes()...))
	writeLE(&msg, crc)

	return append(header, msg.Bytes()...)
}

// ============================================================
// MAIN — VOID-123 deterministic golden vector emission
// ============================================================

type packetSpec struct {
	file  string
	fn    func(bool) []byte
	ccsds int
	snlp  int
}

var goldenPackets = []packetSpec{
	{"packet_a.bin", genPacketA, 72, 80},
	{"packet_b.bin", genPacketB, 184, 192},
	{"packet_c.bin", genPacketC, 104, 112},
	{"packet_d.bin", genPacketD, 128, 136},
	{"packet_h.bin", genPacketH, 112, 120},
	{"packet_ack.bin", genPacketAck, 120, 136},
	{"packet_l.bin", genPacketL, 40, 48},
}

var tiers = []struct {
	name   string
	isSnlp bool
}{
	{"ccsds", false},
	{"snlp", true},
}

func main() {
	deterministic := flag.Bool("deterministic", false, "Emit VOID-123 golden wire-format vectors (required).")
	outDir := flag.String("out", "test/vectors", "Output directory for golden vectors.")
	flag.Parse()

	if !*deterministic {
		fmt.Fprintln(os.Stderr, "generate_packets: --deterministic is required (see VOID-123).")
		fmt.Fprintln(os.Stderr, "  Usage: go run gateway/test/utils/generate_packets.go --deterministic [--out test/vectors]")
		os.Exit(2)
	}

	if err := writeGoldenVectors(*outDir); err != nil {
		log.Fatalf("%v", err)
	}
}

func writeGoldenVectors(root string) error {
	for _, t := range tiers {
		dir := filepath.Join(root, t.name)
		if err := os.MkdirAll(dir, 0755); err != nil {
			return fmt.Errorf("mkdir %s: %w", dir, err)
		}
	}

	fmt.Printf("VOID-123: writing deterministic golden vectors to %s/\n", root)
	for _, p := range goldenPackets {
		for _, t := range tiers {
			data := p.fn(t.isSnlp)
			want := p.ccsds
			if t.isSnlp {
				want = p.snlp
			}
			if err := verifyVector(t.name, p.file, data, want); err != nil {
				return err
			}
			path := filepath.Join(root, t.name, p.file)
			if err := os.WriteFile(path, data, 0644); err != nil {
				return fmt.Errorf("write %s: %w", path, err)
			}
			fmt.Printf("  %-14s  %-6s  %4d bytes\n", p.file, t.name, len(data))
		}
	}
	fmt.Printf("VOID-123: 14 golden vectors written.\n")
	return nil
}

func verifyVector(tier, file string, data []byte, want int) error {
	n := len(data)
	if n != want {
		return fmt.Errorf("%s/%s: got %d bytes, want %d", tier, file, n, want)
	}
	if n%4 != 0 {
		return fmt.Errorf("%s/%s: %d bytes fails %%4==0 (32-bit alignment)", tier, file, n)
	}
	if n%8 != 0 {
		return fmt.Errorf("%s/%s: %d bytes fails %%8==0 (64-bit alignment)", tier, file, n)
	}
	if n > loraMaxLen {
		return fmt.Errorf("%s/%s: %d bytes exceeds LoRa %d-byte ceiling", tier, file, n, loraMaxLen)
	}
	return nil
}
