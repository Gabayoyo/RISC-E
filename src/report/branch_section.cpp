#include "risc-e/report/branch_section.hpp"

#include <ostream>

BranchSection::BranchSection(const BranchPredictor* predictor, const BranchStats& stats)
    : predictor_(predictor), stats_(&stats) {}

std::string_view BranchSection::title() const {
    return "branch prediction";
}

void BranchSection::render(std::ostream& out) const {
    const BranchStats& s = *stats_;
    if (predictor_ != nullptr) {
        out << "  predictor: " << predictor_->name() << '\n';
    }
    if (s.control_total == 0) {
        out << "  hit rate: n/a\n"
            << "  miss rate: n/a\n";
    } else {
        const double hit_rate = s.hit_rate();
        out << "  hit rate: " << hit_rate << "%\n"
            << "  miss rate: " << (100.0 - hit_rate) << "%\n";
    }
    out << "  hits: " << s.hits << '\n'
        << "  misses: " << s.misses << '\n'
        << "  branches: " << s.control_total << '\n';
}
