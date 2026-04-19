/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_smoke.cpp
 * Desc:      VOID-138 bootstrap smoke test — proves the GoogleTest +
 *            ctest toolchain is wired up for the ground-station/ tree.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>

// When this test executes, it proves:
//   • GoogleTest is fetched, built, and linked against the test target.
//   • gtest_main supplies the executable's main().
//   • gtest_discover_tests scanned this binary at CMake configure time
//     and registered individual test cases with ctest.
//   • `ctest --test-dir ground-station/build` invokes this executable
//     and reports results.
//
// Subsequent PRs under VOID-138 (JSON scanner, hex decoder, HTTP client,
// poll orchestrator) will replace this smoke with real assertions.
TEST(GroundStationSmoke, ToolchainIsWired) {
    EXPECT_EQ(1 + 1, 2);
}
