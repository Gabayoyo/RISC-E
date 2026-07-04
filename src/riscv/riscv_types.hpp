#pragma once

#include <cstdint>
#include <array>

// ---- Fields extracted from a 32-bit instruction ----
struct DecodedInstruction {
    uint32_t raw;            // original instruction word
    uint8_t  opcode;         // bits [6:0]
    uint8_t  rd;             // destination register, bits [11:7]
    uint8_t  funct3;         // bits [14:12]
    uint8_t  rs1;            // source register 1, bits [19:15]
    uint8_t  rs2;            // source register 2, bits [24:20]
    uint8_t  funct7;         // bits [31:25] (for R-type)
    int32_t  imm;            // sign‑extended immediate (varies by type)

    bool terminates_block;   // true for BRANCH, JAL, JALR, SYSTEM, etc.
    bool is_direct_branch;   // BRANCH, JAL → target computable from imm
    bool is_indirect_jump;   // JALR → target not statically known
    uint32_t fallthrough_pc; // address + 4 (only valid if terminates_block == false)
    uint32_t branch_target;  // computed for direct branches / JAL

    // Optional classification
    enum class Format {
        R, I, S, B, U, J, UNKNOWN
    } format;
};

static consteval std::array<DecodedInstruction::Format, 128> build_format_table() {
        std::array<DecodedInstruction::Format, 128> t{};
        t.fill(DecodedInstruction::Format::UNKNOWN);

        // I‑type
        std::get<0b11001>(t) = DecodedInstruction::Format::I; // JALR
        std::get<0b00000>(t) = DecodedInstruction::Format::I; // LOAD
        std::get<0b00011>(t) = DecodedInstruction::Format::I; // MISC_MEM
        std::get<0b00100>(t) = DecodedInstruction::Format::I; // OP_IMM
        std::get<0b00110>(t) = DecodedInstruction::Format::I; // OP_IMM_32
        std::get<0b11100>(t) = DecodedInstruction::Format::I; // SYSTEM

        // U‑type
        std::get<0b01101>(t) = DecodedInstruction::Format::U; // LUI
        std::get<0b00101>(t) = DecodedInstruction::Format::U; // AUIPC

        // J‑type
        std::get<0b11011>(t) = DecodedInstruction::Format::J; // JAL

        // B‑type
        std::get<0b11000>(t) = DecodedInstruction::Format::B; // BRANCH

        // S‑type
        std::get<0b01000>(t) = DecodedInstruction::Format::S; // STORE
        std::get<0b01001>(t) = DecodedInstruction::Format::S; // STORE_FP

        // R‑type
        std::get<0b01100>(t) = DecodedInstruction::Format::R; // OP
        std::get<0b01110>(t) = DecodedInstruction::Format::R; // OP_32
        std::get<0b01011>(t) = DecodedInstruction::Format::R; // AMO

        return t;
};

inline constexpr std::array<DecodedInstruction::Format, 128> format_table = build_format_table();