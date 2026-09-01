#include "risc-e/disasm/disasm.hpp"

#include "risc-e/component/icache/icache_stats.hpp"
#include "risc-e/decoder/decoder.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/memory/physical_memory.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Assembles one RV32I instruction by hand and returns its disassembly at
// `addr`. Keeps the tests independent of a cross-assembler.
std::string disasm(uint32_t word, uint32_t addr) {
    return disassemble_instruction(decode_raw_inst(word, addr));
}

void test_mnemonics() {
    expect(disasm(0xff010113, 0x10000) == "addi sp, sp, -16", "addi with negative imm");
    expect(disasm(0x00208463, 0x10004) == "beq ra, sp, 0x0001000c",
           "branch target resolved to absolute address");
    expect(disasm(0x008000ef, 0x1000c) == "jal ra, 0x00010014", "jal target");
    expect(disasm(0x00008067, 0x10010) == "jalr zero, 0(ra)", "jalr offset form");
    expect(disasm(0x0005a503, 0x10014) == "lw a0, 0(a1)", "load offset form");
    expect(disasm(0x02112023, 0x10018) == "sw ra, 32(sp)", "store offset form");
    expect(disasm(0x00111093, 0x1001c) == "slli ra, sp, 1", "shift immediate");
    expect(disasm(0x40115093, 0x10020) == "srai ra, sp, 1", "srai funct7");
    expect(disasm(0x00005537, 0x10024) == "lui a0, 0x5", "lui 20-bit immediate");
    expect(disasm(0x00000073, 0x10028) == "ecall", "ecall");
    expect(disasm(0x00100073, 0x1002c) == "ebreak", "ebreak");
    expect(disasm(0x00000000, 0x10030) == "illegal (0x00000000)", "illegal encoding");
}

void test_block_table() {
    ICacheStats stats;
    stats.blocks.push_back(BlockInfo{0x10000, 3, 9});  // entry, execs, dyn insts
    stats.blocks.push_back(BlockInfo{0x1000c, 2, 4});
    stats.seen_pcs = {0x10000, 0x10004, 0x10008, 0x1000c, 0x10010};

    const std::vector<BlockStat> blocks = build_block_table(stats);
    expect(blocks.size() == 2, "one row per block");
    expect(blocks[0].entry_pc == 0x10000 && blocks[0].static_size == 3,
           "block size counts executed PCs before the next entry");
    expect(blocks[1].entry_pc == 0x1000c && blocks[1].static_size == 2,
           "last block counts executed PCs to the end");
    expect(blocks[0].executions == 3 && blocks[0].dynamic_instructions == 9,
           "dynamic counts carried through");

    std::ostringstream out;
    print_block_table(out, blocks, 13);
    expect(out.str().find("0x00010000") != std::string::npos, "block table lists entries");
    expect(out.str().find("100.00%") == std::string::npos, "percentages are per-block");
}

void test_print_disassembly() {
    PhysicalMemory mem;
    mem.map_region(0x10000, 32);
    // Zero padding first (the page-alignment gap a real ELF would have)…
    mem.store32(0x10000, 0x00000000);
    mem.store32(0x10004, 0x00000000);
    // …then code.
    mem.store32(0x10008, 0xff010113);  // addi sp, sp, -16
    mem.store32(0x1000c, 0x008000ef);  // jal ra, 8
    mem.store32(0x10010, 0x00000073);  // ecall
    mem.store32(0x10014, 0x00008067);  // jalr zero, 0(ra)
    mem.store32(0x10018, 0x00000000);  // trailing padding

    ICacheStats stats;
    stats.blocks.push_back(BlockInfo{0x10008, 1, 2});  // entry PC is a block start
    stats.pc_to_id[0x10008] = 0;

    std::vector<LoadedSegment> segments;
    segments.push_back(LoadedSegment{0x10000, 32, 0x1, {}});  // PF_X

    std::ostringstream out;
    print_disassembly(out, segments, mem, &stats);
    const std::string text = out.str();
    expect(text.find("0x00010000:") == std::string::npos, "leading zero padding trimmed");
    expect(text.find("0x00010004:") == std::string::npos, "padding word not listed");
    expect(text.find("0x00010018:") == std::string::npos, "trailing zero padding trimmed");
    expect(text.find("=> 0x00010008") != std::string::npos, "block start marked");
    expect(text.find("addi sp, sp, -16") != std::string::npos, "instruction text present");
    expect(text.find("jalr zero, 0(ra)") != std::string::npos, "walk covers the segment");
    expect(text.find("; block 0, x1") != std::string::npos, "entry annotated with exec count");
}

} // namespace

int main() {
    test_mnemonics();
    test_block_table();
    test_print_disassembly();
    std::puts("ok");
    return 0;
}
