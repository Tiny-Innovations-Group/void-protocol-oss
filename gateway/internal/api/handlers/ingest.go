package handlers

import (
	"net/http"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/models"
	"github.com/gin-gonic/gin"
)

// IngestPacket receives the sanitized payload from the C++ Bouncer
func IngestPacket(c *gin.Context) {
	var invoice models.InnerInvoice

	// Bind the incoming JSON from the C++ Ground Station
	if err := c.ShouldBindJSON(&invoice); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid payload format"})
		return
	}

	// TODO: Pass this to internal/core/settlement to hit the blockchain

	// Send a success response back to the C++ Ground Station
	c.JSON(http.StatusOK, gin.H{
		"status":  "success",
		"message": "Packet accepted for L2 batching",
		"sat_id":  invoice.SatID,
	})
}
