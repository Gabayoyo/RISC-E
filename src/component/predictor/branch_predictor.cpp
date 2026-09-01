#include "risc-e/component/predictor/branch_predictor.hpp"

#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/predictor/implementations/always_not_taken.hpp"
#include "risc-e/component/run_context.hpp"

#include <array>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The six conditional branch types, indexed by funct3 (0..7). Only these are
// valid for RV32I; the arrays in BranchStats reserve 8 slots.
constexpr std::array<std::pair<uint8_t, const char*>, 6> kBranchTypes = {
    {{0, "beq"}, {1, "bne"}, {4, "blt"}, {5, "bge"}, {6, "bltu"}, {7, "bgeu"}}};

std::string fixed(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// Cycle accounting shared by the printed report and the JSON report: the
// pipeline's cost with this predictor's misses vs with the always-not-taken
// baseline's misses. Empty when there is no recorded trace or pipeline.
struct BranchCycles {
    bool has_value = false;
    uint64_t total = 0;
    uint64_t baseline = 0;
    int64_t saved = 0;
    double saved_pct = 0.0;
    double speedup = 0.0;
};

BranchCycles compute_branch_cycles(const BranchStats& s, const RunContext& ctx) {
    BranchCycles c;
    if (ctx.pipeline == nullptr || s.trace.empty()) return c;

    const uint64_t total =
        compute_pipeline_stats(ctx.instruction_count, s.misses, *ctx.pipeline).total_cycles;
    AlwaysNotTakenPredictor baseline;
    const BranchStats base = replay_trace(s.trace, baseline);
    const uint64_t baseline_cycles =
        compute_pipeline_stats(ctx.instruction_count, base.misses, *ctx.pipeline).total_cycles;

    c.total = total;
    c.baseline = baseline_cycles;
    c.saved = static_cast<int64_t>(baseline_cycles) - static_cast<int64_t>(total);
    if (baseline_cycles != 0) {
        c.saved_pct = 100.0 * static_cast<double>(c.saved) / static_cast<double>(baseline_cycles);
        c.speedup = static_cast<double>(baseline_cycles) / static_cast<double>(total);
    }
    c.has_value = true;
    return c;
}

} // namespace

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

    // Cycles saved vs the no-prediction baseline, in the same three numbers
    // as the comparison table.
    const BranchCycles c = compute_branch_cycles(s, ctx);
    if (c.has_value) {
        out << "  cycles saved: " << c.saved << " (" << fixed(c.saved_pct, 2)
            << "%) vs no prediction \u2014 " << fixed(c.speedup, 2) << "x\n";
    }

    if (ctx.verbose) {
        out << "  by type:\n";
        for (const auto& [funct3, name] : kBranchTypes) {
            out << "    " << name << ": " << s.type_total[funct3] << " total, "
                << s.type_taken[funct3] << " taken\n";
        }
        out << "  control transfers: " << s.control_total << '\n';
        const uint64_t cond = s.cond_hits + s.cond_misses;
        const uint64_t indirect = s.indirect_hits + s.indirect_misses;
        if (cond + indirect != 0) {
            out << "  predictor: " << s.cond_hits << "/" << cond << " conditional, "
                << s.indirect_hits << "/" << indirect << " indirect\n";
        }
    }
}

void BranchPredictor::write_json(std::ostream& out, const RunContext& ctx) const {
    if (ctx.branch_stats == nullptr) {
        out << "{}";
        return;
    }
    const BranchStats& s = *ctx.branch_stats;
    const BranchCycles c = compute_branch_cycles(s, ctx);
    out << "{\"predictor\":\"" << json_escape(name()) << "\","
        << "\"hits\":" << s.hits << ",\"misses\":" << s.misses
        << ",\"branches\":" << s.control_total
        << ",\"hit_rate\":" << fixed(s.hit_rate(), 6)
        << ",\"miss_rate\":" << fixed(100.0 - s.hit_rate(), 6);
    if (c.has_value) {
        out << ",\"cycles_saved\":" << c.saved << ",\"saved_pct\":" << fixed(c.saved_pct, 6)
            << ",\"speedup\":" << fixed(c.speedup, 6);
    }
    if (ctx.verbose) {
        out << ",\"by_type\":[";
        for (std::size_t i = 0; i < kBranchTypes.size(); ++i) {
            const auto [funct3, name] = kBranchTypes[i];
            out << "{\"funct3\":" << static_cast<int>(funct3) << ",\"name\":\"" << name
                << "\",\"total\":" << s.type_total[funct3]
                << ",\"taken\":" << s.type_taken[funct3] << "}";
            if (i + 1 < kBranchTypes.size()) out << ",";
        }
        out << "],\"control_transfers\":" << s.control_total
            << ",\"cond_hits\":" << s.cond_hits << ",\"cond_misses\":" << s.cond_misses
            << ",\"indirect_hits\":" << s.indirect_hits
            << ",\"indirect_misses\":" << s.indirect_misses;
    }
    out << "}";
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
