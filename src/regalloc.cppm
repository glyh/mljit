module;

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <format>
#include <variant>
#include <limits>
#include <algorithm>

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
  std::vector<Pos>             block_from;         // indexed by BlockId.value
  std::vector<Pos>             block_to;           // indexed by BlockId.value
  std::vector<std::uint32_t>   block_order_index;  // BlockId.value -> index in `order`, or kNoIndex
};

[[nodiscard]] auto compute_numbering(ir::Function const& fn) -> Numbering;
[[nodiscard]] auto dump_numbering(ir::Function const& fn, Numbering const& n) -> std::string;

// ── Live intervals ────────────────────────────────────────────
//
// A value's lifetime is a sorted list of half-open [from, to) ranges.  A gap
// between two ranges is a *hole*: a stretch where the value is provably dead
// (some other value may borrow its register there).  `uses` records the
// program points at which the value is read, ascending.
struct LiveRange {
  Pos from;
  Pos to;
};

struct LiveInterval {
  ir::ValueId            value{0};
  std::vector<LiveRange> ranges;   // sorted, disjoint (holes between), ascending
  std::vector<Pos>       uses;     // ascending, unique
};

struct IntervalSet {
  // Indexed by ValueId.value; entries with empty `ranges` are never live.
  std::vector<LiveInterval> intervals;
};

[[nodiscard]] auto build_intervals(ir::Function const& fn, Numbering const& n) -> IntervalSet;
[[nodiscard]] auto dump_intervals(IntervalSet const& intervals) -> std::string;

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
  n.block_from.assign(nblocks, 0);
  n.block_to.assign(nblocks, 0);
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

    n.block_from[bid.value] = bn.from;
    n.block_to[bid.value]   = bn.to;
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

// ── Interval construction ─────────────────────────────────────

namespace {

// Value inputs read by an instruction (in a stable order).
auto inst_inputs(ir::InstPayload const& p) -> std::vector<ir::ValueId> {
  return std::visit(ir::overload{
    [](ir::ConstI64 const&) { return std::vector<ir::ValueId>{}; },
    [](ir::IAdd const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::ISub const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::IMul const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::IDiv const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::IRem const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::ICmp const& op) { return std::vector<ir::ValueId>{op.lhs, op.rhs}; },
    [](ir::Call const& op) { return op.args; },
  }, p);
}

// Value inputs read by a terminator: the branch condition (if any) plus every
// block argument passed to a successor.
auto term_inputs(ir::TermPayload const& t) -> std::vector<ir::ValueId> {
  return std::visit(ir::overload{
    [](ir::Ret const& op) { return std::vector<ir::ValueId>{op.value}; },
    [](ir::Jump const& op) { return op.args; },
    [](ir::Branch const& op) {
      std::vector<ir::ValueId> out;
      out.push_back(op.cond);
      out.insert(out.end(), op.true_args.begin(), op.true_args.end());
      out.insert(out.end(), op.false_args.begin(), op.false_args.end());
      return out;
    },
  }, t);
}

// Add a live range [from, to) to an interval, keeping ranges sorted and
// coalescing any that overlap or touch (so adjacent blocks fuse, but a real
// gap stays a hole).
void add_range(LiveInterval& iv, Pos from, Pos to) {
  if (from >= to) return;
  iv.ranges.push_back(LiveRange{from, to});
  std::sort(iv.ranges.begin(), iv.ranges.end(),
            [](LiveRange const& a, LiveRange const& b) { return a.from < b.from; });
  std::vector<LiveRange> merged;
  for (auto const& r : iv.ranges) {
    if (!merged.empty() && merged.back().to >= r.from)
      merged.back().to = std::max(merged.back().to, r.to);
    else
      merged.push_back(r);
  }
  iv.ranges = std::move(merged);
}

// Shorten the earliest range to start at `from` (the value's definition point).
// A dead definition (no range yet) gets a single-slot range.
void set_from(LiveInterval& iv, Pos from) {
  if (iv.ranges.empty()) {
    iv.ranges.push_back(LiveRange{from, from + 1});
    return;
  }
  iv.ranges.front().from = from;
}

// Loop-end position for each loop header, or 0 for non-headers.  A back edge
// b -> s (s still on the DFS stack) makes s a loop header; the loop end is the
// furthest `to` among the latches that close back to it.
auto compute_loop_ends(ir::Function const& fn, Numbering const& n) -> std::vector<Pos> {
  auto const nblocks = fn.blocks.size();
  std::vector<Pos> loop_end(nblocks, 0);
  enum class Color : std::uint8_t { White, Gray, Black };
  std::vector<Color> color(nblocks, Color::White);

  auto dfs = [&](auto&& self, ir::BlockId b) -> void {
    color[b.value] = Color::Gray;
    for (auto s : successors(fn.blocks[b.value])) {
      if (color[s.value] == Color::Gray) {
        // back edge b -> s: s is a loop header, b is a latch
        loop_end[s.value] = std::max(loop_end[s.value], n.block_to[b.value]);
      } else if (color[s.value] == Color::White) {
        self(self, s);
      }
    }
    color[b.value] = Color::Black;
  };
  if (nblocks > 0) dfs(dfs, ir::BlockId{0});
  return loop_end;
}

}  // namespace

