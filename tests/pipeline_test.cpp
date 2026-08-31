#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/run_context.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void test_penalty_derivation() {
    PipelineModel m;
    expect(m.stages == 5, "default is a 5-stage pipeline");
    expect(m.penalty_cycles() == 2, "5-stage derives a 2-cycle penalty");

    PipelineModel single;
    single.stages = 1;
    expect(single.penalty_cycles() == 0, "single-cycle machine has no penalty");

    PipelineModel deep;
    deep.stages = 10;
    expect(deep.penalty_cycles() == 7, "10-stage derives a 7-cycle penalty");

    PipelineModel explicit_penalty;
    explicit_penalty.stall_penalty = 4;
    expect(explicit_penalty.penalty_cycles() == 4,
           "explicit penalty wins over the derived default");
}

void test_compute() {
    PipelineModel m;
    const PipelineStats s = compute_pipeline_stats(100, 10, m);  // 10 stall events x 2 cycles
    expect(s.instructions == 100 && s.ideal_cycles == 100, "instructions equal ideal cycles");
    expect(s.penalty_cycles == 20 && s.total_cycles == 120, "penalty added to total");
    expect(std::abs(s.cpi - 1.2) < 1e-9, "CPI is total / instructions");
    expect(std::abs(s.slowdown_pct - 20.0) < 1e-9, "slowdown is penalty / ideal");

    const PipelineStats zero = compute_pipeline_stats(0, 5, m);
    expect(zero.cpi == 0.0 && zero.slowdown_pct == 0.0,
           "no instructions yields no ratios (no division by zero)");
}

void test_description() {
    PipelineModel m;
    expect(m.description() == "5-stage pipeline (2-cycle stall penalty)",
           "description reflects the stage count and penalty");

    PipelineModel single;
    single.stages = 1;
    expect(single.description() == "1-stage pipeline (0-cycle stall penalty)",
           "single-cycle description");
}

void test_cycle_cost() {
    PipelineModel m;
    BranchStats s;
    s.misses = 10;
    RunContext ctx;
    ctx.instruction_count = 100;
    ctx.branch_stats = &s;

    const std::optional<CycleCost> cc = m.cycle_cost(ctx);
    expect(cc.has_value(), "pipeline provides a cost answer");
    expect(cc->total_cycles == 120 && cc->baseline_cycles == 100,
           "pipeline cost vs stall-free (ideal)");
    expect(cc->baseline_name == "stall-free", "cost baseline named");

    // No run data -> no answer, not a crash.
    RunContext empty;
    expect(!m.cycle_cost(empty).has_value(), "pipeline cost needs a run");
}

} // namespace

int main() {
    test_penalty_derivation();
    test_compute();
    test_description();
    test_cycle_cost();

    std::printf("pipeline tests passed\n");
    return 0;
}
