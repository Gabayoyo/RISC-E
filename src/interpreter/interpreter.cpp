#include "interpreter.hpp"
#include "src/decoder/decoder.hpp"
#include "src/ir/ir.hpp"

#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <utility>

Interpreter::Interpreter(LoadedElf elf)
    : memory_(std::move(elf.memory)),
      base_vaddr_(elf.base_vaddr),
      pc_(elf.entry),
      entry_(elf.entry)
{
    reset();
}

void Interpreter::load(const IRModule& module)
{
    // ── Pass 1: decode every instruction, record address → flat index ──────
    std::vector<std::shared_ptr<Operation>> flat;
    std::unordered_map<uint32_t, uint32_t>  addrToIndex; // addr → flat idx

    for (const auto& fn : module.functions) {
        for (const auto& bb : fn->blocks) {
            for (auto& instr : bb->instructions) {
                // Re-decode from the raw word so OpBuilder can produce the
                // concrete subclass (including any JumpPatchable ones).
                uint32_t idx = static_cast<uint32_t>(flat.size());
                addrToIndex[instr->addr] = idx;

                flat.push_back(instr); // clone to get a unique_ptr
            }
        }
    }

    // ── Pass 2: rewrite branch/jump targets from addresses to flat indices ──
    for (auto& op : flat) {
        if (auto* p = dynamic_cast<BType*>(op.get())) {
            uint32_t targetAddr = p->rawTarget;
            auto it = addrToIndex.find(targetAddr);
            if (it == addrToIndex.end()) {
                throw std::runtime_error(
                    "flattenIRModule: branch target 0x"
                    + std::to_string(targetAddr)
                    + " has no corresponding instruction in the module");
            }
            p->targetIndex = static_cast<int32_t>(it->second);
        }

        if (auto* p = dynamic_cast<JType*>(op.get())) {
            uint32_t targetAddr = p->rawTarget;
            auto it = addrToIndex.find(targetAddr);
            if (it == addrToIndex.end()) {
                throw std::runtime_error(
                    "flattenIRModule: jal target 0x"
                    + std::to_string(targetAddr)
                    + " has no corresponding instruction in the module");
            }
            p->targetIndex = static_cast<int32_t>(it->second);
        }
    }

    code_ = std::move(flat);
}


void Interpreter::reset() {
    std::memset(regs_, 0, sizeof(regs_));
    pc_ = entry_;
}

uint32_t Interpreter::get_register(int idx) const {
    if (idx == 0) return 0;
    return regs_[idx];
}

uint32_t Interpreter::fetch_instruction(uint32_t vaddr) const {
    uint32_t offset = vaddr - base_vaddr_;
    if (offset + 4 > memory_.size())
        throw std::runtime_error("instruction fetch out of bounds");
    // Little‑endian read of 4 bytes
    return memory_[offset] |
           (memory_[offset+1] << 8) |
           (memory_[offset+2] << 16) |
           (memory_[offset+3] << 24);
}

uint32_t Interpreter::load_memory(uint32_t vaddr, int size_bytes) {
    // size_bytes should be 1, 2, or 4 for RISC-V loads
    if (size_bytes <= 0 || size_bytes > 4)
        return 0;

    uint64_t offset = static_cast<uint64_t>(vaddr) - base_vaddr_;
    if (offset + size_bytes > memory_.size())
        return 0;  // out of bounds: return 0 for now (could trap instead)

    uint32_t value = 0;
    for (int i = 0; i < size_bytes; ++i)
        value |= static_cast<uint32_t>(memory_[offset + i]) << (i * 8);
    return value;
}

void Interpreter::store_memory(uint32_t vaddr, int size_bytes, uint32_t value) {
    if (size_bytes <= 0 || size_bytes > 4)
        return;

    uint64_t offset = static_cast<uint64_t>(vaddr) - base_vaddr_;
    if (offset + size_bytes > memory_.size())
        return;  // out of bounds: ignore or trap

    for (int i = 0; i < size_bytes; ++i)
        memory_[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

void Interpreter::handle_ecall() {
    // Minimal Linux syscall interface: a7 = syscall number, a0 = arg
    // Stop interpreter? For now throw or set a flag.
    throw std::runtime_error("ecall encountered – syscall not implemented");
}

void Interpreter::step() {
}

void Interpreter::run() {
    // TODO: Implement a loop that fetches, decodes, and executes instructions until an ECALL or error occurs.
    while (true) {
        step();
    }
}