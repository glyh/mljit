import mljit.driver;
import mljit.frontend;
import mljit.ir;
import mljit.ir.printer;
import mljit.regalloc;

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ── Harness ─────────────────────────────────────────────────

struct CliResult {
  int code = 0;
  std::string out;
  std::string err;
};

auto cli(std::vector<std::string> args, std::string stdin_text = {}) -> CliResult {
  std::istringstream in(std::move(stdin_text));
  CliResult result;
  result.code = mljit::driver::run(args, in, result.out, result.err);
  return result;
}

// A source file living in the temp dir for the duration of the test binary.
auto source_file(std::string_view name, std::string_view text) -> std::string {
  auto path = std::filesystem::temp_directory_path() / ("mljit-cli-" + std::string(name) + ".ml");
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.close();
  return path.string();
}

// ── Fixtures ────────────────────────────────────────────────

constexpr std::string_view kFib =
    "fun fib(n) = if n < 2 then n else fib(n - 1) + fib(n - 2)\n";

constexpr std::string_view kGcd =
    "fun gcd(a, b) = if b == 0 then a else gcd(b, a % b)\n";

constexpr std::string_view kMain = "fun main() = 6 * 7\n";

}  // namespace

// ════════════════════════════════════════════════════════════
//  Top-level dispatch
// ════════════════════════════════════════════════════════════

TEST_CASE("cli: no arguments is a usage error", "[cli]") {
  auto const r = cli({});
  CHECK(r.code == 2);
  CHECK(r.out.empty());
  CHECK(r.err.starts_with("error: missing verb\n"));
}

TEST_CASE("cli: unknown verb is a usage error", "[cli]") {
  auto const r = cli({"frobnicate", "x.ml"});
  CHECK(r.code == 2);
  CHECK(r.out.empty());
  CHECK(r.err.contains("unknown verb 'frobnicate'"));
}

TEST_CASE("cli: --version prints one line and exits 0", "[cli]") {
  auto const r = cli({"--version"});
  CHECK(r.code == 0);
  CHECK(r.out == "mljit 0.1.0\n");
  CHECK(r.err.empty());
}

TEST_CASE("cli: --help exits 0 on stdout", "[cli]") {
  auto const r = cli({"--help"});
  CHECK(r.code == 0);
  CHECK(r.out.starts_with("usage: mljit <verb>"));
  CHECK(r.err.empty());

  auto const per_verb = cli({"run", "--help"});
  CHECK(per_verb.code == 0);
  CHECK(per_verb.out.starts_with("usage: mljit run"));
}

TEST_CASE("cli: bench is specified but not implemented", "[cli]") {
  auto const r = cli({"bench", source_file("bench", kFib), "10"});
  CHECK(r.code == 2);
  CHECK(r.err.contains("bench"));
}

TEST_CASE("cli: a missing file operand is a usage error", "[cli]") {
  CHECK(cli({"check"}).code == 2);
  CHECK(cli({"run", "--backend=interp"}).code == 2);
}

TEST_CASE("cli: an unreadable file is a usage error", "[cli]") {
  auto const r = cli({"check", "/nonexistent/definitely/not/here.ml"});
  CHECK(r.code == 2);
  CHECK(r.err.contains("cannot open file"));
}

// ════════════════════════════════════════════════════════════
//  check
// ════════════════════════════════════════════════════════════

TEST_CASE("cli: check is silent on success", "[cli]") {
  auto const r = cli({"check", source_file("check-ok", kFib)});
  CHECK(r.code == 0);
  CHECK(r.out.empty());
  CHECK(r.err.empty());
}

TEST_CASE("cli: check reports frontend errors with GCC-style locations", "[cli]") {
  auto const path = source_file("check-unbound", "fun f(x) = y + 1\n");
  auto const r = cli({"check", path});
  CHECK(r.code == 1);
  CHECK(r.out.empty());
  CHECK(r.err.starts_with(path + ":1:"));
  CHECK(r.err.contains(": error: unbound variable 'y'"));
}

TEST_CASE("cli: check reports parse errors", "[cli]") {
  auto const r = cli({"check", source_file("check-parse", "fun f(x) = = =\n")});
  CHECK(r.code == 1);
  CHECK(r.err.contains(": error: "));
}

TEST_CASE("cli: check accepts stdin and cites <stdin>", "[cli]") {
  auto const ok = cli({"check", "-"}, std::string(kFib));
  CHECK(ok.code == 0);
  CHECK(ok.err.empty());

  auto const bad = cli({"check", "-"}, "fun f(x) = zzz\n");
  CHECK(bad.code == 1);
  CHECK(bad.err.starts_with("<stdin>:1:"));
}

// ════════════════════════════════════════════════════════════
//  run
// ════════════════════════════════════════════════════════════

