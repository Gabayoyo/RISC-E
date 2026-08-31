#include "risc-e/harness/component.hpp"

#include <exception>
#include <string>

std::optional<long> parse_parameter_value(std::string_view value, std::string& error) {
    if (value.empty()) {
        error = "expected an integer value";
        return std::nullopt;
    }
    const std::string text(value);
    std::size_t consumed = 0;
    long result = 0;
    try {
        result = std::stol(text, &consumed);
    } catch (const std::exception&) {
        error = "\"" + text + "\" is not an integer";
        return std::nullopt;
    }
    if (consumed != text.size()) {
        error = "\"" + text + "\" is not an integer";
        return std::nullopt;
    }
    if (result < 0) {
        error = "expected a non-negative integer";
        return std::nullopt;
    }
    return result;
}
