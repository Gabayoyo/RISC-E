#pragma once

#include "risc-e/decoder/decoded_instruction.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
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

    virtual std::string_view name() const = 0;

    // Optional: clear all learned state.
    virtual void reset() {}
};

// Builds a predictor from its CLI name (see predictor_names()).
// Returns nullptr for unknown names.
std::unique_ptr<BranchPredictor> make_predictor(std::string_view name);

// Names accepted by make_predictor(), e.g. for --predictor / --list-predictors.
std::vector<std::string_view> predictor_names();

// Shared 2-bit saturating counter updates (counters range 0..3, >= 2 = taken).
inline std::uint8_t saturating_increment(std::uint8_t c) { return c < 3 ? c + 1 : 3; }
inline std::uint8_t saturating_decrement(std::uint8_t c) { return c > 0 ? c - 1 : 0; }
