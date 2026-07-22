module;

#include <cstdint>
#include <vector>
#include <array>
#include <utility>
#include <variant>
#include <cassert>

export module mljit.codegen;

import mljit.ir;
import mljit.x64;
import mljit.regalloc;

export namespace mljit::codegen {

// Compile one function to executable machine code by direct emission: run the
// register-allocation pipeline, then fold over the SSA in linear order,
// streaming x64 into a code buffer.  (v1: only self-recursive calls —
// cross-function calls need resolved addresses, a later step.)
[[nodiscard]] auto compile(ir::Module const& mod, ir::FunctionId fid) -> x64::ExecBuffer;

}  // namespace mljit::codegen

// ═══════════════════════════════════════════════════════════════
//  Implementation
// ═══════════════════════════════════════════════════════════════

namespace mljit::codegen {

namespace {

// System V AMD64 integer argument registers.
constexpr std::array<x64::Gpr, 6> kArgRegs = {
  x64::Gpr::rdi, x64::Gpr::rsi, x64::Gpr::rdx,
  x64::Gpr::rcx, x64::Gpr::r8,  x64::Gpr::r9,
};

// Callee-saved allocatable registers: preserved across a call, so any we use
// are saved in the prologue and restored in the epilogue.
constexpr std::array<x64::Gpr, 5> kCalleeSaved = {
  x64::Gpr::rbx, x64::Gpr::r12, x64::Gpr::r13, x64::Gpr::r14, x64::Gpr::r15,
};

// Reserved (never allocated) scratch registers for reloading spilled operands.
constexpr x64::Gpr kScratch1 = x64::Gpr::r10;
constexpr x64::Gpr kScratch2 = x64::Gpr::r11;

// Streams x64 for one function, resolving each value through the allocation and
// reloading spilled operands through the scratch registers.
struct Emitter {
  x64::Assembler&              a;
  regalloc::Allocation const&  alloc;
  std::uint32_t                num_saved;   // callee-saved slots above the spills

  auto loc(ir::ValueId v) const -> regalloc::Location { return *alloc.value_locs[v]; }

  // Stack address of a spill slot: below the saved callee-saved registers.
  auto slot_mem(regalloc::SpillSlot s) const -> x64::Mem {
    return x64::Mem{x64::Gpr::rbp, -static_cast<std::int32_t>(8 * (num_saved + s.index + 1))};
  }

  // Value into a register: its own if in one, else loaded into `scratch`.
  auto use(ir::ValueId v, x64::Gpr scratch) -> x64::Gpr {
    return std::visit(ir::overload{
      [&](x64::Gpr r) -> x64::Gpr { return r; },
      [&](regalloc::SpillSlot s) -> x64::Gpr { a.mov_rm(scratch, slot_mem(s)); return scratch; },
    }, loc(v));
  }

  // Register to compute a result into: its own, or `scratch` if spilled.
  auto dest(ir::ValueId v, x64::Gpr scratch) -> x64::Gpr {
    return std::visit(ir::overload{
      [&](x64::Gpr r) -> x64::Gpr { return r; },
      [&](regalloc::SpillSlot) -> x64::Gpr { return scratch; },
    }, loc(v));
  }

  // Store a just-computed result back to its slot if spilled.
  void store(ir::ValueId v, x64::Gpr reg) {
    std::visit(ir::overload{
      [&](x64::Gpr) {},
      [&](regalloc::SpillSlot s) { a.mov_mr(slot_mem(s), reg); },
    }, loc(v));
  }

