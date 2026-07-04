#include "src/riscv/decoder.hpp"
#include "src/riscv/riscv_types.hpp"

#include <array>

DecodedInstruction decode_raw_inst(uint32_t inst) {
    DecodedInstruction d;
    d.raw    = inst;
    d.opcode = inst & 0x7F;
    d.rd     = (inst >> 7)  & 0x1F;
    d.funct3 = (inst >> 12) & 0x07;
    d.rs1    = (inst >> 15) & 0x1F;
    d.rs2    = (inst >> 20) & 0x1F;
    d.funct7 = (inst >> 25) & 0x7F;

    d.format = format_table[d.opcode];

    switch (d.format) {
        case DecodedInstruction::Format::I:
            d.imm = static_cast<int32_t>(inst) >> 20;           // sign‑extend
            break;
        case DecodedInstruction::Format::S:
            d.imm = ((static_cast<int32_t>(inst) >> 20) & ~0x1F)  // bits [11:5], sign-extended
                | ((inst >> 7) & 0x1F);  
            break;
        case DecodedInstruction::Format::B:
            d.imm = ((static_cast<int32_t>(inst) >> 31) << 12)
                | (((inst >> 7) & 0x1)   << 11)
                | (((inst >> 25) & 0x3F) << 5)
                | (((inst >> 8) & 0xF)   << 1);
            break;
        case DecodedInstruction::Format::U:
            d.imm = inst & 0xFFFFF000;
            break;
        case DecodedInstruction::Format::J:
            d.imm = ((static_cast<int32_t>(inst) >> 31) << 20)
                | (((inst >> 12) & 0xFF)  << 12)
                | (((inst >> 20) & 0x1)   << 11)
                | (((inst >> 21) & 0x3FF) << 1);
            break;
        case DecodedInstruction::Format::R:
        default:
            d.imm = 0;
            break;
    }
    return d;
}