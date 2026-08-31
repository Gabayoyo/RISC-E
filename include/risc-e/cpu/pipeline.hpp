#pragma once

#include "risc-e/harness/component.hpp"

#include <cstdint>
#include <string>
#include <string_view>

// Simplified in-order pipeline model that turns stall events into cycle cost
// for the CLI report. The model is deliberately small: a fetch-to-commit
// depth and a per-stall-event penalty. As a Component it plugs into --param /
// --list and renders the pipeline report section. The stall source is
// whatever the run reports (branch mispredictions today); the model itself
// never names one.
struct PipelineModel : public Component {
    static constexpr std::string_view kName = "pipeline";

    int stages = 5;          // pipeline depth (IF through WB)
    int stall_penalty = 0;   // explicit penalty; 0 means "derive from stages"

    // Cycles one stall event costs. Defaults to the classic in-order
    // assumption: a stalled control transfer resolves in the EX stage (stage
    // 3), so the younger stages fetched behind it are flushed -> stages - 3.
    int penalty_cycles() const {
        if (stall_penalty > 0) return stall_penalty;
        const int derived = stages - 3;
        return derived > 0 ? derived : 0;
    }

    std::string_view name() const override { return kName; }
    std::string_view type() const override { return "pipeline"; }

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

    std::string_view report_title() const override;
    void report(std::ostream& out, const RunContext& ctx) const override;

    // Cost answer: cycles under this model, vs a stall-free pipeline.
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override;

    // Human-readable description for the report, e.g.
    // "5-stage pipeline (2-cycle stall penalty)".
    std::string description() const;
};

// Cycle accounting for one run under a pipeline model. ideal_cycles assumes a
// stall-free pipeline (1 instruction per cycle); penalty_cycles adds the cost
// of every stall event.
struct PipelineStats {
    uint64_t instructions = 0;
    uint64_t ideal_cycles = 0;
    uint64_t penalty_cycles = 0;
    uint64_t total_cycles = 0;
    double cpi = 0.0;            // total_cycles / instructions (0 when no instructions)
    double slowdown_pct = 0.0;   // penalty / ideal * 100
};

PipelineStats compute_pipeline_stats(uint64_t inst_count, uint64_t stall_events,
                                     const PipelineModel& model);
