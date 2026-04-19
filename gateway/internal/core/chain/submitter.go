package chain

import (
	"context"
	"log"
	"sync"
	"time"

	"github.com/ethereum/go-ethereum/common"
)

// IntentSubmitter is the narrow surface the BufferedSubmitter needs from
// an EscrowClient. Split out as an interface so unit tests can inject a
// mock that records every batch without spinning up a simulated chain.
type IntentSubmitter interface {
	SettleBatch(ctx context.Context, intents []SettlementIntent) (common.Hash, error)
}

// Enqueuer is the narrow surface consumed by the ingest handler. The
// handler owns exactly one operation — "this verified PacketB should
// become a settlement intent" — and should not see batching,
// timers, or RPC semantics. *BufferedSubmitter satisfies this
// interface. Tests can inject a mock that records enqueues.
type Enqueuer interface {
	Enqueue(intent SettlementIntent)
}

// Defaults documented alongside VOID-052 acceptance criteria.
const (
	DefaultMaxBatch = 10
	DefaultInterval = 5 * time.Second
	// A single on-chain retry after an initial failure, then we drop.
	// The ticket scope is explicit: "retry once, then log failure".
	// A durable queue + DLQ lands in Phase B.
	submitRetries = 2 // 1 initial + 1 retry
	// Upper bound on how long a submit+mining round-trip is allowed
	// before we consider the attempt failed. Anvil resolves in a
	// few ms; 30 s is generous for any future RPC hop latency.
	submitTimeout = 30 * time.Second
)

// BufferedSubmitter batches SettlementIntents and forwards them to an
// EscrowClient on whichever of these two triggers fires first:
//
//  1. The buffer reaches MaxBatch entries.
//  2. Interval elapses since the first entry of the current batch.
//
// Matches the VOID-052 AC: "Buffer up to 10 settlements (or flush on
// 5-second timer, whichever comes first). Submit batch to settle_batch
// via RPC. Handle RPC errors gracefully (retry once, then log failure)."
//
// The enqueue path is non-blocking in the common case (buffer not full).
// When the Nth entry triggers a flush, Enqueue blocks for the RPC
// round-trip — acceptable for flat-sat (single sat, single in-flight
// batch). A worker-pool variant can land when we move beyond one
// gateway instance (post-HAB).
type BufferedSubmitter struct {
	client   IntentSubmitter
	maxBatch int
	interval time.Duration

	mu     sync.Mutex
	buf    []SettlementIntent
	timer  *time.Timer
	closed bool
}

// NewBufferedSubmitter constructs a submitter with the given client and
// thresholds. Use DefaultMaxBatch / DefaultInterval for the flat-sat
// spec values.
func NewBufferedSubmitter(client IntentSubmitter, maxBatch int, interval time.Duration) *BufferedSubmitter {
	if maxBatch <= 0 {
		maxBatch = DefaultMaxBatch
	}
	if interval <= 0 {
		interval = DefaultInterval
	}
	return &BufferedSubmitter{
		client:   client,
		maxBatch: maxBatch,
		interval: interval,
		buf:      make([]SettlementIntent, 0, maxBatch),
	}
}

// Enqueue appends an intent to the current batch. If this entry makes
// the batch full, Enqueue synchronously triggers the flush (blocking
// for the RPC) so the caller sees back-pressure rather than silent
// overflow. On the empty→1 transition we (re)start the interval timer.
//
// Safe to call from multiple goroutines. A call after Stop is a no-op.
func (s *BufferedSubmitter) Enqueue(intent SettlementIntent) {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return
	}
	s.buf = append(s.buf, intent)
	// Start interval timer on the empty→1 transition so the buffer
	// doesn't linger past the promised flush deadline.
	if len(s.buf) == 1 {
		s.timer = time.AfterFunc(s.interval, s.onTimer)
	}
	if len(s.buf) < s.maxBatch {
		s.mu.Unlock()
		return
	}
	// Full: steal the buffer and flush outside the lock.
	batch := s.stealLocked()
	s.mu.Unlock()
	s.submit(batch)
}

// Flush forces an immediate submission of whatever is currently in the
// buffer. Useful in tests and at shutdown. Returns the number of
// intents submitted (or would have been submitted — failure still
// counts here).
func (s *BufferedSubmitter) Flush() int {
	s.mu.Lock()
	if s.closed {
		s.mu.Unlock()
		return 0
	}
	batch := s.stealLocked()
	s.mu.Unlock()
	if len(batch) == 0 {
		return 0
	}
	s.submit(batch)
	return len(batch)
}

// Stop releases the interval timer and flushes any pending intents. A
// submitter is single-use: after Stop, Enqueue is a no-op. Intended to
// be called on gateway shutdown so no settlements are silently dropped.
func (s *BufferedSubmitter) Stop() {
	s.mu.Lock()
	s.closed = true
	batch := s.stealLocked()
	s.mu.Unlock()
	if len(batch) > 0 {
		s.submit(batch)
	}
}

// stealLocked MUST be called with s.mu held. It empties the current
// buffer, stops the interval timer, and returns the stolen slice.
// Splitting it out keeps Enqueue / Flush / Stop consistent about the
// "take exclusive ownership of the pending batch" primitive.
func (s *BufferedSubmitter) stealLocked() []SettlementIntent {
	batch := s.buf
	s.buf = make([]SettlementIntent, 0, s.maxBatch)
	if s.timer != nil {
		s.timer.Stop()
		s.timer = nil
	}
	return batch
}

// onTimer fires on the AfterFunc goroutine when the interval elapses.
// If the buffer is still non-empty, flush it; otherwise no-op (a race
// where another flush drained the buffer is OK).
func (s *BufferedSubmitter) onTimer() {
	s.mu.Lock()
	if s.closed || len(s.buf) == 0 {
		s.mu.Unlock()
		return
	}
	batch := s.stealLocked()
	s.mu.Unlock()
	s.submit(batch)
}

// submit runs the retry loop described in the VOID-052 acceptance
// criteria: one initial attempt, one retry on error, then log + drop.
// Each attempt uses its own timeout-bounded context so a hung RPC
// doesn't stall subsequent flushes.
func (s *BufferedSubmitter) submit(batch []SettlementIntent) {
	if len(batch) == 0 {
		return
	}
	for attempt := 1; attempt <= submitRetries; attempt++ {
		ctx, cancel := context.WithTimeout(context.Background(), submitTimeout)
		txHash, err := s.client.SettleBatch(ctx, batch)
		cancel()
		if err == nil {
			log.Printf(
				"level=info event=settlebatch.ok intents=%d tx_hash=%s attempt=%d",
				len(batch), txHash.Hex(), attempt,
			)
			return
		}
		log.Printf(
			"level=warn event=settlebatch.fail intents=%d attempt=%d err=%q",
			len(batch), attempt, err.Error(),
		)
	}
	log.Printf(
		"level=error event=settlebatch.drop intents=%d retries_exhausted=true",
		len(batch),
	)
}
