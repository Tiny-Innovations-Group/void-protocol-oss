package handlers_test

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"encoding/hex"
	"encoding/json"
	"io"
	"math/big"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/receipt"
	"github.com/ethereum/go-ethereum/accounts/abi/bind"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/crypto"
	"github.com/ethereum/go-ethereum/ethclient/simulated"
	"github.com/ethereum/go-ethereum/params"
	"github.com/gin-gonic/gin"
)

// VOID-135b E2E — the full flat-sat loop's gateway-side:
//
//   settleBatch on chain → SettlementCreated event → Watcher.Poll →
//   Processor.Handle → PENDING record in Store → GET /egress/pending
//   returns it → POST /egress/ack → DISPATCHED → GET /egress/pending empty.
//
// This is the "paying→paid" acceptance story for #15b's gateway side.
// The bouncer-side C++ poller lands next; this test pins the HTTP
// surface it will talk to.

const (
	e2eAnvilAcct0Priv = "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"
	e2eAnvilAcct1Addr = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"
	e2eDemoSeedHex    = "bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb"
)

// spinEscrow is a local harness duplicated from
// receipt/integration_test.go. Deploys Escrow on a simulated backend;
// returns the backend (for Commit) and a client (for SettleBatch).
// Duplicated rather than shared because exporting a test-only helper
// across packages adds more surface than the ~30 lines cost.
func spinEscrow(t *testing.T) (*simulated.Backend, *chain.EscrowClient, *ecdsa.PrivateKey) {
	t.Helper()

	key, err := crypto.HexToECDSA(e2eAnvilAcct0Priv)
	if err != nil {
		t.Fatalf("decode deployer key: %v", err)
	}
	deployer := crypto.PubkeyToAddress(key.PublicKey)

	alloc := types.GenesisAlloc{
		deployer: {Balance: new(big.Int).Mul(big.NewInt(1000), big.NewInt(params.Ether))},
	}
	sim := simulated.NewBackend(alloc, simulated.WithBlockGasLimit(30_000_000))
	t.Cleanup(func() { _ = sim.Close() })

	backend := sim.Client()
	ctx := context.Background()
	chainID, err := backend.ChainID(ctx)
	if err != nil {
		t.Fatalf("ChainID: %v", err)
	}
	auth, err := bind.NewKeyedTransactorWithChainID(key, chainID)
	if err != nil {
		t.Fatalf("transactor: %v", err)
	}
	addr, _, _, err := bind.DeployContract(auth, chain.EscrowABI, chain.EscrowBytecode, backend)
	if err != nil {
		t.Fatalf("deploy: %v", err)
	}
	sim.Commit()

	client, err := chain.NewEscrowClient(backend, addr, e2eAnvilAcct0Priv, chainID)
	if err != nil {
		t.Fatalf("EscrowClient: %v", err)
	}
	return sim, client, key
}

// newE2EEgressRouter wires both the VOID-052 ingest endpoint and the
// VOID-135b egress endpoints into the same gin engine, mirroring
// cmd/server/main.go. Only egress is exercised here.
func newE2EEgressRouter() *gin.Engine {
	gin.SetMode(gin.TestMode)
	r := gin.New()
	g := r.Group("/api/v1")
	egress := g.Group("/egress")
	egress.GET("/pending", handlers.HandleEgressPending)
	egress.POST("/ack", handlers.HandleEgressAck)
	return r
}

