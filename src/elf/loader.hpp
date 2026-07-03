#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct LoadedElf {
    std::vector<uint8_t> memory;   // loaded segments, base = base_vaddr
    uint64_t entry         = 0;
    uint64_t base_vaddr    = 0;    // lowest virtual address loaded
    uint64_t end_vaddr     = 0;    // base_vaddr + memory.size()
};

// Load a 64‑bit little‑endian RISC‑V ELF executable.
// Throws std::runtime_error on any failure.
LoadedElf load_elf(const std::string& path);