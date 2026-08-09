# MLJIT

MLJIT is a hobby compiler/JIT exploring block-parameter SSA, register allocation, and direct x86-64 code generation in modern C++.

The input language is a tiny ML-flavored language, but the focus is the compiler backend: a small, inspectable pipeline from source to native machine code, with a stable textual dump at every phase. No LLVM, no asmjit — the SSA IR, verifier, interpreter, register allocator, and x64 assembler are all project-owned.

## Project status

The v1 pipeline runs end to end. A source file compiles to native x86-64 machine code in memory and executes, and every phase is dumpable:

```text
tiny ANTLR frontend
  → block-parameter SSA IR          (mljit.ir, mljit.ir.printer)
  → SSA verifier                    (mljit.ir.verifier)
  → i64 SSA interpreter             (mljit.ir.runtime)
  → linear-scan register allocation (mljit.regalloc)
  → direct x64 emission             (mljit.codegen, mljit.x64)
  → mmap'd executable code
```

There is no materialized machine IR: the register allocator produces side-tables over untouched SSA, and the emitter is a direct fold over SSA into the code buffer.

Implemented: the i64 slice — arithmetic, comparisons, block jumps and conditional branches, block parameters, self-recursive fixed-arity calls, and i64 returns. Both demo functions (recursive `fib`, iterative `gcd`) compile natively and match the interpreter across inputs. The register allocator is Wimmer-Franz linear scan on SSA with whole-interval Belady spilling.

Known gaps, all reported as clean `error: jit: unsupported ...` diagnostics rather than crashes — `--backend=interp` handles every one of them:

- **cross-function calls** — only self-recursion is lowered natively ([design settled](docs/wayfinder/tickets/design-module-level-jit-compilation.md), implementation pending);
- **i1 as a value** — an `icmp` must fuse into the branch that follows it;
- **more than 6 parameters or call arguments**;
- `bench`, `--emit-x64`, and structured tracing are specified but not implemented.

Closures, heap values, ADTs, tuples, strings, tagging, and GC are deliberately deferred until the register backend is complete.

## Using it

```bash
$ cat gcd.ml
fun gcd(a, b) =
  if b == 0 then a else gcd(b, a - (a / b) * b)

$ mljit run --entry=gcd gcd.ml 1071 462
21
```

`run` defaults to the JIT; `--backend=interp` uses the SSA interpreter instead. `check` parses and verifies, silent on success. `dump --phase=ssa|regalloc` (repeatable) prints the phase dumps:

```text
$ mljit dump --phase=ssa gcd.ml
func @gcd(i64, i64) -> i64 {
^entry(v0: i64, v1: i64):
  v2: i64 = const_i64 0
  v3: i1 = icmp eq v1, v2
  branch v3, ^bb1(), ^bb2()
^bb1():
  jump ^bb3(v0)
^bb2():
  v5: i64 = idiv v0, v1
  v6: i64 = imul v5, v1
  v7: i64 = isub v0, v6
  v8: i64 = call @gcd(v1, v7)
  jump ^bb3(v8)
^bb3(v4: i64):
  ret v4
}
```

A `-` operand reads the source from stdin. Diagnostics are GCC-style on stderr; exit codes are 0 (success), 1 (compile/runtime error), 2 (bad invocation). The full contract is [`docs/cli.md`](docs/cli.md), and the IR syntax above is specified in [`docs/ir-format.md`](docs/ir-format.md) — both pinned by golden tests.

## Design goals

- Modern C++26 with modules at subsystem boundaries.
- Small, explicit compiler data structures with strongly typed IDs.
- Value-semantic IR APIs, with controlled mutability for backend passes where it matters.
- Stable textual dumps for compiler phases, from day one.
- Interpreter/native equivalence tests before performance work.
- No LLVM, asmjit, or external JIT abstraction in the compiler core.

## Build

The project targets x86-64 Linux and requires Clang with C++26 module support, CMake ≥3.30, and Ninja. A Nix flake supplies the whole toolchain plus ANTLR 4.13 and Catch2 v3:

```bash
nix develop        # or direnv allow
```

```bash
cmake --preset debug          # ASan + UBSan + -Werror
cmake --build --preset debug
ctest --preset debug
./build/debug/src/mljit run --entry=gcd gcd.ml 1071 462
```

Other presets: `release` (with IPO) and `release-asan`.

Dependency scope is governed by [`docs/dependency-policy.md`](docs/dependency-policy.md); implementation conventions live in [`docs/coding-standard.md`](docs/coding-standard.md). ANTLR (frontend only) and Catch2 (tests only) are the sole external dependencies — the compiler core and runtime have none.

## Design planning

The design map lives in [`docs/wayfinder/mljit-design-map.md`](docs/wayfinder/mljit-design-map.md), with per-decision tickets in `docs/wayfinder/tickets/`. Open tickets track module-level compilation (cross-function calls), i1 materialization, and structured trace events.
