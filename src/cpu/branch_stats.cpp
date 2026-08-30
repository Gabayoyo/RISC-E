#include "risc-e/cpu/branch_stats.hpp"

void BranchStats::reset() {
    total = taken = not_taken = 0;
    type_total.fill(0);
    type_taken.fill(0);
    control_total = 0;
    hits = misses = 0;
    cond_hits = cond_misses = 0;
    indirect_hits = indirect_misses = 0;
    trace.clear();
}

double BranchStats::hit_rate() const {
    const uint64_t predicted = hits + misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(hits) / static_cast<double>(predicted);
}

double BranchStats::conditional_hit_rate() const {
    const uint64_t predicted = cond_hits + cond_misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(cond_hits) / static_cast<double>(predicted);
}

double BranchStats::indirect_hit_rate() const {
    const uint64_t predicted = indirect_hits + indirect_misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(indirect_hits) / static_cast<double>(predicted);
}
