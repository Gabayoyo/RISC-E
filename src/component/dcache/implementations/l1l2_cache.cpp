#include "risc-e/component/dcache/implementations/l1l2_cache.hpp"

#include "risc-e/component/run_context.hpp"

#include <iomanip>
#include <optional>
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

// Applies one level's shared tunable (hit-latency, miss-penalty, line-size,
// sets, ways) to `config`. False when the name is not one of these.
bool set_level_parameter(DCacheConfig& config, std::string_view name, long value,
                         std::string& error) {
    if (name == "hit-latency") {
        config.hit_latency = value;
        return true;
    }
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
    if (name == "sets") {
        if (value < 1) {
            error = "sets must be >= 1";
            return false;
        }
        config.sets = value;
        return true;
    }
    if (name == "ways") {
        if (value < 1) {
            error = "ways must be >= 1";
            return false;
        }
        config.ways = value;
        return true;
    }
    (void)error;
    return false;
}

void append_level_parameters(std::vector<ParamSpec>& out, std::string_view prefix,
                             const DCacheConfig& config) {
    const std::string p(prefix);
    out.push_back({p + "hit-latency", "cycles for a hit at this level", 0, 0,
                   std::to_string(config.hit_latency)});
    out.push_back({p + "miss-penalty",
                   "cycles for a miss at this level (L2: the DRAM round trip)", 0, 0,
                   std::to_string(config.miss_penalty)});
    out.push_back({p + "line-size", "cache line size in bytes", 1, 0,
                   std::to_string(config.line_size)});
    out.push_back({p + "sets", "number of sets", 1, 0, std::to_string(config.sets)});
    out.push_back({p + "ways", "lines per set (1 = direct-mapped)", 1, 0,
                   std::to_string(config.ways)});
}

void print_level(std::ostream& out, const std::string& label, const DCacheConfig& config,
                 const DCacheResult& r, uint64_t hit_latency) {
    out << "  " << label << " (" << config.sets << " set" << (config.sets == 1 ? "" : "s")
        << " x " << config.ways << " way" << (config.ways == 1 ? "" : "s") << ", line "
        << config.line_size << " B, write-back, " << hit_latency << "-cycle hit):\n"
        << "    hits: " << r.hits << " (" << fixed(r.hit_rate, 2) << "%)\n"
        << "    misses: " << r.misses << '\n'
        << "    compulsory misses: " << r.compulsory_misses << '\n'
        << "    conflict misses: " << r.conflict_misses << '\n'
        << "    capacity misses: " << r.capacity_misses << '\n'
        << "    evictions: " << r.evictions << '\n'
        << "    writebacks: " << r.writebacks << '\n';
}

// One cache level as a JSON object: its config (verbose only) plus the
// result fields the printed report shows.
void write_level_json(std::ostream& out, const DCacheConfig& config, const DCacheResult& r,
                      bool verbose) {
    out << "{";
    // Config is verbose detail: the default report keeps headline stats.
    if (verbose) {
        out << "\"config\": {\"hit_latency\": " << config.hit_latency
            << ", \"miss_penalty\": " << config.miss_penalty << ", \"line_size\": " << config.line_size
            << ", \"sets\": " << config.sets << ", \"ways\": " << config.ways
            << ", \"write_buffer_depth\": " << config.write_buffer_depth
            << ", \"write_policy\": \""
            << (config.write_policy == WritePolicy::WriteBack ? "write-back" : "write-through")
            << "\", \"write_allocate\": "
            << (config.write_allocate == WriteAllocate::WriteAllocate ? "true" : "false")
            << "}, ";
    }
    out << "\"hits\": " << r.hits << ", \"hit_rate\": " << fixed(r.hit_rate, 6)
        << ", \"misses\": " << r.misses << ", \"compulsory_misses\": " << r.compulsory_misses
        << ", \"conflict_misses\": " << r.conflict_misses
        << ", \"capacity_misses\": " << r.capacity_misses << ", \"evictions\": " << r.evictions
        << ", \"writebacks\": " << r.writebacks << "}";
}

// Simulates the L1->L2 chain (L1's refills and dirty evictions are forwarded
// to L2, so L1 only pays its hits) and the L1-only baseline, where every L1
// miss and writeback pays DRAM directly.
struct HierarchyResult {
    DCacheResult l1;
    DCacheResult l2;
    uint64_t total_cycles = 0;
    uint64_t baseline_cycles = 0;
};

