# MLJIT Textual SSA IR Format

This document specifies the textual format for MLJIT's block-parameter SSA IR:
the syntax `mljit.ir.printer::to_text` emits, which Catch2 snapshot tests
compare verbatim, and which a future parser will accept for round-tripping.
The reference examples at the end are pinned by the `[ir][spec]` snapshot
tests in `test/test_ir.cpp`; the spec and those tests must change together.

## Design goals

- **Deterministic**: the same module always prints to the same bytes, so
  golden tests can compare with `==`.
- **Line-oriented**: one item per line (header, block label, instruction,
  terminator), so diffs and grep work well.
- **Round-trippable in principle**: the grammar is unambiguous and carries
  every fact in the data model, so a parser can reconstruct the module.
  (The parser itself is future work; today the format is dump-only.)

## Lexical structure

- Encoding is ASCII; indentation is exactly two spaces for instructions and
  terminators; block labels and function headers start in column zero.
- Every line ends with `\n`, including the last.
- **Identifiers** match `[A-Za-z_][A-Za-z0-9_]*`. Sigils distinguish the
  three namespaces:
  - `@name` — function
  - `^name` — block
  - bare `name` — value (block parameter or instruction result)
- **Comments** (accepted by the future parser, never emitted by the
  printer): `//` to end of line.
- Integer literals are signed decimal i64 (`-9223372036854775808` ..
  `9223372036854775807`).

## Names

Every function, block, and value has an optional debug name. The printer
uses the debug name when present; otherwise it falls back to an
auto-generated one:

| entity   | fallback     | derived from                    |
|----------|--------------|---------------------------------|
| function | `f<index>`   | position in the module          |
| block    | `bb<id>`     | `BlockId` (creation order)      |
| value    | `v<id>`      | `ValueId` (creation order)      |

Fallback names are stable for a fixed builder call sequence but renumber
when construction order changes — see [Stability](#stability-for-snapshot-tests).

## Grammar

```
module      ::= function*
function    ::= "func @" name "(" type-list? ")" " -> " type " {\n"
                block+
                "}\n"
block       ::= "^" name "(" param-list? "):\n" inst* term?
param-list  ::= value ": " type ("," " " value ": " type)*
inst        ::= "  " value ": " type " = " op "\n"
op          ::= "const_i64 " int
              | ("iadd" | "isub" | "imul" | "idiv" | "irem") " " value ", " value
              | "icmp " cond " " value ", " value
              | "call @" name "(" value-list? ")"
cond        ::= "eq" | "ne" | "slt" | "sle" | "sgt" | "sge"
term        ::= "  ret " value "\n"
              | "  jump " target "\n"
              | "  branch " value ", " target ", " target "\n"
target      ::= "^" name "(" value-list? ")"
value-list  ::= value (", " value)*
type-list   ::= type (", " type)*
type        ::= "i64" | "i1"
```

Notes on the grammar:

- The function header lists entry-block **parameter types only**; the
  parameter names and their types repeat on the entry block's label line.
  A parser must check the two lists agree.
- Blocks print in `BlockId` order; the entry block is always first.
- Block-parameter SSA: there are no phi instructions. Every control
  transfer to a block passes that block's parameters as **branch
  arguments** in the target's parentheses. The parentheses are mandatory
  even when empty (`^recur()`), including on `jump`.
- `branch cond, ^true(...), ^false(...)` — condition is `i1`, then the
  taken-if-true target, then the taken-if-false target.
- `call @callee(args...)` names the callee by its printed function name.
- A block with no terminator prints none (an unfinished function under
  construction); verified IR always has one terminator per block.
- An empty module prints as the empty string.

## Types and constants

Only `i64` and `i1` exist. `const_i64` is the only constant-producing
instruction; `i1` values arise only from `icmp`. There are no literal
operands — every operand is a value name.

## Stability for snapshot tests

Snapshot tests (`CHECK(to_text(mod) == expected)`) may rely on all of:

- the full line format of headers, labels, instructions, and terminators,
  including the two-space indent, `, ` separators, and sigils;
- the mnemonics and condition-code spellings above;
- function, block, and instruction ordering (creation order);
- debug names appearing verbatim.

They should **not** rely on auto-generated `v<id>`/`bb<id>`/`f<index>`
numbering surviving unrelated edits to the builder call sequence — inserting
one instruction renumbers everything after it. A test that wants immunity
from renumbering should give its values debug names; a test asserting the
whole dump (like the spec examples below) accepts that an IR change means
re-blessing the snapshot.

Adding new instructions, types, or terminators extends the grammar without
changing existing lines, so old snapshots survive additive growth.

## Reference examples

Pinned verbatim by `test/test_ir.cpp` (`[ir][spec]`).

Iterative GCD — loop via back-edge branch args, `irem`, block parameters:

```
func @gcd(i64, i64) -> i64 {
^entry(v0: i64, v1: i64):
  v2: i64 = const_i64 0
  v3: i1 = icmp eq v1, v2
  branch v3, ^base(v0), ^loop(v0, v1)
^base(v4: i64):
  ret v4
^loop(v5: i64, v6: i64):
  v7: i64 = irem v5, v6
  v8: i64 = const_i64 0
  v9: i1 = icmp eq v7, v8
  branch v9, ^base(v6), ^loop(v6, v7)
}
```

Recursive Fibonacci — self-call, empty branch-argument list:

```
func @fib(i64) -> i64 {
^entry(v0: i64):
  v1: i64 = const_i64 2
  v2: i1 = icmp slt v0, v1
  branch v2, ^base(v0), ^recur()
^base(v3: i64):
  ret v3
^recur():
  v4: i64 = const_i64 1
  v5: i64 = isub v0, v4
  v6: i64 = call @fib(v5)
  v7: i64 = const_i64 2
  v8: i64 = isub v0, v7
  v9: i64 = call @fib(v8)
  v10: i64 = iadd v6, v9
  ret v10
}
```
