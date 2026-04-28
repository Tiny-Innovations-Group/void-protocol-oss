package handlers

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"io"
	"log"
	"math/big"
	"net/http"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/security"
	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol/protocol"
	"github.com/ethereum/go-ethereum/common"
	"github.com/gin-gonic/gin"
	"github.com/kaitai-io/kaitai_struct_go_runtime/kaitai"
)

// Submitter is the optional on-chain settlement enqueue hook (VOID-052).
// The server wires a *chain.BufferedSubmitter into this var at startup;
// tests inject a mock to assert enqueue behaviour without a simulated
// chain. nil means "no on-chain pipeline" — every other handler path
// still works (HTTP 200, sig verify, CRC gate), the intent is just not
// submitted. This keeps existing tests and the Kaitai parser suite
// independent of the chain package.
var Submitter chain.Enqueuer

// Helper function to pretty-print any struct as JSON -> useful for debugging complex payloads
func prettyPrintStruct(name string, v interface{}) {
	b, err := json.MarshalIndent(v, "", "  ")
	if err == nil {
		log.Printf("\n--- %s ---\n%s\n-------------------\n", name, string(b))
	} else {
		log.Printf("Failed to marshal %s: %v", name, err)
	}
}

// VOID-006 / F-03: body-offset-0 magic byte discriminants for the
// 122-byte collision zone. Enforced application-side here as
// defence-in-depth; the regenerated Kaitai parser (VOID-117) will
// additionally route dispatch_122 on this byte. A single RF bit-flip
// cannot cross between 0xD0 and 0xAC (Hamming distance 4).
const (
	magicPacketD   byte = 0xD0
	magicPacketAck byte = 0xAC
)

// enqueueSettlementIntent extracts {amount, asset_id} from the 62-byte
// InvoicePayload, looks up the seller wallet from the registry, derives
// the on-chain txNonce, and hands the result to the package-level
// Submitter. Safe to call only after Ed25519 verification has passed.
//
// Layout echoed from Protocol-spec-SNLP.md §4.3 / CCSDS §3.3:
//
//	00-07 epoch_ts  (not needed here — we use b.EpochTs instead)
//	08-31 pos_vec   (ignored)
//	32-43 vel_vec   (ignored)
//	44-47 sat_id    (ignored — b.SatId is the authoritative source)
//	48-55 amount    (u64 LE — extracted below)
//	56-57 asset_id  (u16 LE — extracted below)
//	58-61 crc32     (ignored — invoice's own CRC, not relevant to settle)
func enqueueSettlementIntent(b *protocol.VoidProtocol_PacketBBody) {
	const innerLen = 62
	if len(b.EncPayload) != innerLen {
		log.Printf("level=warn event=packetb.enqueue_skip reason=inner_payload_len len=%d want=%d",
			len(b.EncPayload), innerLen)
		return
	}
	satRec, exists := registry.GetSat(b.SatId)
	if !exists {
		// Unreachable in practice — VerifyPacketSignature rejects
		// unknown sat_ids upstream — but log + skip defensively so a
		// future refactor of the verify path can't silently submit
		// intents with no wallet.
		log.Printf("level=warn event=packetb.enqueue_skip reason=unknown_sat sat_id=%d", b.SatId)
		return
	}
	if satRec.Wallet == "" {
		log.Printf("level=warn event=packetb.enqueue_skip reason=empty_wallet sat_id=%d", b.SatId)
		return
	}
	amount := binary.LittleEndian.Uint64(b.EncPayload[48:56])
	assetID := binary.LittleEndian.Uint16(b.EncPayload[56:58])
	intent := chain.SettlementIntent{
		SatId:   b.SatId,
		Amount:  new(big.Int).SetUint64(amount),
		AssetId: assetID,
		TxNonce: chain.DeriveTxNonce(b.SatId, b.EpochTs),
		Wallet:  common.HexToAddress(satRec.Wallet),
	}
	Submitter.Enqueue(intent)
	log.Printf(
		"level=info event=packetb.enqueued sat_id=%d amount=%d asset_id=%d nonce=%s wallet=%s",
		b.SatId, amount, assetID, intent.TxNonce.String(), intent.Wallet.Hex(),
	)
}