  // Emit one resolution/parallel move.  The outer visit picks copy vs. swap;
  // the inner visit dispatches on *both* operands at once, so every
  // register/slot combination is handled exhaustively.
  void move(regalloc::Move const& m) {
    using regalloc::SpillSlot;
    std::visit(ir::overload{
      [&](regalloc::MovOp const& mv) {
        std::visit(ir::overload{
          [&](x64::Gpr d,  x64::Gpr s)  { a.mov_rr(d, s); },
          [&](x64::Gpr d,  SpillSlot s) { a.mov_rm(d, slot_mem(s)); },
          [&](SpillSlot d, x64::Gpr s)  { a.mov_mr(slot_mem(d), s); },
          [&](SpillSlot d, SpillSlot s) { a.mov_rm(kScratch1, slot_mem(s));
                                          a.mov_mr(slot_mem(d), kScratch1); },
        }, mv.dst, mv.src);
      },
      [&](regalloc::XchgOp const& xc) {
        std::visit(ir::overload{
          [&](x64::Gpr a1, x64::Gpr b1) { a.xchg_rr(a1, b1); },
          [&](x64::Gpr a1, SpillSlot b1){ swap_reg_slot(a1, b1); },
          [&](SpillSlot a1, x64::Gpr b1){ swap_reg_slot(b1, a1); },
          [&](SpillSlot a1, SpillSlot b1){ a.mov_rm(kScratch1, slot_mem(a1));
                                           a.mov_rm(kScratch2, slot_mem(b1));
                                           a.mov_mr(slot_mem(a1), kScratch2);
                                           a.mov_mr(slot_mem(b1), kScratch1); },
        }, xc.a, xc.b);
      },
    }, m);
  }

  void swap_reg_slot(x64::Gpr reg, regalloc::SpillSlot slot) {
    a.mov_rm(kScratch1, slot_mem(slot));
    a.mov_mr(slot_mem(slot), reg);
    a.mov_rr(reg, kScratch1);
  }

  template <typename Op>
  void binary_commutative(ir::ValueId res, ir::ValueId lhs, ir::ValueId rhs, Op op) {
    x64::Gpr const l = use(lhs, kScratch1);
    x64::Gpr const r = use(rhs, kScratch2);
    x64::Gpr const d = dest(res, kScratch1);
    if      (d == l) op(d, r);
    else if (d == r) op(d, l);
    else { a.mov_rr(d, l); op(d, r); }
    store(res, d);
  }

  void sub(ir::ValueId res, ir::ValueId lhs, ir::ValueId rhs) {
    x64::Gpr const l = use(lhs, kScratch1);
    x64::Gpr const r = use(rhs, kScratch2);
    x64::Gpr const d = dest(res, kScratch1);
    if      (d == l) a.sub_rr(d, r);
    else if (d == r) { a.sub_rr(d, l); a.neg_r(d); }
    else { a.mov_rr(d, l); a.sub_rr(d, r); }
    store(res, d);
  }

  void div(ir::ValueId res, ir::ValueId lhs, ir::ValueId rhs, bool want_rem) {
    x64::Gpr const l = use(lhs, kScratch1);
    if (l != x64::Gpr::rax) a.mov_rr(x64::Gpr::rax, l);
    x64::Gpr const r = use(rhs, kScratch2);   // never rax/rdx (fixed intervals)
    a.cqo();
    a.idiv_r(r);
    x64::Gpr const src = want_rem ? x64::Gpr::rdx : x64::Gpr::rax;
    x64::Gpr const d = dest(res, kScratch1);
    if (d != src) a.mov_rr(d, src);
    store(res, d);
  }

  void instruction(ir::Instruction const& inst) {
    std::visit(ir::overload{
      [&](ir::ConstI64 const& op) {
        x64::Gpr const d = dest(inst.result_id, kScratch1);
        a.mov_ri(d, op.value);
        store(inst.result_id, d);
      },
      [&](ir::IAdd const& op) { binary_commutative(inst.result_id, op.lhs, op.rhs,
                                  [&](x64::Gpr d, x64::Gpr s) { a.add_rr(d, s); }); },
      [&](ir::IMul const& op) { binary_commutative(inst.result_id, op.lhs, op.rhs,
                                  [&](x64::Gpr d, x64::Gpr s) { a.imul_rr(d, s); }); },
      [&](ir::ISub const& op) { sub(inst.result_id, op.lhs, op.rhs); },
      [&](ir::IDiv const& op) { div(inst.result_id, op.lhs, op.rhs, false); },
      [&](ir::IRem const& op) { div(inst.result_id, op.lhs, op.rhs, true); },
      // icmp feeds a branch: emit the compare here (the block's last
      // instruction, so flags stay fresh) and fuse the jCC at the terminator.
      [&](ir::ICmp const& op) {
        x64::Gpr const l = use(op.lhs, kScratch1);
        x64::Gpr const r = use(op.rhs, kScratch2);
        a.cmp_rr(l, r);
      },
      [&](ir::Call const&) { assert(false && "call handled separately"); },
    }, inst.payload);
  }

