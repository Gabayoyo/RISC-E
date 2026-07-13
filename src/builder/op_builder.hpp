#pragma once

#include "src/ir/operation.hpp"
#include "src/decoder/decoder.hpp"

#include <memory>

class OpBuilder {
public:
    virtual ~OpBuilder() = default;

    // Primary decode, allocates and populates a concrete Operation.
    std::unique_ptr<Operation> decode(const DecodedInstruction& d) const;

    // Lightweight queries used by IRBuilder::findLeaders().
    bool     isBranchOrJump(const DecodedInstruction& d) const;
    bool     hasStaticTarget(const DecodedInstruction& d) const;  // false for JALR
    int32_t  staticOffset(const DecodedInstruction& d) const;     // PC-relative imm

private:
    // Per-opcode factories
    std::unique_ptr<Operation> tryDecodeLoad  (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeStore (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeOpImm (const DecodedInstruction& d) const;  // Immediates
    std::unique_ptr<Operation> tryDecodeOp    (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeBranch(const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeJal   (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeJalr  (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeLui   (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeAuipc (const DecodedInstruction& d) const;
    std::unique_ptr<Operation> tryDecodeSystem(const DecodedInstruction& d) const;
};