#include "risc-e/core/cpu/branch_stats.hpp"
#include "risc-e/core/elf/loader.hpp"
#include "risc-e/core/interpreter/interpreter.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <string>

namespace {

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
              << "  total: " << stats.total
              << " (taken: " << stats.taken
              << ", not taken: " << stats.not_taken << ")\n";
    for (int i = 0; i < 8; ++i) {
        if (stats.type_total[i] == 0) continue;
        std::cout << "  " << kTypeNames[i] << ": " << stats.type_taken[i]
                  << "/" << stats.type_total[i] << " taken\n";
    }
    if (predictor != nullptr) {
        std::cout << "  predictor: " << predictor->name() << "\n"
                  << "  hits: " << stats.hits << ", misses: " << stats.misses
                  << ", hit rate: " << stats.hit_rate() << "%\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string elf_path = "../files/output/sample.elf";
    bool show_branch_stats = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--branch-stats") {
            show_branch_stats = true;
        } else {
            elf_path = arg;
        }
    }

    try {
        LoadedElf elf = load_elf(elf_path);

        TwoBitSaturatingPredictor predictor;
        BranchPredictor* predictor_ptr = show_branch_stats ? &predictor : nullptr;

        Interpreter interpreter(std::move(elf), predictor_ptr);
        if (show_branch_stats) interpreter.set_branch_trace(true);

        std::optional<uint32_t> exit_code = interpreter.run();

        if (show_branch_stats) {
            print_branch_stats(interpreter.branch_stats(), predictor_ptr);
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
