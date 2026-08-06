---
title: Define the Phase-Dump CLI Contract
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: glyh
blocked_by:
  - specify-textual-ssa-ir-format.md
---

# Define the Phase-Dump CLI Contract

## Question

What stable CLI contract should expose compiler phase dumps and execution modes from day one?

The answer should define commands/options for emitting SSA, verifier diagnostics, interpreter traces, later machine IR/regalloc/x64 listings, selecting interpreter vs JIT execution, and running benchmarks. It should distinguish Milestone 1 commands from future placeholders.

## Resolution (2026-08-06)

Settled via grilling; the full contract is codified as [docs/cli.md](../../cli.md). The decisions:

- **Invocation model: subcommands** (git/cargo style), not compiler-driver flags. Four verbs, no others: `run`, `dump`, `check`, `bench`. `run`/`dump`/`check` ship now; `bench` is specified here but lands with the benchmark milestone. Input is a frontend source file or `-` for stdin; textual-IR input stays deferred with the parser fog.
- **`dump` phase selection**: a single repeatable enum flag `--phase=<name>`; multiple phases print in pipeline order separated by `;; == <phase> ==` lines. Vocabulary today: `ssa` (the ir-format.md printer view) and `regalloc` (the 4-section `dump_regalloc` view, verbatim — existing golden tests double as CLI tests); `x64` is the one reserved phase. **No `mir` phase, ever** — recorded so the Transient-MIR decision isn't accidentally undone.
- **Execution backend**: `--backend=jit|interp`, **default `jit`** with no silent fallback — a JIT capability gap is a clean `error: jit: unsupported <construct>; try --backend=interp` diagnostic (exit 1), never an assert. This makes the two known emitter gaps (cross-function calls, i1 materialization) scheduled feature work rather than contract shape.
- **`run` semantics**: entry is `main` by default, `--entry=<name>` overrides; trailing positionals are decimal i64 arguments passed by position, arity-checked before execution; stdout is exactly the returned i64 as a bare decimal line; the return value is never the exit code.
- **Diagnostics & exit codes**: GCC-style `file:line:col: error:` lines on stderr, all accumulated errors printed; verifier errors cite IR locations (`@fn/^block`). Exit codes: 0 success, 1 program-at-fault, 2 invocation-at-fault; no finer taxonomy. `check` = frontend + verifier, silent on success.
- **`bench`**: same entry/args grammar as `run`; always both backends; result-equality gate (mismatch = loud exit 1, a free differential test per run); `--warmup` (3) + `--iters` (10), median/min/max wall time with compile time reported separately; human table + `speedup: N.Nx` line, **field-stable not byte-stable**; `--json` reserved.
- **Stability tiers written into the doc**: byte-stable (dump outputs, `run` stdout), field-stable (bench fields, diagnostic shape, exit codes), unstable (wording, help), reserved (`x64` phase, `bench --json`, and the whole `--trace*` namespace, which is owned by the Design Structured Trace Events ticket).
- Argument parsing is hand-rolled per the dependency policy; no CLI library is named.
