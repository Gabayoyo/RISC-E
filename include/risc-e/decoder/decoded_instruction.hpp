#pragma once

#include <cstdint>

// ---- Fields extracted from a 32-bit instruction ----
struct DecodedInstruction {
    uint32_t addr;           // address of the instruction in memory (for debugging/tracing)
    uint32_t raw;            // original instruction word
    uint8_t  opcode;         // bits [6:0]
    uint8_t  rd;             // destination register, bits [11:7]
    uint8_t  funct3;         // bits [14:12]
    uint8_t  rs1;            // source register 1, bits [19:15]
    uint8_t  rs2;            // source register 2, bits [24:20]
    uint8_t  funct7;         // bits [31:25] (for R-type)
    int32_t  imm;            // sign-extended immediate (varies by type)
};
