#include "risc-e/cpu/icache.hpp"
#include "risc-e/cpu/icache/fully_associative.hpp"
#include "risc-e/cpu/icache/prefetch.hpp"
#include "risc-e/cpu/icache/pseudo_lru.hpp"
#include "risc-e/cpu/icache/set_associative.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/harness/registry.hpp"
#include "risc-e/harness/run_context.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
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

// Fully associative, 16-byte lines: the whole program spans two lines
// (0x1000 and 0x1001), so after the two compulsory misses everything hits.
void test_fa() {
    ICacheConfig fa;
    fa.sets = 1;
    fa.ways = 16;
    const ICacheResult t = simulate_icache(run_program(), fa);
    expect(t.entries == 8 && t.hits == 6 && t.misses == 2, "fa: 6 hits, 2 misses");
    expect(t.compulsory_misses == 2 && t.conflict_misses == 0 && t.capacity_misses == 0,
           "fa: both misses compulsory");
    expect(t.evictions == 0, "fa: two lines fit in 16 ways");
    expect(t.miss_stalls == 100 && t.total_cycles == 116, "fa: instructions + 2 x 50 stalls");
    expect(t.baseline_cycles == 416 && t.saved_cycles == 300,
           "fa: saved vs no instruction cache");
    expect(std::abs(t.saved_pct - 72.12) < 0.01 && std::abs(t.hit_rate - 75.0) < 1e-9,
           "fa: rates");
}

