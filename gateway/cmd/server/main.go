package main

import (
	"log"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"

	"github.com/gin-gonic/gin"
)

func main() {
	// Initialize Gin in release mode for speed (or debug for local)
	router := gin.Default()

	// Setup our API group
	v1 := router.Group("/api/v1")
	{
		// The C++ Bouncer will send HTTP POST requests here
		v1.POST("/ingest", handlers.IngestPacket)
	}

	// Start the server on port 8080
	log.Println("🚀 VOID Enterprise Gateway listening on :8080")
	if err := router.Run(":8080"); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}
