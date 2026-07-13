#pragma once

#include <cstdint>
#include <string>

#include "riscv_types.hpp"

// Core decoder function: takes a 32-bit instruction and returns a DecodedInstruction struct.
DecodedInstruction decode_raw_inst(uint32_t inst, uint32_t addr);

// returns a string mnemonic for the given decoded instruction, e.g., "ADD", "LW", etc.
std::string mnemonic(const DecodedInstruction& d);
