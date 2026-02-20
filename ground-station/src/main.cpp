
/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      CLI Entry point for the Void Protocol Ground Station (C++).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <cstdio>
#include <cstring>
#include <cstdint>

// --- CLI Constants ---
constexpr size_t CLI_INPUT_MAX = 16;

// --- CLI Command Handlers (Stubs) ---
static void trigger_handshake() {
	std::puts("[INFO] Handshake triggered.");
}

static void approve_buy() {
	std::puts("[INFO] Buy approved.");
}

static void shutdown() {
	std::puts("[INFO] Shutting down ground station.");
}

// --- Main CLI Loop ---
int main() {
	char input[CLI_INPUT_MAX] = {0};
	std::puts("\n💻 CLI Ready. Commands: 'h' (Handshake), 'ack' (Approve Buy), 'exit' (Quit)");

	while (true) {
		std::fputs("CLI> ", stdout);
		if (!std::fgets(input, sizeof(input), stdin)) {
			std::puts("[ERROR] Input error. Exiting.");
			break;
		}
		// Remove trailing newline
		size_t len = std::strlen(input);
		if (len > 0 && input[len - 1] == '\n') {
			input[len - 1] = '\0';
		}

		// Convert to lowercase (ASCII only)
		for (size_t i = 0; i < len; ++i) {
			if (input[i] >= 'A' && input[i] <= 'Z') {
				input[i] = static_cast<char>(input[i] + ('a' - 'A'));
			}
		}

		if (std::strcmp(input, "h") == 0) {
			trigger_handshake();
		} else if (std::strcmp(input, "ack") == 0) {
			approve_buy();
		} else if (std::strcmp(input, "exit") == 0) {
			shutdown();
			break;
		} else if (input[0] != '\0') {
			std::puts("[WARN] Unknown command. Try 'h', 'ack', or 'exit'.");
		}
	}

	return 0;
}
