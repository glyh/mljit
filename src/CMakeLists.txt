# ── Library with module interface ────────────────────────────
add_library(mljit-lib STATIC)
add_library(mljit::lib ALIAS mljit-lib)

target_sources(mljit-lib
  PUBLIC
    FILE_SET CXX_MODULES
      BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
      FILES
        ir.cppm
        ir_printer.cppm
        ir_verifier.cppm
        ir_runtime.cppm
        frontend.cppm
        x64.cppm
        regalloc.cppm
)

target_compile_features(mljit-lib PUBLIC cxx_std_26)
set_project_warnings(mljit-lib)
enable_sanitizers(mljit-lib)

# ANTLR4 frontend wiring (generated sources + runtime link)
add_subdirectory(frontend)

# ── Executable ──────────────────────────────────────────────
add_executable(mljit main.cpp)

target_compile_features(mljit PUBLIC cxx_std_26)
target_link_libraries(mljit PRIVATE mljit-lib)
set_project_warnings(mljit)
enable_sanitizers(mljit)
