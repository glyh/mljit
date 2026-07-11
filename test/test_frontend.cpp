import mljit.frontend;
import mljit.ir;
import mljit.ir.verifier;
import mljit.ir.runtime;

#include <catch2/catch_test_macros.hpp>

namespace {

auto compile_and_run(std::string_view source, std::string_view fn_name,
                     std::vector<std::int64_t> args) -> int64_t {
  auto mod = mljit::frontend::compile(source);
  REQUIRE(mod.has_value());
  if (!mljit::ir::verifier::verify(*mod).ok()) {
    FAIL("verification failed: " << mljit::ir::verifier::verify(*mod).errors[0].detail);
  }
  std::optional<mljit::ir::FunctionId> fid;
  for (uint32_t i = 0; i < mod->functions.size(); ++i) {
    if (mod->functions[i].debug_name == fn_name) {
      fid = mljit::ir::FunctionId{i};
      break;
    }
  }
  REQUIRE(fid.has_value());
  mljit::ir::runtime::Interpreter interp(*mod);
  auto res = interp.run(*fid, args);
  REQUIRE(res.has_value());
  return res->value;
}

} // namespace

TEST_CASE("frontend: add1", "[frontend]") {
  auto src = "fun add1(x) = x + 1";
  CHECK(compile_and_run(src, "add1", {41}) == 42);
  CHECK(compile_and_run(src, "add1", {-5}) == -4);
}

TEST_CASE("frontend: abs", "[frontend]") {
  auto src = "fun abs(x) = if x < 0 then 0 - x else x";
  CHECK(compile_and_run(src, "abs", {-7}) == 7);
  CHECK(compile_and_run(src, "abs", {5}) == 5);
  CHECK(compile_and_run(src, "abs", {0}) == 0);
}

TEST_CASE("frontend: subtraction", "[frontend]") {
  auto src = "fun sub(x, y) = x - y";
  CHECK(compile_and_run(src, "sub", {10, 3}) == 7);
}

TEST_CASE("frontend: multiplication and division", "[frontend]") {
  auto src = R"(
    fun mul(x, y) = x * y
    fun div(x, y) = x / y
    fun rem(x, y) = x % y
  )";
  CHECK(compile_and_run(src, "mul", {6, 7}) == 42);
  CHECK(compile_and_run(src, "div", {42, 6}) == 7);
  CHECK(compile_and_run(src, "rem", {17, 5}) == 2);
}

TEST_CASE("frontend: comparisons", "[frontend]") {
  auto src = R"(
    fun lt(x, y)  = if x < y  then 1 else 0
    fun le(x, y)  = if x <= y then 1 else 0
    fun gt(x, y)  = if x > y  then 1 else 0
    fun ge(x, y)  = if x >= y then 1 else 0
    fun eq(x, y)  = if x == y then 1 else 0
    fun ne(x, y)  = if x != y then 1 else 0
  )";
  CHECK(compile_and_run(src, "lt", {3, 5}) == 1);
  CHECK(compile_and_run(src, "lt", {5, 3}) == 0);
  CHECK(compile_and_run(src, "le", {3, 3}) == 1);
  CHECK(compile_and_run(src, "gt", {7, 2}) == 1);
  CHECK(compile_and_run(src, "ge", {2, 2}) == 1);
  CHECK(compile_and_run(src, "eq", {4, 4}) == 1);
  CHECK(compile_and_run(src, "eq", {4, 5}) == 0);
  CHECK(compile_and_run(src, "ne", {4, 5}) == 1);
}

TEST_CASE("frontend: let binding", "[frontend]") {
  auto src = R"(
    fun foo(x) =
      let
        y = x + 1;
        z = y * 2
      in
        z + 3
      end
  )";
  CHECK(compile_and_run(src, "foo", {5}) == 15);  // (5+1)*2 + 3 = 15
}

TEST_CASE("frontend: fib(10)", "[frontend]") {
  auto src = R"(
    fun fib(n) =
      if n < 2 then n
      else fib(n - 1) + fib(n - 2)
  )";
  CHECK(compile_and_run(src, "fib", {10}) == 55);
  CHECK(compile_and_run(src, "fib", {0}) == 0);
  CHECK(compile_and_run(src, "fib", {1}) == 1);
}

TEST_CASE("frontend: gcd", "[frontend]") {
  auto src = R"(
    fun gcd(a, b) =
      if b == 0 then a
      else gcd(b, a % b)
  )";
  CHECK(compile_and_run(src, "gcd", {48, 18}) == 6);
  CHECK(compile_and_run(src, "gcd", {7, 13}) == 1);
}

TEST_CASE("frontend: negation", "[frontend]") {
  auto src = "fun neg(x) = 0 - x";
  CHECK(compile_and_run(src, "neg", {42}) == -42);
  CHECK(compile_and_run(src, "neg", {-5}) == 5);
}

TEST_CASE("frontend: parentheses", "[frontend]") {
  auto src = "fun paren(x, y) = (x + y) * (x - y)";
  CHECK(compile_and_run(src, "paren", {5, 3}) == 16); // (5+3)*(5-3) = 8*2 = 16
}

TEST_CASE("frontend: parse error", "[frontend]") {
  auto src = "fun bad(x) = x + ";
  auto mod = mljit::frontend::compile(src);
  REQUIRE(!mod.has_value());
  REQUIRE(!mod.error().empty());
}

TEST_CASE("frontend: unbound variable", "[frontend]") {
  auto src = "fun bad(x) = y + 1";
  auto mod = mljit::frontend::compile(src);
  REQUIRE(!mod.has_value());
  REQUIRE(!mod.error().empty());
}
