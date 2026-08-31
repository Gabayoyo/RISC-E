#include "risc-e/cpu/branch_stats.hpp"

#include "risc-e/decoder/decoder.hpp"

void record_control_transfer(BranchStats& stats, BranchPredictor* predictor,
                             const BranchContext& ctx, bool taken, uint32_t next_pc,
                             uint64_t inst_count) {
    ++stats.control_total;

    if (predictor != nullptr) {
        const Prediction pred = predictor->predict(ctx);
        const uint32_t predicted_pc = pred.next_pc.value_or(ctx.fallthrough_pc());
        predictor->resolve(ctx, Resolution{taken, next_pc});

        if (predicted_pc == next_pc) {
            ++stats.hits;
            if (ctx.is_conditional_branch()) {
                ++stats.cond_hits;
            } else if (ctx.is_jalr()) {
                ++stats.indirect_hits;
            }
        } else {
            ++stats.misses;
            if (ctx.is_conditional_branch()) {
                ++stats.cond_misses;
            } else if (ctx.is_jalr()) {
                ++stats.indirect_misses;
            }
        }
    }

    if (stats.trace_enabled && stats.trace.size() < BranchStats::kMaxTrace) {
        stats.trace.push_back(BranchRecord{inst_count, ctx.pc, ctx.raw, ctx.funct3, taken, next_pc});
    }
}

void BranchStats::reset() {
    total = taken = not_taken = 0;
    type_total.fill(0);
    type_taken.fill(0);
    control_total = 0;
    hits = misses = 0;
    cond_hits = cond_misses = 0;
    indirect_hits = indirect_misses = 0;
    trace.clear();
}

double BranchStats::hit_rate() const {
    const uint64_t predicted = hits + misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(hits) / static_cast<double>(predicted);
}

double BranchStats::conditional_hit_rate() const {
    const uint64_t predicted = cond_hits + cond_misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(cond_hits) / static_cast<double>(predicted);
}

double BranchStats::indirect_hit_rate() const {
    const uint64_t predicted = indirect_hits + indirect_misses;
    if (predicted == 0) return 0.0;
    return 100.0 * static_cast<double>(indirect_hits) / static_cast<double>(predicted);
}

BranchStats replay_trace(const std::vector<BranchRecord>& trace, BranchPredictor& predictor) {
    BranchStats stats;
    for (const BranchRecord& rec : trace) {
        const DecodedInstruction d = decode_raw_inst(rec.raw, rec.pc);
        const BranchContext ctx = BranchContext::from_decoded(d);
        record_control_transfer(stats, &predictor, ctx, rec.taken, rec.target);
    }
    return stats;
}
