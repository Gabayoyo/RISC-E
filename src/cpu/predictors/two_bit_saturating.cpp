#include "risc-e/cpu/predictors/two_bit_saturating.hpp"

#include <algorithm>
#include <stdexcept>

TwoBitSaturatingPredictor::TwoBitSaturatingPredictor(std::size_t table_size, std::size_t ras_depth)
    : table_size_(table_size), ras_(ras_depth) {
    configure(table_size);
}

void TwoBitSaturatingPredictor::configure(std::size_t table_size) {
    if (table_size == 0 || (table_size & (table_size - 1)) != 0) {
        throw std::invalid_argument("two-bit table_size must be a nonzero power of two");
    }
    table_size_ = table_size;
    counters_.assign(table_size, 2);
}

std::size_t TwoBitSaturatingPredictor::index(std::uint32_t pc) const {
    // Table size is validated to be a power of two, so the hash can be masked (no division).
    return (pc ^ (pc >> 4) ^ (pc >> 8)) & (counters_.size() - 1);
}

Prediction TwoBitSaturatingPredictor::predict(const BranchContext& ctx) const {
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

void TwoBitSaturatingPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (ctx.is_conditional_branch()) {
        std::uint8_t& counter = counters_[index(ctx.pc)];
        counter = res.taken ? saturating_increment(counter) : saturating_decrement(counter);
    } else if (ctx.is_call()) {
        ras_.push(ctx.pc + 4);
    } else if (ctx.is_return()) {
        const auto predicted = ras_.pop();
        if (predicted && *predicted != res.next_pc) ras_.push(res.next_pc);
    }
}

void TwoBitSaturatingPredictor::reset() {
    std::fill(counters_.begin(), counters_.end(), 2);
    ras_.reset();
}

std::vector<ParamSpec> TwoBitSaturatingPredictor::parameters() const {
    return {
        {"table-size", "number of 2-bit counters (power of two)", 1, 0,
         std::to_string(table_size_)},
        {"ras-depth", "return-address stack depth (0 disables)", 0,
         static_cast<long>(ReturnAddressStack::kMaxDepth), std::to_string(ras_.depth())},
    };
}

bool TwoBitSaturatingPredictor::set_parameter(std::string_view name, std::string_view value,
                                              std::string& error) {
    if (name == "table-size") {
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
