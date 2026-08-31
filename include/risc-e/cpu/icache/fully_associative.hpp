#pragma once

#include "risc-e/cpu/icache.hpp"

#include <string_view>

// Fully-associative instruction cache: one set of `ways` lines, true LRU.
// No conflicts are possible — every re-entry miss is a capacity miss.
class FullyAssociativeICache : public ICacheComponent {
public:
    static constexpr std::string_view kName = "icache-fa";

    FullyAssociativeICache() {
        config.sets = 1;
        config.ways = 16;
    }

    std::string_view name() const override { return kName; }
    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;
};
