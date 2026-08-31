#pragma once

#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/report/report_section.hpp"

// Predictor summary: name first, then rates, then counts and the number of
// branches scored. Per-opcode and conditional/indirect breakdowns are not
// shown here.
class BranchSection : public ReportSection {
public:
    BranchSection(const BranchPredictor* predictor, const BranchStats& stats);

    std::string_view title() const override;
    void render(std::ostream& out) const override;

private:
    const BranchPredictor* predictor_;
    const BranchStats* stats_;
};
