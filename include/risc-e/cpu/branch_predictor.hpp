#pragma once

#include "risc-e/decoder/decoded_instruction.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// RISC-V opcodes the predictor is consulted for.
inline constexpr uint8_t kOpcodeBranch = 0b1100011;  // conditional branch
inline constexpr uint8_t kOpcodeJal    = 0b1101111;  // unconditional direct jump
inline constexpr uint8_t kOpcodeJalr   = 0b1100111;  // unconditional indirect jump

// Conventional link registers for call/return (ra and t0).
inline constexpr uint8_t kLinkRegRa = 1;
inline constexpr uint8_t kLinkRegT0 = 5;

// Everything a predictor may need to know about a control-flow instruction.
struct BranchContext {
    uint32_t pc     = 0;
    uint8_t  opcode = 0;
    uint8_t  funct3 = 0;
    uint8_t  rd     = 0;
    uint8_t  rs1    = 0;
    uint8_t  rs2    = 0;
    int32_t  imm    = 0;

    static BranchContext from_decoded(const DecodedInstruction& d);

    bool is_conditional_branch() const { return opcode == kOpcodeBranch; }
    bool is_jal() const                { return opcode == kOpcodeJal; }
    bool is_jalr() const               { return opcode == kOpcodeJalr; }
    bool is_call() const {
        return (is_jal() || is_jalr()) && (rd == kLinkRegRa || rd == kLinkRegT0);
    }
    bool is_return() const {
        return is_jalr() && rd == 0 && imm == 0 && (rs1 == kLinkRegRa || rs1 == kLinkRegT0);
    }

    uint32_t fallthrough_pc() const { return pc + 4; }

    // Present for conditional branches and JAL (target encoded in the instruction).
    std::optional<uint32_t> direct_target() const {
        if (is_jal() || is_conditional_branch()) {
            return pc + static_cast<uint32_t>(imm);
        }
        return std::nullopt;
    }
};

// The predictor's answer at fetch time. next_pc == nullopt means "fall through".
struct Prediction {
    std::optional<uint32_t> next_pc;
};

// The actual outcome, reported back once the instruction resolves.
struct Resolution {
    bool taken = false;
    uint32_t next_pc = 0;
};

// Abstract contract for any branch predictor. A predictor may own arbitrary
// components (BTB, RAS, direction tables, ...) as private members.
class BranchPredictor {
public:
    virtual ~BranchPredictor() = default;

    // Predict the next PC for a control-flow instruction.
    virtual Prediction predict(const BranchContext& ctx) const = 0;

    // Feed the actual outcome back so the predictor can learn.
    virtual void resolve(const BranchContext& ctx, const Resolution& res) = 0;

    virtual const char* name() const = 0;

    // Optional: clear all learned state.
    virtual void reset() {}
};

// Classic 2-bit saturating counter predictor indexed by a PC hash.
class TwoBitSaturatingPredictor : public BranchPredictor {
public:
    static constexpr std::size_t kDefaultTableSize = 1024;

    // table_size must be a power of two (the index uses a mask, not a division).
    explicit TwoBitSaturatingPredictor(std::size_t table_size = kDefaultTableSize);

    Prediction predict(const BranchContext& ctx) const override;
    void resolve(const BranchContext& ctx, const Resolution& res) override;
    const char* name() const override { return "2-bit saturating"; }

private:
    std::vector<std::uint8_t> counters_;  // 0..3, >= 2 means "taken"
    std::size_t index(uint32_t pc) const;
};

// Trivial baseline predictor: conditional branches are predicted not taken.
class AlwaysNotTakenPredictor : public BranchPredictor {
public:
    Prediction predict(const BranchContext& ctx) const override {
        // Direct jumps are unconditional; predict their encoded target.
        if (ctx.is_jal()) {
            return {ctx.direct_target()};
        }
        // Everything else falls through (indirect jumps are predicted wrong).
        return {ctx.fallthrough_pc()};
    }
    void resolve(const BranchContext& ctx, const Resolution& res) override {
        (void)ctx;
        (void)res;
    }
    const char* name() const override { return "always not-taken"; }
};
