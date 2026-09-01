#include "risc-e/disasm/disasm.hpp"

#include "risc-e/component/icache/icache_stats.hpp"
#include "risc-e/decoder/decoded_instruction.hpp"
#include "risc-e/decoder/decoder.hpp"
#include "risc-e/decoder/opcodes.hpp"
#include "risc-e/elf/loader.hpp"
#include "risc-e/memory/memory_interface.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace opc = opcode;

namespace {

constexpr uint32_t PF_X = 0x1;  // ELF p_flags: executable

// ABI names, in register number order (x0..x31).
const char* kRegNames[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1",
    "a2",   "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

std::string reg(uint8_t idx) {
    return idx < 32 ? kRegNames[idx] : "x" + std::to_string(idx);
}

std::string illegal(const DecodedInstruction& d) {
    std::ostringstream out;
    out << "illegal (0x" << std::hex << std::setw(8) << std::setfill('0') << d.raw << ')';
    return out.str();
}

std::string hex8(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

// R-type: mnemonic rd, rs1, rs2.
std::string format_r(const char* mnemonic, const DecodedInstruction& d) {
    return std::string(mnemonic) + " " + reg(d.rd) + ", " + reg(d.rs1) + ", " + reg(d.rs2);
}

// I-type ALU: mnemonic rd, rs1, imm.
std::string format_i(const char* mnemonic, const DecodedInstruction& d) {
    return std::string(mnemonic) + " " + reg(d.rd) + ", " + reg(d.rs1) + ", " +
           std::to_string(d.imm);
}

// Shift: mnemonic rd, rs1, shamt (the shamt field rides in rs2).
std::string format_shift(const char* mnemonic, const DecodedInstruction& d) {
    return std::string(mnemonic) + " " + reg(d.rd) + ", " + reg(d.rs1) + ", " +
           std::to_string(d.rs2 & 0x1F);
}

// Load/store with offset: lw rd, imm(rs1) / sw rs2, imm(rs1).
std::string format_offset(const char* mnemonic, const DecodedInstruction& d, bool is_store) {
    return std::string(mnemonic) + " " + reg(is_store ? d.rs2 : d.rd) + ", " +
           std::to_string(d.imm) + "(" + reg(d.rs1) + ")";
}

// Branch: mnemonic rs1, rs2, target (resolved to an absolute address).
std::string format_branch(const char* mnemonic, const DecodedInstruction& d) {
    return std::string(mnemonic) + " " + reg(d.rs1) + ", " + reg(d.rs2) + ", " +
           hex8(static_cast<uint32_t>(d.addr + d.imm));
}

// U-type: lui/auipc print the 20-bit immediate field.
std::string format_u(const char* mnemonic, const DecodedInstruction& d) {
    std::ostringstream out;
    out << mnemonic << " " << reg(d.rd) << ", 0x" << std::hex
        << (static_cast<uint32_t>(d.imm) >> 12);
    return out.str();
}

} // namespace

std::string disassemble_instruction(const DecodedInstruction& d) {
    switch (d.opcode) {
        case opc::kOp:
            switch (d.funct3) {
                case F3_ADD_SUB:
                    return format_r(d.funct7 == F7_SUB_SRA ? "sub" : "add", d);
                case F3_SLL:     return format_r("sll", d);
                case F3_SLT:     return format_r("slt", d);
                case F3_SLTU:    return format_r("sltu", d);
                case F3_XOR:     return format_r("xor", d);
                case F3_SRL_SRA: return format_r(d.funct7 == F7_SUB_SRA ? "sra" : "srl", d);
                case F3_OR:      return format_r("or", d);
                case F3_AND:     return format_r("and", d);
                default:         return illegal(d);
            }

        case opc::kOpImm:
            switch (d.funct3) {
                case F3_ADDI:  return format_i("addi", d);
                case F3_SLLI:
                    return d.funct7 == F7_SLLI ? format_shift("slli", d) : illegal(d);
                case F3_SLTI:  return format_i("slti", d);
                case F3_SLTIU: return format_i("sltiu", d);
                case F3_XORI:  return format_i("xori", d);
                case F3_SRLI:
                    if (d.funct7 == F7_SRAI) return format_shift("srai", d);
                    if (d.funct7 == F7_SLLI) return format_shift("srli", d);
                    return illegal(d);
                case F3_ORI:   return format_i("ori", d);
                case F3_ANDI:  return format_i("andi", d);
                default:       return illegal(d);
            }

        case opc::kLoad:
            switch (d.funct3) {
                case F3_LB:  return format_offset("lb", d, false);
                case F3_LH:  return format_offset("lh", d, false);
                case F3_LW:  return format_offset("lw", d, false);
                case F3_LBU: return format_offset("lbu", d, false);
                case F3_LHU: return format_offset("lhu", d, false);
                default:     return illegal(d);
            }

        case opc::kStore:
            switch (d.funct3) {
                case F3_SB: return format_offset("sb", d, true);
                case F3_SH: return format_offset("sh", d, true);
                case F3_SW: return format_offset("sw", d, true);
                default:    return illegal(d);
            }

        case opc::kBranch:
            switch (d.funct3) {
                case F3_BEQ:  return format_branch("beq", d);
                case F3_BNE:  return format_branch("bne", d);
                case F3_BLT:  return format_branch("blt", d);
                case F3_BGE:  return format_branch("bge", d);
                case F3_BLTU: return format_branch("bltu", d);
                case F3_BGEU: return format_branch("bgeu", d);
                default:      return illegal(d);
            }

        case opc::kLui:   return format_u("lui", d);
        case opc::kAuipc: return format_u("auipc", d);

        case opc::kJal:
            return "jal " + reg(d.rd) + ", " + hex8(static_cast<uint32_t>(d.addr + d.imm));

        case opc::kJalr:
            return d.funct3 == 0 ? format_offset("jalr", d, false) : illegal(d);

        case opc::kMiscMem:
            switch (d.funct3) {
                case 0: return "fence";
                case 1: return "fence.i";
                default: return illegal(d);
            }

        case opc::kSystem: {
            // ECALL/EBREAK require funct3 == 0, rd == 0 and rs1 == 0 (as in
            // the interpreter); the immediate carries the function number.
            const bool valid_env = d.funct3 == 0 && d.rd == 0 && d.rs1 == 0;
            if (valid_env && d.imm == 0) return "ecall";
            if (valid_env && d.imm == 1) return "ebreak";
            return illegal(d);
        }

        default:
            return illegal(d);
    }
}

void print_disassembly(std::ostream& out, const std::vector<LoadedSegment>& segments,
                       MemoryInterface& mem, const ICacheStats* profile) {
    std::unordered_set<uint32_t> entries;
    std::unordered_map<uint32_t, uint32_t> entry_to_id;
    if (profile != nullptr) {
        for (std::size_t id = 0; id < profile->blocks.size(); ++id) {
            const uint32_t pc = profile->blocks[id].entry_pc;
            entries.insert(pc);
            entry_to_id[pc] = static_cast<uint32_t>(id);
        }
    }

    for (const LoadedSegment& seg : segments) {
        if ((seg.flags & PF_X) == 0) continue;

        // The first loadable segment usually starts at the file's byte 0, so
        // its first bytes are the ELF header and program headers, not code.
        // When the segment begins with the ELF magic, skip that container
        // region (sizes read from the header itself) so the listing starts at
        // the first real instruction.
        uint32_t walk_start = seg.vaddr;
        if (seg.data.size() >= 52 && seg.data[0] == '\x7f' && seg.data[1] == 'E' &&
            seg.data[2] == 'L' && seg.data[3] == 'F') {
            const uint32_t phoff =
                seg.data[28] | (seg.data[29] << 8) | (seg.data[30] << 16) |
                (seg.data[31] << 24);
            const uint16_t phentsize = seg.data[42] | (seg.data[43] << 8);
            const uint16_t phnum = seg.data[44] | (seg.data[45] << 8);
            const uint32_t skip = phoff + static_cast<uint32_t>(phentsize) * phnum;
            if (skip < seg.size) walk_start = seg.vaddr + skip;
        }

        out << "text segment: 0x" << std::hex << std::setw(8) << std::setfill('0')
            << seg.vaddr << " .. 0x" << std::hex << std::setw(8) << std::setfill('0')
            << (seg.vaddr + seg.size) << " (" << std::dec << seg.size << " bytes)\n";

        // Trim the zero padding that pads the code to its alignment: it would
        // otherwise list as pages of "illegal (0x00000000)" before the first
        // real instruction.
        uint32_t first_pc = 0;
        uint32_t last_pc = 0;
        for (uint32_t pc = walk_start; pc + 4 <= seg.vaddr + seg.size; pc += 4) {
            if (mem.fetch32(pc) != 0) {
                if (first_pc == 0) first_pc = pc;
                last_pc = pc;
            }
        }
        if (first_pc == 0) continue;  // nothing but padding

        for (uint32_t pc = first_pc; pc <= last_pc; pc += 4) {
            const DecodedInstruction d = decode_raw_inst(mem.fetch32(pc), pc);
            const auto it = entries.find(pc);
            out << (it != entries.end() ? "=> " : "   ") << hex8(pc) << ":  "
                << disassemble_instruction(d);
            if (it != entries.end()) {
                const BlockInfo& b = profile->blocks[entry_to_id[pc]];
                out << "   ; block " << entry_to_id[pc] << ", x" << b.executions;
            }
            out << '\n';
        }
    }
}

std::vector<BlockStat> build_block_table(const ICacheStats& stats) {
    std::vector<uint32_t> entries;
    entries.reserve(stats.blocks.size());
    for (const BlockInfo& b : stats.blocks) entries.push_back(b.entry_pc);
    std::sort(entries.begin(), entries.end());

    std::vector<BlockStat> out;
    out.reserve(stats.blocks.size());
    for (const BlockInfo& b : stats.blocks) {
        const auto next = std::upper_bound(entries.begin(), entries.end(), b.entry_pc);
        const uint32_t limit = next == entries.end() ? 0xFFFFFFFFu : *next;
        uint32_t size = 0;
        for (const uint32_t pc : stats.seen_pcs) {
            if (pc >= b.entry_pc && pc < limit) ++size;
        }
        out.push_back(BlockStat{b.entry_pc, size, b.executions, b.instructions});
    }
    std::sort(out.begin(), out.end(),
              [](const BlockStat& a, const BlockStat& b) { return a.entry_pc < b.entry_pc; });
    return out;
}

void print_block_table(std::ostream& out, const std::vector<BlockStat>& blocks,
                       uint64_t total_instructions) {
    out << "    entry      size  execs  dyn-insts    %dyn\n";
    for (const BlockStat& b : blocks) {
        const double pct =
            total_instructions == 0
                ? 0.0
                : 100.0 * static_cast<double>(b.dynamic_instructions) /
                      static_cast<double>(total_instructions);
        std::ostringstream pct_s;
        pct_s << std::fixed << std::setprecision(2) << pct;
        out << "    " << hex8(b.entry_pc) << "  " << std::setw(4) << b.static_size << "  "
            << std::setw(5) << b.executions << "  " << std::setw(9) << b.dynamic_instructions
            << "  " << pct_s.str() << "%\n";
    }
}
