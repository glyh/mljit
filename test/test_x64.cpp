import mljit.x64;

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using namespace mljit::x64;

static_assert(!std::is_copy_constructible_v<ExecBuffer>);
static_assert(!std::is_copy_assignable_v<ExecBuffer>);
static_assert(std::is_move_constructible_v<ExecBuffer>);
static_assert(std::is_move_assignable_v<ExecBuffer>);

// ── Helpers ────────────────────────────────────────────────────
static auto vec(std::initializer_list<std::uint8_t> il) -> std::vector<std::uint8_t> {
  return std::vector<std::uint8_t>(il);
}

// ================================================================
//  mov_rr
// ================================================================

TEST_CASE("mov_rr base registers", "[x64][mov]") {
  Assembler a;
  a.mov_rr(Gpr::rax, Gpr::rcx);   // rax = rcx
  CHECK(a.bytes() == vec({0x48, 0x89, 0xC8}));
}

TEST_CASE("mov_rr extended registers", "[x64][mov]") {
  Assembler a;
  a.mov_rr(Gpr::r8, Gpr::r9);     // r8 = r9
  // REX.W + R + B = 0x4D, 0x89, ModRM reg=r9(1) rm=r8(0) = C8
  CHECK(a.bytes() == vec({0x4D, 0x89, 0xC8}));
}

TEST_CASE("mov_rr mixed extended", "[x64][mov]") {
  Assembler a;
  a.mov_rr(Gpr::rax, Gpr::r10);   // rax = r10
  // ModRM reg=r10(2) rm=rax(0), REX.R = 1 (r10 is extended)
  // REX = 0100_W_R_B = 0100_1_1_0 = 0x4C, 0x89, ModRM C0|(2<<3)|0 = 0xD0
  CHECK(a.bytes() == vec({0x4C, 0x89, 0xD0}));
}

// ================================================================
//  neg_r
// ================================================================

TEST_CASE("neg_r base + extended", "[x64][neg]") {
  {
    Assembler a;
    a.neg_r(Gpr::rax);   // REX.W + F7 + ModRM /3 rm=rax(0) = D8
    CHECK(a.bytes() == vec({0x48, 0xF7, 0xD8}));
  }
  {
    Assembler a;
    a.neg_r(Gpr::r13);   // REX.W+B + F7 + ModRM /3 rm=r13(5) = DD
    CHECK(a.bytes() == vec({0x49, 0xF7, 0xDD}));
  }
}

// ================================================================
//  xchg_rr
// ================================================================

TEST_CASE("xchg_rr base registers", "[x64][xchg]") {
  Assembler a;
  a.xchg_rr(Gpr::rax, Gpr::rcx);   // swap rax, rcx
  // REX.W + 87 + ModRM reg=rax(0) rm=rcx(1) = C1
  CHECK(a.bytes() == vec({0x48, 0x87, 0xC1}));
}

TEST_CASE("xchg_rr extended registers", "[x64][xchg]") {
  Assembler a;
  a.xchg_rr(Gpr::r8, Gpr::r15);    // swap r8, r15
  // REX.W + R(r8) + B(r15) = 0x4D, 0x87, ModRM reg=r8(0) rm=r15(7) = C7
  CHECK(a.bytes() == vec({0x4D, 0x87, 0xC7}));
}

// ================================================================
//  mov_ri
// ================================================================

TEST_CASE("mov_ri rax 42", "[x64][mov]") {
  Assembler a;
  a.mov_ri(Gpr::rax, 42);
  // REX.W + B8+0 + imm64
  auto expected = vec({0x48, 0xB8, 42, 0, 0, 0, 0, 0, 0, 0});
  CHECK(a.bytes() == expected);
}

