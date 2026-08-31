#pragma once

#include "risc-e/component/icache/icache.hpp"

#include <string_view>

// Set-associative instruction cache with tree-based pseudo-LRU replacement
// (the policy most real cores ship: nearly LRU quality at a fraction of the
// hardware). Ways must be a power of two.
class PseudoLruICache : public ICacheComponent {
public:
    static constexpr std::string_view kName = "icache-plru";

    PseudoLruICache() {
        config.sets = 16;
        config.ways = 4;
        config.policy = Replacement::PLRU;
    }

    std::string_view name() const override { return kName; }
    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;
};
