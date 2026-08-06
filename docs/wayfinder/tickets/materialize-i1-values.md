---
title: Materialize i1 Values in the Native Backend
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: open
assignee:
blocked_by: []
---

# Materialize i1 Values in the Native Backend

## Question

Add a setCC/movzx lowering path so `icmp` results can be used as plain values, removing the `guard_no_i1_values` restriction.

Today the emitter fuses every `icmp` into its consuming branch and asserts no icmp result is used otherwise. The moment the frontend produces boolean-valued expressions (a comparison stored, passed, or returned), the JIT rejects the program — with the [CLI contract](../../cli.md) defaulting to the JIT, this is user-visible. Keep the existing icmp/branch fusion for the sole-branch-use case; materialize via `setCC` + `movzx` only when the result has value uses. Verify with interpreter-vs-native differential tests over boolean-valued functions. Graduated from the map's Fog.
