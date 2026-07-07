#pragma once

#include "src/ir/operand.hpp"
#include "src/ir/traits.hpp"

#include <string>
#include <vector>
#include <optional>
#include <array>
#include <set>

// class BasicBlock;  // forward declaration

class Operation {
public:
    virtual ~Operation() = default;

    virtual int getNumResults() const = 0;   // 0 or 1 in your current design
    virtual int getNumOperands() const = 0;  // number of sources

    std::optional<Operand> dest;      // result operand (optional)
    std::vector<Operand> srcs;        // source operands

    uint32_t addr; // original PC, kept for debugging
    uint32_t raw; // original encoding
    Traits traits; // instruction traits (control flow, memory access, etc.)
    // BasicBlock*       parent = nullptr;
protected:
    Operation(Traits t)
        : traits(t) {}
};

template <typename Derived, int NumResults_, int NumOperands_, Traits Traits_>
class OpBase : public Operation {
public:
    static constexpr int kNumResults = NumResults_;
    static constexpr int kNumOperands = NumOperands_;
    static constexpr Traits kTraits = Traits_;

    OpBase() : Operation(kTraits) {}

    int getNumResults() const final { return kNumResults; }
    int getNumOperands() const final { return kNumOperands; }

    static bool classof(const Operation* op) {
        return op->traits == kTraits &&
               op->getNumResults() == kNumResults &&
               op->getNumOperands() == kNumOperands;
    }
};