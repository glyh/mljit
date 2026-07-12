---
title: Design the Project-Owned x64 Assembler Layer
parent: ../mljit-design-map.md
labels:
  - wayfinder:prototype
status: closed
assignee:
blocked_by:
  - design-block-parameter-ssa-ir.md
---

# Design the Project-Owned x64 Assembler Layer

## Resolution

MLJIT will implement a lightweight x86-64 assembler layer in a new module
`mljit.x64`.  The assembler is a pure instruction-encoding layer at the bottom of the
backend: it knows nothing about SSA IR, Machine IR, register allocation, or frontend
concepts.  It exposes an `Assembler` class that owns an append-only byte buffer, a label
table, and a list of rel32 fixups.  Callers (higher-level codegen) feed it instructions,
labels, and immediates; after finalization the caller retrieves an executable buffer ready
to run.

The design keeps the instruction surface small, explicit, zero-external-dependency, and
tuned for MLJIT's i64-centric codegen.

## API Boundary

**Module file:** `src/x64.cppm`, exported as `mljit.x64`  
**Namespace:** `mljit::x64`

### Public Types

```cpp
namespace mljit::x64 {

enum class Gpr : uint8_t {
  rax, rcx, rdx, rbx,
  rsp, rbp, rsi, rdi,
  r8,  r9,  r10, r11,
  r12, r13, r14, r15
};

struct Label {
  uint32_t id;
  // Binding status tracked internally; opaque handle to the caller.
};

struct Mem {
  Gpr     base;
  int32_t disp;      // base + disp32
};

}  // namespace mljit::x64
```

- `Gpr` values are used as indices into encoding tables and also serve as direct
  REX.B/R/ModRM field inputs.
- `Label` is an opaque handle created by the assembler; callers store it and use it
  later for `bind()` or branch targets.
- `Mem` covers the common base+displacement addressing.  v1 does not need scaled-index
  or RIP-relative; those can be added later.

### `Assembler` class

```cpp
class Assembler {
public:
  Assembler();
  ~Assembler();

  // --- Labels ---
  Label new_label();
  void  bind(Label);

  // --- Data movement ---
  void mov_rr(Gpr dst, Gpr src);   // dst = src (64-bit)
  void mov_ri(Gpr dst, int64_t imm);
  void mov_mr(Mem dst, Gpr src);   // [dst] = src
  void mov_rm(Gpr dst, Mem src);   // dst = [src]
  void movzx_rr(Gpr dst, Gpr src); // zero-extend 8→64

  // --- Arithmetic ---
  void add_rr(Gpr dst, Gpr src);
  void sub_rr(Gpr dst, Gpr src);
  void imul_rr(Gpr dst, Gpr src);
  void add_ri(Gpr dst, int32_t imm);
  void sub_ri(Gpr dst, int32_t imm);
  void cqo();           // sign-extend rax → rdx:rax
  void idiv_r(Gpr src); // signed divide rdx:rax by src

  // --- Comparison & setcc ---
  void cmp_rr(Gpr lhs, Gpr rhs);
  void sete (Gpr dst);
  void setne(Gpr dst);
  void setl (Gpr dst);
  void setle(Gpr dst);
  void setg (Gpr dst);
  void setge(Gpr dst);

  // --- Control flow ---
  void jmp    (Label target);
  void je     (Label target);
  void jne    (Label target);
  void jl     (Label target);
  void jle    (Label target);
  void jg     (Label target);
  void jge    (Label target);
  void call   (Label target);
  void call_r (Gpr target);
  void ret();

  // --- Stack ---
  void push(Gpr);
  void pop (Gpr);

  // --- Finalization / introspection ---
  void     finalize();           // patch all pending fixups, prevent further emission
  ExecBuffer executable_copy();  // return a copy of the buffer in executable memory
  size_t   size() const;         // bytes emitted so far

private:
  std::vector<uint8_t> buf_;
  std::vector</*Fixup*/> fixups_;
  std::vector</*LabelState*/> labels_;
  uint32_t next_label_id_ = 0;
};
```

## Labels and Fixups

- `new_label()`: allocates a unique `Label{ .id = next_label_id_++ }` and records it as
  unbound.
- `bind(label)`: records the current `buf_.size()` as the label's offset, then iterates
  the fixup list for any pending rel32 references to that label and patches them in-place
  (little-endian 32-bit signed delta from the end of each fixup site).
- Forward branches / calls: emit a 5-byte (opcode + rel32) placeholder with zero in the
  rel32 slot and push a `(offset_of_rel32, label.id)` fixup entry.  `bind()` resolves
  these.
