// SSA IR verifier tests — positive (valid) and negative (error-kind) cases.
import mljit.ir;
import mljit.ir.verify;

#include <catch2/catch_test_macros.hpp>
#include <variant>

using namespace mljit::ir;
namespace verify_ns = mljit::ir::verify;

// ── Test-only helpers for constructing malformed IR ─────────
// These mutate exported IR structs directly, bypassing builder
// assertions, to exercise specific verifier error kinds.

namespace {

void set_jump(Module& mod, FunctionId fid, BlockId bid, BlockId target,
              std::vector<ValueId> args) {
  mod.functions[fid.value].blocks[bid.value].terminator =
    Jump{target, std::move(args)};
}

void set_branch(Module& mod, FunctionId fid, BlockId bid,
                ValueId cond, BlockId tb, std::vector<ValueId> ta,
                BlockId fb, std::vector<ValueId> fa) {
  mod.functions[fid.value].blocks[bid.value].terminator =
    Branch{cond, tb, std::move(ta), fb, std::move(fa)};
}

void set_block_param_type(Module& mod, FunctionId fid, BlockId bid,
                          size_t idx, Type t) {
  mod.functions[fid.value].blocks[bid.value].params[idx].type = t;
}

void corrupt_call_callee(Module& mod, FunctionId fid, InstructionId iid, FunctionId new_callee) {
  auto& inst = mod.functions[fid.value].instructions[iid.value];
  if (auto* call = std::get_if<Call>(&inst.payload))
    call->callee = new_callee;
}

void add_block_param(Module& mod, FunctionId fid, BlockId bid, Type t) {
  auto& f = mod.functions[fid.value];
  auto& b = f.blocks[bid.value];
  ValueId vid{f.next_value_id++};
  b.params.push_back(BlockParam{vid, t, std::nullopt, std::nullopt});
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════
//  Positive tests
// ════════════════════════════════════════════════════════════

TEST_CASE("valid add1 verifies ok", "[verify]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "add1");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);
  auto one = fb.const_i64(entry, 1);
  auto sum = fb.iadd(entry, x, one);
  fb.ret(entry, sum);

  auto mod = mb.finish();
  CHECK(verify_ns::verify(mod).ok());
}

TEST_CASE("valid block-param abs verifies ok", "[verify]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "abs");
  auto fb = mb.function_builder(fid);
  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);

  auto zero1 = fb.const_i64(entry, 0);
  auto cmp   = fb.icmp(entry, IcmpCond::slt, x, zero1);
  auto neg   = fb.append_block({Type::i64}, "negative");
  auto done  = fb.append_block({Type::i64}, "done");
  fb.branch(entry, cmp, neg, {x}, done, {x});

  auto neg_x = fb.param_id(neg, 0);
  auto zero2 = fb.const_i64(neg, 0);
  auto pos   = fb.isub(neg, zero2, neg_x);
  fb.jump(neg, done, {pos});

  auto result = fb.param_id(done, 0);
  fb.ret(done, result);

  auto mod = mb.finish();
  CHECK(verify_ns::verify(mod).ok());
}

TEST_CASE("valid cross-function call verifies ok", "[verify]") {
  ModuleBuilder mb;
  auto add1 = mb.create_function({Type::i64}, Type::i64, "add1");
  {
    auto fb = mb.function_builder(add1);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto one = fb.const_i64(entry, 1);
    auto sum = fb.iadd(entry, x, one);
    fb.ret(entry, sum);
  }
  auto twice = mb.create_function({Type::i64}, Type::i64, "twice");
  {
    auto fb = mb.function_builder(twice);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    auto a = fb.call(entry, add1, {x});
    auto b = fb.call(entry, add1, {a});
    fb.ret(entry, b);
  }

  auto mod = mb.finish();
  CHECK(verify_ns::verify(mod).ok());
}

// ── Entry param used directly in successor (dominates regression) ──
TEST_CASE("entry param dominates successor", "[verify]") {
  // entry(n): cmp = icmp slt n, 2; branch cmp -> base, recurse
  // base:     ret n    // directly uses entry param
  // recurse:  ret 0
  ModuleBuilder mb;
  auto fid = mb.create_function({Type::i64}, Type::i64, "test");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto n = fb.param_id(entry, 0);
    auto two = fb.const_i64(entry, 2);
    auto cmp = fb.icmp(entry, IcmpCond::slt, n, two);
    auto base = fb.append_block({}, "base");
    auto recurse = fb.append_block({}, "recurse");
    fb.branch(entry, cmp, base, {}, recurse, {});
    fb.ret(base, n);            // n from entry — entry dominates base
    auto zero = fb.const_i64(recurse, 0);
    fb.ret(recurse, zero);
  }
  auto mod = mb.finish();
  auto res = verify_ns::verify(mod);
  CHECK(res.ok());
}

