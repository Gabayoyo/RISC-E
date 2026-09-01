#include "risc-e/component/icache/implementations/prefetch.hpp"

#include <optional>
#include <string>
#include <vector>

std::vector<ParamSpec> PrefetchICache::parameters() const {
    std::vector<ParamSpec> out;
    append_shared_parameters(out);
    append_geometry_parameters(out, "lines per set (1 = direct-mapped)");
    return out;
}

bool PrefetchICache::set_parameter(std::string_view name, std::string_view value,
                                   std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (set_shared_parameter(name, *parsed, error)) return true;
    if (set_geometry_parameter(name, *parsed, error)) return true;
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
