#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/component.hpp"
#include "risc-e/component/registry.hpp"
#include "risc-e/component/run_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

} // namespace

int main() {
    // Types are declared at the type level, in registration order.
    const std::vector<std::string_view> types = component_types();
    expect(types.size() == 4 && types[0] == "predictor" && types[1] == "pipeline" &&
               types[2] == "icache" && types[3] == "cache",
           "types register in declaration order");

    // Registry: construction and unknown-name rejection.
    expect(make_component("two-bit") != nullptr, "two-bit registers");
    expect(make_component("pipeline") != nullptr, "pipeline registers");
    expect(make_component("nope") == nullptr, "unknown name rejected");

    // Types group components for comparison.
    expect(make_component("two-bit")->type() == "predictor", "two-bit is a predictor");
    expect(make_component("pipeline")->type() == "pipeline", "pipeline is a pipeline");

    const std::vector<std::string_view> preds = component_names("predictor");
    expect(std::find(preds.begin(), preds.end(), "two-bit") != preds.end(),
           "predictor names list two-bit");
    expect(component_names("pipeline").size() == 1, "one pipeline registered");

    // Pipeline config through the generic --param path.
    auto p = make_component("pipeline");
    std::string error;
    expect(p->set_parameter("stages", "10", error), "pipeline stages accepted");
    expect(static_cast<PipelineModel*>(p.get())->stages == 10, "stages applied");
    expect(p->set_parameter("stall-penalty", "4", error), "penalty accepted");
    expect(!p->set_parameter("stages", "0", error), "stages >= 1 enforced");
    expect(!p->set_parameter("bogus", "1", error), "unknown parameter rejected");

    // The canonical cost hook defaults to "no answer" without a run.
    RunContext empty_ctx;
    expect(!make_component("two-bit")->cycle_cost(empty_ctx).has_value(),
           "predictor cost needs a run");
    expect(!make_component("pipeline")->cycle_cost(empty_ctx).has_value(),
           "pipeline cost needs a run");

    std::printf("harness test: all passed\n");
    return 0;
}
