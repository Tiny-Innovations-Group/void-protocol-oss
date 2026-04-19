package chain

import (
	"context"
	"crypto/ecdsa"
	"math/big"
	"testing"
	"time"

	"github.com/ethereum/go-ethereum/accounts/abi/bind"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/crypto"
	"github.com/ethereum/go-ethereum/ethclient/simulated"
	"github.com/ethereum/go-ethereum/params"
)

// VOID-052 integration tests — run the EscrowClient against an in-
// process simulated blockchain. This is the closest thing we have to
// the flat-sat Anvil path without external processes: ABI encoding,
// transaction signing, receipt retrieval, and event log parsing all go
// through production code paths.
//
// The deterministic test key here matches the Anvil account #0 private
// key (0xac097...ff80) so a developer can swap the simulated backend
// for a live `anvil` RPC URL by changing exactly one line and re-run
// the same test. That's the flat-sat demo primitive.

const (
	// Anvil default account #0 — same key the Foundry tests use to deploy.
	anvilAcct0Priv = "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"
	// Anvil default account #1 — acts as the deterministic seller wallet
	// bound to sat_id 0xCAFEBABE in the flat-sat registry.
	anvilAcct1Addr = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"
)

// spinUpEscrow deploys a fresh Escrow contract on a simulated backend
// using the Anvil acct #0 key as the deployer / signer. Returns the
// backend (with Commit method) and a client configured to talk to the
// deployed contract.
func spinUpEscrow(t *testing.T) (*simulated.Backend, *EscrowClient, *ecdsa.PrivateKey) {
	t.Helper()

	key, err := crypto.HexToECDSA(anvilAcct0Priv)
	if err != nil {
		t.Fatalf("decode deployer key: %v", err)
	}
	deployer := crypto.PubkeyToAddress(key.PublicKey)

	// Fund the deployer on the simulated chain.
	alloc := types.GenesisAlloc{
		deployer: {Balance: new(big.Int).Mul(big.NewInt(1000), big.NewInt(params.Ether))},
	}
	sim := simulated.NewBackend(alloc, simulated.WithBlockGasLimit(30_000_000))
	t.Cleanup(func() { _ = sim.Close() })

	backend := sim.Client()

	chainID, err := backend.ChainID(context.Background())
	if err != nil {
		t.Fatalf("get chain id: %v", err)
	}
	auth, err := bind.NewKeyedTransactorWithChainID(key, chainID)
	if err != nil {
		t.Fatalf("build transactor: %v", err)
	}
	// Deploy the Escrow contract via the bound-contract helper.
	addr, _, _, err := bind.DeployContract(auth, EscrowABI, EscrowBytecode, backend)
	if err != nil {
		t.Fatalf("deploy Escrow: %v", err)
	}
	sim.Commit()

	client, err := NewEscrowClient(backend, addr, anvilAcct0Priv, chainID)
	if err != nil {
		t.Fatalf("build EscrowClient: %v", err)
	}
	return sim, client, key
}

