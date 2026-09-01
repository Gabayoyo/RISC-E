#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/predictor/implementations/always_not_taken.hpp"
#include "risc-e/component/predictor/implementations/gshare.hpp"
#include "risc-e/component/predictor/implementations/ras.hpp"
#include "risc-e/component/predictor/implementations/tournament.hpp"
#include "risc-e/component/predictor/implementations/two_bit_saturating.hpp"
#include "risc-e/component/predictor/return_address_stack.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/component/registry.hpp"
#include "risc-e/component/run_context.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Countdown loop + direct call + indirect return, hand-encoded for RV32I.
//   0x10000: addi t0, x0, 5        (li t0, 5)
//   0x10004: addi t0, t0, -1
//   0x10008: bne  t0, x0, 0x10004  (loop)
//   0x1000C: jal  ra, 0x10018      (call twice)
//   0x10010: addi a7, x0, 93       (li a7, 93)
//   0x10014: ecall                 (exit with a0)
//   0x10018: addi a0, x0, 7        (twice: li a0, 7)
//   0x1001C: jalr x0, 0(ra)        (return)
constexpr uint32_t kInst[] = {
    0x00500293, 0xFFF28293, 0xFE029EE3, 0x00C000EF,
    0x05D00893, 0x00000073, 0x00700513, 0x00008067,
};

LoadedElf build_program() {
    LoadedElf elf;
    elf.entry     = 0x10000;
    elf.end_vaddr = 0x10020;

    LoadedSegment seg;
    seg.vaddr = 0x10000;
    seg.size  = sizeof(kInst);
    seg.data.assign(reinterpret_cast<const uint8_t*>(kInst),
                    reinterpret_cast<const uint8_t*>(kInst) + sizeof(kInst));
    elf.segments.push_back(std::move(seg));
    return elf;
}

std::pair<BranchStats, std::optional<uint32_t>> run_with(BranchPredictor& predictor) {
    Interpreter interp(build_program());
    interp.set_predictor(&predictor);
    const std::optional<uint32_t> exit_code = interp.run();
    return {interp.branch_stats(), exit_code};
}

// With a return-address stack the indirect return is predicted correctly;
// without one (ras_depth 0) it falls through and misses.
void expect_ras_behavior(const BranchStats& stats, bool ras) {
    expect(stats.total == 5, "5 conditional branches executed");
    expect(stats.taken == 4 && stats.not_taken == 1, "loop: 4 taken, 1 not taken");
    expect(stats.control_total == 7, "7 control transfers (5 branches + JAL + JALR)");
    expect(stats.cond_hits == 4 && stats.cond_misses == 1, "conditional: 4 hits, 1 miss");
    if (ras) {
        expect(stats.hits == 6 && stats.misses == 1, "RAS: the JALR return becomes a hit");
        expect(stats.indirect_hits == 1 && stats.indirect_misses == 0, "RAS: indirect 1 hit");
    } else {
        expect(stats.hits == 5 && stats.misses == 2, "no RAS: JALR predicts fall-through");
        expect(stats.indirect_hits == 0 && stats.indirect_misses == 1, "no RAS: indirect miss");
    }
}

void test_two_bit_saturating() {
    TwoBitSaturatingPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect_ras_behavior(stats, true);
}

void test_always_not_taken() {
    AlwaysNotTakenPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect(stats.hits == 2 && stats.misses == 5, "always-not-taken: 2 hits, 5 misses");
    expect(stats.cond_hits == 1 && stats.cond_misses == 4, "always-not-taken conditional: 1 hit, 4 misses");
    expect(stats.indirect_hits == 0 && stats.indirect_misses == 1, "always-not-taken JALR: 0 hits, 1 miss");
}

void test_gshare() {
    GsharePredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect_ras_behavior(stats, true);
}

void test_tournament() {
    TournamentPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect_ras_behavior(stats, true);
}

void test_ras() {
    RasPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect(stats.hits == 3 && stats.misses == 4, "ras: only JAL + final not-taken + return hit");
    expect(stats.cond_hits == 1 && stats.cond_misses == 4, "ras conditional: not-taken only");
    expect(stats.indirect_hits == 1 && stats.indirect_misses == 0, "ras JALR: return hit");
}

void test_parameterized() {
    // Without a RAS the indirect return misses again.
    GsharePredictor gshare(4, 0);
    const auto [gs, gs_code] = run_with(gshare);
    expect(gs_code.has_value() && *gs_code == 7, "parameterized gshare should exit with code 7");
    expect_ras_behavior(gs, false);

    TwoBitSaturatingPredictor two_bit(16, 0);
    const auto [tb, tb_code] = run_with(two_bit);
    expect(tb_code.has_value() && *tb_code == 7, "parameterized two-bit should exit with code 7");
    expect_ras_behavior(tb, false);

    // Table sizes that break the mask-based index are rejected.
    bool threw = false;
    try {
        TwoBitSaturatingPredictor bad(1000);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "non-power-of-two table size is rejected");
}

