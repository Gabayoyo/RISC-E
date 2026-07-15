#pragma once

#include "src/elf/loader.hpp"   // LoadedElf with 32‑bit addresses
#include "src/ir/ir_module.hpp" // IRModule, IRFunction, BasicBlock, Operation

#include <cstdint>
#include <vector>
#include <stdexcept>

/**
 * @brief A simple RV32I interpreter.
 *
 * It loads code from a LoadedElf and executes instructions
 * starting at the entry point. Only base integer instructions are supported.
 */
class Interpreter {
public:
    static constexpr int REG_COUNT = 32;

    explicit Interpreter(LoadedElf elf);

    // Disallow copy (holds reference to external memory)
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Allow move
    Interpreter(Interpreter&&) = default;
    Interpreter& operator=(Interpreter&&) = delete;

    void load(const IRModule& module);

    // Execute a single instruction.
    void step();

    // Run until an ECALL (system call) is encountered or an error.
    void run();

    // Reset PC to entry point and zero all registers.
    void reset();

    void print_code(std::ostream& os) const {
        os << "Interpreter code (" << code_.size() << " instructions):\n";
        for (const auto& op : code_) {
            op->print(os, 2);
        }
    }

    

    uint32_t get_pc() const { return pc_; }
    uint32_t get_register(int idx) const;

private:
    // Memory reference and address mapping
    std::vector<uint8_t> memory_;
    uint32_t base_vaddr_;
    uint32_t entry_; // original entry point preserved for reset

    // Architectural state
    uint32_t regs_[REG_COUNT];
    uint32_t pc_;
    std::vector<std::shared_ptr<Operation>> code_;

    // Helpers
    uint32_t fetch_instruction(uint32_t vaddr) const;
    uint32_t load_memory(uint32_t vaddr, int size_bytes);
    void store_memory(uint32_t vaddr, int size_bytes, uint32_t value);
    void handle_ecall();
};