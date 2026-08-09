---
title: Support Out-of-Range and Host Call Targets
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: open
assignee:
blocked_by:
  - implement-module-level-compilation.md
---

# Support Out-of-Range and Host Call Targets

## Question

How should MLJIT call a target that is *not* co-located in the module's code buffer?

[Design Module-Level JIT Compilation](design-module-level-jit-compilation.md)
deliberately took the shortest call encoding — `call rel32`, 5 bytes, no register
cost — and paid for it with a hard constraint: **every call target must live in the
same `ExecBuffer`**. This ticket is the recorded todo for lifting that constraint,
and should not be worked until something actually needs it.

Two things will need it, in this likely order:

1. **Host/runtime helper calls.** A C++ helper (division-by-zero trap, allocator, GC
   barrier) sits in the host binary at an unbounded distance from the JIT buffer, so
   rel32 is unsound for it. v1 is i64-only with no allocation, so no helper exists
   yet; the first one forces this.
2. **Per-function recompilation, lazy compilation, or tiering.** The module is
   currently one immutable buffer with no patchable indirection, so a function cannot
   be replaced after emission.

The answer should settle: whether to introduce a second call kind
(`mov r11, imm64; call r11`) alongside the fast `MlCall`, or a function address table
that both kinds route through; whether the two mechanisms should unify or stay
deliberately separate; whether `r11` remains available given the register allocator
reserves `r10`/`r11` as emission scratch; and what this implies for `ExecBuffer`
ownership if per-function buffers ever return.