// TestE2E_SettleToEgressToAck — the full happy-path:
//
//   1. settleBatch on chain
//   2. Watcher.Poll picks up SettlementCreated
//   3. Processor.Handle writes a PENDING receipt to the Store
//   4. GET /api/v1/egress/pending returns the record
//   5. POST /api/v1/egress/ack flips DispatchStatus to DISPATCHED
//   6. GET /api/v1/egress/pending is now empty
//
// If this test goes red in future work, something in the
// chain-to-bouncer pipeline broke.
func TestE2E_SettleToEgressToAck(t *testing.T) {
	sim, client, _ := spinEscrow(t)

	// --- 1. submit settlement ---
	nonce := chain.DeriveTxNonce(0xCAFEBABE, 1710000100000)
	intent := chain.SettlementIntent{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(420_000_000),
		AssetId: 1,
		TxNonce: nonce,
		Wallet:  common.HexToAddress(e2eAnvilAcct1Addr),
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	txHash, err := client.SettleBatch(ctx, []chain.SettlementIntent{intent})
	if err != nil {
		t.Fatalf("SettleBatch: %v", err)
	}
	sim.Commit()

	// --- 2. poll the watcher once ---
	watcher, err := chain.NewWatcher(sim.Client(), client.Address(), 0, 0)
	if err != nil {
		t.Fatalf("NewWatcher: %v", err)
	}
	events, err := watcher.Poll(ctx)
	if err != nil {
		t.Fatalf("Poll: %v", err)
	}
	if len(events) != 1 {
		t.Fatalf("expected 1 event, got %d", len(events))
	}

	// --- 3. processor writes PENDING to store ---
	path := filepath.Join(t.TempDir(), "receipts.json")
	store, err := receipt.NewStore(path)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	t.Cleanup(func() { _ = store.Close() })

	seed, _ := hex.DecodeString(e2eDemoSeedHex)
	proc, err := receipt.NewProcessor(store, receipt.SellerAPID100, seed)
	if err != nil {
		t.Fatalf("NewProcessor: %v", err)
	}
	if err := proc.Handle(events[0]); err != nil {
		t.Fatalf("Handle: %v", err)
	}

	// --- wire the handler package's global store slot ---
	handlers.EgressStore = store
	t.Cleanup(func() { handlers.EgressStore = nil })
	r := newE2EEgressRouter()

	// --- 4. GET /pending returns the record ---
	req1 := httptest.NewRequest(http.MethodGet, "/api/v1/egress/pending", nil)
	w1 := httptest.NewRecorder()
	r.ServeHTTP(w1, req1)
	if w1.Code != http.StatusOK {
		t.Fatalf("GET #1: got %d, want 200", w1.Code)
	}
	var pending []receipt.Record
	if err := json.Unmarshal(w1.Body.Bytes(), &pending); err != nil {
		t.Fatalf("decode JSON: %v", err)
	}
	if len(pending) != 1 {
		t.Fatalf("pending #1: got %d, want 1", len(pending))
	}
	got := pending[0]
	if got.PaymentID != nonce.String() {
		t.Errorf("PaymentID: got %s, want %s", got.PaymentID, nonce.String())
	}
	if got.SettlementTxHash != txHash.Hex() {
		t.Errorf("SettlementTxHash: got %s, want %s",
			got.SettlementTxHash, txHash.Hex())
	}
	if got.DispatchStatus != receipt.StatusPending {
		t.Errorf("DispatchStatus: got %q, want %q",
			got.DispatchStatus, receipt.StatusPending)
	}
	if got.PacketCHex == "" {
		t.Errorf("PacketCHex empty — bouncer can't TX without the bytes")
	}

	// --- 5. POST /ack flips to DISPATCHED ---
	ackBody, _ := json.Marshal(map[string]string{
		"payment_id":         got.PaymentID,
		"settlement_tx_hash": got.SettlementTxHash,
	})
	req2 := httptest.NewRequest(http.MethodPost, "/api/v1/egress/ack",
		bytes.NewReader(ackBody))
	req2.Header.Set("Content-Type", "application/json")
	w2 := httptest.NewRecorder()
	r.ServeHTTP(w2, req2)
	if w2.Code != http.StatusOK {
		b, _ := io.ReadAll(w2.Body)
		t.Fatalf("POST /ack: got %d, want 200. body=%s", w2.Code, string(b))
	}

	// --- 6. GET /pending is now empty ---
	req3 := httptest.NewRequest(http.MethodGet, "/api/v1/egress/pending", nil)
	w3 := httptest.NewRecorder()
	r.ServeHTTP(w3, req3)
	if w3.Code != http.StatusOK {
		t.Fatalf("GET #2: got %d, want 200", w3.Code)
	}
	if s := bytes.TrimSpace(w3.Body.Bytes()); string(s) != "[]" {
		t.Errorf("GET #2 body: got %q, want []", string(s))
	}

	// Store still has the record (DISPATCHED, not removed).
	if !store.Has(nonce.String(), txHash.Hex()) {
		t.Errorf("Has=false after ACK — DISPATCHED records must persist")
	}
}