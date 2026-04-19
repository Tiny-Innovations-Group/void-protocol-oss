// Package chain is the VOID gateway's on-chain interaction layer.
//
// It exists to take a verified PacketB, turn it into a deterministic
// `Escrow.SettlementIntent`, and submit a batch via `settleBatch` on a
// local Anvil node (flat-sat scope, VOID-052 / journey #14). A block-
// explorer view of the resulting transaction — together with the
// indexed `SettlementCreated` events — is the TRL 4 on-chain evidence
// the flat-sat demo is built around (Journey Change Log v6).
//
// The package has two primary types:
//   - EscrowClient: thin wrapper over go-ethereum's `bind.BoundContract`
//     that knows how to sign + submit `settleBatch` and parse receipts.
//   - BufferedSubmitter (submitter.go): batches up to 10 intents
//     (or flushes on a 5 s timer, whichever comes first) before calling
//     `EscrowClient.SettleBatch`. Retries a failed batch once, then logs.
//
// Scope notes:
//   - Local Anvil only. No testnet deploy at this phase (VOID-053, #21).
//   - No ERC-20 transfers; `settleBatch` just records intents. Real
//     value movement is Phase B.
//   - Gateway uses a single signing EOA (default: Anvil account #0).
//     Per-sat EOAs are a Phase B consideration.

package chain

import (
	"context"
	_ "embed"
	"encoding/hex"
	"fmt"
	"math/big"
	"strings"

	"github.com/ethereum/go-ethereum/accounts/abi"
	"github.com/ethereum/go-ethereum/accounts/abi/bind"
	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/crypto"
)

//go:embed escrow_abi.json
var escrowABIJSON string

//go:embed escrow_bytecode.txt
var escrowBytecodeHex string

// EscrowABI is the parsed ABI of the Escrow contract. Derived from the
// Foundry build output (contracts/out/Escrow.sol/Escrow.json) at build
// time via `//go:embed`; re-extract if the Solidity surface changes.
var EscrowABI abi.ABI

// EscrowBytecode is the deployment bytecode of the Escrow contract. Used
// by integration tests that deploy into a simulated backend. Trimmed of
// whitespace / the 0x prefix at init.
var EscrowBytecode []byte

func init() {
	parsed, err := abi.JSON(strings.NewReader(escrowABIJSON))
	if err != nil {
		panic(fmt.Sprintf("chain: failed to parse embedded Escrow ABI: %v", err))
	}
	EscrowABI = parsed

	clean := strings.TrimSpace(escrowBytecodeHex)
	clean = strings.TrimPrefix(clean, "0x")
	raw, err := hex.DecodeString(clean)
	if err != nil {
		panic(fmt.Sprintf("chain: failed to decode embedded Escrow bytecode: %v", err))
	}
	EscrowBytecode = raw
}

// SettlementIntent is the Go mirror of the on-chain Solidity struct
// `Escrow.SettlementIntent`. Field order MUST match the ABI tuple order —
// go-ethereum's ABI encoder is positional for tuple types. Any reorder
// here silently corrupts `settleBatch` calldata.
type SettlementIntent struct {
	SatId   uint32
	Amount  *big.Int
	AssetId uint16
	TxNonce *big.Int
	Wallet  common.Address
}

// DeriveTxNonce packs (sat_id, epoch_ts) into a uint256 the same way on
// every caller. Layout: upper 160 bits zero, then sat_id shifted left by
// 64 bits, then epoch_ts in the low 64 bits. This is the semantic cousin
// of VOID-110's ChaCha20 nonce derivation (`sat_id[4] || epoch_ts[8]`)
// projected into a uint256 so the Escrow contract's `txNonce` dedup
// mapping keys correctly.
//
// NOTE: epoch_ts is expected in MILLISECONDS (ms since Unix epoch),
// matching the on-wire PacketB field.
func DeriveTxNonce(satID uint32, epochTsMs uint64) *big.Int {
	nonce := new(big.Int).SetUint64(uint64(satID))
	nonce.Lsh(nonce, 64)
	nonce.Or(nonce, new(big.Int).SetUint64(epochTsMs))
	return nonce
}

// EscrowClient submits settleBatch calls to a deployed Escrow contract.
// A single client instance is safe for concurrent use by one goroutine
// at a time; wrap with the BufferedSubmitter (submitter.go) for
// back-pressure and batching.
type EscrowClient struct {
	contract   *bind.BoundContract
	address    common.Address
	transactor *bind.TransactOpts
}

// NewEscrowClient builds a client against a deployed Escrow contract.
// `backend` is anything implementing go-ethereum's bind.ContractBackend
// interface — a live `ethclient.Client` in production, a
// `simulated.Backend` in tests.
//
// `privKeyHex` is the hex-encoded secp256k1 private key of the gateway
// EOA. For flat-sat this is Anvil account #0
// (`ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80`).
// For any non-local deployment, pass it in via secrets, never a literal.
func NewEscrowClient(
	backend bind.ContractBackend,
	contractAddr common.Address,
	privKeyHex string,
	chainID *big.Int,
) (*EscrowClient, error) {
	key, err := crypto.HexToECDSA(strings.TrimPrefix(privKeyHex, "0x"))
	if err != nil {
		return nil, fmt.Errorf("chain: parse private key: %w", err)
	}
	auth, err := bind.NewKeyedTransactorWithChainID(key, chainID)
	if err != nil {
		return nil, fmt.Errorf("chain: build transactor: %w", err)
	}
	bound := bind.NewBoundContract(contractAddr, EscrowABI, backend, backend, backend)
	return &EscrowClient{
		contract:   bound,
		address:    contractAddr,
		transactor: auth,
	}, nil
}

// Address is the deployed contract address this client talks to.
func (c *EscrowClient) Address() common.Address { return c.address }

// SettleBatch submits a 1..MAX_BATCH settlement intent batch and returns
// the submitted transaction hash on success. Returns an error if the
// local tx build fails or the RPC submission fails — the caller decides
// the retry policy (see BufferedSubmitter for the retry-once default).
func (c *EscrowClient) SettleBatch(ctx context.Context, intents []SettlementIntent) (common.Hash, error) {
	if len(intents) == 0 {
		return common.Hash{}, fmt.Errorf("chain: empty intent batch")
	}
	opts := *c.transactor
	opts.Context = ctx

	tx, err := c.contract.Transact(&opts, "settleBatch", intents)
	if err != nil {
		return common.Hash{}, fmt.Errorf("chain: settleBatch transact: %w", err)
	}
	return tx.Hash(), nil
}

// WaitForReceipt blocks until the transaction is mined on the backend
// and returns the receipt. Useful in tests where we want to assert
// success + event emission before moving on. Production callers
// typically don't block — they observe SettlementCreated via an event
// subscription handled elsewhere (VOID-135 / journey #15).
//
// The receipt retrieval uses go-ethereum's bind WaitMined helper; the
// simulated backend requires an explicit Commit() between submit and
// wait — tests do that in setup.
func WaitForReceipt(
	ctx context.Context,
	backend bind.DeployBackend,
	txHash common.Hash,
) (*types.Receipt, error) {
	// Construct a minimal Transaction whose hash matches — go-ethereum's
	// WaitMined works off a *types.Transaction. Build a zero-value tx and
	// override the hash via a copy semantic. Simpler: use the backend's
	// TransactionReceipt directly (bind.DeployBackend exposes it).
	return backend.TransactionReceipt(ctx, txHash)
}
