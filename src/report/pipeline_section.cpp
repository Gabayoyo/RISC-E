#include "risc-e/report/pipeline_section.hpp"

#include "risc-e/cpu/predictors/always_not_taken.hpp"

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

PipelineSection::PipelineSection(uint64_t inst_count, const BranchStats& stats,
                                 const std::vector<BranchRecord>& trace,
                                 const PipelineModel& model)
    : inst_count_(inst_count), stats_(&stats), trace_(&trace), model_(model) {}

std::string_view PipelineSection::title() const {
    return "pipeline";
}

void PipelineSection::render(std::ostream& out) const {
    const BranchStats& s = *stats_;
    const uint64_t misses = s.misses;
    const PipelineStats ps = compute_pipeline_stats(inst_count_, misses, model_);
    const int penalty = model_.penalty_cycles();

    out << "  model: " << model_.description() << '\n'
        << "  instructions: " << ps.instructions << '\n'
        << "  ideal cycles: " << ps.ideal_cycles << '\n'
        << "  penalty cycles: " << ps.penalty_cycles << " (" << misses << " miss"
        << (misses == 1 ? "" : "es") << " x " << penalty << " cycles)\n"
        << "  total cycles: " << ps.total_cycles << '\n';

    if (inst_count_ != 0) {
        out << "  CPI: " << fixed(ps.cpi, 3) << '\n'
            << "  slowdown: " << fixed(ps.slowdown_pct, 2, true) << "% compared to perfect\n";
    }

    // How many cycles the chosen predictor saves versus doing nothing (always
    // not-taken). Only meaningful when a trace and a non-zero penalty exist.
    if (!trace_->empty() && penalty > 0) {
        AlwaysNotTakenPredictor baseline;
        const BranchStats base = replay_trace(*trace_, baseline);
        const int64_t saved = static_cast<int64_t>(base.misses) * penalty -
                              static_cast<int64_t>(misses) * penalty;
        out << "  cycles saved: " << saved << " vs always-not-taken\n";
    }
}
