#include "risc-e/cpu/icache/fully_associative.hpp"

#include <optional>
#include <string>
#include <vector>

std::vector<ParamSpec> FullyAssociativeICache::parameters() const {
    std::vector<ParamSpec> out;
    append_shared_parameters(out);
    out.push_back({"ways", "number of lines (capacity = ways x line-size)", 1, 0,
                   std::to_string(config.ways)});
    return out;
}

bool FullyAssociativeICache::set_parameter(std::string_view name, std::string_view value,
                                           std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (set_shared_parameter(name, *parsed, error)) return true;
    if (name == "ways") {
        if (*parsed < 1) {
            error = "ways must be >= 1";
            return false;
        }
        config.ways = *parsed;
        return true;
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
