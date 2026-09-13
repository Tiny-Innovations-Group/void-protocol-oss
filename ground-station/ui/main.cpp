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

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "proc_manager.h"

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

// --- Sample content (spec section 6.1, verbatim) ---
const char* kLogRf =
    "10:14:02  A     Invoice TX'd                 apid=100 len=80\n"
    "10:14:05  B     Payment RX                   sig OK\n"
    "10:14:06  ACK   Buyer ack TX'd               apid=101 len=136\n"
    "10:14:12  SETTLE settleBatch block=2          tx=0xb21f...\n"
    "10:14:14  C     Receipt built                status=PENDING\n"
    "10:14:15  C     Receipt dispatched           apid=100 len=112\n"
    "10:14:18  D     Delivery RX                  CRC OK\n"
    "10:15:23  HB    heartbeat                    VBAT=3980mV T=38C";

const char* kLogHttp =
    "10:14:05  200  POST  /api/v1/ingest    packetb.enqueued\n"
    "10:14:12  200  GET   /pending          receipts=true\n"
    "10:14:14  200  POST  /ack              status=dispatched\n"
    "10:14:15  200  GET   /pending          receipts=false\n"
    "10:15:02  200  GET   /api/v1/status\n"
    "10:15:47  400  POST  /api/v1/ingest    sig fail";

const char* kLogChain =
    "10:14:12  block 2 mined\n"
    "10:14:12  tx 0xb21f  settleBatch()            escrow\n"
    "10:14:12  event SettlementCreated             block=2";

const char* kLogErrors =
    "10:15:47  gw      packetb.sig_fail — 400 (bad Ed25519 sig)\n"
    "10:15:12  chain   escrow submit retry #1 (RPC timeout)\n"
    "10:14:59  rf      PacketA CRC fail — frame dropped";

const char* kReceiptsTail =
    "{\"payment_id\":\"d78a91\",\"tx_hash\":\"0x2b3c\",\"status\":\"DISPATCHED\"}\n"
    "{\"payment_id\":\"c3d4e5\",\"tx_hash\":\"0x9f8e\",\"status\":\"DISPATCHED\"}\n"
    "{\"payment_id\":\"f0a1b2\",\"tx_hash\":\"0x5d6c\",\"status\":\"PENDING\"}\n"
    "{\"payment_id\":\"708a9b\",\"tx_hash\":\"0x1a2b\",\"status\":\"DISPATCHED\"}\n"
    "{\"payment_id\":\"4d5e6f\",\"tx_hash\":\"0x77cc\",\"status\":\"DISPATCHED\"}\n"
    "{\"payment_id\":\"9c0d1e\",\"tx_hash\":\"0xa1e2\",\"status\":\"PENDING\"}\n"
    "{\"payment_id\":\"e5f6a7\",\"tx_hash\":\"0xc3a9\",\"status\":\"DISPATCHED\"}\n"
    "{\"payment_id\":\"a1b2c3\",\"tx_hash\":\"0xb21f\",\"status\":\"DISPATCHED\"}";

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

// --- Inert UI state (spec section 6.4) ---
bool g_drawer_open       = false;
bool g_help_open         = false;
bool g_help_just_opened  = false;
char g_last_action[64]     = "--";
char g_contract_value[32]  = "500";
char g_run_buf[16]         = "RUN 00:00:00";

// Set in main() right after the window exists — EXIT's real close path
// needs the handle (VOID-142 stage-1 wiring).
GLFWwindow* g_window = nullptr;

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// 10x10 square status marker, vertically centred on the current text line.
void draw_marker(const ImVec4& col) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float line_h = ImGui::GetTextLineHeight();
    const float y = p.y + ((line_h - 10.0f) * 0.5f);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, y), ImVec2(p.x + 10.0f, y + 10.0f),
        ImGui::ColorConvertFloat4ToU32(col));
    ImGui::Dummy(ImVec2(10.0f, 0.0f));
    ImGui::SameLine(0.0f, 7.0f);
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
    ImGui::SetCursorPos(ImVec2(x, y));
    ImGui::BeginChild(id, ImVec2(kPanelW, kPanelH), true);
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
    ImGui::TextUnformatted(
        "Status:           UNCONNECTED\n"
        "Packets in:       --\n"
        "Sig verify ok:    --\n"
        "Sig verify fail:  --\n"
        "Intents queued:   --\n"
        "Receipts PENDING: --\n"
        "Receipts SENT:    --");
    ImGui::TextDisabled("HTTP LOG (sample)");
    log_box("GwLog", kLogHttp, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
}

