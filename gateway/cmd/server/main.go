package main

import (
	"context"
	"encoding/hex"
	"log"
	"math/big"
	"os"
	"path/filepath"
	"time"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/receipt"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/registry"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/ethclient"

	"github.com/gin-gonic/gin"
)

// Env-var surface. Missing VOID_ESCROW_ADDRESS disables the entire
// on-chain pipeline (submitter + receipt watcher) and reduces the
// gateway to parse + verify only — still useful for parser regression
// runs and partial demos.
//
//	VOID_ETH_RPC_URL         — default http://127.0.0.1:8545 (local Anvil)
//	VOID_ESCROW_ADDRESS      — deployed Escrow contract address (enables on-chain path)
//	VOID_GATEWAY_PRIVATE_KEY — hex secp256k1 key for signing settleBatch (Anvil acct #0 default)
//	VOID_RECEIPTS_PATH       — default gateway/data/receipts.json
//	VOID_RECEIPTS_SELLER_SEED — hex Ed25519 seed for PacketC signing (flat-sat demo default)
//	VOID_RECEIPTS_SELLER_APID — default 100 (apidSatA from golden vectors)
const (
	defaultEthRPCURL = "http://127.0.0.1:8545"
	// Anvil default acct #0 private key. Fine for flat-sat (local-only),
	// NEVER ok for testnet / mainnet — gated behind VOID_GATEWAY_PRIVATE_KEY
	// so a production deployment must inject its own.
	anvilAcct0Priv = "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"

	// Deterministic demo seed — matches gateway/test/utils/generate_packets.go
	// detSeedHex. Byte-exact PacketC emission depends on this being the
	// signer. Phase B VOID-121 replaces it with a PUF-backed per-seller
	// key vault.
	demoSellerSeedHex = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"

	defaultReceiptsPath = "gateway/data/receipts.json"
)

func main() {
	// VOID-127: Alpha plaintext mode — when set, the gateway treats
	// enc_payload as cleartext (no ChaCha20 decrypt). Ed25519 signature
	// verification is unaffected.
	if os.Getenv("VOID_ALPHA_PLAINTEXT") == "1" {
		log.Println("WARNING: VOID-127 ALPHA PLAINTEXT MODE — ChaCha20 decryption DISABLED. DO NOT run in production.")
	}

	// Context that lives for the server's lifetime. Background
	// goroutines (receipt watcher, etc.) watch this to shut down
	// cleanly when main exits.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	router := gin.Default()
	registry.Initialize()

	// VOID-127: seed the flat-sat demo pubkey so PacketB sig-verify
	// works against the same Ed25519 keypair the firmware signs with
	// (void-core/src/security_manager.cpp::begin under
	// VOID_ALPHA_PLAINTEXT). VOID-121 replaces this post-HAB.
	if err := registry.LoadFlatSatDemoKey(demoSellerSeedHex); err != nil {
		log.Fatalf("flat-sat demo key load failed: %v", err)
	}

	// --- On-chain pipeline (VOID-052 submitter + VOID-135a receipts). ---
	// Both share the same ethclient so we dial once.
	chainDeps, cleanup, err := maybeInitChain()
	if err != nil {
		log.Fatalf("on-chain init failed: %v", err)
	}
	if chainDeps != nil {
		defer cleanup()

		handlers.Submitter = chainDeps.submitter
		log.Printf("🔗 On-chain submitter wired | rpc=%s escrow=%s chain_id=%s",
			chainDeps.rpcURL, chainDeps.addr.Hex(), chainDeps.chainID.String())

		// VOID-135a: watch the Escrow contract for SettlementCreated
		// events and, for each, build+sign a PacketC and persist to
		// receipts.json. Runs in a background goroutine; dies with ctx.
		if err := startReceiptWatcher(ctx, chainDeps); err != nil {
			log.Fatalf("receipt watcher init failed: %v", err)
		}
	}

	v1 := router.Group("/api/v1")
	{
		v1.POST("/ingest", handlers.IngestPacket)

		// VOID-135b: bouncer drains pending receipts here, TXes each
		// PacketC via LoRa, then ACKs back. Routes always mount — if
		// handlers.EgressStore is nil (on-chain pipeline disabled)
		// the handlers return 503, so the bouncer gets a clear signal
		// rather than a 404.
		egress := v1.Group("/egress")
		egress.GET("/pending", handlers.HandleEgressPending)
		egress.POST("/ack", handlers.HandleEgressAck)
	}

	log.Println("🚀 VOID Enterprise Gateway listening on :8080")
	if err := router.Run(":8080"); err != nil {
		log.Fatalf("Server crashed: %v", err)
	}
}

