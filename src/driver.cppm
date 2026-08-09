module;

// ─── Global includes ────
#include <charconv>
#include <cstdint>
#include <expected>
#include <format>
#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

export module mljit.driver;

import mljit.ir;
import mljit.ir.printer;
import mljit.ir.verifier;
import mljit.ir.runtime;
import mljit.frontend;
import mljit.regalloc;
import mljit.codegen;
import mljit.x64;

// ════════════════════════════════════════════════════════════════
//  mljit.driver — the CLI driver behind `main`.
//
//  Implements the contract in docs/cli.md: hand-rolled subcommand and
//  flag dispatch (no CLI library), the `run` / `dump` / `check` verbs,
//  GCC-style stderr diagnostics, and 0/1/2 exit codes.  `run` never
//  asserts on a JIT capability gap: a pre-emission scan turns each known
//  gap into an `error: jit: unsupported ...` diagnostic.
//
//  The driver is pure with respect to the process: it writes into caller
//  supplied strings and reads stdin from a caller-supplied stream, so the
//  whole surface is unit-testable without spawning a process.
// ════════════════════════════════════════════════════════════════

export namespace mljit::driver {

// ── Exit codes (docs/cli.md) ────────────────────────────────
enum class ExitCode : int {
  Ok = 0,            // success
  ProgramError = 1,  // the program is at fault
  UsageError = 2,    // the invocation is at fault
};

[[nodiscard]] constexpr auto to_int(ExitCode code) -> int {
  return static_cast<int>(code);
}

// ── Parsed invocation ───────────────────────────────────────
enum class Verb { Run, Dump, Check, Bench };

enum class Backend { Jit, Interp };

// Phase vocabulary of `dump`, declared in pipeline order.
enum class Phase { Ssa, Regalloc };

enum class UsageErrorKind {
  NoVerb,
  UnknownVerb,
  UnimplementedVerb,
  UnknownFlag,
  MalformedFlag,
  UnknownPhase,
  NoPhase,
  UnknownBackend,
  MissingFile,
  UnreadableFile,
  ExtraOperand,
  MalformedArgument,
  UnknownEntry,
};

struct UsageError {
  UsageErrorKind kind;
  std::string message;
};

// The whole CLI. `args` excludes argv[0]; `in` supplies the source when the
// file operand is `-`. stdout/stderr text is appended to `out` / `err` so
// tests can assert on bytes. Returns the process exit code.
[[nodiscard]] auto run(std::vector<std::string> const& args, std::istream& in,
                       std::string& out, std::string& err) -> int;

}  // namespace mljit::driver

// ════════════════════════════════════════════════════════════════
//  Implementation
// ════════════════════════════════════════════════════════════════

module : private;

