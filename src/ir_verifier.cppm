module;

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <cassert>
#include <functional>

export module mljit.ir.verifier;

import mljit.ir;

export namespace mljit::ir::verifier {

// ── Error kinds ────────────────────────────────────────────
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

// ── Verify error ──────────────────────────────────────────
struct VerifyError {
  VerifyErrorKind kind;
  FunctionId      function;
  std::optional<BlockId>        block;
  std::optional<InstructionId>  instruction;
  std::optional<ValueId>        value;
  std::optional<BlockId>        target_block;
  std::string detail;
};

// ── Verify result ─────────────────────────────────────────
struct VerifyResult {
  std::vector<VerifyError> errors;
  [[nodiscard]] auto ok() const -> bool { return errors.empty(); }
};

// ═══════════════════════════════════════════════════════════
//  Internal helpers (detail namespace — not exported)
// ═══════════════════════════════════════════════════════════

namespace detail {

// ── Successor extraction from a terminator ─────────────────
auto terminator_successors(TermPayload const& term) -> std::vector<BlockId> {
  return std::visit(overload{
    [](Ret const&)     -> std::vector<BlockId> { return {}; },
    [](Jump const& op) -> std::vector<BlockId> { return {op.target}; },
    [](Branch const& op) -> std::vector<BlockId> { return {op.true_block, op.false_block}; },
  }, term);
}

// ── Value type lookup ─────────────────────────────────────
auto value_type(Function const& func, ValueId id) -> std::optional<Type> {
  for (auto const& blk : func.blocks)
    for (auto const& p : blk.params)
      if (p.id == id) return p.type;
  for (auto const& inst : func.instructions)
    if (inst.result_id == id) return inst.type;
  return std::nullopt;
}

// ── Cooper-Harvey-Kennedy iterative dominators ────────────
struct Dominance {
  std::vector<size_t> idom;
  std::vector<bool>   reachable;
  std::vector<size_t> postorder;
  size_t              n_blocks = 0;

  void compute(Function const& func) {
    n_blocks = func.blocks.size();
    idom.assign(n_blocks, SIZE_MAX);
    reachable.assign(n_blocks, false);

    std::vector<std::vector<size_t>> preds(n_blocks);
    for (size_t bi = 0; bi < n_blocks; ++bi) {
      auto const& blk = func.blocks[bi];
      if (!blk.terminator) continue;
      for (auto succ : terminator_successors(*blk.terminator))
        if (succ.value < n_blocks) preds[succ.value].push_back(bi);
    }

    postorder.assign(n_blocks, 0);
    std::vector<bool> visited(n_blocks, false);
    std::vector<size_t> postorder_list;

    std::function<void(size_t)> dfs = [&](size_t b) {
      if (b >= n_blocks || visited[b]) return;
      visited[b] = true;
      auto const& blk = func.blocks[b];
      if (blk.terminator)
        for (auto succ : terminator_successors(*blk.terminator))
          if (succ.value < n_blocks && !visited[succ.value]) dfs(succ.value);
      postorder[b] = postorder_list.size();
      postorder_list.push_back(b);
    };
    dfs(0);

    for (size_t i = 0; i < n_blocks; ++i) reachable[i] = visited[i];
    if (n_blocks == 0) return;
    idom[0] = 0;

    bool changed = true;
    while (changed) {
      changed = false;
      for (auto it = postorder_list.rbegin(); it != postorder_list.rend(); ++it) {
        size_t b = *it;
        if (b == 0) continue;
        size_t new_idom = SIZE_MAX;
        for (size_t p : preds[b]) {
          if (!reachable[p] || idom[p] == SIZE_MAX) continue;
          if (new_idom == SIZE_MAX) new_idom = p;
          else new_idom = intersect(new_idom, p);
        }
        if (new_idom != SIZE_MAX && new_idom != idom[b]) {
          idom[b] = new_idom;
          changed = true;
        }
      }
    }
  }

  [[nodiscard]] auto dominates(size_t a, size_t b) const -> bool {
    if (!reachable[b] || !reachable[a]) return false;
    if (a == b) return true;
    // Walk up idom chain; stop when we reach the entry (idom == self) without finding a.
    for (size_t cur = b; ; cur = idom[cur]) {
      if (cur == a) return true;
      if (cur == idom[cur]) return false; // reached entry, not found
    }
  }

private:
  [[nodiscard]] auto intersect(size_t a, size_t b) const -> size_t {
    size_t f1 = a, f2 = b;
    while (f1 != f2) {
      while (postorder[f1] < postorder[f2]) f1 = idom[f1];
      while (postorder[f2] < postorder[f1]) f2 = idom[f2];
    }
    return f1;
  }
};

// ── Per-function verifier ──────────────────────────────────
struct FunctionVerifier {
  Function const&      func;
  FunctionId           func_id;
  Dominance            dom;
  std::vector<VerifyError>& errors;

