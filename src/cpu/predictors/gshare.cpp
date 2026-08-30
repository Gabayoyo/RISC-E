#include "risc-e/cpu/predictors/gshare.hpp"

#include <algorithm>
#include <stdexcept>

GsharePredictor::GsharePredictor(std::size_t history_bits) {
    if (history_bits < 1 || history_bits > 16) {
        throw std::invalid_argument("gshare history_bits must be in [1, 16]");
    }
    history_mask_ = (std::uint32_t(1) << history_bits) - 1;
    counters_.assign(std::size_t(1) << history_bits, 2);
}

std::size_t GsharePredictor::index(std::uint32_t pc) const {
    return (pc ^ global_history_) & (counters_.size() - 1);
}

Prediction GsharePredictor::predict(const BranchContext& ctx) const {
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

void GsharePredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (!ctx.is_conditional_branch()) return;

    std::uint8_t& counter = counters_[index(ctx.pc)];
    counter = res.taken ? saturating_increment(counter) : saturating_decrement(counter);

    global_history_ = ((global_history_ << 1) | (res.taken ? 1u : 0u)) & history_mask_;
}

void GsharePredictor::reset() {
    std::fill(counters_.begin(), counters_.end(), 2);
    global_history_ = 0;
}