namespace mljit::driver {
namespace {

constexpr std::string_view kVersion = "mljit 0.1.0";

constexpr std::string_view kUsage =
    "usage: mljit <verb> [flags] <file> [args...]\n"
    "\n"
    "verbs:\n"
    "  run    [--backend=jit|interp] [--entry=<name>] <file> [i64 args...]\n"
    "  dump   --phase=ssa|regalloc [--phase=...] <file>\n"
    "  check  <file>\n"
    "  bench  (reserved; not implemented yet)\n"
    "\n"
    "  <file> is a source file, or `-` to read the source from stdin.\n"
    "\n"
    "other:\n"
    "  --help     print this message\n"
    "  --version  print the version line\n";

constexpr std::string_view kRunUsage =
    "usage: mljit run [--backend=jit|interp] [--entry=<name>] <file> [i64 args...]\n";
constexpr std::string_view kDumpUsage =
    "usage: mljit dump --phase=ssa|regalloc [--phase=...] <file>\n";
constexpr std::string_view kCheckUsage = "usage: mljit check <file>\n";

// ── Small text helpers ──────────────────────────────────────

// `--name=value` -> value, when the token names this flag.
[[nodiscard]] auto flag_value(std::string_view token, std::string_view name)
    -> std::optional<std::string_view> {
  if (!token.starts_with(name)) return std::nullopt;
  auto const rest = token.substr(name.size());
  if (!rest.starts_with("=")) return std::nullopt;
  return rest.substr(1);
}

[[nodiscard]] auto parse_i64(std::string_view text) -> std::optional<std::int64_t> {
  std::int64_t value = 0;
  auto const* const begin = text.data();
  auto const* const end = begin + text.size();
  auto const [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return value;
}

[[nodiscard]] auto phase_name(Phase phase) -> std::string_view {
  switch (phase) {
    case Phase::Ssa: return "ssa";
    case Phase::Regalloc: return "regalloc";
  }
  return "?";
}

// ── IR labels for diagnostics (mirrors the printer's naming) ─

[[nodiscard]] auto func_label(ir::Module const& mod, ir::FunctionId fid) -> std::string {
  auto const& fn = mod.functions[fid.value];
  if (fn.debug_name) return *fn.debug_name;
  return "f" + std::to_string(fid.value);
}

[[nodiscard]] auto block_label(ir::Function const& fn, ir::BlockId bid) -> std::string {
  auto const& blk = fn.blocks[bid.value];
  if (blk.debug_name) return *blk.debug_name;
  return "bb" + std::to_string(bid.value);
}

// ── Invocation ──────────────────────────────────────────────

struct Invocation {
  Verb verb = Verb::Run;
  std::string file;                    // as written; `-` means stdin
  std::vector<std::int64_t> arguments; // run: positional i64 operands
  Backend backend = Backend::Jit;
  std::string entry = "main";
  std::vector<Phase> phases;           // dump: deduped, pipeline order
  bool help = false;                   // per-verb --help
};

[[nodiscard]] auto usage_error(UsageErrorKind kind, std::string message)
    -> std::unexpected<UsageError> {
  return std::unexpected(UsageError{kind, std::move(message)});
}

[[nodiscard]] auto parse_verb(std::string_view token) -> std::optional<Verb> {
  if (token == "run") return Verb::Run;
  if (token == "dump") return Verb::Dump;
  if (token == "check") return Verb::Check;
  if (token == "bench") return Verb::Bench;
  return std::nullopt;
}

// Hand-rolled dispatch. Flags precede the file operand; everything after it is
// a positional operand, so negative i64 arguments never look like flags.
[[nodiscard]] auto parse(std::vector<std::string> const& args)
    -> std::expected<Invocation, UsageError> {
  if (args.empty())
    return usage_error(UsageErrorKind::NoVerb, "missing verb");

  Invocation inv;
  auto const maybe_verb = parse_verb(args[0]);
  if (!maybe_verb)
    return usage_error(UsageErrorKind::UnknownVerb,
                       std::format("unknown verb '{}'; expected one of: run, dump, check, bench",
                                   args[0]));
  inv.verb = *maybe_verb;

  // `bench` is part of the contract's vocabulary but lands with the benchmark
  // milestone; reject it up front so the message never depends on its flags.
  if (inv.verb == Verb::Bench)
    return usage_error(UsageErrorKind::UnimplementedVerb,
                       "verb 'bench' is specified in docs/cli.md but not implemented yet");

  bool have_file = false;
  bool phase_ssa = false;
  bool phase_regalloc = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    std::string_view const token = args[i];

    if (!have_file && token.size() > 1 && token.starts_with("--")) {
      if (token == "--help") {
        inv.help = true;
        return inv;
      }
      if (inv.verb == Verb::Run) {
        if (auto v = flag_value(token, "--backend")) {
          if (*v == "jit") inv.backend = Backend::Jit;
          else if (*v == "interp") inv.backend = Backend::Interp;
          else
            return usage_error(UsageErrorKind::UnknownBackend,
                               std::format("unknown backend '{}'; expected jit or interp", *v));
          continue;
        }
        if (auto v = flag_value(token, "--entry")) {
          if (v->empty())
            return usage_error(UsageErrorKind::MalformedFlag, "--entry requires a function name");
          inv.entry = std::string(*v);
          continue;
        }
        if (token == "--backend" || token == "--entry")
          return usage_error(UsageErrorKind::MalformedFlag,
                             std::format("{} requires a value, as {}=<value>", token, token));
      }
      if (inv.verb == Verb::Dump) {
        if (auto v = flag_value(token, "--phase")) {
          if (*v == "ssa") phase_ssa = true;
          else if (*v == "regalloc") phase_regalloc = true;
          else
            return usage_error(
                UsageErrorKind::UnknownPhase,
                std::format("unknown phase '{}'; expected one of: ssa, regalloc", *v));
          continue;
        }
        if (token == "--phase")
          return usage_error(UsageErrorKind::MalformedFlag,
                             "--phase requires a value, as --phase=<name>");
      }
      return usage_error(UsageErrorKind::UnknownFlag,
                         std::format("unknown flag '{}' for verb '{}'", token, args[0]));
    }

    if (!have_file) {
      inv.file = std::string(token);
      have_file = true;
      continue;
    }

    if (inv.verb != Verb::Run)
      return usage_error(UsageErrorKind::ExtraOperand,
                         std::format("unexpected operand '{}' for verb '{}'", token, args[0]));

    auto const value = parse_i64(token);
    if (!value)
      return usage_error(UsageErrorKind::MalformedArgument,
                         std::format("invalid i64 argument '{}'", token));
    inv.arguments.push_back(*value);
  }

  if (!have_file)
    return usage_error(UsageErrorKind::MissingFile,
                       std::format("missing <file> operand for verb '{}'", args[0]));

  if (inv.verb == Verb::Dump) {
    if (phase_ssa) inv.phases.push_back(Phase::Ssa);
    if (phase_regalloc) inv.phases.push_back(Phase::Regalloc);
    if (inv.phases.empty())
      return usage_error(UsageErrorKind::NoPhase,
                         "dump requires at least one --phase=<name>; expected one of: ssa, regalloc");
  }

  return inv;
}

// ── Source loading ──────────────────────────────────────────

struct Source {
  std::string name;  // diagnostic prefix: the path, or <stdin>
  std::string text;
};

[[nodiscard]] auto load_source(std::string const& file, std::istream& in)
    -> std::expected<Source, UsageError> {
  if (file == "-")
    return Source{"<stdin>", std::string(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>())};

  std::ifstream stream(file, std::ios::binary);
  if (!stream)
    return std::unexpected(
        UsageError{UsageErrorKind::UnreadableFile, std::format("cannot open file '{}'", file)});
  return Source{file, std::string(std::istreambuf_iterator<char>(stream),
                                  std::istreambuf_iterator<char>())};
}

// ── Front half of the pipeline: parse + verify, all errors reported ──

[[nodiscard]] auto compile_and_verify(Source const& src, std::string& err)
    -> std::optional<ir::Module> {
  auto compiled = frontend::compile(src.text);
  if (!compiled) {
    for (auto const& e : compiled.error())
      // ANTLR reports 0-based columns; diagnostics are 1-based like GCC.
      err += std::format("{}:{}:{}: error: {}\n", src.name, e.line, e.column + 1, e.detail);
    return std::nullopt;
  }

  auto const result = ir::verifier::verify(*compiled);
  if (!result.ok()) {
    for (auto const& e : result.errors) {
      auto where = "@" + func_label(*compiled, e.function);
      if (e.block) where += "/^" + block_label(compiled->functions[e.function.value], *e.block);
      err += std::format("{}: verify error in {}: {}\n", src.name, where, e.detail);
    }
    return std::nullopt;
  }

  return std::move(*compiled);
}

[[nodiscard]] auto find_function(ir::Module const& mod, std::string_view name)
    -> std::optional<ir::FunctionId> {
  for (std::uint32_t i = 0; i < mod.functions.size(); ++i)
    if (mod.functions[i].debug_name == name) return ir::FunctionId{i};
  return std::nullopt;
}

// ── JIT capability scan ─────────────────────────────────────
//
// The x64 emitter asserts on constructs it cannot lower yet. The contract
// requires a clean diagnostic instead, so scan the function first and report
// the first gap found. This detects gaps; it does not close them.

// System V passes at most 6 integer arguments in registers, which is all the
// emitter's ABI lowering knows.
constexpr std::size_t kMaxAbiArgs = 6;

[[nodiscard]] auto jit_gap(ir::Module const& mod, ir::FunctionId fid) -> std::optional<std::string> {
  auto const& fn = mod.functions[fid.value];

  if (fn.blocks.empty()) return std::string("function without an entry block");
  if (fn.blocks[0].params.size() > kMaxAbiArgs)
    return std::format("function with more than {} parameters", kMaxAbiArgs);

  // Defining instruction per value; block parameters map to nullptr.
  std::vector<ir::Instruction const*> def(fn.next_value_id, nullptr);
  for (auto const& inst : fn.instructions) def[inst.result_id.value] = &inst;

  auto is_i1 = [&](ir::ValueId v) {
    auto const* d = def[v.value];
    return d != nullptr && std::holds_alternative<ir::ICmp>(d->payload);
  };

  std::optional<std::string> gap;
  auto report = [&](std::string message) {
    if (!gap) gap = std::move(message);
  };
  auto check_operands = [&](std::string_view what, std::initializer_list<ir::ValueId> vs) {
    for (auto v : vs)
      if (is_i1(v))
        report(std::format("i1 value used as an operand of {} "
                           "(an icmp result can only feed a branch)",
                           what));
  };

  for (auto const& inst : fn.instructions) {
    std::visit(ir::overload{
      [&](ir::IAdd const& o) { check_operands("iadd", {o.lhs, o.rhs}); },
      [&](ir::ISub const& o) { check_operands("isub", {o.lhs, o.rhs}); },
      [&](ir::IMul const& o) { check_operands("imul", {o.lhs, o.rhs}); },
      [&](ir::IDiv const& o) { check_operands("idiv", {o.lhs, o.rhs}); },
      [&](ir::IRem const& o) { check_operands("irem", {o.lhs, o.rhs}); },
      [&](ir::ICmp const& o) { check_operands("icmp", {o.lhs, o.rhs}); },
      [&](ir::Call const& o) {
        if (o.callee != fid)
          report(std::format("cross-function call to @{} "
                             "(only self-recursive calls are lowered)",
                             func_label(mod, o.callee)));
        if (o.args.size() > kMaxAbiArgs)
          report(std::format("call with more than {} arguments", kMaxAbiArgs));
        for (auto v : o.args) check_operands("a call", {v});
      },
      [&](ir::ConstI64 const&) {},
    }, inst.payload);
  }

  for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi) {
    auto const& blk = fn.blocks[bi];
    if (!blk.terminator) continue;
    std::visit(ir::overload{
      [&](ir::Ret const& t) { check_operands("ret", {t.value}); },
      [&](ir::Jump const& t) { for (auto v : t.args) check_operands("a jump argument", {v}); },
      [&](ir::Branch const& t) {
        for (auto v : t.true_args) check_operands("a branch argument", {v});
        for (auto v : t.false_args) check_operands("a branch argument", {v});
        // The emitter fuses icmp+branch, so the condition must be produced by
        // the block's last instruction (its flags must still be live).
        bool const fusable =
            !blk.instructions.empty() &&
            fn.instructions[blk.instructions.back().value].result_id == t.cond && is_i1(t.cond);
        if (!fusable)
          report(std::format("branch in ^{} whose condition is not the immediately "
                             "preceding icmp (an i1 value cannot be materialized yet)",
                             block_label(fn, ir::BlockId{static_cast<std::uint32_t>(bi)})));
      },
    }, *blk.terminator);
  }

