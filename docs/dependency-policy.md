# MLJIT Dependency Policy

This document captures dependency boundaries and library decisions. It is separate from the coding standard: dependency inspection is about project risk, build portability, and scope control, not code formatting or local style.

## Boundary

MLJIT should stay dependency-light in compiler core/runtime.

Allowed by default:

- build tooling dependencies;
- test dependencies;
- parser/lexer generator tooling once chosen;
- formatting/static-analysis tooling if it improves consistency without leaking into runtime/core APIs.

Requires explicit justification:

- compiler core/runtime dependencies;
- external backend/JIT abstractions;
- logging/tracing/profiling frameworks;
- broad utility frameworks that shape architecture.

Avoid by default:

- LLVM as a backend dependency;
- asmjit or external JIT abstractions;
- fmt/spdlog/Quill as core logging infrastructure;
- broad framework-heavy architecture;
- parser-combinator libraries for the frontend.

## Acquisition

Use Nix flakes for all dependencies (toolchain, build tools, libraries).

Dependency checks should answer:

1. Is this dependency isolated to tests/tooling, or does it enter compiler core/runtime?
2. Is it available through Nix/nixpkgs?
3. Does it preserve C++ modules/CMake simplicity?
4. Is it cheaper and safer than a small local implementation?
5. Does it improve the project signal, or obscure the compiler/JIT work behind a library?

## Current accepted dependencies

### Catch2

Status: accepted.

Scope:

- test-only;
- acquired through Nix flake;
- integrated through CMake/CTest;
- must not leak into `mljit-lib` or compiler core APIs.

## Current rejected/deferred dependencies

### Pattern matching libraries

Status: rejected for now.

Use `std::variant` + `std::visit` with a tiny local `overload` helper. Reconsider a library only if richer destructuring becomes clearly valuable.

### Tracing/profiling/logging frameworks

Status: rejected/deferred for now.

Checked options include Perfetto, Tracy, OpenTelemetry, LTTng, spdlog, and Quill. They are currently too heavy, not test-focused, or aimed at profiling/logging rather than MLJIT's compiler phase event needs.

Current recommendation:

- local `mljit.trace` module;
- in-memory event records for tests;
- JSONL and/or Chrome Trace Event export for inspection;
- no JSON library until the schema outgrows simple hand-written output.

Reconsider Perfetto or Tracy only if MLJIT needs professional timeline/profiling visualization beyond simple phase events.

### External x64 assembler / JIT libraries

Status: rejected for project goals.

MLJIT should build a project-owned x64 assembler layer. This is part of the systems/JIT signal and should not be replaced by asmjit-style abstractions unless the project direction changes.

### LLVM backend

Status: rejected for project goals.

LLVM would hide the backend work MLJIT is intended to demonstrate. It may be useful as an external comparison point later, but not as the primary backend.
