#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"
#include "risc-e/cpu/return_address_stack.hpp"
#include "risc-e/decoder/decoder.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include "runtime_files.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
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

void print_branch_stats(const BranchStats& stats, const BranchPredictor* predictor) {
    static const char* kTypeNames[8] = {"BEQ", "BNE", "?", "?", "BLT", "BGE", "BLTU", "BGEU"};

    std::cout << "branch stats:\n"
              << "  conditional branches: " << stats.total
              << " (taken: " << stats.taken
              << ", not taken: " << stats.not_taken << ")\n";
    for (int i = 0; i < 8; ++i) {
        if (stats.type_total[i] == 0) continue;
        std::cout << "  " << kTypeNames[i] << ": " << stats.type_taken[i]
                  << "/" << stats.type_total[i] << " taken\n";
    }
    if (predictor != nullptr) {
        std::cout << "  predictor: " << predictor->name() << "\n"
                  << "  control transfers: " << stats.control_total << "\n"
                  << "  hits: " << stats.hits << ", misses: " << stats.misses
                  << ", hit rate: " << stats.hit_rate() << "%\n"
                  << "  conditional hit rate: " << stats.conditional_hit_rate() << "%\n"
                  << "  indirect (JALR) hit rate: " << stats.indirect_hit_rate() << "%\n";
    }
}

// Applies every override that targets `predictor` to it. Overrides are
// validated in main before the run, so this cannot fail here.
void apply_overrides(BranchPredictor& predictor, const std::vector<ParamOverride>& overrides) {
    for (const ParamOverride& o : overrides) {
        if (o.predictor != predictor.name()) continue;
        std::string error;
        const bool ok = predictor.set_parameter(o.name, o.value, error);
        (void)ok;
    }
}

// Checks that every override targets a real predictor, that it matches the
// selected predictor in single-predictor mode, and that the target accepts the
// parameter and value. Returns false after reporting the first problem.
bool validate_overrides(const std::vector<ParamOverride>& overrides, bool replay,
                        const std::string& predictor_name) {
    for (const ParamOverride& o : overrides) {
        auto target = make_predictor(o.predictor);
        if (target == nullptr) {
            std::cerr << "RISC-E error: --param targets unknown predictor \"" << o.predictor
                      << "\"\n";
            return false;
        }
        if (!replay && o.predictor != predictor_name) {
            std::cerr << "RISC-E error: --param \"" << o.predictor << "." << o.name
                      << "\" targets \"" << o.predictor << "\" but --predictor selected \""
                      << predictor_name << "\"\n";
            return false;
        }
        std::string error;
        if (!target->set_parameter(o.name, o.value, error)) {
            std::cerr << "RISC-E error: predictor \"" << o.predictor << "\": " << error
                      << "; valid parameters:";
            for (const ParamSpec& p : target->parameters()) {
                std::cerr << " " << p.name;
            }
            std::cerr << '\n';
            return false;
        }
    }
    return true;
}

// Replays a recorded control-flow trace through every available predictor and
// prints a comparison table. Reuses the same accounting as live execution.
void print_replay_results(const std::vector<BranchRecord>& trace,
                          const std::vector<ParamOverride>& overrides) {
    if (trace.empty()) {
        std::cout << "replay: no control transfers recorded\n";
        return;
    }
    std::cout << "replay results (" << trace.size() << " control transfers):\n";
    for (const std::string_view name : predictor_names()) {
        auto predictor = make_predictor(name);
        apply_overrides(*predictor, overrides);  // validated in main, cannot fail here
        BranchStats stats;
        for (const BranchRecord& rec : trace) {
            const DecodedInstruction d = decode_raw_inst(rec.raw, rec.pc);
            const BranchContext ctx = BranchContext::from_decoded(d);
            record_control_transfer(stats, predictor.get(), ctx, rec.taken, rec.target);
        }
        std::cout << "  " << name << ": " << stats.hits << "/" << stats.control_total
                  << " hits (" << stats.hit_rate() << "%), cond " << stats.cond_hits << "/"
                  << (stats.cond_hits + stats.cond_misses) << ", indirect "
                  << stats.indirect_hits << "/" << (stats.indirect_hits + stats.indirect_misses)
                  << '\n';
    }
}

