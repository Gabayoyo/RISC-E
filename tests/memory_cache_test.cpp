#include "risc-e/component/dcache/implementations/l1l2_cache.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/component/registry.hpp"
#include "risc-e/component/run_context.hpp"
#include "risc-e/interpreter/interpreter.hpp"
#include "risc-e/component/dcache/dcache_stats.hpp"
#include "risc-e/component/dcache/dcache.hpp"
#include "risc-e/component/dcache/replacement.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Builds an access trace of 4-byte accesses from (kind, address) pairs.
DCacheStats make_trace(
    std::initializer_list<std::pair<DCacheKind, uint32_t>> accesses) {
    DCacheStats tr;
    for (const auto& [kind, addr] : accesses) {
        tr.record(kind, addr, 4);
    }
    return tr;
}

// Small geometry shared by the direct simulation tests: 2 sets x 2 ways.
DCacheConfig base_config() {
    DCacheConfig c;
    c.sets = 2;
    c.ways = 2;
    return c;
}

// A hit on a resident line is cheap; a miss costs the memory round trip.
void test_basic() {
    DCacheConfig c = base_config();
    const DCacheStats tr = make_trace({// two lines, each loaded twice
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Load, 0x20},
                                       {DCacheKind::Load, 0x20}});
    const DCacheResult t = simulate_dcache(tr, c);
    expect(t.accesses == 4 && t.loads == 4 && t.stores == 0, "4 loads");
    expect(t.hits == 2 && t.misses == 2, "2 hits, 2 compulsory misses");
    expect(t.compulsory_misses == 2 && t.conflict_misses == 0 && t.capacity_misses == 0,
           "both misses compulsory");
    expect(t.evictions == 0 && t.writebacks == 0, "nothing evicted");
    expect(t.miss_stalls == 200 && t.total_cycles == 202, "hits x 1 + 2 x 100");
    expect(t.baseline_cycles == 400 && t.saved_cycles == 198,
           "saved vs no data cache");
    expect(t.hit_rate == 50.0, "hit rate 50%");
}

// Direct-mapped (ways == 1): two lines in the same set evict each other, so
// the re-demand of the first line is a conflict miss.
void test_conflict() {
    DCacheConfig c = base_config();
    c.ways = 1;
    const DCacheStats tr = make_trace({// 0x10 and 0x30 both map to set 1
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Load, 0x30},
                                       {DCacheKind::Load, 0x10}});
    const DCacheResult t = simulate_dcache(tr, c);
    expect(t.misses == 3 && t.hits == 0, "three misses in a thrashing set");
    expect(t.compulsory_misses == 2 && t.conflict_misses == 1,
           "2 compulsory + 1 conflict miss");
    expect(t.evictions == 2, "every refill evicts in a direct-mapped set");
    expect(t.total_cycles == 300 && t.baseline_cycles == 300,
           "no cache benefit on a pure thrash");
}

// A store to a resident line: write-through absorbs stores into the write
// buffer (depth 4 here, so a short trace never fills it) while write-back
// only dirties the line and defers the cost to eviction.
void test_write_policies() {
    DCacheConfig wt = base_config();  // write-through, no-write-allocate
    const DCacheStats tr = make_trace({// store, load, store of the same line
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Store, 0x10}});

    const DCacheResult wt_r = simulate_dcache(tr, wt);
    expect(wt_r.hits == 1 && wt_r.misses == 2, "wt: store miss (no alloc), load miss, store hit");
    expect(wt_r.compulsory_misses == 1 && wt_r.no_allocate_store_misses == 1,
           "wt: only the load refills; the store miss is a no-allocate miss");
    expect(wt_r.miss_stalls == 100 && wt_r.write_stalls == 0,
           "wt: store is buffered, not a direct memory write");
    expect(wt_r.total_cycles == 101 && wt_r.saved_cycles == 199,
           "wt: short trace never fills the write buffer");

    DCacheConfig wb = base_config();
    wb.write_policy = WritePolicy::WriteBack;
    wb.write_allocate = WriteAllocate::WriteAllocate;
    const DCacheResult wb_r = simulate_dcache(tr, wb);
    expect(wb_r.hits == 2 && wb_r.misses == 1, "wb: one refill, two hits");
    expect(wb_r.miss_stalls == 100 && wb_r.write_stalls == 0,
           "wb: stores stay in the cache");
    expect(wb_r.writebacks == 0, "wb: no dirty line evicted yet");
    expect(wb_r.total_cycles == 102 && wb_r.saved_cycles == 198,
           "wb defers all write cost");
}

