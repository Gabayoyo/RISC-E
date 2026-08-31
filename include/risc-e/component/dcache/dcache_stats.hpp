#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// What kind of memory access a trace entry records. Load/Store are recorded
// today; Fetch exists so a future unified cache or shared L2 can consume the
// same trace without a format change.
enum class DCacheKind : uint8_t { Load, Store, Fetch };

// One recorded memory access (only kept when the trace is not capped).
struct DCacheRecord {
    uint32_t addr;    // accessed address
    uint8_t  size;    // bytes (1, 2 or 4)
    DCacheKind kind;  // load, store, or (future) fetch
};

// Cumulative memory-access statistics of one run: total counts plus the
// ordered access stream (the input to the data-cache simulations). Populated
// by the interpreter as a side effect of stepping; read back through
// RunContext like BranchStats. Recording is model-free: every cache design
// consumes the same trace.
struct DCacheStats {
    static constexpr std::size_t kMaxRecords = 1000000;  // recorded-access cap

    uint64_t loads = 0;
    uint64_t stores = 0;

    std::vector<DCacheRecord> records;  // capped at kMaxRecords

    void reset();
    void record(DCacheKind kind, uint32_t addr, uint8_t size);
};
