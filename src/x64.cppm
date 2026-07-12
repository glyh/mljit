module;

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>
#include <cstring>
#include <system_error>
#include <sys/mman.h>
#include <unistd.h>

export module mljit.x64;

export namespace mljit::x64 {

// ── Register ──────────────────────────────────────────────────
enum class Gpr : std::uint8_t {
  rax, rcx, rdx, rbx,
  rsp, rbp, rsi, rdi,
  r8,  r9,  r10, r11,
  r12, r13, r14, r15
};

// ── Label ─────────────────────────────────────────────────────
struct Label {
  std::uint32_t id;
};

// ── Memory operand (base + disp32) ────────────────────────────
struct Mem {
  Gpr           base;
  std::int32_t  disp;
};

// ── ExecBuffer (move-only RAII, wraps mmap executable memory) ─
class ExecBuffer {
public:
  ExecBuffer() = default;

  [[nodiscard]] static auto copy_from(std::uint8_t const* bytes, std::size_t size) -> ExecBuffer;

  ~ExecBuffer() { release(); }

  ExecBuffer(ExecBuffer const&) = delete;
  auto operator=(ExecBuffer const&) -> ExecBuffer& = delete;

  ExecBuffer(ExecBuffer&& other) noexcept
    : ptr_(other.ptr_), size_(other.size_)
  {
    other.ptr_  = nullptr;
    other.size_ = 0;
  }

  auto operator=(ExecBuffer&& other) noexcept -> ExecBuffer& {
    if (this != &other) {
      release();
      ptr_  = other.ptr_;
      size_ = other.size_;
      other.ptr_  = nullptr;
      other.size_ = 0;
    }
    return *this;
  }

  [[nodiscard]] auto ptr()  const noexcept -> void*       { return ptr_; }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

  template <typename Fn>
  [[nodiscard]] auto as() const noexcept -> Fn* {
    return reinterpret_cast<Fn*>(ptr_);
  }

  // Call JIT code at ptr_ returning T. Per-function no_sanitize
  // keeps UBSan active everywhere else while suppressing the false
  // positive on the JIT buffer (UBSan can't type-check mmap'd code).
  template <typename T, typename... Args>
  __attribute__((no_sanitize("function")))
  auto invoke(Args... args) const noexcept -> T {
    return reinterpret_cast<T (*)(Args...)>(ptr_)(args...);
  }

private:
  explicit ExecBuffer(void* p, std::size_t sz) noexcept
    : ptr_(p), size_(sz) {}

  void release() noexcept {
    if (ptr_) {
      ::munmap(ptr_, size_);
      ptr_  = nullptr;
      size_ = 0;
    }
  }

  void*       ptr_  = nullptr;
  std::size_t size_ = 0;
};

// ── Assembler ─────────────────────────────────────────────────
class Assembler {
public:
  Assembler() = default;

  // ── Labels ──────────────────────────────────────────────────
  [[nodiscard]] auto new_label() -> Label;
  void bind(Label);

  // ── Data movement ───────────────────────────────────────────
  void mov_rr(Gpr dst, Gpr src);           // dst = src (64-bit)
  void mov_ri(Gpr dst, std::int64_t imm);  // dst = imm64
  void mov_mr(Mem dst, Gpr src);           // [dst] = src
  void mov_rm(Gpr dst, Mem src);           // dst = [src]
  void movzx_rr(Gpr dst, Gpr src);         // dst = zero-extend(src.byte)

  // ── Arithmetic ──────────────────────────────────────────────
  void add_rr(Gpr dst, Gpr src);
  void sub_rr(Gpr dst, Gpr src);
  void imul_rr(Gpr dst, Gpr src);
  void add_ri(Gpr dst, std::int32_t imm);
  void sub_ri(Gpr dst, std::int32_t imm);
  void cqo();                              // sign-extend rax → rdx:rax
  void idiv_r(Gpr src);                    // signed divide rdx:rax by src

  // ── Comparison & setcc ──────────────────────────────────────
  void cmp_rr(Gpr lhs, Gpr rhs);
  void sete (Gpr);
  void setne(Gpr);
  void setl (Gpr);
  void setle(Gpr);
  void setg (Gpr);
  void setge(Gpr);

  // ── Control flow ────────────────────────────────────────────
  void jmp    (Label);
  void je     (Label);
  void jne    (Label);
  void jl     (Label);
  void jle    (Label);
  void jg     (Label);
  void jge    (Label);
  void call   (Label);
  void call_r (Gpr);
  void ret();

  // ── Stack ───────────────────────────────────────────────────
  void push(Gpr);
  void pop (Gpr);