// The write buffer batches stores into memory transactions: every
// buffer-depth stores costs one drain, so a store burst is amortized instead
// of paying a full memory round trip each time. Depth 1 is the unbuffered
// baseline (every store drains immediately).
void test_write_buffer() {
    DCacheConfig c = base_config();  // write-through, no-write-allocate
    c.write_buffer_depth = 2;
    const DCacheStats tr = make_trace({// four stores, all to non-resident lines
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Store, 0x10}});
    const DCacheResult t = simulate_dcache(tr, c);
    expect(t.misses == 4 && t.no_allocate_store_misses == 4,
           "all stores miss without allocating");
    expect(t.miss_stalls == 0, "stores are buffered, not served directly");
    expect(t.write_stalls == 200 && t.write_buffer_drains == 2,
           "4 stores / depth 2 = 2 memory transactions");
    expect(t.total_cycles == 200 && t.baseline_cycles == 400,
           "only the drain cost is charged");

    c.write_buffer_depth = 1;
    const DCacheResult u = simulate_dcache(tr, c);
    expect(u.write_stalls == 400 && u.write_buffer_drains == 4,
           "depth 1 = unbuffered: every store drains");
    expect(u.total_cycles == 400, "unbuffered write-through costs like no cache here");
}

// Write-back + write-allocate with a single line: every store refills the
// line and dirties it, so each eviction writes a dirty line back.
void test_writeback_eviction() {
    DCacheConfig c;
    c.sets = 1;
    c.ways = 1;
    c.write_policy = WritePolicy::WriteBack;
    c.write_allocate = WriteAllocate::WriteAllocate;
    const DCacheStats tr = make_trace({// three different lines in one slot
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Store, 0x30},
                                       {DCacheKind::Store, 0x10}});
    const DCacheResult t = simulate_dcache(tr, c);
    expect(t.misses == 3 && t.hits == 0, "every store misses in a 1-line cache");
    expect(t.compulsory_misses == 2 && t.capacity_misses == 1,
           "capacity miss on the re-demand");
    expect(t.evictions == 2 && t.writebacks == 2, "both evictions write back");
    expect(t.miss_stalls == 300 && t.writeback_stalls == 200,
           "3 refills + 2 dirty writebacks");
    expect(t.total_cycles == 500, "stall sums");
}

// Write-through + write-allocate: a store miss refills the line and the store
// joins the write buffer, so the refill is the only immediate cost.
void test_write_through_allocate() {
    DCacheConfig c = base_config();
    c.write_allocate = WriteAllocate::WriteAllocate;
    const DCacheStats tr = make_trace({// store (refill + buffered write), then load
                                       {DCacheKind::Store, 0x10},
                                       {DCacheKind::Load, 0x10}});
    const DCacheResult t = simulate_dcache(tr, c);
    expect(t.hits == 1 && t.misses == 1, "store miss refills, load hits");
    expect(t.miss_stalls == 100 && t.write_stalls == 0,
           "refill charged; the write waits in the buffer");
    expect(t.total_cycles == 101 && t.saved_cycles == 99, "one memory service");
}

// The shared replacement module answers the same victims the simulator relies
// on: LRU evicts the least recently used way, the PLRU tree walks to a leaf.
void test_replacement() {
    ReplacementState lru(4, ReplacementPolicy::LRU);
    lru.touch(0);
    lru.touch(1);
    lru.touch(2);
    lru.touch(3);
    expect(lru.victim() == 0, "LRU evicts the least recently used way");
    lru.touch(0);
    expect(lru.victim() == 1, "LRU updates after a re-touch");

    ReplacementState plru(4, ReplacementPolicy::PLRU);
    plru.touch(0);
    plru.touch(1);
    plru.touch(2);
    plru.touch(3);
    expect(plru.victim() == 0, "PLRU tree victim after touching all ways");
    plru.touch(0);
    expect(plru.victim() == 2, "PLRU tree victim changes after a re-touch");
}

