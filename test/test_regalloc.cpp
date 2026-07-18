import mljit.ir;
import mljit.regalloc;
import mljit.x64;

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <set>

using namespace mljit;

// ── Pressure test helpers ─────────────────────────────────────

// A function with N simultaneously-live values (N constants, all live until a
// final sum), forcing spills once N exceeds the 14-register file.
static auto make_pressure_fn(int count) -> ir::Module {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "pressure");
  auto fb  = mb.function_builder(fid);
  auto entry = fb.entry_block();
  std::vector<ir::ValueId> vs;
  for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
    vs.push_back(fb.const_i64(entry, static_cast<std::int64_t>(i) + 1,
                              "v" + std::to_string(i)));
  ir::ValueId acc = vs[0];
  for (std::size_t i = 1; i < vs.size(); ++i) acc = fb.iadd(entry, acc, vs[i]);
  fb.ret(entry, acc);
  return mb.finish();
}

static auto ranges_overlap(regalloc::LiveInterval const& a,
                           regalloc::LiveInterval const& b) -> bool {
  for (auto ra : a.ranges)
    for (auto rb : b.ranges)
      if (std::max(ra.from, rb.from) < std::min(ra.to, rb.to)) return true;
  return false;
}

// Structural correctness of an allocation, independent of execution.
static void check_invariants(regalloc::IntervalSet const& iv,
                             regalloc::Allocation const& alloc) {
  // (1) Every live value has a location.
  for (auto const& interval : iv.intervals)
    if (!interval.ranges.empty())
      CHECK(alloc.value_locs[interval.value.value].has_value());

  // (2) No two overlapping intervals share a register.
  for (std::size_t i = 0; i < iv.intervals.size(); ++i) {
    auto const& a = iv.intervals[i];
    auto const& la = alloc.value_locs[a.value.value];
    if (a.ranges.empty() || !la || la->kind != regalloc::LocKind::Reg) continue;
    for (std::size_t j = i + 1; j < iv.intervals.size(); ++j) {
      auto const& b = iv.intervals[j];
      auto const& lb = alloc.value_locs[b.value.value];
      if (b.ranges.empty() || !lb || lb->kind != regalloc::LocKind::Reg) continue;
      if (la->reg == lb->reg) CHECK_FALSE(ranges_overlap(a, b));
    }
  }

  // (3) Spilled values get distinct slots, each within the frame.
  std::set<std::uint32_t> slots;
  for (auto const& loc : alloc.value_locs)
    if (loc && loc->kind == regalloc::LocKind::Spill) {
      CHECK(loc->slot < alloc.num_spill_slots);
      CHECK(slots.insert(loc->slot).second);   // no duplicate slot
    }
}

