module;

#include <string_view>
#include <print>

export module mljit.util;

export namespace mljit::util {

/// Print a greeting to stdout.
inline auto greet(std::string_view name) -> void {
  std::println("ml-jit :: hello, {}!", name);
}

/// Project version — available at compile time.
inline consteval auto version() -> std::string_view {
  return "0.1.0";
}

} // namespace mljit::util
