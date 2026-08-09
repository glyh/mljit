# MLJIT CLI Contract

This document specifies the command-line contract of the `mljit` binary: its
subcommands, phase-dump vocabulary, execution modes, diagnostics, exit codes,
and — the point of having a written contract — which parts of the surface are
stable and to what degree. Settled in the design-map ticket
[Define the Phase-Dump CLI Contract](wayfinder/tickets/define-phase-dump-cli-contract.md).

## Design goals

- **One pipeline, four verbs**: the CLI is a thin dispatch over the existing
  pipeline (frontend → verifier → interpreter | regalloc → x64 JIT); verbs
  select what to do with the result, flags select variants.
- **Script-composable**: stdout carries exactly the requested artifact;
  diagnostics go to stderr; exit codes distinguish "your program is wrong"
  from "your invocation is wrong".
- **Explicit stability tiers**: every output is byte-stable, field-stable, or
  unstable — named below, so golden tests and scripts know what they may pin.
- **Dependency-light**: argument parsing is hand-rolled; this contract names
  no CLI library (see [Dependency Policy](dependency-policy.md)).

## Invocation model

Subcommand style, git/cargo dialect:

```
mljit <verb> [flags] <file> [args...]
mljit --help | --version
```

- `<file>` is a frontend source file; `-` means read source from stdin
  (diagnostics then cite `<stdin>`). Textual-IR input is future work
  (no parser yet — see the design map's Fog).
- `--help` works bare and per-verb; `mljit --version` prints one line.
- Unknown verbs, flags, phases, or entries are usage errors (exit 2).

## Verbs

### `mljit run`

```
mljit run [--backend=jit|interp] [--entry=<name>] <file> [i64 args...]
```

Compile and execute one function, print its result.

- **Entry point**: the function named `main` by default; `--entry=<name>`
  runs any function in the module (the fib/gcd demos are plain functions).
- **Arguments**: trailing positionals are parsed as signed decimal i64 and
  passed by position. Arity mismatch is a clean error before execution.
- **Backend**: `--backend=jit` (**default**) or `--backend=interp`. There is
  no silent fallback: if the JIT does not yet support a construct the
  program uses, `run` fails with a diagnostic prefixed `error: jit:` naming
  the construct and suggesting `--backend=interp` (exit 1). The default is
  stable; going native by default is the point of the project.
- **Output**: the returned i64 on stdout as a bare decimal line — nothing
  else on stdout. Exit code 0 on success; the return *value* is never the
  exit code.

### `mljit dump`

```
mljit dump --phase=<name> [--phase=<name> ...] <file>
```

Emit a phase's deterministic textual view on stdout and exit; no execution.

Phase vocabulary:

| phase      | status       | view                                                     |
| ---------- | ------------ | -------------------------------------------------------- |
| `ssa`      | shipped      | `mljit.ir.printer::to_text` per [ir-format.md](ir-format.md) |
| `regalloc` | shipped      | the 4-section `regalloc::dump_regalloc` view, verbatim    |
| `x64`      | **reserved** | disassembly-style listing of emitted machine code         |

- `--phase` is repeatable; multiple phases print in pipeline order, each
  preceded by a `;; == <phase> ==` separator line.
- There is deliberately **no `mir` phase**: MLJIT has no materialized
  machine IR (see the Transient Machine IR Boundary decision); `regalloc`
  *is* the machine-level view.
- `dump` covers the whole module. `ssa` is already a module view; `regalloc`
  is per function, so when a module holds more than one function each
  function's dump is preceded by a `;; @<name>` line. A single-function
  module dumps verbatim — framing lines appear only where something needs
  disambiguating.
- Unknown phase names are usage errors (exit 2) listing the valid set.
- The CLI emits the underlying printers' output verbatim, so the existing
  golden tests for those printers double as CLI-output tests.

### `mljit check`

```
mljit check <file>
```

Frontend + verifier only. Prints all diagnostics (both error streams already
accumulate rather than abort) and exits 0/1. Silent on success — no banner.

### `mljit bench`

```
mljit bench [--entry=<name>] [--warmup=N] [--iters=N] <file> [i64 args...]
```

Interpreter-vs-native comparison on the same input; same entry/args grammar
as `run`, but always runs **both** backends.

- **Correctness gate**: both backends must return the same value; a mismatch
  is a loud exit-1 failure. Every benchmark run is a differential test.
- **Methodology**: `--warmup=N` (default 3) discarded iterations, then
  `--iters=N` (default 10) timed iterations per backend; report the
  **median** wall time plus min/max. Compile/JIT time is reported separately
  from execution time.
- **Output**: a human-readable table on stdout with, per backend: result,
  compile time, median/min/max execution time; then a final `speedup: N.Nx`
  line. The table is **field-stable, not byte-stable** — fields will not
  vanish, exact text and whitespace may improve. A `--json` output mode is
  reserved but unspecified.

## Diagnostics

All diagnostics go to stderr, one line per error, all errors printed:

- Frontend (has source locations):
  `file.ml:3:7: error: unbound variable 'x'`
- Verifier (has IR locations):
  `file.ml: verify error in @fib/^loop: <detail>`
- JIT capability gaps:
  `file.ml: error: jit: unsupported <construct>; try --backend=interp`

The `file:line:col: error:` shape is field-stable; message text is not.

## Exit codes

| code | meaning                                                                  |
| ---- | ------------------------------------------------------------------------ |
| 0    | success                                                                  |
| 1    | the program is at fault: parse/verify error, runtime trap, arity or bench mismatch, JIT-unsupported construct |
| 2    | the invocation is at fault: unknown verb/flag/phase/entry, missing file  |

No finer per-error-kind codes: nobody scripts against them and they ossify
internals.

## Stability tiers

| tier             | surface                                                                                       |
| ---------------- | --------------------------------------------------------------------------------------------- |
| **byte-stable**  | `dump` phase outputs (inherit the [ir-format.md](ir-format.md) snapshot contract; likewise the regalloc dump); `run`'s stdout (bare result line) |
| **field-stable** | `bench` table fields; diagnostic shape (`file:line:col: error:` prefix); exit-code meanings   |
| **unstable**     | everything else: help text, error message wording, stderr ordering                            |
| **reserved**     | the `x64` dump phase; `bench --json`; the **`--trace*` flag namespace**, owned by the Design Structured Trace Events ticket — this contract deliberately does not define it |
