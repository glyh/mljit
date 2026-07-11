// ml-jit entry point — C++26 modules
import mljit.ir;
import mljit.ir.dump;

#include <print>
#include <cstdlib>

auto main(int /*argc*/, char* /*argv*/[]) -> int {
  std::println("ml-jit: SSA IR package bootstrapped.");
  return EXIT_SUCCESS;
}
