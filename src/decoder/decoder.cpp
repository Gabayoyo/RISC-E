#include "risc-e/decoder/decoder.hpp"

#include "risc-e/decoder/opcodes.hpp"

#include <array>
#include <cstdint>

namespace {

// Instruction format classification. Internal to the decoder: used only to
// decide how to sign-extend the immediate. Kept out of DecodedInstruction
// because no consumer needs it (the interpreter switches on the opcode).
enum class Format : uint8_t {
    R, I, S, B, U, J, UNKNOWN
};

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

constexpr std::array<Format, 128> build_format_table() {
    std::array<Format, 128> t{};
    t.fill(Format::UNKNOWN);

    // I-type
    t[opcode::kLoad]    = Format::I;
    t[opcode::kMiscMem] = Format::I;
    t[opcode::kOpImm]   = Format::I;
    t[opcode::kOpImm32] = Format::I;
    t[opcode::kJalr]    = Format::I;
    t[opcode::kSystem]  = Format::I;

    // U-type
    t[opcode::kLui]   = Format::U;
    t[opcode::kAuipc] = Format::U;

    // J-type
    t[opcode::kJal] = Format::J;

    // B-type
    t[opcode::kBranch] = Format::B;

    // S-type
    t[opcode::kStore]   = Format::S;
    t[opcode::kStoreFp] = Format::S;

    // R-type
    t[opcode::kOp]   = Format::R;
    t[opcode::kOp32] = Format::R;
    t[opcode::kAmo]  = Format::R;

    return t;
}

inline constexpr std::array<Format, 128> format_table = build_format_table();

Format classify_opcode(uint8_t op) {
    if (op < 128) return format_table[op];
    return Format::UNKNOWN;
}

} // namespace

DecodedInstruction decode_raw_inst(uint32_t inst, uint32_t addr) {
    DecodedInstruction d{};
    d.addr    = addr;
    d.raw     = inst;
    d.opcode  = inst & 0x7F;
    d.rd      = (inst >> 7) & 0x1F;
    d.funct3  = (inst >> 12) & 0x7;
    d.rs1     = (inst >> 15) & 0x1F;
    d.rs2     = (inst >> 20) & 0x1F;
    d.funct7  = (inst >> 25) & 0x7F;

    switch (classify_opcode(d.opcode)) {
        case Format::R:
            d.imm = 0;
            break;
        case Format::I:
            d.imm = decode_i_imm(inst);
            break;
        case Format::S:
            d.imm = decode_s_imm(inst);
            break;
        case Format::B:
            d.imm = decode_b_imm(inst);
            break;
        case Format::U:
            d.imm = decode_u_imm(inst);
            break;
        case Format::J:
            d.imm = decode_j_imm(inst);
            break;
        default:
            d.imm = 0;
            break;
    }

    return d;
}
