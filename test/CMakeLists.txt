# ── Test suite: Catch2 ────────────────────────────────────────
find_package(Catch2 3 REQUIRED)

# ── Test executable ──────────────────────────────────────────
add_executable(mljit-tests test_ir.cpp test_ir_verify.cpp test_ir_runtime.cpp)

target_link_libraries(mljit-tests PRIVATE
  mljit::lib
  Catch2::Catch2WithMain
)

target_compile_features(mljit-tests PUBLIC cxx_std_26)
set_project_warnings(mljit-tests)
enable_sanitizers(mljit-tests)

# Catch2 uses __COUNTER__ which Clang 22+ warns about as a C2y extension.
# Suppress so -Werror doesn't break the build on up-to-date toolchains.
target_compile_options(mljit-tests PRIVATE -Wno-c2y-extensions)

# ── CTest discovery ──────────────────────────────────────────
include(Catch)
catch_discover_tests(mljit-tests)
