#include "risc-e/cpu/profile.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/harness/registry.hpp"
#include "risc-e/harness/run_context.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <cmath>
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

// Countdown loop + direct call + indirect return, hand-encoded for RV32I
// (identical to branches.S):
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

ProfileStats run_program() {
    Interpreter interp(build_program());
    const std::optional<uint32_t> exit_code = interp.run();
    expect(exit_code.has_value() && *exit_code == 7, "program exits with code 7");
    return interp.profile_stats();
}

void test_footprint(const ProfileStats& s) {
    expect(s.instructions == 16, "16 instructions executed");
    expect(s.seen_pcs.size() == 8, "8 distinct instructions executed");
}

void test_blocks(const ProfileStats& s) {
    expect(s.blocks.size() == 5, "5 dynamic basic blocks");
    expect(s.pc_to_id.size() == 5, "5 interned entry PCs");

    // Block 0: the entry sequence. The first trip through the loop runs
    // straight-line from 0x10000, so the init, decrement and compare belong
    // to one block before the back-edge is ever taken.
    expect(s.blocks[0].entry_pc == 0x10000 && s.blocks[0].executions == 1 &&
               s.blocks[0].instructions == 3,
           "entry block: init + first loop trip");
    // Block 1: the loop body, entered once per taken branch.
    expect(s.blocks[1].entry_pc == 0x10004 && s.blocks[1].executions == 4 &&
               s.blocks[1].instructions == 8,
           "loop block: 4 executions of addi+bne");
    expect(s.blocks[2].entry_pc == 0x1000C && s.blocks[2].executions == 1 &&
               s.blocks[2].instructions == 1,
           "call site block: the jal");
    expect(s.blocks[3].entry_pc == 0x10018 && s.blocks[3].executions == 1 &&
               s.blocks[3].instructions == 2,
           "callee block: li a0 + jalr");
    expect(s.blocks[4].entry_pc == 0x10010 && s.blocks[4].executions == 1 &&
               s.blocks[4].instructions == 2,
           "return-continuation block: li a7 + ecall");

    uint64_t entries = 0;
    for (const BlockInfo& b : s.blocks) entries += b.executions;
    expect(entries == 8, "8 total block entries (1 + 4 + 1 + 1 + 1)");

    uint64_t instrs = 0;
    for (const BlockInfo& b : s.blocks) instrs += b.instructions;
    expect(instrs == s.instructions, "block instructions partition the run");
}

void test_component() {
    auto comp = make_component("icache");
    expect(comp != nullptr, "icache registers as a component");
    expect(comp->type() == "profile", "icache is a profile");
    expect(component_names("profile").size() == 1, "one profile component registered");

    const std::vector<ParamSpec> params = comp->parameters();
    expect(params.size() == 2, "profile exposes two cache tunables");
    expect(params[0].name == "miss-penalty" && params[1].name == "cache-capacity",
           "cache tunable names");

    Interpreter interp(build_program());
    (void)interp.run();

    RunContext ctx;
    ctx.instruction_count = interp.instruction_count();
    ctx.profile_stats = &interp.profile_stats();

    auto profile = dynamic_cast<ProfileComponent*>(comp.get());
    expect(profile != nullptr, "registered component is a ProfileComponent");

    // The canonical cost answer for the comparison table.
    const std::optional<CycleCost> cc = profile->cycle_cost(ctx);
    expect(cc.has_value(), "profile provides a cost answer");
    expect(cc->total_cycles == 266 && cc->baseline_cycles == 416,
           "icache cycle cost vs no cache");
    expect(cc->baseline_name == "no instruction cache", "cost baseline named");
}

