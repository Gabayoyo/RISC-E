#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// One tunable a component exposes to the CLI. Bounds are inclusive; a bound
// of 0 means unbounded on that end.
struct ParamSpec {
    std::string name;            // CLI key, e.g. "history-bits"
    std::string help;            // one-line description for --list
    long min = 0;
    long max = 0;
    std::string default_value;   // current value, for display
};

// One CLI-provided override: "<component>.<parameter>=<value>" (see main.cpp).
struct ParamOverride {
    std::string component;
    std::string name;
    std::string value;
};

// Parses a non-negative integer parameter value. Returns nullopt and fills
// `error` when the value is not a non-negative integer.
std::optional<long> parse_parameter_value(std::string_view value, std::string& error);

// One component's cost answer over the recorded run, in cycles. total_cycles
// is the run's cost under this component; baseline_cycles is the cost of a
// reference design named by baseline_name ("no prediction", "no instruction
// cache", ...). Every comparison is the same three columns derived from it:
// cycles before (baseline), cycles after (this design), speedup
// (baseline / total).
struct CycleCost {
    uint64_t total_cycles = 0;
    uint64_t baseline_cycles = 0;
    std::string_view baseline_name;
};

struct RunContext;

// Base class for every swappable, configurable unit: branch predictors,
// pipeline models, memory, ... A component plugs into the registry and gets
// CLI configuration (--param), a report section, and within-type comparison
// for free. Behaviour (predict/resolve, load/store, ...) stays in the
// derived classes.
class Component {
public:
    virtual ~Component() = default;

    // Identity.
    virtual std::string_view name() const = 0;
    virtual std::string_view type() const = 0;

    // Configuration. The CLI reads parameters() for --list and routes
    // --param overrides through set_parameter().
    virtual std::vector<ParamSpec> parameters() const { return {}; }
    virtual bool set_parameter(std::string_view name, std::string_view value,
                               std::string& error) {
        (void)value;
        error = "unknown parameter \"" + std::string(name) + "\"";
        return false;
    }

    // Clears all learned/configured state. The comparison driver calls this
    // before asking for metrics.
    virtual void reset() {}

    // Output hook: one report section for the run. A component with an empty
    // report_title() is skipped by the report loop.
    virtual std::string_view report_title() const { return {}; }
    virtual void report(std::ostream& out, const RunContext& ctx) const {
        (void)out;
        (void)ctx;
    }

    // Comparison hook, non-const: trace-replay components recompute their
    // state here; the driver resets first. The canonical cost answer for the
    // cycles-before / cycles-after / speedup columns. Components that do not
    // model time return nullopt and are skipped by the comparison. The
    // baseline is a property of the component's model, so the columns are
    // comparable within a type but never across types.
    virtual std::optional<CycleCost> cycle_cost(const RunContext&) { return std::nullopt; }
};
