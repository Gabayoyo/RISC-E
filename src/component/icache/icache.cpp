#include "risc-e/component/icache/icache.hpp"

#include "risc-e/component/run_context.hpp"

#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string fixed(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

// Tree-based pseudo-LRU. bits[1..ways-1] form the tree: a 0 bit sends the
// eviction walk left, a 1 sends it right. Touching a way sets the bits along
// its path away from it, marking that way most-recently-used.
void touch_plru(std::vector<uint8_t>& bits, std::size_t ways, std::size_t way) {
    std::size_t node = 1;
    std::size_t mask = ways >> 1;
    while (mask != 0) {
        if (way & mask) {
            bits[node] = 0;  // eviction prefers the left subtree
            node = node * 2 + 1;
        } else {
            bits[node] = 1;  // eviction prefers the right subtree
            node = node * 2;
        }
        mask >>= 1;
    }
}

std::size_t victim_plru(const std::vector<uint8_t>& bits, std::size_t ways) {
    std::size_t node = 1;
    while (node < ways) {
        node = bits[node] ? node * 2 + 1 : node * 2;
    }
    return node - ways;
}

} // namespace

ICacheResult simulate_icache(const ICacheStats& stats, const ICacheConfig& cfg) {
    ICacheResult t;
    if (cfg.sets < 1 || cfg.ways < 1 || cfg.line_size < 1) return t;

    const uint64_t line_bytes = static_cast<uint64_t>(cfg.line_size);
    const uint64_t sets = static_cast<uint64_t>(cfg.sets);
    const uint64_t ways = static_cast<uint64_t>(cfg.ways);
    const uint64_t penalty = static_cast<uint64_t>(cfg.miss_penalty);

    struct Way {
        uint64_t tag;
        uint64_t last_use;
    };
    struct Set {
        std::vector<Way> ways;
        std::vector<uint8_t> plru;  // tree bits (indices 1..ways-1); empty for LRU
    };
    std::vector<Set> cache(sets);
    for (Set& s : cache) {
        s.ways.reserve(ways);
        if (cfg.policy == Replacement::PLRU) s.plru.assign(ways, 0);
    }
    std::vector<char> block_seen(stats.blocks.size(), 0);

    const auto index_of = [&](uint64_t line) -> uint64_t { return line % sets; };
    const auto tag_of = [&](uint64_t line) -> uint64_t { return line / sets; };

    const auto resident = [&](uint64_t line) -> bool {
        const uint64_t tag = tag_of(line);
        for (const Way& w : cache[index_of(line)].ways) {
            if (w.tag == tag) return true;
        }
        return false;
    };

    // Updates replacement metadata for a resident line.
    const auto touch = [&](uint64_t line, uint64_t tick) {
        const uint64_t tag = tag_of(line);
        Set& s = cache[index_of(line)];
        for (std::size_t i = 0; i < s.ways.size(); ++i) {
            if (s.ways[i].tag == tag) {
                if (cfg.policy == Replacement::LRU) {
                    s.ways[i].last_use = tick;
                } else {
                    touch_plru(s.plru, ways, i);
                }
                return;
            }
        }
    };

    const auto evict_one = [&](Set& s) {
        std::size_t victim = 0;
        if (cfg.policy == Replacement::PLRU) {
            victim = victim_plru(s.plru, ways);
        } else {
            for (std::size_t i = 1; i < s.ways.size(); ++i) {
                if (s.ways[i].last_use < s.ways[victim].last_use) victim = i;
            }
        }
        s.ways.erase(s.ways.begin() + static_cast<std::ptrdiff_t>(victim));
    };

    // Inserts a line, evicting when its set is full. `is_prefetch` only
    // changes the accounting; the fill itself is free (backgrounded).
    const auto refill = [&](uint64_t line, uint64_t tick, bool is_prefetch) {
        Set& s = cache[index_of(line)];
        const uint64_t tag = tag_of(line);
        for (const Way& w : s.ways) {
            if (w.tag == tag) return;  // already resident
        }
        if (s.ways.size() == ways) {
            evict_one(s);
            ++t.evictions;
        }
        s.ways.push_back(Way{tag, tick});
        if (cfg.policy == Replacement::PLRU) {
            touch_plru(s.plru, ways, s.ways.size() - 1);
        }
        if (is_prefetch) ++t.prefetches;
    };

    uint64_t tick = 0;
    for (const uint32_t id : stats.entry_sequence) {
        ++tick;
        ++t.entries;
        const BlockInfo& b = stats.blocks[id];
        const uint64_t per_exec = b.executions == 0 ? 0 : b.instructions / b.executions;
        const uint64_t first_line = static_cast<uint64_t>(b.entry_pc) / line_bytes;
        const uint64_t last_line =
            (static_cast<uint64_t>(b.entry_pc) + per_exec * 4u - 1u) / line_bytes;

        // A fetch (block entry) hits iff every line it spans is resident.
        bool all_resident = true;
        for (uint64_t line = first_line; line <= last_line; ++line) {
            if (!resident(line)) {
                all_resident = false;
                break;
            }
        }
        if (all_resident) {
            for (uint64_t line = first_line; line <= last_line; ++line) touch(line, tick);
            ++t.hits;
            continue;
        }

        ++t.misses;
        if (!block_seen[id]) {
            block_seen[id] = 1;
            ++t.compulsory_misses;
        } else if (sets > 1) {
            ++t.conflict_misses;   // re-entry miss under set pressure
        } else {
            ++t.capacity_misses;   // re-entry miss in a fully associative cache
        }

        for (uint64_t line = first_line; line <= last_line; ++line) {
            if (!resident(line)) refill(line, tick, false);
        }
        if (cfg.prefetch) refill(last_line + 1, tick, true);
    }

    t.miss_stalls = t.misses * penalty;
    t.total_cycles = stats.instructions + t.miss_stalls;
    t.baseline_cycles = stats.instructions + t.entries * penalty;
    t.saved_cycles =
        static_cast<int64_t>(t.baseline_cycles) - static_cast<int64_t>(t.total_cycles);
    if (t.baseline_cycles != 0) {
        t.saved_pct = 100.0 * static_cast<double>(t.saved_cycles) /
                      static_cast<double>(t.baseline_cycles);
    }
    t.hit_rate =
        t.entries == 0 ? 0.0 : 100.0 * static_cast<double>(t.hits) / static_cast<double>(t.entries);
    return t;
}

