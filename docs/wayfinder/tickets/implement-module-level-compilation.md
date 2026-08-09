---
title: Implement Module-Level Compilation (Cross-Function Calls)
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: open
assignee:
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
