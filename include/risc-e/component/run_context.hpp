#pragma once

#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/dcache/dcache_stats.hpp"

#include <cstdint>

struct ICacheStats;

// Everything a component may need from a run to render its report or compute
// its comparison metrics. Fields grow as new component types record their own
// traces (a memory-access trace for the data-cache models).
struct RunContext {
    uint64_t instruction_count = 0;
    const BranchStats* branch_stats = nullptr;   // live-run stats (incl. trace)
    const PipelineModel* pipeline = nullptr;     // active pipeline model
    const ICacheStats* profile_stats = nullptr; // dynamic execution profile
    const DCacheStats* access_trace = nullptr;   // load/store access stream
};
