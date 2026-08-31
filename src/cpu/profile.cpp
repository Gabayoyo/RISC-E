#include "risc-e/cpu/profile.hpp"

#include "risc-e/harness/run_context.hpp"

#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>

namespace {

std::string fixed(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

} // namespace

void ProfileStats::reset() {
    instructions = 0;
    seen_pcs.clear();
    pc_to_id.clear();
    blocks.clear();
    entry_sequence.clear();
}

uint32_t ProfileStats::record_block_entry(uint32_t entry_pc) {
    auto it = pc_to_id.find(entry_pc);
    uint32_t id;
    if (it == pc_to_id.end()) {
        id = static_cast<uint32_t>(blocks.size());
        pc_to_id.emplace(entry_pc, id);
        blocks.push_back(BlockInfo{entry_pc, 0, 0});
    } else {
        id = it->second;
    }
    ++blocks[id].executions;
    if (entry_sequence.size() < kMaxEntries) entry_sequence.push_back(id);
    return id;
}

void ProfileStats::record_instruction(uint32_t pc) {
    ++instructions;
    seen_pcs.insert(pc);
}

void ProfileStats::record_block_instruction(uint32_t block_id) {
    if (block_id != kNoBlock) ++blocks[block_id].instructions;
}

CacheTime simulate_icache(const ProfileStats& stats, const CacheModel& model) {
    CacheTime t;

    // Instruction cache: resident blocks with their footprint and last-use
    // tick. Capacity is measured in instructions; LRU evicts the least
    // recently used block when a new one does not fit.
    struct CacheEntry {
        uint32_t block_id;
        uint64_t size;
        uint64_t last_use;
    };
    std::vector<CacheEntry> cache;
    uint64_t cache_used = 0;

    std::vector<char> seen(stats.blocks.size(), 0);
    const uint64_t capacity = static_cast<uint64_t>(model.cache_capacity);

    uint64_t tick = 0;
    for (const uint32_t id : stats.entry_sequence) {
        ++tick;
        ++t.entries;
        const BlockInfo& b = stats.blocks[id];
        const uint64_t per_exec = b.executions == 0 ? 0 : b.instructions / b.executions;

        // Resident: hit, refresh recency.
        std::size_t resident = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0; i < cache.size(); ++i) {
            if (cache[i].block_id == id) {
                resident = i;
                break;
            }
        }
        if (resident != std::numeric_limits<std::size_t>::max()) {
            cache[resident].last_use = tick;
            ++t.hits;
            continue;
        }

        // Not resident: miss. The first touch of a block is compulsory; a
        // later miss after eviction is a capacity miss.
        ++t.misses;
        if (!seen[id]) {
            seen[id] = 1;
            ++t.compulsory_misses;
        }
        if (capacity != 0 && per_exec > capacity) continue;  // block does not fit at all

        // Make room: evict least-recently-used blocks until it fits.
        while (capacity != 0 && cache_used + per_exec > capacity && !cache.empty()) {
            std::size_t victim = 0;
            for (std::size_t i = 1; i < cache.size(); ++i) {
                if (cache[i].last_use < cache[victim].last_use) victim = i;
            }
            cache_used -= cache[victim].size;
            cache.erase(cache.begin() + static_cast<std::ptrdiff_t>(victim));
            ++t.evictions;
        }
        cache.push_back({id, per_exec, tick});
        cache_used += per_exec;
    }

    t.miss_stalls = t.misses * static_cast<uint64_t>(model.miss_penalty);
    t.total_cycles = stats.instructions + t.miss_stalls;
    t.baseline_cycles = stats.instructions + t.entries * static_cast<uint64_t>(model.miss_penalty);
    t.saved_cycles = static_cast<int64_t>(t.baseline_cycles) - static_cast<int64_t>(t.total_cycles);
    if (t.baseline_cycles != 0) {
        t.saved_pct = 100.0 * static_cast<double>(t.saved_cycles) /
                      static_cast<double>(t.baseline_cycles);
    }
    t.hit_rate = t.entries == 0 ? 0.0 : 100.0 * static_cast<double>(t.hits) /
                                             static_cast<double>(t.entries);
    return t;
}

std::vector<ParamSpec> ProfileComponent::parameters() const {
    return {
        {"miss-penalty", "stall cycles per instruction-cache miss", 0, 0,
         std::to_string(model.miss_penalty)},
        {"cache-capacity", "cache size in instructions; 0 = unlimited", 0, 0,
         std::to_string(model.cache_capacity)},
    };
}

bool ProfileComponent::set_parameter(std::string_view name, std::string_view value,
                                     std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (name == "miss-penalty") {
        model.miss_penalty = *parsed;
        return true;
    }
    if (name == "cache-capacity") {
        model.cache_capacity = *parsed;
        return true;
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}

std::string_view ProfileComponent::report_title() const {
    return "profile";
}

std::optional<CycleCost> ProfileComponent::cycle_cost(const RunContext& ctx) {
    if (ctx.profile_stats == nullptr) return std::nullopt;
    const CacheTime t = simulate_icache(*ctx.profile_stats, model);
    return CycleCost{t.total_cycles, t.baseline_cycles, "no instruction cache"};
}

void ProfileComponent::report(std::ostream& out, const RunContext& ctx) const {
    if (ctx.profile_stats == nullptr) return;
    const ProfileStats& s = *ctx.profile_stats;

    out << "  instructions executed: " << s.instructions << '\n'
        << "  distinct instructions: " << s.seen_pcs.size() << '\n'
        << "  basic blocks: " << s.blocks.size() << '\n';

    // Simulated instruction-cache model: blocks are fetched on first touch
    // (compulsory miss), hit while resident, and miss again after LRU
    // eviction. Each miss stalls the fetch stage for miss-penalty cycles;
    // cycles saved is measured against a machine with no instruction cache.
    const CacheTime t = simulate_icache(s, model);
    out << "  instruction cache (miss penalty " << model.miss_penalty << ", cache "
        << (model.cache_capacity == 0 ? "unlimited" : std::to_string(model.cache_capacity))
        << " instrs):\n"
        << "    hits: " << t.hits << " (" << fixed(t.hit_rate, 2) << "%)\n"
        << "    misses: " << t.misses << '\n'
        << "    compulsory misses: " << t.compulsory_misses << '\n'
        << "    evictions: " << t.evictions << '\n'
        << "    miss stalls: " << t.miss_stalls << " (" << t.misses << " x "
        << model.miss_penalty << " cycles)\n"
        << "    total cycles: " << t.total_cycles << '\n'
        << "    no-cache baseline: " << t.baseline_cycles << '\n'
        << "    cycles saved: " << t.saved_cycles << " (" << fixed(t.saved_pct, 2)
        << "%)\n";
    if (s.entry_sequence.size() == ProfileStats::kMaxEntries) {
        out << "    note: entry sequence truncated at " << ProfileStats::kMaxEntries
            << "; later entries not simulated\n";
    }
}
