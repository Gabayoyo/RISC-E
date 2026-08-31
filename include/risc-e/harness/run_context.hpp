#pragma once

#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/pipeline.hpp"

#include <cstdint>

// Everything a component may need from a run to render its report or compute
// its comparison metrics. Fields grow as new component types record their own
// traces (e.g. a memory-access trace for a cache model).
struct RunContext {
    uint64_t instruction_count = 0;
    const BranchStats* branch_stats = nullptr;   // live-run stats (incl. trace)
    const PipelineModel* pipeline = nullptr;     // active pipeline model
};
