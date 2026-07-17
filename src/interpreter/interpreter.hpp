#pragma once

#include "src/elf/loader.hpp"   // LoadedElf with 32‑bit addresses
#include "src/interpreter/physical_memory.hpp"
#include "src/decoder/decoded_instruction.hpp"
#include "src/interpreter/state.hpp"

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <map>

/**
 * @brief A simple RV32I interpreter.
 *
 * It loads code from a LoadedElf and executes instructions
 * starting at the entry point. Only base integer instructions are supported.
 */
class Interpreter : public TrapSink {
public:
    static constexpr int REG_COUNT = 32;

    explicit Interpreter(LoadedElf elf);

    // Disallow copy (holds reference to external memory)
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Allow move
    Interpreter(Interpreter&&) = default;
    Interpreter& operator=(Interpreter&&) = delete;

    // Execute a single instruction.
    void step();

    // Run until an ECALL (system call) is encountered or an error.
    void run();

    // Reset PC to entry point and zero all registers.
    void reset();

    uint32_t get_pc() const { return state_.pc; }
    uint32_t get_register(int idx) const;
    void raiseTrap(TrapCause cause, uint32_t value = 0) override;

private:
    uint32_t base_vaddr_;
    uint32_t entry_; // original entry point preserved for reset

    std::vector<DecodedInstruction> decode_cache_;

    CPUstate state_; // for exception handling and JIT integration
    PhysicalMemory mem_; // concrete memory implementation

    void execute(const DecodedInstruction& d);

    // Helpers
    uint32_t fetch_instruction(uint32_t vaddr) const;
    void handle_ecall();
    void load_elf_segments(PhysicalMemory& mem, const LoadedElf& elf);
};