#pragma once

#include "risc-e/core/memory/memory_interface.hpp"

#include <cstdint>

enum class HaltReason : uint8_t {
    NONE,   // still running
    ECALL,  // halted via ecall instruction
    EBREAK, // halted via ebreak instruction
    TRAP,   // halted due to an unhandled trap
};

struct CPUstate {
    // General-purpose registers x0-x31. x0 is hardwired to 0.
    uint32_t x[32] = {};

    // Program counter (current or next instruction address)
    uint32_t pc = 0;

    // Pointer to the memory subsystem
    MemoryInterface* mem = nullptr;

    // True if the CPU is currently running (not halted or waiting for an event)
    bool running = true;

    // Records why the CPU stopped (for run() to report)
    HaltReason halt_reason = HaltReason::NONE;

    // Trap state
    uint32_t mepc   = 0;   // exception program counter (saved PC)
    uint32_t mcause = 0;   // trap cause
    uint32_t mtval  = 0;   // trap value (address or 0)
};
