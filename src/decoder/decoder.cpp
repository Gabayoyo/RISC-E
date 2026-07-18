#include "src/decoder/decoder.hpp"
#include "src/decoder/decoded_instruction.hpp"

#include <cstdint>
#include <iostream>
#include <bitset>

namespace {

int32_t decode_i_imm(uint32_t inst) {
    return static_cast<int32_t>(inst) >> 20;
}

int32_t decode_s_imm(uint32_t inst) {
    return ((static_cast<int32_t>(inst) >> 20) & ~0x1F) | ((inst >> 7) & 0x1F);
}

int32_t decode_b_imm(uint32_t inst) {
    return ((static_cast<int32_t>(inst) >> 19) & 0xFFFFF000)
        | (((inst >> 7) & 0x1) << 11)
        | (((inst >> 25) & 0x3F) << 5)
        | (((inst >> 8) & 0xF) << 1);
}

int32_t decode_u_imm(uint32_t inst) {
    return static_cast<int32_t>(inst & 0xFFFFF000);
}

int32_t decode_j_imm(uint32_t inst) {
    return ((static_cast<int32_t>(inst) >> 11) & 0xFFF00000)
        | (((inst >> 12) & 0xFF) << 12)
        | (((inst >> 20) & 0x1) << 11)
        | (((inst >> 21) & 0x3FF) << 1);
}

} // namespace

DecodedInstruction decode_raw_inst(uint32_t inst, uint32_t addr) {
    DecodedInstruction d;
    d.raw    = inst;
    d.opcode = inst & 0x7F;
    d.addr   = addr;
    d.rd     = (inst >> 7)  & 0x1F;
    d.funct3 = (inst >> 12) & 0x07;
    d.rs1    = (inst >> 15) & 0x1F;
    d.rs2    = (inst >> 20) & 0x1F;
    d.funct7 = (inst >> 25) & 0x7F;

    d.format = format_table[d.opcode];

    switch (d.format) {
        case DecodedInstruction::Format::I:
            d.imm = decode_i_imm(inst);
            break;
        case DecodedInstruction::Format::S:
            d.imm = decode_s_imm(inst);
            break;
        case DecodedInstruction::Format::B:
            d.imm = decode_b_imm(inst);
            break;
        case DecodedInstruction::Format::U:
            d.imm = decode_u_imm(inst);
            break;
        case DecodedInstruction::Format::J:
            d.imm = decode_j_imm(inst);
            break;
        case DecodedInstruction::Format::R:
        default:
            d.imm = 0;
            break;
    return d;
}