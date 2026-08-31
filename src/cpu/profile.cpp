#include "risc-e/cpu/profile.hpp"

void ProfileStats::reset() {
    instructions = 0;
    seen_pcs.clear();
    pc_to_id.clear();
    blocks.clear();
    entry_sequence.clear();
}

uint32_t ProfileStats::record_block_entry(uint32_t entry_pc) {
    auto it = pc_to_id.find(entry_pc);
    uint32_t id;
    if (it == pc_to_id.end()) {
        id = static_cast<uint32_t>(blocks.size());
        pc_to_id.emplace(entry_pc, id);
        blocks.push_back(BlockInfo{entry_pc, 0, 0});
    } else {
        id = it->second;
    }
    ++blocks[id].executions;
    if (entry_sequence.size() < kMaxEntries) entry_sequence.push_back(id);
    return id;
}

void ProfileStats::record_instruction(uint32_t pc) {
    ++instructions;
    seen_pcs.insert(pc);
}

void ProfileStats::record_block_instruction(uint32_t block_id) {
    if (block_id != kNoBlock) ++blocks[block_id].instructions;
}
