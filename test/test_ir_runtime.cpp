// SSA Interpreter runtime tests.
import mljit.ir;
import mljit.ir.printer;
import mljit.ir.verifier;
import mljit.ir.runtime;

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mljit::ir;
namespace printer = mljit::ir::printer;
namespace verifier = mljit::ir::verifier;
using namespace mljit::ir::runtime;

// ════════════════════════════════════════════════════════════
//  Basic tests
// ════════════════════════════════════════════════════════════

TEST_CASE("add1(41) = 42", "[runtime]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "add1");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);
  auto one = fb.const_i64(entry, 1);
  auto sum = fb.iadd(entry, x, one);
  fb.ret(entry, sum);

  auto mod = mb.finish();
  CHECK(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {41});
  REQUIRE(res.has_value());
  CHECK(res->value == 42);
}

TEST_CASE("abs(-7) = 7", "[runtime]") {
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

  auto mod = mb.finish();
  CHECK(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {-7});
  REQUIRE(res.has_value());
  CHECK(res->value == 7);
}

TEST_CASE("gcd(48, 18) = 6", "[runtime]") {
  // Euclidean algorithm without jumping back to entry (rejected by verifier).
  // entry(%a, %b): branch %b==0 -> base(%a), loop(%a, %b)
  // base(%r): ret %r
  // loop(%a, %b): %rem = irem %a, %b; branch %rem==0 -> base(%b), loop(%b, %rem)
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64, Type::i64}, Type::i64, "gcd");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto a = fb.param_id(entry, 0);
  auto b = fb.param_id(entry, 1);

  auto zero = fb.const_i64(entry, 0);
  auto cond = fb.icmp(entry, IcmpCond::eq, b, zero);
  auto base = fb.append_block({Type::i64}, "base");
  auto loop = fb.append_block({Type::i64, Type::i64}, "loop");
  std::vector<ValueId> base_args = {a};
  std::vector<ValueId> loop_args = {a, b};
  fb.branch(entry, cond, base, base_args, loop, loop_args);

  // base(%result): ret %result
  auto base_r = fb.param_id(base, 0);
  fb.ret(base, base_r);

  // loop(%a2, %b2): %rem = irem %a2, %b2; branch %rem==0 -> base(%b2), loop(%b2, %rem)
  auto a2 = fb.param_id(loop, 0);
  auto b2 = fb.param_id(loop, 1);
  auto rem = fb.irem(loop, a2, b2);
  auto zero2 = fb.const_i64(loop, 0);
  auto cond2 = fb.icmp(loop, IcmpCond::eq, rem, zero2);
  std::vector<ValueId> base_args2 = {b2};
  std::vector<ValueId> loop_args2 = {b2, rem};
  fb.branch(loop, cond2, base, base_args2, loop, loop_args2);

  auto mod = mb.finish();
  auto vres = verifier::verify(mod);
  REQUIRE(vres.ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {48, 18});
  REQUIRE(res.has_value());
  CHECK(res->value == 6);
}

TEST_CASE("fib(10) = 55", "[runtime]") {
  ModuleBuilder mb;
  auto fib = mb.create_function({Type::i64}, Type::i64, "fib");
  auto fb = mb.function_builder(fib);
  auto entry = fb.entry_block();
  auto n = fb.param_id(entry, 0);

  auto two = fb.const_i64(entry, 2);
  auto cond = fb.icmp(entry, IcmpCond::slt, n, two);
  auto base = fb.append_block({Type::i64}, "base");
  auto recur = fb.append_block({}, "recur");
  fb.branch(entry, cond, base, {n}, recur, {});

  // base(%n): ret %n
  auto base_n = fb.param_id(base, 0);
  fb.ret(base, base_n);

  // recur: n1 = isub n, 1; a = call @fib(n1)
  //        n2 = isub n, 2; b = call @fib(n2)
  //        result = iadd a, b; ret result
  auto one = fb.const_i64(recur, 1);
  auto n1 = fb.isub(recur, n, one);
  auto a = fb.call(recur, fib, {n1});
  auto two_r = fb.const_i64(recur, 2);
  auto n2 = fb.isub(recur, n, two_r);
  auto b = fb.call(recur, fib, {n2});
  auto result = fb.iadd(recur, a, b);
  fb.ret(recur, result);

  auto mod = mb.finish();

  // Verify (higher step limit for recursion)
  auto vres = verifier::verify(mod);
  REQUIRE(vres.ok());

  Interpreter interp(mod);
  auto res = interp.run(fib, {10}, {.max_steps = 10'000'000, .max_call_depth = 1000});
  if (!res) {
    auto const& err = res.error();
    FAIL("runtime error: " << static_cast<int>(err.error.kind) << " " << err.error.detail
         << " steps=" << err.stats.steps_executed << " depth=" << err.stats.max_call_depth_observed);
  }
  REQUIRE(res.has_value());
  CHECK(res->value == 55);
}

