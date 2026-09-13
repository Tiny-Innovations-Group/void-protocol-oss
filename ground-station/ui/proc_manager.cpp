/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      proc_manager.cpp
 * Desc:      VOID-142 stage-1 — child process lifecycle (posix_spawn,
 *            process-group kill, non-blocking log pipes). POSIX only;
 *            Win32 stubs keep the UI target portable.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "proc_manager.h"

#ifndef _WIN32

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <crt_externs.h> // _NSGetEnviron — environ is unavailable on Darwin
#endif

namespace {

// --- Static state (no heap, no dynamic sizing) ---
struct proc_slot_t {
    const char* name;
    pid_t       pid;
    int         log_fd;
};

proc_slot_t g_procs[PROC_COUNT] = {
    {"anvil",   0, -1},
    {"gateway", 0, -1},
    {"bouncer", 0, -1},
};

char g_repo_root[1024] = "";
char g_escrow[64]      = "";
bool g_inited          = false;

// forge create emits at most a few hundred bytes; 8 KiB absorbs-exec
// chatter (compile logs can appear when the cache is cold).
char g_forge_out[8192];

// Bounded envp workspace — the parent environ typically has <64 slots.
constexpr std::size_t kMaxEnvp = 128;

// --- Repo root anchor (walk up for .git, mirrors the Go gateway) ---
void resolve_repo_root(void) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return;
    }
    for (;;) {
        char probe[1050];
        std::snprintf(probe, sizeof(probe), "%s/.git", cwd);
        struct stat st;
        if (stat(probe, &st) == 0) {
            std::snprintf(g_repo_root, sizeof(g_repo_root), "%s", cwd);
            return;
        }
        if (cwd[0] == '/' && cwd[1] == '\0') {
            return; // filesystem top — no root found, stay empty
        }
        // Strip the last path component (bounded, no VLA).
        char* last = std::strrchr(cwd, '/');
        if (last == nullptr || last == cwd) {
            cwd[1] = '\0';
        } else {
            *last = '\0';
        }
    }
}

// Join environ (or _NSGetEnviron on Darwin) pointers + overrides into
// a static-safe envp array. We never mutate the strings, only the
// pointer vector — no heap, no setenv (which allocates internally).
std::size_t build_envp(char* envp[], std::size_t max,
                       const char* extra1, const char* extra2) {
#if defined(__APPLE__)
    char** env = *_NSGetEnviron();
#else
    extern char** environ;
    char** env = environ;
#endif
    std::size_t n = 0;
    const std::size_t extras = 2; // worst case
    for (char** e = env; e != nullptr && *e != nullptr; ++e) {
        if (n + extras + 1 >= max) {
            break; // leave room for extras + NULL
        }
        envp[n++] = *e;
    }
    if (extra1 != nullptr) { envp[n++] = const_cast<char*>(extra1); }
    if (extra2 != nullptr) { envp[n++] = const_cast<char*>(extra2); }
    envp[n] = nullptr;
    return n;
}

// Spawn a long-running service child on its own process group with a
// non-blocking merged stdout/stderr pipe read back by proc_drain_log.
int spawn_service(const int id, char* const argv[], const char* cwd,
                  char* const envp[]) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
    if (cwd != nullptr) {
#if defined(__APPLE__)
        // macOS 26 renamed addchdir_np → addchdir; same semantics.
        posix_spawn_file_actions_addchdir(&fa, cwd);
#else
        posix_spawn_file_actions_addchdir_np(&fa, cwd);
#endif
    }
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0); // child leads its own group

    pid_t pid = 0;
    // envp == nullptr → child inherits the parent's environment.
    const int rc = posix_spawnp(&pid, argv[0], &fa, &attr, argv, envp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    close(pipefd[1]);
    if (rc != 0) {
        close(pipefd[0]);
        return -1;
    }
    // Non-blocking read end — the UI polls it once per frame.
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    proc_slot_t& slot = g_procs[id];
    slot.pid = pid;
    slot.log_fd = pipefd[0];
    return 0;
}

// TCP connect probe — used to gate anvil readiness before we spawn the
// gateway or run forge against :8545.
int tcp_probe_ok(const unsigned short port) {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return 0;
    }
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(0x7F000001U); // 127.0.0.1
    const int ok = (connect(s, reinterpret_cast<struct sockaddr*>(&addr),
                            sizeof(addr)) == 0);
    close(s);
    return ok ? 1 : 0;
}

