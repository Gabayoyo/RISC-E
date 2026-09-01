#include "risc-e/component/predictor/implementations/ras.hpp"

#include <string>

RasPredictor::RasPredictor(std::size_t depth) : ras_(depth) {}

Prediction RasPredictor::predict(const BranchContext& ctx) const {
    if (ctx.is_jal()) return predict_taken_or_fallthrough(ctx, true);
    return predict_call_return(ctx, ras_);
}

void RasPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    resolve_call_return(ctx, res, ras_);
}

void RasPredictor::reset() { ras_.reset(); }

std::vector<ParamSpec> RasPredictor::parameters() const {
    return {ras_depth_parameter(ras_)};
}

bool RasPredictor::set_parameter(std::string_view name, std::string_view value,
                                 std::string& error) {
    if (name == "ras-depth") return set_ras_depth_parameter(value, ras_, error);
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
