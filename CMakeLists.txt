cmake_minimum_required(VERSION 3.30...4.0)

# ── Project ─────────────────────────────────────────────────
project(mljit
  VERSION 0.1.0
  LANGUAGES CXX
  DESCRIPTION "A JIT-compiled subset of Standard ML"
  HOMEPAGE_URL "https://github.com/user/mljit"
)

# ── C++ standard: require C++26 ─────────────────────────────
if(NOT "cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  message(FATAL_ERROR "C++26 compiler required (GCC ≥14, Clang ≥19)")
endif()

# ── CMake modules ───────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# ── Enforce out-of-source builds ────────────────────────────
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not allowed.\n"
    "  Configure: cmake --preset debug\n"
    "  Or:        cmake -S . -B build -G Ninja"
  )
endif()

# ── Export compile commands ─────────────────────────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Export no package registry ──────────────────────────────
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)

# ── Options ─────────────────────────────────────────────────
option(BUILD_TESTING "Build tests" ON)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# ── CTest integration ───────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
  enable_testing()
endif()

# ── Subdirectories ──────────────────────────────────────────
add_subdirectory(src)

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(test)
endif()
