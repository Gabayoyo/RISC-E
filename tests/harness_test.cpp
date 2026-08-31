#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/pipeline.hpp"
#include "risc-e/harness/component.hpp"
#include "risc-e/harness/registry.hpp"

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
    expect(types.size() == 2 && types[0] == "predictor" && types[1] == "pipeline",
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
    expect(p->set_parameter("mispredict-penalty", "4", error), "penalty accepted");
    expect(!p->set_parameter("stages", "0", error), "stages >= 1 enforced");
    expect(!p->set_parameter("bogus", "1", error), "unknown parameter rejected");

    // Metric formatting is centralized in the harness.
    expect(format_metric(Metric{"hits", uint64_t{6}, uint64_t{7}, ""}) == "6/7",
           "ratio metric formats as n/m");
    expect(format_metric(Metric{"hit rate", 85.71, std::nullopt, "%"}) == "85.71%",
           "measurement formats with unit");
    expect(format_metric(Metric{"cycles", uint64_t{18}, std::nullopt, ""}) == "18",
           "count metric formats plain");

    std::printf("harness test: all passed\n");
    return 0;
}
