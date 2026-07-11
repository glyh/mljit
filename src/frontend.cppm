module;
// ─── Global includes ────
#include <string>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <expected>
#include <stdexcept>
#include <any>
#include <utility>
#include <cstdint>
#include <functional>

// ─── ANTLR4 includes ────
#include "antlr4-runtime.h"
#include "MLJITLexer.h"
#include "MLJITParser.h"
#include "MLJITBaseVisitor.h"

export module mljit.frontend;

import mljit.ir;

export namespace mljit::frontend {

// ────────────────────────────────────────────────────────────
//  typed errors
// ────────────────────────────────────────────────────────────
enum class CompileErrorKind {
  LexError,
  ParseError,
  UnboundVariable,
  CallToUnknownFunction,
};

struct CompileError {
  CompileErrorKind kind;
  std::uint64_t line{};
  std::uint64_t column{};
  std::string detail;
};

// ────────────────────────────────────────────────────────────
//  main entry point
// ────────────────────────────────────────────────────────────
auto compile(std::string_view source)
    -> std::expected<ir::Module, std::vector<CompileError>>;

} // namespace mljit::frontend

// ════════════════════════════════════════════════════════════════
//  module implementation
// ════════════════════════════════════════════════════════════════
module : private;

namespace mljit::frontend {
namespace {

using namespace ir;

// ────────────────────────────────────────────────────────────
//  lowering visitor
// ────────────────────────────────────────────────────────────
struct LoweringVisitor : grammar::MLJITBaseVisitor {
  ModuleBuilder module;

  // per-function state
  ir::BlockId cur_block{0};
  std::unordered_map<std::string, ir::ValueId> symbols;

  // FunctionId by name
  std::unordered_map<std::string, ir::FunctionId> func_table;

  // collected errors
  std::vector<CompileError> errors;

  void error(CompileErrorKind kind, std::size_t line, std::size_t col,
             std::string detail) {
    errors.emplace_back(kind,
                        static_cast<std::uint64_t>(line),
                        static_cast<std::uint64_t>(col),
                        std::move(detail));
  }

  // ── helper: get a FunctionBuilder for the current frame ──
  auto fb(ir::FunctionId fid) -> ir::FunctionBuilder {
    return module.function_builder(fid);
  }

  // ── visitor overrides ──

  std::any visitProgram(grammar::MLJITParser::ProgramContext* ctx) override {
    // pass 1: declare all functions
    for (auto* fd : ctx->funDef()) {
      std::string name = fd->ID(0)->getText();
      std::vector<ir::Type> param_types(fd->ID().size() - 1, ir::Type::i64);
      auto fid = module.create_function(param_types, ir::Type::i64, name);
      func_table[name] = fid;
    }
    // pass 2: define bodies
    for (auto* fd : ctx->funDef()) {
      visitFunDef(fd);
    }
    return {};
  }

  std::any visitFunDef(grammar::MLJITParser::FunDefContext* ctx) override {
    std::string name = ctx->ID(0)->getText();
    symbols.clear();

    auto fid = func_table.at(name);
    current_fid_ = fid;
    cur_block = fb(fid).entry_block();

    // bind params
    std::size_t pcount = ctx->ID().size() - 1;
    for (std::size_t i = 0; i < pcount; ++i) {
      std::string pname = ctx->ID(i + 1)->getText();
      symbols[pname] = fb(fid).param_id(cur_block, i);
    }

    // lower body
    auto result = lower_expr(ctx->expr());
    fb(fid).ret(cur_block, result);
    return {};
  }

  auto lower_expr(grammar::MLJITParser::ExprContext* expr) -> ir::ValueId {
    return std::any_cast<ir::ValueId>(expr->accept(this));
  }

  // ── Expr: if ──

