#pragma once

#include <string>
#include <sstream>
#include <cstdint>

inline std::string toHex(uint32_t value) {
    std::ostringstream oss;
    oss << std::hex << value;
    return oss.str();
}