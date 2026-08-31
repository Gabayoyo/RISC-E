#pragma once

#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/predictor/return_address_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Classic 2-bit saturating counter predictor indexed by a PC hash, with a
// return-address stack for call/return targets.
class TwoBitSaturatingPredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "two-bit";
    static constexpr std::size_t kDefaultTableSize = 1024;

    // table_size must be a power of two (the index uses a mask, not a division).
    // ras_depth 0 disables the return-address stack.
    explicit TwoBitSaturatingPredictor(std::size_t table_size = kDefaultTableSize,
                                       std::size_t ras_depth = ReturnAddressStack::kDefaultDepth);

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    std::string_view name() const override { return kName; }
    void reset() override;

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

private:
    std::size_t table_size_ = kDefaultTableSize;
    std::vector<std::uint8_t> counters_;  // 0..3, >= 2 means "taken"
    ReturnAddressStack ras_;
    std::size_t index(std::uint32_t pc) const;
    void configure(std::size_t table_size);  // validate + (re)build the table
};
