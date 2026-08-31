#include "risc-e/component/predictor/implementations/always_not_taken.hpp"

Prediction AlwaysNotTakenPredictor::predict(const BranchContext& ctx) const {
    // Direct jumps are unconditional; predict their encoded target.
    if (ctx.is_jal()) {
        return {ctx.direct_target()};
    }
    // Everything else falls through (indirect jumps are predicted wrong).
    return {ctx.fallthrough_pc()};
}

void AlwaysNotTakenPredictor::resolve(const BranchContext& ctx, const Resolution& res) {
    (void)ctx;
    (void)res;
}
