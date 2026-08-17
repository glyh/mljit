---
title: Choose the Frontend Stack
parent: ../mljit-design-map.md
labels:
  - wayfinder:research
status: closed
resolution: ANTLR4 grammar-driven parser/visitor, generated code in mljit-antlr-gen, isolated to mljit.frontend module
closed_date: 2026-07-11
assignee:
blocked_by: []
---

# Choose the Frontend Stack

## Resolution

MLJIT will use **ANTLR4** (C++ target, `-visitor`) for the first source-language frontend.

This is an explicit dependency-policy exception: ANTLR4 types are allowed only inside the frontend implementation. No ANTLR4 type may leak into `mljit.ir`, `mljit.ir.verifier`, `mljit.ir.runtime`, backend code, or public compiler-core APIs.

## Parser Stack

- Use ANTLR4 (version 4.13.2), grammar at `src/frontend/MLJIT.g4`.
- Generate lexer/parser/visitor C++ files with `-visitor -no-listener -Dlanguage=Cpp`.
- Generated files committed to `src/frontend/generated/` (no regeneration step in CMake).
- Compiled as a separate C++17 OBJECT library (`mljit-antlr-gen`) with `-Wno-unused-parameter`.
- Acquire ANTLR4 tool and C++ runtime through Nix flake (`pkgs.antlr4_13`, `pkgs.antlr4_13.runtime.cpp`, `pkgs.jre`).
- LoweringVisitor in `src/frontend.cppm` walks the ANTLR parse tree and emits SSA IR directly (no intermediate AST).
- Public API: `frontend::compile(source) → std::expected<ir::Module, std::vector<CompileError>>`.

Rationale:

- ANTLR4 keeps the grammar explicit and reviewable while avoiding a hand-written parser detour.
- The frontend is intentionally small and not where the project's interesting work is; a scoped generated parser keeps iteration cheap.
- Keeping the generated-parser dependency isolated prevents the frontend stack from shaping the compiler core.

## Module Architecture

Single module: `mljit.frontend` (`src/frontend.cppm`)

```cpp
export module mljit.frontend;
import mljit.ir;

namespace mljit::frontend {
  auto compile(const std::string& source)
    -> std::expected<Module, std::vector<CompileError>>;
}
```

Implementation may use private helper headers under `src/frontend/` if that keeps `frontend.cppm` readable, but only `mljit.frontend` is public.

## Lowering Boundary

The frontend lowers directly from the ANTLR parse tree into `ir::Module` using private helper state inside `src/frontend.cppm`. The public boundary remains project-owned: callers see `ir::Module` and `CompileError`, never parser-runtime types.

## Language Subset (v1)

ML-style syntax with function definitions, sequential `let`, `if`, calls, arithmetic, comparisons, and parentheses.

```text
program  ::= fundef*

fundef   ::= "fun" ID param* "=" expr

expr     ::= if_expr
if_expr  ::= "if" expr "then" expr "else" expr
           | let_expr
let_expr ::= "let" binding (";" binding)* "in" expr "end"
           | cmp_expr
cmp_expr ::= add_expr (("<" | "<=" | ">" | ">=" | "==" | "!=") add_expr)?
add_expr ::= mul_expr (("+" | "-") mul_expr)*
mul_expr ::= app_expr (("*" | "/" | "%") app_expr)*
app_expr ::= ID atom*        // function application
           | atom
atom     ::= INT | ID | "-" atom | "(" expr ")"

binding  ::= ID "=" expr
```

No typechecking, no `true`/`false` literals, no closures, no top-level expressions in the batch compiler.

## Lowering Strategy

Single pass: parse-tree walk → `IrBuilder` method calls.

- `let x = e1; y = e2 in body end` lowers to sequential instructions in the current block. The local symbol table maps names to `ValueId`s.
- `if cond then e1 else e2` lowers to branch → then/else blocks → join block with one block parameter for the merged expression result.
- `f x y` lowers to `call @f(%x, %y)` after resolving `f` to `FunctionId`.
- Arithmetic/comparison lowers to `iadd`, `isub`, `imul`, `idiv`, `irem`, and `icmp`.

## Error Model

Typed `CompileErrorKind`, accumulated in a vector, with `SourceSpan` location.

```cpp
enum class CompileErrorKind {
  LexError,
  ParseError,
  UndefinedVariable,
  UnknownFunction,
  ArgumentCountMismatch,
  NotAFunction,
};

struct CompileError {
  CompileErrorKind kind;
  SourceSpan span;
  std::string detail;
};
```

Parser diagnostics should produce `ParseError`; lowering diagnostics should produce structured semantic errors such as `UndefinedVariable`, `UnknownFunction`, and `ArgumentCountMismatch`.

## Acceptance Tests

End-to-end: `frontend::compile(source)` → `verifier::verify(module)` → `runtime::Interpreter`.

Required positive tests:

- `add1(41) = 42`
- `abs(-7) = 7`
- `fib(10) = 55`
- `gcd(48, 18) = 6`
- sequential `let` bindings
- arithmetic precedence and parentheses
- division/remainder source syntax

Required negative tests:

- parse error
- undefined variable
- unknown function
- argument-count mismatch

## Guardrails

Do not add parser generation steps to the CMake build. Regenerate deliberately, commit generated files under `src/frontend/generated/`, and keep parser-runtime types isolated to the frontend implementation.