  // ── Finalisation / introspection ────────────────────────────
  void finalize();
  [[nodiscard]] auto executable_copy() -> ExecBuffer;
  [[nodiscard]] auto size() const -> std::size_t { return buf_.size(); }

  // Test-only introspection
  [[nodiscard]] auto bytes() const -> std::vector<std::uint8_t> const& { return buf_; }

private:
  // ── Internal helpers ────────────────────────────────────────
  static auto low3(Gpr r) -> std::uint8_t { return static_cast<std::uint8_t>(r) & 7; }
  static auto is_ext(Gpr r) -> bool { return static_cast<std::uint8_t>(r) >= 8; }

  void emit_u8(std::uint8_t);
  void emit_u32(std::uint32_t);
  void emit_u64(std::uint64_t);

  void rex(bool w, bool r, bool x, bool b);
  void modrm_reg(std::uint8_t reg_field, std::uint8_t rm);
  void emit_modrm_mem(std::uint8_t reg_field, Gpr base, std::int32_t disp);

  void emit_setcc(std::uint8_t cc_byte, Gpr dst);

  // ── Internal state ─────────────────────────────────────────
  struct LabelState {
    bool       bound  = false;
    std::size_t offset = 0;
  };
  std::vector<LabelState> labels_;

  struct Fixup {
    std::uint32_t label_id;
    std::size_t   imm_offset;   // offset of the 4-byte rel32 slot
  };
  std::vector<Fixup> fixups_;

