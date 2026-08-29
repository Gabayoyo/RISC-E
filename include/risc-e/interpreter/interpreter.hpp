#pragma once

#include "risc-e/cpu/state.hpp"
#include "risc-e/cpu/trap.hpp"
#include "risc-e/decoder/decoded_instruction.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/memory/physical_memory.hpp"

#include <cstdint>
#include <optional>

class Interpreter : public TrapSink {
public:
    static constexpr int REG_COUNT = 32;

    explicit Interpreter(LoadedElf elf);

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&&) = default;
    Interpreter& operator=(Interpreter&&) = delete;

    void step();
    std::optional<uint32_t> run();
    void reset();

    uint32_t get_pc() const { return state_.pc; }
    uint32_t get_register(int idx) const;

    void raiseTrap(TrapCause cause, uint32_t value = 0) override;

    MemoryInterface& memory() { return mem_; }
    const MemoryInterface& memory() const { return mem_; }

private:
    uint32_t entry_;
    uint32_t heap_break_;

    CPUstate state_;
    PhysicalMemory mem_;

    void execute(const DecodedInstruction& d);
    uint32_t fetch_instruction(uint32_t vaddr) const;
    void handle_trap();
};
