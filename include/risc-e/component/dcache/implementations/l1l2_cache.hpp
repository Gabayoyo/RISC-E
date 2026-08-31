#pragma once

#include "risc-e/component/component.hpp"
#include "risc-e/component/dcache/dcache.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The L1 + L2 data-cache hierarchy: one component that chains two cache
// levels. L1 (write-back, write-allocate) sees every load/store; its misses
// and dirty evictions are forwarded to L2, which is backed by DRAM. Cycles
// are the sum of the two levels' costs under documented latency constants
// (defaults: L1 hit 4, L2 hit 14, DRAM 100). The comparison baseline is the
// same L1 with no L2, so the speedup answers "what does the second level
// buy" against a real design, not against "no cache at all".
class L1L2Cache : public Component {
public:
    static constexpr std::string_view kName = "l1-l2";

    L1L2Cache();

    std::string_view name() const override { return kName; }
    std::string_view type() const override { return "cache"; }

    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;

    std::string_view report_title() const override { return "cache"; }
    void report(std::ostream& out, const RunContext& ctx) const override;

    // Cost answer: L1+L2 cycles vs the L1-only baseline.
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override;

private:
    DCacheConfig l1;  // write-back, write-allocate; hit latency 4
    DCacheConfig l2;  // write-back, write-allocate; hit 14, DRAM miss penalty
};
