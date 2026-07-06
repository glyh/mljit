// ml-jit entry point — C++26 modules
import mljit.util;

#include <print>
#include <ranges>
#include <vector>
#include <expected>
#include <cstdlib>

namespace rv = std::ranges::views;

auto main(int /*argc*/, char* /*argv*/[]) -> int {
  mljit::util::greet("world");
  std::println("ml-jit v{} :: C++26 modules bootstrapped.", mljit::util::version());

  // Ranges + views demo
  const std::vector<int> nums{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto even_squares = nums
    | rv::filter([](int n) { return n % 2 == 0; })
    | rv::transform([](int n) { return n * n; });

  std::print("Even squares: ");
  for (int n : even_squares) { std::print("{} ", n); }
  std::println("");

  // C++23: std::expected
  auto safe_divide = [](int a, int b) -> std::expected<int, std::string_view> {
    if (b == 0) return std::unexpected("division by zero");
    return a / b;
  };
  if (auto r = safe_divide(10, 2)) {
    std::println("10 / 2 = {}", *r);
  }

  return EXIT_SUCCESS;
}