// Three 4-instruction blocks (16 B each) at 0x1000/0x2000/0x1010 -> lines
// 0x100, 0x200, 0x101; entered interleaved id0 id1 id2 id0 id1 id2 id0 id0.
// A direct-mapped cache with 2 sets puts 0x100 and 0x200 in set 0: they
// evict each other (conflict misses) while 0x101 stays alone in set 1. The
// same trace under full associativity has no conflicts at all.
ProfileStats make_thrash_profile() {
    ProfileStats s;
    const uint32_t pcs[3] = {0x1000, 0x2000, 0x1010};
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

void test_conflict() {
    const ProfileStats s = make_thrash_profile();  // 32 instructions, 8 entries
    expect(s.entry_sequence.size() == 8 && s.instructions == 32, "thrash profile built");

    ICacheConfig dm;  // direct-mapped, 2 sets
    dm.sets = 2;
    dm.ways = 1;
    const ICacheResult t = simulate_icache(s, dm);
    expect(t.misses == 6 && t.hits == 2, "conflicting lines thrash in direct-mapped");
    expect(t.compulsory_misses == 3 && t.conflict_misses == 3,
           "3 compulsory + 3 conflict misses");
    expect(t.capacity_misses == 0 && t.evictions == 4, "set pressure evicts 4 times");

    // Fully associative with the same trace: everything fits, no conflicts.
    ICacheConfig fa;
    fa.sets = 1;
    fa.ways = 8;
    const ICacheResult tf = simulate_icache(s, fa);
    expect(tf.misses == 3 && tf.hits == 5, "fa: all three lines stay resident");
    expect(tf.conflict_misses == 0 && tf.evictions == 0,
           "fully associative has no conflict misses");
}

// Four 4-instruction blocks at 0x1000..0x4000 (lines 0x100..0x400) plus a
// fifth at 0x5000, in a 4-way fully-associative cache. Sequence
// id0 id1 id2 id3 id0 id1 id2 id4 id3: the fifth line evicts something; LRU
// drops line 0x400 (stale since fill), the PLRU tree drops a different way,
// so the following id3 demand hits under PLRU and misses under LRU.
ProfileStats make_plru_profile() {
    ProfileStats s;
    const uint32_t pcs[5] = {0x1000, 0x2000, 0x3000, 0x4000, 0x5000};
    const uint32_t order[9] = {0, 1, 2, 3, 0, 1, 2, 4, 3};
    for (const uint32_t k : order) {
        const uint32_t id = s.record_block_entry(pcs[k]);
        for (int i = 0; i < 4; ++i) {
            s.record_block_instruction(id);
            s.record_instruction(pcs[k] + i);
        }
    }
    return s;
}

void test_plru() {
    const ProfileStats s = make_plru_profile();

    ICacheConfig lru;
    lru.sets = 1;
    lru.ways = 4;
    const ICacheResult lr = simulate_icache(s, lru);
    expect(lr.misses == 6 && lr.hits == 3, "LRU: the re-demand of 0x400 misses");

    ICacheConfig plru;
    plru.sets = 1;
    plru.ways = 4;
    plru.policy = Replacement::PLRU;
    const ICacheResult pr = simulate_icache(s, plru);
    expect(pr.misses == 5 && pr.hits == 4, "PLRU: a different way was evicted");
    expect(pr.conflict_misses == 0 && pr.evictions == 1, "PLRU evicts once");
    expect(lr.misses == pr.misses + 1, "PLRU evicts a less useful line here");
}

// Two adjacent lines: 0x1000 (line 0x100) then 0x1010 (line 0x101). Next-line
// prefetch turns the second block's first demand into a hit.
ProfileStats make_prefetch_profile() {
    ProfileStats s;
    const uint32_t pcs[2] = {0x1000, 0x1010};
    for (int k = 0; k < 2; ++k) {
        const uint32_t id = s.record_block_entry(pcs[k]);
        for (int i = 0; i < 4; ++i) {
            s.record_block_instruction(id);
            s.record_instruction(pcs[k] + i);
        }
    }
    return s;
}

void test_prefetch() {
    const ProfileStats s = make_prefetch_profile();

    ICacheConfig plain;
    plain.sets = 8;
    plain.ways = 1;
    const ICacheResult base = simulate_icache(s, plain);
    expect(base.misses == 2 && base.prefetches == 0, "no prefetch: both demand misses");

    ICacheConfig pf;
    pf.sets = 8;
    pf.ways = 1;
    pf.prefetch = true;
    const ICacheResult t = simulate_icache(s, pf);
    expect(t.misses == 1 && t.hits == 1 && t.prefetches == 1,
           "prefetch hides the second line's demand miss");
}

void test_component() {
    expect(component_names("icache").size() == 4, "four icache components registered");

    auto fa = make_component("icache-fa");
    expect(fa != nullptr && fa->type() == "icache", "icache-fa registers");
    expect(make_component("icache-setassoc") != nullptr, "icache-setassoc registers");
    expect(make_component("icache-plru") != nullptr, "icache-plru registers");
    expect(make_component("icache-prefetch") != nullptr, "icache-prefetch registers");
    expect(make_component("icache-nope") == nullptr, "unknown cache rejected");

    expect(fa->parameters().size() == 3, "fa exposes miss-penalty, line-size, ways");

    // PLRU restricts ways to a power of two.
    auto plru = make_component("icache-plru");
    std::string error;
    expect(!plru->set_parameter("ways", "3", error), "PLRU ways must be a power of two");
    expect(plru->set_parameter("ways", "8", error), "PLRU power-of-two ways accepted");

    // The shared cost answer for the comparison table.
    Interpreter interp(build_program());
    (void)interp.run();
    RunContext ctx;
    ctx.profile_stats = &interp.profile_stats();
    auto* fa_comp = dynamic_cast<ICacheComponent*>(fa.get());
    expect(fa_comp != nullptr, "icache-fa is an ICacheComponent");
    const std::optional<CycleCost> cc = fa_comp->cycle_cost(ctx);
    expect(cc.has_value(), "icache provides a cost answer");
    expect(cc->total_cycles == 116 && cc->baseline_cycles == 416,
           "fa cost: 116 vs 416 no-cache");
    expect(cc->baseline_name == "no instruction cache", "cost baseline named");
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
    test_fa();
    test_conflict();
    test_plru();
    test_prefetch();
    test_component();
    test_reset();

    std::printf("icache test: all passed\n");
    return 0;
}
