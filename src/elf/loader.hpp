#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <utility>

struct ElfSymbol {
    std::string name;
    uint32_t    address;
    uint32_t    size;
    uint8_t     bind;   // e.g., STB_GLOBAL
    uint8_t     type;   // e.g., STT_FUNC
};

struct LoadedSegment {
    uint32_t             vaddr;
    uint32_t             size;    // memsz
    uint32_t             flags;   // p_flags
    std::vector<uint8_t> data;    // zero‑extended to memsz
};

struct LoadedElf {
    std::vector<uint8_t> memory;   // loaded segments, base = base_vaddr
    uint64_t entry         = 0;
    uint64_t base_vaddr    = 0;    // lowest virtual address loaded
    uint64_t end_vaddr     = 0;    // base_vaddr + memory.size()
    std::vector<ElfSymbol> symbols; // all symbols from .symtab
    std::vector<std::pair<uint32_t, uint32_t>> instructions;
    std::vector<LoadedSegment> segments; // all PT_LOAD segments
};

// Load a 64‑bit little‑endian RISC‑V ELF executable.
// Throws std::runtime_error on any failure.
LoadedElf load_elf(const std::string& path);