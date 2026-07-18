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

// Compile a single function to executable machine code by direct emission:
// run the register-allocation pipeline, then fold over the SSA in linear order,
// streaming x64 into a code buffer.  (v1: single first-order function, no
// spilling; control flow and calls arrive in later slices.)
[[nodiscard]] auto compile(ir::Function const& fn) -> x64::ExecBuffer;

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

void emit_instruction(x64::Assembler& a, regalloc::Allocation const& alloc,
                      ir::Instruction const& inst) {
  x64::Gpr const dst = reg_of(alloc, inst.result_id);
  std::visit(ir::overload{
    [&](ir::ConstI64 const& op) { a.mov_ri(dst, op.value); },
    [&](ir::IAdd const& op) {
      emit_commutative(a, [&](x64::Gpr d, x64::Gpr s) { a.add_rr(d, s); },
                       dst, reg_of(alloc, op.lhs), reg_of(alloc, op.rhs));
    },
    [&](ir::IMul const& op) {
      emit_commutative(a, [&](x64::Gpr d, x64::Gpr s) { a.imul_rr(d, s); },
                       dst, reg_of(alloc, op.lhs), reg_of(alloc, op.rhs));
    },
    [&](auto const&) { assert(false && "instruction not yet lowered (later slice)"); },
  }, inst.payload);
}

}  // namespace

auto compile(ir::Function const& fn) -> x64::ExecBuffer {
  auto const n     = regalloc::compute_numbering(fn);
  auto const iv    = regalloc::build_intervals(fn, n);
  auto const alloc = regalloc::allocate(fn, n, iv);

  x64::Assembler a;

  // ── Prologue ──
  a.push(x64::Gpr::rbp);
  a.mov_rr(x64::Gpr::rbp, x64::Gpr::rsp);

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

  // ── Body (linear order) ──
  for (auto bid : n.order) {
    auto const& blk = fn.blocks[bid.value];
    for (auto iid : blk.instructions)
      emit_instruction(a, alloc, fn.instructions[iid.value]);

    assert(blk.terminator && "block without terminator");
    std::visit(ir::overload{
      [&](ir::Ret const& r) {
        if (reg_of(alloc, r.value) != x64::Gpr::rax)
          a.mov_rr(x64::Gpr::rax, reg_of(alloc, r.value));
        a.pop(x64::Gpr::rbp);   // ── Epilogue ──
        a.ret();
      },
      [&](auto const&) { assert(false && "terminator not yet lowered (later slice)"); },
    }, *blk.terminator);
  }

  a.finalize();
  return a.executable_copy();
}

}  // namespace mljit::codegen
