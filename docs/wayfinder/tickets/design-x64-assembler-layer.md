---
title: Design the Project-Owned x64 Assembler Layer
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Design the Project-Owned x64 Assembler Layer

## Question

What should MLJIT's internal x64 assembler layer expose before full native codegen exists?

The answer should define the API for byte emission, register operands, immediates, labels, patching, relative calls/jumps, executable-buffer handoff, minimal instruction set for i64 arithmetic/branches/calls, and unit tests for instruction bytes. It must avoid external assembler/JIT libraries while keeping codegen readable.