  std::any visitIfExp(grammar::MLJITParser::IfExpContext* ctx) override {
    auto* ie = ctx->ifExpr();
    auto cond = lower_expr(ie->expr(0));

    // Create then/else/join blocks
    auto then_blk = fb(current_fid_).append_block({});
    auto else_blk = fb(current_fid_).append_block({});
    auto join_blk = fb(current_fid_).append_block({ir::Type::i64});

    fb(current_fid_).branch(cur_block, cond, then_blk, {}, else_blk, {});

    // then arm
    cur_block = then_blk;
    auto tval = lower_expr(ie->expr(1));
    fb(current_fid_).jump(cur_block, join_blk, {tval});

    // else arm
    cur_block = else_blk;
    auto eval = lower_expr(ie->expr(2));
    fb(current_fid_).jump(cur_block, join_blk, {eval});

    // continue in join block
    cur_block = join_blk;
    return std::any(fb(current_fid_).param_id(join_blk, 0));
  }

  std::any visitLetExp(grammar::MLJITParser::LetExpContext* ctx) override {
    auto* le = ctx->letExpr();
    for (auto* b : le->binding()) {
      std::string bname = b->ID()->getText();
      auto val = lower_expr(b->expr());
      symbols[bname] = val;
    }
    return std::any(lower_expr(le->expr()));
  }

  // ── Eq chain: == / != ──

  std::any visitEqAlt(grammar::MLJITParser::EqAltContext* ctx) override {
    return ctx->eqExpr()->accept(this);
  }

  std::any visitEqBin(grammar::MLJITParser::EqBinContext* ctx) override {
    auto lhs = std::any_cast<ir::ValueId>(ctx->eqExpr()->accept(this));
    auto rhs = std::any_cast<ir::ValueId>(ctx->cmpExpr()->accept(this));
    std::string op = ctx->op->getText();
    auto cond = (op == "==") ? ir::IcmpCond::eq : ir::IcmpCond::ne;
    return std::any(fb(current_fid_).icmp(cur_block, cond, lhs, rhs));
  }

  // ── Cmp chain: < <= > >= ──

  std::any visitCmpAlt(grammar::MLJITParser::CmpAltContext* ctx) override {
    return ctx->cmpExpr()->accept(this);
  }

  std::any visitCmpBin(grammar::MLJITParser::CmpBinContext* ctx) override {
    auto lhs = std::any_cast<ir::ValueId>(ctx->cmpExpr()->accept(this));
    auto rhs = std::any_cast<ir::ValueId>(ctx->addExpr()->accept(this));
    std::string op = ctx->op->getText();
    ir::IcmpCond cond;
    if (op == "<")       cond = ir::IcmpCond::slt;
    else if (op == "<=") cond = ir::IcmpCond::sle;
    else if (op == ">")  cond = ir::IcmpCond::sgt;
    else                 cond = ir::IcmpCond::sge;
    return std::any(fb(current_fid_).icmp(cur_block, cond, lhs, rhs));
  }

  // ── Add chain: + - ──

  std::any visitAddAlt(grammar::MLJITParser::AddAltContext* ctx) override {
    return ctx->addExpr()->accept(this);
  }

  std::any visitAddBin(grammar::MLJITParser::AddBinContext* ctx) override {
    auto lhs = std::any_cast<ir::ValueId>(ctx->addExpr()->accept(this));
    auto rhs = std::any_cast<ir::ValueId>(ctx->mulExpr()->accept(this));
    auto f = fb(current_fid_);
    if (ctx->op->getText() == "+")
      return std::any(f.iadd(cur_block, lhs, rhs));
    return std::any(f.isub(cur_block, lhs, rhs));
  }

  // ── Mul chain: * / % ──

  std::any visitMulAlt(grammar::MLJITParser::MulAltContext* ctx) override {
    return ctx->mulExpr()->accept(this);
  }

  std::any visitMulBin(grammar::MLJITParser::MulBinContext* ctx) override {
    auto lhs = std::any_cast<ir::ValueId>(ctx->mulExpr()->accept(this));
    auto rhs = std::any_cast<ir::ValueId>(ctx->unaryExpr()->accept(this));
    auto f = fb(current_fid_);
    std::string op = ctx->op->getText();
    if (op == "*") return std::any(f.imul(cur_block, lhs, rhs));
    if (op == "/") return std::any(f.idiv(cur_block, lhs, rhs));
    return std::any(f.irem(cur_block, lhs, rhs));
  }

