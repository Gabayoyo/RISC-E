#pragma once

#include "risc-e/cpu/icache.hpp"

#include <string_view>

// Set-associative instruction cache with true LRU plus next-line prefetch:
// on a miss the following line is fetched too, so sequential blocks hit
// without waiting for their demand miss.
class PrefetchICache : public ICacheComponent {
public:
    static constexpr std::string_view kName = "icache-prefetch";

    PrefetchICache() {
        config.sets = 16;
        config.ways = 4;
        config.prefetch = true;
    }

    std::string_view name() const override { return kName; }
    std::vector<ParamSpec> parameters() const override;
    bool set_parameter(std::string_view name, std::string_view value,
                       std::string& error) override;
};
