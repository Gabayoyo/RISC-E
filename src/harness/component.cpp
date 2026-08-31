#include "risc-e/harness/component.hpp"

#include <iomanip>
#include <sstream>

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

std::string format_metric(const Metric& m) {
    std::ostringstream out;
    if (m.denominator.has_value()) {
        out << std::get<uint64_t>(m.value) << "/" << *m.denominator;
    } else if (std::holds_alternative<double>(m.value)) {
        out << std::fixed << std::setprecision(2) << std::get<double>(m.value);
    } else {
        out << std::get<uint64_t>(m.value);
    }
    if (!m.unit.empty()) out << m.unit;
    return out.str();
}
