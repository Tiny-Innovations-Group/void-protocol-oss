/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      proc_manager.h
 * Desc:      VOID-142 stage-1 — child process lifecycle for the operator
 *            console: anvil, gateway, (bouncer reserved for stage 2),
 *            forge contract deploy. Static storage only, POSIX.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_PROC_MANAGER_H
#define VOID_GROUND_STATION_UI_PROC_MANAGER_H

#include <cstddef>

// Child slots. PROC_BOUNCER is reserved for VOID-142 stage 2 (serial
// wiring) — the slot exists so the table shape is stable now.
enum proc_id_t {
    PROC_ANVIL = 0,
    PROC_GATEWAY,
    PROC_BOUNCER,
    PROC_COUNT
};

// One-time init: ignore SIGPIPE (PORT_SPEC §8 gotcha — a dying child
// must not take the UI down) and anchor the repo root (walk up for
// .git, mirroring the gateway's receipts anchor). Safe to call again.
void proc_init(void);

// Repo root containing .git, or "" when not found (paths then fail).
const char* proc_repo_root(void);

// Spawn `anvil` (repo-root CWD) and block until 127.0.0.1:8545 accepts
// a TCP connection (≤5 s). Idempotent: returns 0 immediately when the
// slot is already running. 0 on success, -1 on failure.
int proc_anvil_start(void);

// Spawn the gateway (`go run ./cmd/server`, CWD <root>/gateway) with
// VOID_ALPHA_PLAINTEXT=1 and VOID_ESCROW_ADDRESS=<proc_escrow()>.
// Requires proc_escrow() to be non-empty (contract loaded first).
// Returns 0 on spawn, -1 on failure / no contract loaded.
int proc_gateway_start(void);

// Restart the gateway on the current contract (APPLY / post-deploy
// re-point). Returns 0/-1.
int proc_gateway_restart(void);

// Pure forge worker: run `forge create src/Escrow.sol:Escrow` and copy
// the "Deployed to:" address into out_addr. Thread-safe by contract —
// it never touches the child slot table, so the render thread owns
// anvil/lifecycle and only the bounded pipe dance runs on the worker.
// Returns 0 on success (out_addr holds 0x+40hex), -1 on failure.
int proc_forge_deploy(char* out_addr, std::size_t out_cap);

// Validate + normalize ("0x" added when missing) + stash an operator-
// supplied escrow address. Returns 0, -1 when the input isn't a clean
// 0x?+40hex form (APPLY echoes INVALID, nothing else changes).
int proc_set_escrow(const char* addr);

// Current escrow address ("" until the first successful deploy).
const char* proc_escrow(void);

// 1 while the child is alive (reaps zombies via WNOHANG), else 0.
int proc_running(int id);

// SIGTERM the child's process group, wait ≤3 s, escalate to SIGKILL.
// No-op when the slot is idle. Returns 0 when fully reaped.
int proc_stop(int id);

// Stop every running child (STOP / EXIT paths). Returns 0 when all
// reaped, -1 if any needed SIGKILL.
int proc_stop_all(void);

// Non-blocking drain of the child's merged stdout/stderr pipe into
// buf (≤cap-1 bytes, NUL-terminated). Returns bytes read, 0 on EAGAIN,
// -1 on bad id / no pipe. Intended to be polled once per UI frame.
int proc_drain_log(int id, char* buf, std::size_t cap);

#endif // VOID_GROUND_STATION_UI_PROC_MANAGER_H
