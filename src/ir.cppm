module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <cassert>

export module mljit.ir;

export namespace mljit::ir {

// ── Types ──────────────────────────────────────────────────
enum class Type : uint8_t { i64, i1 };

constexpr auto to_string(Type t) -> std::string_view {
  switch (t) {
  case Type::i64: return "i64";
  case Type::i1:  return "i1";
  }
  return "?";
}

// ── Comparison predicates ──────────────────────────────────
enum class IcmpCond : uint8_t { eq, ne, slt, sle, sgt, sge };

constexpr auto to_string(IcmpCond c) -> std::string_view {
  switch (c) {
  case IcmpCond::eq:  return "eq";
  case IcmpCond::ne:  return "ne";
  case IcmpCond::slt: return "slt";
  case IcmpCond::sle: return "sle";
  case IcmpCond::sgt: return "sgt";
  case IcmpCond::sge: return "sge";
  }
  return "?";
}

// ── Source span ────────────────────────────────────────────
struct SourceSpan {
  uint32_t source_id = 0;
  uint32_t begin    = 0;
  uint32_t end      = 0;
};

// ── Strongly typed IDs ─────────────────────────────────────
struct FunctionId    { uint32_t value = 0; auto operator<=>(FunctionId const&) const = default; };
struct BlockId       { uint32_t value = 0; auto operator<=>(BlockId const&) const = default; };
struct ValueId       { uint32_t value = 0; auto operator<=>(ValueId const&) const = default; };
struct InstructionId { uint32_t value = 0; auto operator<=>(InstructionId const&) const = default; };

// ── Block parameter ────────────────────────────────────────
struct BlockParam {
  ValueId id;
  Type type;
  std::optional<std::string> debug_name;
  std::optional<SourceSpan> source_span;
};

// ── Instruction payload structs ────────────────────────────
struct ConstI64 { int64_t value = 0; };
struct IAdd     { ValueId lhs; ValueId rhs; };
struct ISub     { ValueId lhs; ValueId rhs; };
struct IMul     { ValueId lhs; ValueId rhs; };
struct IDiv     { ValueId lhs; ValueId rhs; };
struct IRem     { ValueId lhs; ValueId rhs; };
struct ICmp     { IcmpCond cond; ValueId lhs; ValueId rhs; };
struct Call     { FunctionId callee; std::vector<ValueId> args; };

using InstPayload = std::variant<ConstI64, IAdd, ISub, IMul, IDiv, IRem, ICmp, Call>;

// ── Terminator payload structs ─────────────────────────────
struct Ret    { ValueId value; };
struct Jump   { BlockId target; std::vector<ValueId> args; };
struct Branch {
  ValueId cond;
  BlockId true_block;
  std::vector<ValueId> true_args;
  BlockId false_block;
  std::vector<ValueId> false_args;
};

using TermPayload = std::variant<Ret, Jump, Branch>;

// ── IR data nodes ──────────────────────────────────────────

/// A single SSA-producing instruction in the function-local arena.
struct Instruction {
  ValueId result_id;
  Type type;
  InstPayload payload;
  std::optional<std::string> debug_name;
  std::optional<SourceSpan> source_span;
};

/// A basic block with block parameters, instruction list, and optional terminator.
struct Block {
  std::vector<BlockParam> params;
  std::vector<InstructionId> instructions;
  std::optional<TermPayload> terminator;   // none until set_terminator
  std::optional<std::string> debug_name;
  std::optional<SourceSpan> source_span;
};

/// A function owns blocks and an instruction arena; ValueIds are function-local.
struct Function {
  std::vector<Block> blocks;
  std::vector<Instruction> instructions;
  Type return_type = Type::i64;
  uint32_t next_value_id = 0;
  std::optional<std::string> debug_name;
  std::optional<SourceSpan> source_span;
};

/// A module owns all functions; FunctionIds are module-local.
struct Module {
  std::vector<Function> functions;
};

// ── Variadic overload helper ───────────────────────────────
template <typename... Fns>
struct overload : Fns... { using Fns::operator()...; };

template <typename... Fns>
overload(Fns...) -> overload<Fns...>;

// ── Forward declarations ───────────────────────────────────
class ModuleBuilder;
class FunctionBuilder;

// ── ModuleBuilder ──────────────────────────────────────────
class ModuleBuilder {
public:
  ModuleBuilder() = default;

  /// Create a new function with the given parameter types and return type.
  /// The entry block is automatically created with the function parameters
  /// as its block parameters.
  [[nodiscard]] auto create_function(
    std::vector<Type> param_types,
    Type return_type,
    std::optional<std::string> debug_name = std::nullopt
  ) -> FunctionId;

  /// Obtain a builder for the given function.
  [[nodiscard]] auto function_builder(FunctionId fid) -> FunctionBuilder;

