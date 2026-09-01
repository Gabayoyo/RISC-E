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

// funct3 / funct7 encodings of the RV32I base ISA. Shared by the
// interpreter and the disassembler so instruction classification can never
// drift between execution and display.
inline constexpr uint8_t F3_BEQ  = 0b000;
inline constexpr uint8_t F3_BNE  = 0b001;
inline constexpr uint8_t F3_BLT  = 0b100;
inline constexpr uint8_t F3_BGE  = 0b101;
inline constexpr uint8_t F3_BLTU = 0b110;
inline constexpr uint8_t F3_BGEU = 0b111;

inline constexpr uint8_t F3_ADDI  = 0b000;
inline constexpr uint8_t F3_SLTI  = 0b010;
inline constexpr uint8_t F3_SLTIU = 0b011;
inline constexpr uint8_t F3_XORI  = 0b100;
inline constexpr uint8_t F3_ORI   = 0b110;
inline constexpr uint8_t F3_ANDI  = 0b111;
inline constexpr uint8_t F3_SLLI  = 0b001;
inline constexpr uint8_t F3_SRLI  = 0b101;  // SRAI shares funct3 with SRLI

inline constexpr uint8_t F7_SLLI = 0x00;
inline constexpr uint8_t F7_SRAI = 0x20;

inline constexpr uint8_t F3_ADD_SUB = 0b000;
inline constexpr uint8_t F3_SLL     = 0b001;
inline constexpr uint8_t F3_SLT     = 0b010;
inline constexpr uint8_t F3_SLTU    = 0b011;
inline constexpr uint8_t F3_XOR     = 0b100;
inline constexpr uint8_t F3_SRL_SRA = 0b101;
inline constexpr uint8_t F3_OR      = 0b110;
inline constexpr uint8_t F3_AND     = 0b111;

inline constexpr uint8_t F7_SUB_SRA = 0x20;

inline constexpr uint8_t F3_LB  = 0b000;
inline constexpr uint8_t F3_LH  = 0b001;
inline constexpr uint8_t F3_LW  = 0b010;
inline constexpr uint8_t F3_LBU = 0b100;
inline constexpr uint8_t F3_LHU = 0b101;

inline constexpr uint8_t F3_SB = 0b000;
inline constexpr uint8_t F3_SH = 0b001;
inline constexpr uint8_t F3_SW = 0b010;
