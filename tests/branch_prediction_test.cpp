#include "risc-e/cpu/branch_predictor.hpp"
#include "risc-e/cpu/branch_stats.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
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

void test_two_bit_saturating() {
    TwoBitSaturatingPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");

    expect(stats.total == 5, "5 conditional branches executed");
    expect(stats.taken == 4 && stats.not_taken == 1, "loop: 4 taken, 1 not taken");
    expect(stats.control_total == 7, "7 control transfers (5 branches + JAL + JALR)");

    expect(stats.hits == 5 && stats.misses == 2, "target-aware: 5 hits, 2 misses");
    expect(stats.cond_hits == 4 && stats.cond_misses == 1, "conditional: 4 hits, 1 miss");
    expect(stats.indirect_hits == 0 && stats.indirect_misses == 1, "JALR: 0 hits, 1 miss");
}

void test_always_not_taken() {
    AlwaysNotTakenPredictor predictor;
    const auto [stats, exit_code] = run_with(predictor);

    expect(exit_code.has_value() && *exit_code == 7, "program should exit with code 7");
    expect(stats.hits == 2 && stats.misses == 5, "always-not-taken: 2 hits, 5 misses");
    expect(stats.cond_hits == 1 && stats.cond_misses == 4, "always-not-taken conditional: 1 hit, 4 misses");
}

} // namespace

int main() {
    test_two_bit_saturating();
    test_always_not_taken();

    std::printf("branch prediction tests passed\n");
    return 0;
}
