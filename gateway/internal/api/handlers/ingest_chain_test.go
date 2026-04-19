package handlers_test

import (
	"net/http"
	"sync"
	"testing"

	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/api/handlers"
	"github.com/Tiny-Innovations-Group/void-protocol-oss/gateway/internal/core/chain"
	"github.com/ethereum/go-ethereum/common"
)

// VOID-052 integration — tests the full ingest-handler → enqueue path.
// The chain package's own tests already cover SettleBatch against a
// simulated backend; this suite asserts the bridge between a parsed,
// verified PacketB and a chain.SettlementIntent handed to the
// BufferedSubmitter.

// mockEnqueuer records every intent passed to Enqueue. Safe for
// parallel calls from the handler goroutine.
type mockEnqueuer struct {
	mu      sync.Mutex
	intents []chain.SettlementIntent
}

func (m *mockEnqueuer) Enqueue(intent chain.SettlementIntent) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.intents = append(m.intents, intent)
}

func (m *mockEnqueuer) snapshot() []chain.SettlementIntent {
	m.mu.Lock()
	defer m.mu.Unlock()
	return append([]chain.SettlementIntent(nil), m.intents...)
}

func (m *mockEnqueuer) reset() {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.intents = nil
}

// withMockEnqueuer wires a fresh mock into the package-level Submitter
// and returns it + a cleanup that restores nil. Keeps tests self-
// contained — the Submitter slot is process-global.
func withMockEnqueuer(t *testing.T) *mockEnqueuer {
	t.Helper()
	mock := &mockEnqueuer{}
	handlers.Submitter = mock
	t.Cleanup(func() { handlers.Submitter = nil })
	return mock
}

// TestIngest_EnqueuesOnVerifiedPacketB — the deterministic PacketB
// golden vector in both tiers must produce exactly one enqueued intent
// with fully-populated fields drawn from the wire + registry lookup.
// This is the "offline → on-chain" bridge the TRL-4 demo depends on:
// a radio-layer success turns into an on-chain settlement candidate
// without any human in the loop.
func TestIngest_EnqueuesOnVerifiedPacketB(t *testing.T) {
	registerTestPubKey(t)
	mock := withMockEnqueuer(t)

	wantWallet := common.HexToAddress("0x70997970C51812dc3A010C7d01b50e0d17dc79C8")
	wantNonce := chain.DeriveTxNonce(0xCAFEBABE, 1710000100000)

	for _, tier := range []string{"ccsds", "snlp"} {
		t.Run(tier, func(t *testing.T) {
			mock.reset()
			r := newIngestRouter()
			raw := loadGoldenPacketB(t, tier)

			w := postIngest(t, r, raw)
			if w.Code != http.StatusOK {
				t.Fatalf("expected 200, got %d", w.Code)
			}

			intents := mock.snapshot()
			if len(intents) != 1 {
				t.Fatalf("expected 1 intent, got %d", len(intents))
			}
			got := intents[0]
			if got.SatId != 0xCAFEBABE {
				t.Errorf("SatId: got 0x%X, want 0xCAFEBABE", got.SatId)
			}
			if got.AssetId != 1 {
				t.Errorf("AssetId: got %d, want 1 (USDC)", got.AssetId)
			}
			if got.Amount.Uint64() != 420_000_000 {
				t.Errorf("Amount: got %d, want 420000000", got.Amount.Uint64())
			}
			if got.TxNonce.Cmp(wantNonce) != 0 {
				t.Errorf("TxNonce: got 0x%x, want 0x%x", got.TxNonce, wantNonce)
			}
			if got.Wallet != wantWallet {
				t.Errorf("Wallet: got %s, want %s", got.Wallet.Hex(), wantWallet.Hex())
			}
		})
	}
}

// TestIngest_DoesNotEnqueueOnSigFailure — if the signature doesn't
// verify, the handler rejects with HTTP 400 and no intent reaches the
// submitter. Without this invariant, a malformed PacketB could drive
// a bogus on-chain settlement via the gateway's signing EOA.
func TestIngest_DoesNotEnqueueOnSigFailure(t *testing.T) {
	registerTestPubKey(t)
	mock := withMockEnqueuer(t)
	r := newIngestRouter()

	// Flip a byte inside the Ed25519 signature (body offset 120 of
	// PacketB; absolute CCSDS offset 6+120 = 126).
	raw := loadGoldenPacketB(t, "ccsds")
	payload := flipByte(raw, 126)

	w := postIngest(t, r, payload)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("expected 400 on sig failure, got %d", w.Code)
	}
	if n := len(mock.snapshot()); n != 0 {
		t.Errorf("expected 0 enqueued intents after sig failure, got %d", n)
	}
}

// TestIngest_NoSubmitter_StillReturns200 — the chain pipeline is
// OPTIONAL at the handler layer. A server booted without wiring
// handlers.Submitter must still accept and verify valid PacketB
// traffic (e.g. for parser-only regression harnesses).
func TestIngest_NoSubmitter_StillReturns200(t *testing.T) {
	registerTestPubKey(t)
	// No withMockEnqueuer — Submitter stays nil.
	handlers.Submitter = nil

	r := newIngestRouter()
	raw := loadGoldenPacketB(t, "ccsds")

	w := postIngest(t, r, raw)
	if w.Code != http.StatusOK {
		t.Fatalf("expected 200 with nil Submitter, got %d", w.Code)
	}
}
