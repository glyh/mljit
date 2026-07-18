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
// streaming x64 into a code buffer.  (v1: no spilling; only self-recursive
// calls — cross-function calls need resolved addresses, a later step.)
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

// Callee-saved allocatable registers: must be preserved across a call, so any
// we use are saved in the prologue and restored in the epilogue.
constexpr std::array<x64::Gpr, 5> kCalleeSaved = {
  x64::Gpr::rbx, x64::Gpr::r12, x64::Gpr::r13, x64::Gpr::r14, x64::Gpr::r15,
};

auto reg_of(regalloc::Allocation const& alloc, ir::ValueId v) -> x64::Gpr {
  auto const& loc = alloc.value_locs[v.value];
  assert(loc && "value has no location");
  assert(loc->kind == regalloc::LocKind::Reg && "spilled values not yet emitted");
  return loc->reg;
}

void emit_move(x64::Assembler& a, regalloc::Move const& m) {
  assert(m.dst.kind == regalloc::LocKind::Reg && m.src.kind == regalloc::LocKind::Reg
         && "memory moves not yet emitted");
  if (m.op == regalloc::MoveOp::Mov) a.mov_rr(m.dst.reg, m.src.reg);
  else                               a.xchg_rr(m.dst.reg, m.src.reg);
}

// dst = lhs <op> rhs for a commutative op, lowered to two-address x86.
template <typename EmitOp>
void emit_commutative(x64::Assembler& a, EmitOp op,
                      x64::Gpr dst, x64::Gpr lhs, x64::Gpr rhs) {
  if (dst == lhs) {
    op(dst, rhs);
  } else if (dst == rhs) {
    op(dst, lhs);
  } else {
    a.mov_rr(dst, lhs);
    op(dst, rhs);
  }
}

// idiv/irem: dividend in rax, sign-extended into rdx:rax by cqo, divisor in a
// register the fixed intervals kept out of rax/rdx.  Quotient -> rax, rem -> rdx.
void emit_div(x64::Assembler& a, regalloc::Allocation const& alloc,
              ir::ValueId result, ir::ValueId lhs, ir::ValueId rhs, bool want_rem) {
  if (reg_of(alloc, lhs) != x64::Gpr::rax) a.mov_rr(x64::Gpr::rax, reg_of(alloc, lhs));
  a.cqo();
  a.idiv_r(reg_of(alloc, rhs));
  x64::Gpr const dst = reg_of(alloc, result);
  x64::Gpr const src = want_rem ? x64::Gpr::rdx : x64::Gpr::rax;
  if (dst != src) a.mov_rr(dst, src);
}

void emit_instruction(x64::Assembler& a, regalloc::Allocation const& alloc,
                      ir::Instruction const& inst) {
  std::visit(ir::overload{
    [&](ir::ConstI64 const& op) { a.mov_ri(reg_of(alloc, inst.result_id), op.value); },
    [&](ir::IAdd const& op) {
      emit_commutative(a, [&](x64::Gpr d, x64::Gpr s) { a.add_rr(d, s); },
                       reg_of(alloc, inst.result_id), reg_of(alloc, op.lhs), reg_of(alloc, op.rhs));
    },
    [&](ir::IMul const& op) {
      emit_commutative(a, [&](x64::Gpr d, x64::Gpr s) { a.imul_rr(d, s); },
                       reg_of(alloc, inst.result_id), reg_of(alloc, op.lhs), reg_of(alloc, op.rhs));
    },
    [&](ir::ISub const& op) {
      x64::Gpr const dst = reg_of(alloc, inst.result_id);
      x64::Gpr const lhs = reg_of(alloc, op.lhs);
      x64::Gpr const rhs = reg_of(alloc, op.rhs);
      if (dst == lhs) {
        a.sub_rr(dst, rhs);              // dst = lhs - rhs
      } else if (dst == rhs) {
        a.sub_rr(dst, lhs);              // dst = rhs - lhs
        a.neg_r(dst);                    // dst = lhs - rhs
      } else {
        a.mov_rr(dst, lhs);
        a.sub_rr(dst, rhs);
      }
    },
    [&](ir::IDiv const& op) { emit_div(a, alloc, inst.result_id, op.lhs, op.rhs, false); },
    [&](ir::IRem const& op) { emit_div(a, alloc, inst.result_id, op.lhs, op.rhs, true); },
    // icmp feeds a branch: emit the compare here (it is the block's last
    // instruction, so flags stay fresh for the jCC), and fuse the jump at the
    // terminator. i1-used-as-a-value is guarded against below.
    [&](ir::ICmp const& op) { a.cmp_rr(reg_of(alloc, op.lhs), reg_of(alloc, op.rhs)); },
    [&](auto const&) { assert(false && "instruction not yet lowered (later slice)"); },
  }, inst.payload);
}

