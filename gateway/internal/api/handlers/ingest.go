package handlers

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"net/http"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/security"
	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
	"github.com/gin-gonic/gin"
	"github.com/kaitai-io/kaitai_struct_go_runtime/kaitai"
)

// Helper function to pretty-print any struct as JSON -> useful for debugging complex payloads
func prettyPrintStruct(name string, v interface{}) {
	b, err := json.MarshalIndent(v, "", "  ")
	if err == nil {
		log.Printf("\n--- %s ---\n%s\n-------------------\n", name, string(b))
	} else {
		log.Printf("Failed to marshal %s: %v", name, err)
	}
}

// handlePayloadBody processes the packet body and returns true if handled, false if unknown type.
// We pass rawData as a pointer to the slice to avoid unnecessary copying, though slices are already descriptors.
func handlePayloadBody(body interface{}, rawData *[]byte, c *gin.Context, packetSize int) bool {
	switch b := body.(type) {
	case *void_protocol.VoidProtocol_HeartbeatBody:
		lat := float64(b.LatFixed) / 10000000.0
		lon := float64(b.LonFixed) / 10000000.0
		temp := float64(b.TempC) / 100.0
		log.Printf("   💓 HEARTBEAT (L) | Temp: %.2f°C | Batt: %dmV | GPS: [%.4f, %.4f] | Speed: %d cm/s",
			temp, b.VbattMv, lat, lon, b.GpsSpeedCms)
		return true

	case *void_protocol.VoidProtocol_PacketABody:
		log.Printf("   🧾 INVOICE (A)   | Seller SatID: %d | Amount: %d | Asset ID: %d",
			b.SatId, b.Amount, b.AssetId)
		return true

	case *void_protocol.VoidProtocol_PacketBBody:
		log.Printf("   💰 PAYMENT (B)   | Buyer SatID: %d | Nonce: %d | Sig Length: %d bytes",
			b.SatId, b.Nonce, len(b.Signature))

		if packetSize < 170 {
			log.Printf("⛔ REJECTED: Packet B too short for signature verification")
			c.JSON(http.StatusBadRequest, gin.H{"error": "Packet too short for signature verification"})
			return true // Return true because we successfully identified the type, even if it failed validation
		}

		// --- SECURITY CHECK (Packet B) ---
		// In Packet B (170 bytes), the signature starts at byte 102.
		// The "Message" we signed is the first 102 bytes of the payload.
		payloadStart := packetSize - 170
		messageBytes := (*rawData)[payloadStart : payloadStart+102]

		// FIX: Check err != nil instead of !bool
		if err := security.VerifyPacketSignature(b.SatId, messageBytes, b.Signature); err != nil {
			log.Printf("⛔ REJECTED: %v", err) // Log the actual error from the verifier
			c.JSON(http.StatusUnauthorized, gin.H{"error": "Invalid Cryptographic Signature"})
			return true // ⛔ BOUNCE THE HACKER
		}
		return true

	case *void_protocol.VoidProtocol_PacketCBody:
		log.Printf("   📜 RECEIPT (C)   | ExecTime: %d | TxID (Encrypted): %d | Status: %d",
			b.ExecTime, b.EncTxId, b.EncStatus)

		if packetSize < 98 {
			log.Printf("⛔ REJECTED: Packet C too short for signature verification")
			c.JSON(http.StatusBadRequest, gin.H{"error": "Packet too short for signature verification"})
			return true
		}

		// --- SECURITY CHECK (Packet C) ---
		// In Packet C (98 bytes), the signature starts at byte 26.
		// The "Message" is the first 26 bytes.
		sellerSatID := uint32(100) // Hardcoded for demo
		payloadStart := packetSize - 98
		messageBytes := (*rawData)[payloadStart : payloadStart+26]

		// FIX: Check err != nil instead of !bool
		if err := security.VerifyPacketSignature(sellerSatID, messageBytes, b.Signature); err != nil {
			log.Printf("⛔ REJECTED: %v", err) // Log the actual error from the verifier
			c.JSON(http.StatusUnauthorized, gin.H{"error": "Invalid Cryptographic Signature"})
			return true // ⛔ BOUNCE THE HACKER
		}

		return true

	case *void_protocol.VoidProtocol_PacketHBody:
		log.Printf("   🤝 HANDSHAKE (H) | TTL: %d sec | Timestamp: %d",
			b.SessionTtl, b.Timestamp)
		return true

	case *void_protocol.VoidProtocol_Dispatch122:
		switch inner := b.Content.(type) {
		case *void_protocol.VoidProtocol_PacketDBody:
			log.Printf("   📦 DELIVERY (D)  | Mule SatID: %d | Downlink TS: %d",
				inner.SatBId, inner.DownlinkTs)
			return true
		case *void_protocol.VoidProtocol_PacketAckBody:
			log.Printf("   ✅ COMMAND ACK   | Target TxID: %d | Status: %d | Freq: %d Hz",
				inner.TargetTxId, inner.Status, inner.RelayOps.Frequency)
			return true
		}
		return false

	case *void_protocol.VoidProtocol_PacketAckBody:
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
		c.JSON(http.StatusBadRequest, gin.H{"error": "Missing binary payload"})
		return
	}

	// 2. Pass the raw bytes into the Kaitai Parser
	stream := kaitai.NewStream(bytes.NewReader(rawData))
	packet := void_protocol.NewVoidProtocol()
	err = packet.Read(stream, nil, packet)

	if err != nil {
		log.Printf("⛔ BOUNCE: Malformed Protocol Frame: %v", err)
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid Void Protocol Frame"})
		return
	}

	packetSize := len(rawData)
	apid, _ := packet.GlobalApid()
	isSnlp, _ := packet.IsSnlp()

	tier := "Enterprise (CCSDS)"
	if isSnlp {
		tier = "Community (SNLP)"
	}

	log.Printf("📥 ADMIT: %s Frame | Size: %d bytes | SatID: %d", tier, packetSize, apid)

	// 3. 🔍 Process Payload Body and Security Checks
	if !handlePayloadBody(packet.Body, &rawData, c, packetSize) {
		log.Printf("   ❓ UNKNOWN PAYLOAD BODY TYPE")
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
