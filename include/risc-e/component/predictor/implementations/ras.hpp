#pragma once

#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/predictor/return_address_stack.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Baseline predictor for indirect control flow: every transfer is predicted
// not taken except returns, which predict the top of the return-address
// stack. Isolates the effect of the RAS alone (no direction tables).
class RasPredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "ras";

    explicit RasPredictor(std::size_t depth = ReturnAddressStack::kDefaultDepth);

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    std::string_view name() const override { return kName; }
    void reset() override;

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

private:
    ReturnAddressStack ras_;
};
