---
title: Design the i64 SSA Interpreter
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
  - define-ssa-verifier-rules.md
---

# Design the i64 SSA Interpreter

## Question

How should the Milestone 1 interpreter execute verified i64 block-parameter SSA IR?

The answer should define the interpreter state model, block transition semantics, function call/recursion handling, argument passing, error handling, trace output, and Catch2 tests for arithmetic, conditionals, loops, recursive Fibonacci, and iterative GCD. It should become the correctness baseline for the later native backend.