// abs(x): a branch/merge with block parameters.
//
//   ^entry(x):   zero = 0; isneg = x < 0; branch isneg, ^neg(x), ^done(x)
//   ^neg(v):     z = 0; r = z - v; jump ^done(r)
//   ^done(res):  ret res
TEST_CASE("numbering: abs (branch + merge)", "[regalloc][numbering]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "abs");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto x     = fb.param_id(entry, 0);
  auto neg   = fb.append_block({ir::Type::i64}, "neg");
  auto done  = fb.append_block({ir::Type::i64}, "done");

  auto zero  = fb.const_i64(entry, 0, "zero");
  auto isneg = fb.icmp(entry, ir::IcmpCond::slt, x, zero, "isneg");
  fb.branch(entry, isneg, neg, {x}, done, {x});

  auto v = fb.param_id(neg, 0);
  auto z = fb.const_i64(neg, 0, "z");
  auto r = fb.isub(neg, z, v, "r");
  fb.jump(neg, done, {r});

  auto res = fb.param_id(done, 0);
  fb.ret(done, res);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n = regalloc::compute_numbering(fn);
  auto dump = regalloc::dump_numbering(fn, n);

  // Block parameters carry no debug name through the builder, so they print
  // as v{id} (same fallback as ir_printer): x=v0, neg param=v1, done param=v2.
  std::string const expected =
    "^entry [0..8):\n"
    "  0: block-entry(v0)\n"
    "  2: zero = const_i64\n"
    "  4: isneg = icmp\n"
    "  6: term branch\n"
    "^neg [8..16):\n"
    "  8: block-entry(v1)\n"
    "  10: z = const_i64\n"
    "  12: r = isub\n"
    "  14: term jump\n"
    "^done [16..20):\n"
    "  16: block-entry(v2)\n"
    "  18: term ret\n";

  CHECK(dump == expected);

  // Intervals: no holes — every value's uses are contiguous with its def.
  auto iv = regalloc::build_intervals(fn, n);
  std::string const expected_iv =
    "v0: [0..7) uses 4,6\n"    // x: entry param, used at icmp(4) and branch(6)
    "v1: [8..13) uses 12\n"    // neg param v, used at isub(12)
    "v2: [16..19) uses 18\n"   // done param res, used at ret(18)
    "v3: [2..5) uses 4\n"      // zero
    "v4: [4..7) uses 6\n"      // isneg
    "v5: [10..13) uses 12\n"   // z
    "v6: [12..15) uses 14\n";  // r, used at jump(14)
  CHECK(regalloc::dump_intervals(iv) == expected_iv);

  // Allocation: no idiv/call, no pressure -> everyone gets a register.
  auto alloc = regalloc::allocate(fn, n, iv);
  std::string const expected_alloc =
    "v0: rcx\n"
    "v1: rcx\n"
    "v2: rcx\n"
    "v3: rsi\n"
    "v4: rdi\n"
    "v5: rsi\n"
    "v6: rdi\n";
  CHECK(regalloc::dump_allocation(alloc) == expected_alloc);
  CHECK(alloc.num_spill_slots == 0);
}

// A diamond where a value is used on only one arm, producing a real lifetime
// hole.
//
//   ^entry(p):  c = 7; zero = 0; cond = p == 0; branch cond, ^A, ^B
//   ^A:         jump ^C(zero)        // uses zero
//   ^B:         jump ^C(c)           // uses c
//   ^C(m):      ret m
//
// Reverse-postorder lays the blocks out as entry, ^B, ^A, ^C (DFS finishes
// ^A's subtree first, so it lands last after reversal).  So:
//   - c is used only in ^B, which immediately follows entry -> contiguous.
//   - zero is used at cond (in entry) and again in ^A (the last block), but is
//     dead in ^B which sits between them -> a hole over ^B, [10..14).
TEST_CASE("intervals: diamond with a lifetime hole", "[regalloc][intervals]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64}, ir::Type::i64, "diamond_hole");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto p = fb.param_id(entry, 0);
  auto ba = fb.append_block({}, "A");
  auto bb = fb.append_block({}, "B");
  auto bc = fb.append_block({ir::Type::i64}, "C");

  auto c    = fb.const_i64(entry, 7, "c");
  auto zero = fb.const_i64(entry, 0, "zero");
  auto cond = fb.icmp(entry, ir::IcmpCond::eq, p, zero, "cond");
  fb.branch(entry, cond, ba, {}, bb, {});

  fb.jump(ba, bc, {zero});
  fb.jump(bb, bc, {c});

  auto m = fb.param_id(bc, 0);
  fb.ret(bc, m);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n  = regalloc::compute_numbering(fn);
  auto iv = regalloc::build_intervals(fn, n);

  std::string const expected_iv =
    "v0: [0..7) uses 6\n"                // p
    "v1: [18..21) uses 20\n"             // m: C param -> ret(20)
    "v2: [2..13) uses 12\n"              // c: used only in ^B (first arm) -> contiguous
    "v3: [4..10) [14..17) uses 6,16\n"   // zero: HOLE over ^B [10..14)
    "v4: [6..9) uses 8\n";               // cond
  CHECK(regalloc::dump_intervals(iv) == expected_iv);
}

