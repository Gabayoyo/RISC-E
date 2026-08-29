#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LoadedSegment {
    uint32_t vaddr;   // virtual address of the segment
    uint32_t size;    // memory size (p_memsz), zero-filled beyond file data
    uint32_t flags;   // p_flags (PF_X / PF_W / PF_R)
    std::vector<uint8_t> data;
};

struct LoadedElf {
    uint32_t entry = 0;
    uint32_t end_vaddr = 0;   // highest virtual address loaded, rounded per segment end
    std::vector<LoadedSegment> segments;
};

// Load a 32-bit little-endian RISC-V ELF executable.
// Throws std::runtime_error on any failure.
LoadedElf load_elf(const std::string& path);
