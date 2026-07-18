#pragma once

#include "src/interpreter/memory_interface.hpp"

class MemoryInterface; // forward declaration

enum class HaltReason : uint8_t {
    NONE,   // still running
    ECALL,  // halted via ecall instruction
    EBREAK, // halted via ebreak instruction
    TRAP,   // halted due to an unhandled trap
};

struct CPUstate {
    // General-purpose registers x0–x31. x0 is hardwired to 0.
    uint32_t x[32] = {};

    // Program counter (current or next instruction address)
    uint32_t pc = 0;

    // Control and status registers (simplified: just a few key ones)
    uint32_t csr[4096] = {};  // sparse storage or map

    // Pointer to memory subsystem (or inline memory interface)
    MemoryInterface* mem = nullptr;

    // ---- fields that will help JIT integration later ----
    // Opaque pointer for JIT-specific state (null when interpreter-only)
    void* jit_state = nullptr;

    // Cycle counter or timestamp (optional)
    uint64_t cycle = 0;

    // True if the CPU is currently running (not halted or waiting for an event)
    bool running = true;

    // Records *why* the CPU stopped (for run() to report)
    HaltReason halt_reason = HaltReason::NONE;

    uint32_t mepc    = 0;   // exception program counter (saved PC)
    uint32_t mcause = 0;   // trap cause
    uint32_t mtval  = 0;   // trap value (address or 0)

    bool machineMode = true;   // if you have privilege modes, otherwise ignore
};
