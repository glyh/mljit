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

### Spilling — allocator side done (whole-interval Belady); emission pending

The allocator now spills: when no register is free for a whole interval it evicts, by Belady
(furthest next use), a value in a register not pinned by a fixed interval — or spills the
current value if its own next use is furthest. One 8-byte slot per spilled value. Chosen over
the design's **spill-by-split** for v1 simplicity (recorded in the map Fog); split remains the
eventual quality refinement. Validated structurally via a `make_pressure_fn(N)` generator and
reusable invariants (all live values located, no overlapping values share a register, distinct
slots).

**Emission of spilled values — done.** Reloading a spilled operand needs a register the
register-only ALU cannot borrow for free, so the choice was **reserve two scratch registers**
(`r10`/`r11` → 12 allocatable) over spill-by-split. `mljit.codegen`'s `Emitter` resolves each
operand through the allocation: register operands are used directly, spilled operands are
reloaded into a scratch register, and spilled results are computed in a scratch and stored.
This covers every op plus call args and block-parameter moves (reg↔slot, slot↔slot, and xchg).
A frame-based prologue reserves the callee-saved saves plus spill slots with one 16-aligned
`sub rsp`. Verified by a high-pressure differential test (up to 30 live values) matching the
interpreter. **spill-by-split** remains the deferred quality refinement (map Fog): it would
reclaim the two scratch registers and avoid whole-interval memory traffic.
