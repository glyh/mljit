import mljit.ir;
import mljit.regalloc;

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace mljit;

// abs(x): a branch/merge with block parameters.
//
//   ^entry(x):   zero = 0; isneg = x < 0; branch isneg, ^neg(x), ^done(x)
//   ^neg(v):     z = 0; r = z - v; jump ^done(r)
//   ^done(res):  ret res
TEST_CASE("numbering: abs (branch + merge)", "[regalloc][numbering]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "abs");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto x     = fb.param_id(entry, 0);
  auto neg   = fb.append_block({ir::Type::i64}, "neg");
  auto done  = fb.append_block({ir::Type::i64}, "done");

  auto zero  = fb.const_i64(entry, 0, "zero");
  auto isneg = fb.icmp(entry, ir::IcmpCond::slt, x, zero, "isneg");
  fb.branch(entry, isneg, neg, {x}, done, {x});

  auto v = fb.param_id(neg, 0);
  auto z = fb.const_i64(neg, 0, "z");
  auto r = fb.isub(neg, z, v, "r");
  fb.jump(neg, done, {r});

  auto res = fb.param_id(done, 0);
  fb.ret(done, res);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n = regalloc::compute_numbering(fn);
  auto dump = regalloc::dump_numbering(fn, n);

  // Block parameters carry no debug name through the builder, so they print
  // as v{id} (same fallback as ir_printer): x=v0, neg param=v1, done param=v2.
  std::string const expected =
    "^entry [0..8):\n"
    "  0: block-entry(v0)\n"
    "  2: zero = const_i64\n"
    "  4: isneg = icmp\n"
    "  6: term branch\n"
    "^neg [8..16):\n"
    "  8: block-entry(v1)\n"
    "  10: z = const_i64\n"
    "  12: r = isub\n"
    "  14: term jump\n"
    "^done [16..20):\n"
    "  16: block-entry(v2)\n"
    "  18: term ret\n";

  CHECK(dump == expected);
}

// gcd(a, b): an iterative loop with a back-edge.
//
//   ^entry(a, b):   jump ^loop(a, b)
//   ^loop(a1, b1):  zero = 0; is_zero = b1 == 0; branch is_zero, ^done(a1), ^body(a1, b1)
//   ^body(a2, b2):  r = a2 % b2; jump ^loop(b2, r)
//   ^done(res):     ret res
TEST_CASE("numbering: gcd (loop / back-edge)", "[regalloc][numbering]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64, ir::Type::i64}, ir::Type::i64, "gcd");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto a = fb.param_id(entry, 0);
  auto b = fb.param_id(entry, 1);
  auto loop = fb.append_block({ir::Type::i64, ir::Type::i64}, "loop");
  auto body = fb.append_block({ir::Type::i64, ir::Type::i64}, "body");
  auto done = fb.append_block({ir::Type::i64}, "done");

  fb.jump(entry, loop, {a, b});

  auto a1 = fb.param_id(loop, 0);
  auto b1 = fb.param_id(loop, 1);
  auto zero    = fb.const_i64(loop, 0, "zero");
  auto is_zero = fb.icmp(loop, ir::IcmpCond::eq, b1, zero, "is_zero");
  fb.branch(loop, is_zero, done, {a1}, body, {a1, b1});

  auto a2 = fb.param_id(body, 0);
  auto b2 = fb.param_id(body, 1);
  auto r  = fb.irem(body, a2, b2, "r");
  fb.jump(body, loop, {b2, r});

  auto res = fb.param_id(done, 0);
  fb.ret(done, res);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n = regalloc::compute_numbering(fn);
  auto dump = regalloc::dump_numbering(fn, n);

  // Params print as v{id}: entry(v0,v1), loop(v2,v3), body(v4,v5), done(v6).
  std::string const expected =
    "^entry [0..4):\n"
    "  0: block-entry(v0, v1)\n"
    "  2: term jump\n"
    "^loop [4..12):\n"
    "  4: block-entry(v2, v3)\n"
    "  6: zero = const_i64\n"
    "  8: is_zero = icmp\n"
    "  10: term branch\n"
    "^body [12..18):\n"
    "  12: block-entry(v4, v5)\n"
    "  14: r = irem\n"
    "  16: term jump\n"
    "^done [18..22):\n"
    "  18: block-entry(v6)\n"
    "  20: term ret\n";

  CHECK(dump == expected);
}
