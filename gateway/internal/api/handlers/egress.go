package handlers

import (
	"net/http"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/receipt"
	"github.com/gin-gonic/gin"
)

// VOID-135b — gateway-side egress HTTP endpoints.
//
// The bouncer polls these every ~1 s to drain pending receipts, LoRa-
// TXes each PacketC, then POSTs back an ACK so the gateway flips the
// record's DispatchStatus from PENDING to DISPATCHED. A gateway crash
// between the PacketC build and the bouncer's ACK leaves the record
// PENDING; on restart the bouncer polls and re-TXes. Safe-at-least-once.
//
// Transport direction: bouncer → gateway. Chosen because the bouncer
// already has a raw-socket HTTP client (ground-station/src/gateway_client.cpp),
// so polling reuses that plumbing — no HTTP server on the bouncer side.
//
// Wire shape:
//   GET  /api/v1/egress/pending     → 200 JSON array of receipt.Record
//                                     (bare array for minimal-JSON C++
//                                     parsing), capped at EgressPageSize.
//   POST /api/v1/egress/ack         → 200 on success / idempotent-success
//                                     Body: {"payment_id", "settlement_tx_hash"}
//                                     400 on bad JSON / missing fields
//                                     404 on unknown dedup key
//                                     503 if EgressStore isn't wired
// Non-goals for this endpoint (handled elsewhere):
//   - auth (flat-sat; bouncer and gateway share a local network)
//   - pagination cursor (page size 10 is enough for a solo sat; the
//     bouncer drains the queue in subsequent polls)

// EgressStore is the package-level *receipt.Store the egress handlers
// read/write. cmd/server/main.go sets this at startup when the receipt
// pipeline is configured. A nil value means the egress endpoints return
// 503 — still serving HTTP but the persistence layer isn't up yet.
var EgressStore *receipt.Store

// EgressPageSize caps the GET /pending response. Tuned for the flat-sat
// (single satellite, single-digit settlements in flight at once). Phase B
// may add a paginated cursor if the drain loop can't keep up.
const EgressPageSize = 10

// HandleEgressPending is the GET /api/v1/egress/pending handler.
// Returns up to EgressPageSize pending receipts (oldest first) as a
// bare JSON array. Always 200 when the store is configured (empty
// array on no pending); 503 when not configured.
func HandleEgressPending(c *gin.Context) {
	if EgressStore == nil {
		c.AbortWithStatusJSON(http.StatusServiceUnavailable,
			gin.H{"error": "egress pipeline not configured"})
		return
	}
	records := EgressStore.PendingRecords(EgressPageSize)
	if records == nil {
		records = []receipt.Record{} // make sure we emit "[]" not "null"
	}
	c.JSON(http.StatusOK, records)
}

// ackRequest is the body the bouncer POSTs on successful LoRa TX.
// Both fields are mandatory — the (payment_id, settlement_tx_hash)
// pair is the dedup key the store indexes by.
type ackRequest struct {
	PaymentID        string `json:"payment_id"`
	SettlementTxHash string `json:"settlement_tx_hash"`
}

// HandleEgressAck is the POST /api/v1/egress/ack handler.
// Flips PENDING → DISPATCHED for the supplied key. Idempotent — a
// second ACK for an already-DISPATCHED record returns 200, since the
// bouncer might retry its ACK if the first response was dropped.
func HandleEgressAck(c *gin.Context) {
	if EgressStore == nil {
		c.AbortWithStatusJSON(http.StatusServiceUnavailable,
			gin.H{"error": "egress pipeline not configured"})
		return
	}
	var req ackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.AbortWithStatusJSON(http.StatusBadRequest,
			gin.H{"error": "invalid JSON"})
		return
	}
	if req.PaymentID == "" || req.SettlementTxHash == "" {
		c.AbortWithStatusJSON(http.StatusBadRequest,
			gin.H{"error": "payment_id and settlement_tx_hash are required"})
		return
	}
	if err := EgressStore.MarkDispatched(req.PaymentID, req.SettlementTxHash); err != nil {
		if err == receipt.ErrUnknown {
			c.AbortWithStatusJSON(http.StatusNotFound,
				gin.H{"error": "unknown payment_id + settlement_tx_hash"})
			return
		}
		c.AbortWithStatusJSON(http.StatusInternalServerError,
			gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"status": "dispatched"})
}