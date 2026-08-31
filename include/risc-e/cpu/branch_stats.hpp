#pragma once

#include "risc-e/cpu/branch_predictor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// One recorded branch execution (only kept when tracing is enabled).
struct BranchRecord {
    uint64_t inst_count;  // instruction count at the time the branch executed
    uint32_t pc;          // address of the branch instruction
    uint32_t raw;         // raw instruction word
    uint8_t  funct3;      // branch type (BEQ=0, BNE=1, BLT=4, BGE=5, BLTU=6, BGEU=7)
    bool     taken;       // actual outcome
    uint32_t target;      // pc after the branch (taken or fall-through)
};

// Cumulative control-flow statistics. Counts are always updated; the trace is
// only recorded when trace_enabled is true (and is capped at kMaxTrace records).
struct BranchStats {
    static constexpr std::size_t kMaxTrace = 100000;

    // Conditional branches only.
    uint64_t total     = 0;
    uint64_t taken     = 0;
    uint64_t not_taken = 0;

    // Per-funct3 (branch type) counters, indexed 0..7.
    std::array<uint64_t, 8> type_total{};
    std::array<uint64_t, 8> type_taken{};

    // Every control transfer: conditional branches + JAL + JALR.
    uint64_t control_total = 0;

    // Target-aware predictor comparison over all control transfers (only
    // meaningful when a predictor is attached). A transfer is a hit when the
    // predicted next PC equals the actual next PC.
    uint64_t hits   = 0;
    uint64_t misses = 0;

    // Breakdown of the predictor comparison.
    uint64_t cond_hits      = 0;
    uint64_t cond_misses    = 0;
    uint64_t indirect_hits  = 0;  // JALR
    uint64_t indirect_misses = 0; // JALR

    bool trace_enabled = false;
    std::vector<BranchRecord> trace;

    void reset();
    double hit_rate() const;
    double conditional_hit_rate() const;
    double indirect_hit_rate() const;
};

// Applies one control transfer to the predictor and the cumulative stats.
// predictor may be null (stats only). A trace entry is recorded when tracing
// is enabled. This is the single source of truth for hit/miss accounting, so
// replaying a recorded trace through any predictor yields the same numbers as
// a live run.
void record_control_transfer(BranchStats& stats, BranchPredictor* predictor,
                             const BranchContext& ctx, bool taken, uint32_t next_pc,
                             uint64_t inst_count = 0);

// Replays a recorded control-flow trace through a predictor and returns the
// cumulative stats, yielding the same hit/miss numbers as a live run. Shared
// by --comparison and the pipeline section's baseline comparison.
BranchStats replay_trace(const std::vector<BranchRecord>& trace, BranchPredictor& predictor);