// A loop whose back edge swaps its two parameters, forcing a register cycle
// (broken with xchg) on a critical edge (loop forks, loop merges -> split).
//
//   ^entry(x, y):  jump ^loop(x, y)
//   ^loop(a, b):   z = 0; c = a == z; branch c, ^exit(a), ^loop(b, a)
//   ^exit(r):      ret r
TEST_CASE("resolution: swap cycle on a critical edge", "[regalloc][resolution]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64, ir::Type::i64}, ir::Type::i64, "swap_loop");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto x = fb.param_id(entry, 0);
  auto y = fb.param_id(entry, 1);
  auto loop = fb.append_block({ir::Type::i64, ir::Type::i64}, "loop");
  auto exit = fb.append_block({ir::Type::i64}, "exit");

  fb.jump(entry, loop, {x, y});

  auto a = fb.param_id(loop, 0);
  auto b = fb.param_id(loop, 1);
  auto z = fb.const_i64(loop, 0, "z");
  auto c = fb.icmp(loop, ir::IcmpCond::eq, a, z, "c");
  fb.branch(loop, c, exit, {a}, loop, {b, a});   // false edge swaps (a,b)->(b,a)

  auto r = fb.param_id(exit, 0);
  fb.ret(exit, r);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n     = regalloc::compute_numbering(fn);
  auto iv    = regalloc::build_intervals(fn, n);
  auto alloc = regalloc::allocate(fn, n, iv);
  auto reso  = regalloc::resolve(fn, n, alloc);

  // loop->loop is critical (loop forks at the branch and merges from entry +
  // itself), so the edge is split; the (a,b)<-(b,a) swap is one register cycle
  // broken with a single xchg.
  std::string const expected_res =
    "edge ^loop -> ^loop (split):\n"
    "  xchg rcx, rsi\n";
  CHECK(regalloc::dump_resolution(fn, reso) == expected_res);
}