void print_predictor_list(const std::string& detail) {
    if (detail.empty()) {
        for (const std::string_view name : predictor_names()) {
            std::cout << name;
            auto predictor = make_predictor(name);
            const std::vector<ParamSpec> params = predictor->parameters();
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
        return;
    }

    auto predictor = make_predictor(detail);
    if (predictor == nullptr) {
        std::cerr << "RISC-E error: unknown predictor \"" << detail << "\"\n";
        return;
    }
    std::cout << detail << ":\n";
    const std::vector<ParamSpec> params = predictor->parameters();
    if (params.empty()) {
        std::cout << "  (no parameters)\n";
        return;
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
}

} // namespace

int main(int argc, char** argv) {
    std::string elf_path = "../files/output/sample.elf";
    std::string predictor_name = std::string(TwoBitSaturatingPredictor::kName);
    std::vector<ParamOverride> overrides;
    bool replay = false;
    bool saw_predictor = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--predictor") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --predictor requires a predictor name "
                             "(--list-predictors)\n";
                return 1;
            }
            predictor_name = argv[++i];
            saw_predictor = true;
        } else if (arg == "--replay") {
            replay = true;
        } else if (arg == "--param") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --param requires <predictor>.<parameter>=<value>, "
                             "e.g. gshare.history-bits=14\n";
                return 1;
            }
            const std::string spec = argv[++i];
            const std::size_t eq = spec.find('=');
            const std::size_t dot = spec.find('.');
            if (eq == std::string::npos || dot == std::string::npos || dot > eq) {
                std::cerr << "RISC-E error: --param expects <predictor>.<parameter>=<value>, "
                             "e.g. gshare.history-bits=14\n";
                return 1;
            }
            ParamOverride o;
            o.predictor = spec.substr(0, dot);
            o.name      = spec.substr(dot + 1, eq - dot - 1);
            o.value     = spec.substr(eq + 1);
            overrides.push_back(std::move(o));
        } else if (arg == "--list-predictors") {
            std::string detail;
            if (i + 1 < argc && argv[i + 1][0] != '-') detail = argv[++i];
            print_predictor_list(detail);
            return detail.empty() ? 0 : (make_predictor(detail) == nullptr ? 1 : 0);
        } else {
            elf_path = arg;
        }
    }

    if (replay && saw_predictor) {
        std::cerr << "RISC-E error: --replay replays every predictor and cannot be combined "
                     "with --predictor\n";
        return 1;
    }

    // Fail fast on overrides that cannot apply in this mode (unknown target
    // predictor, mismatch with --predictor, unknown parameter, bad value).
    if (!validate_overrides(overrides, replay, predictor_name)) return 1;

    try {
        std::unique_ptr<BranchPredictor> predictor;
        if (!replay) {
            predictor = make_predictor(predictor_name);
            if (predictor == nullptr) {
                std::cerr << "RISC-E error: unknown predictor \"" << predictor_name
                          << "\"; available predictors:";
                for (const std::string_view name : predictor_names()) {
                    std::cerr << " " << name;
                }
                std::cerr << '\n';
                return 1;
            }
            apply_overrides(*predictor, overrides);  // validated in main, cannot fail here
        }

        TempFileGuard temp_files;
        const std::string load_path =
            is_source_file(elf_path) ? compile_source(elf_path, temp_files) : elf_path;

        LoadedElf elf = load_elf(load_path);

        Interpreter interpreter(std::move(elf), predictor.get());
        interpreter.set_branch_trace(true);

        std::optional<uint32_t> exit_code = interpreter.run();

        print_branch_stats(interpreter.branch_stats(), predictor.get());

        if (replay) {
            print_replay_results(interpreter.branch_stats().trace, overrides);
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