  std::vector<std::uint8_t> buf_;
  std::uint32_t next_label_id_ = 0;
  bool finalized_ = false;
};

// ═══════════════════════════════════════════════════════════════
//  Implementation
// ═══════════════════════════════════════════════════════════════

// ── Labels ────────────────────────────────────────────────────

inline auto Assembler::new_label() -> Label {
  Label l{next_label_id_++};
  labels_.push_back(LabelState{});
  return l;
}

inline void Assembler::bind(Label label) {
  assert(!finalized_ && "bind after finalize");
  auto& st = labels_.at(label.id);
  assert(!st.bound && "label already bound");
  st.bound  = true;
  st.offset = buf_.size();
  // Patch every fixup that references this label
  for (auto& f : fixups_) {
    if (f.label_id == label.id) {
      auto end  = f.imm_offset + 4;
      auto diff = static_cast<std::int32_t>(buf_.size() - end);
      buf_[f.imm_offset + 0] = static_cast<std::uint8_t>(diff >> 0);
      buf_[f.imm_offset + 1] = static_cast<std::uint8_t>(diff >> 8);
      buf_[f.imm_offset + 2] = static_cast<std::uint8_t>(diff >> 16);
      buf_[f.imm_offset + 3] = static_cast<std::uint8_t>(diff >> 24);
    }
  }
}

// ── Emit helpers ──────────────────────────────────────────────

inline void Assembler::emit_u8(std::uint8_t v) {
  buf_.push_back(v);
}

inline void Assembler::emit_u32(std::uint32_t v) {
  buf_.push_back(static_cast<std::uint8_t>(v >> 0));
  buf_.push_back(static_cast<std::uint8_t>(v >> 8));
  buf_.push_back(static_cast<std::uint8_t>(v >> 16));
  buf_.push_back(static_cast<std::uint8_t>(v >> 24));
}

inline void Assembler::emit_u64(std::uint64_t v) {
  buf_.push_back(static_cast<std::uint8_t>(v >> 0));
  buf_.push_back(static_cast<std::uint8_t>(v >> 8));
  buf_.push_back(static_cast<std::uint8_t>(v >> 16));
  buf_.push_back(static_cast<std::uint8_t>(v >> 24));
  buf_.push_back(static_cast<std::uint8_t>(v >> 32));
  buf_.push_back(static_cast<std::uint8_t>(v >> 40));
  buf_.push_back(static_cast<std::uint8_t>(v >> 48));
  buf_.push_back(static_cast<std::uint8_t>(v >> 56));
}

inline void Assembler::rex(bool w, bool r, bool x, bool b) {
  // Omit entirely when no bits are set
  if (!w && !r && !x && !b) return;
  emit_u8(static_cast<std::uint8_t>(0x40 | (w << 3) | (r << 2) | (x << 1) | b));
}

inline void Assembler::modrm_reg(std::uint8_t reg_field, std::uint8_t rm) {
  emit_u8(static_cast<std::uint8_t>(0xC0 | (reg_field << 3) | rm));
}

inline void Assembler::emit_modrm_mem(std::uint8_t reg_field, Gpr base, std::int32_t disp) {
  auto b = low3(base);
  // mod = 10b (disp32)
  emit_u8(static_cast<std::uint8_t>(0x80 | (reg_field << 3) | b));
  // SIB needed when base is rsp or r12 (low3 == 4)
  if (b == 4) {
    // scale=0, index=4 (none), base = b
    emit_u8(0x24);
  }
  emit_u32(static_cast<std::uint32_t>(disp));
}

// ── Data movement ─────────────────────────────────────────────

inline void Assembler::mov_rr(Gpr dst, Gpr src) {
  assert(!finalized_);
  // REX.W + 89 /r  —  reg=src, rm=dst
  rex(true, is_ext(src), false, is_ext(dst));
  emit_u8(0x89);
  modrm_reg(low3(src), low3(dst));
}

inline void Assembler::mov_ri(Gpr dst, std::int64_t imm) {
  assert(!finalized_);
  // REX.W + B8+r + imm64
  rex(true, false, false, is_ext(dst));
  emit_u8(static_cast<std::uint8_t>(0xB8 | low3(dst)));
  emit_u64(static_cast<std::uint64_t>(imm));
}

inline void Assembler::mov_mr(Mem dst, Gpr src) {
  assert(!finalized_);
  // REX.W + 89 /r  —  reg=src, rm=dst.base (memory)
  rex(true, is_ext(src), false, is_ext(dst.base));
  emit_u8(0x89);
  emit_modrm_mem(low3(src), dst.base, dst.disp);
}

inline void Assembler::mov_rm(Gpr dst, Mem src) {
  assert(!finalized_);
  // REX.W + 8B /r  —  reg=dst, rm=src.base (memory)
  rex(true, is_ext(dst), false, is_ext(src.base));
  emit_u8(0x8B);
  emit_modrm_mem(low3(dst), src.base, src.disp);
}

inline void Assembler::movzx_rr(Gpr dst, Gpr src) {
  assert(!finalized_);
  // REX.W + 0F B6 /r  —  reg=dst, rm=src
  rex(true, is_ext(dst), false, is_ext(src));
  emit_u8(0x0F);
  emit_u8(0xB6);
  modrm_reg(low3(dst), low3(src));
}

// ── Arithmetic ────────────────────────────────────────────────

inline void Assembler::add_rr(Gpr dst, Gpr src) {
  assert(!finalized_);
  // REX.W + 01 /r  —  reg=src, rm=dst
  rex(true, is_ext(src), false, is_ext(dst));
  emit_u8(0x01);
  modrm_reg(low3(src), low3(dst));
}

inline void Assembler::sub_rr(Gpr dst, Gpr src) {
  assert(!finalized_);
  // REX.W + 29 /r  —  reg=src, rm=dst
  rex(true, is_ext(src), false, is_ext(dst));
  emit_u8(0x29);
  modrm_reg(low3(src), low3(dst));
}

inline void Assembler::imul_rr(Gpr dst, Gpr src) {
  assert(!finalized_);
  // REVERSED vs add/sub/mov: ModRM.reg = dst, ModRM.rm = src
  // REX.W + 0F AF /r
  rex(true, is_ext(dst), false, is_ext(src));
  emit_u8(0x0F);
  emit_u8(0xAF);
  modrm_reg(low3(dst), low3(src));
}

inline void Assembler::add_ri(Gpr dst, std::int32_t imm) {
  assert(!finalized_);
  // REX.W + 81 /0 + imm32
  rex(true, false, false, is_ext(dst));
  emit_u8(0x81);
  modrm_reg(0, low3(dst));
  emit_u32(static_cast<std::uint32_t>(imm));
}

inline void Assembler::sub_ri(Gpr dst, std::int32_t imm) {
  assert(!finalized_);
  // REX.W + 81 /5 + imm32
  rex(true, false, false, is_ext(dst));
  emit_u8(0x81);
  modrm_reg(5, low3(dst));
  emit_u32(static_cast<std::uint32_t>(imm));
}

inline void Assembler::cmp_rr(Gpr lhs, Gpr rhs) {
  assert(!finalized_);
  // REX.W + 39 /r  —  reg=rhs, rm=lhs
  rex(true, is_ext(rhs), false, is_ext(lhs));
  emit_u8(0x39);
  modrm_reg(low3(rhs), low3(lhs));
}

inline void Assembler::cqo() {
  assert(!finalized_);
  // REX.W + 99
  emit_u8(0x48);
  emit_u8(0x99);
}

inline void Assembler::idiv_r(Gpr src) {
  assert(!finalized_);
  // REX.W + F7 /7
  rex(true, false, false, is_ext(src));
  emit_u8(0xF7);
  modrm_reg(7, low3(src));
}

// ── SetCC ─────────────────────────────────────────────────────

inline void Assembler::emit_setcc(std::uint8_t cc_byte, Gpr dst) {
  assert(!finalized_);
  auto b = low3(dst);
  if (is_ext(dst)) {
    // r8–r15: REX.B = 1 to access the low byte
    rex(false, false, false, true);
  } else if (b >= 4) {
    // rsp/rbp/rsi/rdi: null REX (0x40) to get SPL/BPL/SIL/DIL
    emit_u8(0x40);
  }
  emit_u8(0x0F);
  emit_u8(cc_byte);
  modrm_reg(0, b);
}

inline void Assembler::sete (Gpr dst) { emit_setcc(0x94, dst); }
inline void Assembler::setne(Gpr dst) { emit_setcc(0x95, dst); }
inline void Assembler::setl (Gpr dst) { emit_setcc(0x9C, dst); }
inline void Assembler::setle(Gpr dst) { emit_setcc(0x9E, dst); }
inline void Assembler::setg (Gpr dst) { emit_setcc(0x9F, dst); }
inline void Assembler::setge(Gpr dst) { emit_setcc(0x9D, dst); }

// ── Control flow ──────────────────────────────────────────────

inline void Assembler::jmp(Label target) {
  assert(!finalized_);
  auto& st = labels_.at(target.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta  = static_cast<std::int64_t>(st.offset)
                - static_cast<std::int64_t>(offset + 2);
    if (delta >= -128 && delta <= 127) {
      // Short rel8 jump (2 bytes)
      emit_u8(0xEB);
      emit_u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(delta)));
      return;
    }
    // Long backward: rel32 (5 bytes)
    delta = static_cast<std::int64_t>(st.offset)
          - static_cast<std::int64_t>(offset + 5);
    emit_u8(0xE9);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  // Forward: placeholder rel32 + fixup
  fixups_.push_back({target.id, buf_.size() + 1});
  emit_u8(0xE9);
  emit_u32(0);
}

