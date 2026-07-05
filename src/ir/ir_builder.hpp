#pragma once

#include "operation.hpp"
#include "src/riscv/riscv_types.hpp"

#include <vector>

class IRbuilder {
    std::vector<std::unique_ptr<Operation>> build_ops_from_instructions(
        const std::vector<DecodedInstruction>& insts
    );
};