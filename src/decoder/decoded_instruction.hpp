#pragma once

#include <cstdint>
#include <array>
#include <algorithm>

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
    int32_t  imm;            // sign‑extended immediate (varies by type)

    // Optional classification
    enum class Format {
        R, I, S, B, U, J, UNKNOWN
    } format;
};

static consteval std::array<DecodedInstruction::Format, 128> build_format_table() {
        std::array<DecodedInstruction::Format, 128> t{};
        t.fill(DecodedInstruction::Format::UNKNOWN);

        // I‑type
    t[0b0000011] = DecodedInstruction::Format::I; // LOAD
    t[0b0001111] = DecodedInstruction::Format::I; // MISC_MEM
    t[0b0010011] = DecodedInstruction::Format::I; // OP_IMM
    t[0b0011011] = DecodedInstruction::Format::I; // OP_IMM_32
    t[0b1100111] = DecodedInstruction::Format::I; // JALR
    t[0b1110011] = DecodedInstruction::Format::I; // SYSTEM

        // U‑type
    t[0b0110111] = DecodedInstruction::Format::U; // LUI
    t[0b0010111] = DecodedInstruction::Format::U; // AUIPC

        // J‑type
    t[0b1101111] = DecodedInstruction::Format::J; // JAL

        // B‑type
    t[0b1100011] = DecodedInstruction::Format::B; // BRANCH

        // S‑type
    t[0b0100011] = DecodedInstruction::Format::S; // STORE
    t[0b0100111] = DecodedInstruction::Format::S; // STORE_FP

        // R‑type
    t[0b0110011] = DecodedInstruction::Format::R; // OP
    t[0b0111011] = DecodedInstruction::Format::R; // OP_32
    t[0b0101111] = DecodedInstruction::Format::R; // AMO

        return t;
};

inline constexpr std::array<DecodedInstruction::Format, 128> format_table = build_format_table();