// The interpreter records every successful load/store into the trace, and a
// faulting access is not recorded.
void test_interpreter_recording() {
    // 0x10000: sw a0, -4(sp)
    // 0x10004: lw t0, -4(sp)
    // 0x10008: addi a7, x0, 93
    // 0x1000C: ecall
    constexpr uint32_t kInst[] = {0xFEA12E23, 0xFFC12283, 0x05D00893, 0x00000073};
    LoadedElf elf;
    elf.entry = 0x10000;
    elf.end_vaddr = 0x10010;
    LoadedSegment seg;
    seg.vaddr = 0x10000;
    seg.size = sizeof(kInst);
    seg.data.assign(reinterpret_cast<const uint8_t*>(kInst),
                    reinterpret_cast<const uint8_t*>(kInst) + sizeof(kInst));
    elf.segments.push_back(std::move(seg));

    Interpreter interp(std::move(elf));
    const std::optional<uint32_t> exit_code = interp.run();
    expect(exit_code.has_value() && *exit_code == 0, "program exits cleanly");
    expect(interp.access_trace().stores == 1 && interp.access_trace().loads == 1,
           "one store and one load recorded");
    expect(interp.access_trace().records.size() == 2,
           "both accesses in the trace");
}

void test_trace_reset() {
    DCacheStats tr = make_trace({{DCacheKind::Load, 0x10}, {DCacheKind::Store, 0x20}});
    expect(tr.loads == 1 && tr.stores == 1 && tr.records.size() == 2, "trace populated");
    tr.reset();
    expect(tr.loads == 0 && tr.stores == 0 && tr.records.empty(), "reset clears the trace");
}

void test_component() {
    // The cache type is the L1+L2 hierarchy component.
    expect(component_names("cache").size() == 1, "one cache component registered");

    auto cache = make_component("l1-l2");
    expect(cache != nullptr && cache->type() == "cache", "l1-l2 registers as a cache");
    expect(make_component("l1-nope") == nullptr, "unknown cache rejected");

    // Ten tunables: five per level, prefixed l1-/l2-.
    expect(cache->parameters().size() == 10, "l1-l2 exposes 5 tunables per level");

    // An l2 tunable routes to the L2 level.
    auto* l1l2 = dynamic_cast<L1L2Cache*>(cache.get());
    expect(l1l2 != nullptr, "l1-l2 is an L1L2Cache");
    std::string error;
    expect(l1l2->set_parameter("l2-ways", "2", error), "l2-ways accepted");

    // The shared cost answer for the comparison table: two lines loaded twice
    // each. L1 (16x4, 16 B, hit 4) misses twice (2 x 4 cycles); both misses
    // fall in one 64 B L2 line, so L2 pays one refill (100) and one hit (14).
    // L1-only baseline: 2 hits x 4 + 2 misses x 100.
    const DCacheStats tr = make_trace({// two lines, each loaded twice
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Load, 0x10},
                                       {DCacheKind::Load, 0x20},
                                       {DCacheKind::Load, 0x20}});
    RunContext ctx;
    ctx.access_trace = &tr;
    const std::optional<CycleCost> cc = cache->cycle_cost(ctx);
    expect(cc.has_value(), "cache provides a cost answer");
    expect(cc->total_cycles == 122 && cc->baseline_cycles == 208,
           "l1-l2: 122 vs 208 L1-only");
    expect(cc->baseline_name == "L1 only", "cost baseline named");
}

} // namespace

int main() {
    test_basic();
    test_conflict();
    test_write_policies();
    test_write_buffer();
    test_writeback_eviction();
    test_write_through_allocate();
    test_replacement();
    test_interpreter_recording();
    test_trace_reset();
    test_component();

    std::printf("memory cache test: all passed\n");
    return 0;
}
