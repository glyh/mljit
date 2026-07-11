// SSA IR tests — verifies ModuleBuilder, FunctionBuilder, and dump output.
import mljit.ir;
import mljit.ir.dump;

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace mljit::ir;
namespace dump = mljit::ir::dump;

// ── Straight-line add1 (full snapshot) ─────────────────────
//
// func @add1(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = const_i64 1
//   v2: i64 = iadd v0, v1
//   ret v2
// }

TEST_CASE("add1 straight-line snapshot", "[ir]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "add1");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);

  auto one = fb.const_i64(entry, 1);
  auto sum = fb.iadd(entry, x, one);
  fb.ret(entry, sum);

  auto mod   = mb.finish();
  auto text  = dump::to_text(mod);

  auto expected =
    "func @add1(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 1\n"
    "  v2: i64 = iadd v0, v1\n"
    "  ret v2\n"
    "}\n";

  CHECK(text == expected);
}

// ── Block-parameter abs (full snapshot) ────────────────────
//
// func @abs(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = const_i64 0
//   v2: i1 = icmp slt v0, v1
//   branch v2, ^negative(v0), ^done(v0)
// ^negative(v3: i64):
//   v5: i64 = const_i64 0
//   v6: i64 = isub v5, v3
//   jump ^done(v6)
// ^done(v4: i64):
//   ret v4
// }

TEST_CASE("abs block-param control-flow snapshot", "[ir]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "abs");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);

  auto zero1 = fb.const_i64(entry, 0);
  auto cmp   = fb.icmp(entry, IcmpCond::slt, x, zero1);
  auto neg   = fb.append_block({Type::i64}, "negative");
  auto done  = fb.append_block({Type::i64}, "done");
  fb.branch(entry, cmp, neg, {x}, done, {x});

  auto neg_x = fb.param_id(neg, 0);
  auto zero2 = fb.const_i64(neg, 0);
  auto pos   = fb.isub(neg, zero2, neg_x);
  fb.jump(neg, done, {pos});

  auto result = fb.param_id(done, 0);
  fb.ret(done, result);

  auto mod   = mb.finish();
  auto text  = dump::to_text(mod);

  auto expected =
    "func @abs(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 0\n"
    "  v2: i1 = icmp slt v0, v1\n"
    "  branch v2, ^negative(v0), ^done(v0)\n"
    "^negative(v3: i64):\n"
    "  v5: i64 = const_i64 0\n"
    "  v6: i64 = isub v5, v3\n"
    "  jump ^done(v6)\n"
    "^done(v4: i64):\n"
    "  ret v4\n"
    "}\n";

  CHECK(text == expected);
}

// ── arith ops decode correctly ─────────────────────────────
TEST_CASE("isub and imul snapshot", "[ir]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64, Type::i64}, Type::i64, "arith");
  auto fb   = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);
  auto y = fb.param_id(entry, 1);

  auto diff = fb.isub(entry, x, y);
  auto prod = fb.imul(entry, x, y);
  fb.ret(entry, prod);
  (void)diff;

  auto mod  = mb.finish();
  auto text = dump::to_text(mod);

  // Snapshot uses stable value numbering — entry params v0,v1,
  // then instruction results v2,v3.
  auto expected =
    "func @arith(i64, i64) -> i64 {\n"
    "^entry(v0: i64, v1: i64):\n"
    "  v2: i64 = isub v0, v1\n"
    "  v3: i64 = imul v0, v1\n"
    "  ret v3\n"
    "}\n";

  CHECK(text == expected);
}

// ── Comparison ops ─────────────────────────────────────────
TEST_CASE("all icmp predicates snapshot", "[ir]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "cmp_all");
  auto fb    = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x     = fb.param_id(entry, 0);

  auto zero = fb.const_i64(entry, 0);
  auto c1 = fb.icmp(entry, IcmpCond::eq,  x, zero);
  auto c2 = fb.icmp(entry, IcmpCond::ne,  x, zero);
  auto c3 = fb.icmp(entry, IcmpCond::slt, x, zero);
  auto c4 = fb.icmp(entry, IcmpCond::sle, x, zero);
  auto c5 = fb.icmp(entry, IcmpCond::sgt, x, zero);
  auto c6 = fb.icmp(entry, IcmpCond::sge, x, zero);
  (void)c1; (void)c2; (void)c3; (void)c4; (void)c5; (void)c6;

  auto one = fb.const_i64(entry, 1);
  fb.ret(entry, one);

  auto mod  = mb.finish();
  auto text = dump::to_text(mod);

  auto expected =
    "func @cmp_all(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 0\n"
    "  v2: i1 = icmp eq v0, v1\n"
    "  v3: i1 = icmp ne v0, v1\n"
    "  v4: i1 = icmp slt v0, v1\n"
    "  v5: i1 = icmp sle v0, v1\n"
    "  v6: i1 = icmp sgt v0, v1\n"
    "  v7: i1 = icmp sge v0, v1\n"
    "  v8: i64 = const_i64 1\n"
    "  ret v8\n"
    "}\n";

  CHECK(text == expected);
}

