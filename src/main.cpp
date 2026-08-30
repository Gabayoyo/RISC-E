#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/cpu/predictors/two_bit_saturating.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>  // access(), getpid()

namespace {

const char* kAssemblerFlags = "-march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -Wl,-Ttext=0x10000";

bool is_assembly_source(const std::string& path) {
    const std::string ext = std::filesystem::path(path).extension().string();
    return ext == ".S" || ext == ".s";
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

// Compiles RISC-V assembly to an ELF in the system temp directory and returns
// its path. The caller owns the temp file. Assembler diagnostics pass through
// to stderr; on failure the temp file is removed and an exception is thrown.
std::string assemble_source(const std::string& source_path) {
    const std::string compiler = find_cross_compiler();
    if (compiler.empty()) {
        throw std::runtime_error(
            "input is assembly (" + source_path +
            ") but no RISC-V cross-compiler was found on PATH; install "
            "riscv64-elf-gcc or set RISCV_GCC");
    }

    const std::string out_path = (std::filesystem::temp_directory_path() /
                                  ("risc-e-" + std::to_string(getpid()) + ".elf"))
                                     .string();
    const std::string command = compiler + " " + kAssemblerFlags + " -o \"" + out_path +
                                "\" \"" + source_path + "\"";
    if (std::system(command.c_str()) != 0) {
        std::remove(out_path.c_str());
        throw std::runtime_error("assembly failed for " + source_path);
    }
    return out_path;
}

struct TempFileGuard {
    std::string path;
    ~TempFileGuard() {
        if (!path.empty()) std::remove(path.c_str());
    }
};

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

} // namespace

int main(int argc, char** argv) {
    std::string elf_path = "../files/output/sample.elf";
    std::string predictor_name = std::string(TwoBitSaturatingPredictor::kName);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--predictor") {
            if (i + 1 >= argc) {
                std::cerr << "RISC-E error: --predictor requires a predictor name "
                             "(--list-predictors)\n";
                return 1;
            }
            predictor_name = argv[++i];
        } else if (arg == "--list-predictors") {
            for (std::string_view name : predictor_names()) {
                std::cout << name << '\n';
            }
            return 0;
        } else {
            elf_path = arg;
        }
    }

    try {
        auto predictor = make_predictor(predictor_name);
        if (predictor == nullptr) {
            std::cerr << "RISC-E error: unknown predictor \"" << predictor_name
                      << "\"; available predictors:";
            for (std::string_view name : predictor_names()) {
                std::cerr << " " << name;
            }
            std::cerr << '\n';
            return 1;
        }

        TempFileGuard temp_elf;
        if (is_assembly_source(elf_path)) {
            temp_elf.path = assemble_source(elf_path);
        }
        const std::string load_path = temp_elf.path.empty() ? elf_path : temp_elf.path;

        LoadedElf elf = load_elf(load_path);

        Interpreter interpreter(std::move(elf), predictor.get());
        interpreter.set_branch_trace(true);

        std::optional<uint32_t> exit_code = interpreter.run();

        print_branch_stats(interpreter.branch_stats(), predictor.get());

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
