#pragma once

#include "src/ir/operation.hpp"

#include <ostream>
#include <string>

class IRFunction; // forward declaration

class BasicBlock {
public:
    std::string              label;       // e.g. "bb_0x10034"
    uint32_t                 startAddr;
    uint32_t                 endAddr;     // addr of last instruction

    std::vector<std::shared_ptr<Operation>> instructions;

    IRFunction* parent = nullptr; // back pointer to containing function

    // CFG edges (filled in by IRBuilder after all blocks are created)
    std::vector<BasicBlock*> successors;
    std::vector<BasicBlock*> predecessors;

    Operation* terminator() {
        if (!instructions.empty() && instructions.back()->traits & Traits::Terminator)
            return instructions.back().get();
        return nullptr; // fall-through block
    }

    void addOperation(std::shared_ptr<Operation> op) {
        op->parent = this;
        instructions.push_back(op);
    }

    // Iterate instructions
    auto begin() { return instructions.begin(); }
    auto end()   { return instructions.end();   }

    void print(std::ostream& os, int indent = 0) const {
        std::string pad(indent, ' ');

        os << pad << label;

        // Predecessor / successor summary on the same header line
        os << "  preds=[";
        for (size_t i = 0; i < predecessors.size(); ++i) {
            if (i) os << ", ";
            os << predecessors[i]->label;
        }
        os << "]  succs=[";
        for (size_t i = 0; i < successors.size(); ++i) {
            if (i) os << ", ";
            os << successors[i]->label;
        }
        os << "] {\n";

        for (const auto& instr : instructions)
            instr->print(os, indent + 4);

        os << pad << "}\n";
    }
};