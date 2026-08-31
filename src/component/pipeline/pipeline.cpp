#include "risc-e/component/pipeline/pipeline.hpp"

#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/run_context.hpp"

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
        << "-cycle stall penalty)";
    return out.str();
}

PipelineStats compute_pipeline_stats(uint64_t inst_count, uint64_t stall_events,
                                     const PipelineModel& model) {
    PipelineStats s;
    s.instructions = inst_count;
    s.ideal_cycles = inst_count;
    s.penalty_cycles = stall_events * static_cast<uint64_t>(model.penalty_cycles());
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
        {"stages", "pipeline depth (penalty = depth - 3 per stall event)", 1, 0,
         std::to_string(stages)},
        {"stall-penalty", "per-stall-event penalty in cycles (0 derives from depth)", 0, 0,
         std::to_string(stall_penalty)},
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
    if (name == "stall-penalty") {
        stall_penalty = static_cast<int>(*parsed);
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

    // arch: the stall source is branch mispredictions today (stall_events ==
    // misses). Memory or other stalls sum in here later; the model and this
    // report never reference the source by name.
    const uint64_t stall_events = s.misses;
    const PipelineStats ps = compute_pipeline_stats(ctx.instruction_count, stall_events, *this);
    const int penalty = penalty_cycles();

    out << "  model: " << description() << '\n'
        << "  instructions: " << ps.instructions << '\n'
        << "  ideal cycles: " << ps.ideal_cycles << '\n'
        << "  stall cycles: " << ps.penalty_cycles << " (" << stall_events << " stall event"
        << (stall_events == 1 ? "" : "s") << " x " << penalty << " cycles)\n"
        << "  total cycles: " << ps.total_cycles << '\n';

    if (ctx.instruction_count != 0) {
        out << "  CPI: " << fixed(ps.cpi, 3) << '\n'
            << "  slowdown: " << fixed(ps.slowdown_pct, 2, true) << "% vs perfect\n";
    }

    // How many cycles the run saves versus a worst-case pipeline that pays
    // the penalty on every control transfer. Neutral baseline: no predictor
    // or policy is named, so the number is a property of the run.
    if (!s.trace.empty() && penalty > 0) {
        const uint64_t events = s.trace.size();
        const int64_t saved = static_cast<int64_t>(events - stall_events) * penalty;
        out << "  cycles saved: " << saved << " vs worst-case (" << events
            << " stall events)\n";
    }
}

std::optional<CycleCost> PipelineModel::cycle_cost(const RunContext& ctx) {
    if (ctx.branch_stats == nullptr) return std::nullopt;
    const uint64_t total =
        compute_pipeline_stats(ctx.instruction_count, ctx.branch_stats->misses, *this)
            .total_cycles;
    return CycleCost{total, ctx.instruction_count, "stall-free"};
}
