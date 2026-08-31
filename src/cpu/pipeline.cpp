#include "risc-e/cpu/pipeline.hpp"

#include <sstream>

std::string PipelineModel::description() const {
    std::ostringstream out;
    out << stages << "-stage pipeline (" << penalty_cycles()
        << "-cycle mispredict penalty)";
    return out.str();
}

PipelineStats compute_pipeline_stats(uint64_t inst_count, uint64_t misses,
                                     const PipelineModel& model) {
    PipelineStats s;
    s.instructions = inst_count;
    s.ideal_cycles = inst_count;
    s.penalty_cycles = misses * static_cast<uint64_t>(model.penalty_cycles());
    s.total_cycles = s.ideal_cycles + s.penalty_cycles;
    if (inst_count != 0) {
        s.cpi = static_cast<double>(s.total_cycles) / static_cast<double>(inst_count);
        s.slowdown_pct =
            100.0 * static_cast<double>(s.penalty_cycles) / static_cast<double>(inst_count);
    }
    return s;
}
