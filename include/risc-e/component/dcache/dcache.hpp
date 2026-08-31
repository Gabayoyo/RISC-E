#pragma once

#include "risc-e/component/dcache/dcache_stats.hpp"
#include "risc-e/component/dcache/replacement.hpp"

#include <cstdint>
#include <string_view>

// Write policy for the data cache.
enum class WritePolicy : uint8_t { WriteThrough, WriteBack };

// Allocation policy on a write miss.
enum class WriteAllocate : uint8_t { NoWriteAllocate, WriteAllocate };

// Configuration of one data-cache level. The shape mirrors ICacheConfig:
// fixed-size lines, `sets` sets of `ways` ways, a replacement policy, plus
// the write side (write-through vs write-back, allocate vs not on a write
// miss). Write-through stores drain through a bounded write buffer
// (`write_buffer_depth` stores per memory transaction). `level` names which
// cache level the config describes ("L1", "L2", ...); it is not a tunable. A
// fully associative cache is sets == 1; direct-mapped is ways == 1.
struct DCacheConfig {
    long hit_latency = 1;     // cycles for a hit (load or store)
    long miss_penalty = 100;  // cycles for a memory service (refill, drain or direct write)
    long line_size = 16;      // bytes
    long sets = 16;           // 1 => fully associative
    long ways = 4;            // lines per set (PLRU requires a power of two)
    long write_buffer_depth = 4;  // stores per memory transaction (write-through only; 1 = unbuffered)
    std::string_view level = "L1";  // which cache level this config describes
    ReplacementPolicy policy = ReplacementPolicy::LRU;
    WritePolicy write_policy = WritePolicy::WriteThrough;
    WriteAllocate write_allocate = WriteAllocate::NoWriteAllocate;

    uint64_t capacity_bytes() const {
        return static_cast<uint64_t>(sets) * static_cast<uint64_t>(ways) *
               static_cast<uint64_t>(line_size);
    }
};

// Result of one run through a cache level. Misses are classified exactly
// like the icache: a miss on a line that has never been resident is
// compulsory; a re-entry miss is a capacity miss (fully associative) or a
// conflict miss (set associative); a store miss under no-write-allocate is
// counted separately (the policy declined to allocate, so no line state
// changes). Stall accounting is first-order and transparent: every miss that
// refills or writes directly costs one miss_penalty, write-through stores
// drain through the write buffer (one memory transaction per buffer-depth
// stores), and every dirty write-back eviction costs one miss_penalty.
struct DCacheResult {
    uint64_t accesses = 0;
    uint64_t loads = 0;
    uint64_t stores = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t compulsory_misses = 0;
    uint64_t conflict_misses = 0;
    uint64_t capacity_misses = 0;
    uint64_t no_allocate_store_misses = 0;  // store misses that did not allocate
    uint64_t evictions = 0;
    uint64_t writebacks = 0;        // dirty lines written back on eviction
    uint64_t miss_stalls = 0;       // refills + direct writes x miss_penalty
    uint64_t write_stalls = 0;      // write-through buffer drains
    uint64_t write_buffer_drains = 0; // memory transactions paid by the write buffer
    uint64_t writeback_stalls = 0;  // dirty evictions x miss_penalty
    uint64_t total_cycles = 0;
    uint64_t baseline_cycles = 0;  // no data cache: every access hits memory
    int64_t saved_cycles = 0;
    double saved_pct = 0.0;
    double hit_rate = 0.0;
};

// Simulates the level over the recorded access sequence. Exact for the
// recorded prefix of the run: addresses and access order are real. When
// `miss_stream` is non-null, every access this level forwards down (a refill
// miss as a load, a dirty write-back eviction as a store) is appended to it,
// so a second level can be chained behind this one.
DCacheResult simulate_dcache(const DCacheStats& trace, const DCacheConfig& config,
                             DCacheStats* miss_stream = nullptr);