void test_return_address_stack() {
    ReturnAddressStack ras(2);
    ras.push(0x10);
    ras.push(0x20);
    ras.push(0x30);  // overflow: the oldest entry is dropped
    expect(ras.peek().value_or(0) == 0x30, "RAS peek returns the top of stack");
    expect(ras.pop().value_or(0) == 0x30, "RAS pop returns and removes the top");
    expect(ras.peek().value_or(0) == 0x20, "RAS overflow dropped the oldest entry");

    ras.reset();
    expect(ras.peek() == std::nullopt, "RAS reset empties the stack");
    expect(ras.pop() == std::nullopt, "RAS pop on an empty stack yields nothing");

    ReturnAddressStack disabled(0);
    disabled.push(0x40);
    expect(disabled.peek() == std::nullopt, "RAS depth 0 disables the stack");
}

void test_factory() {
    expect(make_component("two-bit") != nullptr, "two-bit is a valid predictor");
    expect(make_component("always-not-taken") != nullptr, "always-not-taken is a valid predictor");
    expect(make_component("gshare") != nullptr, "gshare is a valid predictor");
    expect(make_component("tournament") != nullptr, "tournament is a valid predictor");
    expect(make_component("ras") != nullptr, "ras is a valid predictor");
    expect(make_component("bogus") == nullptr, "unknown predictor names are rejected");

    const std::vector<std::string_view> names = component_names("predictor");
    expect(names.size() == 5, "five predictors registered");
    for (std::string_view name : names) {
        expect(make_component(name) != nullptr, "every registered name constructs a predictor");
    }
}

void test_parameters() {
    auto comp = make_component("gshare");
    auto predictor = dynamic_cast<BranchPredictor*>(comp.get());
    expect(predictor != nullptr, "gshare is a branch predictor");

    std::string error;
    expect(predictor->set_parameter("history-bits", "6", error), "history-bits accepted");
    expect(predictor->set_parameter("ras-depth", "0", error), "ras-depth accepted");
    expect(!predictor->set_parameter("bogus", "1", error), "unknown parameter rejected");
    expect(!predictor->set_parameter("history-bits", "99", error), "out-of-range value rejected");
    expect(!predictor->set_parameter("history-bits", "-1", error), "negative value rejected");
    expect(!predictor->set_parameter("history-bits", "abc", error), "non-integer value rejected");

    const auto [stats, exit_code] = run_with(*predictor);
    expect(exit_code.has_value() && *exit_code == 7, "tuned gshare runs");
    expect_ras_behavior(stats, false);  // ras-depth 0: the JALR return misses again

    const std::vector<ParamSpec> params = predictor->parameters();
    expect(params.size() == 2, "gshare exposes two parameters");
    expect(params[0].name == "history-bits" && params[1].name == "ras-depth",
           "gshare parameter names");

    // two-bit validates power-of-two table sizes through the same interface.
    TwoBitSaturatingPredictor two_bit;
    expect(!two_bit.set_parameter("table-size", "1000", error),
           "non-power-of-two table-size rejected");
    expect(two_bit.set_parameter("table-size", "16", error), "power-of-two table-size accepted");

    // ras-depth 0 disables the RAS: the return is no longer predicted.
    RasPredictor ras;
    expect(ras.set_parameter("ras-depth", "0", error), "ras accepts ras-depth");
    const auto [rs, rs_code] = run_with(ras);
    expect(rs_code.has_value() && *rs_code == 7, "ras with depth 0 runs");
    expect(rs.hits == 2 && rs.misses == 5, "ras with depth 0: the return misses");
}

void test_cycle_cost() {
    TwoBitSaturatingPredictor predictor;
    Interpreter interp(build_program());
    interp.set_predictor(&predictor);
    interp.set_branch_trace(true);
    (void)interp.run();

    RunContext ctx;
    ctx.instruction_count = interp.instruction_count();
    ctx.branch_stats = &interp.branch_stats();
    PipelineModel pipeline;
    ctx.pipeline = &pipeline;

    const std::optional<CycleCost> cc = predictor.cycle_cost(ctx);
    expect(cc.has_value(), "two-bit provides a cost answer");
    expect(cc->total_cycles == 18 && cc->baseline_cycles == 26,
           "two-bit: 18 cycles vs 26 with no prediction");
    expect(cc->baseline_name == "no prediction", "cost baseline named");
}

} // namespace

int main() {
    test_two_bit_saturating();
    test_always_not_taken();
    test_gshare();
    test_tournament();
    test_ras();
    test_parameterized();
    test_return_address_stack();
    test_factory();
    test_parameters();
    test_cycle_cost();

    std::printf("branch prediction tests passed\n");
    return 0;
}
