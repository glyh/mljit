# ── ANTLR4 C++ frontend ─────────────────────────────────────
# Isolated in its own subdirectory; no ANTLR types leak into core IR targets.

find_package(antlr4-runtime REQUIRED)
get_target_property(ANTLR4_INCLUDE_DIR antlr4_static INTERFACE_INCLUDE_DIRECTORIES)

# Locate ANTLR4 tool (provided by Nix devshell or system PATH).
find_program(ANTLR4 antlr4 REQUIRED)

# Build-tree output directory for generated sources.
set(ANTLR4_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Generated C++ sources and headers produced by ANTLR.
set(ANTLR4_GENERATED_SOURCES
  "${ANTLR4_GENERATED_DIR}/MLJITLexer.cpp"
  "${ANTLR4_GENERATED_DIR}/MLJITParser.cpp"
  "${ANTLR4_GENERATED_DIR}/MLJITBaseVisitor.cpp"
  "${ANTLR4_GENERATED_DIR}/MLJITVisitor.cpp"
)
set(ANTLR4_GENERATED_HEADERS
  "${ANTLR4_GENERATED_DIR}/MLJITLexer.h"
  "${ANTLR4_GENERATED_DIR}/MLJITParser.h"
  "${ANTLR4_GENERATED_DIR}/MLJITBaseVisitor.h"
  "${ANTLR4_GENERATED_DIR}/MLJITVisitor.h"
)

# Custom command: generate ANTLR C++ output into the build tree.
add_custom_command(
  OUTPUT
    ${ANTLR4_GENERATED_SOURCES}
    ${ANTLR4_GENERATED_HEADERS}
  COMMAND
    ${ANTLR4} -Dlanguage=Cpp -visitor -no-listener
              -package "mljit::frontend::grammar"
              -o "${ANTLR4_GENERATED_DIR}"
              "${CMAKE_CURRENT_SOURCE_DIR}/MLJIT.g4"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/MLJIT.g4"
  COMMENT "Generating ANTLR4 C++ frontend from MLJIT.g4"
)

# Generated sources (C++17, -w to suppress unavoidable warnings)
add_library(mljit-antlr-gen OBJECT ${ANTLR4_GENERATED_SOURCES})
target_compile_features(mljit-antlr-gen PUBLIC cxx_std_17)
target_include_directories(mljit-antlr-gen PRIVATE
  "${ANTLR4_INCLUDE_DIR}"
  "${ANTLR4_GENERATED_DIR}")
target_link_libraries(mljit-antlr-gen PRIVATE antlr4_static Threads::Threads)
# Suppress unavoidable warnings in generated code (e.g. unused _localctx in sempred)
target_compile_options(mljit-antlr-gen PRIVATE -Wno-unused-parameter)

# Wire ANTLR runtime and generated sources into the main library.
# mljit-lib is defined in the parent scope (src/CMakeLists.txt).
target_link_libraries(mljit-lib PRIVATE antlr4_static mljit-antlr-gen)
target_include_directories(mljit-lib PRIVATE
  "${ANTLR4_INCLUDE_DIR}"
  "${ANTLR4_GENERATED_DIR}")
