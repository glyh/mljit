---
title: Design Structured Trace Events
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - define-ssa-verifier-rules.md
  - design-i64-ssa-interpreter.md
  - define-phase-dump-cli-contract.md
---

# Design Structured Trace Events

## Question

What structured compiler/JIT phase events should MLJIT expose for observability and integration tests?

The answer should define event kinds, field representation, in-memory test recording, JSONL output, CLI flags, and where trace hooks belong in verifier/interpreter/backend phases. Implementation conventions live in [Coding Standard](../../coding-standard.md); dependency/library decisions live in [Dependency Policy](../../dependency-policy.md).

## Research note

External tracing libraries were checked. The current recommendation is to implement a tiny local `mljit.trace` module first and adopt a standard output format rather than depend on a profiler/logging stack now. See [Dependency Policy](../../dependency-policy.md) for library-decision details.

- Prefer local in-memory `Event` records for tests plus JSONL/Chrome Trace Event export for inspection.
- Borrow the useful idea from LLVM optimization remarks: stable pass/event names with structured key-value arguments.