void test_icache() {
    // Defaults (miss penalty 50, cache 64 instrs): every block fits, so only
    // the loop body re-hits; the five blocks' first touches are compulsory
    // misses.
    const CacheModel model;
    const CacheTime t = simulate_icache(run_program(), model);
    expect(t.entries == 8 && t.hits == 3 && t.misses == 5, "5 misses, 3 loop hits");
    expect(t.compulsory_misses == 5 && t.evictions == 0,
           "all misses are compulsory with a roomy cache");
    expect(t.miss_stalls == 250 && t.total_cycles == 266, "instructions + miss stalls");
    expect(t.baseline_cycles == 416 && t.saved_cycles == 150,
           "cycles saved vs no instruction cache");
    expect(std::abs(t.saved_pct - 36.0577) < 1e-2 && std::abs(t.hit_rate - 37.5) < 1e-9,
           "hit rate and saved percentage");
    expect(std::abs(t.hit_rate - 37.5) < 1e-9, "hit rate is hits / entries");

    // No blocks -> no fetches, no division by zero.
    const CacheTime empty = simulate_icache(ProfileStats{}, model);
    expect(empty.entries == 0 && empty.baseline_cycles == 0 && empty.saved_pct == 0.0,
           "empty profile yields zero time");

    // Parameter validation through the harness.
    auto comp = make_component("icache");
    std::string error;
    expect(!comp->set_parameter("miss-penalty", "-1", error), "negative penalty rejected");
    expect(comp->set_parameter("miss-penalty", "100", error), "miss penalty accepted");
    expect(comp->set_parameter("cache-capacity", "0", error), "cache capacity 0 = unlimited");
    expect(comp->set_parameter("cache-capacity", "8", error), "cache capacity accepted");
    expect(!comp->set_parameter("cache-capacity", "-1", error), "negative capacity rejected");
    expect(!comp->set_parameter("bogus", "1", error), "unknown parameter rejected");
}

// Three 4-instruction blocks, entered interleaved: id0 id1 id2 id0 id1 id2
// id0 id0. With a 12-instruction cache everything stays resident; with an
// 8-instruction cache (two blocks) every new block evicts the
// least-recently-used one, forcing capacity misses.
ProfileStats make_thrash_profile() {
    ProfileStats s;
    const uint32_t pcs[3] = {0x1000, 0x2000, 0x3000};
    for (int round = 0; round < 2; ++round) {
        for (int k = 0; k < 3; ++k) {
            const uint32_t id = s.record_block_entry(pcs[k]);
            for (int i = 0; i < 4; ++i) {
                s.record_block_instruction(id);
                s.record_instruction(pcs[k] + i);
            }
        }
    }
    for (int k = 0; k < 2; ++k) {
        const uint32_t id = s.record_block_entry(pcs[0]);
        for (int i = 0; i < 4; ++i) {
            s.record_block_instruction(id);
            s.record_instruction(0x1000 + i);
        }
    }
    return s;
}

void test_eviction() {
    const ProfileStats s = make_thrash_profile();  // 32 instructions, 8 entries
    expect(s.entry_sequence.size() == 8 && s.instructions == 32, "thrash profile built");

    // Cache big enough for all three blocks: no evictions, first touches
    // miss, later entries hit.
    const CacheTime fits = simulate_icache(s, CacheModel{50, 12});
    expect(fits.evictions == 0 && fits.misses == 3 && fits.hits == 5,
           "roomy cache: 3 compulsory misses, 5 hits");
    expect(fits.compulsory_misses == 3, "roomy cache misses are all compulsory");
    expect(fits.miss_stalls == 150 && fits.total_cycles == 182,
           "roomy cache cycle total");
    expect(fits.baseline_cycles == 432 && fits.saved_cycles == 250,
           "roomy cache saves cycles vs no cache");
    expect(std::abs(fits.saved_pct - 57.87) < 1e-2, "roomy cache saves 57.9%");

    // Cache holds two blocks: every new block after the third evicts the LRU
    // one, so the displaced blocks miss again on their next entry (capacity
    // misses). The cache still saves cycles, just less.
    const CacheTime tight = simulate_icache(s, CacheModel{50, 8});
    expect(tight.evictions == 5 && tight.misses == 7 && tight.hits == 1,
           "tight cache evicts and re-misses");
    expect(tight.compulsory_misses == 3, "5 of the 7 misses are capacity misses");
    expect(tight.miss_stalls == 350 && tight.total_cycles == 382,
           "tight cache cycle total");
    expect(tight.baseline_cycles == 432 && tight.saved_cycles == 50,
           "tight cache still beats no cache");
    expect(std::abs(tight.saved_pct - 11.57) < 1e-2, "tight cache saves 11.6%");
}

void test_reset() {
    ProfileStats s;
    s.record_instruction(0x10000);
    s.record_block_entry(0x10000);
    expect(s.instructions == 1 && s.blocks.size() == 1, "profile populated");
    s.reset();
    expect(s.instructions == 0 && s.blocks.empty() && s.seen_pcs.empty(),
           "reset clears the profile");
}

} // namespace

int main() {
    const ProfileStats s = run_program();
    test_footprint(s);
    test_blocks(s);
    test_component();
    test_icache();
    test_eviction();
    test_reset();

    std::printf("profile test: all passed\n");
    return 0;
}
