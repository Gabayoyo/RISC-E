#pragma once

#include "risc-e/component/predictor/branch_predictor.hpp"

#include <string_view>

// Trivial baseline predictor: conditional branches are predicted not taken.
class AlwaysNotTakenPredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "always-not-taken";

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    std::string_view name() const override { return kName; }
};
