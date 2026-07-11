// ml-jit smoke test — verifies the util module API via Catch2.
import mljit.util;

#include <catch2/catch_test_macros.hpp>

TEST_CASE("util::version returns expected string", "[smoke]") {
  CHECK(mljit::util::version() == "0.1.0");
}

TEST_CASE("util::greet runs without throwing", "[smoke]") {
  CHECK_NOTHROW(mljit::util::greet("catch2"));
}

TEST_CASE("version is a non-empty string view", "[smoke]") {
  auto const v = mljit::util::version();
  CHECK_FALSE(v.empty());
  CHECK(v.size() == 5);  // "0.1.0"
}
