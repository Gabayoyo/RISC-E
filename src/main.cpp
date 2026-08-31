#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/pipeline.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/harness/component.hpp"
#include "risc-e/harness/registry.hpp"
#include "risc-e/harness/run_context.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include "runtime_files.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>  // access(), getpid()

namespace {

const char* kAssemblerFlags = "-march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -Wl,-Ttext=0x10000";

// Extra flags for C inputs: freestanding so no libc symbols are assumed,
// no PIE so the result stays an ET_EXEC (the loader rejects ET_DYN).
const char* kCFlags = "-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie";

bool is_source_file(const std::string& path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    return ext == ".S" || ext == ".s" || ext == ".c";
}

bool is_c_source(const std::string& path) {
    return std::filesystem::path(path).extension().string() == ".c";
}

std::string find_cross_compiler() {
    if (const char* env = std::getenv("RISCV_GCC"); env != nullptr && *env != '\0') {
        return env;
    }
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) return "";

    const std::string path(path_env);
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string dir = path.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        const std::string dir_path = dir.empty() ? "." : dir;
        for (const char* name : {"riscv64-unknown-elf-gcc", "riscv64-elf-gcc"}) {
            const std::string candidate = dir_path + "/" + name;
            if (access(candidate.c_str(), X_OK) == 0) {
                return candidate;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

struct TempFileGuard {
    std::vector<std::string> paths;
    ~TempFileGuard() {
        std::error_code ec;
        for (const std::string& path : paths) {
            std::filesystem::remove_all(path, ec);
        }
    }
    void add(std::string path) { paths.push_back(std::move(path)); }
};

// Compiles a RISC-V C or assembly source to an ELF in the system temp
// directory and returns its path. The caller owns the temp files via the
// guard. Compiler diagnostics pass through to stderr; on failure the temp
// files are removed and an exception is thrown.
std::string compile_source(const std::string& source_path, TempFileGuard& temp_files) {
    const std::string compiler = find_cross_compiler();
    if (compiler.empty()) {
        throw std::runtime_error(
            "input is a source file (" + source_path +
            ") but no RISC-V cross-compiler was found on PATH; install "
            "riscv64-elf-gcc or set RISCV_GCC");
    }

    const std::string out_path = (std::filesystem::temp_directory_path() /
                                  ("risc-e-" + std::to_string(getpid()) + ".elf"))
                                     .string();
    temp_files.add(out_path);
    std::string command = compiler + " " + kAssemblerFlags;

    if (is_c_source(source_path)) {
        // C programs link against a bundled freestanding runtime: a crt0 that
        // calls main() and exits with its return value, plus risc-e.h with
        // write/exit/brk syscall wrappers. Both are written to a per-process
        // temp directory that is passed to the compiler with -I.
        const std::string runtime_dir = (std::filesystem::temp_directory_path() /
                                         ("risc-e-runtime-" + std::to_string(getpid())))
                                            .string();
        const std::string crt0_path = runtime_dir + "/crt0.S";
        const std::string header_path = runtime_dir + "/risc-e.h";
        std::filesystem::create_directories(runtime_dir);
        temp_files.add(runtime_dir);
        {
            std::ofstream crt0(crt0_path);
            crt0 << kCrt0Source;
            std::ofstream header(header_path);
            header << kRuntimeHeader;
            if (!crt0 || !header) {
                throw std::runtime_error("could not write runtime files for " + source_path);
            }
        }
        // -lgcc must come after the input files: with -nostdlib the compiler
        // driver does not link libgcc, and div/mod helpers (__divsi3, ...)
        // are only pulled in when the archive is seen after the objects.
        command += " " + std::string(kCFlags) + " -I\"" + runtime_dir + "\" -o \"" + out_path +
                   "\" \"" + crt0_path + "\" \"" + source_path + "\" -lgcc";
    } else {
        command += " -o \"" + out_path + "\" \"" + source_path + "\"";
    }

    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("compilation failed for " + source_path);
    }
    return out_path;
}

const char* halt_reason_name(HaltReason reason) {
    switch (reason) {
        case HaltReason::ECALL:  return "ECALL";
        case HaltReason::EBREAK: return "EBREAK";
        case HaltReason::TRAP:   return "trap";
        default:                 return "unknown";
    }
}

// Applies every override that targets `component` to it. Overrides are
// validated in main before the run, so this cannot fail here.
void apply_overrides(Component& component, const std::vector<ParamOverride>& overrides) {
    for (const ParamOverride& o : overrides) {
        if (o.component != component.name()) continue;
        std::string error;
        (void)component.set_parameter(o.name, o.value, error);
    }
}

// Checks that every override targets a real component, that it matches a
// component active in the run (outside comparison mode), and that the target
// accepts the parameter and value. Returns false after reporting the first
// problem.
bool validate_overrides(const std::vector<ParamOverride>& overrides, bool comparison_mode,
                        const std::string& predictor_name) {
    for (const ParamOverride& o : overrides) {
        auto comp = make_component(o.component);
        if (comp == nullptr) {
            std::cerr << "RISC-E error: --param targets unknown component \"" << o.component
                      << "\"\n";
            return false;
        }
        if (!comparison_mode && o.component != predictor_name &&
            o.component != PipelineModel::kName) {
            std::cerr << "RISC-E error: --param \"" << o.component << "." << o.name
                      << "\" targets \"" << o.component << "\", which is not active in this run"
                      << " (--predictor selected \"" << predictor_name << "\")\n";
            return false;
        }
        std::string error;
        if (!comp->set_parameter(o.name, o.value, error)) {
            std::cerr << "RISC-E error: component \"" << o.component << "\": " << error
                      << "; valid parameters:";
            for (const ParamSpec& p : comp->parameters()) {
                std::cerr << " " << p.name;
            }
            std::cerr << '\n';
            return false;
        }
    }
    return true;
}

void print_type_rows(std::string_view type) {
    for (const std::string_view name : component_names(type)) {
        auto comp = make_component(name);
        std::cout << "  " << name;
        const std::vector<ParamSpec> params = comp->parameters();
        if (!params.empty()) {
            std::cout << " [";
            for (std::size_t i = 0; i < params.size(); ++i) {
                if (i != 0) std::cout << ", ";
                std::cout << params[i].name << "=" << params[i].default_value;
            }
            std::cout << "]";
        }
        std::cout << '\n';
    }
}

// Prints a component list. Empty detail: all components grouped by type.
// Otherwise: the detail view of one component, or the grouped list of one
// type. Returns false for an unknown name/type.
bool print_component_list(const std::string& detail) {
    if (detail.empty()) {
        for (const std::string_view type : component_types()) {
            std::cout << type << ":\n";
            print_type_rows(type);
        }
        return true;
    }

    if (make_component(detail) != nullptr) {
        auto comp = make_component(detail);
        std::cout << detail << ":\n";
        const std::vector<ParamSpec> params = comp->parameters();
        if (params.empty()) {
            std::cout << "  (no parameters)\n";
            return true;
        }
        for (const ParamSpec& p : params) {
            std::cout << "  " << p.name << "  default " << p.default_value;
            if (p.min != 0 || p.max != 0) {
                std::cout << "  range [" << p.min;
                if (p.max != 0) std::cout << ", " << p.max;
                else            std::cout << ", ...";
                std::cout << "]";
            }
            std::cout << "  " << p.help << '\n';
        }
        return true;
    }

    if (!component_names(detail).empty()) {
        std::cout << detail << ":\n";
        print_type_rows(detail);
        return true;
    }

    std::cerr << "RISC-E error: unknown component or type \"" << detail << "\"\n";
    return false;
}

// Compares components of one type over the recorded run: resets each, asks
// for its metrics, and prints a side-by-side table. `only` restricts the set
// to a single component. Metrics are self-describing (label/value/unit); the
// harness only aligns labels and formats, never interprets the numbers.
void print_comparison_table(std::string_view type, const std::string& only,
                            const RunContext& ctx,
                            const std::vector<ParamOverride>& overrides) {
    std::vector<std::string_view> names = component_names(type);
    if (!only.empty()) names = {only};

    std::vector<std::string> labels;
    std::vector<std::pair<std::string, std::vector<Metric>>> rows;
    for (const std::string_view name : names) {
        auto comp = make_component(name);
        if (comp == nullptr) continue;
        apply_overrides(*comp, overrides);
        comp->reset();
        std::vector<Metric> metrics = comp->metrics(ctx);
        if (metrics.empty()) continue;
        for (const Metric& m : metrics) {
            if (std::find(labels.begin(), labels.end(), m.label) == labels.end()) {
                labels.push_back(m.label);
            }
        }
        rows.emplace_back(std::string(name), std::move(metrics));
    }

    if (rows.empty()) {
        std::cout << "comparison: no metrics for type \"" << type << "\"\n";
        return;
    }

    const uint64_t events = ctx.branch_stats == nullptr ? 0 : ctx.branch_stats->trace.size();
    std::cout << "comparison (" << events << " events"
              << (ctx.pipeline == nullptr ? "" : ", " + ctx.pipeline->description()) << "):\n";
    std::cout << "  " << std::left << std::setw(18) << "component";
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (i + 1 == labels.size()) {
            std::cout << labels[i];
        } else {
            std::cout << std::setw(12) << labels[i];
        }
    }
    std::cout << '\n';
    for (const auto& [row_name, metrics] : rows) {
        std::cout << "  " << std::setw(18) << row_name;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            std::string text;
            for (const Metric& m : metrics) {
                if (m.label == labels[i]) {
                    text = format_metric(m);
                    break;
                }
            }
            if (i + 1 == labels.size()) {
                std::cout << text;
            } else {
                std::cout << std::setw(12) << text;
            }
        }
        std::cout << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string elf_path = "../files/output/sample.elf";
    std::string predictor_name = std::string(TwoBitSaturatingPredictor::kName);
    std::vector<ParamOverride> overrides;
    PipelineModel pipeline;
    bool comparison_mode = false;
    bool saw_predictor = false;
    std::string comparison_type = "predictor";
    std::string comparison_only;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--predictor") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --predictor requires a predictor name "
                             "(--list)\n";
                return 1;
            }
            predictor_name = argv[++i];
            saw_predictor = true;
        } else if (arg == "--comparison") {
            comparison_mode = true;
            // Optional argument: compare just one component. Only consume the
            // next token when it names a known component, so an ELF path
            // following --comparison is never mistaken for a component name.
            if (i + 1 < argc && make_component(argv[i + 1]) != nullptr) {
                comparison_only = argv[++i];
                comparison_type = std::string(make_component(comparison_only)->type());
            }
        } else if (arg == "--pipeline-stages") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --pipeline-stages requires the number of stages "
                             "(default 5)\n";
                return 1;
            }
            const std::string value = argv[++i];
            std::string error;
            const std::optional<long> stages = parse_parameter_value(value, error);
            if (!stages.has_value() || *stages < 1) {
                std::cerr << "RISC-E error: --pipeline-stages expects an integer >= 1 (got \""
                          << value << "\")\n";
                return 1;
            }
            pipeline.stages = static_cast<int>(*stages);
        } else if (arg == "--mispredict-penalty") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --mispredict-penalty requires a penalty in cycles "
                             "(default derived from the pipeline depth)\n";
                return 1;
            }
            const std::string value = argv[++i];
            std::string error;
            const std::optional<long> penalty = parse_parameter_value(value, error);
            if (!penalty.has_value()) {
                std::cerr << "RISC-E error: --mispredict-penalty expects a non-negative integer "
                             "(got \"" << value << "\")\n";
                return 1;
            }
            pipeline.mispredict_penalty = static_cast<int>(*penalty);
        } else if (arg == "--param") {
            // arch: --param is component-namespaced ("<component>.<tunable>=<value>").
            // Predictors and the pipeline register today; memory and other
            // component types reuse the same syntax and ParamSpec machinery.
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --param requires <component>.<parameter>=<value>, "
                             "e.g. gshare.history-bits=14\n";
                return 1;
            }
            const std::string spec = argv[++i];
            const std::size_t eq = spec.find('=');
            const std::size_t dot = spec.find('.');
            if (eq == std::string::npos || dot == std::string::npos || dot > eq) {
                std::cerr << "RISC-E error: --param expects <component>.<parameter>=<value>, "
                             "e.g. gshare.history-bits=14\n";
                return 1;
            }
            ParamOverride o;
            o.component = spec.substr(0, dot);
            o.name      = spec.substr(dot + 1, eq - dot - 1);
            o.value     = spec.substr(eq + 1);
            overrides.push_back(std::move(o));
        } else if (arg == "--list" || arg == "--list-predictors") {
            std::string detail;
            if (i + 1 < argc && argv[i + 1][0] != '-') detail = argv[++i];
            return print_component_list(detail) ? 0 : 1;
        } else {
            elf_path = arg;
        }
    }

    if (comparison_mode && saw_predictor) {
        std::cerr << "RISC-E error: --comparison compares components and cannot be combined "
                     "with --predictor; use --comparison <name> to select one\n";
        return 1;
    }

    // Fail fast on overrides that cannot apply in this mode (unknown target
    // component, mismatch with the active components, unknown parameter, bad
    // value).
    if (!validate_overrides(overrides, comparison_mode, predictor_name)) return 1;

    try {
        std::unique_ptr<Component> predictor_component;
        BranchPredictor* predictor = nullptr;
        if (!comparison_mode) {
            predictor_component = make_component(predictor_name);
            predictor = dynamic_cast<BranchPredictor*>(predictor_component.get());
            if (predictor == nullptr) {
                std::cerr << "RISC-E error: unknown predictor \"" << predictor_name
                          << "\"; available predictors:";
                for (const std::string_view name : component_names("predictor")) {
                    std::cerr << " " << name;
                }
                std::cerr << '\n';
                return 1;
            }
            apply_overrides(*predictor, overrides);
        }
        apply_overrides(pipeline, overrides);

        TempFileGuard temp_files;
        const std::string load_path =
            is_source_file(elf_path) ? compile_source(elf_path, temp_files) : elf_path;

        LoadedElf elf = load_elf(load_path);

        Interpreter interpreter(std::move(elf), predictor);
        interpreter.set_branch_trace(true);

        std::optional<uint32_t> exit_code = interpreter.run();

        RunContext ctx;
        ctx.instruction_count = interpreter.instruction_count();
        ctx.branch_stats = &interpreter.branch_stats();
        ctx.pipeline = &pipeline;

        if (comparison_mode) {
            print_comparison_table(comparison_type, comparison_only, ctx, overrides);
        } else {
            std::cout << predictor->report_title() << '\n';
            predictor->report(std::cout, ctx);
            std::cout << '\n';
            std::cout << pipeline.report_title() << '\n';
            pipeline.report(std::cout, ctx);
            std::cout << '\n';
        }

        if (exit_code.has_value()) {
            std::cout << "exit code: " << *exit_code << '\n';
            return static_cast<int>(*exit_code);
        }

        std::cout << "halted (" << halt_reason_name(interpreter.halt_reason()) << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "RISC-E error: " << e.what() << '\n';
        return 1;
    }
}