TEST_CASE("cli: run prints a bare decimal line on stdout", "[cli]") {
  auto const path = source_file("run-fib", kFib);

  auto const jit = cli({"run", "--entry=fib", path, "10"});
  CHECK(jit.code == 0);
  CHECK(jit.out == "55\n");
  CHECK(jit.err.empty());

  auto const interp = cli({"run", "--backend=interp", "--entry=fib", path, "10"});
  CHECK(interp.code == 0);
  CHECK(interp.out == "55\n");
}

TEST_CASE("cli: run defaults to the jit backend and the main entry", "[cli]") {
  auto const r = cli({"run", source_file("run-main", kMain)});
  CHECK(r.code == 0);
  CHECK(r.out == "42\n");
}

TEST_CASE("cli: run passes positional i64 arguments by position", "[cli]") {
  auto const path = source_file("run-gcd", kGcd);
  CHECK(cli({"run", "--entry=gcd", path, "1071", "462"}).out == "21\n");
  CHECK(cli({"run", "--backend=interp", "--entry=gcd", path, "1071", "462"}).out == "21\n");
}

TEST_CASE("cli: run accepts negative i64 arguments", "[cli]") {
  auto const path = source_file("run-abs", "fun abs(x) = if x < 0 then 0 - x else x\n");
  auto const r = cli({"run", "--entry=abs", path, "-7"});
  CHECK(r.code == 0);
  CHECK(r.out == "7\n");
}

TEST_CASE("cli: run reads the source from stdin", "[cli]") {
  auto const r = cli({"run", "--entry=fib", "-", "12"}, std::string(kFib));
  CHECK(r.code == 0);
  CHECK(r.out == "144\n");
}

TEST_CASE("cli: run rejects an unknown entry as a usage error", "[cli]") {
  auto const r = cli({"run", "--entry=nope", source_file("run-entry", kFib), "1"});
  CHECK(r.code == 2);
  CHECK(r.out.empty());
  CHECK(r.err.contains("no function named 'nope'"));
}

TEST_CASE("cli: run reports an arity mismatch before executing", "[cli]") {
  auto const r = cli({"run", "--entry=fib", source_file("run-arity", kFib)});
  CHECK(r.code == 1);
  CHECK(r.out.empty());
  CHECK(r.err.contains("expects 1 argument(s), got 0"));
}

TEST_CASE("cli: run reports a runtime trap", "[cli]") {
  auto const path = source_file("run-trap", "fun main(x) = 10 / x\n");
  auto const r = cli({"run", "--backend=interp", path, "0"});
  CHECK(r.code == 1);
  CHECK(r.out.empty());
  CHECK(r.err.contains("error: runtime: "));
}

TEST_CASE("cli: run rejects a malformed i64 argument", "[cli]") {
  auto const r = cli({"run", "--entry=fib", source_file("run-badarg", kFib), "ten"});
  CHECK(r.code == 2);
  CHECK(r.err.contains("invalid i64 argument 'ten'"));
}

TEST_CASE("cli: run rejects an unknown backend and unknown flags", "[cli]") {
  auto const path = source_file("run-flags", kMain);
  CHECK(cli({"run", "--backend=llvm", path}).code == 2);
  CHECK(cli({"run", "--backend", path}).code == 2);
  CHECK(cli({"run", "--nonsense", path}).code == 2);
}

// ── JIT capability gaps surface as diagnostics, never asserts ──

TEST_CASE("cli: jit runs a cross-function call natively", "[cli]") {
  auto const path = source_file("run-crossfn",
                                "fun helper(x) = x + 1\n"
                                "fun main(x) = helper(x) * 2\n");

  auto const jit = cli({"run", path, "5"});
  CHECK(jit.code == 0);
  CHECK(jit.err.empty());
  CHECK(jit.out == "12\n");

  // Both backends agree.
  auto const interp = cli({"run", "--backend=interp", path, "5"});
  CHECK(interp.code == 0);
  CHECK(interp.out == jit.out);
}

TEST_CASE("cli: jit runs mutual recursion natively", "[cli]") {
  // is_even calls is_odd before is_odd is emitted: a forward call fixup.
  auto const path = source_file("run-mutual",
                                "fun is_even(n) = if n == 0 then 1 else is_odd(n - 1)\n"
                                "fun is_odd(n) = if n == 0 then 0 else is_even(n - 1)\n");

  auto const jit = cli({"run", "--entry=is_even", path, "7"});
  CHECK(jit.code == 0);
  CHECK(jit.out == "0\n");

  auto const interp = cli({"run", "--backend=interp", "--entry=is_even", path, "8"});
  CHECK(interp.out == cli({"run", "--entry=is_even", path, "8"}).out);
}

