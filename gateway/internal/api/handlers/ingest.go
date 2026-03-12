package handlers

import (
	"bytes"
	"encoding/json"
	"io"
	"log"
	"net/http"

	void_protocol "github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/void_protocol"
	"github.com/gin-gonic/gin"
	"github.com/kaitai-io/kaitai_struct_go_runtime/kaitai"
)

// Helper function to pretty-print any struct as JSON
func prettyPrintStruct(name string, v interface{}) {
	b, err := json.MarshalIndent(v, "", "  ")
	if err == nil {
		log.Printf("\n--- %s ---\n%s\n-------------------\n", name, string(b))
	} else {
		log.Printf("Failed to marshal %s: %v", name, err)
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

	// 3. 🔍 PRINT HUMAN-READABLE PAYLOAD DATA
	// We use a type switch to cast the interface into the specific generated struct
	switch body := packet.Body.(type) {

	case *void_protocol.VoidProtocol_HeartbeatBody:
		// Convert scaled integers back to floats for reading
		lat := float64(body.LatFixed) / 10000000.0
		lon := float64(body.LonFixed) / 10000000.0
		temp := float64(body.TempC) / 100.0
		log.Printf("   💓 HEARTBEAT (L) | Temp: %.2f°C | Batt: %dmV | GPS: [%.4f, %.4f] | Speed: %d cm/s",
			temp, body.VbattMv, lat, lon, body.GpsSpeedCms)

	case *void_protocol.VoidProtocol_PacketABody:
		log.Printf("   🧾 INVOICE (A)   | Seller SatID: %d | Amount: %d | Asset ID: %d",
			body.SatId, body.Amount, body.AssetId)

	case *void_protocol.VoidProtocol_PacketBBody:
		log.Printf("   💰 PAYMENT (B)   | Buyer SatID: %d | Nonce: %d | Sig Length: %d bytes",
			body.SatId, body.Nonce, len(body.Signature))

	case *void_protocol.VoidProtocol_PacketCBody:
		log.Printf("   📜 RECEIPT (C)   | ExecTime: %d | TxID (Encrypted): %d | Status: %d",
			body.ExecTime, body.EncTxId, body.EncStatus)

	case *void_protocol.VoidProtocol_PacketHBody:
		log.Printf("   🤝 HANDSHAKE (H) | TTL: %d sec | Timestamp: %d",
			body.SessionTtl, body.Timestamp)

	case *void_protocol.VoidProtocol_Dispatch122:
		// 122-Byte packets have a collision, so Kaitai wraps them in a Dispatcher.
		// We do a nested switch to see if it's Delivery(D) or an ACK Command.
		switch inner := body.Content.(type) {
		case *void_protocol.VoidProtocol_PacketDBody:
			log.Printf("   📦 DELIVERY (D)  | Mule SatID: %d | Downlink TS: %d",
				inner.SatBId, inner.DownlinkTs)
		case *void_protocol.VoidProtocol_PacketAckBody:
			log.Printf("   ✅ COMMAND ACK   | Target TxID: %d | Status: %d | Freq: %d Hz",
				inner.TargetTxId, inner.Status, inner.RelayOps.Frequency)
		}

	case *void_protocol.VoidProtocol_PacketAckBody:
		// Handle 114-byte ACKs (CCSDS size)
		log.Printf("   ✅ COMMAND ACK   | Target TxID: %d | Status: %d | Freq: %d Hz",
			body.TargetTxId, body.Status, body.RelayOps.Frequency)

	default:
		log.Printf("   ❓ UNKNOWN PAYLOAD BODY TYPE")
	}

	// TODO: Verify Crypto Signatures here
	// TODO: Hit the Blockchain L2 Settlement here

	c.JSON(http.StatusOK, gin.H{
		"status":  "success",
		"message": "Packet parsed and accepted",
		"sat_id":  apid,
		"tier":    tier,
	})
}
