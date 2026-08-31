#include "risc-e/component/predictor/branch_predictor.hpp"

#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/predictor/implementations/always_not_taken.hpp"
#include "risc-e/component/run_context.hpp"

#include <ostream>
#include <string_view>
#include <vector>

BranchContext BranchContext::from_decoded(const DecodedInstruction& d) {
    BranchContext ctx;
    ctx.pc     = d.addr;
    ctx.raw    = d.raw;
    ctx.opcode = d.opcode;
    ctx.funct3 = d.funct3;
    ctx.rd     = d.rd;
    ctx.rs1    = d.rs1;
    ctx.rs2    = d.rs2;
    ctx.imm    = d.imm;
    return ctx;
}

std::string_view BranchPredictor::report_title() const {
    return "branch prediction";
}

void BranchPredictor::report(std::ostream& out, const RunContext& ctx) const {
    if (ctx.branch_stats == nullptr) return;
    const BranchStats& s = *ctx.branch_stats;
    out << "  predictor: " << name() << '\n';
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

std::optional<CycleCost> BranchPredictor::cycle_cost(const RunContext& ctx) {
    if (ctx.branch_stats == nullptr || ctx.branch_stats->trace.empty() ||
        ctx.pipeline == nullptr) {
        return std::nullopt;
    }

    // Total cycles under the active pipeline for this predictor vs the
    // no-prediction baseline (always not-taken).
    const BranchStats replay = replay_trace(ctx.branch_stats->trace, *this);
    AlwaysNotTakenPredictor baseline;
    const BranchStats base = replay_trace(ctx.branch_stats->trace, baseline);
    const uint64_t total =
        compute_pipeline_stats(ctx.instruction_count, replay.misses, *ctx.pipeline).total_cycles;
    const uint64_t baseline_cycles =
        compute_pipeline_stats(ctx.instruction_count, base.misses, *ctx.pipeline).total_cycles;
    return CycleCost{total, baseline_cycles, "no prediction"};
}