  FunctionVerifier(Function const& f, FunctionId fid,
                   std::vector<VerifyError>& errs)
    : func(f), func_id(fid), errors(errs) {}

  void verify();

private:
  auto value_exists(ValueId id) const -> bool {
    if (id.value >= func.next_value_id) return false;
    for (auto const& blk : func.blocks)
      for (auto const& p : blk.params)
        if (p.id == id) return true;
    for (auto const& inst : func.instructions)
      if (inst.result_id == id) return true;
    return false;
  }

  void type_error(ValueId vid, Type expected, InstructionId iid, char const* ctx) {
    if (!value_exists(vid)) {
      errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, std::nullopt, iid, vid, std::nullopt, ctx});
      return;
    }
    auto actual = value_type(func, vid);
    if (actual && *actual != expected)
      errors.push_back({VerifyErrorKind::InstructionOperandTypeMismatch, func_id, std::nullopt, iid, vid, std::nullopt,
        std::string(ctx) + " expected " + std::string(to_string(expected)) + " got " + std::string(to_string(*actual))});
  }

  auto find_def_block(ValueId vid) const -> std::optional<size_t> {
    for (size_t bi = 0; bi < func.blocks.size(); ++bi)
      for (auto const& p : func.blocks[bi].params)
        if (p.id == vid) return bi;
    for (size_t bi = 0; bi < func.blocks.size(); ++bi)
      for (auto iid : func.blocks[bi].instructions)
        if (iid.value < func.instructions.size() && func.instructions[iid.value].result_id == vid) return bi;
    return std::nullopt;
  }

  auto is_block_param(ValueId vid) const -> bool {
    for (auto const& blk : func.blocks)
      for (auto const& p : blk.params)
        if (p.id == vid) return true;
    return false;
  }

  auto find_def_instruction(ValueId vid) const -> std::optional<InstructionId> {
    for (size_t i = 0; i < func.instructions.size(); ++i)
      if (func.instructions[i].result_id == vid)
        return InstructionId{static_cast<uint32_t>(i)};
    return std::nullopt;
  }
};

// ── Module-level call target checks ────────────────────────
void verify_calls(Module const& mod, std::vector<VerifyError>& errors) {
  for (size_t fi = 0; fi < mod.functions.size(); ++fi) {
    FunctionId fid{static_cast<uint32_t>(fi)};
    auto const& func = mod.functions[fi];
    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
      BlockId bid{static_cast<uint32_t>(bi)};
      auto const& blk = func.blocks[bi];
      for (auto iid : blk.instructions) {
        if (iid.value >= func.instructions.size()) continue;
        auto const& inst = func.instructions[iid.value];
        auto const* call_ptr = std::get_if<Call>(&inst.payload);
        if (!call_ptr) continue;
        auto const& call_op = *call_ptr;

        if (call_op.callee.value >= mod.functions.size()) {
          errors.push_back({VerifyErrorKind::InvalidFunctionTarget, fid, bid, iid, std::nullopt, std::nullopt,
            "callee FunctionId " + std::to_string(call_op.callee.value) + " out of range"});
          continue;
        }

        auto const& callee = mod.functions[call_op.callee.value];
        if (callee.blocks.empty()) continue;
        auto const& callee_entry = callee.blocks[0];

        if (call_op.args.size() != callee_entry.params.size()) {
          errors.push_back({VerifyErrorKind::CallArgumentCountMismatch, fid, bid, iid, std::nullopt, std::nullopt,
            "call provides " + std::to_string(call_op.args.size()) + " args, callee expects " + std::to_string(callee_entry.params.size())});
        } else {
          for (size_t i = 0; i < call_op.args.size(); ++i) {
            auto at = value_type(func, call_op.args[i]);
            auto pt = callee_entry.params[i].type;
            if (at && *at != pt)
              errors.push_back({VerifyErrorKind::CallArgumentTypeMismatch, fid, bid, iid, call_op.args[i], std::nullopt,
                "arg " + std::to_string(i) + " expected " + std::string(to_string(pt)) + " got " + std::string(to_string(*at))});
          }
        }
      }
    }
  }
}

