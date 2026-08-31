#pragma once

#include "risc-e/harness/component.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// One identified basic block: a maximal run of straight-line code starting at
// a control-transfer target (or the program entry). Blocks are interned at
// first sight: the entry PC maps to a sequential id that later consumers (IR,
// layout) can key on without carrying addresses around. The execution and
// instruction counts feed the simulated instruction-cache model.
struct BlockInfo {
    uint32_t entry_pc = 0;
    uint64_t executions = 0;     // dynamic: times control entered the block
    uint64_t instructions = 0;   // dynamic: instructions executed inside it
};

// Cumulative execution profile of one run: the static distinct-instruction
// footprint, interned basic-block counts, and the ordered sequence of block
// entries (the input to the simulated instruction-cache). Populated by the
// interpreter as a side effect of stepping; read back through RunContext like
// BranchStats.
struct ProfileStats {
    static constexpr uint32_t kNoBlock = 0xFFFFFFFFu;    // no block active yet
    static constexpr std::size_t kMaxEntries = 100000;   // recorded-entry cap

    uint64_t instructions = 0;                        // decoded + executed
    std::unordered_set<uint32_t> seen_pcs;            // static instruction footprint
    std::unordered_map<uint32_t, uint32_t> pc_to_id;  // entry PC -> interned id
    std::vector<BlockInfo> blocks;                    // id == index
    std::vector<uint32_t> entry_sequence;             // block ids in entry order (capped)

    void reset();

    // Returns the interned id for an entry PC, creating the block on first
    // sight. Then records one entry execution of that block and appends the
    // id to the entry sequence.
    uint32_t record_block_entry(uint32_t entry_pc);
    // Records one executed instruction: total count, static footprint.
    void record_instruction(uint32_t pc);
    // Records one more instruction executed inside an already-active block.
    void record_block_instruction(uint32_t block_id);
};

// Cost model for the simulated instruction-cache view of a run. A block is
// fetched into the cache on its first entry (compulsory miss) and hits while
// it stays resident; a miss stalls the fetch stage for miss_penalty cycles.
// When the cache is full, the least-recently-used block is evicted, so a
// block that is re-entered after eviction misses again. Blocks are the cache
// lines (each line holds one block's instructions); cache_capacity is the
// cache size in instructions, 0 = unlimited.
struct CacheModel {
    long miss_penalty = 50;    // stall cycles per cache miss
    long cache_capacity = 64;  // cache size in instructions; 0 = unlimited

    CacheModel() = default;
    CacheModel(long penalty, long capacity) : miss_penalty(penalty), cache_capacity(capacity) {}
};

// Result of the instruction-cache simulation over one run's recorded entry
// sequence.
struct CacheTime {
    uint64_t entries = 0;             // block entries (fetches)
    uint64_t hits = 0;                // resident at fetch time
    uint64_t misses = 0;              // not resident at fetch time
    uint64_t compulsory_misses = 0;   // first touch of a block
    uint64_t evictions = 0;           // capacity evictions
    uint64_t miss_stalls = 0;         // misses x penalty
    uint64_t total_cycles = 0;        // instructions + miss stalls
    uint64_t baseline_cycles = 0;     // no cache: every entry misses
    int64_t saved_cycles = 0;         // baseline - total (negative: cache loses)
    double saved_pct = 0.0;
    double hit_rate = 0.0;
};

// Simulates the instruction cache over the recorded entry sequence with LRU
// replacement. Exact for the recorded prefix of the run (blocks always
// execute the same instructions, so per-entry sizes are constant).
CacheTime simulate_icache(const ProfileStats& stats, const CacheModel& model);

// The profile report section, a Component so it gets --list, --param and
// --comparison through the harness. Reads ProfileStats recorded during the
// run; like the pipeline model it is a view over the run, not a swappable
// behaviour, so a single implementation is registered.
class ProfileComponent : public Component {
public:
    static constexpr std::string_view kName = "icache";

    std::string_view name() const override { return kName; }
    std::string_view type() const override { return "profile"; }

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

    std::string_view report_title() const override;
    void report(std::ostream& out, const RunContext& ctx) const override;

    // Cost answer: cycles under the instruction-cache model, vs no cache.
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override;

    CacheModel model;  // instruction-cache tunables, applied at report time
};
