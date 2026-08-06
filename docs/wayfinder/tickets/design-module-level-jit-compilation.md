---
title: Design Module-Level JIT Compilation (Cross-Function Calls)
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by: []
---

# Design Module-Level JIT Compilation (Cross-Function Calls)

## Question

How should the native backend compile a whole module so functions can call each other?

Today `codegen::compile()` is per-function and asserts `callee == self` (only self-recursion works). With the [CLI contract](../../cli.md) defaulting `run` to the JIT, cross-function calls are the largest capability gap. The answer should settle: the module-level compilation driver (compile order, one code buffer vs per-function buffers), how call sites resolve callee addresses (function address table vs direct-call fixups/patching), whether compilation is eager whole-module or lazy per-function, and how the design leaves room for later runtime helper calls. Graduated from the map's Fog.
