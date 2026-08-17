---
title: Implement Module-Level Compilation (Cross-Function Calls)
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: closed
assignee: glyh
closed_date: 2026-08-09
blocked_by:
  - design-module-level-jit-compilation.md
---

# Implement Module-Level Compilation (Cross-Function Calls)

## Question

Implement the module-level compilation driver settled in
[Design Module-Level JIT Compilation](design-module-level-jit-compilation.md),
removing the `assert(c.callee == self && "cross-function calls not yet supported")`
in `src/codegen.cppm`.

Scope: replace `codegen::compile(mod, fid) -> x64::ExecBuffer` with a module-level
entry point returning a `CompiledModule` that owns one `x64::ExecBuffer` plus a
`FunctionId -> byte offset` table. One `Assembler` for the module; one `Label` per
function allocated up front; emit in declaration order, `bind()`ing each function's
label at its entry; a single `finalize()` and a single W^X flip. Call sites lower to
`a.call(label_of[callee])` — the existing rel32 fixup path, unchanged — so mutual
recursion resolves through the normal forward-fixup mechanism.

Callers to update: the CLI driver's `run --backend=jit` (see
[Implement the mljit CLI Driver](implement-cli-driver.md)) and the existing
per-function `test_codegen.cpp` tests. The CLI's pre-emission unsupported-construct
scan becomes whole-module rather than per-function, and the cross-function-call arm
of that diagnostic is retired.

Tests: mutual recursion (`is_even`/`is_odd`) as the forward-reference case, a
straight-line multi-function call chain, and interpreter-vs-native differential
coverage for both. Verify each function's callee-saved/frame handling still holds
when several functions share one buffer.

## Resolution (2026-08-09)

Shipped, and the design's central claim held: it needed *no new machinery* in
`mljit.x64`. The whole cross-function mechanism is `a.call(func_label[callee])`
against the same `Label`/`Fixup` list intra-function jumps already used.

- **`codegen::compile(mod, fid) -> ExecBuffer` became
  `codegen::compile(mod) -> CompiledModule`.** `CompiledModule` owns the single
  `ExecBuffer` plus a `FunctionId -> byte offset` table, and exposes
  `entry_offset(fid)` and `invoke<T>(fid, args...)`. The per-function body moved
  to an internal `emit_function(a, fn, self_label, func_label)`; `compile` now
  allocates every function's label up front, emits in declaration order
  recording `a.size()` as each entry offset, then does one `finalize()` and one
  `executable_copy()`.
- **An entry point is an offset, not a base address.** `x64::ExecBuffer` gained
  `invoke_at<T>(offset, args...)` — carrying the existing `no_sanitize`
  suppression — with `invoke<T>` delegating to offset 0. Every function of a
  module is independently callable, which the tests use directly.
- **Mutual recursion works for free**, as predicted: `is_even` is emitted before
  `is_odd` exists in the buffer, and `bind()` patches the forward call site. It
  needed no code beyond naming the callee's label.
- **The CLI's capability scan became whole-module.** `jit_gap(mod)` walks every
  function in declaration order and reports the first gap; the cross-function
  arm is gone and the remaining messages are now function-qualified
  (`... in @weird`, `branch in @f/^bb1 ...`). Whole-module is the honest scope:
  compilation is eager and whole-module, so a gap in a function the entry never
  calls still blocks emission, and it must be reported before any output.
- **Tests**: mutual recursion (forward reference), a three-function call chain
  over already-emitted callees with a value live across nested calls, and a
  mixed-frame module (a spill-heavy caller calling self-recursive `fib`) that
  checks per-function prologue/callee-saved handling survives buffer sharing —
  all differentially checked against the interpreter. At CLI level: a
  cross-function program now runs natively, mutual recursion runs natively, and
  a gap in a non-entry function blocks the module. 139 → 144 tests, green under
  both the ASan/UBSan debug preset and release.

The rel32 co-location constraint is unchanged and remains the subject of
[Support Out-of-Range and Host Call Targets](support-out-of-range-call-targets.md).
