#pragma once

#include "risc-e/component/icache/icache.hpp"

#include <string_view>

// Set-associative instruction cache: `sets` sets of `ways` lines, true LRU
// within a set. Direct-mapped is ways == 1. Lines that collide on a set
// cause conflict misses.
class SetAssociativeICache : public ICacheComponent {
public:
    static constexpr std::string_view kName = "icache-setassoc";

    SetAssociativeICache() {
        config.sets = 16;
        config.ways = 4;
    }

    std::string_view name() const override { return kName; }
    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;
};