  // ── Unary: -expr ──

  std::any visitUnaryAlt(grammar::MLJITParser::UnaryAltContext* ctx) override {
    return ctx->unaryExpr()->accept(this);
  }

  std::any visitNegate(grammar::MLJITParser::NegateContext* ctx) override {
    auto f = fb(current_fid_);
    auto zero = f.const_i64(cur_block, 0);
    auto val = std::any_cast<ir::ValueId>(ctx->unaryExpr()->accept(this));
    return std::any(f.isub(cur_block, zero, val));
  }

  // ── Primary ──

  std::any visitPrimary(grammar::MLJITParser::PrimaryContext* ctx) override {
    return ctx->primaryExpr()->accept(this);
  }

  std::any visitIntLit(grammar::MLJITParser::IntLitContext* ctx) override {
    return std::any(fb(current_fid_).const_i64(cur_block,
        std::stoll(ctx->INT()->getText())));
  }

  std::any visitVariable(grammar::MLJITParser::VariableContext* ctx) override {
    auto name = ctx->ID()->getText();
    auto it = symbols.find(name);
    if (it == symbols.end()) {
      error(CompileErrorKind::UnboundVariable,
            ctx->getStart()->getLine(),
            ctx->getStart()->getCharPositionInLine(),
            "unbound variable '" + name + "'");
      return std::any(fb(current_fid_).const_i64(cur_block, 0));
    }
    return std::any(it->second);
  }

  std::any visitCall(grammar::MLJITParser::CallContext* ctx) override {
    auto name = ctx->ID()->getText();
    auto it = func_table.find(name);
    if (it == func_table.end()) {
      error(CompileErrorKind::CallToUnknownFunction,
            ctx->getStart()->getLine(),
            ctx->getStart()->getCharPositionInLine(),
            "call to unknown function '" + name + "'");
      return std::any(fb(current_fid_).const_i64(cur_block, 0));
    }
    std::vector<ir::ValueId> args;
    for (auto* e : ctx->expr()) {
      args.push_back(lower_expr(e));
    }
    return std::any(fb(current_fid_).call(cur_block, it->second, args));
  }

  std::any visitParens(grammar::MLJITParser::ParensContext* ctx) override {
    return std::any(lower_expr(ctx->expr()));
  }

private:
  ir::FunctionId current_fid_{0};
  void set_fid(ir::FunctionId fid) { current_fid_ = fid; }
};

// ════════════════════════════════════════════════════════════════
//  compile
// ════════════════════════════════════════════════════════════════
}  // namespace

auto compile(std::string_view source)
    -> std::expected<ir::Module, std::vector<CompileError>> {
  std::string src(source);
  antlr4::ANTLRInputStream input(src);
  grammar::MLJITLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  grammar::MLJITParser parser(&tokens);

  struct ErrorListener : antlr4::BaseErrorListener {
    std::vector<CompileError>& out;
    explicit ErrorListener(std::vector<CompileError>& o) : out(o) {}
    void syntaxError(antlr4::Recognizer*, antlr4::Token*,
                     std::size_t line, std::size_t col,
                     const std::string& msg, std::exception_ptr) override {
      out.emplace_back(CompileErrorKind::ParseError,
                       static_cast<std::uint64_t>(line),
                       static_cast<std::uint64_t>(col), msg);
    }
  };

  LoweringVisitor visitor;
  ErrorListener listener(visitor.errors);
  lexer.removeErrorListeners();
  lexer.addErrorListener(&listener);
  parser.removeErrorListeners();
  parser.addErrorListener(&listener);

  auto* tree = parser.program();
  if (parser.getNumberOfSyntaxErrors() > 0) {
    return std::unexpected(std::move(visitor.errors));
  }

  visitor.visit(tree);

  if (!visitor.errors.empty()) {
    return std::unexpected(std::move(visitor.errors));
  }

  return visitor.module.finish();
}

}  // namespace mljit::frontend
