package handlers

// VOID-142 — GET /api/v1/status: read-only operator-console status
// endpoint for the ImGui ground station (Panel 2 "Go Server").
//
// Exposes the counters the console's frozen readout needs (PORT_SPEC
// §5.2): packets in, sig verify ok/fail, intents queued, receipts
// PENDING/SENT, plus uptime and chain identity. Everything here is
// monotonic process-lifetime state — no resets on restart (the
// receipts counts come from the persisted store, so they survive;
// the counters are per-process by design).
//
// Counters are package-level atomics incremented at the exact log
// sites in ingest.go so the numbers can never drift from what the
// operator sees in the gateway log stream.

import (
	"net/http"
	"sync/atomic"
	"time"

	"github.com/gin-gonic/gin"
)

var (
	// packetsIn counts every /ingest POST that delivered a non-empty
	// body (parse success or failure — it arrived).
	packetsIn atomic.Int64
	// sigVerifyOK counts PacketB frames that passed Ed25519 verify.
	sigVerifyOK atomic.Int64
	// sigVerifyFail counts PacketB frames rejected on Ed25519 verify
	// (HTTP 400, event=packetb.sig_fail).
	sigVerifyFail atomic.Int64
	// intentsQueued counts settlement intents handed to the Submitter.
	intentsQueued atomic.Int64

	// bootTime anchors uptime_sec. Package-load time ≈ process boot;
	// close enough for an operator console.
	bootTime = time.Now()
)

// EscrowAddress is the hex Escrow contract address the on-chain
// pipeline is pointed at. Set once by cmd/server/main.go during
// startup (before the router serves), read by HandleStatus. Empty
// string = on-chain pipeline disabled.
var EscrowAddress string

// ResetStatusCounters zeroes the monotonic counters. Exported for
// tests that need deterministic assertions independent of other
// tests running earlier in the same process. Production code never
// calls this.
func ResetStatusCounters() {
	packetsIn.Store(0)
	sigVerifyOK.Store(0)
	sigVerifyFail.Store(0)
	intentsQueued.Store(0)
}

// HandleStatus is the GET /api/v1/status handler. Always 200 — the
// console distinguishes "gateway up" from "gateway down" by whether
// the poll gets any HTTP response at all. Receipt counts are 0 when
// the on-chain pipeline is disabled (EgressStore nil).
func HandleStatus(c *gin.Context) {
	pending, dispatched := 0, 0
	if EgressStore != nil {
		pending, dispatched = EgressStore.CountByStatus()
	}
	c.JSON(http.StatusOK, gin.H{
		"status":              "ok",
		"uptime_sec":          int64(time.Since(bootTime).Seconds()),
		"packets_in":          packetsIn.Load(),
		"sig_verify_ok":       sigVerifyOK.Load(),
		"sig_verify_fail":     sigVerifyFail.Load(),
		"intents_queued":      intentsQueued.Load(),
		"receipts_pending":    pending,
		"receipts_dispatched": dispatched,
		"chain_enabled":       Submitter != nil,
		"escrow_address":      EscrowAddress,
	})
}
