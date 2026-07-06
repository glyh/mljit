---
title: Design the Block-Parameter SSA IR Data Model
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by: []
---

# Design the Block-Parameter SSA IR Data Model

## Question

What should the Milestone 1 SSA IR data model look like in C++26?

Resolve the shape of modules, functions, blocks, block parameters, instructions, terminators, values, types, constants, IDs, ownership, builders, and mutation boundaries for an i64-first block-parameter SSA IR. The answer should preserve value-semantic public APIs with strongly typed IDs and builder-owned storage, while leaving room for controlled mutable backend artifacts later.