// bodyOffsetZero returns the byte at body offset 0 for a parsed frame,
// or false if the raw buffer is too short to contain a body byte after
// the routing header. Header length is derived from the parsed tier
// (6B CCSDS, 14B SNLP) — both tiers are locked by VOID-113/114B.
func bodyOffsetZero(raw []byte, isSnlp bool) (byte, bool) {
	headerLen := 6
	if isSnlp {
		headerLen = 14
	}
	if len(raw) <= headerLen {
		return 0, false
	}
	return raw[headerLen], true
}

// handlePayloadBody processes the packet body and returns true if handled, false if unknown type.
// We pass rawData as a pointer to the slice to avoid unnecessary copying, though slices are already descriptors.
func handlePayloadBody(body interface{}, rawData *[]byte, c *gin.Context, packetSize int, isSnlp bool) bool {
	switch b := body.(type) {
	case *protocol.VoidProtocol_HeartbeatBody:
		lat := float64(b.LatFixed) / 10000000.0
		lon := float64(b.LonFixed) / 10000000.0
		temp := float64(b.TempC) / 100.0
		log.Printf("   💓 HEARTBEAT (L) | Temp: %.2f°C | Batt: %dmV | GPS: [%.4f, %.4f] | Speed: %d cm/s",
			temp, b.VbattMv, lat, lon, b.GpsSpeedCms)
		return true

	case *protocol.VoidProtocol_PacketABody:
		log.Printf("   🧾 INVOICE (A)   | Seller SatID: %d | Amount: %d | Asset ID: %d",
			b.SatId, b.Amount, b.AssetId)
		return true

	case *protocol.VoidProtocol_PacketBBody:
		log.Printf("   💰 PAYMENT (B)   | Buyer SatID: %d | EpochTs: %d | Sig Length: %d bytes",
			b.SatId, b.EpochTs, len(b.Signature.Raw))

		// VOID-127: When VOID_ALPHA_PLAINTEXT=1, enc_payload carries cleartext
		// (no ChaCha20 decryption needed). Ed25519 signature verification below
		// is unaffected — it covers the same scope in both modes (VOID-111).

		// VOID-110 + VOID-114B: Packet B body is 178 bytes (174 + 4-byte tail pad).
		//   • VOID-110 removed the wire nonce (derived from sat_id||epoch_ts).
		//   • VOID-114B added _pad_head[2] + _pre_sat[2] + _pre_sig[4] so every
		//     critical field is naturally aligned, and _tail_pad[4] so frame
		//     totals land at 184 (CCSDS ÷8) and 192 (SNLP ÷64 cache line).
		// Signature still covers (header + body[0..105]) = 106 body bytes.
		// Constants come from gateway/internal/void_protocol/constants.go —
		// shared with tests and the deterministic vector generator.
		if packetSize < void_protocol.PacketBBodyLen {
			log.Printf("level=warn event=packetb.bounds_fail sat_id=%d packet_size=%d min=%d",
				b.SatId, packetSize, void_protocol.PacketBBodyLen)
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet too short for signature verification"})
			return true // Return true because we successfully identified the type, even if it failed validation
		}

		// Signed region is (rawData header) + first PacketBSigScopeBody body bytes.
		bodyStart := packetSize - void_protocol.PacketBBodyLen
		headerBytes := (*rawData)[0:bodyStart]
		bodyPreSig := (*rawData)[bodyStart : bodyStart+void_protocol.PacketBSigScopeBody]
		messageBytes := append(append([]byte{}, headerBytes...), bodyPreSig...)

		// VOID-129: Full Ed25519 verify over the VOID-111 scope. Signature
		// failure returns HTTP 400 (malformed input — the frame failed its
		// integrity contract) with a structured log line so the field
		// operator can grep `event=packetb.sig_fail` out of the gateway
		// log stream. Not 401: 401 would imply an auth challenge the
		// sender could retry; a bad signature is a content defect.
		if err := security.VerifyPacketSignature(b.SatId, messageBytes, b.Signature.Raw); err != nil {
			log.Printf("level=warn event=packetb.sig_fail sat_id=%d err=%q", b.SatId, err.Error())
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Invalid Cryptographic Signature"})
			return true // ⛔ BOUNCE THE HACKER
		}
		// VOID-052: once structurally sound and sig-verified, hand the
		// settlement intent to the BufferedSubmitter (or a test mock).
		// nil Submitter = "no on-chain pipeline wired" — still a valid
		// config for unit tests of the parse/verify path alone.
		if Submitter != nil {
			enqueueSettlementIntent(b)
		}
		return true

	case *protocol.VoidProtocol_PacketCBody:
		log.Printf("   📜 RECEIPT (C)   | ExecTime: %d | TxID (Encrypted): %d | Status: %d",
			b.ExecTime, b.EncTxId, b.EncStatus)

		if packetSize < 98 {
			log.Printf("⛔ REJECTED: Packet C too short for signature verification")
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet too short for signature verification"})
			return true
		}

		// --- SECURITY CHECK (Packet C) ---
		// In Packet C (98 bytes), the signature starts at byte 26.
		// The "Message" is the first 26 bytes.
		sellerSatID := uint32(100) // Hardcoded for demo
		payloadStart := packetSize - 98
		messageBytes := (*rawData)[payloadStart : payloadStart+26]

		// FIX: Check err != nil instead of !bool
		if err := security.VerifyPacketSignature(sellerSatID, messageBytes, b.Signature.Raw); err != nil {
			log.Printf("⛔ REJECTED: %v", err) // Log the actual error from the verifier
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "Invalid Cryptographic Signature"})
			return true // ⛔ BOUNCE THE HACKER
		}

		return true

	case *protocol.VoidProtocol_PacketHBody:
		log.Printf("   🤝 HANDSHAKE (H) | TTL: %d sec | Timestamp: %d",
			b.SessionTtl, b.Timestamp)
		return true

	case *protocol.VoidProtocol_Dispatch122:
		// F-03: body offset 0 MUST carry the packet-type magic byte.
		// The magic is our defence against the dispatch_122 single-bit-
		// flip collision between Packet D and Packet ACK. The currently
		// deployed Kaitai parser still routes on the CCSDS Type bit;
		// this application-layer check rejects any mismatch between
		// the Type-bit routing and the magic byte, and also rejects
		// unknown magic values outright.
		magic, ok := bodyOffsetZero(*rawData, isSnlp)
		if !ok {
			log.Printf("⛔ REJECTED: dispatch_122 frame too short to read body magic")
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Dispatch_122 frame too short for magic byte"})
			return true
		}
		switch inner := b.Content.(type) {
		case *protocol.VoidProtocol_PacketDBody:
			if magic != magicPacketD {
				log.Printf("⛔ REJECTED: Packet D magic mismatch: got 0x%02X, want 0x%02X", magic, magicPacketD)
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet D magic byte mismatch (F-03)"})
				return true
			}
			// VOID-122: CRC-first — reject any bit-flip inside the CRC-
			// covered region before we log or trust business fields.
			crcOffset := packetSize - void_protocol.PacketDCrcOffsetFromEnd
			if err := void_protocol.ValidateFrameCRC32(*rawData, crcOffset); err != nil {
				log.Printf("level=warn event=packetd.crc_fail err=%q", err.Error())
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet D CRC mismatch (VOID-122)"})
				return true
			}
			log.Printf("   📦 DELIVERY (D)  | Mule SatID: %d | Downlink TS: %d",
				inner.SatBId, inner.DownlinkTs)
			return true
		case *protocol.VoidProtocol_PacketAckBodySnlp:
			if magic != magicPacketAck {
				log.Printf("⛔ REJECTED: Packet ACK (SNLP) magic mismatch: got 0x%02X, want 0x%02X", magic, magicPacketAck)
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet ACK magic byte mismatch (F-03)"})
				return true
			}
			// VOID-122: CRC-first — reject any bit-flip inside the CRC-
			// covered region before we log or trust business fields.
			crcOffset := packetSize - void_protocol.PacketAckCrcOffsetFromEnd
			if err := void_protocol.ValidateFrameCRC32(*rawData, crcOffset); err != nil {
				log.Printf("level=warn event=packetack.crc_fail err=%q", err.Error())
				c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet ACK CRC mismatch (VOID-122)"})
				return true
			}
			log.Printf("   ✅ COMMAND ACK   | Target TxID: %d | Status: %d | Freq: %d Hz",
				inner.TargetTxId, inner.Status, inner.RelayOps.Frequency)
			return true
		}
		return false

	case *protocol.VoidProtocol_PacketAckBodyCcsds:
		// F-03: CCSDS ACK (body 114B) is routed at the root, not via
		// dispatch_122. Magic-byte check still applies — body offset 0
		// must be 0xAC.
		magic, ok := bodyOffsetZero(*rawData, isSnlp)
		if !ok {
			log.Printf("⛔ REJECTED: Packet ACK frame too short to read body magic")
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet ACK frame too short for magic byte"})
			return true
		}
		if magic != magicPacketAck {
			log.Printf("⛔ REJECTED: Packet ACK magic mismatch: got 0x%02X, want 0x%02X", magic, magicPacketAck)
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet ACK magic byte mismatch (F-03)"})
			return true
		}
		// VOID-122: CRC-first gate — same semantics as the SNLP path
		// above, applied to the CCSDS-tier ACK routing.
		crcOffset := packetSize - void_protocol.PacketAckCrcOffsetFromEnd
		if err := void_protocol.ValidateFrameCRC32(*rawData, crcOffset); err != nil {
			log.Printf("level=warn event=packetack.crc_fail err=%q", err.Error())
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Packet ACK CRC mismatch (VOID-122)"})
			return true
		}
		log.Printf("   ✅ COMMAND ACK   | Target TxID: %d | Status: %d | Freq: %d Hz",
			b.TargetTxId, b.Status, b.RelayOps.Frequency)
		return true

	default:
		return false
	}
}

