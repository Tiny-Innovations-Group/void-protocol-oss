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

# --- 3. Define the Test Executable ---
add_executable(void_full_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_security_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_packing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_bouncer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../void-core/src/security_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/src/bouncer.cpp
)

# --- 4. Include Directories ---
target_include_directories(void_full_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../void-core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../ground-station/include
    ${libsodium_SOURCE_DIR}/src/libsodium/include
    ${libsodium_BINARY_DIR}/src/libsodium/include
)

# --- 5. Link Libraries ---
target_link_libraries(void_full_tests PRIVATE 
    gtest_main 
    sodium
)

# --- 6. Register Tests ---
enable_testing()
include(GoogleTest)
gtest_discover_tests(void_full_tests)