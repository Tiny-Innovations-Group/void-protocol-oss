# =====================================================================
# 🛰️ VOID PROTOCOL: Core Validation Suite
# =====================================================================
cmake_minimum_required(VERSION 3.14)
# We must include 'C' here because libsodium is a C library
project(VoidCoreTests LANGUAGES CXX C)

# Enforce strict C++14 standard as per .cursorrules
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# --- 1. Fetch GoogleTest (Cross-Platform) ---
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


FetchContent_Declare(Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a # HEAD, last updated at 2025-04-13
)

set(SODIUM_DISABLE_TESTS ON)
FetchContent_MakeAvailable(Sodium)

# --- 3. Shared test sources ---
# Single source list reused across both tier test binaries so the Go/C++
# regression pack (VOID-124 + VOID-125) exercises CCSDS and SNLP from the
# same test code.
set(VOID_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_security_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_packing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_bouncer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_packet_sizes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_offsets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_golden_vectors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sign_verify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_packet_d_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../void-core/src/security_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../void-core/src/packet_d_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/src/bouncer.cpp
)

set(VOID_TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../void-core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/include
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${libsodium_BINARY_DIR}/src/libsodium/include
)

# Absolute path to the checked-in golden vectors (VOID-123). Exposed to the
# test binaries as a compile definition so test_golden_vectors.cpp and
# test_sign_verify.cpp can open them without CWD assumptions.
set(VOID_TEST_VECTORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../test/vectors")

# --- 4. Per-tier test executables ---
# VOID-125 requires the four new tests to exercise BOTH wire tiers. The
# packet routing in void_packets.h is #define-driven, so only one tier is
# visible per translation unit; we therefore build one test binary per
# tier, sharing the same sources.
function(void_add_tier_suite target tier_type tier_name)
    add_executable(${target} ${VOID_TEST_SOURCES})
    target_include_directories(${target} PRIVATE ${VOID_TEST_INCLUDES})
    target_link_libraries(${target} PRIVATE gtest_main sodium)
    target_compile_definitions(${target} PRIVATE
        VOID_PROTOCOL_TYPE=${tier_type}
        VOID_TEST_VECTORS_DIR="${VOID_TEST_VECTORS_DIR}"
        VOID_TEST_VECTORS_TIER="${tier_name}"
    )
    gtest_discover_tests(${target}
        TEST_PREFIX "${tier_name}."
    )
endfunction()

enable_testing()
include(GoogleTest)

void_add_tier_suite(void_full_tests       2 "snlp")
void_add_tier_suite(void_full_tests_ccsds 1 "ccsds")
