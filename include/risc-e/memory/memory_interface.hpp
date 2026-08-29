#pragma once

#include "risc-e/cpu/trap.hpp"

#include <cstdint>

class MemoryInterface {
protected:
    TrapSink* trapSink_ = nullptr;

public:
    void setTrapSink(TrapSink* sink) { trapSink_ = sink; }

    virtual uint32_t load32(uint32_t addr) = 0;
    virtual void    store32(uint32_t addr, uint32_t value) = 0;
    virtual uint16_t load16(uint32_t addr) = 0;
    virtual void    store16(uint32_t addr, uint16_t value) = 0;
    virtual uint8_t  load8(uint32_t addr) = 0;
    virtual void    store8(uint32_t addr, uint8_t value) = 0;
    virtual ~MemoryInterface() = default;
};
