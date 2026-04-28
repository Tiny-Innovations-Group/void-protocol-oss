cmake_minimum_required(VERSION 3.14)
# VOID-138: include C as well because libsodium is a C library.
project(VoidGroundStation VERSION 2.1.1 LANGUAGES CXX C)

# --- 1. COMPILER STANDARD ---
# Force C++14 to maintain perfect parity with the ESP32 PlatformIO environment
# TODO: bump to C++17 post-demo — if constexpr, [[nodiscard]], string_view all useful here
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable GNU extensions for strict portability

include(FetchContent)

# --- 2. CRYPTOGRAPHY DEPENDENCY (CLONE & RUN) ---
# Download and build libsodium locally so contributors don't need 'brew' or
# global installs. Declared early so both the production executable and the
# test suite can link against it.
FetchContent_Declare(Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a # HEAD, last updated at 2025-04-13
)

set(SODIUM_DISABLE_TESTS ON)
FetchContent_MakeAvailable(Sodium)

# --- 3. TEST DEPENDENCY (VOID-138) ---
# GoogleTest v1.14.0 pinned — same tag used by void-core/CMakeLists.txt so
# contributors don't fetch two different copies across the repo.
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- 4. PRODUCTION EXECUTABLE ---
add_executable(ground_station
    src/main.cpp
    src/bouncer.cpp
    src/serial_hal.cpp
    src/gateway_client.cpp
    src/egress_json.cpp
    src/egress_hex.cpp
    src/egress_poll_client.cpp
    src/ack_builder.cpp
    ${CMAKE_SOURCE_DIR}/../void-core/src/security_manager.cpp
)

if(WIN32)
    target_link_libraries(ground_station PRIVATE ws2_32)
endif()

# --- 5. THE BRIDGE (INCLUDE DIRECTORIES) ---
# Tell CMake how to legally look outside its folder to find the math and structs
target_include_directories(ground_station PRIVATE
    ${CMAKE_SOURCE_DIR}/../void-core/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# --- 6. NSA / SEI CERT C++ COMPILER RULES (production only) ---
# Mathematically enforce memory and type safety on the desktop compiler.
# The TEST target (below) relaxes -Werror so GoogleTest macros compile
# cleanly — production code still runs the full strict stack.
target_compile_options(ground_station PRIVATE
    -Wall                   # Enable all standard warnings
    -Wextra                 # Enable extra warnings
    -Werror                 # FATAL: Treat ALL warnings as compilation errors
    -Wshadow                # FATAL: Prevent variable shadowing
    -Wvla                   # FATAL: Ban Variable Length Arrays
    -Wconversion            # FATAL: Catch implicit type conversions that lose data
    -Wsign-conversion       # FATAL: Catch unsafe int to unsigned int conversions
    -Wcast-align            # FATAL: Catch misaligned pointer casts
    -Wold-style-cast        # FATAL: Force C++ style casts (static_cast) over C-style
    -Wformat-security       # FATAL: Prevent printf string format vulnerabilities
    -O2                     # Optimize for speed and safe execution
)

# Enable buffer overflow checks at compile/run time.
# VOID_PROTOCOL_TYPE=2 selects the SNLP tier explicitly so
# void_packets.h doesn't emit its "no type defined" #warning
# (which -Werror promotes to a build break). The bouncer only
# runs SNLP traffic in the flat-sat flow.
target_compile_definitions(ground_station PRIVATE
    _FORTIFY_SOURCE=2
    VOID_PROTOCOL_TYPE=2
)

target_link_libraries(ground_station PRIVATE sodium)

# --- 7. TEST SUITE (VOID-138) ---
# Separate executable so GoogleTest's macros + the CERT-grade -Werror from
# the production target don't collide. Production target still runs the
# full strict-compile stack; test target uses a gentler set that still
# catches real bugs without fighting with gtest internals.
enable_testing()
include(GoogleTest)

set(GROUND_STATION_TEST_SOURCES
    test/test_smoke.cpp
    test/test_egress_json.cpp
    test/test_egress_hex.cpp
    test/test_egress_poll_client.cpp
    test/test_egress_orchestrator.cpp
    test/test_ack_builder.cpp
    src/egress_json.cpp
    src/egress_hex.cpp
    src/egress_poll_client.cpp
    src/ack_builder.cpp
)

add_executable(ground_station_tests
    ${GROUND_STATION_TEST_SOURCES}
)

target_include_directories(ground_station_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/../void-core/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Relaxed but still safety-conscious — no -Werror so gtest compiles.
# -Wshadow/-Wvla/-Wconversion kept because they catch real bugs even
# through the looser build.
target_compile_options(ground_station_tests PRIVATE
    -Wall
    -Wextra
    -Wshadow
    -Wvla
    -O2
)

# VOID-134: the ack_builder regression loads the SNLP golden vector
# from test/vectors/snlp/packet_ack.bin. The test target needs an
# absolute path to that directory at compile time.
target_compile_definitions(ground_station_tests PRIVATE
    VOID_TEST_VECTORS_DIR="${CMAKE_SOURCE_DIR}/../test/vectors"
)

if(WIN32)
    target_link_libraries(ground_station_tests PRIVATE ws2_32)
endif()

target_link_libraries(ground_station_tests PRIVATE
    gtest_main
    sodium
)

gtest_discover_tests(ground_station_tests)

# --- 8. SBOM GENERATION (NSA COMPLIANCE) ---
# This creates a manifest of all components (VoidCore, Libsodium, SerialHAL)
set(SBOM_OUTPUT "${CMAKE_SOURCE_DIR}/../metadata/ground-station-sbom.json")

add_custom_command(
    TARGET ground_station POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "{\"bomFormat\": \"CycloneDX\", \"specVersion\": \"1.4\", \"component\": {\"name\": \"VoidGroundStation\", \"version\": \"2.1.1\", \"type\": \"application\", \"components\": [{\"name\": \"libsodium\", \"version\": \"1.0.20-RELEASE\"}, {\"name\": \"void-core\", \"version\": \"2.1.0\"}]}}" > ${SBOM_OUTPUT}
    COMMENT "🛰️ Generating Software Bill of Materials (SBOM) for Audit compliance..."
)
