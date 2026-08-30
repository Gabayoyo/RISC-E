#pragma once

#include <cstdint>

// Single source of truth for RISC-V base opcodes (instruction bits [6:0]).
// Everyone who switches on or classifies opcodes includes this header instead
// of redefining the literals.
namespace opcode {

inline constexpr uint8_t kLoad     = 0b0000011;  // LOAD
inline constexpr uint8_t kMiscMem  = 0b0001111;  // MISC-MEM (FENCE / FENCE.I)
inline constexpr uint8_t kOpImm    = 0b0010011;  // OP-IMM
inline constexpr uint8_t kAuipc    = 0b0010111;  // AUIPC
inline constexpr uint8_t kStore    = 0b0100011;  // STORE
inline constexpr uint8_t kAmo      = 0b0101111;  // AMO
inline constexpr uint8_t kLui      = 0b0110111;  // LUI
inline constexpr uint8_t kOp       = 0b0110011;  // OP
inline constexpr uint8_t kOpImm32  = 0b0011011;  // OP-IMM-32 (RV64I)
inline constexpr uint8_t kOp32     = 0b0111011;  // OP-32 (RV64I)
inline constexpr uint8_t kStoreFp  = 0b0100111;  // STORE-FP (RV32F)
inline constexpr uint8_t kBranch   = 0b1100011;  // BRANCH
inline constexpr uint8_t kJal      = 0b1101111;  // JAL
inline constexpr uint8_t kJalr     = 0b1100111;  // JALR
inline constexpr uint8_t kSystem   = 0b1110011;  // SYSTEM (ECALL / EBREAK / CSRs)

} // namespace opcode
