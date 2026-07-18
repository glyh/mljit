---
title: Design Linear Scan Register Allocation
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: glyh
resolution: Wimmer-Franz linear scan on SSA (with holes); pure (Function, Liveness) -> Allocation side-table, SSA untouched; System V ABI, reserve rsp+rbp, 14 allocatable regs, fixed-interval constraint modeling; one 8-byte spill slot per value; xchg-based cycle breaking + on-demand critical-edge splitting; deterministic 4-section --emit-regalloc dump; structural-invariant + interpreter-oracle differential tests via a pressure generator.
closed_date: 2026-07-18
blocked_by:
  - design-block-parameter-ssa-ir.md
  - design-x64-assembler-layer.md
---

# Design Linear Scan Register Allocation

## Question

How should MLJIT v1 perform linear scan register allocation with spill support?

The answer should define inputs and outputs, live interval construction, register classes for i64, caller/callee-saved policy, fixed registers for calls, spill-slot assignment, reload/move insertion strategy, debug/allocation trace format, and tests for register pressure.

## Resolution

MLJIT v1 uses **Wimmer & Franz linear scan register allocation on SSA form** (CGO 2010),
with lifetime holes. The algorithm is an especially good fit because it natively handles
phi-functions, which are exactly MLJIT's block parameters.

### Boundary — inputs and outputs (Q1)

The allocator is a **pure function** `(ir::Function, Liveness) -> Allocation`. It does
**not** mutate SSA (the map's standing "SSA is persistent and round-trippable" rule). The
`Allocation` result is a side-table: a `ValueId -> Location` map (register or spill slot),
spill-slot assignments, and an ordered list of resolution moves keyed to program points /
edges. x64 emission later walks the SSA and consults this table. This keeps the allocator
testable in isolation before Machine IR exists, and the `Allocation` struct becomes the
concrete input the Transient Machine IR Boundary ticket designs around.

### Algorithm — Wimmer-Franz with holes (Q2)

Chosen over the simpler conservative single-interval (Poletto-Sarkar) approach despite the
higher implementation complexity. Rationale captured during grilling: the asymptotic cost
difference is irrelevant at MLJIT's tiny per-function value counts, and the worst-case
superlinear behavior only fires under high register pressure (many simultaneously-live
values, deep loops, calls-in-loops — i.e. machine-generated code) that MLJIT will not
generate in v1. The **holes = exact liveness** (dead regions carved out); the conservative
interval is the over-approximation. The **inactive** interval state is what lets a value's
holes be lent to other values while tracking when the original owner reclaims the register.

Phases: (0) reverse-postorder linear block order + operation numbering in steps of 2;
(1) backward interval construction producing per-value range-lists with holes + use
positions, with the loop-header extension rule; (2) the scan over unhandled/active/
inactive/handled sets; (3a) free-register selection via `freeUntilPos`; (3b) spill-by-split
via furthest-`nextUsePos` (Belady); (4) `resolveDataFlow` move insertion.

### Register file model (Q3)

- **System V AMD64 ABI** (forced by interop — MLJIT is called from the C++ harness and v1
  intra-module calls follow it; external/libc calls later work via `call_r` + a loaded
  absolute address, no rework).
- **Reserve `rsp` and `rbp`.** `rbp` is the frame pointer (stable `[rbp - N]` spill
  addressing, clean stack frames, frame-pointer chain aids tracing/debugging). Allocatable
  pool is the remaining **14** registers: rax, rcx, rdx, rbx, rsi, rdi, r8-r15.
- **Forced uses modeled as Wimmer-Franz fixed intervals** — rax/rdx at every `idiv`/`irem`,
  all nine caller-saved registers spanning every `call`, argument registers pinned at call
  sites, rax at return. The scan needs no special cases; constraints are just pre-occupied
  intervals it scans around.
- Any callee-saved register actually used is push/pop'd in the prologue/epilogue.

### Spill-slot assignment and frame layout (Q4)

**One 8-byte slot per spilled SSA value** (all split parts of a value reload from the same
slot), no stack-slot reuse in v1. Every value is i64/i1 -> uniform 8-byte slots. Layout
top-down from `rbp`: saved rbp, pushed callee-saved regs, then spill slots at
`[rbp - 8], [rbp - 16], ...`. Frame size = `8 * #slots` rounded up to a multiple of 16 so
`sub rsp, N` preserves the 16-byte alignment System V requires at every `call`.

### Move resolution — reload/move insertion (Q5)

`resolveDataFlow` is the fiddliest part (most bug-prone; gets the most test attention).
- Block-parameter argument passing and split reconciliation are **parallel assignments**,
  sequentialized by **topological ordering** of the move dependency graph.
- **Register cycles broken with `xchg`** (reg-reg xchg does not assert LOCK, ~3 cycles, no
  temp and no permanently-reserved scratch register); longer register cycles rotate via a
  sequence of `xchg`s; a cycle that involves a memory (spilled) location falls back to an
  8-byte memory temp. **Follow-on dependency:** `mljit.x64` gains an `xchg_rr(Gpr, Gpr)`
  method (`REX.W + 0x87 /r`) — a small extension to the closed x64 assembler ticket.
- **Critical edges split on demand** — when resolution must place moves on an edge whose
  source has >1 successor and destination has >1 predecessor, a synthetic resolution block
  is materialized. Synthetic blocks live in the backend emission plan / `Allocation` only,
  never in the canonical SSA.

### Debug / allocation trace format (Q6)

A deterministic, greppable `--emit-regalloc` text dump with four lossless sections:
(1) live intervals (ranges with holes + use positions over the numbered instruction line),
(2) final assignments (`ValueId -> Location`), (3) spills/splits with split points,
(4) resolution moves per edge (including synthetic split-blocks). Reuses the existing
`ir_printer` value/block naming so it lines up with `--emit-ir`. Exact event/field schema
is deferred to the Design Structured Trace Events ticket; this ticket commits only to
emitting these four sections deterministically.

### Tests for register pressure (Q7)

Two layers, both enabled by the pure-function boundary:
- **Structural (runnable before x64 emission):** golden `Allocation` dumps plus reusable
  **invariant assertions** run on every test function's result — (1) no two overlapping
  live intervals share a register, (2) every value has a location, (3) fixed intervals
  honored (nothing in rax/rdx across `idiv`, nothing caller-saved across `call`),
  (4) distinct spilled values get distinct slots and frame size is 16-aligned.
- **Differential (lands with x64 emission):** compile -> execute -> compare against the
  existing `mljit.ir.runtime` interpreter as oracle.

Register pressure is driven by a **generator** `make_pressure_fn(N)` (compute N independent
values then sum them, so all N are live to the end) sweeping **N in {10, 14, 15, 20, 30}**
to bracket the 14-register threshold. Plus targeted cases: `idiv`/`irem` (rax/rdx fixed
intervals), a call with values live across it (caller-saved clobber + arg pinning), a loop
whose block-params form a swap (`xchg` cycle-breaking), and a critical-edge case (on-demand
splitting). Bias toward broad coverage.

### Deferred to Fog (revisit under higher register pressure / generated code)

Recorded in the design map's Fog: (1) stack-slot reuse/coalescing, (2) freeing `rbp` for a
15th allocatable register via `rsp`-relative addressing, (3) a caller/callee-saved
allocation preference for values live across calls. All settled-to-revisit, not open.