// ── FunctionVerifier::verify ──────────────────────────────
void FunctionVerifier::verify() {
  if (func.blocks.empty()) return;

  // ── Pass 1: structural and type checks ──────────────────
  for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
    auto const& blk = func.blocks[bi];
    BlockId bid{static_cast<uint32_t>(bi)};

    if (!blk.terminator.has_value()) {
      errors.push_back({VerifyErrorKind::MissingTerminator, func_id, bid,
        std::nullopt, std::nullopt, std::nullopt,
        "block ^" + (blk.debug_name ? *blk.debug_name : std::to_string(bi))});
      continue;
    }

    auto const& term = *blk.terminator;
    std::visit(overload{
      [&](Ret const& op) {
        if (!value_exists(op.value))
          errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, std::nullopt, op.value, std::nullopt, "ret value"});
        auto vt = value_type(func, op.value);
        if (vt && *vt != func.return_type)
          errors.push_back({VerifyErrorKind::ReturnTypeMismatch, func_id, bid, std::nullopt, op.value, std::nullopt, "ret value"});
      },
      [&](Jump const& op) {
        if (op.target.value >= func.blocks.size()) {
          errors.push_back({VerifyErrorKind::InvalidBlockTarget, func_id, bid, std::nullopt, std::nullopt, op.target, "jump target"});
          return;
        }
        auto const& tgt = func.blocks[op.target.value];
        if (op.args.size() != tgt.params.size())
          errors.push_back({VerifyErrorKind::BlockArgumentCountMismatch, func_id, bid, std::nullopt, std::nullopt, op.target,
            std::to_string(op.args.size()) + " args, expected " + std::to_string(tgt.params.size())});
        else for (size_t i = 0; i < op.args.size(); ++i) {
          auto at = value_type(func, op.args[i]);
          if (at && *at != tgt.params[i].type)
            errors.push_back({VerifyErrorKind::BlockArgumentTypeMismatch, func_id, bid, std::nullopt, op.args[i], op.target, "arg " + std::to_string(i)});
        }
        for (auto arg : op.args)
          if (!value_exists(arg))
            errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, std::nullopt, arg, std::nullopt, "jump arg"});
      },
      [&](Branch const& op) {
        auto ct = value_type(func, op.cond);
        if (ct && *ct != Type::i1)
          errors.push_back({VerifyErrorKind::BranchConditionTypeMismatch, func_id, bid, std::nullopt, op.cond, std::nullopt, "branch condition"});
        if (!value_exists(op.cond))
          errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, std::nullopt, op.cond, std::nullopt, "branch condition"});

        auto check = [&](BlockId tbid, std::vector<ValueId> const& args, char const* label) {
          if (tbid.value >= func.blocks.size()) {
            errors.push_back({VerifyErrorKind::InvalidBlockTarget, func_id, bid, std::nullopt, std::nullopt, tbid, label});
            return;
          }
          auto const& tgt = func.blocks[tbid.value];
          if (args.size() != tgt.params.size())
            errors.push_back({VerifyErrorKind::BlockArgumentCountMismatch, func_id, bid, std::nullopt, std::nullopt, tbid,
              std::string(label) + ": " + std::to_string(args.size()) + " args, expected " + std::to_string(tgt.params.size())});
          else for (size_t i = 0; i < args.size(); ++i) {
            auto at = value_type(func, args[i]);
            if (at && *at != tgt.params[i].type)
              errors.push_back({VerifyErrorKind::BlockArgumentTypeMismatch, func_id, bid, std::nullopt, args[i], tbid,
                std::string(label) + " arg " + std::to_string(i)});
          }
          for (auto arg : args)
            if (!value_exists(arg))
              errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, std::nullopt, arg, std::nullopt, std::string(label) + " arg"});
        };
        check(op.true_block, op.true_args, "true");
        check(op.false_block, op.false_args, "false");
      },
    }, term);

    for (auto iid : blk.instructions) {
      if (iid.value >= func.instructions.size()) continue;
      auto const& inst = func.instructions[iid.value];
      if (!value_exists(inst.result_id))
        errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, iid, inst.result_id, std::nullopt, "instruction result"});

      std::visit(overload{
        [&](ConstI64 const&) {},
        [&](IAdd const& op) { type_error(op.lhs, Type::i64, iid, "iadd lhs"); type_error(op.rhs, Type::i64, iid, "iadd rhs"); },
        [&](ISub const& op) { type_error(op.lhs, Type::i64, iid, "isub lhs"); type_error(op.rhs, Type::i64, iid, "isub rhs"); },
        [&](IMul const& op) { type_error(op.lhs, Type::i64, iid, "imul lhs"); type_error(op.rhs, Type::i64, iid, "imul rhs"); },
        [&](IDiv const& op) { type_error(op.lhs, Type::i64, iid, "idiv lhs"); type_error(op.rhs, Type::i64, iid, "idiv rhs"); },
        [&](IRem const& op) { type_error(op.lhs, Type::i64, iid, "irem lhs"); type_error(op.rhs, Type::i64, iid, "irem rhs"); },
        [&](ICmp const& op) { type_error(op.lhs, Type::i64, iid, "icmp lhs"); type_error(op.rhs, Type::i64, iid, "icmp rhs"); },
        [&](Call const& op) {
          for (auto arg : op.args)
            if (!value_exists(arg))
              errors.push_back({VerifyErrorKind::InvalidValueReference, func_id, bid, iid, arg, std::nullopt, "call arg"});
        },
      }, inst.payload);
    }
  }

  // ── Pass 2: CFG reachability ────────────────────────────
  dom.compute(func);

  // UnreachableBlock
  for (size_t bi = 1; bi < func.blocks.size(); ++bi)
    if (!dom.reachable[bi])
      errors.push_back({VerifyErrorKind::UnreachableBlock, func_id, BlockId{static_cast<uint32_t>(bi)},
        std::nullopt, std::nullopt, std::nullopt, ""});

  // ── Pass 3: SSA use-dominance ───────────────────────────
  for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
    if (!dom.reachable[bi]) continue;
    auto const& blk = func.blocks[bi];
    BlockId bid{static_cast<uint32_t>(bi)};
    if (!blk.terminator) continue;

    auto check_use = [&](ValueId vid, std::optional<InstructionId> use_iid, char const* ctx) {
      if (!value_exists(vid)) return;
      auto def_block = find_def_block(vid);
      if (!def_block) return;

      if (*def_block == bi) {
        if (!is_block_param(vid)) {
          auto def_iid = find_def_instruction(vid);
            if (def_iid) {
              // Check that definition precedes use in the same block.
              // For terminator uses (use_iid == nullopt), any instruction def is valid.
              bool before = false;
              if (use_iid) {
                for (auto instr_id : blk.instructions) {
                  if (instr_id == *def_iid) before = true;
                  if (instr_id == *use_iid) break;
                }
              } else {
                before = true; // terminator: all preceding defs available
                for (auto instr_id : blk.instructions) {
                  if (instr_id == *def_iid) { before = true; break; }
                }
              }
              if (!before)
              errors.push_back({VerifyErrorKind::UseDoesNotDominate, func_id, bid, use_iid, vid, std::nullopt,
                std::string(ctx) + " — same-block use before def"});
          }
        }
      } else {
        if (!dom.dominates(*def_block, bi))
          errors.push_back({VerifyErrorKind::UseDoesNotDominate, func_id, bid, use_iid, vid, std::nullopt, ctx});
      }
    };

    std::visit(overload{
      [&](Ret const& op)     { check_use(op.value, std::nullopt, "ret"); },
      [&](Jump const& op)    { for (auto a : op.args) check_use(a, std::nullopt, "jump arg"); },
      [&](Branch const& op)  {
        check_use(op.cond, std::nullopt, "branch cond");
        for (auto a : op.true_args)  check_use(a, std::nullopt, "branch true arg");
        for (auto a : op.false_args) check_use(a, std::nullopt, "branch false arg");
      },
    }, *blk.terminator);

    for (auto iid : blk.instructions) {
      if (iid.value >= func.instructions.size()) continue;
      auto const& inst = func.instructions[iid.value];
      std::visit(overload{
        [&](ConstI64 const&) {},
        [&](IAdd const& op)   { check_use(op.lhs, iid, "iadd lhs"); check_use(op.rhs, iid, "iadd rhs"); },
        [&](ISub const& op)   { check_use(op.lhs, iid, "isub lhs"); check_use(op.rhs, iid, "isub rhs"); },
        [&](IMul const& op)   { check_use(op.lhs, iid, "imul lhs"); check_use(op.rhs, iid, "imul rhs"); },
        [&](IDiv const& op)   { check_use(op.lhs, iid, "idiv lhs"); check_use(op.rhs, iid, "idiv rhs"); },
        [&](IRem const& op)   { check_use(op.lhs, iid, "irem lhs"); check_use(op.rhs, iid, "irem rhs"); },
        [&](ICmp const& op)   { check_use(op.lhs, iid, "icmp lhs"); check_use(op.rhs, iid, "icmp rhs"); },
        [&](Call const& op)   { for (auto a : op.args) check_use(a, iid, "call arg"); },
      }, inst.payload);
    }
  }
}

} // namespace detail

// ═══════════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════════

auto verify(Module const& mod) -> VerifyResult {
  std::vector<VerifyError> errors;
  detail::verify_calls(mod, errors);
  for (size_t fi = 0; fi < mod.functions.size(); ++fi) {
    FunctionId fid{static_cast<uint32_t>(fi)};
    detail::FunctionVerifier fv(mod.functions[fi], fid, errors);
    fv.verify();
  }
  return VerifyResult{std::move(errors)};
}

} // namespace mljit::ir::verifier
