---
title: Define the Phase-Dump CLI Contract
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - specify-textual-ssa-ir-format.md
---

# Define the Phase-Dump CLI Contract

## Question

What stable CLI contract should expose compiler phase dumps and execution modes from day one?

The answer should define commands/options for emitting SSA, verifier diagnostics, interpreter traces, later machine IR/regalloc/x64 listings, selecting interpreter vs JIT execution, and running benchmarks. It should distinguish Milestone 1 commands from future placeholders.
