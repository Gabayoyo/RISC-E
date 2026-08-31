#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Replacement policy for the ways of a cache set. Named ReplacementPolicy to
// avoid colliding with the icache's Replacement enum; this module serves every
// memory-component design (and later levels of a hierarchy).
enum class ReplacementPolicy : uint8_t { LRU, PLRU, NMRU };

// Tracks the recency of the `ways` entries in one cache set and answers the
// next eviction victim. Shared by every cache model: the caller keeps the
// tag/dirty storage and calls touch() on every access and refill, and
// victim() when a full set must evict (never before the set is full).
// LRU is exact; PLRU is the tree-based approximation real cores ship; NMRU
// evicts any line that is not the most recently used one.
class ReplacementState {
public:
    // ways: entries per set. PLRU requires a power of two.
    ReplacementState(std::size_t ways, ReplacementPolicy policy);

    // Marks `way` as most recently used (call on hit and on refill).
    void touch(std::size_t way);

    // Index of the way to evict. Only valid when every way is live.
    std::size_t victim() const;

private:
    ReplacementPolicy policy_;
    std::size_t ways_;
    std::vector<uint64_t> lru_;    // LRU/NMRU: last-touch tick per way
    std::vector<uint8_t> plru_;    // PLRU: tree bits, indices 1..ways-1
    uint64_t tick_ = 0;
};
