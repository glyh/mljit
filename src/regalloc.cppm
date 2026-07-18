module;

#include <cstdint>
#include <vector>
#include <string>
#include <format>
#include <variant>
#include <limits>

export module mljit.regalloc;

import mljit.ir;

export namespace mljit::regalloc {

// ── Program-point numbering ───────────────────────────────────
//
// The allocator lays the function's blocks out in one linear order (reverse
// postorder) and numbers every program point with an even integer.  Within a
// block the layout is:
//
//   from      : block-entry slot   (block parameters are "defined" here)
//   from+2    : first instruction
//   ...       : subsequent instructions
//   term_pos  : the terminator
//   to        : one past the terminator  (half-open block end)
//
// Even numbering leaves the odd slots between program points free for later
// insertion of resolution moves.
using Pos = std::uint32_t;

inline constexpr std::uint32_t kNoIndex = std::numeric_limits<std::uint32_t>::max();

struct BlockNumbering {
  ir::BlockId block;
  Pos from = 0;   // block-entry position (params defined here)
  Pos to   = 0;   // one past the terminator (half-open)
};

struct Numbering {
  std::vector<ir::BlockId>     order;              // blocks in reverse postorder
  std::vector<BlockNumbering>  blocks;             // parallel to `order`
  std::vector<Pos>             inst_pos;           // indexed by InstructionId.value
  std::vector<Pos>             term_pos;           // indexed by BlockId.value
  std::vector<std::uint32_t>   block_order_index;  // BlockId.value -> index in `order`, or kNoIndex
};

[[nodiscard]] auto compute_numbering(ir::Function const& fn) -> Numbering;
[[nodiscard]] auto dump_numbering(ir::Function const& fn, Numbering const& n) -> std::string;

}  // namespace mljit::regalloc

// ═══════════════════════════════════════════════════════════════
//  Implementation
// ═══════════════════════════════════════════════════════════════

namespace mljit::regalloc {

namespace {

// Control-flow successors of a block, in a stable order.
auto successors(ir::Block const& b) -> std::vector<ir::BlockId> {
  std::vector<ir::BlockId> out;
  if (!b.terminator) return out;
  std::visit(ir::overload{
    [&](ir::Ret const&) {},
    [&](ir::Jump const& j) { out.push_back(j.target); },
    [&](ir::Branch const& br) {
      out.push_back(br.true_block);
      out.push_back(br.false_block);
    },
  }, *b.terminator);
  return out;
}

auto value_name(ir::ValueId id, ir::Function const& fn) -> std::string {
  for (auto const& blk : fn.blocks)
    for (auto const& p : blk.params)
      if (p.id == id && p.debug_name) return *p.debug_name;
  for (auto const& inst : fn.instructions)
    if (inst.result_id == id && inst.debug_name) return *inst.debug_name;
  return "v" + std::to_string(id.value);
}

auto inst_kind(ir::InstPayload const& p) -> std::string_view {
  return std::visit(ir::overload{
    [](ir::ConstI64 const&) -> std::string_view { return "const_i64"; },
    [](ir::IAdd const&)     -> std::string_view { return "iadd"; },
    [](ir::ISub const&)     -> std::string_view { return "isub"; },
    [](ir::IMul const&)     -> std::string_view { return "imul"; },
    [](ir::IDiv const&)     -> std::string_view { return "idiv"; },
    [](ir::IRem const&)     -> std::string_view { return "irem"; },
    [](ir::ICmp const&)     -> std::string_view { return "icmp"; },
    [](ir::Call const&)     -> std::string_view { return "call"; },
  }, p);
}

auto term_kind(ir::TermPayload const& t) -> std::string_view {
  return std::visit(ir::overload{
    [](ir::Ret const&)    -> std::string_view { return "ret"; },
    [](ir::Jump const&)   -> std::string_view { return "jump"; },
    [](ir::Branch const&) -> std::string_view { return "branch"; },
  }, t);
}

auto block_label(ir::Block const& b, ir::BlockId id) -> std::string {
  if (b.debug_name) return *b.debug_name;
  return "bb" + std::to_string(id.value);
}

}  // namespace

auto compute_numbering(ir::Function const& fn) -> Numbering {
  Numbering n;
  auto const nblocks = fn.blocks.size();

  // Reverse postorder via a postorder DFS from the entry block.
  std::vector<bool> visited(nblocks, false);
  std::vector<ir::BlockId> postorder;
  auto dfs = [&](auto&& self, ir::BlockId b) -> void {
    if (visited[b.value]) return;
    visited[b.value] = true;
    for (auto s : successors(fn.blocks[b.value])) self(self, s);
    postorder.push_back(b);
  };
  if (nblocks > 0) dfs(dfs, ir::BlockId{0});
  n.order.assign(postorder.rbegin(), postorder.rend());

  // Assign positions.
  n.inst_pos.assign(fn.instructions.size(), 0);
  n.term_pos.assign(nblocks, 0);
  n.block_order_index.assign(nblocks, kNoIndex);

  Pos pos = 0;
  for (std::uint32_t idx = 0; idx < n.order.size(); ++idx) {
    auto const bid = n.order[idx];
    n.block_order_index[bid.value] = idx;
    auto const& blk = fn.blocks[bid.value];

    BlockNumbering bn;
    bn.block = bid;
    bn.from = pos;
    pos += 2;                       // block-entry / params slot
    for (auto iid : blk.instructions) {
      n.inst_pos[iid.value] = pos;
      pos += 2;
    }
    n.term_pos[bid.value] = pos;
    pos += 2;                       // terminator slot
    bn.to = pos;
    n.blocks.push_back(bn);
  }
  return n;
}

auto dump_numbering(ir::Function const& fn, Numbering const& n) -> std::string {
  std::string out;
  for (auto const& bn : n.blocks) {
    auto const& blk = fn.blocks[bn.block.value];
    out += std::format("^{} [{}..{}):\n",
                       block_label(blk, bn.block), bn.from, bn.to);

    // block-entry with parameter names
    out += std::format("  {}: block-entry(", bn.from);
    for (std::size_t i = 0; i < blk.params.size(); ++i) {
      if (i > 0) out += ", ";
      out += value_name(blk.params[i].id, fn);
    }
    out += ")\n";

    for (auto iid : blk.instructions) {
      auto const& inst = fn.instructions[iid.value];
      out += std::format("  {}: {} = {}\n",
                         n.inst_pos[iid.value],
                         value_name(inst.result_id, fn),
                         inst_kind(inst.payload));
    }

    if (blk.terminator)
      out += std::format("  {}: term {}\n",
                         n.term_pos[bn.block.value], term_kind(*blk.terminator));
  }
  return out;
}

}  // namespace mljit::regalloc