void render_panel3() {
    begin_panel("PanelL2Chain", kMargin, kMargin + kPanelH + kSpacing);
    panel_title("3. L2 Blockchain (Anvil)", true);
    ImGui::Separator();
    // VOID-142 stage-1: Contract field is live (proc_escrow); the rest
    // comes online in the 1c poll pass.
    ImGui::TextUnformatted("Status:      UNCONNECTED");
    ImGui::TextUnformatted("Block:       --");
    ImGui::Text("Contract:    %s",
               (proc_escrow()[0] != '\0') ? proc_escrow() : "--");
    ImGui::TextUnformatted("Settlements: --");
    ImGui::TextDisabled("BLOCKCHAIN LOG (sample — newest at bottom)");
    log_box("L2Log", kLogChain, ImGui::GetContentRegionAvail().y);
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

    ImGui::TextUnformatted("CONTRACT VALUE:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("##contract_value", g_contract_value,
                     static_cast<int>(sizeof(g_contract_value)),
                     ImGuiInputTextFlags_CallbackCharFilter, digit_filter);
    ImGui::TextDisabled("amount used by LOAD NEXT CONTRACT");

    const float avail = ImGui::GetContentRegionAvail().x;
    // VOID-142 stage-1: LOAD ensures anvil, deploys Escrow via forge,
    // and re-points the gateway. Result address echoes into the action
    // line (value kept per spec §6.4).
    if (ImGui::Button("LOAD NEXT CONTRACT", ImVec2(-1.0f, 40.0f))) {
        const int rc = proc_deploy_contract();
        const char* value = (g_contract_value[0] != '\0') ? g_contract_value : "0";
        if (rc == 0) {
            std::snprintf(g_last_action, sizeof(g_last_action),
                          "LOAD CONTRACT (value=%s) %s", value, proc_escrow());
        } else {
            std::snprintf(g_last_action, sizeof(g_last_action),
                          "LOAD CONTRACT (value=%s) FAILED", value);
        }
    }
    // START: anvil first, then gateway (uses the loaded contract).
    if (ImGui::Button("START", ImVec2(avail * 0.5f - 4.0f, 40.0f))) {
        int rc = proc_anvil_start();
        if (rc == 0) {
            rc = proc_gateway_start();
        }
        std::snprintf(g_last_action, sizeof(g_last_action), "START (%s)",
                      (rc == 0) ? "anvil+gateway" : "FAILED");
    }
    ImGui::SameLine();
    if (ImGui::Button("STOP", ImVec2(avail * 0.5f - 4.0f, 40.0f))) {
        const int rc = proc_stop_all();
        std::snprintf(g_last_action, sizeof(g_last_action), "STOP (%s)",
                      (rc == 0) ? "clean" : "forced");
    }
    const float third_w = (avail - 16.0f) / 3.0f;
    if (ImGui::Button("SEND ACK", ImVec2(third_w, 40.0f))) {
        std::snprintf(g_last_action, sizeof(g_last_action), "SEND ACK");
    }
    ImGui::SameLine();
    const char* receipts_label = g_drawer_open ? "HIDE RECEIPTS" : "VIEW RECEIPTS";
    if (ImGui::Button(receipts_label, ImVec2(third_w, 40.0f))) {
        g_drawer_open = !g_drawer_open;
        std::snprintf(g_last_action, sizeof(g_last_action), "%s",
                      g_drawer_open ? "SHOW RECEIPTS" : "HIDE RECEIPTS");
    }
    ImGui::SameLine();
    if (ImGui::Button("HELP", ImVec2(third_w, 40.0f))) {
        g_help_open = true;
        g_help_just_opened = true;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, kDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kDangerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kDangerActive);
    if (ImGui::Button("EXIT", ImVec2(-1.0f, 40.0f))) {
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

void render_drawer() {
    if (!g_drawer_open) {
        return;
    }
    ImGui::SetCursorPos(ImVec2(kMargin + kPanelW + kSpacing + kPanelW + kSpacing,
                               0.0f));
    ImGui::BeginChild("ReceiptsDrawer", ImVec2(kDrawerW, kDrawerH), true);
    ImGui::TextDisabled("receipts.json — read-only tail (sample)");
    log_box("DrawerLog", kReceiptsTail, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
}

void render_error_strip() {
    ImGui::SetCursorPos(ImVec2(0.0f, kStripY));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kLogBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kLogBorder);
    ImGui::BeginChild("ErrorStrip", ImVec2(1280.0f, kStripH), true);
    ImGui::TextDisabled("ERRORS — all panels (sample)");
    log_box("ErrLog", kLogErrors, 64.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void render_help_modal(const ImVec2& display) {
    if (!g_help_open) {
        return;
    }
    constexpr float kModalW = 620.0f;
    constexpr float kModalH = 660.0f;
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
        render_drawer();
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
