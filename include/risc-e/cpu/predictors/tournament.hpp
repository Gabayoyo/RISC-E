#pragma once

#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/return_address_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Tournament predictor: a local-history predictor and a global-history
// predictor compete, with a choice table (indexed by global history)
// selecting which one drives the prediction. Also owns a return-address
// stack for call/return targets.
class TournamentPredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "tournament";
    static constexpr std::size_t kDefaultHistoryBits = 10;

    // history_bits sizes every table (1 << history_bits entries).
    // ras_depth 0 disables the return-address stack.
    explicit TournamentPredictor(std::size_t history_bits = kDefaultHistoryBits,
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
    std::vector<std::uint32_t> local_history_;  // per-PC recent outcomes
    std::vector<std::uint8_t> local_pht_;       // indexed by local history
    std::vector<std::uint8_t> global_pht_;      // indexed by global history
    std::vector<std::uint8_t> choice_pht_;      // >= 2 selects the local predictor
    ReturnAddressStack ras_;

    std::size_t local_index(std::uint32_t pc) const;
    void configure(std::size_t history_bits);  // validate + (re)build the tables
};
