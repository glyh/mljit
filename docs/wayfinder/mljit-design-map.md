---
title: MLJIT Design Map
labels:
  - wayfinder:map
status: open
---

# MLJIT Design Map

## Notes

Domain: MLJIT is a hobby compiler/JIT project. Its primary signal is systems/JIT engineering plus modern C++ engineering, not broad language UX or deep type-theory novelty.

Skills every session should consult when relevant:

- `wayfinder` for design-map work.
- `domain-modeling` when terminology or project vocabulary is being defined.
- `grill-me` for unresolved design choices.
- `codebase-memory` for structural code exploration once implementation grows.

Standing preferences and constraints for this effort:

- Use C++26, Clang, CMake, Ninja, modules, and clangd-friendly project setup.
- Target any x86-64 Linux environment; do not initially target Windows, macOS, or non-x86 JIT backends.
- Use Nix flakes for portable dependency acquisition.
- Use Catch2 with CTest/CMake for tests.
- Keep compiler core/runtime dependency-light; dependency decisions live in [Dependency Policy](../dependency-policy.md), while implementation conventions live in [Coding Standard](../coding-standard.md).
- Use a tiny generated frontend plus direct IR construction in tests; frontend should not block IR/backend work.
- JIT capstone is a register-IR backend: block-parameter SSA → register allocation → x86-64 emission.
- Start with first-order functions only; add closures after the register backend works.
- Use block-parameter SSA from day one.
- First native backend is i64-only: arithmetic, comparisons, block jumps/branches with block parameters, first-order fixed-arity calls, and i64 returns.
- First wow demo should include recursive Fibonacci and iterative GCD for interpreter-vs-native benchmarking.
- Documentation should be architecture-heavy: README, design notes, IR spec, and JIT/codegen notes.
- Milestone 1 is the SSA IR package: data model, textual dump format, verifier, i64 interpreter, and hand-built fib/gcd IR tests; no x86 yet.
- Prefer value semantics and strongly typed IDs for the canonical IR, with controlled mutable/ref-like backend artifacts later.
- Start with a fixed explicit pass pipeline; evolve into a pass manager when the project has enough passes/backends to justify it.
- Use a function-at-a-time compilation driver: SSA is the only module-level round-trippable IR; Machine IR is transient per-function backend state; x64 emission streams into a code buffer.
- Build a project-owned x64 assembler layer; do not depend on asmjit.
- v1 register allocation should be linear scan with spill support.
- Expose stable textual phase dumps from day one.
- Use structured compiler/JIT phase tracing for observability; concrete event/API details live in [Design Structured Trace Events](tickets/design-structured-trace-events.md).

## Decisions so far

<!-- Closed tickets will be linked here as the map is worked. Charting decisions above are standing notes; future decision details live in their tickets. -->

