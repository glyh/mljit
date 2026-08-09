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

<!-- MILESTONE (2026-07-18): the v1 native x86-64 backend runs end to end. SSA ->
numbering -> live intervals -> linear-scan allocation -> move resolution -> direct
x64 emission -> mmap'd executable. Both benchmark-demo functions compile to native
machine code and match the SSA interpreter across inputs: iterative gcd (loop,
block-parameter moves, irem) and recursive fib (self-calls, callee-saved registers,
stack alignment). Everything project-owned — no LLVM, no asmjit. -->

<!-- MILESTONE (2026-08-09): the CLI driver ships, so the pipeline is drivable from
a source file end to end (`run`/`dump`/`check`). Register spilling landed with the
allocator. Remaining v1 gaps: cross-function calls (design settled, implementation
pending) and the i1-as-value emission refinement — both now surface as clean
`error: jit: unsupported ...` diagnostics with an interpreter fallback rather than
asserts. -->

- [Update PROJECT.md for the New Design Direction](tickets/update-project-spec-for-new-design.md) — replaced stale `PROJECT.md` with a short backend-first `README.md` that states current status and links to the design map.
- [Plan vcpkg and Catch2 Integration](tickets/plan-vcpkg-catch2-integration.md) — added `vcpkg.json` manifest, wired Catch2 v3 + CTest discovery, and verified with 3/3 passing smoke tests. **Superseded:** Nix now supplies all dependencies; vcpkg has been dropped.
- [Design the Block-Parameter SSA IR Data Model](tickets/design-block-parameter-ssa-ir.md) — settled the Milestone 1 IR package boundary: minimal module-owned functions, function-local block/value/instruction IDs, entry-block params, `i64`/`i1`, separate terminators, variant payloads, builder-controlled construction, and first dump tests for `add1` plus block-param `abs`.
- [Define the SSA Verifier Rules](tickets/define-ssa-verifier-rules.md) — settled `mljit.ir.verifier`: accumulate deterministic typed `VerifyError`s, reject unreachable blocks, use private Cooper-Harvey-Kennedy dominance, and cover valid IR plus one negative test per major invariant with test-only unsafe mutation helpers.
- [Design the i64 SSA Interpreter](tickets/design-i64-ssa-interpreter.md) — settled `mljit.ir.runtime`: reusable `runtime::Interpreter` over a verified immutable module view, explicit continuation stack with `return_target`, dense word environments, checked arithmetic/division, `idiv`/`irem` full vertical slice, and fib/gcd runtime acceptance tests.
- [Choose the Frontend Stack](tickets/choose-generated-frontend-stack.md) — settled on ANTLR4 (grammar → generated lexer/parser/visitor), isolated to `mljit.frontend` module via `src/frontend.cppm`; public API returns project-owned `ir::Module`; see `docs/dependency-policy.md` for scoping.
- [Design the Project-Owned x64 Assembler Layer](tickets/design-x64-assembler-layer.md) — settled `mljit.x64` module (`src/x64.cppm`), namespace `mljit::x64`: Gpr enum, Mem operand, Label/fixup system, RAII ExecBuffer (mmap + `no_sanitize("function")`), assembler surface for i64 mov/add/sub/imul/idiv/cqo/cmp/setCC/movzx/push/pop/jmp/jCC/call/ret, and 42 golden-byte + exec smoke tests.
- [Design the Transient Machine IR Boundary](tickets/design-transient-machine-ir-boundary.md) — settled: **no materialized MIR for v1**. The numbering + Allocation + Resolution side-tables over untouched SSA *are* the transient per-function backend state; the emitter is a **direct fold over SSA** into the x64 code buffer (no machine-instruction IR, no `--emit-mir` — `--emit-regalloc` is the machine-level view). Records the emitter lowering rules: rbp-frame prologue/epilogue + callee-saved save, ABI-move params in, 3-address→2-address ALU, idiv/irem via rax:rdx, icmp/branch fusion, System V call lowering, rax return. Calling convention applied entirely at emission.
- [Implement Linear Scan Register Allocation](tickets/implement-linear-scan-regalloc.md) — shipped the v1 allocator + spill-aware emission: pure side-table pipeline in `mljit.regalloc`, whole-interval Belady spilling with `r10`/`r11` reserved as emission scratch (12 allocatable), emission-time ABI moves instead of arg/return pinning, and the consolidated 4-section `dump_regalloc` (`--emit-regalloc` view); verified by golden dumps, structural invariants, pressure sweeps, and a 30-live-value interpreter-vs-native differential test.
- [Design Linear Scan Register Allocation](tickets/design-linear-scan-regalloc.md) — settled the v1 allocator: **Wimmer-Franz linear scan on SSA** (holes; block-params = phi); a **pure `(Function, Liveness) -> Allocation` side-table** leaving SSA untouched; **System V ABI**, reserve rsp+rbp, **14 allocatable** regs with forced uses (idiv rax/rdx, call clobbers, arg/return regs) modeled as **fixed intervals**; **one 8-byte spill slot per value**, 16-aligned frame; move resolution via topological ordering with **`xchg` cycle-breaking** (adds `xchg_rr` to `mljit.x64`) and **on-demand critical-edge splitting**; deterministic 4-section `--emit-regalloc` dump; and structural-invariant + interpreter-oracle differential tests driven by a `make_pressure_fn(N)` generator across the 14-reg threshold. Quality optimizations (stack-slot reuse, freeing rbp, callee-saved preference) deferred to Fog.

