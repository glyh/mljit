# MLJIT

MLJIT is a hobby compiler/JIT project exploring block-parameter SSA, register allocation, and direct x86-64 code generation in modern C++.

The input language is planned as a tiny ML-flavored language, but the main focus is the compiler backend: a small, inspectable pipeline from IR to native machine code.

## Project status

MLJIT is currently at the project skeleton and design stage.

What exists today:

- C++26 project skeleton using Clang, CMake, Ninja, and modules.
- Strict warning/sanitizer presets.
- A small starter module/executable proving the build setup.
- Local Wayfinder design map and tickets.

The first implementation milestone is the SSA IR package:

- block-parameter SSA data model;
- textual SSA dump format;
- SSA verifier;
- i64 interpreter;
- hand-built Fibonacci and GCD IR tests.

No parser, runtime, or native JIT backend is implemented yet.

## Planned architecture

```text
tiny generated frontend
  → block-parameter SSA IR
  → SSA verifier
  → i64 SSA interpreter
  → machine/lowering IR
  → linear-scan register allocation
  → project-owned x64 assembler
  → executable x86-64 code
```

The first native backend targets x86-64 Linux and starts with an i64-only slice:

- arithmetic and comparisons;
- block jumps and conditional branches;
- block parameters;
- first-order fixed-arity function calls;
- i64 returns.

Closures, heap values, ADTs, tuples, strings, tagging, and GC are intentionally deferred until the register backend works.

## Design goals

- Modern C++26 codebase with modules at subsystem boundaries.
- Small, explicit compiler data structures with strongly typed IDs.
- Value-semantic IR APIs, with controlled mutability for backend passes where it matters.
- Stable textual dumps for compiler phases.
- Interpreter/native equivalence tests before performance work.
- No LLVM, asmjit, or external JIT abstraction in the compiler core.

## Demo target

The first benchmark/demo pair is planned to be:

- recursive Fibonacci, to exercise calls and recursion;
- iterative GCD, to exercise loops, branches, and block parameters.

Both should run through the SSA interpreter first, then later through the native backend for interpreter-vs-JIT comparison.

## Build

Requirements today:

- Clang with C++26/module support;
- CMake;
- Ninja.

Configure and build:

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/src/mljit
```

Other presets:

```bash
cmake --preset release
cmake --build --preset release

cmake --preset release-asan
cmake --build --preset release-asan
```

Dependencies will be managed through vcpkg manifest mode as the project grows. The current skeleton has no external runtime/compiler-core dependencies.

## Design planning

The current design map lives in [`docs/wayfinder/mljit-design-map.md`](docs/wayfinder/mljit-design-map.md).

Open design tickets in `docs/wayfinder/tickets/` track the next decisions, including the SSA IR data model, textual IR format, verifier rules, interpreter semantics, vcpkg/Catch2 integration, and the x64 backend layers.
