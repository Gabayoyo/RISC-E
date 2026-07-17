#pragma once

#include "src/decoder/decoded_instruction.hpp"

#include <ostream>
#include <string>
#include <vector>

enum class BranchType { None, Direct, Indirect, Call, Return };

class BasicBlock {
public:
    std::string              label;       // e.g. "bb_0x10034"
    uint32_t                 startAddr;
    uint32_t                 endAddr;     // addr of last instruction

    std::vector<DecodedInstruction> instructions;

    uint32_t   fallthrough_pc; // 0 if unconditional jump

    uint32_t   branch_target_pc; // 0 if indirect or unknown
    BranchType exit_type;

    uint64_t exec_count;         // profiling counter

    void*      native_entry;     // pointer to compiled code
    bool       is_compiled;
};