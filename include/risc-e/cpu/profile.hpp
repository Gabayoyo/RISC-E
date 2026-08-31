#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// One identified basic block: a maximal run of straight-line code starting at
// a control-transfer target (or the program entry). Blocks are interned at
// first sight: the entry PC maps to a sequential id that later consumers (IR,
// layout) can key on without carrying addresses around. The execution and
// instruction counts feed the simulated instruction-cache models.
struct BlockInfo {
    uint32_t entry_pc = 0;
    uint64_t executions = 0;     // dynamic: times control entered the block
    uint64_t instructions = 0;   // dynamic: instructions executed inside it
};

// Cumulative execution profile of one run: the static distinct-instruction
// footprint, interned basic-block counts, and the ordered sequence of block
// entries (the input to the instruction-cache simulations). Populated by the
// interpreter as a side effect of stepping; read back through RunContext like
// BranchStats. Recording is model-free: every cache design consumes the same
// profile.
struct ProfileStats {
    static constexpr uint32_t kNoBlock = 0xFFFFFFFFu;    // no block active yet
    static constexpr std::size_t kMaxEntries = 100000;   // recorded-entry cap

    uint64_t instructions = 0;                        // decoded + executed
    std::unordered_set<uint32_t> seen_pcs;            // static instruction footprint
    std::unordered_map<uint32_t, uint32_t> pc_to_id;  // entry PC -> interned id
    std::vector<BlockInfo> blocks;                    // id == index
    std::vector<uint32_t> entry_sequence;             // block ids in entry order (capped)

    void reset();

    // Returns the interned id for an entry PC, creating the block on first
    // sight. Then records one entry execution of that block and appends the
    // id to the entry sequence.
    uint32_t record_block_entry(uint32_t entry_pc);
    // Records one executed instruction: total count, static footprint.
    void record_instruction(uint32_t pc);
    // Records one more instruction executed inside an already-active block.
    void record_block_instruction(uint32_t block_id);
};
