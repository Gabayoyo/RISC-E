#pragma once

#include <cstdint>

enum class Traits : uint16_t {
  None       = 0,

  // --- control flow ---
  Terminator = 1 << 0,  // unconditionally transfers control (JAL, JALR without link)
  Branch     = 1 << 1,  // conditional branch (BEQ, BNE, etc.)
  Indirect   = 1 << 2,  // target is register (JALR)
  Call       = 1 << 3,  // writes a return address (JAL with link, JALR with link)
  Return     = 1 << 4,  // equivalent to `Indirect | Call` but semantic marker (JALR x0, ra)

  // --- memory ---
  Load       = 1 << 5,  // reads from memory (LB, LW, FLW, …)
  Store      = 1 << 6,  // writes to memory (SB, SW, FSW, …)

  // --- side effects & ordering ---
  SideEffect = 1 << 7,  // any observable effect beyond registers (CSR, I/O, ECALL)
  Barrier    = 1 << 8,  // memory ordering fence (FENCE, FENCE.I)
  System     = 1 << 9,  // privileged / system instruction (ECALL, EBREAK, MRET)

  // --- data type hints ---
  Float      = 1 << 10, // operates on floating-point registers
  Vector     = 1 << 11, // vector extension (V)
  Compressed = 1 << 12, // 16‑bit compressed instruction (C extension)
};

// Allow bitwise combination
constexpr Traits operator|(Traits a, Traits b) {
  return static_cast<Traits>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
constexpr bool operator&(Traits a, Traits b) {
  return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}