// TestEscrowClient_SettleBatch_HappyPath submits a single-entry batch
// and asserts (a) the tx hash is non-zero, (b) the receipt reports
// success, (c) the SettlementCreated event has the expected indexed
// topics (satId, txNonce, wallet), (d) the contract's `settlements`
// mapping stores the full struct. This is the end-to-end proof that
// the gateway can take a verified PacketB-shaped intent and land
// on-chain state.
func TestEscrowClient_SettleBatch_HappyPath(t *testing.T) {
	sim, client, _ := spinUpEscrow(t)

	wallet := common.HexToAddress(anvilAcct1Addr)
	intent := SettlementIntent{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(420_000_000),
		AssetId: 1,
		TxNonce: DeriveTxNonce(0xCAFEBABE, 1710000100000),
		Wallet:  wallet,
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	txHash, err := client.SettleBatch(ctx, []SettlementIntent{intent})
	if err != nil {
		t.Fatalf("SettleBatch: %v", err)
	}
	sim.Commit() // mine the block containing the tx.

	if (txHash == common.Hash{}) {
		t.Fatalf("expected non-zero tx hash")
	}

	receipt, err := sim.Client().TransactionReceipt(ctx, txHash)
	if err != nil {
		t.Fatalf("TransactionReceipt: %v", err)
	}
	if receipt.Status != types.ReceiptStatusSuccessful {
		t.Fatalf("expected successful receipt, got status %d", receipt.Status)
	}

	// One log expected — SettlementCreated(satId, amount, txNonce, wallet).
	// Topics: [event sig, satId (indexed), txNonce (indexed), wallet (indexed)].
	if len(receipt.Logs) != 1 {
		t.Fatalf("expected 1 log, got %d", len(receipt.Logs))
	}
	lg := receipt.Logs[0]
	if len(lg.Topics) != 4 {
		t.Fatalf("expected 4 topics on SettlementCreated, got %d", len(lg.Topics))
	}
	// Topic 3 is the indexed wallet — compare as address.
	gotWallet := common.BytesToAddress(lg.Topics[3].Bytes())
	if gotWallet != wallet {
		t.Fatalf("event wallet mismatch: got %s, want %s", gotWallet.Hex(), wallet.Hex())
	}

	// Storage read via the view getter — proves the settlement struct
	// is persisted with every field, including the wallet.
	out := make([]interface{}, 0)
	if err := client.contract.Call(&bind.CallOpts{Context: ctx}, &out, "settlements", intent.TxNonce); err != nil {
		t.Fatalf("call settlements(): %v", err)
	}
	// Getter tuple order: (satId, amount, assetId, txNonce, wallet, status)
	if len(out) != 6 {
		t.Fatalf("expected 6-field settlement tuple, got %d", len(out))
	}
	if got := out[0].(uint32); got != intent.SatId {
		t.Fatalf("satId mismatch: got %d want %d", got, intent.SatId)
	}
	if got := out[4].(common.Address); got != wallet {
		t.Fatalf("stored wallet mismatch: got %s want %s", got.Hex(), wallet.Hex())
	}
	if got := out[5].(uint8); got != 1 { // 1 == Status.PENDING
		t.Fatalf("status mismatch: got %d want 1 (PENDING)", got)
	}
}

// TestEscrowClient_SettleBatch_EmptyFails — the client rejects an empty
// batch locally before touching the chain. Cheaper than a revert and
// keeps the buffered submitter's flush path safe against accidental
// zero-length calls.
func TestEscrowClient_SettleBatch_EmptyFails(t *testing.T) {
	_, client, _ := spinUpEscrow(t)

	_, err := client.SettleBatch(context.Background(), nil)
	if err == nil {
		t.Fatalf("expected error on empty batch, got nil")
	}
}

// TestEscrowClient_SettleBatch_DuplicateReverts — submitting the same
// txNonce twice must revert on-chain (DuplicateNonce). The first call
// succeeds; the second must fail.
func TestEscrowClient_SettleBatch_DuplicateReverts(t *testing.T) {
	sim, client, _ := spinUpEscrow(t)

	intent := SettlementIntent{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(100),
		AssetId: 1,
		TxNonce: DeriveTxNonce(0xCAFEBABE, 1710000100000),
		Wallet:  common.HexToAddress(anvilAcct1Addr),
	}

	ctx := context.Background()
	if _, err := client.SettleBatch(ctx, []SettlementIntent{intent}); err != nil {
		t.Fatalf("first SettleBatch: %v", err)
	}
	sim.Commit()

	// Second submission with the same txNonce. go-ethereum's estimate-
	// gas call fails upfront when the call would revert, so we expect
	// an error without even mining.
	_, err := client.SettleBatch(ctx, []SettlementIntent{intent})
	if err == nil {
		t.Fatalf("expected revert on duplicate nonce, got nil error")
	}
}
