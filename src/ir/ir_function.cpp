#include "src/ir/ir_function.hpp"
#include "src/ir/ir_module.hpp"
#include "src/utils/utils.hpp"

BasicBlock* IRFunction::addBlock(uint32_t startAddr) {
    auto bb       = std::make_unique<BasicBlock>();
    bb->label     = "bb_0x" + toHex(startAddr);
    bb->startAddr = startAddr;
    bb->parent    = this;
    auto* ptr = bb.get();
    blocks.push_back(std::move(bb));
    return ptr;
}

BasicBlock* IRFunction::findBlock(uint32_t addr) {
    for (auto& bb : blocks)
        if (bb->startAddr == addr) return bb.get();
    return nullptr;
}