// Block (≤5 s) until anvil's JSON-RPC port accepts connections.
int wait_for_anvil(void) {
    const unsigned short anvil_port = 8545;
    for (int i = 0; i < 50; ++i) {
        if (tcp_probe_ok(anvil_port) != 0) {
            return 0;
        }
        usleep(100000); // 100 ms
    }
    return -1;
}

// Byte-scan helper: children of the repo need no <string> — bounded
// memmem-style search over a bounded buffer.
char* find_substr(char* hay, const char* needle, const std::size_t haylen) {
    const std::size_t nlen = std::strlen(needle);
    if (nlen == 0 || haylen < nlen) {
        return nullptr;
    }
    for (std::size_t i = 0; i + nlen <= haylen; ++i) {
        if (std::memcmp(hay + i, needle, nlen) == 0) {
            return hay + i;
        }
    }
    return nullptr;
}

} // namespace

void proc_init(void) {
    if (g_inited) {
        return;
    }
    // PORT_SPEC §8: the UI must ignore SIGPIPE — a dying child closing
    // a pipe must not kill the console.
    signal(SIGPIPE, SIG_IGN);
    resolve_repo_root();
    g_inited = true;
}

const char* proc_repo_root(void) {
    proc_init();
    return g_repo_root;
}

int proc_anvil_start(void) {
    proc_init();
    if (proc_running(PROC_ANVIL) != 0) {
        return 0;
    }
    if (g_repo_root[0] == '\0') {
        return -1;
    }
    char* const argv[] = {const_cast<char*>("anvil"), nullptr};
    if (spawn_service(PROC_ANVIL, argv, g_repo_root, nullptr) != 0) {
        return -1;
    }
    return wait_for_anvil();
}

int proc_gateway_start(void) {
    proc_init();
    if (g_escrow[0] == '\0') {
        return -1; // LOAD NEXT CONTRACT must run first
    }
    if (proc_running(PROC_GATEWAY) != 0) {
        return 0;
    }
    char cwd[1050];
    std::snprintf(cwd, sizeof(cwd), "%s/gateway", g_repo_root);
    char env_plain[] = "VOID_ALPHA_PLAINTEXT=1";
    char env_escrow[96];
    std::snprintf(env_escrow, sizeof(env_escrow),
                  "VOID_ESCROW_ADDRESS=%s", g_escrow);
    char* envp[kMaxEnvp];
    build_envp(envp, kMaxEnvp, env_plain, env_escrow);
    char* const argv[] = {const_cast<char*>("go"),
                          const_cast<char*>("run"),
                          const_cast<char*>("./cmd/server"),
                          nullptr};
    return spawn_service(PROC_GATEWAY, argv, cwd, envp);
}

int proc_deploy_contract(void) {
    proc_init();
    if (proc_anvil_start() != 0) {
        return -1;
    }
    char cwd[1050];
    std::snprintf(cwd, sizeof(cwd), "%s/contracts", g_repo_root);
    char* const argv[] = {
        const_cast<char*>("forge"),
        const_cast<char*>("create"),
        const_cast<char*>("src/Escrow.sol:Escrow"),
        const_cast<char*>("--rpc-url"),
        const_cast<char*>("http://127.0.0.1:8545"),
        const_cast<char*>("--private-key"),
        const_cast<char*>("0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80"),
        const_cast<char*>("--broadcast"),
        nullptr
    };

    // Blocking pipe read: forge exits, we drain, parse, reap.
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, pipefd[0]);
#if defined(__APPLE__)
    // macOS 26 renamed addchdir_np → addchdir; same semantics.
    posix_spawn_file_actions_addchdir(&fa, cwd);
#else
    posix_spawn_file_actions_addchdir_np(&fa, cwd);
