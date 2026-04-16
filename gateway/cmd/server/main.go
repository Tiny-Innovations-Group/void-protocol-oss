package main

import (
	"log"
	"os"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"

	"github.com/gin-gonic/gin"
)

func main() {
	// VOID-127: Alpha plaintext mode — when set, the gateway treats
	// enc_payload as cleartext (no ChaCha20 decrypt). Ed25519 signature
	// verification is unaffected.
	if os.Getenv("VOID_ALPHA_PLAINTEXT") == "1" {
		log.Println("WARNING: VOID-127 ALPHA PLAINTEXT MODE — ChaCha20 decryption DISABLED. DO NOT run in production.")
	}

	// Initialize Gin in release mode for speed (or debug for local)
	router := gin.Default()
	registry.Initialize()

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
