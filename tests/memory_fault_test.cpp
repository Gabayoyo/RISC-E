#include "risc-e/interpreter/interpreter.hpp"
#include "risc-e/memory/physical_memory.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

class FakeTrapSink : public TrapSink {
public:
    TrapCause last_cause = static_cast<TrapCause>(0);
    uint32_t last_value = 0;
    bool fired = false;

    void raise_trap(TrapCause cause, uint32_t value = 0) override {
        fired = true;
        last_cause = cause;
        last_value = value;
    }
};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

void test_physical_memory_faults() {
    FakeTrapSink sink;
    PhysicalMemory mem;
    mem.set_trap_sink(&sink);

    // Unmapped read faults (returns 0, raises LOAD_FAULT)
    expect(mem.load8(0) == 0, "unmapped read should return 0");
    expect(sink.fired && sink.last_cause == TrapCause::LOAD_FAULT, "unmapped read should raise LOAD_FAULT");
    sink.fired = false;

    // Unmapped write faults (raises STORE_FAULT)
    mem.store8(0, 0x55);
    expect(sink.fired && sink.last_cause == TrapCause::STORE_FAULT, "unmapped write should raise STORE_FAULT");
    sink.fired = false;

    // Mapped region round-trips
    mem.map_region(0x1000, 4096);
    mem.store8(0x1000, 0x42);
    expect(mem.load8(0x1000) == 0x42, "mapped byte should round-trip");
    mem.store32(0x1004, 0xDEADBEEF);
    expect(mem.load32(0x1004) == 0xDEADBEEF, "mapped word should round-trip");

    // Mapping allocates whole pages; the rest of the page is zero-filled
    mem.map_region(0x2004, 8);
    mem.store8(0x2004, 7);
    expect(mem.load8(0x2004) == 7, "partial page access should work");
    expect(mem.load8(0x2000) == 0, "rest of the mapped page should be zero");
    expect(!sink.fired, "mapped page access should not fault");

    // Misaligned loads fault
    mem.load32(0x1001);
    expect(sink.fired && sink.last_cause == TrapCause::LOAD_MISALIGNED, "misaligned load should fault");
    sink.fired = false;

    // Stack-like region below 0x80000000
    mem.map_region(0x7F800000, 0x800000);
    mem.store32(0x7FFFFFFC, 1234);
    expect(mem.load32(0x7FFFFFFC) == 1234, "stack region access should work");
    expect(!sink.fired, "mapped stack access should not fault");
}

void test_interpreter_mapping() {
    LoadedElf elf;
    elf.entry     = 0x10000;
    elf.end_vaddr = 0x10004;

    LoadedSegment seg;
    seg.vaddr = 0x10000;
    seg.size  = 4;
    seg.data  = {0x13, 0x00, 0x00, 0x00};  // NOP
    elf.segments.push_back(seg);

    Interpreter interp(std::move(elf));
    MemoryInterface& mem = interp.memory();

    expect(mem.load8(0x10000) == 0x13, "ELF segment should be mapped");
    mem.store32(0x7FFFFFFC, 9);
    expect(mem.load32(0x7FFFFFFC) == 9, "stack region should be mapped");
}

} // namespace

int main() {
    test_physical_memory_faults();
    test_interpreter_mapping();

    std::printf("memory fault tests passed\n");
    return 0;
}