- [Specify the Textual SSA IR Format](tickets/specify-textual-ssa-ir-format.md) — codified the existing printer syntax as [docs/ir-format.md](../ir-format.md): line-oriented deterministic dumps, `@func`/`^block`/bare-value sigils, block-parameter SSA with mandatory branch-arg parens, full EBNF, a snapshot-stability contract, and verbatim fib/gcd examples pinned by `[ir][spec]` golden tests.
- [Define the Phase-Dump CLI Contract](tickets/define-phase-dump-cli-contract.md) — codified as [docs/cli.md](../cli.md): subcommand style with exactly `run`/`dump`/`check`/`bench`; repeatable `--phase=ssa|regalloc` (+ reserved `x64`, never `mir`); `--backend=jit|interp` **defaulting to `jit`** with clean `error: jit:` diagnostics instead of silent fallback; `--entry` + positional i64 args; GCC-style stderr diagnostics with 0/1/2 exit codes; both-backend `bench` with a result-equality gate; and explicit byte-/field-/unstable/reserved stability tiers (`--trace*` reserved for the trace-events ticket).
- [Implement the mljit CLI Driver](tickets/implement-cli-driver.md) — shipped `run`/`dump`/`check` as a new `mljit.driver` module (`src/driver.cppm`), leaving `main.cpp` a 17-line shell: hand-rolled verb/flag dispatch with positionals after the file operand (so negative i64 args parse), file and `-`/stdin input, phase dumps emitted verbatim, and GCC-style stderr diagnostics on 0/1/2 exit codes. `driver::run` writes into caller-owned strings and reads an injected `istream`, so all 30 new `[cli]` tests run in process (suite 109 → 139). The four `codegen` assert sites became a pre-emission `jit_gap` scan reporting `error: jit: unsupported ...; try --backend=interp`; `bench` is rejected as unimplemented.
- [Design Module-Level JIT Compilation (Cross-Function Calls)](tickets/design-module-level-jit-compilation.md) — settled: **one `Assembler`/`ExecBuffer` per module**, **eager whole-module** compilation in declaration order, and cross-function calls lowered as the **shortest encoding — `call rel32`** (5 bytes, no register cost, no indirection). Needs no new machinery: the existing intra-function `Label`/`Fixup` rel32 system is promoted from function to module lifetime, so mutual recursion resolves through the normal forward-fixup path and `finalize()` still asserts every fixup bound. Eager is forced by the [CLI contract](../cli.md)'s promise of an up-front `error: jit: unsupported ...` — a lazy stub design would surface gaps mid-execution. `codegen::compile(mod, fid)` becomes a module-level entry point returning a `CompiledModule` owning the buffer plus a `FunctionId -> offset` table. **Accepted constraint:** rel32's ±2GB reach is sound only because every target shares one buffer, so host/runtime-helper calls and per-function recompilation/tiering are out of reach until a second call kind lands — deferred to [Support Out-of-Range and Host Call Targets](tickets/support-out-of-range-call-targets.md), with the driver itself in [Implement Module-Level Compilation](tickets/implement-module-level-compilation.md).

## Fog
- A textual-IR **parser** (round-tripping [docs/ir-format.md](../ir-format.md) back into `ir::Module`) is deferred until something needs IR-as-input — e.g. file-based test fixtures or a `--run-ir` CLI mode; the grammar was designed unambiguous so this stays possible.
- The exact shape of the mutable/ref-like backend layer is intentionally deferred until SSA IR and the first backend lowering artifacts exist.
- Machine IR persistence policy is decided: it should be transient per-function backend state. Its concrete instruction/value/block shape should be specified in [Design the Transient Machine IR Boundary](tickets/design-transient-machine-ir-boundary.md) after the first SSA IR implementation slice lands.
- Structured tracing policy is decided at a high level. Its exact event schema, sink API, and CLI/test integration should be specified in [Design Structured Trace Events](tickets/design-structured-trace-events.md) after the first verifier/interpreter phases exist; dependency decisions belong in [Dependency Policy](../dependency-policy.md) and implementation conventions belong in [Coding Standard](../coding-standard.md).
- Runtime representation beyond i64 is deferred: closures, heap objects, ADTs, tuples, strings, tagging, and GC/refcounting should only graduate after the i64 register backend is demonstrably working.
- Benchmark presentation details are deferred until fib/gcd run under both interpreter and native backend.
- CI/Nix caching strategy is deferred until the Nix flake is finalized and the Catch2 test target exists.
- Linear-scan register allocation v1 deliberately ships the simple-but-correct path; several quality optimizations are deferred until MLJIT ingests higher-pressure (e.g. machine-generated) code that can actually exercise them: (1) stack-slot reuse/coalescing for spilled values — v1 assigns one 8-byte slot per spilled SSA value; (2) freeing `rbp` for a 15th allocatable register via `rsp`-relative spill addressing — v1 reserves `rbp` as frame pointer; (3) a caller/callee-saved allocation preference (prefer callee-saved registers for values live across calls to avoid spills) — v1 relies on fixed-interval clobbers to force correct-but-unoptimized splits. These are settled to be revisited under [Design Linear Scan Register Allocation](tickets/design-linear-scan-regalloc.md), not open questions. Two further deviations were settled during implementation and are recorded in [Implement Linear Scan Register Allocation](tickets/implement-linear-scan-regalloc.md): (4) args/return/call-args use emission-time ABI moves instead of fixed-interval register pinning (ABI-correct, simpler; pinning would elide the boundary moves); and (5) spilling shipped as whole-interval Belady with `r10`/`r11` reserved as emission scratch (12 allocatable registers) — spill-by-split remains the deferred refinement that would reclaim both scratch registers and avoid whole-interval memory traffic.
