#pragma once

#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/pipeline.hpp"
#include "risc-e/report/report_section.hpp"

#include <cstdint>
#include <vector>

// Cycle-cost view of a run: what a perfect predictor would cost, what the
// chosen predictor actually costs, and how many cycles it saves versus a
// static always-not-taken baseline (computed from the recorded trace).
class PipelineSection : public ReportSection {
public:
    PipelineSection(uint64_t inst_count, const BranchStats& stats,
                    const std::vector<BranchRecord>& trace, const PipelineModel& model);

    std::string_view title() const override;
    void render(std::ostream& out) const override;

private:
    uint64_t inst_count_;
    const BranchStats* stats_;
    const std::vector<BranchRecord>* trace_;
    PipelineModel model_;
};
