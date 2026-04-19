package main

import (
	"context"
	"log"
	"os"
	"time"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/ethclient"

	"github.com/gin-gonic/gin"
)

// VOID-052 env-var surface. All three are optional — if any is missing
// the on-chain pipeline is NOT wired and the gateway runs as a parse +
// verify node only (useful for CI, parser regression, and partial
// demos).
//
//	VOID_ETH_RPC_URL         — default http://127.0.0.1:8545 (local Anvil)
//	VOID_ESCROW_ADDRESS      — deployed Escrow contract address (required)
//	VOID_GATEWAY_PRIVATE_KEY — hex-encoded secp256k1 key for signing
//	                           settleBatch txs (Anvil acct #0 for flat-sat)
const (
	defaultEthRPCURL = "http://127.0.0.1:8545"
	// Anvil default acct #0 private key. Fine for flat-sat (local-only),
	// NEVER ok for testnet / mainnet — gated behind VOID_GATEWAY_PRIVATE_KEY
	// so a production deployment must inject its own.
	anvilAcct0Priv = "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"
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

	// VOID-052: optionally wire the on-chain BufferedSubmitter. Missing
	// env vars are OK — handlers.IngestPacket gracefully skips enqueue
	// when Submitter stays nil.
	submitter, cleanup, err := maybeInitSubmitter()
	if err != nil {
		log.Fatalf("on-chain submitter init failed: %v", err)
	}
	if submitter != nil {
		handlers.Submitter = submitter
		defer cleanup()
	}

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

// maybeInitSubmitter returns (submitter, cleanup, err).
// Returns (nil, noop, nil) when VOID_ESCROW_ADDRESS is unset — that's
// the "no on-chain pipeline" path. Returns a wired submitter when all
// the plumbing succeeds; cleanup flushes pending intents + closes the
// RPC on shutdown.
func maybeInitSubmitter() (chain.Enqueuer, func(), error) {
	addrHex := os.Getenv("VOID_ESCROW_ADDRESS")
	if addrHex == "" {
		log.Println("⚠️  VOID_ESCROW_ADDRESS not set — on-chain pipeline disabled (gateway runs as parse+verify only).")
		return nil, func() {}, nil
	}
	rpcURL := os.Getenv("VOID_ETH_RPC_URL")
	if rpcURL == "" {
		rpcURL = defaultEthRPCURL
	}
	privKey := os.Getenv("VOID_GATEWAY_PRIVATE_KEY")
	if privKey == "" {
		privKey = anvilAcct0Priv
		log.Println("⚠️  VOID_GATEWAY_PRIVATE_KEY not set — using Anvil account #0 (flat-sat DEMO ONLY).")
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	client, err := ethclient.DialContext(ctx, rpcURL)
	if err != nil {
		return nil, nil, err
	}
	chainID, err := client.ChainID(ctx)
	if err != nil {
		client.Close()
		return nil, nil, err
	}
	escrow, err := chain.NewEscrowClient(client, common.HexToAddress(addrHex), privKey, chainID)
	if err != nil {
		client.Close()
		return nil, nil, err
	}
	submitter := chain.NewBufferedSubmitter(escrow, chain.DefaultMaxBatch, chain.DefaultInterval)

	log.Printf("🔗 On-chain submitter wired | rpc=%s escrow=%s chain_id=%s",
		rpcURL, addrHex, chainID.String())

	cleanup := func() {
		submitter.Stop()
		client.Close()
	}
	return submitter, cleanup, nil
}
