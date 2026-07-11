module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include <cassert>
#include <climits>

export module mljit.ir.runtime;

import mljit.ir;

export namespace mljit::ir::runtime {

using RuntimeWord = std::int64_t;

struct RunOptions {
  std::optional<std::uint64_t> max_steps = 1'000'000;
  std::optional<std::uint32_t> max_call_depth = 4096;
};

struct RunStats {
  std::uint64_t steps_executed = 0;
  std::uint32_t max_call_depth_observed = 0;
};

enum class RuntimeErrorKind {
  InvalidEntryFunction,
  ArgumentCountMismatch,
  ArithmeticOverflow,
  DivisionByZero,
  DivisionOverflow,
  StepLimitExceeded,
  CallDepthExceeded,
};

struct RuntimeError {
  RuntimeErrorKind kind;
  FunctionId       function;
  std::optional<BlockId>       block;
  std::optional<InstructionId> instruction;
  std::string detail;
};

struct RunSuccess {
  RuntimeWord value = 0;
  RunStats    stats;
};

struct RunFailure {
  RuntimeError error;
  RunStats     stats;
};

using RunResult = std::expected<RunSuccess, RunFailure>;

class Interpreter {
public:
  explicit Interpreter(Module const& module);

  [[nodiscard]] auto run(
    FunctionId entry,
    std::vector<RuntimeWord> const& args,
    RunOptions options = {}
  ) const -> RunResult;

private:
  struct StackFrame {
    FunctionId fid;
    BlockId     current_block;
    size_t      next_inst_idx = 0; // next instruction to execute in current block
    std::optional<ValueId> return_target;
    std::vector<RuntimeWord> env;
  };

