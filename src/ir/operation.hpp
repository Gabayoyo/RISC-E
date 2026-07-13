#pragma once

#include "src/ir/operand.hpp"
#include "src/ir/traits.hpp"

#include <string>
#include <vector>
#include <optional>
#include <array>
#include <set>
#include <ostream>

class BasicBlock;  // forward declaration

class Operation {
public:
    virtual ~Operation() = default;
    virtual std::string mnemonic() const = 0;

    Traits traits = Traits::None;
    uint32_t addr = 0;
    uint32_t raw = 0;

    BasicBlock* parent = nullptr;

    virtual void print(std::ostream& os, int indent = 0) const {
        os << std::string(indent, ' ')
        << "[0x" << std::hex << addr << std::dec << "] "
        << mnemonic();

        // Print relevant traits as flags
        if (traits != Traits::None) {
            os << "  <";
            if (static_cast<int>(traits) & static_cast<int>(Traits::Terminator)) os << "terminator ";
            // add other trait flags here as needed
            os << ">";
        }
        os << '\n';
    }
protected:
    Operation(Traits t = Traits::None) : traits(t) {}
};

// class Operation {
// public:
//     virtual ~Operation() = default;
// 
//     virtual int getNumResults() const = 0;
//     virtual int getNumOperands() const = 0; 
// 
//     std::optional<Operand> dest;      // optional result operand
//     std::vector<Operand> srcs;        // source operands
// 
//     uint32_t addr; // original PC, kept for debugging
//     uint32_t raw; // original encoding
//     Traits traits; // instruction traits (control flow, memory access, etc.)
//     // BasicBlock*       parent = nullptr;
// protected:
//     Operation(Traits t)
//         : traits(t) {}
// };

// template <typename Derived, int NumResults_, int NumOperands_, Traits Traits_>
// class OpBase : public Operation {
// public:
//     static constexpr int kNumResults = NumResults_;
//     static constexpr int kNumOperands = NumOperands_;
//     static constexpr Traits kTraits = Traits_;
// 
//     OpBase() : Operation(kTraits) {}
// 
//     int getNumResults() const final { return kNumResults; }
//     int getNumOperands() const final { return kNumOperands; }
// 
//     static bool classof(const Operation* op) {
//         return op->traits == kTraits &&
//                op->getNumResults() == kNumResults &&
//                op->getNumOperands() == kNumOperands;
//     }
// };