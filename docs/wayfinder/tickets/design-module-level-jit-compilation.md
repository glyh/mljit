---
title: Design Module-Level JIT Compilation (Cross-Function Calls)
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: glyh
resolution: One Assembler/ExecBuffer per module, eager whole-module compilation in declaration order, and cross-function calls lowered as the shortest encoding — `call rel32` (5 bytes, no register cost) — reusing the existing intra-function Label/fixup machinery unchanged. Accepted constraint: every call target must be co-located in the same buffer, so host/runtime-helper calls and per-function recompilation are out of reach until a second call kind lands.
closed_date: 2026-08-09
blocked_by: []
---

# Design Module-Level JIT Compilation (Cross-Function Calls)

## Question

How should the native backend compile a whole module so functions can call each other?

Today `codegen::compile()` is per-function and asserts `callee == self` (only self-recursion works). With the [CLI contract](../../cli.md) defaulting `run` to the JIT, cross-function calls are the largest capability gap. The answer should settle: the module-level compilation driver (compile order, one code buffer vs per-function buffers), how call sites resolve callee addresses (function address table vs direct-call fixups/patching), whether compilation is eager whole-module or lazy per-function, and how the design leaves room for later runtime helper calls. Graduated from the map's Fog.

## Resolution

**One buffer per module, eager whole-module compilation, and the shortest call
encoding — `call rel32` — with co-location accepted as a hard constraint.**

The decisive observation is that this needs *no new machinery*. `mljit.x64` already
carries an intra-function `Label`/`Fixup` system that patches 4-byte rel32 slots on
`bind()` and asserts every fixup resolved at `finalize()`. Promoting it from
function scope to module scope is a change of *lifetime*, not of mechanism.

### The module compilation driver

`codegen::compile(mod, fid) -> ExecBuffer` is replaced by a module-level entry point
returning a `CompiledModule` that owns the single `ExecBuffer` plus a
`FunctionId -> byte offset` table (for entry-point lookup and for the eventual
`--emit-x64` view):

1. Allocate **one `x64::Assembler`** for the module.
2. Allocate **one `Label` per function** up front, before emitting anything.
3. Emit functions in **declaration order**, `bind()`ing each function's label at its
   own entry. Order is arbitrary for correctness — it exists only to make dumps
   deterministic.
4. `finalize()` once, then flip the whole buffer to `PROT_READ|PROT_EXEC` once.

### Call lowering

A call site emits `a.call(label_of[callee])` — the identical path self-recursion
already takes, with the callee's label instead of the entry label. The existing
fixup list handles forward references, so **mutual recursion works for free**: a call
to a not-yet-emitted function records a fixup, and `bind()` patches it later.
`guard_no_i1_values`-style pre-scanning is unaffected.

Cost per cross-function call: **5 bytes, zero registers, zero indirection** — a
direct near call the branch predictor handles optimally. This is the shortest
correct encoding available on x86-64, which is the point.

### Why eager, not lazy

Eager whole-module compilation is not an independent preference here — it is forced
by the [CLI contract](../../cli.md), which promises that a JIT capability gap is a
clean `error: jit: unsupported ...` diagnostic at exit code 1. That guarantee is only
honest if every function has been scanned before *any* code runs; lazy stub-patching
would let a gap in a cold function surface mid-execution, after `main` has begun and
possibly after output has been written.

### The accepted constraint

`call rel32` reaches ±2GB from the call site. This is sound **only** because every
target lives in the same `ExecBuffer` — that is what the one-buffer-per-module choice
buys, and the two decisions must not be separated. A single module's code will not
approach 2GB, so the limit is theoretical for MLJIT-generated code.

What the constraint genuinely costs, deferred deliberately:

- **No calls to host/runtime helpers.** A C++ function (a division-by-zero trap, an
  allocator, a GC barrier) lives in the host binary at an arbitrary distance, with no
  guarantee of rel32 reach. The first helper will require a second call kind
  (`mov r11, imm64; call r11`, or an indirect table). None exists today — v1 is
  i64-only with no allocation — so nothing is specified for it now.
- **No per-function recompilation, lazy compilation, or tiering.** The module is one
  immutable buffer; there is no per-function `ExecBuffer` to free or replace, and no
  patchable indirection to swap.
- **`ExecBuffer` ownership moves** from per-function RAII to the module-level
  `CompiledModule`.

Both limitations are the subject of
[Support Out-of-Range and Host Call Targets](support-out-of-range-call-targets.md);
neither blocks v1. The driver itself is
[Implement Module-Level Compilation](implement-module-level-compilation.md).