  Module const& module_;
  std::vector<Function const*> func_table_;
};

// ═════════════════════════════════════════════════════════════
//  Implementation
// ═════════════════════════════════════════════════════════════

inline Interpreter::Interpreter(Module const& module)
  : module_(module)
{
  func_table_.resize(module_.functions.size());
  for (size_t i = 0; i < module_.functions.size(); ++i)
    func_table_[i] = &module_.functions[i];
}

inline auto Interpreter::run(
  FunctionId entry,
  std::vector<RuntimeWord> const& args,
  RunOptions options
) const -> RunResult {
  // ── Validate entry function ─────────────────────────────
  if (entry.value >= func_table_.size())
    return std::unexpected(RunFailure{
      RuntimeError{RuntimeErrorKind::InvalidEntryFunction, entry, std::nullopt, std::nullopt,
        "FunctionId " + std::to_string(entry.value) + " out of range"},
      RunStats{}
    });

  auto const* entry_func = func_table_[entry.value];
  if (entry_func->blocks.empty())
    return std::unexpected(RunFailure{
      RuntimeError{RuntimeErrorKind::InvalidEntryFunction, entry, std::nullopt, std::nullopt,
        "entry function has no blocks"},
      RunStats{}
    });

  auto const& entry_block = entry_func->blocks[0];
  if (args.size() != entry_block.params.size())
    return std::unexpected(RunFailure{
      RuntimeError{RuntimeErrorKind::ArgumentCountMismatch, entry, std::nullopt, std::nullopt,
        "expected " + std::to_string(entry_block.params.size()) + " args, got " + std::to_string(args.size())},
      RunStats{}
    });

  // ── Initialize first frame ──────────────────────────────
  std::vector<StackFrame> call_stack;
  call_stack.push_back(StackFrame{
    .fid = entry,
    .current_block = BlockId{0},
    .return_target = std::nullopt,
    .env = std::vector<RuntimeWord>(entry_func->next_value_id, 0)
  });

  // Bind entry params
  for (size_t i = 0; i < args.size(); ++i)
    call_stack.back().env[entry_block.params[i].id.value] = args[i];

  // ── Helper: bind block params from terminator args ──────
  // Snapshot all source values first, then write targets, so that
  // overlapping source/target slots (e.g. self-edge with permuted args)
  // don't cause lost copies.
  auto bind_block_params = [](StackFrame& frame, Function const& func, BlockId bid,
                               std::vector<ValueId> const& arg_ids)
  {
    auto const& blk = func.blocks[bid.value];
    std::vector<RuntimeWord> vals(blk.params.size());
    for (size_t i = 0; i < blk.params.size(); ++i)
      vals[i] = frame.env[arg_ids[i].value];
    for (size_t i = 0; i < blk.params.size(); ++i)
      frame.env[blk.params[i].id.value] = vals[i];
  };

  // ── Execution state ─────────────────────────────────────
  std::uint64_t steps = 0;
  std::uint32_t max_depth = 0;

  while (!call_stack.empty()) {
    // ── Re-acquire frame pointers (might change after push/pop) ──
    auto& frame = call_stack.back();
    auto const* func = func_table_[frame.fid.value];
    auto const& blk = func->blocks[frame.current_block.value];

    // Step limit check
    if (options.max_steps && steps >= *options.max_steps)
      return std::unexpected(RunFailure{
        RuntimeError{RuntimeErrorKind::StepLimitExceeded, frame.fid, frame.current_block, std::nullopt,
          "step limit reached"},
        RunStats{steps, max_depth}
      });

    // ── Execute block instructions ─────────────────────────
    bool call_dispatched = false;
    for (; frame.next_inst_idx < blk.instructions.size(); ++frame.next_inst_idx) {
      auto iid = blk.instructions[frame.next_inst_idx];
      if (iid.value >= func->instructions.size()) continue;
      auto const& inst = func->instructions[iid.value];

      if (options.max_steps && steps >= *options.max_steps)
        return std::unexpected(RunFailure{
          RuntimeError{RuntimeErrorKind::StepLimitExceeded, frame.fid, frame.current_block, iid,
            "step limit reached"},
          RunStats{steps, max_depth}
        });

      // ── Handle Call ──────────────────────────────────────
      auto const* call_ptr = std::get_if<Call>(&inst.payload);
      if (call_ptr) {
        auto const& callee_func = *func_table_[call_ptr->callee.value];
        if (callee_func.blocks.empty())
          return std::unexpected(RunFailure{
            RuntimeError{RuntimeErrorKind::InvalidEntryFunction, call_ptr->callee, std::nullopt, std::nullopt,
              "callee has no blocks"},
            RunStats{steps, max_depth}
          });

        if (options.max_call_depth && call_stack.size() >= *options.max_call_depth)
          return std::unexpected(RunFailure{
            RuntimeError{RuntimeErrorKind::CallDepthExceeded, frame.fid, frame.current_block, iid,
              "max call depth exceeded"},
            RunStats{steps, max_depth}
          });

        // Evaluate args in caller's env
        std::vector<RuntimeWord> call_args;
        call_args.reserve(call_ptr->args.size());
        for (auto arg_id : call_ptr->args)
          call_args.push_back(frame.env[arg_id.value]);

        // Push callee frame
        StackFrame callee_frame{
          .fid = call_ptr->callee,
          .current_block = BlockId{0},
          .return_target = inst.result_id,
          .env = std::vector<RuntimeWord>(callee_func.next_value_id, 0)
        };
        auto const& callee_entry = callee_func.blocks[0];
        for (size_t i = 0; i < callee_entry.params.size(); ++i)
          callee_frame.env[callee_entry.params[i].id.value] = call_args[i];

        ++frame.next_inst_idx; // advance past call (frame still valid before push)
        ++steps;               // count the call step before push (frame may dangle after)
        call_stack.push_back(std::move(callee_frame));
        if (call_stack.size() > max_depth) max_depth = static_cast<std::uint32_t>(call_stack.size());
        call_dispatched = true;
        break; // exit instruction loop — skip rest of block for now
      }

      // ── Non-call instructions ────────────────────────────
      auto const& payload = inst.payload;
      auto err = std::visit(overload{
        [&](ConstI64 const& op) -> std::optional<RuntimeError> {
          frame.env[inst.result_id.value] = op.value;
          return std::nullopt;
        },
        [&](IAdd const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          RuntimeWord result;
          if (__builtin_saddl_overflow(lhs, rhs, &result))
            return RuntimeError{RuntimeErrorKind::ArithmeticOverflow, frame.fid, frame.current_block, iid, "iadd overflow"};
          frame.env[inst.result_id.value] = result;
          return std::nullopt;
        },
        [&](ISub const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          RuntimeWord result;
          if (__builtin_ssubl_overflow(lhs, rhs, &result))
            return RuntimeError{RuntimeErrorKind::ArithmeticOverflow, frame.fid, frame.current_block, iid, "isub overflow"};
          frame.env[inst.result_id.value] = result;
          return std::nullopt;
        },
        [&](IMul const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          RuntimeWord result;
          if (__builtin_smull_overflow(lhs, rhs, &result))
            return RuntimeError{RuntimeErrorKind::ArithmeticOverflow, frame.fid, frame.current_block, iid, "imul overflow"};
          frame.env[inst.result_id.value] = result;
          return std::nullopt;
        },
        [&](IDiv const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          if (rhs == 0)
            return RuntimeError{RuntimeErrorKind::DivisionByZero, frame.fid, frame.current_block, iid, "idiv by zero"};
          if (lhs == INT64_MIN && rhs == -1)
            return RuntimeError{RuntimeErrorKind::DivisionOverflow, frame.fid, frame.current_block, iid, "idiv overflow"};
          frame.env[inst.result_id.value] = lhs / rhs;
          return std::nullopt;
        },
        [&](IRem const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          if (rhs == 0)
            return RuntimeError{RuntimeErrorKind::DivisionByZero, frame.fid, frame.current_block, iid, "irem by zero"};
          if (lhs == INT64_MIN && rhs == -1)
            return RuntimeError{RuntimeErrorKind::DivisionOverflow, frame.fid, frame.current_block, iid, "irem overflow"};
          frame.env[inst.result_id.value] = lhs % rhs;
          return std::nullopt;
        },
        [&](ICmp const& op) -> std::optional<RuntimeError> {
          RuntimeWord lhs = frame.env[op.lhs.value];
          RuntimeWord rhs = frame.env[op.rhs.value];
          bool result;
          switch (op.cond) {
            case IcmpCond::eq:  result = (lhs == rhs); break;
            case IcmpCond::ne:  result = (lhs != rhs); break;
            case IcmpCond::slt: result = (lhs < rhs);  break;
            case IcmpCond::sle: result = (lhs <= rhs); break;
            case IcmpCond::sgt: result = (lhs > rhs);  break;
            case IcmpCond::sge: result = (lhs >= rhs); break;
            default: result = false; break;
          }
          frame.env[inst.result_id.value] = result ? 1 : 0;
          return std::nullopt;
        },
        [&](Call const&) -> std::optional<RuntimeError> {
          return std::nullopt; // handled above
        },
      }, payload);

      if (err.has_value())
        return std::unexpected(RunFailure{*err, RunStats{steps, max_depth}});

      ++steps;
    }

    // If a call was dispatched, skip terminator and restart while loop
    if (call_dispatched) continue;

    // ── Dispatch terminator ───────────────────────────────
    assert(blk.terminator.has_value() && "missing terminator: verifier should catch this");

    bool returned = false;
    RuntimeWord retval = 0;

    std::visit(overload{
      [&](Ret const& op) {
        retval = frame.env[op.value.value];
        returned = true;
      },
      [&](Jump const& op) {
        frame.current_block = op.target;
        frame.next_inst_idx = 0;
        bind_block_params(frame, *func, op.target, op.args);
      },
      [&](Branch const& op) {
        frame.next_inst_idx = 0;
        bool cond = (frame.env[op.cond.value] != 0);
        if (cond) {
          frame.current_block = op.true_block;
          bind_block_params(frame, *func, op.true_block, op.true_args);
        } else {
          frame.current_block = op.false_block;
          bind_block_params(frame, *func, op.false_block, op.false_args);
        }
      },
    }, *blk.terminator);

    ++steps;

    if (returned) {
      auto ret_target = frame.return_target; // capture before pop
      call_stack.pop_back();
      if (!call_stack.empty()) {
        auto& caller = call_stack.back();
        if (ret_target)
          caller.env[ret_target->value] = retval;
      } else {
        return RunSuccess{retval, RunStats{steps, max_depth}};
      }
    }
  }

  return RunSuccess{0, RunStats{steps, max_depth}};
}

} // namespace mljit::ir::runtime
