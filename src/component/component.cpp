#include "risc-e/component/component.hpp"

#include "risc-e/component/run_context.hpp"

#include <cstdio>
#include <exception>
#include <ostream>
#include <string>

void print_component_config(std::ostream& out, const Component& comp, const RunContext& ctx) {
    if (!ctx.verbose) return;
    for (const ParamSpec& p : comp.parameters()) {
        out << "    " << p.name << " = " << p.default_value << '\n';
    }
}

std::string json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

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
