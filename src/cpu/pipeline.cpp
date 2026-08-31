#include "risc-e/cpu/pipeline.hpp"

#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/predictors/always_not_taken.hpp"
#include "risc-e/harness/run_context.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>

namespace {

// Fixed-width decimal formatting for the numeric rows.
std::string fixed(double value, int precision, bool signed_value = false) {
    std::ostringstream out;
    if (signed_value) out << std::showpos;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

} // namespace

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

std::vector<ParamSpec> PipelineModel::parameters() const {
    return {
        {"stages", "pipeline depth (penalty = depth - 3 per miss)", 1, 0,
         std::to_string(stages)},
        {"mispredict-penalty", "per-miss penalty in cycles (0 derives from depth)", 0, 0,
         std::to_string(mispredict_penalty)},
    };
}

bool PipelineModel::set_parameter(std::string_view name, std::string_view value,
                                  std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (name == "stages") {
        if (*parsed < 1) {
            error = "stages must be >= 1";
            return false;
        }
        stages = static_cast<int>(*parsed);
        return true;
    }
    if (name == "mispredict-penalty") {
        mispredict_penalty = static_cast<int>(*parsed);
        return true;
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}

std::string_view PipelineModel::report_title() const {
    return "pipeline";
}

void PipelineModel::report(std::ostream& out, const RunContext& ctx) const {
    if (ctx.branch_stats == nullptr) return;
    const BranchStats& s = *ctx.branch_stats;
    const uint64_t misses = s.misses;
    const PipelineStats ps = compute_pipeline_stats(ctx.instruction_count, misses, *this);
    const int penalty = penalty_cycles();

    out << "  model: " << description() << '\n'
        << "  instructions: " << ps.instructions << '\n'
        << "  ideal cycles: " << ps.ideal_cycles << '\n'
        << "  penalty cycles: " << ps.penalty_cycles << " (" << misses << " miss"
        << (misses == 1 ? "" : "es") << " x " << penalty << " cycles)\n"
        << "  total cycles: " << ps.total_cycles << '\n';

    if (ctx.instruction_count != 0) {
        out << "  CPI: " << fixed(ps.cpi, 3) << '\n'
            << "  slowdown: " << fixed(ps.slowdown_pct, 2, true) << "% compared to perfect\n";
    }

    // How many cycles the chosen predictor saves versus doing nothing (always
    // not-taken). Only meaningful when a trace and a non-zero penalty exist.
    if (!s.trace.empty() && penalty > 0) {
        AlwaysNotTakenPredictor baseline;
        const BranchStats base = replay_trace(s.trace, baseline);
        const int64_t saved = static_cast<int64_t>(base.misses) * penalty -
                              static_cast<int64_t>(misses) * penalty;
        out << "  cycles saved: " << saved << " vs always-not-taken\n";
    }
}
