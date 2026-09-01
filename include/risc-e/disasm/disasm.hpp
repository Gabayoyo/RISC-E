#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct DecodedInstruction;
struct ICacheStats;
class MemoryInterface;
struct LoadedSegment;

// One basic block with its static size, derived from the run's execution
// profile: the block spans [entry_pc, next_entry_pc), so its size is the
// number of distinct executed instructions in that range. No decode or memory
// access is needed — the profile already knows which instructions ran.
struct BlockStat {
    uint32_t entry_pc = 0;
    uint32_t static_size = 0;        // instructions in the block
    uint64_t executions = 0;         // dynamic: times control entered the block
    uint64_t dynamic_instructions = 0;
};

// Formats one decoded instruction as RISC-V assembly text (ABI register
// names, resolved branch/jump targets, "illegal (0x........)" for invalid
// encodings). Mirrors the interpreter's validity rules.
std::string disassemble_instruction(const DecodedInstruction& d);

// Objdump-style static listing of the executable segments: one line per
// instruction, with "=>" marking basic-block entry points recorded in
// `profile` (may be null) and a "; block N, xM" annotation of the entry's
// dynamic execution count.
void print_disassembly(std::ostream& out, const std::vector<LoadedSegment>& segments,
                       MemoryInterface& mem, const ICacheStats* profile);

// Per-block table for the verbose report, sorted by entry PC.
std::vector<BlockStat> build_block_table(const ICacheStats& stats);

// The verbose report's block table: entry, size, executions, dynamic
// instruction count, and the share of the run's dynamic instructions.
void print_block_table(std::ostream& out, const std::vector<BlockStat>& blocks,
                       uint64_t total_instructions);
