#include "risc-e/component/dcache/dcache_stats.hpp"

void DCacheStats::reset() {
    loads = 0;
    stores = 0;
    records.clear();
}

void DCacheStats::record(DCacheKind kind, uint32_t addr, uint8_t size) {
    if (kind == DCacheKind::Load) {
        ++loads;
    } else {
        ++stores;  // stores (and a future Fetch stream) count on their own side
    }
    if (records.size() < kMaxRecords) {
        records.push_back(DCacheRecord{addr, size, kind});
    }
}
