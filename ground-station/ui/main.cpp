/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      VOID-141 operator console — full port of frozen mockup design
 *            (docs/VOID-141_IMGUI_PORT_SPEC.md). Inert sample data only;
 *            stack wiring is a later stage.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "proc_manager.h"
#include "log_store.h"
#include "service_poller.h"
#include "receipts_tailer.h"

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

const ImVec4& state_color(const LegState s) {
    switch (s) {
        case LegState::Active: return kBtnHover;
        case LegState::Ok:     return kStateOk;
        case LegState::Fail:   return kDangerActive;
        case LegState::Idle:
        default:               return kTextDim;
    }
}

// --- Sample content (spec section 6.1) — only the RF sample remains;
//     Panels 2/3 + drawer + error strip are live rings (VOID-142 1c). ---
const char* kLogRf =
    "10:14:02  A     Invoice TX'd                 apid=100 len=80\n"
    "10:14:05  B     Payment RX                   sig OK\n"
    "10:14:06  ACK   Buyer ack TX'd               apid=101 len=136\n"
    "10:14:12  SETTLE settleBatch block=2          tx=0xb21f...\n"
    "10:14:14  C     Receipt built                status=PENDING\n"
    "10:14:15  C     Receipt dispatched           apid=100 len=112\n"
    "10:14:18  D     Delivery RX                  CRC OK\n"
    "10:15:23  HB    heartbeat                    VBAT=3980mV T=38C";

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
    "2. Go Server (Gateway) — gateway health, verify counters, HTTP request log.\n"
    "3. L2 Blockchain (Anvil) — mined blocks, settleBatch txs, SettlementCreated\n"
    "    events; newest at bottom.\n"
    "4. Operator Controls — the demo lifecycle lives here: load, start, stop.";

const char* kHelpStates =
    "[IDLE]   no activity yet\n"
    "[ACTIVE] leg currently in progress\n"
    "[OK]     leg completed successfully\n"
    "[FAIL]   CRC or signature failed — resets the pass counter";

const char* kHelpGate =
    "Passes 10/10 · Tamper rejects visible · Restart-safe:\n"
    "a full demo pass is one clean A → B → ACK → SETTLE → C → D run.\n"
    "10 consecutive passes = flat-sat alpha done.";

// Set in main() right after the window exists — EXIT's real close path
// needs the handle (VOID-142 stage-1 wiring).
GLFWwindow* g_window = nullptr;

// --- Inert UI state (spec section 6.4) ---
bool g_drawer_open       = false;
bool g_help_open         = false;
bool g_help_just_opened  = false;
char g_last_action[64]     = "--";
char g_contract_value[32]  = "500";
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
log_ring_t      g_ring_http  = {};
log_ring_t      g_ring_chain = {};
log_ring_t      g_ring_errors = {};
log_ring_t      g_ring_cmds   = {}; // executed forge queries (raw)
double          g_last_poll  = 0.0;

// Errors filter tab. "" = ALL, else a ring tag ("gw" / "anvil" / "ui").
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
char g_errors_text[8448] = "";
char g_cmds_text[8448]  = "";

