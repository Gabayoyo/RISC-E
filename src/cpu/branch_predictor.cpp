#include "risc-e/cpu/branch_predictor.hpp"

#include "risc-e/cpu/predictors/always_not_taken.hpp"
#include "risc-e/cpu/predictors/gshare.hpp"
#include "risc-e/cpu/predictors/ras.hpp"
#include "risc-e/cpu/predictors/tournament.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

BranchContext BranchContext::from_decoded(const DecodedInstruction& d) {
    BranchContext ctx;
    ctx.pc     = d.addr;
    ctx.raw    = d.raw;
    ctx.opcode = d.opcode;
    ctx.funct3 = d.funct3;
    ctx.rd     = d.rd;
    ctx.rs1    = d.rs1;
    ctx.rs2    = d.rs2;
    ctx.imm    = d.imm;
    return ctx;
}

std::optional<long> parse_parameter_value(std::string_view value, std::string& error) {
    if (value.empty()) {
        error = "expected an integer value";
        return std::nullopt;
    }
    const std::string text(value);
    std::size_t consumed = 0;
    long result = 0;
    try {
        result = std::stol(text, &consumed);
    } catch (const std::exception&) {
        error = "\"" + text + "\" is not an integer";
        return std::nullopt;
    }
    if (consumed != text.size()) {
        error = "\"" + text + "\" is not an integer";
        return std::nullopt;
    }
    if (result < 0) {
        error = "expected a non-negative integer";
        return std::nullopt;
    }
    return result;
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
    if (name == RasPredictor::kName) {
        return std::make_unique<RasPredictor>();
    }
    return nullptr;
}

std::vector<std::string_view> predictor_names() {
    return {
        TwoBitSaturatingPredictor::kName,
        AlwaysNotTakenPredictor::kName,
        GsharePredictor::kName,
        TournamentPredictor::kName,
        RasPredictor::kName,
    };
}