- [Update PROJECT.md for the New Design Direction](tickets/update-project-spec-for-new-design.md) — replaced stale `PROJECT.md` with a short backend-first `README.md` that states current status and links to the design map.
- [Plan vcpkg and Catch2 Integration](tickets/plan-vcpkg-catch2-integration.md) — added `vcpkg.json` manifest, wired Catch2 v3 + CTest discovery, and verified with 3/3 passing smoke tests. **Superseded:** Nix now supplies all dependencies; vcpkg has been dropped.
- [Design the Block-Parameter SSA IR Data Model](tickets/design-block-parameter-ssa-ir.md) — settled the Milestone 1 IR package boundary: minimal module-owned functions, function-local block/value/instruction IDs, entry-block params, `i64`/`i1`, separate terminators, variant payloads, builder-controlled construction, and first dump tests for `add1` plus block-param `abs`.
- [Define the SSA Verifier Rules](tickets/define-ssa-verifier-rules.md) — settled `mljit.ir.verifier`: accumulate deterministic typed `VerifyError`s, reject unreachable blocks, use private Cooper-Harvey-Kennedy dominance, and cover valid IR plus one negative test per major invariant with test-only unsafe mutation helpers.
- [Design the i64 SSA Interpreter](tickets/design-i64-ssa-interpreter.md) — settled `mljit.ir.runtime`: reusable `runtime::Interpreter` over a verified immutable module view, explicit continuation stack with `return_target`, dense word environments, checked arithmetic/division, `idiv`/`irem` full vertical slice, and fib/gcd runtime acceptance tests.
- [Choose the Frontend Stack](tickets/choose-generated-frontend-stack.md) — settled on ANTLR4 (grammar → generated lexer/parser/visitor), isolated to `mljit.frontend` module via `src/frontend.cppm`; public API returns project-owned `ir::Module`; see `docs/dependency-policy.md` for scoping.
- [Design the Project-Owned x64 Assembler Layer](tickets/design-x64-assembler-layer.md) — settled `mljit.x64` module (`src/x64.cppm`), namespace `mljit::x64`: Gpr enum, Mem operand, Label/fixup system, RAII ExecBuffer (mmap + `no_sanitize("function")`), assembler surface for i64 mov/add/sub/imul/idiv/cqo/cmp/setCC/movzx/push/pop/jmp/jCC/call/ret, and 42 golden-byte + exec smoke tests.
- [Design Linear Scan Register Allocation](tickets/design-linear-scan-regalloc.md) — settled the v1 allocator: **Wimmer-Franz linear scan on SSA** (holes; block-params = phi); a **pure `(Function, Liveness) -> Allocation` side-table** leaving SSA untouched; **System V ABI**, reserve rsp+rbp, **14 allocatable** regs with forced uses (idiv rax/rdx, call clobbers, arg/return regs) modeled as **fixed intervals**; **one 8-byte spill slot per value**, 16-aligned frame; move resolution via topological ordering with **`xchg` cycle-breaking** (adds `xchg_rr` to `mljit.x64`) and **on-demand critical-edge splitting**; deterministic 4-section `--emit-regalloc` dump; and structural-invariant + interpreter-oracle differential tests driven by a `make_pressure_fn(N)` generator across the 14-reg threshold. Quality optimizations (stack-slot reuse, freeing rbp, callee-saved preference) deferred to Fog.

## Fog
- The exact shape of the mutable/ref-like backend layer is intentionally deferred until SSA IR and the first backend lowering artifacts exist.
- Machine IR persistence policy is decided: it should be transient per-function backend state. Its concrete instruction/value/block shape should be specified in [Design the Transient Machine IR Boundary](tickets/design-transient-machine-ir-boundary.md) after the first SSA IR implementation slice lands.
- Structured tracing policy is decided at a high level. Its exact event schema, sink API, and CLI/test integration should be specified in [Design Structured Trace Events](tickets/design-structured-trace-events.md) after the first verifier/interpreter phases exist; dependency decisions belong in [Dependency Policy](../dependency-policy.md) and implementation conventions belong in [Coding Standard](../coding-standard.md).
- Runtime representation beyond i64 is deferred: closures, heap objects, ADTs, tuples, strings, tagging, and GC/refcounting should only graduate after the i64 register backend is demonstrably working.
- Benchmark presentation details are deferred until fib/gcd run under both interpreter and native backend.
- CI/Nix caching strategy is deferred until the Nix flake is finalized and the Catch2 test target exists.
- Linear-scan register allocation v1 deliberately ships the simple-but-correct path; several quality optimizations are deferred until MLJIT ingests higher-pressure (e.g. machine-generated) code that can actually exercise them: (1) stack-slot reuse/coalescing for spilled values — v1 assigns one 8-byte slot per spilled SSA value; (2) freeing `rbp` for a 15th allocatable register via `rsp`-relative spill addressing — v1 reserves `rbp` as frame pointer; (3) a caller/callee-saved allocation preference (prefer callee-saved registers for values live across calls to avoid spills) — v1 relies on fixed-interval clobbers to force correct-but-unoptimized splits. These are settled to be revisited under [Design Linear Scan Register Allocation](tickets/design-linear-scan-regalloc.md), not open questions. Two further items surfaced during implementation and are recorded in [Implement Linear Scan Register Allocation](tickets/implement-linear-scan-regalloc.md): (4) the allocator narrows the design's fixed-interval scope — args/return/call-args use emission-time ABI moves instead of register pinning (ABI-correct, simpler); and (5) spilling is not yet implemented, and the design's spill-by-split vs. a simpler whole-interval Belady spill is still to be chosen when that slice is picked up.
