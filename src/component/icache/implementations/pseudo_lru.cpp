#include "risc-e/component/icache/implementations/pseudo_lru.hpp"

#include <optional>
#include <string>
#include <vector>

std::vector<ParamSpec> PseudoLruICache::parameters() const {
    std::vector<ParamSpec> out;
    append_shared_parameters(out);
    append_geometry_parameters(out, "lines per set (power of two for PLRU)");
    return out;
}

bool PseudoLruICache::set_parameter(std::string_view name, std::string_view value,
                                    std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (set_shared_parameter(name, *parsed, error)) return true;
    if (set_geometry_parameter(name, *parsed, error, /*power_of_two_ways=*/true)) return true;
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
