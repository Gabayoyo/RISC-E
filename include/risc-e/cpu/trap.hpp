#pragma once

#include <cstdint>

enum class TrapCause : uint32_t {
    INSTRUCTION_ADDRESS_MISALIGNED = 0,
    INSTRUCTION_ACCESS_FAULT       = 1,
    ILLEGAL_INSTRUCTION            = 2,
    LOAD_MISALIGNED                = 4,
    LOAD_FAULT                     = 5,
    STORE_MISALIGNED               = 6,
    STORE_FAULT                    = 7,
    ENVIRONMENT_CALL_FROM_MMODE    = 11,
};

class TrapSink {
public:
    virtual void raise_trap(TrapCause cause, uint32_t value = 0) = 0;
    virtual ~TrapSink() = default;
};
