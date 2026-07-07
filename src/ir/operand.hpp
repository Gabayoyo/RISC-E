#pragma once

#include <cstdint>
#include <variant>

enum class OperandKind { Register, Immediate, Memory };

// Register operand: x0–x31
struct Reg  { uint8_t index; };           // e.g. x5
// Immediate operand: sign-extended constant
struct Imm  { int32_t value; };
// Memory operand: [base + offset]  (for loads/stores)
struct Mem  { uint8_t base; int32_t offset; };

struct Operand {
    OperandKind kind;

    std::variant<Reg, Imm, Mem> data;

    static Operand reg(uint8_t r)                    { return {OperandKind::Register, Reg{r}}; }
    static Operand imm(int32_t v)                    { return {OperandKind::Immediate, Imm{v}}; }
    static Operand mem(uint8_t base, int32_t offset) { return {OperandKind::Memory, Mem{base, offset}}; }

    // Convenience accessors
    uint8_t regIndex()  const { return std::get<Reg>(data).index;  }
    int32_t immValue()  const { return std::get<Imm>(data).value;  }
    const Mem& memRef() const { return std::get<Mem>(data);        }
};