- Backward `jmp`: when the label is already bound, the assembler checks whether the
  backward displacement fits in a signed 8-bit value.  If yes, it emits a 2-byte
  `jmp rel8`; otherwise a 5-byte `jmp rel32`.  (This optimisation is jmp-only for v1.)
- All conditional jumps (`je`, `jne`, …) and `call(Label)` use rel32 only for v1,
  keeping the encoder simple.  The `jmp rel8` optimisation is opt-in and only applies
  when the label is already bound.

## Instruction Surface (v1)

Below is the complete v1 instruction set.  Each entry lists the assembly mnemonic and
its x86-64 encoding strategy, omitting REX prefix details (see Encoding Traps section).

| Method        | Description                        | Encoding strategy             |
|---------------|------------------------------------|-------------------------------|
| `mov_rr`      | 64-bit register–register move      | REX.W + 0x89 /rm             |
| `mov_ri`      | 64-bit immediate to register       | REX.W + 0xB8+r + imm64       |
| `mov_mr`      | Register to memory                 | REX.W + 0x89 /r              |
| `mov_rm`      | Memory to register                 | REX.W + 0x8B /r              |
| `movzx_rr`   | Zero-extend byte to 64-bit        | REX.W + 0x0F B6 /r           |
| `add_rr`      | 64-bit reg += reg                  | REX.W + 0x01 /r              |
| `sub_rr`      | 64-bit reg -= reg                  | REX.W + 0x29 /r              |
| `imul_rr`     | 64-bit signed multiply dst *= src  | REX.W + 0x0F AF /r           |
| `add_ri`      | 64-bit reg += sign-extended s32    | REX.W + 0x81 /0 + imm32      |
| `sub_ri`      | 64-bit reg -= sign-extended s32    | REX.W + 0x81 /5 + imm32      |
| `cmp_rr`      | Compare reg, reg (sets flags)      | REX.W + 0x39 /r              |
| `sete` / etc  | Set byte on condition              | 0x0F 0x94..0x9F + ModRM      |
| `jmp`(Label)  | Unconditional near jump (rel32)    | 0xE9 + rel32; rel8 when back  |
| `je` / etc    | Conditional near jump (rel32)      | 0x0F 0x84..0x8F + rel32      |
| `call`(Label) | Near call (rel32)                  | 0xE8 + rel32                 |
| `call_r`      | Call via register (absolute)       | REX.W + 0xFF /2              |
| `ret`         | Near return                        | 0xC3                         |
| `push`        | Push register                      | 0x50+r (or REX.B)            |
| `pop`         | Pop register                       | 0x58+r (or REX.B)            |
| `cqo`         | Sign-extend rax → rdx:rax          | REX.W + 0x99                 |
| `idiv_r`      | Signed divide rdx:rax by src       | REX.W + 0xF7 /7              |

Notes:
- `imul_rr` places the destination in ModRM.reg and the source in ModRM.rm — the
  reverse of the `add`/`sub`/`mov`/`cmp` convention where dst is rm and src is reg.
- `idiv_r` operates only on implicit `rdx:rax`; the assembler emits just the `idiv`
  instruction.  Callers must prepare operands (e.g. by emitting `cqo()` beforehand).
- `setCC` writes only the low 8 bits of the destination register.  Codegen must emit
  `movzx_rr(dst, dst)` afterwards if a 64-bit zero-extended value is needed.
- `push`/`pop` use the `50+r` / `58+r` opcodes for the base eight registers and apply
  REX.B for extended registers r8–r15.

## Encoding Traps

### REX prefix rules

A REX prefix byte is `0b0100_W_R_B`:

- **W** (bit 3): set for 64-bit operand size.  All instructions in v1 that operate on
  64-bit values (mov, add, sub, imul, cmp, cqo, idiv) use REX.W = 1.
- **R** (bit 2): extends the ModRM.reg field (bit 0 of the GPR encoding when GPR
  ≥ 8).  Set when the register that occupies the ModRM.reg slot is an extended
  register (r8–r15).
- **B** (bit 0): extends the ModRM.rm field, the base register in memory operands, or
  the register encoded in the opcode (e.g. `push rN` / `pop rN`).  Set when the relevant
  register is r8–r15.

The REX byte is omitted entirely when W, R, and B are all 0 (base registers only).  This
is important for `setCC` (see below).

### `imul_rr` operand order

In contrast to most two-operand ALU ops (add, sub, cmp, mov) where the destination
occupies ModRM.rm and the source occupies ModRM.reg, x86 `imul r64, r/m64` places the
destination in ModRM.reg and the source in ModRM.rm.  The assembler must swap fields
accordingly when encoding.

### `setCC` address-size pitfalls

