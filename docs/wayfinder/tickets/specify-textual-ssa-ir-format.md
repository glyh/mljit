---
title: Specify the Textual SSA IR Format
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: closed
closed_date: 2026-08-06
assignee: glyh
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Specify the Textual SSA IR Format

## Question

What textual format should MLJIT use to dump, snapshot, and eventually parse or round-trip its block-parameter SSA IR?

The answer should define syntax for functions, block parameters, instructions, terminators, branch arguments, calls, constants, types, and comments. It should include fib/gcd examples and explain which parts must be stable enough for Catch2 snapshot-style tests.

## Resolution

Specified as [docs/ir-format.md](../../ir-format.md), codifying the format
`mljit.ir.printer::to_text` already emits (it had grown a de facto shape under
the existing golden tests) rather than inventing a second syntax. Key points:

- Line-oriented, deterministic, byte-exact dumps: `func @name(types) -> type {`,
  `^block(param: type, …):` labels, two-space-indented `value: type = op` lines,
  and `ret` / `jump` / `branch` terminators with mandatory (possibly empty)
  branch-argument parentheses — block-parameter SSA, no phis.
- Three sigil namespaces: `@function`, `^block`, bare `value`. Debug names print
  verbatim; fallbacks are `f<index>` / `bb<id>` / `v<id>` in creation order.
- Full EBNF grammar in the doc; `//` line comments are reserved for the future
  parser and never emitted. The grammar is unambiguous and information-complete,
  so round-tripping is possible later; today the format is dump-only.
- Stability contract for snapshot tests: everything about line shape, mnemonics,
  ordering, and debug names is stable; auto-generated `v<id>` numbering is not
  stable under builder-sequence edits, so full-dump snapshots accept re-blessing.
- The required fib/gcd reference examples are quoted verbatim in the doc and
  pinned by two new `[ir][spec]` snapshot tests in `test/test_ir.cpp`
  (gcd: loop back-edge with branch args; fib: self-call, empty branch args),
  built from the same IR as the runtime acceptance tests. 109/109 tests pass.

This unblocks [Define the Phase-Dump CLI Contract](define-phase-dump-cli-contract.md).
A textual-IR parser (round-trip) stays in the map Fog until something needs
IR-as-input.
