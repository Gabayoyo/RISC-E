#pragma once

#include "risc-e/core/decoder/decoded_instruction.hpp"

#include <cstdint>

// Core decoder function: takes a 32-bit instruction and returns a DecodedInstruction struct.
DecodedInstruction decode_raw_inst(uint32_t inst, uint32_t addr);
