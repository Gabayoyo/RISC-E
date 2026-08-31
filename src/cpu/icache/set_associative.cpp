#include "risc-e/cpu/icache/set_associative.hpp"

#include <optional>
#include <string>
#include <vector>

std::vector<ParamSpec> SetAssociativeICache::parameters() const {
    std::vector<ParamSpec> out;
    append_shared_parameters(out);
    out.push_back({"sets", "number of sets", 1, 0, std::to_string(config.sets)});
    out.push_back({"ways", "lines per set (1 = direct-mapped)", 1, 0,
                   std::to_string(config.ways)});
    return out;
}

bool SetAssociativeICache::set_parameter(std::string_view name, std::string_view value,
                                         std::string& error) {
    const std::optional<long> parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (set_shared_parameter(name, *parsed, error)) return true;
    if (name == "sets") {
        if (*parsed < 1) {
            error = "sets must be >= 1";
            return false;
        }
        config.sets = *parsed;
        return true;
    }
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
