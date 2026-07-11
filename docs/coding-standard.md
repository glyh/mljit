# MLJIT Coding Standard

This document captures the implementation conventions for MLJIT. The design map describes compiler architecture and project direction; this file describes how code should be written.

## Baseline

- Use C++26 with Clang as the primary compiler.
- Keep code warning-clean under the project CMake warning settings.
- Prefer CMake targets, C++ modules, and explicit dependencies over ad-hoc build glue.
- Use Catch2 for tests through CTest/CMake.

## Style

- Types, classes, concepts, and enum values: `PascalCase`.
- Functions, variables, and namespaces: `snake_case`.
- Private data members: trailing underscore, e.g. `blocks_`.
- True constants: `kPascalCase`.
- Module names: lowercase dotted names, e.g. `mljit.ir.verifier`.
- File names: lowercase with underscores when needed, e.g. `ir_printer.cppm`.
- Prefer explicit return types with trailing return syntax for nontrivial APIs:

```cpp
auto append_block(FunctionId function, std::string name) -> BlockId;
```

Use `auto` when the type is obvious or unimportant. Spell out important domain types:

```cpp
ValueId value = ...;
BlockId block = ...;
Type type = ...;
```

## Modules

Use modules as subsystem boundaries, not as a single giant facade.

Likely modules:

```text
mljit.util
mljit.ir
mljit.ir.printer
mljit.ir.verifier
mljit.ir.interp
mljit.trace
mljit.frontend
mljit.codegen.x64
```

Rules:

- Public APIs live in `.cppm` module interface units.
- Implementation details can move into module implementation units as modules grow.
- Generated parser/lexer code should stay behind a clean frontend module facade.
- Do not expose broad mutable implementation containers just because they are convenient inside a module.

## Data and ownership

Default to value-oriented, RAII-managed code.

- Use strongly typed IDs for persistent IR references.
- Use owning containers such as `std::vector` for canonical IR storage.
- Avoid raw owning pointers.
- Use `std::unique_ptr` only when unique dynamic ownership is needed.
- Avoid `std::shared_ptr` unless ownership is genuinely shared.
- Use `std::span`, `std::string_view`, and references for non-owning views.

Preferred IR shape:

```cpp
struct FunctionId {
  std::uint32_t value;
};

struct BlockId {
  std::uint32_t value;
};

struct ValueId {
  std::uint32_t value;
};
```

Avoid using raw integers for unrelated compiler identities.

## Mutation boundaries

Canonical IR should be hard to corrupt accidentally.

- Prefer builder APIs for IR construction.
- Expose read-only spans/views for inspection.
- Make mutation explicit in API names and pass boundaries.
- Keep backend mutation in backend-owned structures such as transient Machine IR, liveness state, register allocation state, and rewrite plans.

Avoid exposing APIs like:

```cpp
auto blocks() -> std::vector<Block>&;
```

Prefer APIs like:

```cpp
auto blocks(FunctionId function) const -> std::span<const Block>;
auto append_block(FunctionId function, std::string name) -> BlockId;
```

## Error handling

No stringly typed exceptions in core code.

Use structured results for ordinary compiler/user errors:

```cpp
auto verify(const Module& module) -> std::expected<void, DiagnosticList>;
```

Use assertions for internal programming invariants.

Exceptions are not normal compiler control flow. If an exception is used at a boundary, it should carry structured data and be converted into a diagnostic or fatal error at the boundary.

Prefer:

```cpp
enum class FatalErrorKind {
  IoFailure,
  InvalidConfiguration,
  UnsupportedHostPlatform,
  ExecutableMemoryFailure,
};

struct FatalError {
  FatalErrorKind kind;
  std::string message;
  std::source_location location;
};
```

Avoid:

```cpp
throw std::runtime_error("bad thing happened");
```

unless it is contained at an outer boundary and immediately converted to structured output.

## Diagnostics

Diagnostics should be structured enough for tests, CLI output, and future tooling.

- Use enum kinds, not string matching.
- Preserve source/location metadata when available.
- Do not print directly from core compiler logic.
- Return or emit diagnostics through explicit sinks/results.

Avoid:

```cpp
std::println("wrong block arg count");
return false;
```

Prefer:

```cpp
diagnostics.push_back(Diagnostic{
  .kind = DiagnosticKind::WrongBlockArgumentCount,
  .message = "wrong number of block arguments",
});
```

## Pattern matching

Use the standard-library approach first:

```cpp
template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;
```

Then match `std::variant` values with `std::visit`:

```cpp
std::visit(overload{
  [](const Add& add) { /* ... */ },
  [](const Ret& ret) { /* ... */ },
}, payload);
```

## Tracing implementation constraints

MLJIT wants structured event tracing, not a global logger.

Preferred initial implementation:

- local `mljit.trace` module;
- in-memory `Event` records for tests;
- JSONL and/or Chrome Trace Event export for inspection;
- no global logger or general-purpose logging framework.

Dependency decisions for tracing/profiling/logging libraries live in [Dependency Policy](dependency-policy.md).

## Textual output and dumps

Compiler dumps are testable product surfaces.

- Keep dumps deterministic.
- Do not include pointer addresses or nondeterministic map iteration.
- Use stable IDs and stable ordering.
- Prefer snapshot-style assertions for textual IR and phase dumps.

## Tests

Every core compiler feature should land with Catch2 coverage.

For IR features, include at least:

- construction tests;
- dump/snapshot-style tests;
- verifier behavior when applicable;
- interpreter/codegen equivalence later.

Tests should assert structure and behavior, not just absence of crashes.

Prefer:

```cpp
REQUIRE(dump(module) == expected);
REQUIRE(error.kind == DiagnosticKind::WrongBlockArgumentCount);
```

Avoid:

```cpp
REQUIRE_NOTHROW(run());
```

## C++ habits to avoid

Avoid code that is:

- stringly typed;
- pointer-heavy;
- inheritance-heavy without a clear need;
- globally mutable;
- exception-driven;
- template-clever before concrete duplication exists;
- diagnostically opaque;
- sloppy about phase boundaries.

Prefer code that is:

- strongly typed;
- boring where possible;
- inspectable;
- value-oriented;
- RAII-managed;
- deterministic;
- module-bounded;
- explicit about ownership and mutation.