func IngestPacket(c *gin.Context) {
	// 1. Read the raw binary bytes from the request
	rawData, err := io.ReadAll(c.Request.Body)
	if err != nil || len(rawData) == 0 {
		c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Missing binary payload"})
		return
	}

	// 2. Pass the raw bytes into the Kaitai Parser
	stream := kaitai.NewStream(bytes.NewReader(rawData))
	packet := protocol.NewVoidProtocol()
	err = packet.Read(stream, nil, packet)

	log.Printf("📥 RAW INGEST: Received %d bytes", len(rawData))

	if err != nil {
		log.Printf("⛔ BOUNCE: Malformed Protocol Frame: %v", err)
		c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Invalid Void Protocol Frame"})
		return
	}

	packetSize := len(rawData)
	apid, _ := packet.GlobalApid()
	isSnlp, _ := packet.IsSnlp()

	// C-02: CCSDS 133.0-B-2 §4.1.3.1 — Version Number MUST be 0 (Version 1).
	// Kaitai `valid:` cannot enforce this on computed instances; enforced here.
	var ccsdsVersion int
	if isSnlp {
		ccsdsVersion, _ = packet.RoutingHeader.(*protocol.SnlpHeader).Ccsds.Version()
	} else {
		ccsdsVersion, _ = packet.RoutingHeader.(*protocol.CcsdsPrimaryHeader).Version()
	}
	if ccsdsVersion != 0 {
		log.Printf("⛔ BOUNCE: Unknown CCSDS version %d — only Version 1 (0b000) accepted", ccsdsVersion)
		c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Unknown CCSDS version — only Version 1 accepted"})
		return
	}

	tier := "Enterprise (CCSDS)"
	if isSnlp {
		tier = "Community (SNLP)"
	}

	log.Printf("📥 ADMIT: %s Frame | Size: %d bytes | SatID: %d", tier, packetSize, apid)

	// 3. 🔍 Process Payload Body and Security Checks
	// VOID-112: reject anything that didn't route to a known body type
	// (nil Body, truncated frame, or unsupported payload_len). This is
	// the final bounds gate — refusing before any downstream cast or
	// settlement logic runs.
	if !handlePayloadBody(packet.Body, &rawData, c, packetSize, isSnlp) {
		log.Printf("⛔ BOUNCE: Unknown or truncated payload body")
		if !c.IsAborted() {
			c.AbortWithStatusJSON(http.StatusBadRequest, gin.H{"error": "Unsupported or truncated Void Protocol frame"})
		}
		return
	}

	// TODO: Hit the Blockchain L2 Settlement here

	// Only send the success response if the context hasn't been aborted by a security failure
	if !c.IsAborted() {
		c.JSON(http.StatusOK, gin.H{
			"status":  "success",
			"message": "Packet parsed and accepted",
			"sat_id":  apid,
			"tier":    tier,
		})
	}
}
