cmake_minimum_required(VERSION 3.14)
project(VoidGroundStation VERSION 2.1.1 LANGUAGES CXX)

# --- 1. COMPILER STANDARD ---
# Force C++14 to maintain perfect parity with the ESP32 PlatformIO environment
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disable GNU extensions for strict portability

# --- 2. DEFINE EXECUTABLE ---
add_executable(ground_station
    src/main.cpp
    src/bouncer.cpp
    ${CMAKE_SOURCE_DIR}/../void-core/src/security_manager.cpp   # <--- ADD THIS LINE
)

# --- 3. THE BRIDGE (INCLUDE DIRECTORIES) ---
# Tell CMake how to legally look outside its folder to find the math and structs
target_include_directories(ground_station PRIVATE
    ${CMAKE_SOURCE_DIR}/../void-core/include
    ${CMAKE_SOURCE_DIR}/src
)

# --- 4. NSA / SEI CERT C++ COMPILER RULES ---
# Mathematically enforce memory and type safety on the desktop compiler
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

# Enable buffer overflow checks at compile/run time
target_compile_definitions(ground_station PRIVATE
    _FORTIFY_SOURCE=2
)

# --- 5. CRYPTOGRAPHY DEPENDENCY (CLONE & RUN) ---
# Download and build libsodium locally so contributors don't need 'brew' or global installs.
include(FetchContent)

FetchContent_Declare(Sodium
    GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
    GIT_TAG e5b985ad0dd235d8c4307ea3a385b45e76c74c6a # HEAD, last updated at 2025-04-13
)

set(SODIUM_DISABLE_TESTS ON)
FetchContent_MakeAvailable(Sodium)

target_link_libraries(ground_station PRIVATE sodium)
