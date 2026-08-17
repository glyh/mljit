import mljit.ir;
import mljit.ir.runtime;
import mljit.codegen;
import mljit.x64;

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>
#include <string>

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
  auto code = codegen::compile(mod);

  for (std::int64_t x_in : {0, 1, 5, -3, 100, -1}) {
    auto jit = code.invoke<std::int64_t>(fid, x_in);
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
  auto code = codegen::compile(mod);

  for (std::int64_t x_in : {0, 1, 5, -3, 7, 12}) {
    auto jit = code.invoke<std::int64_t>(fid, x_in);
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
  auto code = codegen::compile(mod);

  for (std::int64_t x_in : {0, 1, 5, -1, -5, 100, -100, 42, -42}) {
    auto jit = code.invoke<std::int64_t>(fid, x_in);
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
  auto code = codegen::compile(mod);

  std::vector<std::pair<std::int64_t, std::int64_t>> cases = {
    {12, 8}, {48, 36}, {17, 5}, {100, 10}, {0, 5}, {7, 0}, {270, 192}, {13, 13},
  };
  for (auto [a_in, b_in] : cases) {
    auto jit = code.invoke<std::int64_t>(fid, a_in, b_in);
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
  auto code = codegen::compile(mod);

  std::int64_t const expected[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};
  for (std::int64_t k = 0; k <= 12; ++k) {
    auto jit = code.invoke<std::int64_t>(fid, k);
    CHECK(jit == interp(mod, fid, {k}));
    CHECK(jit == expected[k]);
  }
}

// High register pressure: compute N intermediates (x+1, x+2, ..., x+N), all
// live at once, then sum them -> forces spilling well past the register file.
// Result is sum_{i=1..N}(x + i) = N*x + N(N+1)/2.
TEST_CASE("codegen: high pressure spills and runs", "[codegen][spill]") {
  for (int N : {12, 15, 20, 30}) {
    ir::ModuleBuilder mb;
    auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "pressure");
    auto fb  = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);

    std::vector<ir::ValueId> vs;
    for (int i = 0; i < N; ++i) {
      auto c = fb.const_i64(entry, i + 1, "c" + std::to_string(i));
      vs.push_back(fb.iadd(entry, x, c, "v" + std::to_string(i)));
    }
    ir::ValueId acc = vs[0];
    for (std::size_t i = 1; i < vs.size(); ++i) acc = fb.iadd(entry, acc, vs[i]);
    fb.ret(entry, acc);

    auto mod = mb.finish();
    auto code = codegen::compile(mod);

    for (std::int64_t x_in : {0, 1, 7, -3}) {
      auto jit = code.invoke<std::int64_t>(fid, x_in);
      CHECK(jit == interp(mod, fid, {x_in}));
      CHECK(jit == static_cast<std::int64_t>(N) * x_in +
                   static_cast<std::int64_t>(N) * (N + 1) / 2);
    }
  }
}

// ── Cross-function calls (one buffer, one label per function) ──

// Mutual recursion: is_even(n) = n == 0 ? 1 : is_odd(n - 1), and vice versa.
// is_even is emitted first, so its call to is_odd is a *forward* reference that
// only the assembler's fixup list can resolve.
TEST_CASE("codegen: mutual recursion matches interpreter", "[codegen][crossfn]") {
  ir::ModuleBuilder mb;
  auto even = mb.create_function({ir::Type::i64}, ir::Type::i64, "is_even");
  auto odd  = mb.create_function({ir::Type::i64}, ir::Type::i64, "is_odd");

  // parity(self, other, at_zero): n == 0 ? at_zero : other(n - 1)
  auto parity = [&](ir::FunctionId self, ir::FunctionId other, std::int64_t at_zero) {
    auto fb    = mb.function_builder(self);
    auto entry = fb.entry_block();
    auto n     = fb.param_id(entry, 0);
    auto base  = fb.append_block({}, "base");
    auto rec   = fb.append_block({ir::Type::i64}, "rec");
    auto zero  = fb.const_i64(entry, 0, "zero");
    auto done  = fb.icmp(entry, ir::IcmpCond::eq, n, zero, "done");
    fb.branch(entry, done, base, {}, rec, {n});

    auto answer = fb.const_i64(base, at_zero, "answer");
    fb.ret(base, answer);

    auto nr  = fb.param_id(rec, 0);
    auto one = fb.const_i64(rec, 1, "one");
    auto n1  = fb.isub(rec, nr, one, "n1");
    auto r   = fb.call(rec, other, {n1}, "r");
    fb.ret(rec, r);
  };
  parity(even, odd, 1);
  parity(odd, even, 0);

  auto mod  = mb.finish();
  auto code = codegen::compile(mod);

  // Both functions are entry points into the same buffer.
  for (std::int64_t n = 0; n <= 12; ++n) {
    auto jit_even = code.invoke<std::int64_t>(even, n);
    auto jit_odd  = code.invoke<std::int64_t>(odd, n);
    CHECK(jit_even == interp(mod, even, {n}));
    CHECK(jit_odd == interp(mod, odd, {n}));
    CHECK(jit_even == (n % 2 == 0 ? 1 : 0));
    CHECK(jit_odd == (n % 2 == 0 ? 0 : 1));
  }
}

// A straight-line call chain over already-emitted callees (backward references):
// inc(x) = x + 1, dbl(x) = x * 2, chain(x) = dbl(inc(x)) + x, so `x` is live
// across two nested calls and must land somewhere call-safe.
TEST_CASE("codegen: multi-function call chain matches interpreter", "[codegen][crossfn]") {
  ir::ModuleBuilder mb;
  auto inc   = mb.create_function({ir::Type::i64}, ir::Type::i64, "inc");
  auto dbl   = mb.create_function({ir::Type::i64}, ir::Type::i64, "dbl");
  auto chain = mb.create_function({ir::Type::i64}, ir::Type::i64, "chain");

  {
    auto fb = mb.function_builder(inc);
    auto e  = fb.entry_block();
    auto x  = fb.param_id(e, 0);
    fb.ret(e, fb.iadd(e, x, fb.const_i64(e, 1, "one"), "r"));
  }
  {
    auto fb = mb.function_builder(dbl);
    auto e  = fb.entry_block();
    auto x  = fb.param_id(e, 0);
    fb.ret(e, fb.imul(e, x, fb.const_i64(e, 2, "two"), "r"));
  }
  {
    auto fb = mb.function_builder(chain);
    auto e  = fb.entry_block();
    auto x  = fb.param_id(e, 0);
    auto a  = fb.call(e, inc, {x}, "a");
    auto b  = fb.call(e, dbl, {a}, "b");
    fb.ret(e, fb.iadd(e, b, x, "r"));
  }

  auto mod  = mb.finish();
  auto code = codegen::compile(mod);

  for (std::int64_t x_in : {0, 1, 5, -3, 100, -7}) {
    auto jit = code.invoke<std::int64_t>(chain, x_in);
    CHECK(jit == interp(mod, chain, {x_in}));
    CHECK(jit == 2 * (x_in + 1) + x_in);
    // The leaves are independently callable at their own entry offsets.
    CHECK(code.invoke<std::int64_t>(inc, x_in) == x_in + 1);
    CHECK(code.invoke<std::int64_t>(dbl, x_in) == 2 * x_in);
  }
}

// A module whose functions differ in frame shape: a spill-heavy caller calling a
// self-recursive callee, checking each function's prologue/epilogue and
// callee-saved handling still holds when they share one buffer.
TEST_CASE("codegen: mixed frames in one module", "[codegen][crossfn][spill]") {
  constexpr int kN = 20;
  ir::ModuleBuilder mb;
  auto fib   = mb.create_function({ir::Type::i64}, ir::Type::i64, "fib");
  auto heavy = mb.create_function({ir::Type::i64}, ir::Type::i64, "heavy");

  {
    auto fb    = mb.function_builder(fib);
    auto entry = fb.entry_block();
    auto n     = fb.param_id(entry, 0);
    auto base  = fb.append_block({ir::Type::i64}, "base");
    auto rec   = fb.append_block({ir::Type::i64}, "rec");
    auto small = fb.icmp(entry, ir::IcmpCond::slt, n, fb.const_i64(entry, 2, "two"), "small");
    fb.branch(entry, small, base, {n}, rec, {n});
    fb.ret(base, fb.param_id(base, 0));
    auto nr = fb.param_id(rec, 0);
    auto n1 = fb.isub(rec, nr, fb.const_i64(rec, 1, "one"), "n1");
    auto f1 = fb.call(rec, fib, {n1}, "f1");
    auto n2 = fb.isub(rec, nr, fb.const_i64(rec, 2, "two2"), "n2");
    auto f2 = fb.call(rec, fib, {n2}, "f2");
    fb.ret(rec, fb.iadd(rec, f1, f2, "r"));
  }
  {
    // kN intermediates live at once (forcing spills), summed with fib(x).
    auto fb = mb.function_builder(heavy);
    auto e  = fb.entry_block();
    auto x  = fb.param_id(e, 0);
    std::vector<ir::ValueId> vs;
    for (int i = 0; i < kN; ++i) {
      auto c = fb.const_i64(e, i + 1, "c" + std::to_string(i));
      vs.push_back(fb.iadd(e, x, c, "v" + std::to_string(i)));
    }
    ir::ValueId acc = fb.call(e, fib, {x}, "f");
    for (auto v : vs) acc = fb.iadd(e, acc, v);
    fb.ret(e, acc);
  }

  auto mod  = mb.finish();
  auto code = codegen::compile(mod);

  for (std::int64_t x_in : {0, 1, 5, 10}) {
    auto jit = code.invoke<std::int64_t>(heavy, x_in);
    CHECK(jit == interp(mod, heavy, {x_in}));
    CHECK(jit == interp(mod, fib, {x_in}) +
                 static_cast<std::int64_t>(kN) * x_in +
                 static_cast<std::int64_t>(kN) * (kN + 1) / 2);
  }
}
