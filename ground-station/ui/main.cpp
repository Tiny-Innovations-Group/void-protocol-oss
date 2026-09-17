/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      VOID-141 operator console — full port of frozen mockup design
 *            (docs/VOID-141_IMGUI_PORT_SPEC.md). All panels live-wired
 *            (VOID-142 stages 1-2): child pipes → rings → panels; Panel 1
 *            RF legs + gate fed by the bouncer's stdout (stage 2).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <cstdio>
#include <cstring>
#include <ctime>
#include <atomic>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "proc_manager.h"
#include "log_store.h"
#include "pass_store.h"
#include "service_poller.h"
#include "receipts_tailer.h"
#include "heartbeats_tailer.h"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#endif
// GLFW drags in the system OpenGL headers (glViewport / glClear / glClearColor).
#include <GLFW/glfw3.h>

namespace {

// --- Window geometry (spec section 2) ---
constexpr float kWinW     = 1760.0f;
constexpr float kWinH     = 826.0f;
constexpr float kMargin   = 12.0f;
constexpr float kSpacing  = 12.0f;
constexpr float kPanelW   = 622.0f;
constexpr float kPanelH   = 342.0f;
constexpr float kDrawerW  = 480.0f;
constexpr float kDrawerH  = 720.0f;
constexpr float kStripY   = 728.0f;
constexpr float kStripH   = 98.0f;

// --- Theme (spec section 3, ImVec4 floats) ---
const ImVec4 kConsoleBg   = ImVec4(0.063f, 0.063f, 0.063f, 1.0f); // #101010
const ImVec4 kBorderCol   = ImVec4(0.247f, 0.247f, 0.282f, 1.0f); // #3f3f48
const ImVec4 kTextDim     = ImVec4(0.502f, 0.502f, 0.502f, 1.0f); // #808080
const ImVec4 kBtnCol      = ImVec4(0.141f, 0.271f, 0.431f, 1.0f); // #24456e
const ImVec4 kBtnHover    = ImVec4(0.259f, 0.592f, 0.980f, 1.0f); // #4297fa
const ImVec4 kBtnActive   = ImVec4(0.059f, 0.529f, 0.980f, 1.0f); // #0f87fa
const ImVec4 kDanger      = ImVec4(0.549f, 0.122f, 0.122f, 1.0f); // #8c1f1f
const ImVec4 kDangerHover = ImVec4(0.749f, 0.200f, 0.200f, 1.0f); // #bf3333
const ImVec4 kDangerActive= ImVec4(0.902f, 0.251f, 0.251f, 1.0f); // #e64040
const ImVec4 kInputBg     = ImVec4(0.114f, 0.184f, 0.290f, 1.0f); // #1d2f4a
const ImVec4 kInputFocus  = ImVec4(0.231f, 0.333f, 0.529f, 1.0f); // #3b5587
const ImVec4 kLogBg       = ImVec4(0.039f, 0.039f, 0.039f, 1.0f); // #0a0a0a
const ImVec4 kLogBorder   = ImVec4(0.165f, 0.165f, 0.188f, 1.0f); // #2a2a30
const ImVec4 kStateOk     = ImVec4(0.263f, 0.776f, 0.400f, 1.0f); // #43c666
const ImVec4 kTextAmber  = ImVec4(0.855f, 0.643f, 0.125f, 1.0f); // #daa420 SAT HEALTH [STALE]
const ImVec4 kMarkerIdle  = ImVec4(0.353f, 0.353f, 0.392f, 1.0f); // #5a5a64
const ImVec4 kBackdrop    = ImVec4(0.000f, 0.000f, 0.000f, 0.65f);

// --- Leg model (spec section 5.1) ---
enum class LegState : int {
    Idle = 0,
    Active,
    Ok,
    Fail,
};

struct Leg {
    const char* name;
    LegState    state;
};

Leg kLegs[] = {
    {"A    — Invoice",      LegState::Idle},
    {"B    — Payment",      LegState::Idle},
    {"ACK  — Sig Verified", LegState::Idle},
    {"SETTLE — On-Chain",   LegState::Idle},
    {"C    — Receipt",      LegState::Idle},
    {"D    — Delivery",     LegState::Idle},
    {"HB   — Heartbeat",    LegState::Idle},
};
constexpr size_t kLegCount = sizeof(kLegs) / sizeof(kLegs[0]);

// Leg positions (kLegs order) — VOID-142 stage-2 addresses legs by
// index (FSM + gateway milestone keywords know positions, not names).
constexpr size_t kLegA      = 0;
constexpr size_t kLegB      = 1;
constexpr size_t kLegAck    = 2;
constexpr size_t kLegSettle = 3;
constexpr size_t kLegC      = 4;
constexpr size_t kLegD      = 5;
constexpr size_t kLegHb     = 6;

const ImVec4& state_color(const LegState s) {
    switch (s) {
        case LegState::Active: return kBtnHover;
        case LegState::Ok:     return kStateOk;
        case LegState::Fail:   return kDangerActive;
        case LegState::Idle:
        default:               return kTextDim;
    }
}

// VOID-142 stage-2: leg-state setter, index addressed. Moved into the
// namespace now that real callers exist (rf_update_legs + the gateway
// milestone keywords in route_child_line).
void set_leg_state(const size_t idx, const LegState state) {
    if (idx >= kLegCount) return;
    kLegs[idx].state = state;
}

const char* kHelpRunbook =
    "1. Set CONTRACT VALUE        (amount for the next invoice)\n"
    "2. LOAD NEXT CONTRACT         deploys a fresh Escrow, re-points gateway\n"
    "3. START                      brings up Anvil - gateway - bouncer in order\n"
    "4. Watch Panel 1 / 3          RF comms live; chain logs settlements\n"
    "5. SEND ACK                   operator authorises the buyer payment\n"
    "6. VIEW RECEIPTS              read-only tail of receipts.json\n"
    "7. STOP / EXIT                graceful halt of run / whole stack";

const char* kHelpPanels =
    "1. RF Comms (Ground ↔ Sat) — everything sent over the radio:\n"
    "    ground-station radio ↔ satellite Heltecs. Invoice (A), Payment (B),\n"
    "    ACK, Receipt (C), Delivery (D), Heartbeat (HB) all appear here.\n"
    "    PASS HISTORY (right column) keeps every completed pass — streak\n"
    "    #, time, A→D duration; red RESET rows mark tamper rejects.\n"
    "2. Go Server (Gateway) — gateway health, verify counters, SAT HEALTH\n"
    "    live snapshot (per-satellite battery, temp, freshness), HTTP log.\n"
    "3. L2 Blockchain (Anvil) — mined blocks, settleBatch txs, SettlementCreated\n"
    "    events; newest at bottom.\n"
    "4. Operator Controls — the demo lifecycle lives here: load, start, stop.\n"
    "Side panel (SHOW PANEL) — EDIT CONTRACT / VIEW RECEIPTS / DEBUG /\n"
    "    HEARTBEATS. DEBUG is a read-only tap of the raw serial wire: every\n"
    "    line the board sent (←) and every line the ground wrote back (→),\n"
    "    each stamped HH:MM:SS, verbatim hex payloads included.\n"
    "    HEARTBEATS is the gateway-validated telemetry history — one row\n"
    "    per frame: time, APID, battery, temp, GPS lock, pressure,\n"
    "    coordinates. Panel 2's SAT HEALTH box shows the same data as a\n"
    "    live per-satellite snapshot with [OK]/[STALE]/[LOST] freshness.";

const char* kHelpStates =
    "[IDLE]   no activity yet\n"
    "[ACTIVE] leg currently in progress\n"
    "[OK]     leg completed successfully\n"
    "[FAIL]   CRC or signature failed — resets the pass counter";

const char* kHelpGate =
    "Passes 10/10 · Tamper rejects visible · Restart-safe:\n"
    "a full demo pass is one clean A → B → ACK → SETTLE → C → D run.\n"
    "10 consecutive passes = flat-sat alpha done.\n"
    "The dot strip fills as the streak grows; a tamper reject empties\n"
    "it and leaves a red RESET row in the history.";

// Set in main() right after the window exists — EXIT's real close path
// needs the handle (VOID-142 stage-1 wiring).
GLFWwindow* g_window = nullptr;

// --- Inert UI state (spec section 6.4) ---
bool g_drawer_open       = false;
bool g_help_open         = false;
bool g_help_just_opened  = false;
bool g_font_loaded       = false; // TTF font found (streak dots need it)
char g_last_action[64]     = "--";
char g_run_buf[16]         = "RUN 00:00:00";

// --- SNAP FULL (VOID-142): uniform render-scale state. Frozen
// geometry stays invariant; only world-rendered scale changes. ---
bool  g_full       = false;
float g_scale      = 1.0f;
int   g_saved_x    = 0;
int   g_saved_y    = 0;
int   g_saved_w    = 1760;
int   g_saved_h    = 826;
float g_base_w     = 1280.0f; // 1280 console-only / 1760 with side panel
inline float SC(const float v) { return v * g_scale; }

// Recompute scale from the current window rect; SHOW/HIDE PANEL calls
// this too so the console re-fits when the side panel appears.
void recompute_scale() {
    int w = 0, h = 0;
    glfwGetWindowSize(g_window, &w, &h);
    g_base_w = g_drawer_open ? 1760.0f : 1280.0f;
    g_scale  = (w > 0) ? (static_cast<float>(w) / g_base_w) : 1.0f;
    ImGui::GetIO().FontGlobalScale = g_scale;
}

void snap_full() {
    glfwGetWindowPos(g_window, &g_saved_x, &g_saved_y);
    int w = 0, h = 0;
    glfwGetWindowSize(g_window, &w, &h);
    g_saved_w = w;
    g_saved_h = h;
    glfwMaximizeWindow(g_window);
    g_full = true;
    recompute_scale();
}

void unsnap_full() {
    glfwRestoreWindow(g_window);
    // glfwRestoreWindow re-shows pre-maximize geometry on most WMs; the
    // explicit set restores saved size deterministically too.
    glfwSetWindowPos(g_window, g_saved_x, g_saved_y);
    glfwSetWindowSize(g_window, g_saved_w, g_saved_h);
    g_full  = false;
    g_scale = 1.0f;
    recompute_scale();
}

// --- Live service state (VOID-142 1c) ---
service_view_t  g_service = {};
receipts_view_t g_receipts = {};
heartbeats_view_t g_heartbeats = {}; // VOID-146: SAT HEALTH + history tap
log_ring_t      g_ring_http  = {};
log_ring_t      g_ring_chain = {};
log_ring_t      g_ring_rf    = {}; // VOID-142 stage-2: bouncer stdout
log_ring_t      g_ring_errors = {};
log_ring_t      g_ring_cmds   = {}; // executed forge queries (raw)
debug_ring_t    g_ring_debug = {}; // VOID-145: raw serial wire tap
double          g_last_poll  = 0.0;

// --- Panel-1 gate state (VOID-142 stage-2) ---
int  g_passes         = 0; // clean A→B→ACK→SETTLE→C→D runs (10 = gate)
int  g_tamper_rejects = 0; // [BOUNCER] ❌ drops (each resets the run)
char g_serial_port[64] = "/dev/cu.usbserial-0001"; // Panel-4 bouncer arg

// --- Panel-1 PASS HISTORY (console polish, session-only) ---
// Ring of per-pass records + reset markers, rendered in the column
// opposite the leg list. Passes keep their record after the legs
// reset — the delivery track the gate counter never showed.
pass_ring_t g_pass_ring = {}; // zero-init == pass_ring_reset state
bool        g_pass_armed = false; // A seen, waiting for the pass to close
double      g_pass_t0    = 0.0;   // ImGui clock at arm time (duration)

// "HH:MM:SS" wall clock now (local, reentrant, bounded). Rendered
// rows carry real-world times for the TRL 4 screen recording; the
// RUN clock stays ImGui-monotonic (session uptime).
void wall_clock_now(char* buf, const std::size_t cap) {
    if (buf == nullptr || cap == 0) return;
    const std::time_t now = std::time(nullptr);
    struct tm tm_buf;
    if (localtime_r(&now, &tm_buf) == nullptr) {
        buf[0] = '\0';
        return;
    }
    std::snprintf(buf, cap, "%02d:%02d:%02d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
}

// Errors filter tab. "" = ALL, else a ring tag ("gw" / "anvil" / "rf" / "ui").
const char* g_err_filter = "";

// Async deploy worker (VOID-142): forge runs detached so the UI never
// freezes. Result lands in g_deploy_state; tick_services() harvests it
// on the render thread (chain logic stays off the worker).
// 0 idle · 1 running · 2 done (g_deploy_rc holds 0/-1)
std::atomic<int> g_deploy_state{0};
int              g_deploy_rc = 0;
bool             g_chain_after_deploy = false;
char             g_deploy_addr[48] = "";

void deploy_worker() {
    g_deploy_rc = proc_forge_deploy(g_deploy_addr, sizeof(g_deploy_addr));
    g_deploy_state.store(2);
}

// 0 kicked off, -1 busy (a deploy is already running).
int start_async_deploy(const bool chain_after) {
    if (g_deploy_state.load() == 1) {
        return -1;
    }
    g_chain_after_deploy = chain_after;
    g_deploy_state.store(1);
    std::thread(deploy_worker).detach();
    return 0;
}

// Child-pipe line assembly: chunks arrive arbitrarily framed, we split
// complete '\n'-terminated lines here before ringing them.
char g_asm[PROC_COUNT][512];
std::size_t g_asm_len[PROC_COUNT] = {0, 0, 0};

// Rendered snapshots for the ring-backed log_box panels. Sized to hold
// all 64 ring lines (worst case 64×128 = 8192, plus margin).
char g_http_text[8448]  = "";
char g_chain_text[8448] = "";
char g_rf_text[8448]    = ""; // VOID-142 stage-2: Panel-1 RF log
char g_errors_text[8448] = "";
char g_cmds_text[8448]  = "";
// VOID-145: DEBUG tab snapshot — sized for all 64 debug lines at the
// full 512-char cap plus margin (64 × 512 = 32768).
char g_debug_text[33280] = "";

// VOID-142 stage-2: Panel-1 leg FSM — one bouncer stdout line in, leg
// states out. Triggers are the bouncer's exact printed literals
// (ground-station/src/main.cpp): [HARDWARE] invoice / payment lines,
// [BOUNCER] verify, [ACK] downlink emit, [EGRESS] receipt dispatch,
// [TELEMETRY] heartbeat and the [SAT-B] echo of the buyer's
// PACKET_D_RX diagnostic. Emoji literals are byte-matched (⚠ without
// VS16 still prefixes the ⚠️ form the bouncer prints). A clean
// A→B→ACK→SETTLE→C→D run closes on the D line: count it, then reset
// the commerce legs — HB is ambient and keeps its state (kHelpGate).
void rf_update_legs(const char* line) {
    if (line == nullptr) return;
    if (std::strstr(line, "Packet A (Invoice)") != nullptr) {
        set_leg_state(kLegA, LegState::Active);
        // Arm the pass timer once per run — a mid-run repeat A keeps
        // the original start (duration stays A→first-close).
        if (!g_pass_armed) {
            g_pass_armed = true;
            g_pass_t0    = ImGui::GetTime();
        }
    }
    if (std::strstr(line, "Received PACKET B") != nullptr) {
        set_leg_state(kLegA, LegState::Ok);
        set_leg_state(kLegB, LegState::Active);
    }
    if (std::strstr(line, "[BOUNCER] ✅") != nullptr) {
        set_leg_state(kLegB, LegState::Ok);
    }
    if (std::strstr(line, "[BOUNCER] ❌") != nullptr) {
        set_leg_state(kLegB, LegState::Fail);
        ++g_tamper_rejects;
        g_passes = 0; // FAIL resets the pass counter (kHelpStates)
        // RESET marker row — the zeroed counter now leaves a trace.
        char tbuf[12];
        wall_clock_now(tbuf, sizeof(tbuf));
        pass_ring_push(&g_pass_ring, true, 0, tbuf, 0.0f);
        g_pass_armed = false;
    }
    if (std::strstr(line, "[ACK] ✅") != nullptr) {
        set_leg_state(kLegAck, LegState::Ok);
    }
    if (std::strstr(line, "[ACK] ⚠") != nullptr) {
        set_leg_state(kLegAck, LegState::Active); // emit failed, retryable
    }
    if (std::strstr(line, "[EGRESS] ✅ Dispatched") != nullptr) {
        set_leg_state(kLegC, LegState::Ok);
    }
    if (std::strstr(line, "PACKET_D") != nullptr) {
        set_leg_state(kLegD, LegState::Ok);
        if (kLegs[kLegA].state == LegState::Ok &&
            kLegs[kLegB].state == LegState::Ok &&
            kLegs[kLegAck].state == LegState::Ok &&
            kLegs[kLegSettle].state == LegState::Ok &&
            kLegs[kLegC].state == LegState::Ok) {
            ++g_passes; // one clean A→B→ACK→SETTLE→C→D run
            // Pass record — the legs reset below, the history keeps
            // the track: streak #, completion time, A→D duration.
            char tbuf[12];
            wall_clock_now(tbuf, sizeof(tbuf));
            const float dur = (g_pass_armed)
                ? static_cast<float>(ImGui::GetTime() - g_pass_t0)
                : -1.0f; // un-armed close (stale FSM) → no duration
            pass_ring_push(&g_pass_ring, false, g_passes, tbuf, dur);
            g_pass_armed = false;
            set_leg_state(kLegA, LegState::Idle);
            set_leg_state(kLegB, LegState::Idle);
            set_leg_state(kLegAck, LegState::Idle);
            set_leg_state(kLegSettle, LegState::Idle);
            set_leg_state(kLegC, LegState::Idle);
            set_leg_state(kLegD, LegState::Idle);
        }
    }
    if (std::strstr(line, "Heartbeat forwarded") != nullptr) {
        set_leg_state(kLegHb, LegState::Ok);
    }
}

// Route one completed child-output line into the right rings, tagged
// by source ("gw" / "anvil" / "rf" / "ui"). "gw" lines fill the HTTP
// log; "anvil" fills the chain log; settlement keywords on a "gw" line
// mirror into the chain log too; "rf" (bouncer) lines fill the Panel-1
// RF log and drive its leg FSM; warn/error-ish lines from any source
// mirror into the error strip.
void route_child_line(const char* tag, const char* line) {
    if (tag == nullptr || line == nullptr) return;
    if (std::strcmp(tag, "gw") == 0) {
        log_ring_push(&g_ring_http, tag, line);
        if (std::strstr(line, "settlebatch") != nullptr ||
            std::strstr(line, "receipt.persisted") != nullptr ||
            std::strstr(line, "escrow") != nullptr ||
            std::strstr(line, "block") != nullptr) {
            log_ring_push(&g_ring_chain, tag, line);
        }
        // VOID-142 stage-2: gateway milestones drive Panel-1 legs.
        // settlebatch.ok (not the .fail/.drop variants) closes SETTLE;
        // receipt.persisted puts C in flight (EGRESS ✅ Dispatched
        // closes it).
        if (std::strstr(line, "settlebatch.ok") != nullptr) {
            set_leg_state(kLegSettle, LegState::Ok);
        }
        if (std::strstr(line, "receipt.persisted") != nullptr) {
            set_leg_state(kLegC, LegState::Active);
        }
    } else if (std::strcmp(tag, "anvil") == 0) {
        log_ring_push(&g_ring_chain, tag, line);
    } else if (std::strcmp(tag, "rf") == 0) {
        // VOID-145: raw serial wire taps ([SERIAL-RX]/[SERIAL-TX] echoes
        // from the bouncer) go to the DEBUG tab ONLY — early return keeps
        // them out of the Panel-1 log, the leg FSM (a raw PACKET_D_RX:
        // line contains the PACKET_D trigger and would re-fire the D leg)
        // and the errors mirror ([SAT-B] twins still feed it as before).
        if (std::strncmp(line, "[SERIAL-RX] ", 12) == 0) {
            char raw[kDebugLineCap];
            char ts[12];
            // VOID-145 follow-up: wall-clock stamp — the same clock the
            // PASS HISTORY rows use, so a raw wire line correlates 1:1
            // with the pass record that closed around it.
            wall_clock_now(ts, sizeof(ts));
            std::snprintf(raw, sizeof(raw), "%s ← %s", ts, line + 12);
            debug_ring_push(&g_ring_debug, raw);
            return;
        }
        if (std::strncmp(line, "[SERIAL-TX] ", 12) == 0) {
            char raw[kDebugLineCap];
            char ts[12];
            wall_clock_now(ts, sizeof(ts));
            std::snprintf(raw, sizeof(raw), "%s → %s", ts, line + 12);
            debug_ring_push(&g_ring_debug, raw);
            return;
        }
        // VOID-142 stage-2: bouncer stdout → Panel-1 RF log + leg FSM.
        log_ring_push(&g_ring_rf, tag, line);
        rf_update_legs(line);
    }
    if (std::strstr(line, "level=warn") != nullptr ||
        std::strstr(line, "level=error") != nullptr ||
        std::strstr(line, "REJECTED") != nullptr ||
        std::strstr(line, "sig_fail") != nullptr ||
        std::strstr(line, "failed") != nullptr ||
        std::strstr(line, "⛔") != nullptr ||
        std::strstr(line, "❌") != nullptr || // VOID-142 stage-2: bouncer drops
        std::strstr(line, "ERROR") != nullptr) {
        log_ring_push(&g_ring_errors, tag, line);
    }
}

// Drain each child's non-blocking pipe and split into lines.
void drain_child_pipes() {
    char chunk[257];
    for (int id = 0; id < PROC_COUNT; ++id) {
        int n = proc_drain_log(id, chunk, sizeof(chunk));
        if (n <= 0) {
            continue;
        }
        // VOID-142 stage-2: bouncer stdout carries the "rf" tag (Panel 1).
        const char* tag = (id == PROC_GATEWAY) ? "gw" :
                          (id == PROC_ANVIL)   ? "anvil" :
                          (id == PROC_BOUNCER) ? "rf" : "ui";
        for (int i = 0; i < n; ++i) {
            const char c = chunk[i];
            if (c == '\n') {
                g_asm[id][g_asm_len[id]] = '\0';
                route_child_line(tag, g_asm[id]);
                g_asm_len[id] = 0;
            } else if (g_asm_len[id] < sizeof(g_asm[id]) - 1) {
                g_asm[id][g_asm_len[id]++] = c;
            }
        }
    }
}

void tick_services() {
    drain_child_pipes();
    // Service polls at 2 Hz — cheap round trips, keeps the readout
    // fresh without throttling the frame rate.
    if (ImGui::GetTime() - g_last_poll >= 0.5) {
        service_poll_tick(&g_service);
        receipts_tail_tick(&g_receipts);
        heartbeats_tail_tick(&g_heartbeats); // VOID-146: incremental
        g_last_poll = ImGui::GetTime();
    }
    // Harvest a finished async deploy on the render thread (VOID-142).
    if (g_deploy_state.load() == 2) {
        g_deploy_state.store(0);
        if (g_deploy_rc == 0 && proc_set_escrow(g_deploy_addr) == 0) {
            char note[128];
            // Chain-after (START pressed with no contract): spawn the
            // gateway now. Plain LOAD: restart it when it was running.
            if (g_chain_after_deploy) {
                // VOID-142 stage-2: chain the bouncer behind the gateway
                // (serial field from Panel 4; empty = test mode). The
                // bounded note carries both outcomes.
                const int rc = proc_gateway_start();
                const int rb = (rc == 0) ? proc_bouncer_start(g_serial_port)
                                         : -1;
                std::snprintf(note, sizeof(note), "DEPLOY OK %s (%s)",
                              g_deploy_addr,
                              (rc != 0) ? "gateway FAILED" :
                              (rb == 0) ? "gateway+bouncer start"
                                        : "gateway start, bouncer FAILED");
            } else if (proc_running(PROC_GATEWAY) != 0) {
                const int rc = proc_gateway_restart();
                std::snprintf(note, sizeof(note), "DEPLOY OK %s (%s)",
                              g_deploy_addr,
                              (rc == 0) ? "gateway re-point" : "repoint FAILED");
            } else {
                std::snprintf(note, sizeof(note), "DEPLOY OK %s",
                              g_deploy_addr);
            }
            log_ring_push(&g_ring_errors, "ui", note);
            log_ring_push(&g_ring_cmds, "cmd", note);
            std::snprintf(g_last_action, sizeof(g_last_action), "%s", note);
        } else {
            log_ring_push(&g_ring_errors, "ui", "DEPLOY FAILED (forge)");
            log_ring_push(&g_ring_cmds, "cmd", "DEPLOY FAILED (forge)");
            std::snprintf(g_last_action, sizeof(g_last_action),
                          "DEPLOY FAILED (forge)");
        }
        g_chain_after_deploy = false;
    }
}

// ...

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// 10x10 square status marker, vertically centred on the current text line.
void draw_marker(const ImVec4& col) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float line_h = ImGui::GetTextLineHeight();
    const float y = p.y + ((line_h - SC(10.0f)) * 0.5f);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, y),
        ImVec2(p.x + SC(10.0f), y + SC(10.0f)),
        ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(SC(10.0f), 0.0f));
    ImGui::SameLine(0.0f, SC(7.0f));
}

