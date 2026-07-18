import mljit.ir;
import mljit.ir.runtime;
import mljit.codegen;
import mljit.x64;

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace mljit;

// Oracle: run the SSA interpreter and return its result.
static auto interp(ir::Module const& mod, ir::FunctionId fid,
                   std::vector<std::int64_t> args) -> std::int64_t {
  ir::runtime::Interpreter interpreter(mod);
  auto res = interpreter.run(fid, args);
  REQUIRE(res.has_value());
  return res->value;
}

// add1(x) = x + 1
TEST_CASE("codegen: add1 matches interpreter", "[codegen]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "add1");
  auto fb  = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x   = fb.param_id(entry, 0);
  auto one = fb.const_i64(entry, 1, "one");
  auto r   = fb.iadd(entry, x, one, "r");
  fb.ret(entry, r);

  auto mod = mb.finish();
  auto buf = codegen::compile(mod, fid);

  for (std::int64_t x_in : {0, 1, 5, -3, 100, -1}) {
    auto jit = buf.invoke<std::int64_t>(x_in);
    CHECK(jit == interp(mod, fid, {x_in}));
    CHECK(jit == x_in + 1);
  }
}

// square_plus(x) = x*x + x  (exercises imul + iadd, both commutative)
TEST_CASE("codegen: x*x + x matches interpreter", "[codegen]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "square_plus");
  auto fb  = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x  = fb.param_id(entry, 0);
  auto sq = fb.imul(entry, x, x, "sq");
  auto r  = fb.iadd(entry, sq, x, "r");
  fb.ret(entry, r);

  auto mod = mb.finish();
  auto buf = codegen::compile(mod, fid);

  for (std::int64_t x_in : {0, 1, 5, -3, 7, 12}) {
    auto jit = buf.invoke<std::int64_t>(x_in);
    CHECK(jit == interp(mod, fid, {x_in}));
    CHECK(jit == x_in * x_in + x_in);
  }
}

// abs(x): branch + merge with block parameters, and isub.
TEST_CASE("codegen: abs matches interpreter", "[codegen]") {
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
  auto buf = codegen::compile(mod, fid);

  for (std::int64_t x_in : {0, 1, 5, -1, -5, 100, -100, 42, -42}) {
    auto jit = buf.invoke<std::int64_t>(x_in);
    CHECK(jit == interp(mod, fid, {x_in}));
    CHECK(jit == (x_in < 0 ? -x_in : x_in));
  }
}

// gcd(a, b): a loop with a back-edge, irem, and block-parameter moves on the
// loop update edge.
TEST_CASE("codegen: gcd matches interpreter", "[codegen]") {
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
  auto rr = fb.irem(body, a2, b2, "r");
  fb.jump(body, loop, {b2, rr});
  auto resv = fb.param_id(done, 0);
  fb.ret(done, resv);

  auto mod = mb.finish();
  auto buf = codegen::compile(mod, fid);

  std::vector<std::pair<std::int64_t, std::int64_t>> cases = {
    {12, 8}, {48, 36}, {17, 5}, {100, 10}, {0, 5}, {7, 0}, {270, 192}, {13, 13},
  };
  for (auto [a_in, b_in] : cases) {
    auto jit = buf.invoke<std::int64_t>(a_in, b_in);
    CHECK(jit == interp(mod, fid, {a_in, b_in}));
  }
}

// fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)  -- recursive self-calls, values
// live across calls (callee-saved), stack alignment, isub with neg.
TEST_CASE("codegen: recursive fib matches interpreter", "[codegen]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "fib");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto n    = fb.param_id(entry, 0);
  auto base = fb.append_block({ir::Type::i64}, "base");
  auto rec  = fb.append_block({ir::Type::i64}, "rec");
  auto two   = fb.const_i64(entry, 2, "two");
  auto small = fb.icmp(entry, ir::IcmpCond::slt, n, two, "small");
  fb.branch(entry, small, base, {n}, rec, {n});

  auto nb = fb.param_id(base, 0);
  fb.ret(base, nb);

  auto nr = fb.param_id(rec, 0);
  auto one  = fb.const_i64(rec, 1, "one");
  auto n1   = fb.isub(rec, nr, one, "n1");
  auto f1   = fb.call(rec, fid, {n1}, "f1");
  auto two2 = fb.const_i64(rec, 2, "two2");
  auto n2   = fb.isub(rec, nr, two2, "n2");
  auto f2   = fb.call(rec, fid, {n2}, "f2");
  auto r    = fb.iadd(rec, f1, f2, "r");
  fb.ret(rec, r);

  auto mod = mb.finish();
  auto buf = codegen::compile(mod, fid);

  std::int64_t const expected[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};
  for (std::int64_t k = 0; k <= 12; ++k) {
    auto jit = buf.invoke<std::int64_t>(k);
    CHECK(jit == interp(mod, fid, {k}));
    CHECK(jit == expected[k]);
  }
}