void ICacheComponent::append_shared_parameters(std::vector<ParamSpec>& out) const {
    out.push_back({"miss-penalty", "stall cycles per cache miss", 0, 0,
                   std::to_string(config.miss_penalty)});
    out.push_back({"line-size", "cache line size in bytes", 1, 0,
                   std::to_string(config.line_size)});
}

void ICacheComponent::append_geometry_parameters(std::vector<ParamSpec>& out,
                                                 std::string_view ways_help) const {
    out.push_back({"sets", "number of sets", 1, 0, std::to_string(config.sets)});
    out.push_back({"ways", std::string(ways_help), 1, 0, std::to_string(config.ways)});
}

bool ICacheComponent::set_geometry_parameter(std::string_view name, long value,
                                             std::string& error, bool power_of_two_ways) {
    if (name == "sets") {
        if (value < 1) {
            error = "sets must be >= 1";
            return false;
        }
        config.sets = value;
        return true;
    }
    if (name == "ways") {
        if (value < 1 || (power_of_two_ways && (value & (value - 1)) != 0)) {
            error = power_of_two_ways ? "ways must be a power of two for PLRU"
                                      : "ways must be >= 1";
            return false;
        }
        config.ways = value;
        return true;
    }
    (void)error;
    return false;
}

bool ICacheComponent::set_shared_parameter(std::string_view name, long value,
                                           std::string& error) {
    if (name == "miss-penalty") {
        config.miss_penalty = value;
        return true;
    }
    if (name == "line-size") {
        if (value < 1) {
            error = "line-size must be >= 1";
            return false;
        }
        config.line_size = value;
        return true;
    }
    (void)error;
    return false;
}

std::optional<CycleCost> ICacheComponent::cycle_cost(const RunContext& ctx) {
    if (ctx.profile_stats == nullptr) return std::nullopt;
    const ICacheResult t = simulate_icache(*ctx.profile_stats, config);
    return CycleCost{t.total_cycles, t.baseline_cycles, "no instruction cache"};
}

void ICacheComponent::report(std::ostream& out, const RunContext& ctx) const {
    if (ctx.profile_stats == nullptr) return;
    const ICacheStats& s = *ctx.profile_stats;

    const ICacheResult t = simulate_icache(s, config);
    out << "  instruction cache (miss penalty " << config.miss_penalty << ", line "
        << config.line_size << " B, " << config.sets << " set" << (config.sets == 1 ? "" : "s")
        << " x " << config.ways << " way" << (config.ways == 1 ? "" : "s") << ", "
        << (config.policy == Replacement::PLRU ? "PLRU" : "LRU")
        << (config.prefetch ? " + prefetch" : "") << "):\n"
        << "    hits: " << t.hits << " (" << fixed(t.hit_rate, 2) << "%)\n"
        << "    misses: " << t.misses << '\n'
        << "    compulsory misses: " << t.compulsory_misses << '\n'
        << "    conflict misses: " << t.conflict_misses << '\n'
        << "    capacity misses: " << t.capacity_misses << '\n'
        << "    evictions: " << t.evictions << '\n';
    if (config.prefetch) {
        out << "    prefetches: " << t.prefetches << '\n';
    }
    const double speedup =
        t.baseline_cycles == 0
            ? 0.0
            : static_cast<double>(t.baseline_cycles) / static_cast<double>(t.total_cycles);
    out << "  cycles saved: " << t.saved_cycles << " (" << fixed(t.saved_pct, 2)
        << "%) vs no instruction cache \u2014 " << fixed(speedup, 2) << "x\n";
    if (s.entry_sequence.size() == ICacheStats::kMaxEntries) {
        out << "    note: entry sequence truncated at " << ICacheStats::kMaxEntries
            << "; later entries not simulated\n";
    }
}
