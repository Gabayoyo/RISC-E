// loader.cpp
#include "loader.hpp"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>

// ELF32 structures (packed, little‑endian)
#pragma pack(push, 1)
struct Elf32_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};
#pragma pack(pop)

static constexpr uint16_t ET_EXEC  = 2;
static constexpr uint16_t EM_RISCV = 243;
static constexpr uint32_t PT_LOAD  = 1;

// read the entire file into a vector of bytes
static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open file: " + path);

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
        throw std::runtime_error("failed to read file: " + path);

    return buffer;
}

LoadedElf load_elf(const std::string& path) {
    auto file_data = read_file(path);
    if (file_data.size() < sizeof(Elf32_Ehdr))
        throw std::runtime_error("file too small to be an ELF");

    const auto* ehdr = reinterpret_cast<const Elf32_Ehdr*>(file_data.data());

    // Magic
    if (std::memcmp(ehdr->e_ident, "\x7f""ELF", 4) != 0)
        throw std::runtime_error("not an ELF file");

    // 32‑bit, little‑endian
    if (ehdr->e_ident[4] != 1)   // ELFCLASS32
        throw std::runtime_error("only 32‑bit ELF supported");
    if (ehdr->e_ident[5] != 1)   // ELFDATA2LSB
        throw std::runtime_error("only little‑endian ELF supported");

    // Executable and RISC‑V
    if (ehdr->e_type != ET_EXEC)
        throw std::runtime_error("only ET_EXEC files supported");
    if (ehdr->e_machine != EM_RISCV)
        throw std::runtime_error("only RISC‑V machine type supported");

    uint32_t phoff  = ehdr->e_phoff;
    uint16_t phnum  = ehdr->e_phnum;
    uint16_t phsize = ehdr->e_phentsize;
    if (phoff + static_cast<uint32_t>(phnum) * phsize > file_data.size())
        throw std::runtime_error("program headers beyond file size");

    // Find bounds of PT_LOAD segments
    uint32_t min_vaddr = UINT32_MAX;
    uint32_t max_vaddr = 0;

    for (uint16_t i = 0; i < phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf32_Phdr*>(
            file_data.data() + phoff + i * phsize);
        if (ph->p_type != PT_LOAD) continue;

        uint32_t seg_start = ph->p_vaddr;
        uint32_t seg_end   = ph->p_vaddr + ph->p_memsz;
        min_vaddr = std::min(min_vaddr, seg_start);
        max_vaddr = std::max(max_vaddr, seg_end);
    }

    if (min_vaddr == UINT32_MAX)
        throw std::runtime_error("no loadable segments found");

    uint32_t total_size = max_vaddr - min_vaddr;
    if (total_size == 0)
        throw std::runtime_error("total segment size is zero");

    // Allocate flat memory, zero‑initialised
    std::vector<uint8_t> memory(total_size, 0);

    // Copy each PT_LOAD segment
    for (uint16_t i = 0; i < phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf32_Phdr*>(
            file_data.data() + phoff + i * phsize);
        if (ph->p_type != PT_LOAD) continue;

        uint32_t offset  = ph->p_offset;
        uint32_t vaddr   = ph->p_vaddr;
        uint32_t filesz  = ph->p_filesz;
        uint32_t memsz   = ph->p_memsz;

        if (offset + filesz > file_data.size())
            throw std::runtime_error("segment data beyond file");

        uint8_t* dest = memory.data() + (vaddr - min_vaddr);
        std::memcpy(dest, file_data.data() + offset, filesz);
        if (memsz > filesz)
            std::memset(dest + filesz, 0, memsz - filesz);
    }

    LoadedElf result;
    result.memory     = std::move(memory);
    result.entry      = ehdr->e_entry;
    result.base_vaddr = min_vaddr;
    result.end_vaddr  = max_vaddr;
    return result;
}