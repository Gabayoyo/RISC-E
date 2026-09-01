#pragma once

#include "risc-e/component/predictor/return_address_stack.hpp"
#include "risc-e/decoder/decoded_instruction.hpp"
#include "risc-e/decoder/opcodes.hpp"
#include "risc-e/component/component.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// RISC-V opcodes the predictor is consulted for (defined in decoder/opcodes.hpp).
inline constexpr uint8_t kOpcodeBranch = opcode::kBranch;  // conditional branch
inline constexpr uint8_t kOpcodeJal    = opcode::kJal;     // unconditional direct jump
inline constexpr uint8_t kOpcodeJalr   = opcode::kJalr;    // unconditional indirect jump

// Conventional link registers for call/return (ra and t0).
inline constexpr uint8_t kLinkRegRa = 1;
inline constexpr uint8_t kLinkRegT0 = 5;

// Everything a predictor may need to know about a control-flow instruction.
struct BranchContext {
    uint32_t pc     = 0;
    uint32_t raw    = 0;  // raw instruction word
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

// Abstract contract for any branch predictor, and a Component:  plug
// into the shared harness (tunables, report section, comparison) while keeping
// their own components (BTB, RAS, direction tables, ...) private.
class BranchPredictor : public Component {
public:
    // Predict the next PC for a control-flow instruction.
    virtual Prediction predict(const BranchContext& ctx) const = 0;

    // Feed the actual outcome back so the predictor can learn.
    virtual void resolve(const BranchContext& ctx, const Resolution& res) = 0;

    std::string_view name() const override = 0;

    std::string_view type() const override { return "predictor"; }

    // Optional: clear all learned state.
    void reset() override {}

    // Report section: predictor name, hit/miss rates and counts.
    std::string_view report_title() const override;
    void report(std::ostream& out, const RunContext& ctx) const override;

    // Cost answer: cycles under the active pipeline, vs no prediction.
    std::optional<CycleCost> cycle_cost(const RunContext& ctx) override;
};

// Shared 2-bit saturating counter updates (counters range 0..3, >= 2 = taken).
inline std::uint8_t saturating_increment(std::uint8_t c) { return c < 3 ? c + 1 : 3; }
inline std::uint8_t saturating_decrement(std::uint8_t c) { return c > 0 ? c - 1 : 0; }

// True when a 2-bit counter means "taken".
inline bool counter_is_taken(std::uint8_t c) { return c >= 2; }

// Prediction for a control-flow instruction given its resolved direction.
// Taken yields the encoded target when the instruction has one; for JALR
// (no direct target) this predicts fall-through, which is the agreed
// behavior without a BTB. Not taken always means fall-through.
inline Prediction predict_taken_or_fallthrough(const BranchContext& ctx, bool taken) {
    if (taken) return Prediction{ctx.direct_target()};
    return Prediction{ctx.fallthrough_pc()};
}

// Predicts the non-direct control transfers (callers handle JAL and
// conditional branches first): a return predicts the top of the return-address
// stack, and everything else falls through — no target source without a BTB.
inline Prediction predict_call_return(const BranchContext& ctx, const ReturnAddressStack& ras) {
    if (ctx.is_return()) {
        if (const auto target = ras.peek()) return Prediction{*target};
    }
    return {ctx.fallthrough_pc()};
}

// Learns from a resolved call or return: a call pushes its return address; a
// return pops the predicted target and re-pushes the actual target when the
// prediction missed, so the stack stays aligned.
inline void resolve_call_return(const BranchContext& ctx, const Resolution& res,
                                ReturnAddressStack& ras) {
    if (ctx.is_call()) {
        ras.push(ctx.pc + 4);
    } else if (ctx.is_return()) {
        const auto predicted = ras.pop();
        if (predicted && *predicted != res.next_pc) ras.push(res.next_pc);
    }
}

// Shared "ras-depth" tunable (0 disables the stack), for the parameter list.
inline ParamSpec ras_depth_parameter(const ReturnAddressStack& ras) {
    return {"ras-depth", "return-address stack depth (0 disables)", 0,
            static_cast<long>(ReturnAddressStack::kMaxDepth), std::to_string(ras.depth())};
}

// Applies a "ras-depth" override (0 disables the stack). False with `error`
// set when the value is out of range.
inline bool set_ras_depth_parameter(std::string_view value, ReturnAddressStack& ras,
                                    std::string& error) {
    const auto parsed = parse_parameter_value(value, error);
    if (!parsed) return false;
    if (*parsed > static_cast<long>(ReturnAddressStack::kMaxDepth)) {
        error = "ras-depth must be in [0, " + std::to_string(ReturnAddressStack::kMaxDepth) + "]";
        return false;
    }
    ras.resize(static_cast<std::size_t>(*parsed));
    return true;
}