  return gap;
}

[[nodiscard]] auto invoke_jit(x64::ExecBuffer const& buf, std::vector<std::int64_t> const& a)
    -> std::int64_t {
  switch (a.size()) {
    case 0: return buf.invoke<std::int64_t>();
    case 1: return buf.invoke<std::int64_t>(a[0]);
    case 2: return buf.invoke<std::int64_t>(a[0], a[1]);
    case 3: return buf.invoke<std::int64_t>(a[0], a[1], a[2]);
    case 4: return buf.invoke<std::int64_t>(a[0], a[1], a[2], a[3]);
    case 5: return buf.invoke<std::int64_t>(a[0], a[1], a[2], a[3], a[4]);
    default: return buf.invoke<std::int64_t>(a[0], a[1], a[2], a[3], a[4], a[5]);
  }
}

// ── Verbs ───────────────────────────────────────────────────

[[nodiscard]] auto verb_check(Source const& src, std::string& err) -> ExitCode {
  return compile_and_verify(src, err) ? ExitCode::Ok : ExitCode::ProgramError;
}

[[nodiscard]] auto verb_dump(Invocation const& inv, Source const& src, std::string& out,
                             std::string& err) -> ExitCode {
  auto mod = compile_and_verify(src, err);
  if (!mod) return ExitCode::ProgramError;

  bool const separate = inv.phases.size() > 1;
  for (auto phase : inv.phases) {
    if (separate) out += std::format(";; == {} ==\n", phase_name(phase));
    switch (phase) {
      case Phase::Ssa:
        out += ir::printer::to_text(*mod);
        break;
      case Phase::Regalloc:
        // dump_regalloc is per function; label the sections when a module has
        // more than one, so the single-function dump stays verbatim.
        for (std::uint32_t i = 0; i < mod->functions.size(); ++i) {
          if (mod->functions.size() > 1)
            out += std::format(";; @{}\n", func_label(*mod, ir::FunctionId{i}));
          out += regalloc::dump_regalloc(mod->functions[i]);
        }
        break;
    }
  }
  return ExitCode::Ok;
}

[[nodiscard]] auto verb_run(Invocation const& inv, Source const& src, std::string& out,
                            std::string& err) -> ExitCode {
  auto mod = compile_and_verify(src, err);
  if (!mod) return ExitCode::ProgramError;

  auto const fid = find_function(*mod, inv.entry);
  if (!fid) {
    err += std::format("error: no function named '{}' in {}\n", inv.entry, src.name);
    return ExitCode::UsageError;
  }

  auto const& fn = mod->functions[fid->value];
  auto const arity = fn.blocks.empty() ? std::size_t{0} : fn.blocks[0].params.size();
  if (arity != inv.arguments.size()) {
    err += std::format("{}: error: @{} expects {} argument(s), got {}\n", src.name, inv.entry,
                       arity, inv.arguments.size());
    return ExitCode::ProgramError;
  }

  if (inv.backend == Backend::Interp) {
    ir::runtime::Interpreter interpreter(*mod);
    auto const result = interpreter.run(*fid, inv.arguments);
    if (!result) {
      err += std::format("{}: error: runtime: {}\n", src.name, result.error().error.detail);
      return ExitCode::ProgramError;
    }
    out += std::format("{}\n", result->value);
    return ExitCode::Ok;
  }

  if (auto const gap = jit_gap(*mod, *fid)) {
    err += std::format("{}: error: jit: unsupported {}; try --backend=interp\n", src.name, *gap);
    return ExitCode::ProgramError;
  }

  auto const buf = codegen::compile(*mod, *fid);
  out += std::format("{}\n", invoke_jit(buf, inv.arguments));
  return ExitCode::Ok;
}

[[nodiscard]] auto verb_usage(Verb verb) -> std::string_view {
  switch (verb) {
    case Verb::Run: return kRunUsage;
    case Verb::Dump: return kDumpUsage;
    case Verb::Check: return kCheckUsage;
    case Verb::Bench: return kUsage;
  }
  return kUsage;
}

}  // namespace

auto run(std::vector<std::string> const& args, std::istream& in, std::string& out,
         std::string& err) -> int {
  if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
    out += kUsage;
    return to_int(ExitCode::Ok);
  }
  if (args.size() == 1 && args[0] == "--version") {
    out += std::format("{}\n", kVersion);
    return to_int(ExitCode::Ok);
  }

  auto const parsed = parse(args);
  if (!parsed) {
    err += std::format("error: {}\n", parsed.error().message);
    err += kUsage;
    return to_int(ExitCode::UsageError);
  }
  if (parsed->help) {
    out += verb_usage(parsed->verb);
    return to_int(ExitCode::Ok);
  }

  auto const src = load_source(parsed->file, in);
  if (!src) {
    err += std::format("error: {}\n", src.error().message);
    return to_int(ExitCode::UsageError);
  }

  switch (parsed->verb) {
    case Verb::Check: return to_int(verb_check(*src, err));
    case Verb::Dump: return to_int(verb_dump(*parsed, *src, out, err));
    case Verb::Run: return to_int(verb_run(*parsed, *src, out, err));
    case Verb::Bench: break;  // rejected during parsing
  }
  return to_int(ExitCode::UsageError);
}

}  // namespace mljit::driver
