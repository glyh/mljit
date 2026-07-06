---
title: Design Linear Scan Register Allocation
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
  - design-x64-assembler-layer.md
---

# Design Linear Scan Register Allocation

## Question

How should MLJIT v1 perform linear scan register allocation with spill support?

The answer should define inputs and outputs, live interval construction, register classes for i64, caller/callee-saved policy, fixed registers for calls, spill-slot assignment, reload/move insertion strategy, debug/allocation trace format, and tests for register pressure.