// Route one completed child-output line into the right rings, tagged
// by source ("gw" / "anvil" / "ui"). "gw" lines fill the HTTP log;
// "anvil" fills the chain log; settlement keywords on a "gw" line
// mirror into the chain log too; warn/error-ish lines from any
// source mirror into the error strip.
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
    } else if (std::strcmp(tag, "anvil") == 0) {
        log_ring_push(&g_ring_chain, tag, line);
    }
    if (std::strstr(line, "level=warn") != nullptr ||
        std::strstr(line, "level=error") != nullptr ||
        std::strstr(line, "REJECTED") != nullptr ||
        std::strstr(line, "sig_fail") != nullptr ||
        std::strstr(line, "failed") != nullptr ||
        std::strstr(line, "⛔") != nullptr ||
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
        const char* tag = (id == PROC_GATEWAY) ? "gw" :
                          (id == PROC_ANVIL)   ? "anvil" : "ui";
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
                const int rc = proc_gateway_start();
                std::snprintf(note, sizeof(note), "DEPLOY OK %s (%s)",
                              g_deploy_addr,
                              (rc == 0) ? "gateway start" : "gateway FAILED");
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
void log_box(const char* id, const char* content, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild(id, ImVec2(0.0f, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(content);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void begin_panel(const char* id, const float x, const float y) {
    ImGui::SetCursorPos(ImVec2(SC(x), SC(y)));
    ImGui::BeginChild(id, ImVec2(SC(kPanelW), SC(kPanelH)), true);
}

// Reject anything that is not an ASCII digit (CONTRACT VALUE field).
// Also enforce the spec 6.4 cap of 16 characters (buffer is 32; the
// length limit lives here, not in the buffer size).
int digit_filter(ImGuiInputTextCallbackData* data) {
    const ImWchar c = data->EventChar;
    if (c < static_cast<ImWchar>('0') || c > static_cast<ImWchar>('9')) {
        return 1;
    }
    if (data->BufTextLen >= 16) {
        return 1;
    }
    return 0;
}

void render_panel1() {
    begin_panel("PanelRfComms", kMargin, kMargin);
    panel_title("1. RF Comms (Ground ↔ Sat)", true);
    ImGui::Separator();

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

    ImGui::Separator();
    ImGui::TextDisabled("LOG (sample)");
    const float gate_h = ImGui::GetTextLineHeightWithSpacing() + 3.0f;
    log_box("RfLog", kLogRf, ImGui::GetContentRegionAvail().y - gate_h);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    ImGui::Text("Passes %d/10 · Tamper rejects %d · Restart-safe %s",
                0, 0, "--");
    ImGui::EndChild();
}

void render_panel2() {
    begin_panel("PanelGoServer", kMargin + kPanelW + kSpacing, kMargin);
    panel_title("2. Go Server (Gateway)", true);
    ImGui::Separator();
    // VOID-142 1c: live status fields from GET /api/v1/status (2 Hz).
    // Fixed 18ch label column, one bounded line per field.
    char line[48];
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
    ImGui::TextDisabled("HTTP LOG (live)");
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
    // START auto-chain: with no contract loaded it deploys first
    // (async → "DEPLOYING…"; tick_services chains the gateway on
    // the deploy result). One click = Panels 2 AND 3 come up.
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
            std::snprintf(g_last_action, sizeof(g_last_action),
                          (rc == 0) ? "START (anvil+gateway)" : "START FAILED");
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
        std::snprintf(g_last_action, sizeof(g_last_action), "SEND ACK");
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
            ImGui::TextDisabled("CONTRACT VALUE (next invoice amount):");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##contract_value", g_contract_value,
                             static_cast<int>(sizeof(g_contract_value)),
                             ImGuiInputTextFlags_CallbackCharFilter, digit_filter);
            ImGui::Spacing();
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
            log_box("CmdPreview", proc_forge_cmdline(), SC(40.0f));
            log_ring_render(&g_ring_cmds, g_cmds_text, sizeof(g_cmds_text), "");
            ImGui::TextDisabled("EXECUTED QUERIES (raw — newest at bottom):");
            log_box("CmdHistory", g_cmds_text, SC(120.0f));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VIEW RECEIPTS")) {
            ImGui::TextDisabled("receipts.json — read-only tail (live)");
            // Upsert table rendered as JSON-ish lines, newest at
            // bottom; file re-scans at 2 Hz.
            static char drawer_text[32768];
            drawer_text[0] = '\0';
            std::size_t used = 0;
            for (std::size_t i = 0; i < g_receipts.key_count; ++i) {
                const receipt_key_t& k = g_receipts.keys[i];
                const int n = std::snprintf(
                    drawer_text + used, sizeof(drawer_text) - used,
                    "{\"payment_id\":\"%s\",\"tx_hash\":\"%s\",\"status\":\"%s\"}\n",
                    k.payment_id, k.tx_hash, k.status);
                if (n < 0 || static_cast<std::size_t>(n) >= sizeof(drawer_text) - used) {
                    break; // leave NUL in place; truncation is explicit
                }
                used += static_cast<std::size_t>(n);
            }
            if (g_receipts.key_count == 0) {
                std::snprintf(drawer_text, sizeof(drawer_text),
                              "%s",
                              (g_receipts.path_ok) ? "-- receipts.json empty --"
                                                   : "-- no receipts.json yet --");
            }
            log_box("DrawerLog", drawer_text, ImGui::GetContentRegionAvail().y);
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
        {"ALL", ""}, {"GATEWAY", "gw"}, {"ANVIL", "anvil"}, {"UI", "ui"}
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

// Non-static on purpose: this is the wiring stage's live-data hook. The
// address is taken once in main() so -Wunused-function stays silent until
// the first real caller lands.
void setLegState(const char* name, const LegState state) {
    for (size_t i = 0; i < kLegCount; ++i) {
        if (std::strcmp(kLegs[i].name, name) == 0) {
            kLegs[i].state = state;
            return;
        }
    }
}

int main(int, char**) {
    // Keep the future wiring hook referenced (see setLegState comment above).
    void (*const leg_state_hook)(const char*, LegState) = &setLegState;
    static_cast<void>(leg_state_hook);

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

    // 14px monospace with the glyphs our text uses (—, ·, ↔, →).
    // Do not hard-depend on a TTF being present: fall back to ImGui's default.
    static const ImWchar kGlyphRanges[] = {
        0x0020, 0x00FF, 0x2013, 0x2014, 0x2190, 0x21FF, 0,
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
