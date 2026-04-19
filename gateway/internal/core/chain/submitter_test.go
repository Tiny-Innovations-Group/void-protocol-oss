package chain

import (
	"context"
	"errors"
	"math/big"
	"sync"
	"testing"
	"time"

	"github.com/ethereum/go-ethereum/common"
)

// mockSubmitter records every SettleBatch call. Configurable to return
// an error N times before succeeding (for retry-policy tests).
type mockSubmitter struct {
	mu       sync.Mutex
	batches  [][]SettlementIntent
	failFor  int        // number of calls to fail before succeeding
	failErr  error      // error to return while failFor > 0
	called   int        // total calls (success + fail)
	nextHash common.Hash
	callCh   chan struct{} // optional: pulsed on every call (tests that want to wait)
}

func (m *mockSubmitter) SettleBatch(_ context.Context, intents []SettlementIntent) (common.Hash, error) {
	m.mu.Lock()
	m.called++
	call := m.called
	batches := append([][]SettlementIntent(nil), m.batches...)
	fail := m.failFor > 0
	if fail {
		m.failFor--
	} else {
		// Only record successful batches — matches how production log
		// reads "settled" as "we got a tx hash".
		m.batches = append(batches, append([]SettlementIntent(nil), intents...))
	}
	ch := m.callCh
	m.mu.Unlock()
	if ch != nil {
		ch <- struct{}{}
	}
	if fail {
		return common.Hash{}, m.failErr
	}
	_ = call
	return m.nextHash, nil
}

func (m *mockSubmitter) Batches() [][]SettlementIntent {
	m.mu.Lock()
	defer m.mu.Unlock()
	return append([][]SettlementIntent(nil), m.batches...)
}

func (m *mockSubmitter) CallCount() int {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.called
}

func sampleIntent(i int) SettlementIntent {
	return SettlementIntent{
		SatId:   0xCAFEBABE,
		Amount:  big.NewInt(int64(420_000_000 + i)),
		AssetId: 1,
		TxNonce: DeriveTxNonce(0xCAFEBABE, uint64(1710000100000+i)),
		Wallet:  common.HexToAddress("0x70997970C51812dc3A010C7d01b50e0d17dc79C8"),
	}
}

// TestEnqueue_FillingBufferFlushesImmediately — AC: "Buffer up to 10
// settlements". The 10th Enqueue must flush synchronously; we must not
// wait for the 5s timer to drain a full buffer.
func TestEnqueue_FillingBufferFlushesImmediately(t *testing.T) {
	mock := &mockSubmitter{nextHash: common.HexToHash("0xdead")}
	s := NewBufferedSubmitter(mock, 10, 5*time.Second)
	defer s.Stop()

	for i := 0; i < 10; i++ {
		s.Enqueue(sampleIntent(i))
	}

	// The 10th call should have flushed synchronously. No sleep needed.
	if got := mock.CallCount(); got != 1 {
		t.Fatalf("expected exactly 1 SettleBatch call after 10 enqueues, got %d", got)
	}
	batches := mock.Batches()
	if len(batches) != 1 || len(batches[0]) != 10 {
		t.Fatalf("expected one batch of 10 intents, got %+v", batches)
	}
}

// TestEnqueue_PartialBatchFlushesOnTimer — AC: "flush on 5-second timer,
// whichever comes first". Use a short interval so the test is quick.
func TestEnqueue_PartialBatchFlushesOnTimer(t *testing.T) {
	mock := &mockSubmitter{
		nextHash: common.HexToHash("0xbeef"),
		callCh:   make(chan struct{}, 1),
	}
	s := NewBufferedSubmitter(mock, 10, 50*time.Millisecond)
	defer s.Stop()

	// Enqueue 3 entries — below MaxBatch, so only the timer can flush.
	for i := 0; i < 3; i++ {
		s.Enqueue(sampleIntent(i))
	}

	// Wait for the flush (bounded — fail fast if the timer doesn't fire).
	select {
	case <-mock.callCh:
		// fired
	case <-time.After(500 * time.Millisecond):
		t.Fatalf("timer did not fire within deadline")
	}

	if got := mock.CallCount(); got != 1 {
		t.Fatalf("expected 1 flush, got %d", got)
	}
	batches := mock.Batches()
	if len(batches) != 1 || len(batches[0]) != 3 {
		t.Fatalf("expected one batch of 3 intents, got %+v", batches)
	}
}