void panel_title(const char* title, bool marker) {
    if (marker) {
        draw_marker(kMarkerIdle);
    }
    ImGui::TextUnformatted(title);
}

// Inset log window (spec section 6.1): dark bg, border, both-axis scroll.
// VOID-145: opt-in tail-follow — the imgui_demo console pattern: stay
// pinned to the newest line while the operator is already at the bottom;
// scrolling up cancels the pin (the pass-list SetScrollY variant yanks
// unconditionally — fine for 16 fixed rows, wrong for a live stream).
void log_box(const char* id, const char* content, float height,
             bool tail_follow = false) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild(id, ImVec2(0.0f, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(content);
    if (tail_follow &&
        ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

// Right-aligned CLEAR on a log label row: resets the ring (the
// per-frame log_ring_render snapshot re-empties the box) and zeroes
// the rendered snapshot as belt-and-braces. SmallButton uses zero
// frame padding, so its width is the label's text width.
void clear_button(const char* id, log_ring_t* ring, char* snap) {
    const float btn_w = ImGui::CalcTextSize("CLEAR").x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btn_w);
    if (ImGui::SmallButton(id)) {
        log_ring_reset(ring);
        if (snap != nullptr) {
            snap[0] = '\0';
        }
    }
}

// Column header pinned above the Panel-2 HTTP log box. Pipe characters
// sit exactly over gin's DefaultLogFormatter columns (non-TTY → no
// ANSI colors in the piped stdout): 6-char "[GIN] " prefix, 21-char
// timestamp, 3-wide status, 13-wide latency, 15-wide client IP, 7-wide
// method, then the path — e.g.
//   [GIN] 2026/09/13 - 15:04:05 | 200 |      1.234ms |       127.0.0.1 | POST     "/api/v1/ingest"
// Non-gin gateway lines (level=info events, packet dumps) appear in
// the box as-is beneath the header.
const char* kHttpLogHeader =
    "      TIMESTAMP             | CODE|       LATENCY |              IP | METHOD  PATH";

// Pinned column header above the HEARTBEATS table (VOID-146). Column
// starts line up with the row snprintf widths in render_side_panel:
// TIME@0 · APID@10 · VBATT@21 · TEMP@28 · GPS@36 · PRESSURE@40 ·
// LAT@51 · LON@62 (the Panel-2 gin-header pattern).
const char* kHbLogHeader =
    "TIME      APID       VBATT  TEMP    GPS PRESSURE   LAT        LON";

void begin_panel(const char* id, const float x, const float y) {
    ImGui::SetCursorPos(ImVec2(SC(x), SC(y)));
    ImGui::BeginChild(id, ImVec2(SC(kPanelW), SC(kPanelH)), true);
}

// Word-wrap helper for the NEXT QUERY preview: inserts '\n' BEFORE a
// non-space run would overflow `width` (hard-breaks only >width
// tokens). Bounded; always NUL-terminated.
void wrap_text(const char* src, char* dst, std::size_t cap,
               const std::size_t width) {
    if (dst == nullptr || cap == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;
    std::size_t used = 0;
    std::size_t col  = 0;
    for (std::size_t i = 0; src[i] != '\0' && used + 1 < cap; ++i) {
        char c = src[i];
        if (c == ' ') {
            // Word boundary: jump to a new line if the NEXT token
            // would pass the width.
            std::size_t tok = 0;
            while (src[i + 1 + tok] != '\0' && src[i + 1 + tok] != ' ') {
                ++tok;
            }
            if (col + 1 + tok > width) {
                dst[used++] = '\n';
                col = 0;
                continue; // skip the space itself
            }
            dst[used++] = ' ';
            ++col;
            continue;
        }
        if (col >= width) {
            dst[used++] = '\n';
            col = 0;
            if (c == '\n') { // should not happen, bound anyway
                continue;
            }
        }
        dst[used++] = (c == '\n') ? ' ' : c;
        ++col;
    }
    dst[used] = '\0';
}

void render_panel1() {
    begin_panel("PanelRfComms", kMargin, kMargin);
    panel_title("1. RF Comms (Ground ↔ Sat)", true);
    ImGui::Separator();

    // Two-column top zone: legs (left, group) + PASS HISTORY (right,
    // bordered child). The history keeps the per-pass track after the
    // legs reset on delivery; streak dots show live gate progress.
    // The child is exactly as tall as the leg block, so the LOG box
    // and gate strip below keep their frozen geometry.
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float cols_h = static_cast<float>(kLegCount) * line_h;

    ImGui::BeginGroup();
    const float char_w = ImGui::CalcTextSize(" ").x;
    const float state_x = 15.0f + (19.0f * char_w); // 19ch name column
    for (size_t i = 0; i < kLegCount; ++i) {
        ImGui::Bullet();
        ImGui::SameLine(0.0f, 7.0f);
        ImGui::TextUnformatted(kLegs[i].name);
        ImGui::SameLine();
        ImGui::SetCursorPosX(state_x);
        ImGui::TextColored(state_color(kLegs[i].state), "[%s]",
            (kLegs[i].state == LegState::Active) ? "ACTIVE" :
            (kLegs[i].state == LegState::Ok)     ? "OK" :
            (kLegs[i].state == LegState::Fail)   ? "FAIL" : "IDLE");
    }
    ImGui::EndGroup();

    // Right column starts beside the leg group (SameLine after
    // EndGroup aligns to the group's top line); width = all remaining
    // panel content to the right of the cursor.
    ImGui::SameLine();
    const float pass_w = ImGui::GetWindowContentRegionMax().x -
                         ImGui::GetCursorPosX();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild("PassCol", ImVec2(pass_w, cols_h), true);

    // Streak dots (10 slots = the gate). U+25CF/U+25CB need a real
    // TTF font — fall back to ASCII when only the default font loaded.
    const char* const dot_on  = g_font_loaded ? "●" : "#";
    const char* const dot_off = g_font_loaded ? "○" : "-";
    ImGui::TextDisabled("PASS HISTORY (newest at bottom)");
    for (int i = 0; i < 10; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::TextColored((i < g_passes) ? kStateOk : kTextDim, "%s",
                           (i < g_passes) ? dot_on : dot_off);
    }
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextDisabled("%d/10", g_passes);

    // Scrollable rows: green = completed pass (#, time, A→D duration),
    // red = tamper-reject RESET marker. Bounded by the 16-record ring.
    ImGui::BeginChild("PassList",
                      ImVec2(0.0f, ImGui::GetContentRegionAvail().y), false);
    for (std::size_t back = g_pass_ring.count; back > 0; --back) {
        const pass_rec_t* const rec = pass_ring_peek(&g_pass_ring, back - 1);
        if (rec == nullptr) continue;
        char row[48];
        if (rec->is_reset) {
            draw_marker(kDangerActive);
            std::snprintf(row, sizeof(row), "RESET — tamper %s", rec->t);
            ImGui::TextColored(kDangerActive, "%s", row);
        } else {
            draw_marker(kStateOk);
            char dur_buf[10];
            if (rec->dur_s >= 0.0f) {
                std::snprintf(dur_buf, sizeof(dur_buf), "%.1fs", rec->dur_s);
            } else {
                std::snprintf(dur_buf, sizeof(dur_buf), "--");
            }
            std::snprintf(row, sizeof(row), "#%-2d %s  %s",
                          rec->n, rec->t, dur_buf);
            ImGui::TextUnformatted(row);
        }
    }
    if (g_pass_ring.count == 0) {
        ImGui::TextDisabled("-- no passes yet --");
    }
    if (g_pass_ring.fresh) { // keep the newest row in view
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        g_pass_ring.fresh = false;
    }
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::Separator();
    ImGui::TextDisabled("LOG (live)");
    clear_button("CLEAR##rf", &g_ring_rf, g_rf_text);
    const float gate_h = ImGui::GetTextLineHeightWithSpacing() + 3.0f;
    // VOID-142 stage-2: Panel-2 pattern — bouncer ring snapshot into
    // g_rf_text (tag "rf"), then the frozen log_box geometry.
    log_ring_render(&g_ring_rf, g_rf_text, sizeof(g_rf_text), "");
    log_box("RfLog", g_rf_text, ImGui::GetContentRegionAvail().y - gate_h);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    // VOID-142 stage-2: live gate — passes, tamper rejects, restart
    // safety (receipts.json persisted ⇒ a settlement survived a
    // gateway restart, VOID-130).
    ImGui::Text("Passes %d/10 · Tamper rejects %d · Restart-safe %s",
                g_passes, g_tamper_rejects,
                (g_receipts.settlements > 0) ? "yes" : "--");
    ImGui::EndChild();
}

void render_panel2() {
    begin_panel("PanelGoServer", kMargin + kPanelW + kSpacing, kMargin);
    panel_title("2. Go Server (Gateway)", true);
    ImGui::Separator();
    // Two-column top zone (VOID-146): counters (left group) + SAT
    // HEALTH (right, bordered child) — Panel 1's legs / PASS HISTORY
    // split mirrored here. The health box is exactly the counter
    // block's height, so the HTTP LOG box below keeps its frozen
    // geometry (acceptance: no disruption to the log streams).
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float cols_h = 7.0f * line_h; // Status + 6 counters
    char line[48];
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImGui::SetCursorPosX(20.0f + ImGui::CalcTextSize("Sig verify fail:").x);
    ImGui::TextColored((g_service.gw_connected != 0) ? kStateOk : kTextDim,
                       "%s",
                       (g_service.gw_connected != 0) ? "CONNECTED" : "UNCONNECTED");
    std::snprintf(line, sizeof(line), "Packets in:     %ld", g_service.packets_in);
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Sig verify ok:  %ld", g_service.sig_ok);
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Sig verify fail:%ld", g_service.sig_fail);
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Intents queued: %ld", g_service.intents_queued);
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Receipts PENDING:%ld", g_service.receipts_pending);
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Receipts SENT:  %ld", g_service.receipts_dispatched);
    ImGui::TextUnformatted(line);
    ImGui::EndGroup();

    // VOID-146: SAT HEALTH — live constellation snapshot keyed by
    // APID (100 = Sat A, 101 = Sat B). In-place upsert: one row per
    // APID, never duplicated, dynamic on a new APID's first frame.
    // Freshness is computed against the record's own gateway wall
    // clock (received_at_ms), so a UI restart can never make stale
    // data look live. Spec thresholds: ≤35 s [OK] · ≤70 s [STALE] ·
    // else [LOST], recomputed every frame.
    ImGui::SameLine();
    const float health_w = ImGui::GetWindowContentRegionMax().x -
                           ImGui::GetCursorPosX();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild("SatHealth", ImVec2(health_w, cols_h), true);
    ImGui::TextDisabled("SAT HEALTH");
    if (g_heartbeats.node_count == 0) {
        ImGui::TextDisabled("-- awaiting first heartbeat --");
    }
    const long long now_ms =
        static_cast<long long>(std::time(nullptr)) * 1000LL;
    for (std::size_t i = 0; i < g_heartbeats.node_count; ++i) {
        const hb_node_t& n = g_heartbeats.nodes[i];
        if (!n.used) {
            continue;
        }
        char row[48];
        std::snprintf(row, sizeof(row), "#%d %-5s %4.2fV %6.1fC",
                      n.apid, hb_role_tag(n.apid),
                      static_cast<double>(n.vbatt_mv) / 1000.0,
                      static_cast<double>(n.temp_c) / 100.0);
        ImGui::TextUnformatted(row);
        long long elapsed_s = (now_ms - n.last_rx_ms) / 1000LL;
        if (elapsed_s < 0) {
            elapsed_s = 0; // clock skew — clamp to OK side
        }
        const bool ok = (elapsed_s <= 35);
        const bool stale = (elapsed_s <= 70);
        char state[24];
        std::snprintf(state, sizeof(state), "%s %llus",
                      ok ? "[OK]" : stale ? "[STALE]" : "[LOST]",
                      static_cast<unsigned long long>(elapsed_s));
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                       ImGui::CalcTextSize(state).x);
        ImGui::TextColored(ok ? kStateOk : stale ? kTextAmber : kDangerActive,
                           "%s", state);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::TextDisabled("HTTP LOG (live)");
    clear_button("CLEAR##http", &g_ring_http, g_http_text);
    // Pinned column header (gin request-line widths) — stays visible
    // while the box below scrolls.
    ImGui::TextDisabled("%s", kHttpLogHeader);
    log_ring_render(&g_ring_http, g_http_text, sizeof(g_http_text), "");
    log_box("GwLog", g_http_text, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
}

void render_panel3() {
    begin_panel("PanelL2Chain", kMargin, kMargin + kPanelH + kSpacing);
    panel_title("3. L2 Blockchain (Anvil)", true);
    ImGui::Separator();
    // VOID-142 1c: Status/Block from eth_blockNumber; Contract from the
    // LOAD flow; Settlements from the receipts upsert table.
    char line[48];
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImGui::SetCursorPosX(20.0f + ImGui::CalcTextSize("Settlements:").x);
    ImGui::TextColored((g_service.chain_connected != 0) ? kStateOk : kTextDim,
                       "%s",
                       (g_service.chain_connected != 0) ? "CONNECTED" : "UNCONNECTED");
    std::snprintf(line, sizeof(line), "Block:      %s",
                  (g_service.block_dec[0] != '\0') ? g_service.block_dec : "--");
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Contract:   %s",
                  (proc_escrow()[0] != '\0') ? proc_escrow() : "--");
    ImGui::TextUnformatted(line);
    std::snprintf(line, sizeof(line), "Settlements: %u",
                  static_cast<unsigned>(g_receipts.settlements));
    ImGui::TextUnformatted(line);
    ImGui::TextDisabled("BLOCKCHAIN LOG (live — newest at bottom)");
    clear_button("CLEAR##chain", &g_ring_chain, g_chain_text);
    log_ring_render(&g_ring_chain, g_chain_text, sizeof(g_chain_text), "");
    log_box("L2Log", g_chain_text, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
}

void render_panel4() {
    begin_panel("PanelControls", kMargin + kPanelW + kSpacing,
                kMargin + kPanelH + kSpacing);

    // Title row: marker + title left, RUN clock right.
    draw_marker(kMarkerIdle);
    ImGui::TextUnformatted("4. Operator Controls");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                    ImGui::CalcTextSize(g_run_buf).x);
    ImGui::TextUnformatted(g_run_buf);
    ImGui::Separator();

    const float avail = ImGui::GetContentRegionAvail().x;
    const float half_w = (avail - SC(8.0f)) * 0.5f;
    // VOID-142 stage-2: bouncer serial port (buyer Heltec USB). Empty
    // = test mode (no radio, 'tst_ack' path). Read before START — the
    // bouncer spawn takes it as its one argument.
    ImGui::TextDisabled("SERIAL PORT (empty = test mode):");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##serial_port", g_serial_port, sizeof(g_serial_port));
    // START auto-chain: with no contract loaded it deploys first
    // (async → "DEPLOYING…"; tick_services chains the gateway +
    // bouncer on the deploy result). One click = Panels 1, 2 AND 3.
    if (ImGui::Button("START", ImVec2(half_w, SC(40.0f)))) {
        if (proc_anvil_start() != 0) {
            std::snprintf(g_last_action, sizeof(g_last_action),
                          "START FAILED (anvil)");
        } else if (proc_escrow()[0] == '\0') {
            if (start_async_deploy(true) == 0) {
                std::snprintf(g_last_action, sizeof(g_last_action),
                              "DEPLOYING…");
            } else {
                std::snprintf(g_last_action, sizeof(g_last_action),
                              "DEPLOY BUSY");
            }
        } else {
            const int rc = proc_gateway_start();
            if (rc != 0) {
                std::snprintf(g_last_action, sizeof(g_last_action),
                              "START FAILED");
            } else {
                // VOID-142 stage-2: bouncer rides along on the direct
                // path (serial field above; empty = test mode).
                const int rb = proc_bouncer_start(g_serial_port);
                std::snprintf(g_last_action, sizeof(g_last_action),
                              (rb == 0) ? "START (anvil+gateway+bouncer)"
                                        : "START (bouncer FAILED)");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("STOP", ImVec2(half_w, SC(40.0f)))) {
        const int rc = proc_stop_all();
        std::snprintf(g_last_action, sizeof(g_last_action),
                      (rc == 0) ? "STOP (clean)" : "STOP (forced)");
    }
    // Row 3: SEND ACK | SHOW PANEL | HELP | SNAP↔RESTORE (four cols).
    const float q_w = (avail - SC(24.0f)) * 0.25f;
    if (ImGui::Button("SEND ACK", ImVec2(q_w, SC(40.0f)))) {
        // VOID-142 stage-2: authorise the buyer payment through the
        // bouncer's CLI stdin ("ack" → ACK_BUY over serial). -1 (no
        // stdin pipe / dead child) reads as "no bouncer".
        std::snprintf(g_last_action, sizeof(g_last_action),
                      (proc_stdin_write(PROC_BOUNCER, "ack\n") == 0)
                          ? "SEND ACK (ok)" : "SEND ACK (no bouncer)");
    }
    ImGui::SameLine();
    const char* panel_label = g_drawer_open ? "HIDE PANEL" : "SHOW PANEL";
    if (ImGui::Button(panel_label, ImVec2(q_w, SC(40.0f)))) {
        g_drawer_open = !g_drawer_open;
        std::snprintf(g_last_action, sizeof(g_last_action), "%s",
                      g_drawer_open ? "SHOW PANEL" : "HIDE PANEL");
        if (g_full) {
            recompute_scale(); // base width changed 1280 ↔ 1760
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("HELP", ImVec2(q_w, SC(40.0f)))) {
        g_help_open = true;
        g_help_just_opened = true;
    }
    ImGui::SameLine();
    const char* snap_label = g_full ? "RESTORE" : "SNAP FULL";
    if (ImGui::Button(snap_label, ImVec2(q_w, SC(40.0f)))) {
        if (g_full) {
            unsnap_full();
        } else {
            snap_full();
        }
        std::snprintf(g_last_action, sizeof(g_last_action), "%s",
                      g_full ? "SNAP FULL" : "RESTORE");
    }

    ImGui::PushStyleColor(ImGuiCol_Button, kDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDangerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDangerActive);
    if (ImGui::Button("EXIT", ImVec2(-1.0f, SC(40.0f)))) {
        // Graceful first: reap the stack, then close the window.
        proc_stop_all();
        std::snprintf(g_last_action, sizeof(g_last_action), "EXIT");
        if (g_window != nullptr) {
            glfwSetWindowShouldClose(g_window, 1);
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();
    ImGui::Text("Last action: %s", g_last_action);
    ImGui::EndChild();
}

// Editable escrow buffer for the side panel's EDIT CONTRACT tab.
char g_address_edit[48] = "";

// 0-9 / a-f / A-F / the '0x' prefix characters (escrow address field).
int address_filter(ImGuiInputTextCallbackData* data) {
    const ImWchar c = data->EventChar;
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F') || (c == 'x');
    return ok ? 0 : 1;
}

void render_side_panel() {
    if (!g_drawer_open) {
        return;
    }
    ImGui::SetCursorPos(ImVec2(SC(kMargin + kPanelW + kSpacing + kPanelW + kSpacing),
                               0.0f));
    ImGui::BeginChild("SidePanel", ImVec2(SC(kDrawerW), SC(kDrawerH)), true);
    // Two-tab panel: EDIT CONTRACT (address/value/load) | VIEW RECEIPTS
    // (receipts.json tail). Tab state persists across hide/show.
    if (ImGui::BeginTabBar("SideTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("EDIT CONTRACT")) {
            ImGui::TextUnformatted("CURRENT:");
            ImGui::SameLine();
            ImGui::TextUnformatted((proc_escrow()[0] != '\0') ? proc_escrow() : "--");
            ImGui::Spacing();
            ImGui::TextDisabled("CONTRACT ADDRESS (edit, then APPLY):");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##escrow_edit", g_address_edit,
                             static_cast<int>(sizeof(g_address_edit)),
                             ImGuiInputTextFlags_CallbackCharFilter,
                             address_filter);
            if (ImGui::Button("APPLY CONTRACT", ImVec2(-1.0f, 40.0f))) {
                if (g_address_edit[0] == '\0') {
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "APPLY — empty address");
                } else if (proc_set_escrow(g_address_edit) == 0) {
                    int rc = 0;
                    if (proc_running(PROC_GATEWAY) != 0) {
                        rc = proc_gateway_restart();
                    }
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "APPLY OK %s (%s)", proc_escrow(),
                                  (rc == 0) ? "repoint" : "repoint failed");
                } else {
                    log_ring_push(&g_ring_errors, "ui",
                                  "APPLY INVALID (need 0x?+40 hex)");
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "APPLY INVALID (need 0x?+40 hex)");
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("LOAD NEXT CONTRACT", ImVec2(-1.0f, SC(40.0f)))) {
                if (proc_anvil_start() != 0) {
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "LOAD FAILED (anvil)");
                } else if (start_async_deploy(false) == 0) {
                    log_ring_push(&g_ring_cmds, "cmd", proc_forge_cmdline());
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "DEPLOYING…");
                } else {
                    std::snprintf(g_last_action, sizeof(g_last_action),
                                  "DEPLOY BUSY");
                }
            }
            ImGui::Spacing();
            // Raw query visibility: the next forge command (source of
            // truth — built from the same statics the worker spawns)
            // plus the executed ring, newest at bottom.
            ImGui::TextDisabled("NEXT QUERY (raw):");
            static char cmd_preview[1024];
            wrap_text(proc_forge_cmdline(), cmd_preview,
                      sizeof(cmd_preview), 72);
            log_box("CmdPreview", cmd_preview, SC(56.0f));
            ImGui::TextDisabled("EXECUTED QUERIES (raw — newest at bottom):");
            clear_button("CLEAR##cmds", &g_ring_cmds, g_cmds_text);
            log_ring_render(&g_ring_cmds, g_cmds_text, sizeof(g_cmds_text), "");
            log_box("CmdHistory", g_cmds_text, SC(104.0f));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VIEW RECEIPTS")) {
            ImGui::TextDisabled("receipts.json — read-only tail (live)");
            // View-only CLEAR: snapshot current keys into `hidden`
            // (receipts.json itself stays untouched — TRL 4 restart
            // evidence). New receipts appear normally after a clear.
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                            ImGui::CalcTextSize("CLEAR").x);
            if (ImGui::SmallButton("CLEAR##receipts")) {
                receipts_clear_view(&g_receipts);
            }
            // Two indented lines per record (chosen layout): payment
            // id on line 1, tx hash + status on line 2. Deterministic
            // wrap, no mid-field breaks. Bounded per frame.
            static char drawer_text[32768];
            drawer_text[0] = '\0';
            std::size_t used = 0;
            std::size_t visible = 0;
            for (std::size_t i = 0; i < g_receipts.key_count; ++i) {
                const receipt_key_t& k = g_receipts.keys[i];
                if (receipt_is_hidden(&g_receipts, k.payment_id,
                                      k.tx_hash)) {
                    continue; // operator-cleared — stay hidden
                }
                ++visible;
                const int n = std::snprintf(
                    drawer_text + used, sizeof(drawer_text) - used,
                    "{\"payment_id\":\"%s\",\n   \"tx_hash\":\"%s\",\"status\":\"%s\"}\n",
                    k.payment_id, k.tx_hash, k.status);
                if (n < 0 || static_cast<std::size_t>(n) >= sizeof(drawer_text) - used) {
                    break; // leave NUL in place; truncation is explicit
                }
                used += static_cast<std::size_t>(n);
            }
            if (visible == 0) {
                std::snprintf(drawer_text, sizeof(drawer_text), "%s",
                              (g_receipts.key_count == 0)
                                  ? ((g_receipts.path_ok)
                                         ? "-- receipts.json empty --"
                                         : "-- no receipts.json yet --")
                                  : "-- receipts cleared — new receipts appear here --");
            }
            log_box("DrawerLog", drawer_text, ImGui::GetContentRegionAvail().y);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DEBUG")) {
            ImGui::TextDisabled("RAW SERIAL STREAM — read-only (← from board · → to board)");
            // View-local CLEAR: the debug ring is a passive tap — clearing
            // it touches nothing downstream (Panel 1 keeps its own view).
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                            ImGui::CalcTextSize("CLEAR").x);
            if (ImGui::SmallButton("CLEAR##debug")) {
                debug_ring_reset(&g_ring_debug);
                g_debug_text[0] = '\0';
            }
            // VOID-145: raw wire lines, both directions, verbatim — full
            // hex payloads included; the unfiltered view behind Panel 1.
            // Read-only by construction: nothing in this tab writes to
            // the bouncer (SEND ACK in Panel 4 stays the only wire-write
            // path). Tail follows only while already at the bottom.
            debug_ring_render(&g_ring_debug, g_debug_text,
                              sizeof(g_debug_text));
            if (g_debug_text[0] == '\0') {
                std::snprintf(g_debug_text, sizeof(g_debug_text), "%s",
                              "-- no serial data (bouncer idle or test mode) --");
            }
            log_box("DebugLog", g_debug_text,
                    ImGui::GetContentRegionAvail().y, true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("HEARTBEATS")) {
            ImGui::TextDisabled("HEARTBEAT HISTORY — gateway-validated frames (live)");
            // View-local CLEAR (spec): flush the history rows only.
            // The Panel-2 SAT HEALTH table and heartbeats.json itself
            // are untouched — new heartbeats keep arriving into both.
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                            ImGui::CalcTextSize("CLEAR").x);
            if (ImGui::SmallButton("CLEAR##hbhist")) {
                heartbeats_clear_view(&g_heartbeats);
            }
            // Pinned header stays visible while the table scrolls.
            ImGui::TextDisabled("%s", kHbLogHeader);
            // VOID-146: append-only history, newest at bottom. Per-row
            // rendering (ImGui clips off-screen rows the way the PASS
            // HISTORY list does — no per-frame snapshot blob to rebuild
            // for 1000 rows). Tail follows only while the operator is
            // already at the bottom; scrolling up cancels the pin
            // (VOID-145 log_box pattern, inlined for per-row items).
            ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
            ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
            ImGui::BeginChild("HbTable",
                              ImVec2(0.0f, ImGui::GetContentRegionAvail().y),
                              true, ImGuiWindowFlags_HorizontalScrollbar);
            const std::size_t cnt = g_heartbeats.hist_count;
            const bool pin = (cnt > 0) &&
                (ImGui::GetScrollY() >= ImGui::GetScrollMaxY());
            if (cnt == 0) {
                ImGui::TextDisabled("-- no heartbeats yet --");
            } else {
                const std::size_t start =
                    (g_heartbeats.hist_next + kHbHistMax - cnt) % kHbHistMax;
                for (std::size_t i = 0; i < cnt; ++i) {
                    const hb_rec_t& r =
                        g_heartbeats.hist[(start + i) % kHbHistMax];
                    char row[96];
                    std::snprintf(row, sizeof(row),
                        "%s  #%-3d %-5s %5.2fV %6.1fC %3u %8uPa %10.6f %10.6f",
                        r.t, r.apid, hb_role_tag(r.apid),
                        static_cast<double>(r.vbatt_mv) / 1000.0,
                        static_cast<double>(r.temp_c) / 100.0,
                        r.sat_lock, r.pressure_pa,
                        static_cast<double>(r.lat_fixed) / 10000000.0,
                        static_cast<double>(r.lon_fixed) / 10000000.0);
                    ImGui::TextUnformatted(row);
                }
            }
            if (pin) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

void render_error_strip() {
    ImGui::SetCursorPos(ImVec2(0.0f, SC(kStripY)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild("ErrorStrip", ImVec2(SC(1280.0f), SC(kStripH)), true);
    // Filter tabs live on the label row — the frozen 98px strip keeps
    // its geometry, the ring filters by source tag.
    ImGui::TextUnformatted("ERRORS");
    ImGui::SameLine();
    const struct { const char* label; const char* tag; } filters[] = {
        {"ALL", ""}, {"GATEWAY", "gw"}, {"ANVIL", "anvil"},
        {"RF", "rf"}, {"UI", "ui"} // VOID-142 stage-2: bouncer source
    };
    for (const auto& f : filters) {
        const bool active = (std::strcmp(g_err_filter, f.tag) == 0);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, kBtnActive);
        }
        if (ImGui::SmallButton(f.label)) {
            g_err_filter = f.tag;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
    }
    // CLEAR sits right-aligned on the same label row; it empties every
    // stored line regardless of the active filter tab.
    clear_button("CLEAR##errors", &g_ring_errors, g_errors_text);
    log_ring_render(&g_ring_errors, g_errors_text, sizeof(g_errors_text),
                    g_err_filter);
    log_box("ErrLog", g_errors_text, SC(64.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void render_help_modal(const ImVec2& display) {
    if (!g_help_open) {
        return;
    }
    const float kModalW = SC(620.0f);
    const float kModalH = SC(660.0f);
    const float mx = (display.x - kModalW) * 0.5f;
    const float my = (display.y - kModalH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBackdrop);
    ImGui::Begin("##help_backdrop", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetCursorPos(ImVec2(mx, my));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kConsoleBg);
    ImGui::BeginChild("HelpModal", ImVec2(kModalW, kModalH), true);

    ImGui::TextUnformatted("What you need to do");
    ImGui::TextUnformatted(kHelpRunbook);
    ImGui::Spacing();
    ImGui::TextUnformatted("Panels");
    ImGui::TextUnformatted(kHelpPanels);
    ImGui::Spacing();
    ImGui::TextUnformatted("Leg states");
    ImGui::TextUnformatted(kHelpStates);
    ImGui::Spacing();
    ImGui::TextUnformatted("Gate (bottom of Panel 1)");
    ImGui::TextUnformatted(kHelpGate);
    ImGui::Spacing();
    if (ImGui::Button("CLOSE HELP", ImVec2(200.0f, 40.0f))) {
        g_help_open = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg

    // Backdrop click outside the modal dismisses it — but not the click that
    // opened it (the HELP button may sit under the modal rect this frame).
    if (g_help_just_opened) {
        g_help_just_opened = false;
    } else {
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const ImVec2 modal_min(win_pos.x + mx, win_pos.y + my);
        const ImVec2 modal_max(modal_min.x + kModalW, modal_min.y + kModalH);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseHoveringRect(modal_min, modal_max)) {
            g_help_open = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

void tick_run_clock() {
    const unsigned long sec = static_cast<unsigned long>(ImGui::GetTime());
    std::snprintf(g_run_buf, sizeof(g_run_buf), "RUN %02lu:%02lu:%02lu",
                  sec / 3600UL, (sec % 3600UL) / 60UL, sec % 60UL);
}

} // namespace

int main(int, char**) {
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == 0) {
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(kWinW), static_cast<int>(kWinH),
        "VOID Ground Station Console (VOID-141)", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    g_window = window; // EXIT's close path
    proc_init();       // SIGPIPE guard + repo-root anchor
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // deterministic fixed layout

    // 14px monospace with the glyphs our text uses (—, ·, ↔, →, ●, ○).
    // Do not hard-depend on a TTF being present: fall back to ImGui's default.
    static const ImWchar kGlyphRanges[] = {
        0x0020, 0x00FF, 0x2013, 0x2014, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0,
    };
    static const char* const kFontCandidates[] = {
        "/System/Library/Fonts/Menlo.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "C:\\Windows\\Fonts\\consola.ttf",
    };
    bool font_loaded = false;
    for (size_t i = 0; i < sizeof(kFontCandidates) / sizeof(kFontCandidates[0]);
         ++i) {
        FILE* probe = std::fopen(kFontCandidates[i], "rb");
        if (probe != nullptr) {
            std::fclose(probe);
            if (io.Fonts->AddFontFromFileTTF(kFontCandidates[i], 14.0f,
                                             nullptr, kGlyphRanges) != nullptr) {
                font_loaded = true;
                break;
            }
        }
    }
    if (!font_loaded) {
        io.Fonts->AddFontDefault();
    }
    g_font_loaded = font_loaded; // streak-dot glyph availability

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding  = 0.0f;
    style.ChildRounding   = 0.0f;
    style.FrameRounding   = 0.0f;
    style.FramePadding    = ImVec2(6.0f, 5.0f);
    style.Colors[ImGuiCol_WindowBg]        = kConsoleBg;
    style.Colors[ImGuiCol_ChildBg]         = kConsoleBg;
    style.Colors[ImGuiCol_Border]          = kBorderCol;
    style.Colors[ImGuiCol_TextDisabled]    = kTextDim;
    style.Colors[ImGuiCol_Button]          = kBtnCol;
    style.Colors[ImGuiCol_ButtonHovered]   = kBtnHover;
    style.Colors[ImGuiCol_ButtonActive]    = kBtnActive;
    style.Colors[ImGuiCol_FrameBg]         = kInputBg;
    style.Colors[ImGuiCol_FrameBgHovered]  = kInputFocus;
    style.Colors[ImGuiCol_FrameBgActive]   = kInputFocus;
    style.Colors[ImGuiCol_Separator]       = kBorderCol;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (glfwWindowShouldClose(window) == 0) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        tick_run_clock();
        tick_services(); // drains child pipes + 2 Hz service polls (VOID-142)

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##stage", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();

        render_panel1();
        render_panel2();
        render_panel3();
        render_panel4();
        render_side_panel();
        render_error_strip();

        ImGui::End();

        render_help_modal(io.DisplaySize);

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
