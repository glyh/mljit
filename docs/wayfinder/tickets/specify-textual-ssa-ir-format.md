---
title: Specify the Textual SSA IR Format
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Specify the Textual SSA IR Format

## Question

What textual format should MLJIT use to dump, snapshot, and eventually parse or round-trip its block-parameter SSA IR?

The answer should define syntax for functions, block parameters, instructions, terminators, branch arguments, calls, constants, types, and comments. It should include fib/gcd examples and explain which parts must be stable enough for Catch2 snapshot-style tests.