// ════════════════════════════════════════════════════════════
//  Negative tests
// ════════════════════════════════════════════════════════════

TEST_CASE("missing terminator detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto no_term_fid = mb.create_function({}, Type::i64, "no_term"); // no builder called -> no terminator
  (void)no_term_fid;
  auto mod = mb.finish();
  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::MissingTerminator) found = true;
  CHECK(found);
}

TEST_CASE("invalid block target detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_jump");
  {
    auto fb = mb.function_builder(fid);
    auto c0 = fb.const_i64(fb.entry_block(), 0);
    (void)c0;
  }
  auto mod = mb.finish();
  // Replace entry terminator with jump to non-existent block
  set_jump(mod, fid, BlockId{0}, BlockId{999}, {});

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::InvalidBlockTarget) found = true;
  CHECK(found);
}

TEST_CASE("block argument count mismatch detected", "[verify][neg]") {
  // Build valid IR, then add a block param to create mismatch
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_arg_count");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto c = fb.const_i64(entry, 42);
    auto target = fb.append_block({Type::i64}, "target");
    fb.jump(entry, target, {c});
    fb.ret(target, fb.param_id(target, 0));
  }
  auto mod = mb.finish();
  // Add extra param to target so jump args (1) < target params (2)
  add_block_param(mod, fid, BlockId{1}, Type::i64);

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::BlockArgumentCountMismatch) found = true;
  CHECK(found);
}

TEST_CASE("block argument type mismatch detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_arg_type");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto c = fb.const_i64(entry, 42);
    auto target = fb.append_block({Type::i64}, "target");
    fb.jump(entry, target, {c});
    fb.ret(target, fb.param_id(target, 0));
  }
  auto mod = mb.finish();
  // Change target param to i1 — jump provides i64 → mismatch
  set_block_param_type(mod, fid, BlockId{1}, 0, Type::i1);

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::BlockArgumentTypeMismatch) found = true;
  CHECK(found);
}

TEST_CASE("return type mismatch detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_ret");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto c = fb.const_i64(entry, 42);
    fb.ret(entry, c); // valid: i64 ret with i64 value
  }
  auto mod = mb.finish();
  // Change function return type to i1 — ret value is i64 → mismatch
  mod.functions[fid.value].return_type = Type::i1;

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::ReturnTypeMismatch) found = true;
  CHECK(found);
}

TEST_CASE("call argument count mismatch detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto callee = mb.create_function({Type::i64}, Type::i64, "callee");
  {
    auto fb = mb.function_builder(callee);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    fb.ret(entry, x);
  }
  auto caller = mb.create_function({}, Type::i64, "caller");
  {
    auto fb = mb.function_builder(caller);
    auto entry = fb.entry_block();
    auto c42 = fb.const_i64(entry, 42);
    auto call_v = fb.call(entry, callee, {c42}); // 1 arg, callee has 1 param → valid
    (void)call_v;
  }
  auto mod = mb.finish();
  // Add a param to callee — now callee has 2 params, call still has 1 arg
  add_block_param(mod, callee, BlockId{0}, Type::i64);

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::CallArgumentCountMismatch) found = true;
  CHECK(found);
}

