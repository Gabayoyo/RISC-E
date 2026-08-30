#include "risc-e/cpu/predictors/tournament.hpp"

#include <algorithm>
#include <stdexcept>

TournamentPredictor::TournamentPredictor(std::size_t history_bits, std::size_t ras_depth)
    : history_bits_(history_bits), ras_(ras_depth) {
    configure(history_bits);
}

void TournamentPredictor::configure(std::size_t history_bits) {
    if (history_bits < 1 || history_bits > 16) {
        throw std::invalid_argument("tournament history_bits must be in [1, 16]");
    }
    history_bits_ = history_bits;
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
    if (ctx.is_jal()) return predict_taken_or_fallthrough(ctx, true);
    if (ctx.is_conditional_branch()) {
        const std::size_t li = local_index(ctx.pc);
        const std::uint32_t local_hist = local_history_[li] & history_mask_;
        const bool taken = counter_is_taken(choice_pht_[global_history_])
            ? counter_is_taken(local_pht_[local_hist])
            : counter_is_taken(global_pht_[global_history_]);
        return predict_taken_or_fallthrough(ctx, taken);
    }
    // Returns predict the top of the return-address stack; other JALRs have no
    // target source without a BTB, so predict fall-through.
    if (ctx.is_return()) {
        if (const auto target = ras_.peek()) return Prediction{*target};
        return {ctx.fallthrough_pc()};
    }
    return {ctx.fallthrough_pc()};
}

void TournamentPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (ctx.is_conditional_branch()) {
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
    } else if (ctx.is_call()) {
        ras_.push(ctx.pc + 4);
    } else if (ctx.is_return()) {
        const auto predicted = ras_.pop();
        if (predicted && *predicted != res.next_pc) ras_.push(res.next_pc);
    }
}

void TournamentPredictor::reset() {
    std::fill(local_history_.begin(), local_history_.end(), 0);
    std::fill(local_pht_.begin(), local_pht_.end(), 2);
    std::fill(global_pht_.begin(), global_pht_.end(), 2);
    std::fill(choice_pht_.begin(), choice_pht_.end(), 2);
    global_history_ = 0;
    ras_.reset();
}

std::vector<ParamSpec> TournamentPredictor::parameters() const {
    return {
        {"history-bits", "pattern-history length (sizes every table)", 1, 16,
         std::to_string(history_bits_)},
        {"ras-depth", "return-address stack depth (0 disables)", 0,
         static_cast<long>(ReturnAddressStack::kMaxDepth), std::to_string(ras_.depth())},
    };
}

bool TournamentPredictor::set_parameter(std::string_view name, std::string_view value,
                                        std::string& error) {
    if (name == "history-bits") {
        const auto parsed = parse_parameter_value(value, error);
        if (!parsed) return false;
        try {
            configure(static_cast<std::size_t>(*parsed));
        } catch (const std::invalid_argument& e) {
            error = e.what();
            return false;
        }
        return true;
    }
    if (name == "ras-depth") {
        const auto parsed = parse_parameter_value(value, error);
        if (!parsed) return false;
        if (*parsed > static_cast<long>(ReturnAddressStack::kMaxDepth)) {
            error = "ras-depth must be in [0, " + std::to_string(ReturnAddressStack::kMaxDepth) + "]";
            return false;
        }
        ras_.resize(static_cast<std::size_t>(*parsed));
        return true;
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