// Self-recursive call: move args into ABI argument registers (a parallel move),
// call the function's entry, take the result from rax. Values live across the
// call already sit in callee-saved registers (the call-clobber fixed intervals
// keep them out of caller-saved), so moving args into rdi/rsi is safe.
void emit_call(x64::Assembler& a, regalloc::Allocation const& alloc,
               ir::FunctionId self, x64::Label entry_label,
               ir::Instruction const& inst, ir::Call const& call) {
  assert(call.callee == self && "cross-function calls not yet supported");
  std::vector<std::pair<regalloc::Location, regalloc::Location>> pm;
  for (std::size_t i = 0; i < call.args.size(); ++i) {
    regalloc::Location const dst{regalloc::LocKind::Reg, kArgRegs[i], 0};
    pm.emplace_back(dst, *alloc.value_locs[call.args[i].value]);
  }
  for (auto const& m : regalloc::sequentialize_parallel_moves(pm)) emit_move(a, m);
  a.call(entry_label);
  x64::Gpr const dst = reg_of(alloc, inst.result_id);
  if (dst != x64::Gpr::rax) a.mov_rr(dst, x64::Gpr::rax);
}

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

// Assert that no icmp result is used as a value (only fused into branches),
// since the emitter lowers icmp to a bare compare with no setcc materialization.
void guard_no_i1_values(ir::Function const& fn,
                        std::vector<ir::Instruction const*> const& def) {
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
  std::vector<ir::Instruction const*> def(fn.next_value_id, nullptr);
  for (auto const& inst : fn.instructions) def[inst.result_id.value] = &inst;
  guard_no_i1_values(fn, def);

  // Callee-saved registers actually used -> saved in prologue / restored in
  // epilogue. Keep rsp 16-byte aligned at calls: push rbp + N callee-saved
  // leaves rsp aligned iff N is even, so pad by 8 when N is odd.
  std::vector<x64::Gpr> saved;
  for (auto r : kCalleeSaved) {
    for (auto const& loc : alloc.value_locs)
      if (loc && loc->kind == regalloc::LocKind::Reg && loc->reg == r) { saved.push_back(r); break; }
  }
  bool const pad = (saved.size() % 2) == 1;

  x64::Assembler a;

  x64::Label func_entry = a.new_label();  // recursion target (start of prologue)

  // A label per block, plus one trampoline label per resolution edge (edges
  // that carry moves; a critical edge's trampoline is its split block).
  std::vector<x64::Label> block_label;
  block_label.reserve(fn.blocks.size());
  for (std::size_t i = 0; i < fn.blocks.size(); ++i) block_label.push_back(a.new_label());

  std::vector<x64::Label> edge_label;    // parallel to res.edges
  edge_label.reserve(res.edges.size());
  for (std::size_t i = 0; i < res.edges.size(); ++i) edge_label.push_back(a.new_label());

  // Where a jump/branch to `to` from `from` should land: the edge's trampoline
  // if it carries moves, else the target block directly.
  auto target_label = [&](ir::BlockId from, ir::BlockId to) -> x64::Label {
    for (std::size_t i = 0; i < res.edges.size(); ++i)
      if (res.edges[i].from == from && res.edges[i].to == to) return edge_label[i];
    return block_label[to.value];
  };

  // ── Prologue ──
  a.bind(func_entry);
  a.push(x64::Gpr::rbp);
  a.mov_rr(x64::Gpr::rbp, x64::Gpr::rsp);
  for (auto r : saved) a.push(r);
  if (pad) a.sub_ri(x64::Gpr::rsp, 8);

  // Move incoming ABI argument registers into the entry params' locations
  // (a parallel assignment: sequentialize so nothing is clobbered early).
  auto const& entry = fn.blocks[0];
  std::vector<std::pair<regalloc::Location, regalloc::Location>> pin;
  for (std::size_t i = 0; i < entry.params.size(); ++i) {
    regalloc::Location const dst = *alloc.value_locs[entry.params[i].id.value];
    regalloc::Location const src{regalloc::LocKind::Reg, kArgRegs[i], 0};
    pin.emplace_back(dst, src);
  }
  for (auto const& m : regalloc::sequentialize_parallel_moves(pin)) emit_move(a, m);

  // ── Epilogue ── (mirror of the prologue)
  auto emit_epilogue = [&] {
    if (pad) a.add_ri(x64::Gpr::rsp, 8);
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) a.pop(*it);
    a.pop(x64::Gpr::rbp);
  };

  // ── Body (linear order) ──
  for (auto bid : n.order) {
    auto const& blk = fn.blocks[bid.value];
    a.bind(block_label[bid.value]);

    for (auto iid : blk.instructions) {
      auto const& inst = fn.instructions[iid.value];
      if (auto const* c = std::get_if<ir::Call>(&inst.payload))
        emit_call(a, alloc, fid, func_entry, inst, *c);
      else
        emit_instruction(a, alloc, inst);
    }

    assert(blk.terminator && "block without terminator");
    std::visit(ir::overload{
      [&](ir::Ret const& t) {
        if (reg_of(alloc, t.value) != x64::Gpr::rax)
          a.mov_rr(x64::Gpr::rax, reg_of(alloc, t.value));
        emit_epilogue();
        a.ret();
      },
      [&](ir::Jump const& t) {
        a.jmp(target_label(bid, t.target));
      },
      [&](ir::Branch const& t) {
        // c is defined by an icmp (whose compare was emitted just above, as the
        // block's last instruction). Jump to the true edge when it holds, else
        // fall through to an unconditional jump to the false edge.
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
    for (auto const& m : res.edges[i].moves) emit_move(a, m);
    a.jmp(block_label[res.edges[i].to.value]);
  }

  a.finalize();
  return a.executable_copy();
}

}  // namespace mljit::codegen
