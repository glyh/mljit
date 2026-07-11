---
title: Define the SSA Verifier Rules
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: workflow-manager
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Define the SSA Verifier Rules

## Question

What invariants should the Milestone 1 verifier enforce for the block-parameter SSA IR?

The answer should cover undefined values, duplicate definitions, terminator requirements, block argument counts and types, branch/call signatures, type correctness for i64 arithmetic/comparisons, dominance/use validity, recursion/function references, and diagnostic format. It should also identify negative Catch2 cases that must exist before backend work begins.

## Resolution

Milestone 1 gets a real SSA verifier, not only structural validation. The verifier lives in its own module, keeps dominance private, emits deterministic structured diagnostics, and rejects unreachable blocks as malformed canonical IR.

### Module/API boundary

- Verifier implementation lives in `mljit.ir.verify` (`src/ir_verify.cppm`) in the existing `mljit-lib` target.
- Public entry point:

```cpp
auto verify(const Module& module) -> VerifyResult;
```

- `VerifyResult` wraps errors instead of returning a raw vector or `std::expected`:

```cpp
struct VerifyResult {
  std::vector<VerifyError> errors;

  [[nodiscard]] auto ok() const -> bool;
};
```

- The verifier accumulates all errors in one run; it does not bail on first error.
- Dominance analysis is private inside `mljit.ir.verify` for v1. Promote it later into an analysis module only if optimizer/backend work needs it.

### Diagnostic model

Errors are strongly typed and inspectable. Human text is secondary context, not the semantic identity of the error.

```cpp
enum class VerifyErrorKind {
  MissingTerminator,

  InvalidBlockTarget,
  InvalidFunctionTarget,
  InvalidValueReference,

  BlockArgumentCountMismatch,
  BlockArgumentTypeMismatch,

  ReturnTypeMismatch,
  CallArgumentCountMismatch,
  CallArgumentTypeMismatch,

  BranchConditionTypeMismatch,
  InstructionOperandTypeMismatch,

  EntryBlockPredecessor,
  EntryBlockParameterCountMismatch,
  EntryBlockParameterTypeMismatch,

  UnreachableBlock,
  UseDoesNotDominate,
};
```

`VerifyError` should include the `VerifyErrorKind` plus optional contextual IDs such as function, block, instruction, value, target block, and human `detail` text.

Diagnostics must be deterministic: emit by module function order, verifier pass order, block order, and instruction order. Do not let unordered container iteration affect diagnostic order.

### Verification passes

Use layered verification rather than one giant flat walk:

1. Structural and type checks.
2. CFG reachability and predecessor collection.
3. Dominance computation.
4. SSA value-use dominance checks.

The verifier should check at least:

- every block has a terminator;
- terminator block targets exist;
- call function targets exist;
- referenced `ValueId`s exist in the current function;
- jump/branch argument count matches target block parameter count;
- jump/branch argument types match target block parameter types;
- `ret` value type matches function return type;
- call argument count and argument types match callee entry-block parameters/function type;
- branch condition is `i1`;
- instruction operand types match instruction requirements;
- entry block is always `BlockId{0}`;
- entry block has no predecessors;
- entry block parameter count/types match the function type;
- every block is reachable from entry;
- every value use is dominated by its definition.

Unreachable blocks are verifier errors (`UnreachableBlock`). This makes canonical IR stronger and keeps interpreter/backend simpler. Dead code elimination can remove unreachable blocks before verification later if needed.

### Dominance algorithm

Use Cooper-Harvey-Kennedy iterative dominators for v1:

```text
idom[entry] = entry
repeat until stable:
  for each reachable block b except entry:
    new_idom = intersection of already-processed predecessor idoms
    if idom[b] changed: update
```

The algorithm computes block dominance. The verifier combines block dominance with instruction order inside a block:

- a block parameter is defined at the start of its block;
- an instruction result is defined at that instruction position;
- a value used in another block must have its definition block dominate the use block;
- a value used in the same block must be defined earlier than the use site;
- terminator arguments are uses in the predecessor block, not in the target block.

When delegating implementation, explain this algorithm explicitly; do not only say "compute dominators".

### Malformed IR test support

Keep normal builders safe. Negative verifier tests should use an isolated test-only unsafe mutation helper/module:

```text
mljit.ir.test_support
```

This module may expose narrowly scoped helpers to remove terminators, corrupt targets, alter argument lists/types, or otherwise construct invalid IR. It should be compiled for tests only and must not become a production construction API.

### First verifier test suite

Cover valid IR plus one negative test per major invariant, not an exhaustive matrix yet.

Positive tests:

- valid straight-line `add1` verifies;
- valid block-parameter `abs` verifies;
- valid cross-function call verifies.

Negative tests:

- missing terminator;
- invalid block target;
- block argument count mismatch;
- block argument type mismatch;
- return type mismatch;
- call argument count mismatch;
- call argument type mismatch;
- branch condition not `i1`;
- unreachable block;
- use does not dominate.

After review, add enum-specific tests for any uncovered `VerifyErrorKind` if needed.
