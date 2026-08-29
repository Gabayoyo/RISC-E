#include "risc-e/elf/loader.hpp"
#include "risc-e/interpreter/interpreter.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <string>

int main(int argc, char** argv) {
    const std::string elf_path = (argc > 1) ? argv[1] : "../files/output/sample.elf";

    try {
        LoadedElf elf = load_elf(elf_path);
        Interpreter interpreter(std::move(elf));

        std::optional<uint32_t> exit_code = interpreter.run();

        if (exit_code.has_value()) {
            std::cout << "exit code: " << *exit_code << '\n';
            return static_cast<int>(*exit_code);
        }

        std::cout << "halted (EBREAK or trap)\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "RISC-E error: " << e.what() << '\n';
        return 1;
    }
}
