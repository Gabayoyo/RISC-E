#pragma once

#include <variant>
#include <cstdint>

struct Register {
    uint8_t id;
};

struct Immediate {
    int32_t value;
};

struct Label {
    uint32_t target;
};

using Operand = std::variant<Register, Immediate, Label>;