// chainDeps groups the long-lived on-chain resources so the submitter
// and the receipt watcher can share a single dialed ethclient.
type chainDeps struct {
	rpcURL    string
	client    *ethclient.Client
	chainID   *big.Int
	addr      common.Address
	privKey   string
	submitter *chain.BufferedSubmitter
}

// maybeInitChain builds the shared on-chain deps (client + escrow +
// submitter) when VOID_ESCROW_ADDRESS is set, otherwise returns
// (nil, noop, nil). Returns an error on genuine failure (bad RPC url,
// bad key, unreachable chain).
func maybeInitChain() (*chainDeps, func(), error) {
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

	dialCtx, dialCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer dialCancel()

	client, err := ethclient.DialContext(dialCtx, rpcURL)
	if err != nil {
		return nil, nil, err
	}
	chainID, err := client.ChainID(dialCtx)
	if err != nil {
		client.Close()
		return nil, nil, err
	}
	addr := common.HexToAddress(addrHex)
	escrow, err := chain.NewEscrowClient(client, addr, privKey, chainID)
	if err != nil {
		client.Close()
		return nil, nil, err
	}
	submitter := chain.NewBufferedSubmitter(escrow, chain.DefaultMaxBatch, chain.DefaultInterval)

	deps := &chainDeps{
		rpcURL:    rpcURL,
		client:    client,
		chainID:   chainID,
		addr:      addr,
		privKey:   privKey,
		submitter: submitter,
	}
	cleanup := func() {
		submitter.Stop()
		client.Close()
	}
	return deps, cleanup, nil
}

// startReceiptWatcher opens the receipts.json store, builds a Processor
// with the demo seller seed, and spins a Watcher goroutine that feeds
// the processor.
func startReceiptWatcher(ctx context.Context, deps *chainDeps) error {
	path := os.Getenv("VOID_RECEIPTS_PATH")
	if path == "" {
		path = defaultReceiptsPath
	}
	// Ensure parent dir exists — the server may run from a clean
	// checkout where gateway/data/ has never been touched.
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}

	store, err := receipt.NewStore(path)
	if err != nil {
		return err
	}
	// Store lifetime matches the server; intentionally NOT closed
	// here — the defer is set up by the caller's cleanup if needed.
	//
	// VOID-135b: expose the same store to the egress HTTP handlers so
	// the bouncer's GET /api/v1/egress/pending + POST /ack surface the
	// same PENDING/DISPATCHED state the watcher is populating.
	handlers.EgressStore = store

	seedHex := os.Getenv("VOID_RECEIPTS_SELLER_SEED")
	if seedHex == "" {
		seedHex = demoSellerSeedHex
		log.Println("⚠️  VOID_RECEIPTS_SELLER_SEED not set — using demo Ed25519 seed (flat-sat DEMO ONLY).")
	}
	seed, err := hex.DecodeString(seedHex)
	if err != nil {
		return err
	}

	proc, err := receipt.NewProcessor(store, receipt.SellerAPID100, seed)
	if err != nil {
		return err
	}

	watcher, err := chain.NewWatcher(deps.client, deps.addr, 0, chain.DefaultWatchInterval)
	if err != nil {
		return err
	}

	log.Printf("📒 Receipt watcher wired | receipts=%s interval=%s",
		path, chain.DefaultWatchInterval)

	go func() {
		err := watcher.Run(ctx, func(ev chain.SettlementEvent) {
			if err := proc.Handle(ev); err != nil {
				log.Printf("level=error event=receipt.handle tx_hash=%s err=%q",
					ev.TxHash.Hex(), err.Error())
				return
			}
			log.Printf("level=info event=receipt.persisted sat_id=%d payment_id=%s tx_hash=%s block=%d wallet=%s",
				ev.SatID, ev.TxNonce.String(), ev.TxHash.Hex(), ev.BlockNumber, ev.Wallet.Hex())
		})
		if err != nil && err != context.Canceled {
			log.Printf("level=error event=receipt.watcher_exit err=%q", err.Error())
		}
	}()

	return nil
}
