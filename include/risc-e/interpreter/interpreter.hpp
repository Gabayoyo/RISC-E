#pragma once

#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/icache/icache_stats.hpp"
#include "risc-e/cpu/state.hpp"
#include "risc-e/cpu/trap.hpp"
#include "risc-e/decoder/decoded_instruction.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/component/dcache/dcache_stats.hpp"
#include "risc-e/memory/physical_memory.hpp"

#include <cstdint>
#include <optional>
#include <string>

class Interpreter : public TrapSink {
public:
    static constexpr int REG_COUNT = 32;

    explicit Interpreter(LoadedElf elf, BranchPredictor* predictor = nullptr);

    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;
    Interpreter(Interpreter&& other) noexcept;
    Interpreter& operator=(Interpreter&&) = delete;

    void step();
    std::optional<uint32_t> run();
    void reset();

    uint32_t get_pc() const { return state_.pc; }
    uint32_t get_register(int idx) const;
    HaltReason halt_reason() const { return state_.halt_reason; }
    uint64_t instruction_count() const { return inst_count_; }

    const BranchStats& branch_stats() const { return branch_stats_; }
    BranchStats& branch_stats() { return branch_stats_; }
    void set_predictor(BranchPredictor* predictor) { predictor_ = predictor; }
    void set_branch_trace(bool enabled) { branch_stats_.trace_enabled = enabled; }
    void reset_branch_stats() { branch_stats_.reset(); }

    const ICacheStats& profile_stats() const { return profile_stats_; }
    ICacheStats& profile_stats() { return profile_stats_; }
    void reset_profile_stats();

    const DCacheStats& access_trace() const { return access_trace_; }
    DCacheStats& access_trace() { return access_trace_; }

    // Everything the program wrote to stdout/stderr via the write syscall,
    // in order; empty when the program printed nothing.
    const std::string& program_output() const { return program_output_; }

    void raise_trap(TrapCause cause, uint32_t value = 0) override;

    MemoryInterface& memory() { return mem_; }
    const MemoryInterface& memory() const { return mem_; }

private:
    uint32_t entry_;
    uint32_t heap_break_;
    uint64_t inst_count_ = 0;

    BranchPredictor* predictor_ = nullptr;
    BranchStats branch_stats_;
    ICacheStats profile_stats_;
    DCacheStats access_trace_;
    std::string program_output_;

    // Basic-block identification state: whether the next instruction starts a
    // new block (set when the previous instruction was a control transfer),
    // and the interned id of the block currently being executed.
    bool block_entering_ = true;
    uint32_t current_block_id_ = ICacheStats::kNoBlock;

    CPUState state_;
    PhysicalMemory mem_;

    void execute(const DecodedInstruction& d);
    uint32_t fetch_instruction(uint32_t vaddr) const;
    void handle_trap();
};
