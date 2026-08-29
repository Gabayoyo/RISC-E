#pragma once

#include "risc-e/core/cpu/trap.hpp"

#include <cstdint>

// Contract between the execution core and a frontend (CLI, playground, debugger,
// tests). A frontend subclasses HostInterface and hands it to the Interpreter;
// the core calls these hooks instead of hard-coding host behavior. The core never
// includes frontend code; the dependency points one way.
//
// Every method has a default implementation so a frontend only overrides what it
// needs. The per-instruction hook is a no-op by default to keep the hot path cheap;
// a tracing frontend flips it on explicitly.
class HostInterface {
public:
    virtual ~HostInterface() = default;

    // ECALL syscall emulation. `number` is a7, `a0`-`a2` the arguments.
    // Return true if handled: the core places `result` in a0 and advances PC past
    // the ECALL. Return false to fall back to the core's built-in emulation
    // (currently write/exit/brk).
    virtual bool syscall(uint32_t number, uint32_t a0, uint32_t a1, uint32_t a2,
                         uint32_t& result) {
        (void)number;
        (void)a0;
        (void)a1;
        (void)a2;
        (void)result;
        return false;
    }

    // Called before each instruction executes, when tracing is enabled.
    virtual void on_instruction(uint32_t pc, uint32_t raw) {
        (void)pc;
        (void)raw;
    }

    // Called when the core raises a trap (including ECALL) at `pc`.
    virtual void on_trap(TrapCause cause, uint32_t pc, uint32_t value) {
        (void)cause;
        (void)pc;
        (void)value;
    }
};
