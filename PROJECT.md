# ML-JIT: A JIT-Compiled Subset of Standard ML

A resume project: implement a Hindley-Milner type-inferred functional language (a subset of Standard ML) with a custom bytecode VM and template-based JIT compiler targeting x86-64. No external compiler dependencies (no LLVM, no MLton). Written in C++20.

## Goals

1. **Learn**: Hindley-Milner type inference (Algorithm W), bytecode VM design, register allocation, native code generation, calling conventions.
2. **Ship**: a self-contained binary that reads `.sml` source files and either interprets them (debug mode) or JIT-compiles and runs them (release mode).
3. **Show**: "I understand compilers end-to-end" — source text to native machine code.

## Non-goals

- Production performance. Correctness and clarity over raw speed.
- Standard ML compatibility. This is a subset; full SML is far too large.
- Cross-platform. x86-64 Linux only (for now).
- Garbage collection. Naive reference counting or arena allocation at first.

## Language Subset

```
types       ::= int | bool | string | unit
              | t1 -> t2
              | t1 * t2                  (* tuples *)
              | typeid                    (* user-defined ADTs *)

patterns    ::= _ | x | n | true | false | ""
              | (p1, p2, ...)
              | Ctor(p1, p2, ...)        (* constructor pattern *)

expr        ::= n | true | false | "" | ()
              | x                         (* variable *)
              | e1 + e2 | e1 - e2 | ...   (* arithmetic *)
              | e1 = e2 | e1 < e2 | ...   (* comparison *)
              | if e1 then e2 else e3
              | let x = e1 in e2
              | let rec f x = e1 in e2    (* recursive function *)
              | fn x => e
              | e1 e2                     (* application *)
              | (e1, e2, ...)
              | case e of p1 => e1 | p2 => e2 | ...

decl        ::= val x = e
              | type typeid = Ctor1 of t1 | Ctor2 of t2 | ...
              | expr                     (* top-level expression *)
```

### Example program

```sml
(* Option type *)
type option = Some of int | None

(* Recursive list type *)
type intlist = Cons of int * intlist | Nil

let rec length xs =
  case xs of
    Nil => 0
  | Cons(h, t) => 1 + length t
in
  let xs = Cons(1, Cons(2, Cons(3, Nil))) in
  length xs
end
end
```

## Architecture

```
Source text
    │
    ▼
┌─────────┐    ┌─────────┐    ┌───────────────┐    ┌──────────────┐    ┌──────────────┐
│  Lexer  │───▶│ Parser  │───▶│  Type Checker  │───▶│   Bytecode    │───▶│  Template    │
│ (regex) │    │(recursive│    │ (Algorithm W)  │    │   Compiler    │    │     JIT      │
│         │    │ descent)│    │                 │    │               │    │ (x86-64)     │
└─────────┘    └─────────┘    └───────────────┘    └──────────────┘    └──────────────┘
                                                          │
                                                          ▼
                                                   ┌──────────────┐
                                                   │  Interpreter │
                                                   │  (bytecode)  │
                                                   └──────────────┘
```

### Stage 1: Frontend

**Lexer**: Hand-written or generated. Produces a token stream. Tracks source locations for error messages.

**Parser**: Recursive-descent. Produces an untyped AST. Reports syntax errors with source locations.

Key: the AST should be simple — no desugaring in the parser. Keep a 1:1 mapping to source syntax.

### Stage 2: Type Checker

Implement **Algorithm W** (Damas-Milner) with let-polymorphism.

- Unification-based type inference
- Type variables, type constructors, arrow types, product types
- Let-polymorphism: `let id = fn x => x in ...` infers `forall a. a -> a`
- Algebraic data types: each `type` declaration introduces type constructors
- Pattern exhaustiveness checking
- Produce a **typed AST** (every node tagged with its inferred type)

Key paper: "Principal type-schemes for functional programs" (Damas & Milner, 1982).

### Stage 3: Bytecode Compiler

Lower the typed AST to a stack-based bytecode.

Instruction set (illustrative, design as needed):

```
PUSH_INT n       push integer constant
PUSH_BOOL b      push boolean constant
ADD              pop two, push sum
CALL nargs       call function at top of stack with nargs arguments
RET              return from function
LOAD idx         push local variable at frame index
STORE idx        pop into local variable at frame index
JMP offset       unconditional jump
JMP_FALSE offset pop and jump if false
MAKE_CLOSURE id nfree   create closure for function id with n free vars
CLOSURE_LOAD n   load nth free variable from current closure
APPLY nargs      tail-call with nargs arguments
CONSTRUCT id n   build ADT value with tag id and n fields
CASE_FAIL        no pattern matched — runtime error
DUP              duplicate top of stack
POP              discard top of stack
HALT             end execution
```

Bytecode is stored in a linear array of instructions with a constant pool. Functions become bytecode objects with arity, local count, and captured free variable count.

### Stage 4: Bytecode Interpreter

A straightforward `while` loop + `switch` over opcodes. The VM has:

- A value stack (tagged union: `Int | Bool | String | Closure | ADT | Unit`)
- A call stack (frames with saved PC, locals, closure pointer)
- A heap for strings and closures (simple bump allocator or `std::vector`)

This stage serves two purposes:
1. Verify correctness before tackling native codegen
2. The interpreter stays as the "debug mode" / ground truth

### Stage 5: Template JIT Compiler