inline void Assembler::je (Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x84);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x84);
  emit_u32(0);
}

inline void Assembler::jne(Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x85);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x85);
  emit_u32(0);
}

inline void Assembler::jl (Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x8C);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x8C);
  emit_u32(0);
}

inline void Assembler::jle(Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x8E);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x8E);
  emit_u32(0);
}

inline void Assembler::jg (Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x8F);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x8F);
  emit_u32(0);
}

inline void Assembler::jge(Label t) {
  assert(!finalized_);
  auto& st = labels_.at(t.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 6);
    emit_u8(0x0F);
    emit_u8(0x8D);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({t.id, buf_.size() + 2});
  emit_u8(0x0F);
  emit_u8(0x8D);
  emit_u32(0);
}

inline void Assembler::call(Label target) {
  assert(!finalized_);
  auto& st = labels_.at(target.id);
  if (st.bound) {
    auto offset = buf_.size();
    auto delta = static_cast<std::int64_t>(st.offset)
               - static_cast<std::int64_t>(offset + 5);
    emit_u8(0xE8);
    emit_u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(delta)));
    return;
  }
  fixups_.push_back({target.id, buf_.size() + 1});
  emit_u8(0xE8);
  emit_u32(0);
}

inline void Assembler::call_r(Gpr target) {
  assert(!finalized_);
  // REX.W + FF /2
  rex(true, false, false, is_ext(target));
  emit_u8(0xFF);
  modrm_reg(2, low3(target));
}

inline void Assembler::ret() {
  assert(!finalized_);
  emit_u8(0xC3);
}

// ── Stack ─────────────────────────────────────────────────────

inline void Assembler::push(Gpr reg) {
  assert(!finalized_);
  // REX.B (when extended) + 50+r
  rex(false, false, false, is_ext(reg));
  emit_u8(static_cast<std::uint8_t>(0x50 | low3(reg)));
}

inline void Assembler::pop(Gpr reg) {
  assert(!finalized_);
  // REX.B (when extended) + 58+r
  rex(false, false, false, is_ext(reg));
  emit_u8(static_cast<std::uint8_t>(0x58 | low3(reg)));
}

// ── Finalization / introspection ──────────────────────────────

inline void Assembler::finalize() {
  assert(!finalized_ && "already finalized");
  for (auto& f : fixups_) {
    assert(labels_.at(f.label_id).bound && "unresolved fixup");
  }
  finalized_ = true;
}

inline auto ExecBuffer::copy_from(std::uint8_t const* bytes, std::size_t size) -> ExecBuffer {
  assert(size > 0 && "no code to copy");
  auto ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED) {
    throw std::system_error(errno, std::generic_category(), "mmap failed");
  }
  std::memcpy(ptr, bytes, size);
  return ExecBuffer{ptr, size};
}

inline auto Assembler::executable_copy() -> ExecBuffer {
  return ExecBuffer::copy_from(buf_.data(), buf_.size());
}

} // namespace mljit::x64
