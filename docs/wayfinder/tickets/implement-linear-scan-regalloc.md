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
   + interval dump + tests. — **done** (`464d981`, `cf397c2`)
2. The scan: free-register selection, fixed intervals. — **done** (`2ae7453`)
3. Move resolution + `xchg_rr` in `mljit.x64`. — **done** (`7ab99bd`)
4. Register spilling + consolidated `--emit-regalloc` dump + pressure generator + invariant
   test battery. — pending.

## Implementation notes and deviations from the design

Recorded as implementation progresses so the (closed) design record stays honest.

### Fixed-interval scope narrowed — args/return handled by emission moves

The design ([Q3](design-linear-scan-regalloc.md)) listed *argument registers pinned at call
sites* and *rax at return* among the fixed intervals. The implementation instead models only
the genuine clobbers as fixed intervals — `rax`/`rdx` at `idiv`/`irem`, and all nine
caller-saved registers spanning a `call` — and leaves incoming parameters, the return value,
and call arguments/results to **emission-time moves** (`mov` into/out of the ABI register in
the prologue, at the call, and before `ret`). This is simpler and still ABI-correct; the cost
is that emission always emits those boundary moves rather than the allocator occasionally
placing a value directly in the ABI register. Re-introducing arg/return pinning to elide
redundant boundary moves is a documented later refinement.

### Spilling deferred; split-vs-whole-interval still open

Slices 1–3 implement the no-spill allocator; the scan asserts if register pressure exceeds the
14 allocatable registers (fib/gcd never reach it). Slice 4 will implement spilling, at which
point the design's **spill-by-split** must be weighed against a simpler **whole-interval Belady
spill** (correct, less code, a code-quality step back in the family already parked in the map
Fog). Decision to be made when the slice is picked up.