// ── call instruction — cross-function call snapshot ─────────
//
// func @add1(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = const_i64 1
//   v2: i64 = iadd v0, v1
//   ret v2
// }
// func @twice(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = call @add1(v0)
//   v2: i64 = call @add1(v1)
//   ret v2
// }

TEST_CASE("call instruction cross-function snapshot", "[ir]") {
  ModuleBuilder mb;
  auto add1 = mb.create_function({Type::i64}, Type::i64, "add1");
  {
    auto fb = mb.function_builder(add1);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto one = fb.const_i64(entry, 1);
    auto sum = fb.iadd(entry, x, one);
    fb.ret(entry, sum);
  }
  auto twice = mb.create_function({Type::i64}, Type::i64, "twice");
  {
    auto fb = mb.function_builder(twice);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto a = fb.call(entry, add1, {x});
    auto b = fb.call(entry, add1, {a});
    fb.ret(entry, b);
  }

  auto mod  = mb.finish();
  auto text = dump::to_text(mod);

  auto expected =
    "func @add1(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 1\n"
    "  v2: i64 = iadd v0, v1\n"
    "  ret v2\n"
    "}\n"
    "func @twice(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = call @add1(v0)\n"
    "  v2: i64 = call @add1(v1)\n"
    "  ret v2\n"
    "}\n";

  CHECK(text == expected);
}

// ── zero-param function edge case ───────────────────────────
//
// func @answer() -> i64 {
// ^entry():
//   v0: i64 = const_i64 42
//   ret v0
// }

TEST_CASE("zero-param function snapshot", "[ir]") {
  ModuleBuilder mb;
  auto answer = mb.create_function({}, Type::i64, "answer");
  {
    auto fb = mb.function_builder(answer);
    auto entry = fb.entry_block();
    auto c = fb.const_i64(entry, 42);
    fb.ret(entry, c);
  }

  auto mod  = mb.finish();
  auto text = dump::to_text(mod);

  auto expected =
    "func @answer() -> i64 {\n"
    "^entry():\n"
    "  v0: i64 = const_i64 42\n"
    "  ret v0\n"
    "}\n";

  CHECK(text == expected);
}

// ── multi-function independent module dump ──────────────────
//
// func @add1(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = const_i64 1
//   v2: i64 = iadd v0, v1
//   ret v2
// }
// func @sub1(i64) -> i64 {
// ^entry(v0: i64):
//   v1: i64 = const_i64 1
//   v2: i64 = isub v0, v1
//   ret v2
// }

TEST_CASE("multi-function independent module snapshot", "[ir]") {
  ModuleBuilder mb;
  auto add1 = mb.create_function({Type::i64}, Type::i64, "add1");
  {
    auto fb = mb.function_builder(add1);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto one = fb.const_i64(entry, 1);
    auto sum = fb.iadd(entry, x, one);
    fb.ret(entry, sum);
  }
  auto sub1 = mb.create_function({Type::i64}, Type::i64, "sub1");
  {
    auto fb = mb.function_builder(sub1);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto one = fb.const_i64(entry, 1);
    auto diff = fb.isub(entry, x, one);
    fb.ret(entry, diff);
  }

  auto mod  = mb.finish();
  auto text = dump::to_text(mod);

  auto expected =
    "func @add1(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 1\n"
    "  v2: i64 = iadd v0, v1\n"
    "  ret v2\n"
    "}\n"
    "func @sub1(i64) -> i64 {\n"
    "^entry(v0: i64):\n"
    "  v1: i64 = const_i64 1\n"
    "  v2: i64 = isub v0, v1\n"
    "  ret v2\n"
    "}\n";

  CHECK(text == expected);
}

// ── empty module edge case ──────────────────────────────────

TEST_CASE("empty module dump", "[ir]") {
  ModuleBuilder mb;
  auto mod  = mb.finish();
  auto text = dump::to_text(mod);
  CHECK(text.empty());
}
