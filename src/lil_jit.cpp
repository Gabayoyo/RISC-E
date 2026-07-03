#include "elf/loader.hpp"
#include <iostream>
#include <iomanip>
#include <exception>

int main() {
    try {
        // Load the ELF
        LoadedElf elf = load_elf("../files/output/sample.elf");

        // Print what we got
        std::cout << "Entry point : 0x" << std::hex << elf.entry << '\n';
        std::cout << "Base vaddr  : 0x" << elf.base_vaddr << '\n';
        std::cout << "End vaddr   : 0x" << elf.end_vaddr << '\n';
        std::cout << "Memory size : " << std::dec << elf.memory.size() << " bytes\n";

        // Optionally peek at the first few instruction bytes
        std::cout << "First instruction bytes: ";
        for (size_t i = 0; i < std::min(elf.memory.size(), size_t(12)); ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(elf.memory[i]) << ' ';
        }
        std::cout << '\n';

        // --- Your interpreter loop would start here ---
        // uint64_t pc = elf.entry;
        // while (running) {
        //     uint8_t* inst_ptr = elf.memory.data() + (pc - elf.base_vaddr);
        //     // decode instruction at inst_ptr...
        //     pc += 4;  // RISC-V instructions are 4 bytes
        // }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}