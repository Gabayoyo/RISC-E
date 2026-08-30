#pragma once

#include "risc-e/cpu/branch_predictor.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Classic 2-bit saturating counter predictor indexed by a PC hash.
class TwoBitSaturatingPredictor : public BranchPredictor {
public:
    static constexpr std::string_view kName = "two-bit";
    static constexpr std::size_t kDefaultTableSize = 1024;

    // table_size must be a power of two (the index uses a mask, not a division).
    explicit TwoBitSaturatingPredictor(std::size_t table_size = kDefaultTableSize);

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    std::string_view name() const override { return kName; }
    void reset() override;

private:
    std::vector<std::uint8_t> counters_;  // 0..3, >= 2 means "taken"
    std::size_t index(std::uint32_t pc) const;
};
