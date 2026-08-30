#include "risc-e/cpu/predictors/two_bit_saturating.hpp"

#include <algorithm>

TwoBitSaturatingPredictor::TwoBitSaturatingPredictor(std::size_t table_size)
    : counters_(table_size, 2) {}

std::size_t TwoBitSaturatingPredictor::index(std::uint32_t pc) const {
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
    counter = res.taken ? saturating_increment(counter) : saturating_decrement(counter);
}

void TwoBitSaturatingPredictor::reset() {
    std::fill(counters_.begin(), counters_.end(), 2);
}
