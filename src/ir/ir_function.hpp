#pragma once
 
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ostream>
 
#include "src/ir/basic_block.hpp"
#include <iostream>
 
class IRModule; // forward
 
class IRFunction {
public:
    std::string                              name;
    uint32_t                                 entryAddr;
    // Blocks stored in program order; first = entry
    std::vector<std::unique_ptr<BasicBlock>> blocks;
    IRModule*                                parent = nullptr;
 
    BasicBlock* entryBlock() { return blocks.empty() ? nullptr : blocks.front().get(); }
 
    BasicBlock* addBlock(uint32_t startAddr);
    BasicBlock* findBlock(uint32_t addr);

    void print(std::ostream& os, int indent = 0) const {
        std::string pad(indent, ' ');
        os << pad << "function " << name
        << "  @0x" << std::hex << entryAddr << std::dec
        << "  (" << blocks.size() << " blocks) {\n";

        for (const auto& bb : blocks) {
            bb->print(os, indent + 2);
        }
        os << pad << "}\n";
    }
};
 