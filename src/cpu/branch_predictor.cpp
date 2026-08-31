#include "risc-e/cpu/branch_predictor.hpp"

#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/pipeline.hpp"
#include "risc-e/harness/run_context.hpp"

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

std::vector<Metric> BranchPredictor::metrics(const RunContext& ctx) {
    std::vector<Metric> out;
    if (ctx.branch_stats == nullptr || ctx.branch_stats->trace.empty()) return out;

    const BranchStats replay = replay_trace(ctx.branch_stats->trace, *this);
    const uint64_t predicted = replay.hits + replay.misses;
    out.push_back(Metric{"hits", replay.hits, replay.control_total, ""});
    if (predicted != 0) {
        out.push_back(Metric{
            "hit rate",
            100.0 * static_cast<double>(replay.hits) / static_cast<double>(predicted),
            std::nullopt, "%"});
    }
    if (ctx.pipeline != nullptr) {
        const PipelineStats ps =
            compute_pipeline_stats(ctx.instruction_count, replay.misses, *ctx.pipeline);
        out.push_back(Metric{"cycles", static_cast<uint64_t>(ps.total_cycles), std::nullopt, ""});
    }
    return out;
}
