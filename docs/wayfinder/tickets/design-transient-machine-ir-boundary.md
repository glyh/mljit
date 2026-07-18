---
title: Design the Transient Machine IR Boundary
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: glyh
resolution: No materialized MIR for v1 — the numbering + Allocation + Resolution side-tables ARE the transient per-function backend state; the emitter is a direct fold over SSA into the x64 code buffer. Lowering rules: prologue/epilogue with rbp frame + callee-saved save, ABI-move params in, 3-address->2-address ALU, idiv/irem via rax:rdx, icmp/branch fusion, System V calls, rax return.
closed_date: 2026-07-18
blocked_by:
  - design-block-parameter-ssa-ir.md
  - design-x64-assembler-layer.md
  - design-linear-scan-regalloc.md
---

# Design the Transient Machine IR Boundary

## Question

What exact function-scoped Machine IR should MLJIT build between canonical block-parameter SSA and x64 emission?

The answer should define the MIR's role as transient backend-private state, not a module-level round-trippable IR. It should specify value/register naming, block representation, instruction set, calling-convention lowering boundary, how liveness/register allocation annotates or mutates MIR, and what textual dumps are required for `--emit-mir` and `--emit-regalloc`.

The answer should preserve the chosen materialization policy: canonical SSA is persistent and round-trippable; MIR exists per function during backend compilation; x64 assembler streams into a code buffer.

## Resolution

**No materialized Machine IR for v1.** MLJIT emits x64 directly from canonical SSA,
consuming the backend side-tables the register allocator already produces. This is the
natural consequence of the regalloc design (Q1): the allocator is a pure function producing
a side-table with SSA left untouched, so a separate machine-instruction IR would only be a
second place to hang those same tables.

### The MIR *is* the side-tables

The map's "transient per-function backend state" is honored by three function-scoped,
backend-private structures over the untouched SSA, none module-level or round-trippable:

- **`Numbering`** — linear (reverse-postorder) block layout + program-point numbering.
- **`Allocation`** — `ValueId -> Location` (register or spill slot).
- **`Resolution`** — per-edge moves (block-parameter passing, split reconciliation).

- *Value/register naming*: SSA `ValueId`s; physical registers via `Allocation`.
- *Block representation*: the SSA blocks in `Numbering` order.
- *Instruction set*: none introduced — the emitter matches on SSA `InstPayload`/`TermPayload`.
- *How regalloc annotates it*: it doesn't mutate anything; it produces `Allocation`/`Resolution`.
- *Dumps*: `--emit-regalloc` (numbering + intervals + allocation + resolution) is the
  machine-level view. No separate `--emit-mir` for v1; a real MIR instruction dump can be
  introduced later if a machine-level optimization pass or a second target ever needs one.

### Emitter lowering rules (the fold over SSA)

A `mljit.codegen` (or `mljit.emit`) pass walks blocks in `Numbering` order and streams into a
`mljit.x64::Assembler`:

- **Prologue**: `push rbp; mov rbp, rsp`; `sub rsp, frame` when there are spill slots
  (16-aligned); push each callee-saved allocatable register actually used.
- **Params in**: `mov <param location>, <ABI arg register>` (rdi, rsi, rdx, rcx, r8, r9).
- **Per instruction** (operands/result resolved through `Allocation`):
  - `const_i64` -> `mov_ri`.
  - `iadd`/`isub`/`imul` -> 3-address to 2-address: if `dst == lhs`, op in place; else
    `mov dst, lhs; op dst, rhs` (with the commutative shortcut for add/mul when `dst == rhs`).
  - `idiv`/`irem` -> `mov rax, lhs; cqo; idiv rhs; mov dst, rax` (idiv) or `rdx` (irem);
    fixed intervals guarantee `rhs`/live values avoid rax/rdx.
  - `icmp` -> only materialized (`cmp; setcc; movzx`) when its result is used as a value; when
    it feeds only a `branch`, it emits nothing and is fused into the branch (below).
  - `call` -> move args into ABI arg registers, ensure 16-byte stack alignment, `call` the
    callee label, `mov dst, rax`. Values live across the call are kept out of caller-saved
    registers by the call-clobber fixed intervals.
- **Terminators**:
  - `ret v` -> `mov rax, v`; epilogue (pop callee-saved, `mov rsp, rbp; pop rbp`); `ret`.
  - `jump t(args)` -> the edge's `Resolution` moves, then `jmp t` (elided on fall-through).
  - `branch c, t(a), f(b)` -> **icmp/branch fusion**: emit `cmp lhs, rhs` from `c`'s defining
    icmp and a `jCC` for the taken edge, with each edge's `Resolution` moves on its own side.
    (The general "i1 used as a value *and* branched on" case is deferred — fib/gcd only need
    the fused form; it may later want a `test`/`cmp`-immediate assembler addition.)

### Calling-convention lowering boundary

System V AMD64 is applied entirely at emission: params moved out of ABI registers in the
prologue, args moved into ABI registers before a `call`, return value moved into rax. The
allocator stays ABI-agnostic apart from the idiv/call-clobber fixed intervals — see the
[regalloc implementation deviations](implement-linear-scan-regalloc.md).
