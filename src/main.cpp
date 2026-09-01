#include "risc-e/component/predictor/branch_predictor.hpp"
#include "risc-e/component/predictor/branch_stats.hpp"
#include "risc-e/component/icache/icache.hpp"
#include "risc-e/component/icache/implementations/fully_associative.hpp"
#include "risc-e/component/dcache/implementations/l1l2_cache.hpp"
#include "risc-e/component/pipeline/pipeline.hpp"
#include "risc-e/component/predictor/implementations/two_bit_saturating.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/component/component.hpp"
#include "risc-e/component/registry.hpp"
#include "risc-e/component/run_context.hpp"
#include "risc-e/disasm/disasm.hpp"
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
                        const std::string& predictor_name, const std::string& icache_name,
                        const std::string& dcache_name) {
    for (const ParamOverride& o : overrides) {
        auto comp = make_component(o.component);
        if (comp == nullptr) {
            std::cerr << "RISC-E error: --param targets unknown component \"" << o.component
                      << "\"\n";
            return false;
        }
        if (!comparison_mode && o.component != predictor_name &&
            o.component != PipelineModel::kName && o.component != icache_name &&
            o.component != dcache_name) {
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

// Prints a section separator: a fixed-width line of dashes with the label
// centered, e.g. "------------------- output -------------------".
void print_section_header(std::string_view label) {
    constexpr std::size_t kWidth = 48;
    const std::string text = " " + std::string(label) + " ";
    const std::size_t left = (kWidth - text.size()) / 2;
    std::cout << std::string(left, '-') << text
              << std::string(kWidth - text.size() - left, '-') << '\n';
}

// Pretty-prints a JSON document: re-emits it with two-space indentation.
// The input is assumed valid JSON (it is machine-generated here); strings are
// copied verbatim, so escaped quotes and control characters stay intact.
std::string pretty_json(std::string_view json) {
    std::string out;
    out.reserve(json.size() + json.size() / 8);
    std::vector<bool> empty_scopes;  // per open bracket: no content emitted yet
    bool in_string = false;
    bool escape = false;
    int depth = 0;

    const auto indent = [&] {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
    };
    const auto mark_content = [&] {
        if (!empty_scopes.empty()) empty_scopes.back() = false;
    };

    for (const char c : json) {
        if (in_string) {
            out.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        switch (c) {
            case '"':
                in_string = true;
                mark_content();
                out.push_back(c);
                break;
            case '{':
            case '[':
                out.push_back(c);
                ++depth;
                indent();
                empty_scopes.push_back(true);
                break;
            case '}':
            case ']':
                --depth;
                if (empty_scopes.back()) {
                    // Empty object/array: drop the newline+indent after the open.
                    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
                        out.pop_back();
                    }
                    out.push_back(c);
                } else {
                    indent();
                    out.push_back(c);
                }
                empty_scopes.pop_back();
                mark_content();
                break;
            case ',':
                out.push_back(c);
                indent();
                break;
            case ':':
                out.push_back(':');
                out.push_back(' ');
                break;
            case ' ':
            case '\n':
            case '\t':
            case '\r':
                break;  // drop source whitespace
            default:
                mark_content();
                out.push_back(c);
                break;
        }
    }
    return out;
}

// One row of a comparison: the same three columns as the printed table.
struct ComparisonRow {
    std::string name;
    uint64_t cycles_before;
    uint64_t cycles_after;
    double speedup;
};

struct ComparisonResult {
    std::string type;
    std::string baseline;
    std::vector<ComparisonRow> rows;
};

// Compares components of one type over the recorded run: resets each, asks
// for its cost answer, and keeps the same three columns for every row —
// cycles before (the type's reference baseline), cycles after (this design),
// and speedup (before / after). `only` restricts the set to a single
// component. Components that do not model time are skipped.
ComparisonResult build_comparison_rows(std::string_view type, const std::string& only,
                                       const RunContext& ctx,
                                       const std::vector<ParamOverride>& overrides) {
    ComparisonResult result;
    result.type = std::string(type);

    std::vector<std::string_view> names = component_names(type);
    if (!only.empty()) names = {only};

    for (const std::string_view name : names) {
        auto comp = make_component(name);
        if (comp == nullptr) continue;
        apply_overrides(*comp, overrides);
        comp->reset();
        const std::optional<CycleCost> cc = comp->cycle_cost(ctx);
        if (!cc.has_value()) continue;
        result.rows.push_back(ComparisonRow{
            std::string(name), cc->baseline_cycles, cc->total_cycles,
            cc->baseline_cycles == 0
                ? 0.0
                : static_cast<double>(cc->baseline_cycles) /
                      static_cast<double>(cc->total_cycles)});
        if (result.baseline.empty()) result.baseline = std::string(cc->baseline_name);
    }
    return result;
}

void print_comparison_table(const ComparisonResult& result, const RunContext& ctx) {
    if (result.rows.empty()) {
        std::cout << "comparison: no cycle cost for type \"" << result.type << "\"\n";
        return;
    }

    const uint64_t events = ctx.branch_stats == nullptr ? 0 : ctx.branch_stats->trace.size();
    std::cout << "comparison (" << events << " events"
              << (ctx.pipeline == nullptr ? "" : ", " + ctx.pipeline->description());
    if (!result.baseline.empty()) std::cout << "; speedup vs " << result.baseline;

    // Columns size to the widest cell; the +1 keeps a visible gap.
    std::size_t name_w = std::string("component").size();
    std::size_t before_w = std::string("cycles before").size();
    std::size_t after_w = std::string("cycles after").size();
    for (const ComparisonRow& r : result.rows) {
        name_w = std::max(name_w, r.name.size());
        before_w = std::max(before_w, std::to_string(r.cycles_before).size());
        after_w = std::max(after_w, std::to_string(r.cycles_after).size());
    }
    std::cout << "):\n"
              << "  " << std::left << std::setw(static_cast<int>(name_w) + 1) << "component"
              << std::setw(static_cast<int>(before_w) + 1) << "cycles before"
              << std::setw(static_cast<int>(after_w) + 1) << "cycles after"
              << "speedup\n";
    for (const ComparisonRow& r : result.rows) {
        std::ostringstream speed;
        speed << std::fixed << std::setprecision(2) << r.speedup << "x";
        std::cout << "  " << std::left << std::setw(static_cast<int>(name_w) + 1) << r.name
                  << std::setw(static_cast<int>(before_w) + 1) << r.cycles_before
                  << std::setw(static_cast<int>(after_w) + 1) << r.cycles_after << speed.str()
                  << '\n';
    }
}

// The comparison as a JSON object for the saved report.
void write_comparison_json(std::ostream& out, const ComparisonResult& result,
                           const RunContext& ctx) {
    const uint64_t events = ctx.branch_stats == nullptr ? 0 : ctx.branch_stats->trace.size();
    out << "{\"type\":\"" << json_escape(result.type) << "\",\"events\":" << events
        << ",\"baseline\":\"" << json_escape(result.baseline) << "\",\"rows\":[";
    for (std::size_t i = 0; i < result.rows.size(); ++i) {
        const ComparisonRow& r = result.rows[i];
        std::ostringstream speed;
        speed << std::fixed << std::setprecision(2) << r.speedup;
        out << "{\"name\":\"" << json_escape(r.name)
            << "\",\"cycles_before\":" << r.cycles_before
            << ",\"cycles_after\":" << r.cycles_after << ",\"speedup\":" << speed.str() << "}";
        if (i + 1 < result.rows.size()) out << ",";
    }
    out << "]}";
}

} // namespace

int main(int argc, char** argv) {
    std::string elf_path = "../files/output/sample.elf";
    std::string predictor_name = std::string(TwoBitSaturatingPredictor::kName);
    std::string icache_name = std::string(FullyAssociativeICache::kName);
    std::string dcache_name = std::string(L1L2Cache::kName);
    std::vector<ParamOverride> overrides;
    PipelineModel pipeline;
    bool comparison_mode = false;
    bool saw_predictor = false;
    bool print_report = false;  // --print: show the human-readable report
    bool verbose = false;       // --verbose: config echo and per-record tables
    bool disasm = false;        // --disasm: static listing with block markers
    std::string comparison_type;
    std::string comparison_only;
    std::string json_path;

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
        } else if (arg == "--icache") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --icache requires an instruction-cache name "
                             "(--list)\n";
                return 1;
            }
            icache_name = argv[++i];
        } else if (arg == "--dcache") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --dcache requires a data-cache name "
                             "(--list)\n";
                return 1;
            }
            dcache_name = argv[++i];
        } else if (arg == "--comparison") {
            // Requires a component or type name: the comparison never
            // defaults to a type, so a bare --comparison (or an ELF path
            // after it) is an error.
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --comparison requires a component or type name "
                             "(--list)\n";
                return 1;
            }
            const std::string name = argv[++i];
            if (make_component(name) != nullptr) {
                comparison_only = name;
                comparison_type = std::string(make_component(name)->type());
            } else if (!component_names(name).empty()) {
                comparison_type = name;  // compare the whole type
            } else {
                std::cerr << "RISC-E error: --comparison requires a component or type name; "
                             "unknown \"" << name << "\" (--list)\n";
                return 1;
            }
            comparison_mode = true;
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
        } else if (arg == "--stall-penalty") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --stall-penalty requires a penalty in cycles "
                             "(default derived from the pipeline depth)\n";
                return 1;
            }
            const std::string value = argv[++i];
            std::string error;
            const std::optional<long> penalty = parse_parameter_value(value, error);
            if (!penalty.has_value()) {
                std::cerr << "RISC-E error: --stall-penalty expects a non-negative integer "
                             "(got \"" << value << "\")\n";
                return 1;
            }
            pipeline.stall_penalty = static_cast<int>(*penalty);
        } else if (arg == "--param") {
            // Format: "<component>.<parameter>=<value>", namespaced by component.
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
        } else if (arg == "--print") {
            print_report = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--disasm") {
            disasm = true;
        } else if (arg == "--json") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --json requires a report file path\n";
                return 1;
            }
            json_path = argv[++i];
        } else if (arg == "--list") {
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
    if (!validate_overrides(overrides, comparison_mode, predictor_name, icache_name,
                            dcache_name))
        return 1;

    try {
        std::unique_ptr<Component> predictor_component;
        BranchPredictor* predictor = nullptr;
        if (!comparison_mode) {
            predictor_component = make_component(predictor_name);
            predictor = dynamic_cast<BranchPredictor*>(predictor_component.get());
            if (predictor == nullptr) {
                std::cerr << "RISC-E error: unknown predictor \"" << predictor_name
                          << "\"; available :";
                for (const std::string_view name : component_names("predictor")) {
                    std::cerr << " " << name;
                }
                std::cerr << '\n';
                return 1;
            }
            apply_overrides(*predictor, overrides);
        }
        apply_overrides(pipeline, overrides);

        // The active instruction-cache design, selected with --icache
        // (default fully associative); only used for the report section.
        std::unique_ptr<Component> icache_component;
        ICacheComponent* icache = nullptr;
        if (!comparison_mode) {
            icache_component = make_component(icache_name);
            icache = dynamic_cast<ICacheComponent*>(icache_component.get());
            if (icache == nullptr) {
                std::cerr << "RISC-E error: unknown instruction cache \"" << icache_name
                          << "\"; available caches:";
                for (const std::string_view name : component_names("icache")) {
                    std::cerr << " " << name;
                }
                std::cerr << '\n';
                return 1;
            }
            apply_overrides(*icache, overrides);
        }

        // The active data-cache design, selected with --dcache (default the
        // L1+L2 hierarchy); only used for the report section.
        std::unique_ptr<Component> dcache_component;
        L1L2Cache* dcache = nullptr;
        if (!comparison_mode) {
            dcache_component = make_component(dcache_name);
            dcache = dynamic_cast<L1L2Cache*>(dcache_component.get());
            if (dcache == nullptr) {
                std::cerr << "RISC-E error: unknown data cache \"" << dcache_name
                          << "\"; available caches:";
                for (const std::string_view name : component_names("cache")) {
                    std::cerr << " " << name;
                }
                std::cerr << '\n';
                return 1;
            }
            apply_overrides(*dcache, overrides);
        }

        TempFileGuard temp_files;
        const std::string load_path =
            is_source_file(elf_path) ? compile_source(elf_path, temp_files) : elf_path;

        LoadedElf elf = load_elf(load_path);

        // --disasm lists the loaded image, so keep the segments the
        // interpreter was built from before it takes ownership of the ELF.
        std::vector<LoadedSegment> segments;
        if (disasm) segments = elf.segments;

        Interpreter interpreter(std::move(elf), predictor);
        interpreter.set_branch_trace(true);

        std::optional<uint32_t> exit_code = interpreter.run();

        RunContext ctx;
        ctx.instruction_count = interpreter.instruction_count();
        ctx.branch_stats = &interpreter.branch_stats();
        ctx.pipeline = &pipeline;
        ctx.profile_stats = &interpreter.profile_stats();
        ctx.access_trace = &interpreter.access_trace();
        ctx.verbose = verbose;

        // The JSON report is the default artifact, saved to the project
        // root's results/<program>.json (or the --json path); --print
        // additionally shows the verbose human-readable report on the
        // terminal.
        const std::string report_path =
            json_path.empty()
                ? (std::filesystem::path(RISC_E_PROJECT_ROOT) / "results" /
                   std::filesystem::path(elf_path).filename().replace_extension(".json"))
                      .string()
                : json_path;

        const std::filesystem::path report_dir = std::filesystem::path(report_path).parent_path();
        if (!report_dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(report_dir, ec);
        }

        // Show the report path relative to the current directory when it is
        // inside it; fall back to the full path otherwise.
        std::error_code rel_ec;
        std::filesystem::path display_path =
            std::filesystem::relative(report_path, std::filesystem::current_path(), rel_ec);
        if (rel_ec || display_path.empty()) display_path = report_path;

        // Build the JSON document compactly, then pretty-print it before
        // writing, so the file is uniformly indented and always valid.
        std::ostringstream doc;
        doc << "{\"program\":\"" << json_escape(elf_path) << "\",\"exit_code\":";
        if (exit_code.has_value()) {
            doc << *exit_code;
        } else {
            doc << "null";
        }
        doc << ",\"halt_reason\":\"" << json_escape(halt_reason_name(interpreter.halt_reason()))
            << "\",\"program_output\":\"" << json_escape(interpreter.program_output()) << "\",";

        ComparisonResult comparison;
        if (comparison_mode) {
            comparison = build_comparison_rows(comparison_type, comparison_only, ctx, overrides);
            doc << "\"comparison\":";
            write_comparison_json(doc, comparison, ctx);
        } else {
            doc << "\"stats\":{\"pipeline\":";
            pipeline.write_json(doc, ctx);
            doc << ",\"branch_prediction\":";
            predictor->write_json(doc, ctx);
            doc << ",\"icache\":";
            icache->write_json(doc, ctx);
            doc << ",\"cache\":";
            dcache->write_json(doc, ctx);
            doc << "}";
        }
        doc << "}";

        const std::string report_doc = pretty_json(doc.str());

        {
            std::ofstream report(report_path);
            if (!report) {
                std::cerr << "RISC-E error: could not write report to \"" << report_path
                          << "\"\n";
                return 1;
            }
            report << report_doc << '\n';
        }
        std::cout << "report saved to " << display_path.string() << '\n';

        if (print_report) {
            // The program's own output (if any) is printed under an "output"
            // separator; the tool's report follows under a "stats" separator.
            if (!interpreter.program_output().empty()) {
                print_section_header("output");
                std::cout << interpreter.program_output();
            }
            print_section_header("stats");
            if (comparison_mode) {
                print_comparison_table(comparison, ctx);
            } else {
                // The pipeline section doubles as the run summary and always
                // leads; the components only report their own behaviour.
                std::cout << pipeline.report_title() << '\n';
                pipeline.report(std::cout, ctx);
                std::cout << '\n';
                std::cout << predictor->report_title() << '\n';
                predictor->report(std::cout, ctx);
                std::cout << '\n';
                std::cout << icache->report_title() << '\n';
                icache->report(std::cout, ctx);
                std::cout << '\n';
                std::cout << dcache->report_title() << '\n';
                dcache->report(std::cout, ctx);
                std::cout << '\n';
            }
        }

        if (disasm) {
            print_section_header("disassembly");
            print_disassembly(std::cout, segments, interpreter.memory(),
                              &interpreter.profile_stats());
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