- `setCC` encodes a ModRM byte whose reg field holds the condition code and whose rm
  field holds the destination register (addressed as a byte).
- Compliant x86-64 behaviour: when the register is rsp (4), rbp (5), rsi (6), or rdi (7),
  the REX prefix (even with W=0, R=0, B=0) changes the addressed byte register from the
  AH/CH/DH/BH legacy aliases to SPL/BPL/SIL/DIL.  The assembler **must** emit a null REX
  prefix (`0b01000000`) for these four registers to obtain the correct byte register.
- For extended registers r8–r15, REX.B is set as usual (REX = `0b01000001` for r8,
  `0b01000010` for r9, etc.); the W and R bits remain 0.
- The `setCC` destination is always the low 8 bits.  If the codegen expects a 64-bit
  condition-value, it must follow the `setCC` with `movzx_rr(dst, dst)`.

### `idiv` implicit operands

`idiv_r` encodes only the `REX.W + 0xF7 /7` form.  The dividend is always `rdx:rax`;
the quotient goes to `rax`, the remainder to `rdx`.  The assembler does not synthesise
`cqo()` or any other preparation — that is the caller's responsibility.

### `call(Label)` vs `call_r`

- `call(Label)` emits a rel32 relative call (5 bytes).  This is only correct when the
  target is within ±2 GiB of the call site, which is true for all intra-module calls in
  the v1 use case.
- `call_r(Gpr)` emits `FF /2` which calls the absolute address held in the register.
  This form exists for calling runtime helpers or function pointers whose address is not
  statically reachable via rel32.

## Executable Buffer

### `ExecBuffer` class (move-only RAII)

```cpp
class ExecBuffer {
public:
  ExecBuffer() = default;
  ~ExecBuffer();   // calls munmap
  ExecBuffer(const ExecBuffer&) = delete;
  ExecBuffer& operator=(const ExecBuffer&) = delete;
  ExecBuffer(ExecBuffer&&) noexcept;
  ExecBuffer& operator=(ExecBuffer&&) noexcept;

  void* ptr() const;
  size_t size() const;
};
```

v1 implementation uses Linux `mmap` with `PROT_READ | PROT_WRITE | PROT_EXEC` (RWX)
for simplicity.  The buffer is allocated with the size of the assembled code, the
assembler contents are `memcpy`'d in, and the `ExecBuffer` is returned to the caller via
`executable_copy()`.  The destructor calls `munmap`.

**Hardening postponed:** switching to W^X with `mprotect` (write-then-execute) or a
dual-mapping approach is a well-understood improvement that can be done after the
prototype works.  It is deliberately not a v1 requirement.

## Tests

All tests are golden-byte comparisons: the test encodes each instruction and compares
the resulting byte sequence against a literal hex array.

| Test case                          | What it covers                                    |
|------------------------------------|---------------------------------------------------|
| `mov_rr`                           | REX.W + 0x89 for every base + extended pair       |
| `mov_ri`                           | REX.W + B8+r + imm64, edge cases imm=0 / -1      |
| `mov_mr`, `mov_rm`                 | Memory forms with rbp/rsp as base, disp32         |
| `add_rr`, `sub_rr`, `cmp_rr`       | ALU ops with reg/reg                              |
| `imul_rr`                          | ModRM.reg = dst, ModRM.rm = src (reverse check)  |
| `add_ri`, `sub_ri`                 | 32-bit sign-extended immediate                    |
| `cqo`                              | REX.W + 0x99, 2 bytes                             |
| `idiv_r`                           | REX.W + 0xF7 /7 with each register                |
| `sete` / `setne` / etc             | Condition code field, null REX for rsp/rbp/rsi/rdi|
| `jmp` backward rel8                | Backward jmp that fits in signed 8 bits           |
| `jmp` forward/backward rel32       | Label patching, forward + long backward           |
| `je` / `jne` / etc forward         | rel32 conditional jumps via label patching        |
| `call(Label)` forward              | rel32 + fixup resolution                          |
| `call_r`                           | FF /2 for each register (including extended)      |
| `push` / `pop`                     | 50+r / 58+r with REX.B for r8–r15                |
| `movzx_rr`                         | REX.W + 0F B6 /r for byte→qword zero-extend      |
| `setCC` + REX edge cases           | Null REX for SPL/BPL/SIL/DIL; REX.B for r8–r15   |
| `executable_copy` smoke test       | Round-trip: emit `mov_ri(rax,42) + ret`, execute, verify result == 42 |

Each test should be a standalone function that calls the assembler, invokes
`executable_copy`, and (for the smoke test) casts the entry point to a function pointer
and calls it.  Non-execution tests simply compare internal `buf_` contents after
finalization (or read them directly via a test-only accessor).