HierarchyResult simulate_hierarchy(const DCacheConfig& l1, const DCacheConfig& l2,
                                   const DCacheStats& trace) {
    DCacheConfig l1_chain = l1;
    l1_chain.miss_penalty = 0;
    DCacheStats miss_stream;
    const DCacheResult r1 = simulate_dcache(trace, l1_chain, &miss_stream);
    const DCacheResult r2 = simulate_dcache(miss_stream, l2);

    DCacheConfig l1_only = l1;
    l1_only.miss_penalty = l2.miss_penalty;
    const DCacheResult baseline_r = simulate_dcache(trace, l1_only);

    return {r1, r2, r1.total_cycles + r2.total_cycles, baseline_r.total_cycles};
}

} // namespace

L1L2Cache::L1L2Cache() {
    l1.hit_latency = 4;    // documented constant: L1 hit
    l1.line_size = 16;
    l1.sets = 16;
    l1.ways = 4;
    l1.write_policy = WritePolicy::WriteBack;
    l1.write_allocate = WriteAllocate::WriteAllocate;

    l2.hit_latency = 14;   // documented constant: L2 hit
    l2.miss_penalty = 100; // documented constant: DRAM round trip
    l2.line_size = 64;
    l2.sets = 32;
    l2.ways = 8;
    l2.write_policy = WritePolicy::WriteBack;
    l2.write_allocate = WriteAllocate::WriteAllocate;
}

std::vector<ParamSpec> L1L2Cache::parameters() const {
    std::vector<ParamSpec> out;
    append_level_parameters(out, "l1-", l1);
    append_level_parameters(out, "l2-", l2);
    return out;
}

bool L1L2Cache::set_parameter(std::string_view name, std::string_view value,
                              std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (name.size() > 3) {
        const std::string_view rest = name.substr(3);
        if (name.substr(0, 3) == "l1-") {
            if (set_level_parameter(l1, rest, *parsed, error)) return true;
        } else if (name.substr(0, 3) == "l2-") {
            if (set_level_parameter(l2, rest, *parsed, error)) return true;
        }
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}

std::optional<CycleCost> L1L2Cache::cycle_cost(const RunContext& ctx) {
    if (ctx.access_trace == nullptr) return std::nullopt;
    const HierarchyResult h = simulate_hierarchy(l1, l2, *ctx.access_trace);
    return CycleCost{h.total_cycles, h.baseline_cycles, "L1 only"};
}

void L1L2Cache::report(std::ostream& out, const RunContext& ctx) const {
    if (ctx.access_trace == nullptr) return;
    const DCacheStats& tr = *ctx.access_trace;
    const HierarchyResult h = simulate_hierarchy(l1, l2, tr);

    const double speedup =
        h.baseline_cycles == 0 ? 0.0
                               : static_cast<double>(h.baseline_cycles) /
                                     static_cast<double>(h.total_cycles);
    const int64_t saved =
        static_cast<int64_t>(h.baseline_cycles) - static_cast<int64_t>(h.total_cycles);
    const double saved_pct = h.baseline_cycles == 0
                                 ? 0.0
                                 : 100.0 * static_cast<double>(saved) /
                                       static_cast<double>(h.baseline_cycles);

    print_level(out, "L1", l1, h.l1, static_cast<uint64_t>(l1.hit_latency));
    print_level(out, "L2", l2, h.l2, static_cast<uint64_t>(l2.hit_latency));
    out << "  cycles saved: " << saved << " (" << fixed(saved_pct, 2) << "%) vs L1 only \u2014 "
        << fixed(speedup, 2) << "x\n";
    if (tr.records.size() == DCacheStats::kMaxRecords) {
        out << "  note: access sequence truncated at " << DCacheStats::kMaxRecords
            << "; later accesses not simulated\n";
    }
}

void L1L2Cache::write_json(std::ostream& out, const RunContext& ctx) const {
    if (ctx.access_trace == nullptr) {
        out << "{}";
        return;
    }
    const HierarchyResult h = simulate_hierarchy(l1, l2, *ctx.access_trace);
    const int64_t saved = static_cast<int64_t>(h.baseline_cycles) - static_cast<int64_t>(h.total_cycles);
    const double saved_pct = h.baseline_cycles == 0
                                 ? 0.0
                                 : 100.0 * static_cast<double>(saved) /
                                       static_cast<double>(h.baseline_cycles);
    const double speedup = h.baseline_cycles == 0
                               ? 0.0
                               : static_cast<double>(h.baseline_cycles) /
                                     static_cast<double>(h.total_cycles);

    out << "{\"l1\":";
    write_level_json(out, l1, h.l1, ctx.verbose);
    out << ",\"l2\":";
    write_level_json(out, l2, h.l2, ctx.verbose);
    out << ",\"cycles_saved\":" << saved << ",\"saved_pct\":" << fixed(saved_pct, 6)
        << ",\"speedup\":" << fixed(speedup, 6) << "}";
}
