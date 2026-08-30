#include "risc-e/cpu/predictors/tournament.hpp"

#include <algorithm>
#include <stdexcept>

TournamentPredictor::TournamentPredictor(std::size_t history_bits) {
    if (history_bits < 1 || history_bits > 16) {
        throw std::invalid_argument("tournament history_bits must be in [1, 16]");
    }
    const std::size_t table_size = std::size_t(1) << history_bits;
    history_mask_ = (std::uint32_t(1) << history_bits) - 1;
    local_history_.assign(table_size, 0);
    local_pht_.assign(table_size, 2);
    global_pht_.assign(table_size, 2);
    choice_pht_.assign(table_size, 2);
}

std::size_t TournamentPredictor::local_index(std::uint32_t pc) const {
    return (pc ^ (pc >> 4)) & (local_history_.size() - 1);
}

Prediction TournamentPredictor::predict(const BranchContext& ctx) const {
    // Direct jumps are unconditional; predict their encoded target.
    if (ctx.is_jal()) {
        return {ctx.direct_target()};
    }
    if (ctx.is_conditional_branch()) {
        const std::size_t li = local_index(ctx.pc);
        const std::uint32_t local_hist = local_history_[li] & history_mask_;
        const bool local_taken = local_pht_[local_hist] >= 2;
        const bool global_taken = global_pht_[global_history_] >= 2;
        const bool use_local = choice_pht_[global_history_] >= 2;
        const bool taken = use_local ? local_taken : global_taken;
        return {taken ? ctx.direct_target() : ctx.fallthrough_pc()};
    }
    // JALR: no target source without a BTB, so predict fall-through.
    return {ctx.fallthrough_pc()};
}

void TournamentPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (!ctx.is_conditional_branch()) return;

    const std::size_t li = local_index(ctx.pc);
    const std::uint32_t local_hist = local_history_[li] & history_mask_;
    const bool local_taken = local_pht_[local_hist] >= 2;
    const bool global_taken = global_pht_[global_history_] >= 2;

    local_pht_[local_hist] =
        res.taken ? saturating_increment(local_pht_[local_hist])
                  : saturating_decrement(local_pht_[local_hist]);
    global_pht_[global_history_] =
        res.taken ? saturating_increment(global_pht_[global_history_])
                  : saturating_decrement(global_pht_[global_history_]);

    // Only adjust the choice when the components disagree; the winner is
    // rewarded.
    if (local_taken != global_taken) {
        std::uint8_t& choice = choice_pht_[global_history_];
        if (local_taken == res.taken) {
            choice = saturating_increment(choice);
        } else {
            choice = saturating_decrement(choice);
        }
    }

    local_history_[li] =
        ((local_history_[li] << 1) | (res.taken ? 1u : 0u)) & history_mask_;
    global_history_ =
        ((global_history_ << 1) | (res.taken ? 1u : 0u)) & history_mask_;
}

void TournamentPredictor::reset() {
    std::fill(local_history_.begin(), local_history_.end(), 0);
    std::fill(local_pht_.begin(), local_pht_.end(), 2);
    std::fill(global_pht_.begin(), global_pht_.end(), 2);
    std::fill(choice_pht_.begin(), choice_pht_.end(), 2);
    global_history_ = 0;
}
