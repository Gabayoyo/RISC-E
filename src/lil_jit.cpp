#include "elf/loader.hpp"
#include "interpreter/interpreter.hpp"

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

        // get all symbol addresses
        // std::set<uint32_t> sym_addrs;
        // for (const auto& sym : elf.symbols)
        //    sym_addrs.insert(sym.address);

        // Decode instructions
        // std::vector<DecodedInstruction> insts;
        // for (const auto& [addr, raw] : elf.instructions) {
        //     DecodedInstruction d = decode_raw_inst(raw, addr);
        //     insts.push_back(d);
        // }

        // construct and load interpreter with ELF file
        Interpreter interpreter(std::move(elf));

        interpreter.run();

        // Get flat list of decoded instructions
        // Interpret it
        // THEN add JIT and IR

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}