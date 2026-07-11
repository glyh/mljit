---
title: Design the i64 SSA Interpreter
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: closed
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
  - define-ssa-verifier-rules.md
---

# Design the i64 SSA Interpreter

## Question

How should the Milestone 1 interpreter execute verified i64 block-parameter SSA IR?

The answer should define the interpreter state model, block transition semantics, function call/recursion handling, argument passing, error handling, trace output, and Catch2 tests for arithmetic, conditionals, loops, recursive Fibonacci, and iterative GCD. It should become the correctness baseline for the later native backend.

## Resolution

The Milestone 1 interpreter should be implemented as the IR runtime component:

```text
file:      src/ir_runtime.cppm
module:    mljit.ir.runtime
namespace: mljit::ir::runtime
```

The public API is a reusable `runtime::Interpreter` object bound to a read-only module view:

```cpp
namespace mljit::ir::runtime {

using RuntimeWord = std::int64_t;

struct RunOptions {
  std::optional<std::uint64_t> max_steps = 1'000'000;
  std::optional<std::uint32_t> max_call_depth = 4096;
};

struct RunStats {
  std::uint64_t steps_executed = 0;
  std::uint32_t max_call_depth_observed = 0;
};

enum class RuntimeErrorKind {
  InvalidEntryFunction,
  ArgumentCountMismatch,
  ArithmeticOverflow,
  DivisionByZero,
  DivisionOverflow,
  StepLimitExceeded,
  CallDepthExceeded,
};

struct RuntimeError {
  RuntimeErrorKind kind;
  FunctionId function;
  std::optional<BlockId> block;
  std::optional<InstructionId> instruction;
  std::string detail;
};

struct RunSuccess {
  RuntimeWord value = 0;
  RunStats stats;
};

struct RunFailure {
  RuntimeError error;
  RunStats stats;
};

using RunResult = std::expected<RunSuccess, RunFailure>;

class Interpreter {
public:
  explicit Interpreter(const Module& module);

  [[nodiscard]] auto run(
      FunctionId entry,
      std::span<const RuntimeWord> args,
      RunOptions options = {}) const -> RunResult;

private:
  const Module& module_;
  std::vector<const Function*> func_table_;
};

}
```

Preconditions are documented, not enforced by the runtime component:

- The `Module` passed to `Interpreter` must already satisfy verifier rules.
- The module must outlive the `Interpreter`.
- The module must not be mutated while the `Interpreter` exists.

The runtime module should not import the verifier. Tests should explicitly model the intended pipeline:

```text
build IR -> verifier::verify(module).ok() -> runtime::Interpreter::run(...)
```

## Execution model

- Use an explicit continuation stack, not the C++ call stack.
- `Interpreter` owns stable module/runtime state: `const Module&` plus a dense function table indexed by `FunctionId::value`.
- Each `run()` owns fresh per-invocation execution state: call stack, step counter, and current stats.
- `run()` is reusable and `const`; errors do not poison the `Interpreter` object.
- Each stack frame owns a dense `std::vector<RuntimeWord>` environment indexed by function-local `ValueId::value`.
- Runtime representation is one machine word in v1: `i64` values use the full `int64_t`; internal `i1` values use `0` or `1`.
- Public entry functions accept only `i64` arguments and must return `i64`; tighten broader type exposure later.
- Function calls use `FunctionId` and dispatch through the dense function table.
- Block parameters are bound by eagerly writing incoming argument words into destination block-parameter env slots on block entry.
- Control flow follows terminators in an explicit loop; reverse-postorder single-pass execution is invalid for loops.

Call/return should model explicit continuation state rather than x86 ABI details:

- A call produces a caller-side SSA `ValueId`.
- The callee frame carries a `return_target` describing which caller env slot receives the returned word.
- On `ret`, the callee frame is popped and the return word is written to the caller frame's `return_target` slot.
- No tail-call optimization in v1, but preserving `return_target` makes later TCO straightforward by replacing the current frame instead of pushing.

## Operations and errors

Add signed division/remainder as a full vertical slice before implementing the GCD acceptance test:

```text
IR payloads/builders: idiv, irem
printer: textual dump support
verifier: operand type checks
runtime: execution semantics
tests: dump, verifier, runtime coverage
```

`idiv`/`irem` use truncating x86/C semantics. When division is defined, `(x / y) * y + (x % y) == x`. Division by zero produces `RuntimeErrorKind::DivisionByZero`. `INT64_MIN / -1` and `INT64_MIN % -1` produce `RuntimeErrorKind::DivisionOverflow`.

Arithmetic overflow for `iadd`, `isub`, and `imul` is a runtime error (`ArithmeticOverflow`), not wrapping behavior.

`RunOptions` limits:

- `max_steps = std::nullopt` means unlimited; `max_steps = 0` means no steps are allowed.
- `max_call_depth = std::nullopt` means unlimited; `max_call_depth = 0` means even the entry frame is rejected.
- Steps count both ordinary instructions and terminator dispatches.
- A `call` instruction counts as one step; callee body execution then consumes its own steps.
- On `StepLimitExceeded`, `RunStats::steps_executed` reports completed steps only, so it equals the configured max.

No execution tracing in v1. Structured tracing should be designed separately after interpreter correctness is stable.

## Naming cleanup prerequisite

Before adding `mljit.ir.runtime`, clean up existing IR component namespaces:

```text
src/ir_dump.cppm      -> src/ir_printer.cppm
module mljit.ir.dump  -> module mljit.ir.printer
namespace ir::dump    -> namespace ir::printer

src/ir_verify.cppm       -> src/ir_verifier.cppm
module mljit.ir.verify   -> module mljit.ir.verifier
namespace ir::verify     -> namespace ir::verifier
```

The intended component vocabulary becomes:

```text
mljit::ir              // data model
mljit::ir::printer     // textual rendering
mljit::ir::verifier    // well-formedness checker
mljit::ir::runtime     // interpreter runtime
```

## Acceptance tests

The first runtime suite should include:

- `add1(41) = 42` as straight-line arithmetic smoke.
- `abs(-7) = 7` as branch/block-param smoke.
- cross-function call smoke.
- recursive `fib(10) = 55` to exercise calls, recursion, continuation stack, and return targets.
- Euclidean `gcd(48, 18) = 6` using `irem` to exercise loops and block parameters.
- overflow returns `RuntimeErrorKind::ArithmeticOverflow`.
- division by zero returns `RuntimeErrorKind::DivisionByZero`.
- signed division overflow returns `RuntimeErrorKind::DivisionOverflow`.
- infinite/long-running loop returns `RuntimeErrorKind::StepLimitExceeded`.
- runaway recursion returns `RuntimeErrorKind::CallDepthExceeded`.

## Status

Closed for implementation. Proceed in two phases: mechanical namespace cleanup first, then runtime/idiv/irem vertical slice.
