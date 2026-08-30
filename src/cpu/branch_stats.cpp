#include "risc-e/cpu/branch_stats.hpp"

#include <algorithm>

void BranchStats::reset() {
    total = taken = not_taken = hits = misses = 0;
    type_total.fill(0);
    type_taken.fill(0);
    trace.clear();
}

double BranchStats::hit_rate() const {
    const uint64_t predicted = hits + misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(hits) / static_cast<double>(predicted);
}

TwoBitSaturatingPredictor::TwoBitSaturatingPredictor(std::size_t table_size)
    : counters_(table_size, 2) {}

std::size_t TwoBitSaturatingPredictor::index(uint32_t pc) const {
    if (counters_.empty()) return 0;
    // Table size must be a power of two so the hash can be masked (no division).
    return (pc ^ (pc >> 4) ^ (pc >> 8)) & (counters_.size() - 1);
}

bool TwoBitSaturatingPredictor::predict(uint32_t pc) const {
    return counters_[index(pc)] >= 2;
}

void TwoBitSaturatingPredictor::update(uint32_t pc, bool taken) {
    std::uint8_t& counter = counters_[index(pc)];
    if (taken) {
        counter = static_cast<std::uint8_t>(std::min<int>(3, counter + 1));
    } else {
        counter = static_cast<std::uint8_t>(std::max<int>(0, counter - 1));
    }
}
