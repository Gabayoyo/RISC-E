#include "risc-e/component/dcache/dcache.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

DCacheResult simulate_dcache(const DCacheStats& trace, const DCacheConfig& cfg,
                             DCacheStats* miss_stream) {
    DCacheResult t;
    if (cfg.sets < 1 || cfg.ways < 1 || cfg.line_size < 1) return t;

    const uint64_t line_bytes = static_cast<uint64_t>(cfg.line_size);
    const uint64_t sets = static_cast<uint64_t>(cfg.sets);
    const uint64_t ways = static_cast<uint64_t>(cfg.ways);
    const uint64_t hit_latency = static_cast<uint64_t>(cfg.hit_latency);
    const uint64_t penalty = static_cast<uint64_t>(cfg.miss_penalty);

    struct Way {
        uint64_t tag = 0;
        bool valid = false;
        bool dirty = false;
    };
    std::vector<Way> storage(sets * ways);
    std::vector<ReplacementState> replacement;
    replacement.reserve(sets);
    for (uint64_t s = 0; s < sets; ++s) {
        replacement.emplace_back(ways, cfg.policy);
    }
    // Lines that have ever been resident, for compulsory-miss classification.
    std::unordered_set<uint64_t> seen_lines;

    const auto index_of = [&](uint64_t line) -> uint64_t { return line % sets; };
    const auto tag_of = [&](uint64_t line) -> uint64_t { return line / sets; };

    const auto find_way = [&](uint64_t set, uint64_t tag) -> int64_t {
        for (uint64_t w = 0; w < ways; ++w) {
            const Way& way = storage[set * ways + w];
            if (way.valid && way.tag == tag) return static_cast<int64_t>(w);
        }
        return -1;
    };

    // Refills `line` in `set`, evicting a victim when the set is full. The
    // victim's dirty line, if any, is written back down (recorded on the miss
    // stream so a lower level sees it).
    const auto refill = [&](uint64_t line, uint64_t set) {
        const uint64_t tag = tag_of(line);
        if (find_way(set, tag) >= 0) return;  // already resident

        uint64_t slot = ways;  // first free way, if any
        for (uint64_t w = 0; w < ways; ++w) {
            if (!storage[set * ways + w].valid) {
                slot = w;
                break;
            }
        }
        if (slot == ways) {
            slot = static_cast<uint64_t>(replacement[set].victim());
            ++t.evictions;
            if (storage[set * ways + slot].dirty) {
                ++t.writebacks;
                t.writeback_stalls += penalty;
                if (miss_stream != nullptr) {
                    const uint64_t victim_line =
                        storage[set * ways + slot].tag * sets + set;
                    miss_stream->record(DCacheKind::Store,
                                        static_cast<uint32_t>(victim_line * line_bytes), 4);
                }
            }
        }
        storage[set * ways + slot] = Way{tag, true, false};
        replacement[set].touch(slot);
    };

    // Write-through stores drain through the write buffer: one memory
    // transaction per buffer-depth stores (leftovers drain in idle cycles the
    // model does not count, so only full-buffer drains stall).
    uint64_t pending_writes = 0;
    const uint64_t buffer_depth = static_cast<uint64_t>(cfg.write_buffer_depth);
    if (buffer_depth < 1) return t;
    const auto buffer_store = [&] {
        if (++pending_writes >= buffer_depth) {
            t.write_stalls += penalty;
            ++t.write_buffer_drains;
            pending_writes = 0;
        }
    };

    for (const DCacheRecord& rec : trace.records) {
        ++t.accesses;
        if (rec.kind == DCacheKind::Load) {
            ++t.loads;
        } else {
            ++t.stores;
        }
        const uint64_t line = static_cast<uint64_t>(rec.addr) / line_bytes;
        const uint64_t set = index_of(line);
        const uint64_t tag = tag_of(line);
        const bool is_store = rec.kind == DCacheKind::Store;
        const int64_t hit = find_way(set, tag);

        if (hit >= 0) {
            ++t.hits;
            replacement[set].touch(static_cast<std::size_t>(hit));
            if (is_store) {
                if (cfg.write_policy == WritePolicy::WriteBack) {
                    storage[set * ways + static_cast<std::size_t>(hit)].dirty = true;
                } else {
                    buffer_store();
                }
            }
            continue;
        }

        ++t.misses;

        // A no-allocate store miss is counted separately: the policy declined
        // to allocate, so no line state changes, and the store goes straight
        // down. Under write-through it is still absorbed by the write buffer;
        // under write-back it is a direct write to the next level.
        if (is_store && cfg.write_allocate == WriteAllocate::NoWriteAllocate) {
            ++t.no_allocate_store_misses;
            if (cfg.write_policy == WritePolicy::WriteThrough) {
                buffer_store();
            } else {
                t.miss_stalls += penalty;
            }
            if (miss_stream != nullptr) {
                miss_stream->record(DCacheKind::Store, rec.addr, rec.size);
            }
            continue;
        }

        // Refill (write-allocate store or any load): the line is fetched from
        // the next level, so the access is forwarded down.
        t.miss_stalls += penalty;
        refill(line, set);
        if (miss_stream != nullptr) {
            miss_stream->record(DCacheKind::Load, rec.addr, rec.size);
        }

        if (!seen_lines.count(line)) {
            seen_lines.insert(line);
            ++t.compulsory_misses;
        } else if (sets > 1) {
            ++t.conflict_misses;
        } else {
            ++t.capacity_misses;
        }

        if (is_store) {
            const int64_t w = find_way(set, tag);
            if (cfg.write_policy == WritePolicy::WriteBack) {
                if (w >= 0) storage[set * ways + static_cast<std::size_t>(w)].dirty = true;
            } else {
                buffer_store();  // write-through write on top of the refill
            }
        }
    }

    t.total_cycles = t.hits * hit_latency + t.miss_stalls + t.write_stalls +
                     t.writeback_stalls;
    t.baseline_cycles = t.accesses * penalty;
    t.saved_cycles =
        static_cast<int64_t>(t.baseline_cycles) - static_cast<int64_t>(t.total_cycles);
    if (t.baseline_cycles != 0) {
        t.saved_pct = 100.0 * static_cast<double>(t.saved_cycles) /
                      static_cast<double>(t.baseline_cycles);
    }
    t.hit_rate = t.accesses == 0 ? 0.0
                                 : 100.0 * static_cast<double>(t.hits) /
                                       static_cast<double>(t.accesses);
    return t;
}
