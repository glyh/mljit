module;

#include <string>
#include <string_view>
#include <vector>
#include <format>
#include <variant>

export module mljit.ir.printer;

import mljit.ir;

// ── Internal helpers (module-internal linkage) ─────────────
namespace {

using namespace mljit::ir;

/// Build a vector indexed by ValueId that maps each value to its printable name.
auto build_value_names(Function const& func) -> std::vector<std::string> {
  std::vector<std::string> names(func.next_value_id);
  for (auto const& blk : func.blocks) {
    for (auto const& param : blk.params) {
      if (param.debug_name)
        names[param.id.value] = *param.debug_name;
      else
        names[param.id.value] = "v" + std::to_string(param.id.value);
    }
  }
  for (auto const& inst : func.instructions) {
    if (inst.debug_name)
      names[inst.result_id.value] = *inst.debug_name;
    else
      names[inst.result_id.value] = "v" + std::to_string(inst.result_id.value);
  }
  return names;
}

auto block_label(Block const& blk, BlockId id) -> std::string {
  if (blk.debug_name) return *blk.debug_name;
  return "bb" + std::to_string(id.value);
}

auto func_label(Function const& func, size_t index) -> std::string {
  if (func.debug_name) return *func.debug_name;
  return "f" + std::to_string(index);
}

auto value_label(ValueId id, std::vector<std::string> const& names) -> std::string {
  if (id.value < names.size() && !names[id.value].empty())
    return names[id.value];
  return "v" + std::to_string(id.value);
}

auto fmt_inst_op(
  InstPayload const& payload,
  std::vector<std::string> const& names,
  Module const& mod
) -> std::string {
  return std::visit(overload{
    [&](ConstI64 const& op) -> std::string {
      return std::format("const_i64 {}", op.value);
    },
    [&](IAdd const& op) -> std::string {
      return std::format("iadd {}, {}", value_label(op.lhs, names), value_label(op.rhs, names));
    },
    [&](ISub const& op) -> std::string {
      return std::format("isub {}, {}", value_label(op.lhs, names), value_label(op.rhs, names));
    },
    [&](IMul const& op) -> std::string {
      return std::format("imul {}, {}", value_label(op.lhs, names), value_label(op.rhs, names));
    },
    [&](ICmp const& op) -> std::string {
      return std::format("icmp {} {}, {}",
        to_string(op.cond),
        value_label(op.lhs, names),
        value_label(op.rhs, names));
    },
    [&](Call const& op) -> std::string {
      std::string r = "call @"
        + func_label(mod.functions[op.callee.value], op.callee.value) + "(";
      for (size_t i = 0; i < op.args.size(); ++i) {
        if (i > 0) r += ", ";
        r += value_label(op.args[i], names);
      }
      r += ")";
      return r;
    },
  }, payload);
}

auto fmt_term(
  TermPayload const& term,
  std::vector<std::string> const& names,
  Function const& func
) -> std::string {
  return std::visit(overload{
    [&](Ret const& op) -> std::string {
      return "ret " + value_label(op.value, names);
    },
    [&](Jump const& op) -> std::string {
      std::string r = "jump ^" + block_label(func.blocks[op.target.value], op.target) + "(";
      for (size_t i = 0; i < op.args.size(); ++i) {
        if (i > 0) r += ", ";
        r += value_label(op.args[i], names);
      }
      r += ")";
      return r;
    },
    [&](Branch const& op) -> std::string {
      std::string r = "branch " + value_label(op.cond, names) + ", ^"
        + block_label(func.blocks[op.true_block.value], op.true_block) + "(";
      for (size_t i = 0; i < op.true_args.size(); ++i) {
        if (i > 0) r += ", ";
        r += value_label(op.true_args[i], names);
      }
      r += "), ^"
        + block_label(func.blocks[op.false_block.value], op.false_block) + "(";
      for (size_t i = 0; i < op.false_args.size(); ++i) {
        if (i > 0) r += ", ";
        r += value_label(op.false_args[i], names);
      }
      r += ")";
      return r;
    },
  }, term);
}

} // anonymous namespace

// ── Public API ─────────────────────────────────────────────

export namespace mljit::ir::printer {

auto to_text(Module const& mod) -> std::string {
  std::string out;

  for (size_t fi = 0; fi < mod.functions.size(); ++fi) {
    auto const& func    = mod.functions[fi];
    auto        names   = build_value_names(func);
    auto const& entry   = func.blocks[0];

    // Function header
    out += "func @" + func_label(func, fi) + "(";
    for (size_t i = 0; i < entry.params.size(); ++i) {
      if (i > 0) out += ", ";
      out += to_string(entry.params[i].type);
    }
    out += ") -> " + std::string(to_string(func.return_type)) + " {\n";

    // Blocks
    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
      auto const& blk = func.blocks[bi];
      BlockId bid{static_cast<uint32_t>(bi)};

      // Block label
      out += "^" + block_label(blk, bid) + "(";
      for (size_t pi = 0; pi < blk.params.size(); ++pi) {
        if (pi > 0) out += ", ";
        out += value_label(blk.params[pi].id, names) + ": " + to_string(blk.params[pi].type);
      }
      out += "):\n";

      // Instructions
      for (auto iid : blk.instructions) {
        auto const& inst = func.instructions[iid.value];
        out += "  " + value_label(inst.result_id, names) + ": " + to_string(inst.type)
             + " = " + fmt_inst_op(inst.payload, names, mod) + "\n";
      }

      // Terminator (print nothing when unset — kept for future verifier)
      if (blk.terminator.has_value())
        out += "  " + fmt_term(*blk.terminator, names, func) + "\n";
    }

    out += "}\n";
  }

  return out;
}

} // namespace mljit::ir::printer