// TestEnqueue_RetriesOnceThenDrops — AC: "Handle RPC errors gracefully
// (retry once, then log failure)".
func TestEnqueue_RetriesOnceThenDrops(t *testing.T) {
	mock := &mockSubmitter{
		failFor: 2, // both attempts fail
		failErr: errors.New("rpc unreachable"),
	}
	s := NewBufferedSubmitter(mock, 1, 5*time.Second) // batch-size=1 so 1 enqueue flushes
	defer s.Stop()

	s.Enqueue(sampleIntent(0))

	if got := mock.CallCount(); got != 2 {
		t.Fatalf("expected exactly 2 attempts (1 initial + 1 retry), got %d", got)
	}
	if len(mock.Batches()) != 0 {
		t.Fatalf("expected no successful batches recorded, got %+v", mock.Batches())
	}
}

// TestEnqueue_RetrySucceedsOnSecondAttempt — when the first RPC fails but
// the retry succeeds, we end up with exactly 2 calls and 1 recorded
// batch.
func TestEnqueue_RetrySucceedsOnSecondAttempt(t *testing.T) {
	mock := &mockSubmitter{
		failFor:  1, // first attempt fails, second succeeds
		failErr:  errors.New("transient"),
		nextHash: common.HexToHash("0xfeed"),
	}
	s := NewBufferedSubmitter(mock, 1, 5*time.Second)
	defer s.Stop()

	s.Enqueue(sampleIntent(0))

	if got := mock.CallCount(); got != 2 {
		t.Fatalf("expected 2 attempts, got %d", got)
	}
	if len(mock.Batches()) != 1 {
		t.Fatalf("expected 1 successful batch after retry, got %+v", mock.Batches())
	}
}

// TestStop_FlushesRemainingBeforeExit — gateway shutdown must not silently
// drop queued intents. Enqueue 3, Stop, assert flush.
func TestStop_FlushesRemainingBeforeExit(t *testing.T) {
	mock := &mockSubmitter{nextHash: common.HexToHash("0xcafe")}
	s := NewBufferedSubmitter(mock, 10, 5*time.Second)

	for i := 0; i < 3; i++ {
		s.Enqueue(sampleIntent(i))
	}

	s.Stop()

	if got := mock.CallCount(); got != 1 {
		t.Fatalf("expected 1 flush on Stop, got %d", got)
	}
	batches := mock.Batches()
	if len(batches) != 1 || len(batches[0]) != 3 {
		t.Fatalf("expected one batch of 3 intents on Stop flush, got %+v", batches)
	}
}

// TestEnqueue_AfterStopIsNoop — once stopped, additional enqueues drop
// silently. No panic, no hang, no extra RPC.
func TestEnqueue_AfterStopIsNoop(t *testing.T) {
	mock := &mockSubmitter{nextHash: common.HexToHash("0xabc1")}
	s := NewBufferedSubmitter(mock, 10, 5*time.Second)
	s.Stop()

	s.Enqueue(sampleIntent(0))

	if got := mock.CallCount(); got != 0 {
		t.Fatalf("expected 0 calls after Stop, got %d", got)
	}
}

// TestDeriveTxNonce_IsDeterministicAndUnique — same inputs yield the
// same nonce; different inputs yield different nonces. Cross-check that
// the VOID-110 layout (sat_id << 64 | epoch_ts) is preserved.
func TestDeriveTxNonce_IsDeterministicAndUnique(t *testing.T) {
	a := DeriveTxNonce(0xCAFEBABE, 1710000100000)
	b := DeriveTxNonce(0xCAFEBABE, 1710000100000)
	if a.Cmp(b) != 0 {
		t.Fatalf("expected deterministic nonce, got %s vs %s", a.String(), b.String())
	}

	c := DeriveTxNonce(0xCAFEBABE, 1710000100001)
	if a.Cmp(c) == 0 {
		t.Fatalf("epoch delta should change the nonce")
	}

	d := DeriveTxNonce(0xCAFEBABF, 1710000100000)
	if a.Cmp(d) == 0 {
		t.Fatalf("sat_id delta should change the nonce")
	}

	// Spot-check layout: (sat_id << 64) | epoch_ts. For sat 0x2, epoch 0x3
	// we expect 0x20000000000000003.
	got := DeriveTxNonce(0x2, 0x3)
	want, _ := new(big.Int).SetString("20000000000000003", 16)
	if got.Cmp(want) != 0 {
		t.Fatalf("layout mismatch: got 0x%x, want 0x%x", got, want)
	}
}
