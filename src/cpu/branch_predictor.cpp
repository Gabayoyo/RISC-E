#include "risc-e/cpu/branch_predictor.hpp"

#include <algorithm>

BranchContext BranchContext::from_decoded(const DecodedInstruction& d) {
    BranchContext ctx;
    ctx.pc     = d.addr;
    ctx.opcode = d.opcode;
    ctx.funct3 = d.funct3;
    ctx.rd     = d.rd;
    ctx.rs1    = d.rs1;
    ctx.rs2    = d.rs2;
    ctx.imm    = d.imm;
    return ctx;
}

TwoBitSaturatingPredictor::TwoBitSaturatingPredictor(std::size_t table_size)
    : counters_(table_size, 2) {}

std::size_t TwoBitSaturatingPredictor::index(uint32_t pc) const {
    if (counters_.empty()) return 0;
    // Table size must be a power of two so the hash can be masked (no division).
    return (pc ^ (pc >> 4) ^ (pc >> 8)) & (counters_.size() - 1);
}

Prediction TwoBitSaturatingPredictor::predict(const BranchContext& ctx) const {
    // Direct jumps are unconditional; predict their encoded target.
    if (ctx.is_jal()) {
        return {ctx.direct_target()};
    }
    if (ctx.is_conditional_branch()) {
        const bool taken = counters_[index(ctx.pc)] >= 2;
        return {taken ? ctx.direct_target() : ctx.fallthrough_pc()};
    }
    // JALR: no target source without a BTB, so predict fall-through.
    return {ctx.fallthrough_pc()};
}

void TwoBitSaturatingPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (!ctx.is_conditional_branch()) return;

    std::uint8_t& counter = counters_[index(ctx.pc)];
    if (res.taken) {
        counter = static_cast<std::uint8_t>(std::min<int>(3, counter + 1));
    } else {
        counter = static_cast<std::uint8_t>(std::max<int>(0, counter - 1));
    }
}