  /// Take ownership of the constructed module.
  [[nodiscard]] auto finish() -> Module;

private:
  Module module_;
};

// ── FunctionBuilder ────────────────────────────────────────
//
// Holds a reference to the owning Module + FunctionId so that
// it survives vector reallocation inside ModuleBuilder.
//
class FunctionBuilder {
public:
  FunctionBuilder() = delete;
  explicit FunctionBuilder(Module& mod, FunctionId fid) : module_(mod), fid_(fid) {}

  // ── Block management ──────────────────────────────────────
  [[nodiscard]] auto append_block(
    std::vector<Type> param_types,
    std::optional<std::string> debug_name = std::nullopt
  ) -> BlockId;

  // ── Instruction appending (lower-level) ───────────────────
  [[nodiscard]] auto append_instruction(
    BlockId block,
    Type type,
    InstPayload payload,
    std::optional<std::string> debug_name = std::nullopt
  ) -> ValueId;

  // ── Terminator ────────────────────────────────────────────
  void set_terminator(BlockId block, TermPayload terminator);

  // ── Convenience instruction creators ──────────────────────
  [[nodiscard]] auto const_i64(BlockId block, int64_t value,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto iadd(BlockId block, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto isub(BlockId block, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto imul(BlockId block, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto idiv(BlockId block, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto irem(BlockId block, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto icmp(BlockId block, IcmpCond cond, ValueId lhs, ValueId rhs,
    std::optional<std::string> name = std::nullopt) -> ValueId;
  [[nodiscard]] auto call(BlockId block, FunctionId callee, std::vector<ValueId> args,
    std::optional<std::string> name = std::nullopt) -> ValueId;

  // ── Convenience terminators ───────────────────────────────
  void ret(BlockId block, ValueId value);
  void jump(BlockId block, BlockId target, std::vector<ValueId> args);
  void branch(BlockId block,
    ValueId cond, BlockId true_block, std::vector<ValueId> true_args,
    BlockId false_block, std::vector<ValueId> false_args);

  // ── Read-only accessors ───────────────────────────────────
  [[nodiscard]] auto entry_block() const -> BlockId { return BlockId{0}; }
  [[nodiscard]] auto param_id(BlockId block, size_t index) const -> ValueId;
  [[nodiscard]] auto block_count() const -> size_t;
  [[nodiscard]] auto instruction_count() const -> size_t;

private:
  // Re-resolve the function reference each time (safe across pushes).
  [[nodiscard]] auto func()       -> Function&       { return module_.functions[fid_.value]; }
  [[nodiscard]] auto func() const -> Function const& { return module_.functions[fid_.value]; }

  // Look up the type of a ValueId by scanning block params and instruction results.
  [[nodiscard]] auto lookup_type(ValueId id) const -> std::optional<Type>;

  Module&   module_;
  FunctionId fid_;
};

// ═══════════════════════════════════════════════════════════
//  Out-of-line member definitions (must follow both classes)
// ═══════════════════════════════════════════════════════════

inline auto FunctionBuilder::param_id(BlockId block, size_t index) const -> ValueId {
  return func().blocks[block.value].params[index].id;
}

inline auto FunctionBuilder::block_count() const -> size_t {
  return func().blocks.size();
}

inline auto FunctionBuilder::instruction_count() const -> size_t {
  return func().instructions.size();
}

inline auto FunctionBuilder::lookup_type(ValueId id) const -> std::optional<Type> {
  for (auto const& blk : func().blocks)
    for (auto const& p : blk.params)
      if (p.id == id) return p.type;
  for (auto const& inst : func().instructions)
    if (inst.result_id == id) return inst.type;
  return std::nullopt;
}

// ═══════════════════════════════════════════════════════════
//  Inline implementations
// ═══════════════════════════════════════════════════════════

inline auto ModuleBuilder::create_function(
  std::vector<Type> param_types,
  Type return_type,
  std::optional<std::string> debug_name
) -> FunctionId {
  Function f;
  f.return_type = return_type;
  f.debug_name = std::move(debug_name);

  Block entry;
  entry.debug_name = "entry";
  for (auto t : param_types) {
    ValueId vid{f.next_value_id++};
    entry.params.push_back(BlockParam{vid, t, std::nullopt, std::nullopt});
  }
  f.blocks.push_back(std::move(entry));

  FunctionId fid{static_cast<uint32_t>(module_.functions.size())};
  module_.functions.push_back(std::move(f));
  return fid;
}

inline auto ModuleBuilder::function_builder(FunctionId fid) -> FunctionBuilder {
  return FunctionBuilder{module_, fid};
}

inline auto ModuleBuilder::finish() -> Module {
  return std::move(module_);
}

inline auto FunctionBuilder::append_block(
  std::vector<Type> param_types,
  std::optional<std::string> debug_name
) -> BlockId {
  Block b;
  b.debug_name = std::move(debug_name);
  for (auto t : param_types) {
    ValueId vid{func().next_value_id++};
    b.params.push_back(BlockParam{vid, t, std::nullopt, std::nullopt});
  }
  BlockId bid{static_cast<uint32_t>(func().blocks.size())};
  func().blocks.push_back(std::move(b));
  return bid;
}

inline auto FunctionBuilder::append_instruction(
  BlockId block,
  Type type,
  InstPayload payload,
  std::optional<std::string> debug_name
) -> ValueId {
  auto& blk      = func().blocks.at(block.value);
  assert(!blk.terminator.has_value() && "cannot append instruction to terminated block");
  auto& f        = func();
  ValueId vid{f.next_value_id++};
  InstructionId iid{static_cast<uint32_t>(f.instructions.size())};
  f.instructions.push_back(Instruction{vid, type, std::move(payload), std::move(debug_name), std::nullopt});
  blk.instructions.push_back(iid);
  return vid;
}

inline void FunctionBuilder::set_terminator(BlockId block, TermPayload terminator) {
  auto& blk = func().blocks.at(block.value);
  assert(!blk.terminator.has_value() && "block already has a terminator");
  blk.terminator = std::move(terminator);
}

// ── Convenience instruction creators ──────────────────────

inline auto FunctionBuilder::const_i64(
  BlockId block, int64_t value, std::optional<std::string> name
) -> ValueId {
  return append_instruction(block, Type::i64, ConstI64{value}, std::move(name));
}

inline auto FunctionBuilder::iadd(
  BlockId block, ValueId lhs, ValueId rhs, std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "iadd lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "iadd rhs must be i64");
  return append_instruction(block, Type::i64, IAdd{lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::isub(
  BlockId block, ValueId lhs, ValueId rhs, std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "isub lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "isub rhs must be i64");
  return append_instruction(block, Type::i64, ISub{lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::imul(
  BlockId block, ValueId lhs, ValueId rhs, std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "imul lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "imul rhs must be i64");
  return append_instruction(block, Type::i64, IMul{lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::idiv(
  BlockId block, ValueId lhs, ValueId rhs, std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "idiv lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "idiv rhs must be i64");
  return append_instruction(block, Type::i64, IDiv{lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::irem(
  BlockId block, ValueId lhs, ValueId rhs, std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "irem lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "irem rhs must be i64");
  return append_instruction(block, Type::i64, IRem{lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::icmp(
  BlockId block, IcmpCond cond, ValueId lhs, ValueId rhs,
  std::optional<std::string> name
) -> ValueId {
  assert(lookup_type(lhs) == Type::i64 && "icmp lhs must be i64");
  assert(lookup_type(rhs) == Type::i64 && "icmp rhs must be i64");
  return append_instruction(block, Type::i1, ICmp{cond, lhs, rhs}, std::move(name));
}

inline auto FunctionBuilder::call(
  BlockId block, FunctionId callee, std::vector<ValueId> args,
  std::optional<std::string> name
) -> ValueId {
  assert(callee.value < module_.functions.size() && "callee FunctionId out of range");
  auto const& callee_func = module_.functions[callee.value];
  auto const& callee_entry = callee_func.blocks[0];
  assert(args.size() == callee_entry.params.size() && "call arg count mismatch");
  for (size_t i = 0; i < args.size(); ++i) {
    assert(lookup_type(args[i]) == callee_entry.params[i].type && "call arg type mismatch");
  }
  return append_instruction(block, callee_func.return_type, Call{callee, std::move(args)}, std::move(name));
}

// ── Convenience terminators ───────────────────────────────

inline void FunctionBuilder::ret(BlockId block, ValueId value) {
  assert(lookup_type(value) == func().return_type && "ret value type must match function return type");
  set_terminator(block, Ret{value});
}

inline void FunctionBuilder::jump(BlockId block, BlockId target, std::vector<ValueId> args) {
  assert(target.value < func().blocks.size() && "jump target BlockId out of range");
  auto const& tgt = func().blocks[target.value];
  assert(args.size() == tgt.params.size() && "jump arg count must match target block param count");
  // Note: we don't assert arg types for jump/branch for now; verifier owns full checking.
  set_terminator(block, Jump{target, std::move(args)});
}

inline void FunctionBuilder::branch(
  BlockId block,
  ValueId cond, BlockId true_block, std::vector<ValueId> true_args,
  BlockId false_block, std::vector<ValueId> false_args
) {
  assert(lookup_type(cond) == Type::i1 && "branch condition must be i1");
  assert(true_block.value < func().blocks.size() && "branch true_block out of range");
  assert(false_block.value < func().blocks.size() && "branch false_block out of range");
  auto const& tb = func().blocks[true_block.value];
  auto const& fb = func().blocks[false_block.value];
  assert(true_args.size() == tb.params.size() && "branch true_args count mismatch");
  assert(false_args.size() == fb.params.size() && "branch false_args count mismatch");
  set_terminator(block, Branch{cond, true_block, std::move(true_args), false_block, std::move(false_args)});
}

} // namespace mljit::ir
