#include "risc-e/cpu/predictors/ras.hpp"

#include <string>

RasPredictor::RasPredictor(std::size_t depth) : ras_(depth) {}

Prediction RasPredictor::predict(const BranchContext& ctx) const {
    if (ctx.is_jal()) return predict_taken_or_fallthrough(ctx, true);
    if (ctx.is_return()) {
        if (const auto target = ras_.peek()) return Prediction{*target};
        return {ctx.fallthrough_pc()};
    }
    // Conditional branches and non-return JALRs: predict not taken.
    return {ctx.fallthrough_pc()};
}

void RasPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    if (ctx.is_call()) {
        ras_.push(ctx.pc + 4);
    } else if (ctx.is_return()) {
        const auto predicted = ras_.pop();
        if (predicted && *predicted != res.next_pc) ras_.push(res.next_pc);
    }
}

void RasPredictor::reset() { ras_.reset(); }

std::vector<ParamSpec> RasPredictor::parameters() const {
    return {{"ras-depth", "return-address stack depth (0 disables)", 0,
             static_cast<long>(ReturnAddressStack::kMaxDepth), std::to_string(ras_.depth())}};
}

bool RasPredictor::set_parameter(std::string_view name, std::string_view value,
                                 std::string& error) {
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
