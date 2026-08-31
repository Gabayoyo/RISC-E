#include "risc-e/component/icache/implementations/pseudo_lru.hpp"

#include <optional>
#include <string>
#include <vector>

std::vector<ParamSpec> PseudoLruICache::parameters() const {
    std::vector<ParamSpec> out;
    append_shared_parameters(out);
    out.push_back({"sets", "number of sets", 1, 0, std::to_string(config.sets)});
    out.push_back({"ways", "lines per set (power of two for PLRU)", 1, 0,
                   std::to_string(config.ways)});
    return out;
}

bool PseudoLruICache::set_parameter(std::string_view name, std::string_view value,
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
        if (*parsed < 1 || (*parsed & (*parsed - 1)) != 0) {
            error = "ways must be a power of two for PLRU";
            return false;
        }
        config.ways = *parsed;
        return true;
    }
    error = "unknown parameter \"" + std::string(name) + "\"";
    return false;
}
