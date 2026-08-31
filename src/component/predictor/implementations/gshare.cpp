#include "risc-e/component/predictor/implementations/gshare.hpp"

#include <algorithm>
#include <stdexcept>

GsharePredictor::GsharePredictor(std::size_t history_bits, std::size_t ras_depth)
    : history_bits_(history_bits), ras_(ras_depth) {
    configure(history_bits);
}

void GsharePredictor::configure(std::size_t history_bits) {
    if (history_bits < 1 || history_bits > 16) {
        throw std::invalid_argument("gshare history_bits must be in [1, 16]");
    }
    history_bits_ = history_bits;
    history_mask_ = (std::uint32_t(1) << history_bits) - 1;
    counters_.assign(std::size_t(1) << history_bits, 2);
}

std::size_t GsharePredictor::index(std::uint32_t pc) const {
    return (pc ^ global_history_) & (counters_.size() - 1);
}

Prediction GsharePredictor::predict(const BranchContext& ctx) const {
    // Direct jumps are unconditional; predict their encoded target.
    if (ctx.is_jal()) return predict_taken_or_fallthrough(ctx, true);
    if (ctx.is_conditional_branch()) {
        return predict_taken_or_fallthrough(ctx, counter_is_taken(counters_[index(ctx.pc)]));
    }
    // Returns predict the top of the return-address stack; other JALRs have no
    // target source without a BTB, so predict fall-through.
    if (ctx.is_return()) {
        if (const auto target = ras_.peek()) return Prediction{*target};
        return {ctx.fallthrough_pc()};
    }
    return {ctx.fallthrough_pc()};
}

void GsharePredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (ctx.is_conditional_branch()) {
        std::uint8_t& counter = counters_[index(ctx.pc)];
        counter = res.taken ? saturating_increment(counter) : saturating_decrement(counter);

        global_history_ = ((global_history_ << 1) | (res.taken ? 1u : 0u)) & history_mask_;
    } else if (ctx.is_call()) {
        ras_.push(ctx.pc + 4);
    } else if (ctx.is_return()) {
        const auto predicted = ras_.pop();
        if (predicted && *predicted != res.next_pc) ras_.push(res.next_pc);
    }
}

void GsharePredictor::reset() {
    std::fill(counters_.begin(), counters_.end(), 2);
    global_history_ = 0;
    ras_.reset();
}

std::vector<ParamSpec> GsharePredictor::parameters() const {
    return {
        {"history-bits", "pattern-history length (table size = 1 << bits)", 1, 16,
         std::to_string(history_bits_)},
        {"ras-depth", "return-address stack depth (0 disables)", 0,
         static_cast<long>(ReturnAddressStack::kMaxDepth), std::to_string(ras_.depth())},
    };
}

bool GsharePredictor::set_parameter(std::string_view name, std::string_view value,
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