  // Self-recursive call: args into ABI registers, call the entry, result from
  // rax. Values live across the call sit in callee-saved registers or slots
  // (both survive the call), kept out of caller-saved by fixed intervals.
  void call(ir::FunctionId self, x64::Label entry_label,
            ir::Instruction const& inst, ir::Call const& c) {
    assert(c.callee == self && "cross-function calls not yet supported");
    std::vector<std::pair<regalloc::Location, regalloc::Location>> pm;
    for (std::size_t i = 0; i < c.args.size(); ++i)
      pm.emplace_back(regalloc::Location{kArgRegs[i]}, loc(c.args[i]));
    for (auto const& m : regalloc::sequentialize_parallel_moves(pm)) move(m);
    a.call(entry_label);
    x64::Gpr const d = dest(inst.result_id, kScratch1);
    if (d != x64::Gpr::rax) a.mov_rr(d, x64::Gpr::rax);
    store(inst.result_id, d);
  }
};

void emit_jcc(x64::Assembler& a, ir::IcmpCond cond, x64::Label target) {
  switch (cond) {
    case ir::IcmpCond::eq:  a.je(target);  break;
    case ir::IcmpCond::ne:  a.jne(target); break;
    case ir::IcmpCond::slt: a.jl(target);  break;
    case ir::IcmpCond::sle: a.jle(target); break;
    case ir::IcmpCond::sgt: a.jg(target);  break;
    case ir::IcmpCond::sge: a.jge(target); break;
  }
}

// Maps a ValueId to its defining instruction, or nullptr for a block parameter.
// A nullable *non-owning* observer — the idiomatic use of a raw pointer (the
// instructions are owned by the Function, not here).
using DefMap = std::vector<ir::Instruction const*>;

// Assert that no icmp result is used as a value (only fused into branches),
// since the emitter lowers icmp to a bare compare with no setcc materialization.
void guard_no_i1_values(ir::Function const& fn, DefMap const& def) {
  auto is_icmp = [&](ir::ValueId v) {
    auto const* d = def[v.value];
    return d && std::holds_alternative<ir::ICmp>(d->payload);
  };
  for (auto const& inst : fn.instructions)
    std::visit(ir::overload{
      [&](ir::IAdd const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](ir::ISub const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](ir::IMul const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](ir::IDiv const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](ir::IRem const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](ir::ICmp const& o) { assert(!is_icmp(o.lhs) && !is_icmp(o.rhs)); },
      [&](auto const&) {},
    }, inst.payload);
  for (auto const& blk : fn.blocks) {
    if (!blk.terminator) continue;
    std::visit(ir::overload{
      [&](ir::Ret const& t) { assert(!is_icmp(t.value)); },
      [&](ir::Jump const& t) { for (auto v : t.args) assert(!is_icmp(v)); },
      [&](ir::Branch const& t) {
        for (auto v : t.true_args)  assert(!is_icmp(v));
        for (auto v : t.false_args) assert(!is_icmp(v));
      },
    }, *blk.terminator);
  }
  (void)is_icmp;
}

}  // namespace

auto compile(ir::Module const& mod, ir::FunctionId fid) -> x64::ExecBuffer {
  auto const& fn   = mod.functions[fid.value];
  auto const n     = regalloc::compute_numbering(fn);
  auto const iv    = regalloc::build_intervals(fn, n);
  auto const alloc = regalloc::allocate(fn, n, iv);
  auto const res   = regalloc::resolve(fn, n, alloc);

  // Defining instruction per value (for icmp/branch fusion). Params -> nullptr.
  DefMap def(fn.next_value_id, nullptr);
  for (auto const& inst : fn.instructions) def[inst.result_id.value] = &inst;
  guard_no_i1_values(fn, def);

  // Callee-saved registers actually used -> saved in the frame.
  std::vector<x64::Gpr> saved;
  for (auto r : kCalleeSaved)
    for (auto const& l : alloc.value_locs)
      if (l && *l == regalloc::Location{r}) { saved.push_back(r); break; }

  auto const num_saved = static_cast<std::uint32_t>(saved.size());
  // Frame below rbp holds the callee-saved saves then the spill slots, rounded
  // up to 16 bytes so rsp stays 16-aligned at every call.
  std::uint32_t const frame = ((8u * (num_saved + alloc.num_spill_slots)) + 15u) & ~15u;

  x64::Assembler a;
  Emitter e{a, alloc, num_saved};

  x64::Label func_entry = a.new_label();  // recursion target (start of prologue)

  std::vector<x64::Label> block_label;
  block_label.reserve(fn.blocks.size());
  for (std::size_t i = 0; i < fn.blocks.size(); ++i) block_label.push_back(a.new_label());

  std::vector<x64::Label> edge_label;     // parallel to res.edges
  edge_label.reserve(res.edges.size());
  for (std::size_t i = 0; i < res.edges.size(); ++i) edge_label.push_back(a.new_label());

  // Where a jump/branch to `to` from `from` lands: the edge's trampoline if it
  // carries moves, else the target block directly.
  auto target_label = [&](ir::BlockId from, ir::BlockId to) -> x64::Label {
    for (std::size_t i = 0; i < res.edges.size(); ++i)
      if (res.edges[i].from == from && res.edges[i].to == to) return edge_label[i];
    return block_label[to.value];
  };

  auto saved_mem = [](std::uint32_t i) {
    return x64::Mem{x64::Gpr::rbp, -static_cast<std::int32_t>(8 * (i + 1))};
  };

  // ── Prologue ──
  a.bind(func_entry);
  a.push(x64::Gpr::rbp);
  a.mov_rr(x64::Gpr::rbp, x64::Gpr::rsp);
  if (frame > 0) a.sub_ri(x64::Gpr::rsp, static_cast<std::int32_t>(frame));
  for (std::uint32_t i = 0; i < num_saved; ++i) a.mov_mr(saved_mem(i), saved[i]);

  // Move incoming ABI argument registers into the entry params' locations.
  auto const& entry = fn.blocks[0];
  std::vector<std::pair<regalloc::Location, regalloc::Location>> pin;
  for (std::size_t i = 0; i < entry.params.size(); ++i)
    pin.emplace_back(*alloc.value_locs[entry.params[i].id],
                     regalloc::Location{kArgRegs[i]});
  for (auto const& m : regalloc::sequentialize_parallel_moves(pin)) e.move(m);

  // ── Epilogue ── (restore callee-saved, unwind frame)
  auto emit_epilogue = [&] {
    for (std::uint32_t i = 0; i < num_saved; ++i) a.mov_rm(saved[i], saved_mem(i));
    a.mov_rr(x64::Gpr::rsp, x64::Gpr::rbp);
    a.pop(x64::Gpr::rbp);
  };

  // ── Body (linear order) ──
  for (auto bid : n.order) {
    auto const& blk = fn.blocks[bid.value];
    a.bind(block_label[bid.value]);

    for (auto iid : blk.instructions) {
      auto const& inst = fn.instructions[iid.value];
      if (auto const* c = std::get_if<ir::Call>(&inst.payload))
        e.call(fid, func_entry, inst, *c);
      else
        e.instruction(inst);
    }

    assert(blk.terminator && "block without terminator");
    std::visit(ir::overload{
      [&](ir::Ret const& t) {
        x64::Gpr const rv = e.use(t.value, kScratch1);
        if (rv != x64::Gpr::rax) a.mov_rr(x64::Gpr::rax, rv);
        emit_epilogue();
        a.ret();
      },
      [&](ir::Jump const& t) { a.jmp(target_label(bid, t.target)); },
      [&](ir::Branch const& t) {
        auto const* d = def[t.cond.value];
        assert(d && std::holds_alternative<ir::ICmp>(d->payload) &&
               "branch condition must come from an icmp");
        assert(!blk.instructions.empty() &&
               fn.instructions[blk.instructions.back().value].result_id == t.cond &&
               "icmp feeding a branch must be the block's last instruction");
        auto const& cmp = std::get<ir::ICmp>(d->payload);
        emit_jcc(a, cmp.cond, target_label(bid, t.true_block));
        a.jmp(target_label(bid, t.false_block));
      },
    }, *blk.terminator);
  }

  // ── Edge trampolines (block-parameter moves / split blocks) ──
  for (std::size_t i = 0; i < res.edges.size(); ++i) {
    a.bind(edge_label[i]);
    for (auto const& m : res.edges[i].moves) e.move(m);
    a.jmp(block_label[res.edges[i].to.value]);
  }

  a.finalize();
  return a.executable_copy();
}

}  // namespace mljit::codegen
