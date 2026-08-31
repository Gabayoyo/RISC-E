#pragma once

#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/return_address_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Gshare predictor: a global history register XORed with the PC indexes a
// table of 2-bit saturating counters, plus a return-address stack for
// call/return targets.
class GsharePredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "gshare";
    static constexpr std::size_t kDefaultHistoryBits = 12;

    // history_bits selects the table size (1 << history_bits entries).
    // ras_depth 0 disables the return-address stack.
    explicit GsharePredictor(std::size_t history_bits = kDefaultHistoryBits,
                             std::size_t ras_depth = ReturnAddressStack::kDefaultDepth);

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    std::string_view name() const override { return kName; }
    void reset() override;

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

private:
    std::size_t history_bits_ = kDefaultHistoryBits;
    std::uint32_t global_history_ = 0;
    std::uint32_t history_mask_ = 0;
    std::vector<std::uint8_t> counters_;  // 0..3, >= 2 means "taken"
    ReturnAddressStack ras_;

    std::size_t index(std::uint32_t pc) const;
    void configure(std::size_t history_bits);  // validate + (re)build the table
};
