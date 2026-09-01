#pragma once

#include "risc-e/component/icache/icache_stats.hpp"
#include "risc-e/component/component.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Replacement policy for the ways of a set.
enum class Replacement { LRU, PLRU };

// Configuration of one instruction-cache design. All built-in designs share
// the shape: fixed-size lines, `sets` sets of `ways` ways, a replacement
// policy, and an optional next-line prefetch. The fetch unit is the basic
// block (one entry = one fetch): a fetch hits iff every line it spans is
// resident, otherwise it misses and refills the missing lines. A fully
// associative cache is simply sets == 1.
struct ICacheConfig {
    long miss_penalty = 50;  // stall cycles per miss
    long line_size = 16;     // bytes (4 RV32I instructions)
    long sets = 16;          // 1 => fully associative
    long ways = 4;           // lines per set (PLRU requires a power of two)
    Replacement policy = Replacement::LRU;
    bool prefetch = false;   // also fetch the next line on a miss

    uint64_t capacity_bytes() const {
        return static_cast<uint64_t>(sets) * static_cast<uint64_t>(ways) *
               static_cast<uint64_t>(line_size);
    }
};

// Result of one run through a cache design. Misses are counted per fetch
// (block entry); the first entry of a block is a compulsory miss, a re-entry
// miss is a capacity miss (fully associative) or a conflict miss (set
// associative).
struct ICacheResult {
    uint64_t entries = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t compulsory_misses = 0;
    uint64_t conflict_misses = 0;
    uint64_t capacity_misses = 0;
    uint64_t evictions = 0;
    uint64_t prefetches = 0;
    uint64_t miss_stalls = 0;
    uint64_t total_cycles = 0;
    uint64_t baseline_cycles = 0;  // no instruction cache: every entry misses
    int64_t saved_cycles = 0;
    double saved_pct = 0.0;
    double hit_rate = 0.0;
};

// Simulates the design over the recorded entry sequence. Exact for the
// recorded prefix of the run: a block always executes the same instructions,
// so its line span is constant across entries.
ICacheResult simulate_icache(const ICacheStats& stats, const ICacheConfig& config);

// Base for every instruction-cache component: shares the report section and
// the comparison cost answer (cycles vs "no instruction cache"). Concrete
// designs only declare their name, defaults, and tunables, so adding a new
// policy is one small class.
class ICacheComponent : public Component {
public:
    std::string_view type() const override { return "icache"; }
    std::string_view report_title() const override { return "icache"; }

    void report(std::ostream& out, const RunContext& ctx) const override;
    void write_json(std::ostream& out, const RunContext& ctx) const override;
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override;

protected:
    // Shared tunables (miss-penalty, line-size) used by every design.
    void append_shared_parameters(std::vector<ParamSpec>& out) const;
    // Applies a shared tunable; false when the name is not a shared one.
    bool set_shared_parameter(std::string_view name, long value, std::string& error);

    // Sets/ways geometry tunables used by the set-associative designs
    // (`ways_help` is the --list description for the ways tunable).
    void append_geometry_parameters(std::vector<ParamSpec>& out,
                                    std::string_view ways_help) const;
    // Applies a sets/ways override; false when the name is not a geometry
    // tunable. PLRU designs pass `power_of_two_ways` to require ways be a
    // power of two.
    bool set_geometry_parameter(std::string_view name, long value, std::string& error,
                                bool power_of_two_ways = false);

    ICacheConfig config;
};