#endif

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv, nullptr);
    posix_spawn_file_actions_destroy(&fa);
    close(pipefd[1]);

    std::size_t used = 0;
    if (rc == 0) {
        for (;;) {
            const ssize_t r = read(pipefd[0], g_forge_out + used,
                                   sizeof(g_forge_out) - 1 - used);
            if (r <= 0 || used + static_cast<std::size_t>(r) >= sizeof(g_forge_out) - 1) {
                break;
            }
            used += static_cast<std::size_t>(r);
        }
        int status = 0;
        waitpid(pid, &status, 0);
    }
    close(pipefd[0]);
    g_forge_out[used] = '\0';

    // "Deployed to: 0x<40 hex>" — bounded parse over the captured text.
    const char* mark = "Deployed to: ";
    char* hit = find_substr(g_forge_out, mark, used);
    if (hit == nullptr) {
        return -1;
    }
    char* addr = hit + std::strlen(mark);
    if (static_cast<std::size_t>(used) -
        static_cast<std::size_t>(addr - g_forge_out) < 42) {
        return -1;
    }
    if (addr[0] != '0' || addr[1] != 'x') {
        return -1;
    }
    for (int i = 2; i < 42; ++i) {
        const char c = addr[i];
        const bool hex_digit = (c >= '0' && c <= '9') ||
                               (c >= 'a' && c <= 'f') ||
                               (c >= 'A' && c <= 'F');
        if (!hex_digit) {
            return -1;
        }
    }
    std::snprintf(g_escrow, sizeof(g_escrow), "%.*s", 42, addr);

    // Running gateway? Restart it on the new contract.
    if (proc_running(PROC_GATEWAY) != 0) {
        proc_stop(PROC_GATEWAY);
        return proc_gateway_start();
    }
    return 0;
}

const char* proc_escrow(void) {
    return g_escrow;
}

int proc_running(const int id) {
    if (id < 0 || id >= PROC_COUNT) {
        return 0;
    }
    proc_slot_t& slot = g_procs[id];
    if (slot.pid == 0) {
        return 0;
    }
    int status = 0;
    const pid_t r = waitpid(slot.pid, &status, WNOHANG);
    if (r == slot.pid) {
        slot.pid = 0;
        if (slot.log_fd >= 0) {
            close(slot.log_fd);
            slot.log_fd = -1;
        }
        return 0;
    }
    return 1;
}

int proc_stop(const int id) {
    if (id < 0 || id >= PROC_COUNT) {
        return -1;
    }
    proc_slot_t& slot = g_procs[id];
    if (slot.pid == 0) {
        return 0;
    }
    // The child spawned as its own process-group leader, so a group
    // kill reaches grandchildren too (go run → compiled binary).
    kill(-slot.pid, SIGTERM);
    for (int i = 0; i < 30; ++i) {
        if (proc_running(id) == 0) {
            return 0;
        }
        usleep(100000);
    }
    kill(-slot.pid, SIGKILL);
    int status = 0;
    waitpid(slot.pid, &status, 0);
    slot.pid = 0;
    if (slot.log_fd >= 0) {
        close(slot.log_fd);
        slot.log_fd = -1;
    }
    return -1; // needed the escalation
}

int proc_stop_all(void) {
    int worst = 0;
    for (int id = 0; id < PROC_COUNT; ++id) {
        if (proc_stop(id) != 0) {
            worst = -1;
        }
    }
    return worst;
}

int proc_drain_log(const int id, char* buf, const std::size_t cap) {
    if (buf == nullptr || cap < 2 || id < 0 || id >= PROC_COUNT) {
        return -1;
    }
    proc_slot_t& slot = g_procs[id];
    if (slot.log_fd < 0) {
        return -1;
    }
    const ssize_t r = read(slot.log_fd, buf, cap - 1);
    if (r <= 0) {
        buf[0] = '\0';
        return 0; // EAGAIN or child closed
    }
    buf[static_cast<std::size_t>(r)] = '\0';
    return static_cast<int>(r);
}

#else // _WIN32 — keep the UI target compilable on Windows; the console
      // workflow is POSIX-targeted for flat-sat (recorded on macOS).

void proc_init(void) {}
const char* proc_repo_root(void) { return ""; }
int proc_anvil_start(void) { return -1; }
int proc_gateway_start(void) { return -1; }
int proc_deploy_contract(void) { return -1; }
const char* proc_escrow(void) { return ""; }
int proc_running(int) { return 0; }
int proc_stop(int) { return -1; }
int proc_stop_all(void) { return -1; }
int proc_drain_log(int, char*, std::size_t) { return -1; }

#endif // _WIN32
