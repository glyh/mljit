---
title: Implement Linear Scan Register Allocation
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: open
assignee: glyh
blocked_by:
  - design-linear-scan-regalloc.md
---

# Implement Linear Scan Register Allocation

## Question

Implement the v1 register allocator settled in
[Design Linear Scan Register Allocation](design-linear-scan-regalloc.md).

Deliverables:

- A `mljit.regalloc` module producing the pure `(ir::Function, Liveness) -> Allocation`
  side-table (SSA untouched).
- Wimmer-Franz linear scan on SSA with lifetime holes: RPO linear order + op numbering,
  backward interval construction with the loop-header extension, the unhandled/active/
  inactive/handled scan, free-register selection, and spill-by-split.
- System V register model: reserve rsp+rbp, 14 allocatable registers, forced uses
  (idiv rax/rdx, call clobbers, arg/return regs) as fixed intervals.
- One 8-byte spill slot per value; 16-aligned frame.
- Move resolution: topological parallel-move ordering, `xchg` cycle-breaking (add
  `xchg_rr` to `mljit.x64`), on-demand critical-edge splitting (backend emission plan only).
- Deterministic 4-section `--emit-regalloc` dump.
- Tests: structural invariants + golden dumps now; interpreter-oracle differential tests
  with x64 emission later; pressure driven by a `make_pressure_fn(N)` generator.

## Implementation slices

1. Core types + linear order/numbering + interval construction (holes, loop extension)
   + interval dump + tests.
2. The scan: free-register selection, fixed intervals, spill-by-split.
3. Move resolution + `xchg_rr` in `mljit.x64`.
4. Full `--emit-regalloc` dump + pressure generator + invariant test battery.
