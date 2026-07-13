#pragma once

#include <cstdint>
#include <variant>

// TODO: some unifying parent class for type safety?

// Register operand: x0–x31
struct Reg  { uint8_t index; };
// Immediate operand: sign-extended constant
struct Imm  { int32_t value; };
// Memory operand: [base + offset]  (for loads/stores)
struct Mem  { uint8_t base; int32_t offset; };