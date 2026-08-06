---
title: Implement the mljit CLI Driver
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: open
assignee:
blocked_by: []
---

# Implement the mljit CLI Driver

## Question

Implement the `run`, `dump`, and `check` verbs of the [CLI contract](../../cli.md) in `src/main.cpp`, replacing the bootstrap stub.

Scope per the contract: hand-rolled subcommand + flag dispatch (no CLI library); source-file and `-`/stdin input; `dump --phase=ssa|regalloc` with the `;; == <phase> ==` multi-phase separator; `run` with `--backend=jit|interp` (default `jit`), `--entry`, positional i64 args, bare-decimal-result stdout; GCC-style stderr diagnostics; exit codes 0/1/2. JIT capability gaps must surface as clean `error: jit: unsupported ...` diagnostics (exit 1), not asserts — a pre-emission scan of the function is acceptable. `bench` is out of scope; it lands with the benchmark milestone. Include CLI-level tests (exit codes, stdout byte-stability for `run`/`dump`).
