---
title: Implement the mljit CLI Driver
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: closed
assignee: glyh
closed_date: 2026-08-09
blocked_by: []
---

# Implement the mljit CLI Driver

## Question

Implement the `run`, `dump`, and `check` verbs of the [CLI contract](../../cli.md) in `src/main.cpp`, replacing the bootstrap stub.

Scope per the contract: hand-rolled subcommand + flag dispatch (no CLI library); source-file and `-`/stdin input; `dump --phase=ssa|regalloc` with the `;; == <phase> ==` multi-phase separator; `run` with `--backend=jit|interp` (default `jit`), `--entry`, positional i64 args, bare-decimal-result stdout; GCC-style stderr diagnostics; exit codes 0/1/2. JIT capability gaps must surface as clean `error: jit: unsupported ...` diagnostics (exit 1), not asserts — a pre-emission scan of the function is acceptable. `bench` is out of scope; it lands with the benchmark milestone. Include CLI-level tests (exit codes, stdout byte-stability for `run`/`dump`).

## Resolution (2026-08-09)

Shipped. The driver lives in a module of its own, `mljit.driver`
(`src/driver.cppm`), so the whole CLI is unit-testable in process;
`src/main.cpp` is now a 17-line shell that packs `argv` into a vector, calls
`driver::run(args, std::cin, out, err)`, and forwards the streams and exit
code. What was decided while implementing:

- **The driver is process-pure.** `run` appends stdout/stderr text to caller
  owned `std::string`s and takes stdin as an `std::istream&`, so every CLI
  test — exit codes, byte-exact stdout, `-`/stdin input, `<stdin>`
  diagnostics — runs in the Catch2 binary with no subprocess and no golden
  files. 30 `[cli]` tests; the suite went 109 → 139, all green.
- **Hand-rolled dispatch, no CLI library** (dependency policy). Verb first,
  then `--name=value` flags, then the file operand; **everything after the
  file operand is a positional**, which is what keeps negative i64 arguments
  (`mljit run --entry=abs abs.ml -7`) from being read as flags. `--backend`
  and `--entry` only take the `=` form; the bare forms are usage errors.
- **JIT capability gaps are scanned, not asserted.** `jit_gap(mod, fid)` walks
  the function before emission and reports the first gap as
  `<file>: error: jit: unsupported <construct>; try --backend=interp`
  (exit 1). It covers all four assert sites in `src/codegen.cppm`:
  cross-function calls, an icmp result used as a value operand, a branch whose
  condition is not the immediately preceding icmp (the practical face of the
  i1-materialization gap — reachable from the frontend via
  `let c = x < 10; d = x + 1 in if c then d else 0 end`), and the >6
  parameter/argument ABI limit. No gap was closed; they stay scheduled feature
  work.
- **`bench` is rejected up front** as a known-but-unimplemented verb
  (exit 2), before its flags are parsed, so the message never depends on them.
- **Contract clarifications** folded back into [docs/cli.md](../../cli.md):
  `dump --phase=regalloc` runs per function, so in a multi-function module each
  function's dump is preceded by a `;; @<name>` line; a single-function module
  still dumps verbatim. Framing lines — that one and the `;; == <phase> ==`
  separator — appear only when something needs disambiguating, which is what
  keeps the printers' golden tests usable as CLI-output tests.
- **Exit-code readings the table did not enumerate**: a malformed i64 operand
  and an unknown `--entry` are invocation faults (2), matching "unknown
  verb/flag/phase/entry"; arity mismatch stays 1 as the table says.
  Frontend columns are reported 1-based (ANTLR counts from 0) to match the
  GCC-style `file:line:col:` shape.
