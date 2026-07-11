---
title: Design the Transient Machine IR Boundary
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
  - design-x64-assembler-layer.md
  - design-linear-scan-regalloc.md
---

# Design the Transient Machine IR Boundary

## Question

What exact function-scoped Machine IR should MLJIT build between canonical block-parameter SSA and x64 emission?

The answer should define the MIR's role as transient backend-private state, not a module-level round-trippable IR. It should specify value/register naming, block representation, instruction set, calling-convention lowering boundary, how liveness/register allocation annotates or mutates MIR, and what textual dumps are required for `--emit-mir` and `--emit-regalloc`.

The answer should preserve the chosen materialization policy: canonical SSA is persistent and round-trippable; MIR exists per function during backend compilation; x64 assembler streams into a code buffer.
