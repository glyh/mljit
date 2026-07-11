---
title: Design the Block-Parameter SSA IR Data Model
parent: ../mljit-design-map.md
labels:
  - wayfinder:grilling
status: closed
assignee: workflow-manager
blocked_by: []
---

# Design the Block-Parameter SSA IR Data Model

## Question

What should the Milestone 1 SSA IR data model look like in C++26?

Resolve the shape of modules, functions, blocks, block parameters, instructions, terminators, values, types, constants, IDs, ownership, builders, and mutation boundaries for an i64-first block-parameter SSA IR. The answer should preserve value-semantic public APIs with strongly typed IDs and builder-owned storage, while leaving room for controlled mutable backend artifacts later.

## Resolution

Milestone 1 starts with the SSA IR package boundary, not parser/frontend, verifier semantics, interpreter semantics, Machine IR, x64, regalloc, tracing, or CLI design.

### Ownership and identity

- A minimal `Module` owns all `Function`s from day one.
- `FunctionId` is module-local.
- `BlockId`, `ValueId`, and `InstructionId` are function-local.
- Function parameters, block parameters, and instruction results share one unified function-local `ValueId` namespace.
- Function parameters are represented as entry-block parameters, not as a separate value-origin category.
- Calls store semantic callee references as `FunctionId`; dumps render function names when available.
- Optional debug names exist for readable deterministic dumps, but IDs remain the only semantic identity.

### Types and signatures

- v1 types are `i64` and `i1`.
- Arithmetic is `i64`-only.
- Comparisons produce `i1`.
- Branch conditions consume `i1`.
- v1 functions are fixed-arity and single-return only.
- No void functions and no multi-return functions yet.

### Blocks, instructions, and terminators

- Block body instructions and terminators are separate IR concepts.
- Ordinary instructions live in a function-local instruction arena.
- Blocks hold ordered `InstructionId` lists plus one terminator.
- Constants are normal SSA-producing instructions, not immediate operands inside arithmetic or branch payloads.
- Instruction operands, terminator arguments, and call arguments are uniformly `ValueId`.
- Instruction and terminator payloads use `std::variant` of small typed structs, not inheritance and not enum-plus-all-fields.

v1 ordinary instructions:

```text
const_i64
iadd
isub
imul
icmp
call
```

v1 terminators:

```text
ret
jump
branch
```

v1 comparison predicates:

```text
eq
ne
slt
sle
sgt
sge
```

Division and modulo are deferred to avoid runtime-error policy in the first slice.

### Construction and validation boundary

- Construction uses `ModuleBuilder` for top-level function creation and scoped `FunctionBuilder` for function-local SSA construction.
- Canonical SSA is builder-controlled and append-oriented.
- Public IR access is mostly read-only.
- Builders enforce local mechanical invariants.
- The verifier owns global SSA correctness.
- If malformed IR is needed for verifier tests, add a future test-only escape hatch instead of making the normal builder permissive.

Local builder checks include ID ownership, append-before-terminator, obvious type sanity, branch condition type, return type, and call arity/type checks.

Verifier checks include complete terminators, block-argument arity/types, dominance/value availability, entry-block rules, CFG consistency, existing references, and unreachable-block policy.

### Source locations and dumps

- IR nodes should have optional opaque source-span/debug-location slots from day one.
- The full source manager is deferred until frontend work.
- The first IR slice includes a stable human-readable textual dump.
- The dump is not required to be parser-roundtrippable yet.
- Dumps are deterministic, print debug names when present, fall back to stable IDs, and include types on definitions and block parameters.

Initial modules:

```text
mljit.ir       // data model + builders
mljit.ir.printer  // textual dump
```

Future modules:

```text
mljit.ir.verifier
mljit.ir.interp
```

### First implementation acceptance

The first implementation slice is accepted when Catch2 tests build and compare stable dumps for:

1. a straight-line `add1` function;
2. a block-parameter control-flow function such as `abs`.

No fib/gcd, verifier, interpreter, parser, or backend is required in this first implementation slice.