// ════════════════════════════════════════════════════════════
//  Error handling tests
// ════════════════════════════════════════════════════════════

TEST_CASE("arithmetic overflow detected", "[runtime][error]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "overflow");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto max = fb.const_i64(entry, INT64_MAX);
  auto one = fb.const_i64(entry, 1);
  auto sum = fb.iadd(entry, max, one); // INT64_MAX + 1
  fb.ret(entry, sum);

  auto mod = mb.finish();
  CHECK(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {});
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().error.kind == RuntimeErrorKind::ArithmeticOverflow);
}

TEST_CASE("division by zero detected", "[runtime][error]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "divzero");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto c42 = fb.const_i64(entry, 42);
  auto zero = fb.const_i64(entry, 0);
  auto q = fb.idiv(entry, c42, zero); // 42 / 0
  fb.ret(entry, q);

  auto mod = mb.finish();
  CHECK(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {});
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().error.kind == RuntimeErrorKind::DivisionByZero);
}

TEST_CASE("idiv INT64_MIN / -1 is division overflow", "[error][runtime]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "div_overflow");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto min = fb.const_i64(entry, std::numeric_limits<std::int64_t>::min());
  auto minus_one = fb.const_i64(entry, -1);
  auto r = fb.idiv(entry, min, minus_one);
  fb.ret(entry, r);
  auto mod = mb.finish();
  REQUIRE(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto result = interp.run(fid, {});
  REQUIRE(!result.has_value());
  CHECK(result.error().error.kind == RuntimeErrorKind::DivisionOverflow);
}

TEST_CASE("max call depth exceeded", "[error][runtime]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "infinite_recurse");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto r = fb.call(entry, fid, {});  // recursive call
  fb.ret(entry, r);
  auto mod = mb.finish();
  REQUIRE(verifier::verify(mod).ok());

  Interpreter interp(mod);
  RunOptions opts;
  opts.max_call_depth = 3;
  auto result = interp.run(fid, {}, opts);
  REQUIRE(!result.has_value());
  CHECK(result.error().error.kind == RuntimeErrorKind::CallDepthExceeded);
  CHECK(result.error().stats.max_call_depth_observed == 3);
}

TEST_CASE("step limit exceeded", "[runtime][error]") {
  // Infinite loop: func @loop() -> i64 { ^entry: const_i64 0; jump ^entry }
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "loop");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto c0 = fb.const_i64(entry, 0);
  fb.jump(entry, entry, {});
  (void)c0;

  auto mod = mb.finish();
  Interpreter interp(mod);
  auto res = interp.run(fid, {}, {.max_steps = 100, .max_call_depth = 10});
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().error.kind == RuntimeErrorKind::StepLimitExceeded);
}

// ── Parallel-copy swap (regression test for bind_block_params) ──
//
// func @swap_loop(%n, %x, %y):
//   if %n == 0: ret %y
//   else: loop(%n, %x, %y) -> jump entry(%next_n, %y, %x)
//
// swap_loop(1, 10, 20) should return 20 after swap becomes 10.
// Without the parallel-copy fix, the self-edge loses %x's original value.

TEST_CASE("self-loop param swap is correct", "[runtime]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64, Type::i64, Type::i64}, Type::i64, "swap_loop");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto n = fb.param_id(entry, 0);
  auto x = fb.param_id(entry, 1);
  auto y = fb.param_id(entry, 2);

  auto zero = fb.const_i64(entry, 0);
  auto cond = fb.icmp(entry, IcmpCond::eq, n, zero);
  auto done = fb.append_block({Type::i64}, "done");
  auto loop_b = fb.append_block({Type::i64, Type::i64, Type::i64}, "loop");
  std::vector<ValueId> done_args = {y};
  std::vector<ValueId> loop_args = {n, x, y};
  fb.branch(entry, cond, done, done_args, loop_b, loop_args);

  auto done_r = fb.param_id(done, 0);
  fb.ret(done, done_r);

  auto n2 = fb.param_id(loop_b, 0);
  auto x2 = fb.param_id(loop_b, 1);
  auto y2 = fb.param_id(loop_b, 2);
  auto one = fb.const_i64(loop_b, 1);
  auto next_n = fb.isub(loop_b, n2, one);
  // Jump back to entry with permuted args: entry(%next_n, %y2, %x2) — swaps x and y
  std::vector<ValueId> entry_args = {next_n, y2, x2};
  fb.jump(loop_b, entry, entry_args);

  auto mod = mb.finish();
  CHECK(verifier::verify(mod).ok());

  Interpreter interp(mod);
  auto res = interp.run(fid, {1, 10, 20});
  REQUIRE(res.has_value());
  // After 1 iteration: entry(0, 20, 10) -> done(%y=10). Without parallel-copy fix, returns 20.
  CHECK(res->value == 10);
}