TEST_CASE("call argument type mismatch detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto callee = mb.create_function({Type::i64}, Type::i64, "callee");
  {
    auto fb = mb.function_builder(callee);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    fb.ret(entry, x);
  }
  auto caller = mb.create_function({}, Type::i64, "caller");
  {
    auto fb = mb.function_builder(caller);
    auto entry = fb.entry_block();
    auto c42 = fb.const_i64(entry, 42);
    auto call_v = fb.call(entry, callee, {c42}); // i64 arg, callee expects i64 → valid
    (void)call_v;
  }
  auto mod = mb.finish();
  // Change callee's entry param to i1 — caller provides i64 → mismatch
  set_block_param_type(mod, callee, BlockId{0}, 0, Type::i1);

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::CallArgumentTypeMismatch) found = true;
  CHECK(found);
}

TEST_CASE("branch condition not i1 detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_branch_cond");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto t = fb.const_i64(entry, 0);
    auto c = fb.icmp(entry, IcmpCond::eq, t, t); // i1
    auto a = fb.append_block({}, "a");
    auto b = fb.append_block({}, "b");
    fb.branch(entry, c, a, {}, b, {});
    fb.ret(a, t);
    fb.ret(b, t);
  }
  auto mod = mb.finish();
  // Corrupt: use i64 value as branch condition instead of i1
  // ValueId{0} = const_i64 0, ValueId{1} = icmp (i1)
  // Replace branch condition with v0 (i64)
  set_branch(mod, fid, BlockId{0},
             ValueId{0}, BlockId{1}, {},
             BlockId{2}, {});

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::BranchConditionTypeMismatch) found = true;
  CHECK(found);
}

TEST_CASE("unreachable block detected", "[verify][neg]") {
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "unreachable");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto t = fb.const_i64(entry, 42);
    fb.ret(entry, t);
    // Append a block that nothing jumps to
    auto dead = fb.append_block({}, "dead");
    fb.ret(dead, t);
  }

  auto mod = mb.finish();
  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::UnreachableBlock) found = true;
  CHECK(found);
}

TEST_CASE("use does not dominate detected", "[verify][neg]") {
  // Diamond: entry -> ^a, ^b -> ^c.
  // Value defined in ^a, used in ^c — ^a does not dominate ^c.
  ModuleBuilder mb;
  auto fid = mb.create_function({}, Type::i64, "bad_dom");
  {
    auto fb = mb.function_builder(fid);
    auto entry = fb.entry_block();
    auto t = fb.const_i64(entry, 0);
    auto c = fb.icmp(entry, IcmpCond::eq, t, t); // i1
    auto a = fb.append_block({}, "a");
    auto b = fb.append_block({}, "b");
    auto merge = fb.append_block({}, "c");
    fb.branch(entry, c, a, {}, b, {});

    // Block a: define %w and jump to merge (forward ref via jump, then ret in merge)
    auto w = fb.const_i64(a, 1);
    fb.jump(a, merge, {});

    // Block b: jump to merge
    fb.jump(b, merge, {});

    // Block c: ret %w — %w defined in a, which doesn't dominate c
    fb.ret(merge, w);
  }

  auto mod = mb.finish();
  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::UseDoesNotDominate) { found = true; break; }
  CHECK(found);
}

TEST_CASE("invalid function target detected", "[verify][neg]") {
  // Build valid call, then corrupt callee FunctionId to non-existent value.
  ModuleBuilder mb;
  auto callee = mb.create_function({Type::i64}, Type::i64, "callee");
  {
    auto fb = mb.function_builder(callee);
    auto entry = fb.entry_block();
    auto x = fb.param_id(entry, 0);
    fb.ret(entry, x);
  }
  auto caller = mb.create_function({}, Type::i64, "caller");
  auto call_iid = InstructionId{1}; // second instruction (after const_i64)
  {
    auto fb = mb.function_builder(caller);
    auto entry = fb.entry_block();
    auto c42 = fb.const_i64(entry, 42);
    auto call_v = fb.call(entry, callee, {c42});
    (void)call_v;
  }
  auto mod = mb.finish();
  // Corrupt callee reference to non-existent FunctionId
  corrupt_call_callee(mod, caller, call_iid, FunctionId{999});

  auto res = verify_ns::verify(mod);
  CHECK_FALSE(res.ok());
  bool found = false;
  for (auto const& e : res.errors)
    if (e.kind == verify_ns::VerifyErrorKind::InvalidFunctionTarget) { found = true; break; }
  CHECK(found);
}