auto build_intervals(ir::Function const& fn, Numbering const& n) -> IntervalSet {
  IntervalSet result;
  result.intervals.assign(fn.next_value_id, LiveInterval{});
  for (std::uint32_t i = 0; i < fn.next_value_id; ++i)
    result.intervals[i].value = ir::ValueId{i};

  auto const loop_end = compute_loop_ends(fn, n);

  // live_in[b] as a membership vector over value ids.
  std::vector<std::vector<std::uint8_t>> live_in(fn.blocks.size());

  auto set_add = [](std::vector<std::uint8_t>& s, ir::ValueId v) { s[v.value] = 1; };

  // Process blocks in reverse linear order.
  for (std::size_t k = n.order.size(); k-- > 0;) {
    auto const bid = n.order[k];
    auto const& blk = fn.blocks[bid.value];
    Pos const bfrom = n.block_from[bid.value];
    Pos const bto   = n.block_to[bid.value];
    Pos const tpos  = n.term_pos[bid.value];

    // live = union of successors' live-in sets.
    std::vector<std::uint8_t> live(fn.next_value_id, 0);
    for (auto s : successors(blk)) {
      auto const& sin = live_in[s.value];
      if (sin.empty()) continue;
      for (std::size_t i = 0; i < live.size(); ++i) live[i] |= sin[i];
    }

    // Everything live-out lives through the whole block (initially).
    for (std::uint32_t i = 0; i < live.size(); ++i)
      if (live[i]) add_range(result.intervals[i], bfrom, bto);

    // Terminator: reads its inputs at tpos.
    if (blk.terminator) {
      for (auto v : term_inputs(*blk.terminator)) {
        add_range(result.intervals[v.value], bfrom, tpos + 1);
        result.intervals[v.value].uses.push_back(tpos);
        set_add(live, v);
      }
    }

    // Instructions, in reverse: define result, then read inputs.
    for (std::size_t j = blk.instructions.size(); j-- > 0;) {
      auto const iid = blk.instructions[j];
      auto const& inst = fn.instructions[iid.value];
      Pos const ipos = n.inst_pos[iid.value];

      set_from(result.intervals[inst.result_id.value], ipos);
      live[inst.result_id.value] = 0;

      for (auto v : inst_inputs(inst.payload)) {
        add_range(result.intervals[v.value], bfrom, ipos + 1);
        result.intervals[v.value].uses.push_back(ipos);
        set_add(live, v);
      }
    }

    // Block parameters are defined at the block-entry slot.
    for (auto const& p : blk.params) {
      set_from(result.intervals[p.id.value], bfrom);
      live[p.id.value] = 0;
    }

    // Loop-header extension: values live into the header stay live across the
    // whole loop body (so their registers survive the back edge).
    if (loop_end[bid.value] != 0) {
      Pos const e = loop_end[bid.value];
      for (std::uint32_t i = 0; i < live.size(); ++i)
        if (live[i]) add_range(result.intervals[i], bfrom, e);
    }

    live_in[bid.value] = std::move(live);
  }

  // Finalize: sort + dedup use lists.
  for (auto& iv : result.intervals) {
    std::sort(iv.uses.begin(), iv.uses.end());
    iv.uses.erase(std::unique(iv.uses.begin(), iv.uses.end()), iv.uses.end());
  }
  return result;
}

auto dump_intervals(IntervalSet const& intervals) -> std::string {
  std::string out;
  for (std::uint32_t i = 0; i < intervals.intervals.size(); ++i) {
    auto const& iv = intervals.intervals[i];
    if (iv.ranges.empty()) continue;
    out += std::format("v{}:", i);
    for (auto const& r : iv.ranges)
      out += std::format(" [{}..{})", r.from, r.to);
    out += " uses ";
    if (iv.uses.empty()) {
      out += "-";
    } else {
      for (std::size_t u = 0; u < iv.uses.size(); ++u) {
        if (u > 0) out += ",";
        out += std::to_string(iv.uses[u]);
      }
    }
    out += "\n";
  }
  return out;
}

}  // namespace mljit::regalloc
