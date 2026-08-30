#include "risc-e/cpu/branch_predictor.hpp"

#include "risc-e/cpu/predictors/always_not_taken.hpp"
#include "risc-e/cpu/predictors/gshare.hpp"
#include "risc-e/cpu/predictors/tournament.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"

#include <memory>
#include <string_view>
#include <vector>

BranchContext BranchContext::from_decoded(const DecodedInstruction& d) {
    BranchContext ctx;
    ctx.pc     = d.addr;
    ctx.opcode = d.opcode;
    ctx.funct3 = d.funct3;
    ctx.rd     = d.rd;
    ctx.rs1    = d.rs1;
    ctx.rs2    = d.rs2;
    ctx.imm    = d.imm;
    return ctx;
}

std::unique_ptr<BranchPredictor> make_predictor(std::string_view name) {
    if (name == TwoBitSaturatingPredictor::kName) {
        return std::make_unique<TwoBitSaturatingPredictor>();
    }
    if (name == AlwaysNotTakenPredictor::kName) {
        return std::make_unique<AlwaysNotTakenPredictor>();
    }
    if (name == GsharePredictor::kName) {
        return std::make_unique<GsharePredictor>();
    }
    if (name == TournamentPredictor::kName) {
        return std::make_unique<TournamentPredictor>();
    }
    return nullptr;
}

std::vector<std::string_view> predictor_names() {
    return {
        TwoBitSaturatingPredictor::kName,
        AlwaysNotTakenPredictor::kName,
        GsharePredictor::kName,
        TournamentPredictor::kName,
    };
}