TEST_CASE("cli: a jit gap in any function blocks the whole module", "[cli]") {
  // main is trivially compilable, but the module is compiled as a whole, so the
  // gap in `weird` must be reported before anything runs.
  auto const path = source_file("run-modulegap",
                                "fun weird(x) = let c = x < 10; d = x + 1 in\n"
                                "  if c then d else 0 end\n"
                                "fun main(x) = x + 1\n");

  auto const jit = cli({"run", path, "5"});
  CHECK(jit.code == 1);
  CHECK(jit.out.empty());
  CHECK(jit.err.contains("error: jit: unsupported "));
  CHECK(jit.err.contains("@weird"));
  CHECK(jit.err.contains("try --backend=interp"));

  auto const interp = cli({"run", "--backend=interp", path, "5"});
  CHECK(interp.code == 0);
  CHECK(interp.out == "6\n");
}

TEST_CASE("cli: jit reports an unfusable branch condition as unsupported", "[cli]") {
  // `c` is an i1 that outlives the icmp, so the emitter cannot fuse it into
  // the branch and would need to materialize the i1 as a value.
  auto const path = source_file("run-i1",
                                "fun main(x) = let c = x < 10; d = x + 1 in\n"
                                "  if c then d else 0 end\n");

  auto const jit = cli({"run", path, "3"});
  CHECK(jit.code == 1);
  CHECK(jit.out.empty());
  CHECK(jit.err.contains("error: jit: unsupported "));
  CHECK(jit.err.contains("try --backend=interp"));

  auto const interp = cli({"run", "--backend=interp", path, "3"});
  CHECK(interp.code == 0);
  CHECK(interp.out == "4\n");
}

// ════════════════════════════════════════════════════════════
//  dump
// ════════════════════════════════════════════════════════════

TEST_CASE("cli: dump --phase=ssa emits the printer output verbatim", "[cli]") {
  auto const r = cli({"dump", "--phase=ssa", source_file("dump-ssa", kGcd)});
  CHECK(r.code == 0);
  CHECK(r.err.empty());

  auto const mod = mljit::frontend::compile(kGcd);
  REQUIRE(mod.has_value());
  CHECK(r.out == mljit::ir::printer::to_text(*mod));
}

TEST_CASE("cli: dump --phase=regalloc emits the 4-section view verbatim", "[cli]") {
  auto const r = cli({"dump", "--phase=regalloc", source_file("dump-ra", kGcd)});
  CHECK(r.code == 0);
  CHECK(r.err.empty());
  CHECK(r.out.starts_with("== intervals ==\n"));

  auto const mod = mljit::frontend::compile(kGcd);
  REQUIRE(mod.has_value());
  REQUIRE(mod->functions.size() == 1);
  CHECK(r.out == mljit::regalloc::dump_regalloc(mod->functions[0]));
}

TEST_CASE("cli: dump labels each function when the module has several", "[cli]") {
  auto const src = std::string(kFib) + std::string(kGcd);
  auto const r = cli({"dump", "--phase=regalloc", source_file("dump-multi", src)});
  CHECK(r.code == 0);
  CHECK(r.out.starts_with(";; @fib\n"));
  CHECK(r.out.contains("\n;; @gcd\n"));
}

TEST_CASE("cli: dump separates multiple phases in pipeline order", "[cli]") {
  auto const path = source_file("dump-both", kGcd);
  auto const r = cli({"dump", "--phase=regalloc", "--phase=ssa", path});
  CHECK(r.code == 0);

  auto const ssa = cli({"dump", "--phase=ssa", path});
  auto const ra = cli({"dump", "--phase=regalloc", path});
  CHECK(r.out == ";; == ssa ==\n" + ssa.out + ";; == regalloc ==\n" + ra.out);
}

TEST_CASE("cli: dump rejects unknown and missing phases", "[cli]") {
  auto const path = source_file("dump-bad", kGcd);

  auto const unknown = cli({"dump", "--phase=x64", path});
  CHECK(unknown.code == 2);
  CHECK(unknown.out.empty());
  CHECK(unknown.err.contains("unknown phase 'x64'"));

  auto const missing = cli({"dump", path});
  CHECK(missing.code == 2);
  CHECK(missing.err.contains("requires at least one --phase"));
}

TEST_CASE("cli: dump does not execute the program", "[cli]") {
  auto const r = cli({"dump", "--phase=ssa", source_file("dump-noexec", "fun main() = 1 / 0\n")});
  CHECK(r.code == 0);
  CHECK(r.out.contains("idiv"));
}

TEST_CASE("cli: dump reports verifier and frontend errors on stderr", "[cli]") {
  auto const r = cli({"dump", "--phase=ssa", source_file("dump-err", "fun f(x) = q\n")});
  CHECK(r.code == 1);
  CHECK(r.out.empty());
  CHECK(r.err.contains("error: unbound variable 'q'"));
}