Translate bytecode to x86-64 machine code at runtime. **Template-based**: each bytecode opcode maps to a pre-written assembly template with slots for immediates.

Key components:

- **Code buffer**: `mmap` with `PROT_READ | PROT_WRITE | PROT_EXEC`
- **Register allocation**: linear scan over basic blocks. Spill to stack when registers exhausted.
- **Calling convention**: System V AMD64 ABI for calls into C runtime (for `printf`, `malloc`, etc.). Internal calls use a custom convention.
- **Value representation**: tagged pointers (least significant bit 0 = pointer, 1 = integer shifted left 1). Or a fat tagged union. Trade off between speed and simplicity.
- **GC**: start with no GC (leak memory). Once stable, add a simple mark-and-sweep or reference counting.

Template examples (pseudocode):

```
PUSH_INT n  →  mov rax, (n << 1) | 1
               push rax

ADD         →  pop rbx
               pop rax
               sar rax, 1
               sar rbx, 1
               add rax, rbx
               shl rax, 1
               or  rax, 1
               push rax

CALL nargs  →  [emit SysV argument setup for nargs args]
               call [closure->code_ptr]
               [handle return value]
```

**Stretch goal**: basic optimizations — constant folding, inlining of small functions, peephole optimization on generated machine code.

## Milestones

### M1: Bootstrap interpreter (no types)
- Lexer, parser, untyped AST
- Direct tree-walking interpreter (no bytecode yet)
- `int`, `bool`, `if/then/else`, `let`, `fn`, `apply`, `+`, `-`, `=`, `<`
- REPL and file runner

### M2: Type inference
- Type representation (variables, constructors, arrows, products)
- Unification
- Algorithm W with let-polymorphism
- Type error reporting with source locations
- ADT type declarations + constructor/pattern typing

### M3: ADTs + pattern matching + tuples
- Parsing and type-checking ADT declarations
- Constructor expressions and case expressions
- Pattern match compilation to decision tree or backtracking automaton
- Tuples (syntax, typing, evaluation)

### M4: Bytecode VM
- Bytecode instruction set design
- Compiler from typed AST to bytecode
- Stack-based interpreter
- Support closures (flat closure conversion for free variables)
- Tail-call optimization via `APPLY` instruction

### M5: Template JIT
- Code buffer management (`mmap`, write-protect after compilation)
- Template macro system or code-gen per opcode
- Calling convention: internal + external (System V)
- Linear-scan register allocator
- Glue code to enter JIT from C++ and return
- Benchmark: JIT vs interpreter speedup

## Tech Stack

- **Language**: C++20
- **Build**: CMake 3.28+ with presets, Ninja
- **Dependencies**: none for the compiler itself. Optionally:
  - `doctest` for unit tests
  - `fmt` for formatting (header-only, CPM.cmake)
  - `spdlog` for logging (optional)
- **Target**: x86-64 Linux (Debian/Ubuntu)
- **Compiler**: GCC 14+ or Clang 18+

## Project Layout

```
ml-jit/
├── CMakeLists.txt
├── CMakePresets.json
├── src/
│   ├── lexer.hpp / lexer.cpp
│   ├── parser.hpp / parser.cpp
│   ├── ast.hpp               # Untyped AST node types
│   ├── types.hpp / types.cpp  # Type representation, unification
│   ├── infer.hpp / infer.cpp  # Algorithm W
│   ├── typed_ast.hpp          # Typed AST node types
│   ├── bytecode.hpp / bytecode.cpp  # Instruction set, compiler
│   ├── vm.hpp / vm.cpp        # Bytecode interpreter
│   ├── jit.hpp / jit.cpp      # Template JIT compiler
│   ├── value.hpp / value.cpp  # Runtime value representation
│   ├── main.cpp               # CLI entry point
│   └── util.hpp               # Result<T,E>, source locations, etc.
├── test/
│   ├── lexer.test.cpp
│   ├── parser.test.cpp
│   ├── infer.test.cpp
│   ├── vm.test.cpp
│   └── integration/           # End-to-end .sml test files
└── examples/                  # Demo .sml programs
```

## Testing Strategy

- **Unit tests** (doctest) per pipeline stage
- **Snapshot tests**: parse → typecheck → bytecode → interpreter. Golden file of expected output.
- **Property tests**: round-trip properties (e.g., interpreter vs JIT produce same result for any valid program)
- **Integration tests**: `.sml` files in `test/integration/` with expected stdout
- **Fuzz testing** (stretch): generate random well-typed programs, run through both interpreter and JIT, assert equal output

## Key References

- Damas & Milner, "Principal type-schemes for functional programs" (1982)
- Appel, "Modern Compiler Implementation in ML" (1998)
- Peyton Jones, "The Implementation of Functional Programming Languages" (1987)
- Aho, Lam, Sethi, Ullman, "Compilers: Principles, Techniques, and Tools" (2006)
- System V AMD64 ABI specification
- Intel x86-64 manual (instruction encoding)

## Resume Talking Points

- "Implemented a Hindley-Milner type inference engine (Algorithm W) with let-polymorphism and algebraic data types"
- "Designed a stack-based bytecode VM with closure conversion and tail-call optimization"
- "Built a template-based JIT compiler generating x86-64 machine code at runtime with linear-scan register allocation"
- "End-to-end compiler pipeline: source text → type checking → bytecode → native code — in ~X lines of C++20"
