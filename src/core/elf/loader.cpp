#include "risc-e/core/elf/loader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

// 32-bit ELF header (ELFCLASS32, little-endian)
#pragma pack(push, 1)
struct Elf32_Ehdr {
    unsigned char  e_ident[16];
    uint16_t       e_type;
    uint16_t       e_machine;
    uint32_t       e_version;
    uint32_t       e_entry;
    uint32_t       e_phoff;
    uint32_t       e_shoff;
    uint32_t       e_flags;
    uint16_t       e_ehsize;
    uint16_t       e_phentsize;
    uint16_t       e_phnum;
    uint16_t       e_shentsize;
    uint16_t       e_shnum;
    uint16_t       e_shstrndx;
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

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open file: " + path);
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("file is empty: " + path);
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("could not read file: " + path);
    }
    return data;
}

static std::vector<LoadedSegment> extract_segments(const std::vector<uint8_t>& file_data, const Elf32_Ehdr* ehdr) {
    std::vector<LoadedSegment> segments;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf32_Phdr*>(
            file_data.data() + ehdr->e_phoff + i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz < ph->p_filesz) throw std::runtime_error("p_memsz < p_filesz");
        if (ph->p_offset + ph->p_filesz > file_data.size()) throw std::runtime_error("segment extends past EOF");

        LoadedSegment seg;
        seg.vaddr = ph->p_vaddr;
        seg.size  = ph->p_memsz;
        seg.flags = ph->p_flags;
        seg.data.resize(ph->p_memsz, 0);
        std::memcpy(seg.data.data(), file_data.data() + ph->p_offset, ph->p_filesz);
        segments.push_back(std::move(seg));
    }
    return segments;
}

LoadedElf load_elf(const std::string& path) {
    auto file_data = read_file(path);
    if (file_data.size() < sizeof(Elf32_Ehdr)) {
        throw std::runtime_error("file too small to be an ELF header");
    }

    const auto* ehdr = reinterpret_cast<const Elf32_Ehdr*>(file_data.data());

    if (std::memcmp(ehdr->e_ident, "\x7f" "ELF", 4) != 0) {
        throw std::runtime_error("bad magic");
    }
    if (ehdr->e_ident[4] != 1) throw std::runtime_error("not a 32-bit ELF");
    if (ehdr->e_ident[5] != 1) throw std::runtime_error("not little-endian");
    if (ehdr->e_type != ET_EXEC) throw std::runtime_error("not an ET_EXEC executable");
    if (ehdr->e_machine != EM_RISCV) throw std::runtime_error("not RISC-V");

    const uint32_t phoff  = ehdr->e_phoff;
    const uint16_t phnum  = ehdr->e_phnum;
    const uint16_t phsize = ehdr->e_phentsize;
    if (phoff + static_cast<uint32_t>(phnum) * phsize > file_data.size()) {
        throw std::runtime_error("program headers beyond file size");
    }

    uint32_t max_vaddr = 0;
    bool found = false;
    for (uint16_t i = 0; i < phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf32_Phdr*>(
            file_data.data() + phoff + i * phsize);
        if (ph->p_type != PT_LOAD) continue;
        found = true;
        max_vaddr = std::max(max_vaddr, ph->p_vaddr + ph->p_memsz);
    }
    if (!found) throw std::runtime_error("no loadable segments");

    LoadedElf result;
    result.entry      = ehdr->e_entry;
    result.end_vaddr  = max_vaddr;
    result.segments   = extract_segments(file_data, ehdr);
    return result;
}
