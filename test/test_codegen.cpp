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
  auto buf = codegen::compile(mod.functions[fid.value]);

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
  auto buf = codegen::compile(mod.functions[fid.value]);

  for (std::int64_t x_in : {0, 1, 5, -3, 7, 12}) {
    auto jit = buf.invoke<std::int64_t>(x_in);
    CHECK(jit == interp(mod, fid, {x_in}));
    CHECK(jit == x_in * x_in + x_in);
  }
}
