#include "elf/loader.hpp"
#include "interpreter/interpreter.hpp"
#include "builder/ir_builder.hpp"

#include <iostream>
#include <iomanip>
#include <exception>
#include <algorithm>
#include <vector>
#include <utility>

int main() {
    try {
        // Load the ELF
        LoadedElf elf = load_elf("../files/output/sample.elf");

        // Print what we got
        std::cout << "Entry point : 0x" << std::hex << elf.entry << '\n';
        std::cout << "Base vaddr  : 0x" << elf.base_vaddr << '\n';
        std::cout << "End vaddr   : 0x" << elf.end_vaddr << '\n';
        std::cout << "Memory size : " << std::dec << elf.memory.size() << " bytes\n\n";

        std::set<uint32_t> sym_addrs;
        for (const auto& sym : elf.symbols)
            sym_addrs.insert(sym.address);

        std::vector<DecodedInstruction> insts;
        for (const auto& [addr, raw] : elf.instructions) {
            DecodedInstruction d = decode_raw_inst(raw, addr);
            insts.push_back(d);
        }

        IRBuilder builder;
        IRModule module;
        builder.buildModule(module, insts, sym_addrs);

        module.print(std::cout);


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