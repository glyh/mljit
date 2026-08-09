// ml-jit entry point — a thin shell over mljit.driver (see docs/cli.md).
import mljit.driver;

#include <iostream>
#include <string>
#include <vector>

auto main(int argc, char* argv[]) -> int {
  std::vector<std::string> args(argv + 1, argv + argc);

  std::string out;
  std::string err;
  int const code = mljit::driver::run(args, std::cin, out, err);

  std::cout << out << std::flush;
  std::cerr << err << std::flush;
  return code;
}
