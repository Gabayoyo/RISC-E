#pragma once

#include <cstdint>
#include <string>

// Simplified in-order pipeline model used to turn branch-misprediction counts
// into cycle costs for the CLI report. The model is deliberately small: a
// fetch-to-commit depth and a per-mispredict penalty.
struct PipelineModel {
    int stages = 5;              // pipeline depth (IF through WB)
    int mispredict_penalty = 0;  // explicit penalty; 0 means "derive from stages"

    // Cycles a mispredicted control transfer costs. Defaults to the classic
    // in-order assumption: the branch resolves in the EX stage (stage 3), so
    // the younger stages fetched behind it are flushed -> stages - 3.
    int penalty_cycles() const {
        if (mispredict_penalty > 0) return mispredict_penalty;
        const int derived = stages - 3;
        return derived > 0 ? derived : 0;
    }

    // Human-readable description for the report, e.g.
    // "5-stage pipeline (2-cycle mispredict penalty)".
    std::string description() const;
};

// Cycle accounting for one run under a pipeline model. ideal_cycles assumes a
// perfect predictor (1 instruction per cycle); penalty_cycles adds the cost of
// every misprediction.
struct PipelineStats {
    uint64_t instructions = 0;
    uint64_t ideal_cycles = 0;
    uint64_t penalty_cycles = 0;
    uint64_t total_cycles = 0;
    double cpi = 0.0;            // total_cycles / instructions (0 when no instructions)
    double slowdown_pct = 0.0;   // penalty / ideal * 100
};

PipelineStats compute_pipeline_stats(uint64_t inst_count, uint64_t misses,
                                     const PipelineModel& model);
