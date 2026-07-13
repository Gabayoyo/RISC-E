#pragma once

#include "src/ir/ir_module.hpp"
#include "src/decoder/decoder.hpp"
#include "src/builder/op_builder.hpp"

#include <string>
#include <iomanip>
#include <sstream>
#include <vector>

// given a flat, ordered list of already-decoded instructions, produce an IRFunction with a complete CFG.
class IRBuilder {
public:
    IRBuilder() = default;
    ~IRBuilder() = default;


    IRModule* buildModule(IRModule& module,
                           const std::vector<DecodedInstruction>& insns,
                           const std::set<uint32_t>& entryAddrs);

private:
    OpBuilder opBuilder;

    static std::string addrToFuncName(uint32_t addr) {
        std::stringstream ss;
        ss << "func_0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << addr;
        return ss.str();
    }

    IRFunction* buildFunction(IRModule& module,
                              const std::string& name,
                              const std::vector<DecodedInstruction>& insns);

    // Pass 1: collect addresses that begin a new basic block.
    std::set<uint32_t> findLeaders(const std::vector<DecodedInstruction>& insns) const;

    // Pass 2: create blocks and lift each instruction into an Operation.
    void liftInstructions(IRFunction& fn,
                          const std::set<uint32_t>& leaders,
                          const std::vector<DecodedInstruction>& insns);

    // Pass 3: wire successor / predecessor edges between basic blocks.
    void wireCFG(IRFunction& fn) const;

    // Edge helper.
    static void addEdge(BasicBlock* from, BasicBlock* to);

    // True if the terminator of `bb` has a statically-known PC-relative target.
    // Fills `target` and `fallthru` (fallthru == 0 means unconditional jump).
    static bool resolveEdges(const BasicBlock& bb,
                             uint32_t& target,
                             uint32_t& fallthru);
};