// gcd(a, b): an iterative loop with a back-edge.
//
//   ^entry(a, b):   jump ^loop(a, b)
//   ^loop(a1, b1):  zero = 0; is_zero = b1 == 0; branch is_zero, ^done(a1), ^body(a1, b1)
//   ^body(a2, b2):  r = a2 % b2; jump ^loop(b2, r)
//   ^done(res):     ret res
TEST_CASE("numbering: gcd (loop / back-edge)", "[regalloc][numbering]") {
  ir::ModuleBuilder mb;
  auto fid = mb.create_function({ir::Type::i64, ir::Type::i64}, ir::Type::i64, "gcd");
  auto fb  = mb.function_builder(fid);

  auto entry = fb.entry_block();
  auto a = fb.param_id(entry, 0);
  auto b = fb.param_id(entry, 1);
  auto loop = fb.append_block({ir::Type::i64, ir::Type::i64}, "loop");
  auto body = fb.append_block({ir::Type::i64, ir::Type::i64}, "body");
  auto done = fb.append_block({ir::Type::i64}, "done");

  fb.jump(entry, loop, {a, b});

  auto a1 = fb.param_id(loop, 0);
  auto b1 = fb.param_id(loop, 1);
  auto zero    = fb.const_i64(loop, 0, "zero");
  auto is_zero = fb.icmp(loop, ir::IcmpCond::eq, b1, zero, "is_zero");
  fb.branch(loop, is_zero, done, {a1}, body, {a1, b1});

  auto a2 = fb.param_id(body, 0);
  auto b2 = fb.param_id(body, 1);
  auto r  = fb.irem(body, a2, b2, "r");
  fb.jump(body, loop, {b2, r});

  auto res = fb.param_id(done, 0);
  fb.ret(done, res);

  auto mod = mb.finish();
  auto const& fn = mod.functions[fid.value];

  auto n = regalloc::compute_numbering(fn);
  auto dump = regalloc::dump_numbering(fn, n);

  // Params print as v{id}: entry(v0,v1), loop(v2,v3), body(v4,v5), done(v6).
  std::string const expected =
    "^entry [0..4):\n"
    "  0: block-entry(v0, v1)\n"
    "  2: term jump\n"
    "^loop [4..12):\n"
    "  4: block-entry(v2, v3)\n"
    "  6: zero = const_i64\n"
    "  8: is_zero = icmp\n"
    "  10: term branch\n"
    "^body [12..18):\n"
    "  12: block-entry(v4, v5)\n"
    "  14: r = irem\n"
    "  16: term jump\n"
    "^done [18..22):\n"
    "  18: block-entry(v6)\n"
    "  20: term ret\n";

  CHECK(dump == expected);

  // Intervals: block-parameter SSA threads all loop-carried state through
  // params, so nothing is live across the back edge and the loop-header
  // extension is a no-op — no holes.
  auto iv = regalloc::build_intervals(fn, n);
  std::string const expected_iv =
    "v0: [0..3) uses 2\n"       // a: entry param -> jump arg
    "v1: [0..3) uses 2\n"       // b: entry param -> jump arg
    "v2: [4..11) uses 10\n"     // a1: loop param -> branch arg
    "v3: [4..11) uses 8,10\n"   // b1: loop param, used at icmp(8) and branch(10)
    "v4: [12..15) uses 14\n"    // a2: body param -> irem(14)
    "v5: [12..17) uses 14,16\n" // b2: body param, used at irem(14) and jump(16)
    "v6: [18..21) uses 20\n"    // res: done param -> ret(20)
    "v7: [6..9) uses 8\n"       // zero
    "v8: [8..11) uses 10\n"     // is_zero
    "v9: [14..17) uses 16\n";   // r: irem result -> jump arg
  CHECK(regalloc::dump_intervals(iv) == expected_iv);

  // Allocation: the irem at pos 14 pins fixed intervals on rax and rdx, so no
  // value is placed there — including v9 (the irem result), which lands in rdi.
  // Emission will move the result out of rax after the divide.
  auto alloc = regalloc::allocate(fn, n, iv);
  std::string const expected_alloc =
    "v0: rcx\n"
    "v1: rsi\n"
    "v2: rcx\n"
    "v3: rsi\n"
    "v4: rcx\n"
    "v5: rsi\n"
    "v6: rcx\n"
    "v7: rdi\n"
    "v8: r8\n"
    "v9: rdi\n";
  CHECK(regalloc::dump_allocation(alloc) == expected_alloc);
  CHECK(alloc.num_spill_slots == 0);

  // No value occupies rax or rdx across the divide (fixed intervals respected).
  for (auto const& loc : alloc.value_locs) {
    if (loc && loc->kind == regalloc::LocKind::Reg) {
      CHECK(loc->reg != x64::Gpr::rax);
      CHECK(loc->reg != x64::Gpr::rdx);
    }
  }

  // Resolution: the only real moves are on the body->loop back edge, threading
  // (a, b) <- (b, a%b): a1(rcx) <- b2(rsi), b1(rsi) <- r(rdi). Emitted in
  // dependency order so rcx is read before rsi is overwritten.
  auto reso = regalloc::resolve(fn, n, alloc);
  std::string const expected_res =
    "edge ^body -> ^loop:\n"
    "  mov rcx, rsi\n"
    "  mov rsi, rdi\n";
  CHECK(regalloc::dump_resolution(fn, reso) == expected_res);
}

// Register pressure: below the 14-register file everyone gets a register;
// above it, the allocator spills — and every allocation stays structurally
// valid (no overlapping values share a register, spill slots are distinct).
TEST_CASE("allocation: register pressure and spilling", "[regalloc][pressure]") {
  // Peak pressure is count+1: at the first reduction add, both operands are
  // still live and the result is born while the other constants remain live.
  // So the 12-register allocatable file (r10/r11 reserved as scratch) is
  // exceeded once count >= 12.
  for (int count : {8, 11, 12, 15, 16, 20, 30, 40}) {
    auto mod = make_pressure_fn(count);
    auto const& fn = mod.functions[0];
    auto n     = regalloc::compute_numbering(fn);
    auto iv    = regalloc::build_intervals(fn, n);
    auto alloc = regalloc::allocate(fn, n, iv);

    check_invariants(iv, alloc);

    if (count <= 11) CHECK(alloc.num_spill_slots == 0);
    else             CHECK(alloc.num_spill_slots > 0);
  }
}
