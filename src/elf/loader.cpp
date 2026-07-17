#include "loader.hpp"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
#include <utility>

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

struct Elf32_Shdr {
    uint32_t sh_name;      // offset into .shstrtab
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

struct Elf32_Sym {
    uint32_t st_name;      // offset into string table
    uint32_t st_value;     // symbol value (address)
    uint32_t st_size;
    uint8_t  st_info;      // type + bind
    uint8_t  st_other;
    uint16_t st_shndx;     // section index
};

// Section types
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;

// Symbol bindings and types (extracted from st_info)
#define ELF32_ST_BIND(i)   ((i) >> 4)
#define ELF32_ST_TYPE(i)   ((i) & 0xf)
#define STB_GLOBAL          1
#define STT_FUNC            2

// Max symbol name length we'll bother copying
static constexpr size_t MAX_SYM_NAME = 256;
#pragma pack(pop)

static constexpr uint16_t ET_EXEC  = 2;
static constexpr uint16_t EM_RISCV = 243;
static constexpr uint32_t PT_LOAD  = 1;
static constexpr uint32_t PF_X     = 0x1;   // segment execute permission flag

// extract symbols from the ELF file data, if present
static std::vector<ElfSymbol> extract_symbols(
    const std::vector<uint8_t>& file_data,
    const Elf32_Ehdr* ehdr)
{
    std::vector<ElfSymbol> symbols;

    uint32_t shoff = ehdr->e_shoff;
    uint16_t shnum = ehdr->e_shnum;
    uint16_t shsize = ehdr->e_shentsize;
    uint16_t shstrndx = ehdr->e_shstrndx;

    // Sanity checks
    if (shoff == 0 || shnum == 0 || shsize == 0 ||
        shoff + static_cast<uint32_t>(shnum) * shsize > file_data.size()) {
        // No section headers – that's OK for some stripped executables
        return symbols;
    }

    // 1. Locate the section header string table (.shstrtab)
    if (shstrndx >= shnum) return symbols;
    const auto* shstrtab_hdr = reinterpret_cast<const Elf32_Shdr*>(
        file_data.data() + shoff + shstrndx * shsize);
    const char* shstrtab = reinterpret_cast<const char*>(
        file_data.data() + shstrtab_hdr->sh_offset);

    // 2. Scan all sections to find the symbol table (SHT_SYMTAB)
    const Elf32_Shdr* symtab_hdr = nullptr;
    const Elf32_Shdr* strtab_hdr = nullptr;   // its linked string table

    for (uint16_t i = 0; i < shnum; ++i) {
        const auto* sh = reinterpret_cast<const Elf32_Shdr*>(
            file_data.data() + shoff + i * shsize);
        if (sh->sh_type == SHT_SYMTAB) {
            symtab_hdr = sh;
            // The linked string table index is sh_link
            if (sh->sh_link < shnum) {
                strtab_hdr = reinterpret_cast<const Elf32_Shdr*>(
                    file_data.data() + shoff + sh->sh_link * shsize);
            }
            break;   // take the first one (usually there's only one .symtab)
        }
    }

    if (!symtab_hdr || !strtab_hdr) return symbols;   // no symbol table

    // 3. Parse the symbol table entries
    size_t num_syms = symtab_hdr->sh_size / sizeof(Elf32_Sym);
    const auto* syms = reinterpret_cast<const Elf32_Sym*>(
        file_data.data() + symtab_hdr->sh_offset);
    const char* strtab = reinterpret_cast<const char*>(
        file_data.data() + strtab_hdr->sh_offset);

    for (size_t i = 0; i < num_syms; ++i) {
        const Elf32_Sym& s = syms[i];

        // Skip unnamed / empty symbols if you like
        if (s.st_name == 0) continue;

        uint8_t type = ELF32_ST_TYPE(s.st_info);
        uint8_t bind = ELF32_ST_BIND(s.st_info);

        // For function detection you may only want STT_FUNC
        // but we store everything for completeness
        ElfSymbol out;
        out.address = s.st_value;
        out.size    = s.st_size;
        out.type    = type;
        out.bind    = bind;

        // Safe copy of name
        const char* name = strtab + s.st_name;
        size_t len = strnlen(name, MAX_SYM_NAME);
        out.name.assign(name, len);

        symbols.push_back(std::move(out));
    }

    return symbols;
}

// Extract instructions **only from the .text section** (by name).
static std::vector<std::pair<uint32_t, uint32_t>> extract_instructions(
    const std::vector<uint8_t>& file_data,
    const Elf32_Ehdr* ehdr)
{
    std::vector<std::pair<uint32_t, uint32_t>> instructions;

    uint32_t shoff = ehdr->e_shoff;
    uint16_t shnum = ehdr->e_shnum;
    uint16_t shsize = ehdr->e_shentsize;
    uint16_t shstrndx = ehdr->e_shstrndx;

    // If section headers are missing, we have nothing to extract.
    if (shoff == 0 || shnum == 0 || shsize == 0 ||
        shoff + static_cast<uint32_t>(shnum) * shsize > file_data.size()) {
        return instructions;
    }

    // Locate section header string table
    if (shstrndx >= shnum) return instructions;
    const auto* shstrtab_hdr = reinterpret_cast<const Elf32_Shdr*>(
        file_data.data() + shoff + shstrndx * shsize);
    if (shstrtab_hdr->sh_offset + shstrtab_hdr->sh_size > file_data.size())
        return instructions;

    const char* shstrtab = reinterpret_cast<const char*>(
        file_data.data() + shstrtab_hdr->sh_offset);

    // Look for the section named ".text"
    for (uint16_t i = 0; i < shnum; ++i) {
        const auto* sh = reinterpret_cast<const Elf32_Shdr*>(
            file_data.data() + shoff + i * shsize);

        // Guard against a corrupt sh_name
        if (sh->sh_name >= shstrtab_hdr->sh_size) continue;

        const char* sec_name = shstrtab + sh->sh_name;
        if (std::strcmp(sec_name, ".text") == 0) {
            // Bounds check: section data must be inside the file
            if (sh->sh_offset + sh->sh_size > file_data.size())
                throw std::runtime_error(".text section data beyond file end");

            const uint8_t* seg_base = file_data.data() + sh->sh_offset;
            uint32_t       vaddr    = sh->sh_addr;
            uint32_t       remaining = sh->sh_size;

            // Walk 4 bytes at a time (RISC‑V instructions are 4‑byte aligned)
            while (remaining >= 4) {
                uint32_t insn;
                std::memcpy(&insn, seg_base, 4);
                instructions.emplace_back(vaddr, insn);
                seg_base  += 4;
                vaddr     += 4;
                remaining -= 4;
            }
            break;   // only the first .text section matters
        }
    }

    // If no .text section was found, return an empty vector (soft failure)
    return instructions;
}

// Extract all PT_LOAD segments into a vector of LoadedSegment descriptors.
static std::vector<LoadedSegment> extract_segments(
    const std::vector<uint8_t>& file_data,
    const Elf32_Ehdr* ehdr)
{
    std::vector<LoadedSegment> segments;

    uint32_t phoff  = ehdr->e_phoff;
    uint16_t phnum  = ehdr->e_phnum;
    uint16_t phsize = ehdr->e_phentsize;

    for (uint16_t i = 0; i < phnum; ++i) {
        const auto* ph = reinterpret_cast<const Elf32_Phdr*>(
            file_data.data() + phoff + i * phsize);
        if (ph->p_type != PT_LOAD) continue;

        // Ensure the segment's file data is within bounds
        if (ph->p_offset + ph->p_filesz > file_data.size())
            throw std::runtime_error("PT_LOAD segment data beyond file end");

        // p_filesz must not exceed p_memsz (malformed ELF)
        if (ph->p_filesz > ph->p_memsz)
            throw std::runtime_error("PT_LOAD file size exceeds memory size");

        LoadedSegment seg;
        seg.vaddr = ph->p_vaddr;
        seg.size  = ph->p_memsz;
        seg.flags = ph->p_flags;

        // Allocate zero‑initialised space for the whole segment (including BSS)
        seg.data.resize(ph->p_memsz, 0);
        if (ph->p_filesz > 0) {
            std::memcpy(seg.data.data(),
                        file_data.data() + ph->p_offset,
                        ph->p_filesz);
        }

        segments.push_back(std::move(seg));
    }

    return segments;
}

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
    result.program      = std::move(memory);
    result.entry        = ehdr->e_entry;
    result.base_vaddr   = min_vaddr;
    result.end_vaddr    = max_vaddr;
    result.symbols      = extract_symbols(file_data, ehdr);
    result.instructions = extract_instructions(file_data, ehdr);
    result.segments     = extract_segments(file_data, ehdr);

    return result;
}