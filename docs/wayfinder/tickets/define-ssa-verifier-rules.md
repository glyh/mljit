---
title: Define the SSA Verifier Rules
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Define the SSA Verifier Rules

## Question

What invariants should the Milestone 1 verifier enforce for the block-parameter SSA IR?

The answer should cover undefined values, duplicate definitions, terminator requirements, block argument counts and types, branch/call signatures, type correctness for i64 arithmetic/comparisons, dominance/use validity, recursion/function references, and diagnostic format. It should also identify negative Catch2 cases that must exist before backend work begins.