TEST_CASE("mov_ri r8 negative", "[x64][mov]") {
  Assembler a;
  a.mov_ri(Gpr::r8, -1);
  // REX.W + B (REX.B=1) = 0x49, B8+low3(r8)=0xB8, imm64 = 0xFFFFFFFFFFFFFFFF
  auto expected = vec({0x49, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  CHECK(a.bytes() == expected);
}

// ================================================================
//  mov_mr / mov_rm  (memory)
// ================================================================

TEST_CASE("mov_mr [rbp+disp32]", "[x64][mem]") {
  Assembler a;
  a.mov_mr(Mem{Gpr::rbp, 0x10}, Gpr::rcx);
  // REX.W + 0x89, ModRM mod=10 rm=5 reg=1 -> 0x89 | (1<<3) | 5 = 0x8D, disp32=0x10
  CHECK(a.bytes() == vec({0x48, 0x89, 0x8D, 0x10, 0x00, 0x00, 0x00}));
}

TEST_CASE("mov_rm [rsp+disp32]", "[x64][mem]") {
  Assembler a;
  a.mov_rm(Gpr::rdx, Mem{Gpr::rsp, 0x20});
  // REX.W + 0x8B, ModRM mod=10 rm=4 reg=2 -> 0x94, SIB 0x24, disp32=0x20
  CHECK(a.bytes() == vec({0x48, 0x8B, 0x94, 0x24, 0x20, 0x00, 0x00, 0x00}));
}

TEST_CASE("mov_mr [r12+disp32] extended base", "[x64][mem]") {
  Assembler a;
  a.mov_mr(Mem{Gpr::r12, 0x30}, Gpr::rax);
  // REX.W + REX.B (r12 base) = 0x49, 0x89, ModRM mod=10 rm=4 reg=0 -> 0x84,
  // SIB 0x24 (base=r12 via REX.B), disp32=0x30
  CHECK(a.bytes() == vec({0x49, 0x89, 0x84, 0x24, 0x30, 0x00, 0x00, 0x00}));
}

// ================================================================
//  movzx_rr
// ================================================================

TEST_CASE("movzx_rr rax, rcx", "[x64][movzx]") {
  Assembler a;
  a.movzx_rr(Gpr::rax, Gpr::rcx);
  // REX.W + 0F B6, ModRM reg=rax(0) rm=rcx(1) -> 0xC1
  CHECK(a.bytes() == vec({0x48, 0x0F, 0xB6, 0xC1}));
}

TEST_CASE("movzx_rr r8, r9", "[x64][movzx]") {
  Assembler a;
  a.movzx_rr(Gpr::r8, Gpr::r9);
  // REX.W + R + B = 0x4D, 0F B6, ModRM reg=r8(0) rm=r9(1) -> 0xC1
  CHECK(a.bytes() == vec({0x4D, 0x0F, 0xB6, 0xC1}));
}

// ================================================================
//  add_rr / sub_rr / cmp_rr
// ================================================================

TEST_CASE("add_rr rcx += rax", "[x64][arith]") {
  Assembler a;
  a.add_rr(Gpr::rcx, Gpr::rax);   // rcx += rax  →  reg=rax(0), rm=rcx(1)
  CHECK(a.bytes() == vec({0x48, 0x01, 0xC1}));
}

TEST_CASE("sub_rr rdx -= rbx", "[x64][arith]") {
  Assembler a;
  a.sub_rr(Gpr::rdx, Gpr::rbx);   // rdx -= rbx  →  reg=rbx(3), rm=rdx(2)
  CHECK(a.bytes() == vec({0x48, 0x29, 0xDA}));
}

TEST_CASE("cmp_rr rax, rcx", "[x64][arith]") {
  Assembler a;
  a.cmp_rr(Gpr::rax, Gpr::rcx);   // cmp rax, rcx  →  reg=rcx(1), rm=rax(0)
  CHECK(a.bytes() == vec({0x48, 0x39, 0xC8}));
}

TEST_CASE("add_rr r8, r9 extended", "[x64][arith]") {
  Assembler a;
  a.add_rr(Gpr::r8, Gpr::r9);     // r8 += r9  →  reg=r9(1), rm=r8(0)
  // REX.W + R + B = 0x4D
  CHECK(a.bytes() == vec({0x4D, 0x01, 0xC8}));
}

// ================================================================
//  imul_rr  (reversed operand order trap)
// ================================================================

TEST_CASE("imul_rr base regs", "[x64][arith]") {
  Assembler a;
  a.imul_rr(Gpr::rcx, Gpr::rax);  // rcx *= rax  →  imul: reg=rcx(1), rm=rax(0)
  // REX.W + 0F AF, ModRM reg=1 rm=0 -> 0xC8
  CHECK(a.bytes() == vec({0x48, 0x0F, 0xAF, 0xC8}));
}

TEST_CASE("imul_rr reversed trap vs add_rr", "[x64][arith]") {
  Assembler a_add, a_imul;
  // add r8, r9  →  reg=r9(1), rm=r8(0)  →  ModRM C0|(1<<3)|0 = 0xC8
  a_add.add_rr(Gpr::r8, Gpr::r9);
  // imul r8, r9  →  reg=r8(0), rm=r9(1)  →  ModRM C0|(0<<3)|1 = 0xC1
  a_imul.imul_rr(Gpr::r8, Gpr::r9);

  CHECK(a_add.bytes()  == vec({0x4D, 0x01, 0xC8}));
  CHECK(a_imul.bytes() == vec({0x4D, 0x0F, 0xAF, 0xC1}));
  // Verify the ModRM byte differs: add=0xC8, imul=0xC1
  CHECK(a_add.bytes()[2]  == 0xC8);
  CHECK(a_imul.bytes()[3] == 0xC1);
}

// ================================================================
//  add_ri / sub_ri
// ================================================================

TEST_CASE("add_ri rax, 123", "[x64][arith]") {
  Assembler a;
  a.add_ri(Gpr::rax, 123);
  // REX.W + 81 /0, ModRM reg=0 rm=0 -> 0xC0, imm32 = 123
  CHECK(a.bytes() == vec({0x48, 0x81, 0xC0, 123, 0, 0, 0}));
}

TEST_CASE("sub_ri r8, 5", "[x64][arith]") {
  Assembler a;
  a.sub_ri(Gpr::r8, 5);
  // REX.W + B = 0x49, 81 /5, ModRM reg=5 rm=0 -> 0xE8, imm32 = 5
  CHECK(a.bytes() == vec({0x49, 0x81, 0xE8, 5, 0, 0, 0}));
}

// ================================================================
//  cqo
// ================================================================

TEST_CASE("cqo", "[x64][arith]") {
  Assembler a;
  a.cqo();
  CHECK(a.bytes() == vec({0x48, 0x99}));
}

// ================================================================
//  idiv_r
// ================================================================

TEST_CASE("idiv rcx", "[x64][arith]") {
  Assembler a;
  a.idiv_r(Gpr::rcx);
  // REX.W + F7 /7, ModRM reg=7 rm=1 -> 0xF9
  CHECK(a.bytes() == vec({0x48, 0xF7, 0xF9}));
}

TEST_CASE("idiv r8 extended", "[x64][arith]") {
  Assembler a;
  a.idiv_r(Gpr::r8);
  // REX.W + B = 0x49, F7 /7, ModRM reg=7 rm=0 -> 0xF8
  CHECK(a.bytes() == vec({0x49, 0xF7, 0xF8}));
}

// ================================================================
//  setCC
// ================================================================

TEST_CASE("sete rax — no REX needed", "[x64][setcc]") {
  Assembler a;
  a.sete(Gpr::rax);
  CHECK(a.bytes() == vec({0x0F, 0x94, 0xC0}));
}

TEST_CASE("setne rdi — null REX for SPL/BPL/SIL/DIL", "[x64][setcc]") {
  Assembler a;
  a.setne(Gpr::rdi);
  // null REX 0x40 + 0F 95 + ModRM reg=0 rm=7 -> 0xC7
  CHECK(a.bytes() == vec({0x40, 0x0F, 0x95, 0xC7}));
}

TEST_CASE("setl rsp — null REX for SPL", "[x64][setcc]") {
  Assembler a;
  a.setl(Gpr::rsp);
  CHECK(a.bytes() == vec({0x40, 0x0F, 0x9C, 0xC4}));
}

TEST_CASE("setle rbp — null REX for BPL", "[x64][setcc]") {
  Assembler a;
  a.setle(Gpr::rbp);
  CHECK(a.bytes() == vec({0x40, 0x0F, 0x9E, 0xC5}));
}

TEST_CASE("setg rsi — null REX for SIL", "[x64][setcc]") {
  Assembler a;
  a.setg(Gpr::rsi);
  CHECK(a.bytes() == vec({0x40, 0x0F, 0x9F, 0xC6}));
}

TEST_CASE("setge r8 — REX.B for extended", "[x64][setcc]") {
  Assembler a;
  a.setge(Gpr::r8);
  // REX.B = 0x41 + 0F 9D + ModRM reg=0 rm=0 -> 0xC0
  CHECK(a.bytes() == vec({0x41, 0x0F, 0x9D, 0xC0}));
}

TEST_CASE("all setCC condition codes", "[x64][setcc]") {
  Assembler a;
  a.sete (Gpr::rdx);  // 0F 94 C2
  a.setne(Gpr::rdx);  // 0F 95 C2
  a.setl (Gpr::rdx);  // 0F 9C C2
  a.setle(Gpr::rdx);  // 0F 9E C2
  a.setg (Gpr::rdx);  // 0F 9F C2
  a.setge(Gpr::rdx);  // 0F 9D C2
  auto expected = vec({
    0x0F, 0x94, 0xC2,
    0x0F, 0x95, 0xC2,
    0x0F, 0x9C, 0xC2,
    0x0F, 0x9E, 0xC2,
    0x0F, 0x9F, 0xC2,
    0x0F, 0x9D, 0xC2
  });
  CHECK(a.bytes() == expected);
}

// ================================================================
//  Labels / control flow
// ================================================================

TEST_CASE("jmp backward rel8", "[x64][jmp]") {
  Assembler a;
  auto l = a.new_label();
  a.bind(l);
  a.jmp(l);  // jmp to itself: rel8 delta = -2 => 0xFE
  CHECK(a.bytes() == vec({0xEB, 0xFE}));
}

TEST_CASE("jmp forward rel32 then bind", "[x64][jmp][label]") {
  Assembler a;
  auto l = a.new_label();
  a.jmp(l);          // E9 + 00 00 00 00 (placeholder)
  a.mov_ri(Gpr::rax, 42);
  a.bind(l);         // patches rel32
  a.ret();
  // After finalize, the bytes should be valid
  a.finalize();
  // Forward jmp rel32: jmp at offset 0 (5 bytes), mov_ri at offset 5 (10 bytes),
  // label at offset 15. delta = 15 - 5 = 10.
  auto const& b = a.bytes();
  CHECK(b[0] == 0xE9);
  CHECK(b[1] == 10);
  CHECK(b[2] == 0);
  CHECK(b[3] == 0);
  CHECK(b[4] == 0);
}

TEST_CASE("je forward label", "[x64][jcc]") {
  Assembler a;
  auto l = a.new_label();
  a.je(l);           // 0F 84 + 00 00 00 00
  a.mov_ri(Gpr::rax, 1);
  a.bind(l);
  a.mov_ri(Gpr::rax, 2);
  a.finalize();
  // je is 6 bytes and skips over the 10-byte mov_ri(rax, 1),
  // so the patched rel32 delta is 10.
  auto const& b = a.bytes();
  CHECK(b[0] == 0x0F);
  CHECK(b[1] == 0x84);
  // rel32 written at bind time:
  CHECK(b[2] == 10);
  CHECK(b[3] == 0);
  CHECK(b[4] == 0);
  CHECK(b[5] == 0);
}

TEST_CASE("call forward label", "[x64][call]") {
  Assembler a;
  auto l = a.new_label();
  a.call(l);         // E8 + 00 00 00 00
  a.bind(l);
  a.ret();
  a.finalize();
  // call at offset 0, 5 bytes => end = 5
  // bind at offset 5
  // patched rel32 = 5 - 5 = 0 (call to next instruction)
  auto const& b = a.bytes();
  CHECK(b[0] == 0xE8);
  CHECK(b[1] == 0);
  CHECK(b[2] == 0);
  CHECK(b[3] == 0);
  CHECK(b[4] == 0);
}

TEST_CASE("call backward label bound before", "[x64][call]") {
  Assembler a;
  auto l = a.new_label();
  a.bind(l);
  a.mov_ri(Gpr::rax, 42);
  a.call(l);         // E8 + delta from end of call to l
  a.finalize();
  // l bound at offset 0
  // mov_ri(rax,42) = 10 bytes, starts at 0, ends at 10
  // call at offset 10, 5 bytes => end = 15
  // delta = 0 - 15 = -15 = 0xFFFFFFF1
  auto const& b = a.bytes();
  CHECK(b[10] == 0xE8);   // call opcode
  CHECK(b[11] == 0xF1);   // -15 as unsigned = 0xFFFFFFF1
  CHECK(b[12] == 0xFF);
  CHECK(b[13] == 0xFF);
  CHECK(b[14] == 0xFF);
}

// ================================================================
//  call_r
// ================================================================

TEST_CASE("call_r r8", "[x64][call]") {
  Assembler a;
  a.call_r(Gpr::r8);
  // REX.W + B = 0x49, FF /2, ModRM reg=2 rm=0 -> 0xD0
  CHECK(a.bytes() == vec({0x49, 0xFF, 0xD0}));
}

TEST_CASE("call_r rax", "[x64][call]") {
  Assembler a;
  a.call_r(Gpr::rax);
  // REX.W = 0x48, FF /2, ModRM reg=2 rm=0 -> 0xD0
  CHECK(a.bytes() == vec({0x48, 0xFF, 0xD0}));
}

// ================================================================
//  push / pop
// ================================================================

TEST_CASE("push rax", "[x64][stack]") {
  Assembler a;
  a.push(Gpr::rax);
  CHECK(a.bytes() == vec({0x50}));
}

TEST_CASE("pop rcx", "[x64][stack]") {
  Assembler a;
  a.pop(Gpr::rcx);
  CHECK(a.bytes() == vec({0x59}));
}

TEST_CASE("push r8 extended", "[x64][stack]") {
  Assembler a;
  a.push(Gpr::r8);
  // REX.B + 50+0 = 0x41 0x50
  CHECK(a.bytes() == vec({0x41, 0x50}));
}

TEST_CASE("pop r9 extended", "[x64][stack]") {
  Assembler a;
  a.pop(Gpr::r9);
  // REX.B + 58+1 = 0x41 0x59
  CHECK(a.bytes() == vec({0x41, 0x59}));
}

// ================================================================
//  ret
// ================================================================

TEST_CASE("ret", "[x64][control]") {
  Assembler a;
  a.ret();
  CHECK(a.bytes() == vec({0xC3}));
}

// ================================================================
//  Multiple instructions
// ================================================================

TEST_CASE("add_rr and sub_rr and ret", "[x64][integ]") {
  Assembler a;
  a.add_rr(Gpr::rax, Gpr::rcx);
  a.sub_rr(Gpr::rdx, Gpr::rbx);
  a.ret();
  CHECK(a.bytes() == vec({
    0x48, 0x01, 0xC8,   // add rax, rcx
    0x48, 0x29, 0xDA,   // sub rdx, rbx
    0xC3                 // ret
  }));
}

// ================================================================
//  executable_copy smoke test
// ================================================================

TEST_CASE("executable_copy returns 42", "[x64][exec]") {
  Assembler a;
  a.mov_ri(Gpr::rax, 42);
  a.ret();
  a.finalize();

  auto code = a.executable_copy();
  REQUIRE(code.ptr() != nullptr);
  REQUIRE(code.invoke<std::int64_t>() == 42);
}

TEST_CASE("ExecBuffer transfers ownership on move", "[x64][exec]") {
  Assembler a;
  a.mov_ri(Gpr::rax, 42);
  a.ret();
  a.finalize();

  auto code = a.executable_copy();
  auto size = code.size();
  auto moved = std::move(code);

  CHECK(code.ptr() == nullptr);
  CHECK(code.size() == 0);
  REQUIRE(moved.ptr() != nullptr);
  CHECK(moved.size() == size);
  REQUIRE(moved.invoke<std::int64_t>() == 42);
}

TEST_CASE("ExecBuffer move assignment releases old mapping", "[x64][exec]") {
  Assembler first;
  first.mov_ri(Gpr::rax, 1);
  first.ret();
  first.finalize();

  Assembler second;
  second.mov_ri(Gpr::rax, 42);
  second.ret();
  second.finalize();

  auto code = first.executable_copy();
  auto replacement = second.executable_copy();
  code = std::move(replacement);

  CHECK(replacement.ptr() == nullptr);
  CHECK(replacement.size() == 0);
  REQUIRE(code.ptr() != nullptr);
  REQUIRE(code.invoke<std::int64_t>() == 42);
}

TEST_CASE("ExecBuffer self-move-assignment is a no-op", "[x64][exec]") {
  Assembler a;
  a.mov_ri(Gpr::rax, 42);
  a.ret();
  a.finalize();

  auto code = a.executable_copy();
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
  code = std::move(code);
#pragma clang diagnostic pop

  REQUIRE(code.ptr() != nullptr);
  REQUIRE(code.invoke<std::int64_t>() == 42);
}
