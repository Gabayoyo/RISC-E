#include "src/builder/ir_builder.hpp"
#include "src/ir/ir.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>
#include <iostream>

IRModule* IRBuilder::buildModule(IRModule& module,
                            const std::vector<DecodedInstruction>& insns,
                            const std::set<uint32_t>& entryAddrs)
{
    if (insns.empty() || entryAddrs.empty())
        return nullptr;

    // Copy and sort entry addresses
    std::vector<uint32_t> sortedEntries(entryAddrs.begin(), entryAddrs.end());
    std::sort(sortedEntries.begin(), sortedEntries.end());

    size_t insnIdx = 0;  // cursor into the full instruction list

    for (size_t i = 0; i < sortedEntries.size(); ++i) {
        uint32_t funcStart = sortedEntries[i];
        // End of this function is just before the next entry, or infinity
        uint32_t funcEnd = (i + 1 < sortedEntries.size()) ? sortedEntries[i + 1]
                                                          : UINT32_MAX;

        // Skip any instructions that lie before this entry
        while (insnIdx < insns.size() && insns[insnIdx].addr < funcStart)
            ++insnIdx;

        // Collect all instructions whose address is in [funcStart, funcEnd)
        std::vector<DecodedInstruction> funcInsns;
        while (insnIdx < insns.size() && insns[insnIdx].addr < funcEnd) {
            funcInsns.push_back(insns[insnIdx]);
            ++insnIdx;
        }

        if (!funcInsns.empty()) {
            std::string name = addrToFuncName(funcStart);
            // Reuse the existing single‑function builder
            buildFunction(module, name, funcInsns);
        }

        // Stop if we've consumed all instructions
        if (insnIdx >= insns.size())
            break;
    }
    return &module;
}

// Public entry point
IRFunction* IRBuilder::buildFunction(IRModule& module,
                                     const std::string& name,
                                     const std::vector<DecodedInstruction>& insns)
{
    if (insns.empty()) return nullptr;

    IRFunction* fn = module.addFunction(name, insns.front().addr);

    auto leaders = findLeaders(insns);
    liftInstructions(*fn, leaders, insns);
    wireCFG(*fn);

    return fn;
}

// ---------------------------------------------------------------------------
// Pass 1 — leader discovery
//
// A "leader" is any address that must begin a new basic block:
//   - the function entry point
//   - the fall-through after any branch/jump
//   - the taken target of any branch/jump  (if it resolves statically)
// ---------------------------------------------------------------------------

std::set<uint32_t> IRBuilder::findLeaders(const std::vector<DecodedInstruction>& insns) const
{
    std::set<uint32_t> leaders;
    leaders.insert(insns.front().addr);

    for (const auto& d : insns) {
        // Ask the decoder if this encoding is a branch or jump — without
        // fully constructing an Operation just yet.
        if (!opBuilder.isBranchOrJump(d)) continue;

        leaders.insert(d.addr + 4);                   // fall-through

        if (opBuilder.hasStaticTarget(d)) {
            leaders.insert(d.addr + opBuilder.staticOffset(d)); // taken target
        }
    }

    return leaders;
}

// ---------------------------------------------------------------------------
// Pass 2 — block creation and instruction lifting
//
// OperationDecoder::decode() owns the mapping from DecodedInstruction to a
// concrete Operation subclass.  IRBuilder just places the returned object
// into the correct BasicBlock.
// ---------------------------------------------------------------------------

void IRBuilder::liftInstructions(IRFunction& fn,
                                 const std::set<uint32_t>& leaders,
                                 const std::vector<DecodedInstruction>& insns)
{
    BasicBlock* current = nullptr;

    for (const auto& d : insns) {
        if (leaders.count(d.addr)) {
            current = fn.addBlock(d.addr);
        }
        assert(current && "instruction address precedes every leader");

        std::unique_ptr<Operation> op = opBuilder.decode(d);
        op->addr   = d.addr;
        op->raw    = d.raw;
        op->parent = current;

        current->endAddr = d.addr;
        current->addOperation(std::move(op));

        // After a terminator the block is closed; the next leader opens a new one.
        if (current->terminator()) current = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Pass 3 — CFG wiring
//
// Only looks at each block's terminator to decide outgoing edges.
// JALR (indirect) targets cannot be resolved here; that requires alias
// analysis or call-graph information.
// ---------------------------------------------------------------------------

void IRBuilder::wireCFG(IRFunction& fn) const
{
    for (auto& bb : fn.blocks) {
        uint32_t target   = 0;
        uint32_t fallthru = 0;

        if (!resolveEdges(*bb, target, fallthru)) {
            // No terminator — plain fall-through block.
            if (BasicBlock* next = fn.findBlock(bb->endAddr + 4))
                addEdge(bb.get(), next);
            continue;
        }

        if (target) {
            if (BasicBlock* t = fn.findBlock(target))
                addEdge(bb.get(), t);
        }
        if (fallthru) {
            if (BasicBlock* f = fn.findBlock(fallthru))
                addEdge(bb.get(), f);
        }
    }
}

// Helpers
void IRBuilder::addEdge(BasicBlock* from, BasicBlock* to)
{
    from->successors.push_back(to);
    to->predecessors.push_back(from);
}

bool IRBuilder::resolveEdges(const BasicBlock& bb,
                              uint32_t& target,
                              uint32_t& fallthru)
{
    const Operation* term = const_cast<BasicBlock&>(bb).terminator();
    if (!term) return false;

    // B-type: BEQ, BNE, BLT, BGE, BLTU, BGEU
    // Both edges live — taken target and fall-through.
    if (const auto* b = dynamic_cast<const BType*>(term)) {
        target   = bb.endAddr + static_cast<uint32_t>(b->imm.value);
        fallthru = bb.endAddr + 4;
        return true;
    }

    // J-type: JAL only.
    // One edge — no fall-through (rd gets the link address but that's a data dep,
    // not a CFG edge; the callee return is a separate JALR back at the call site).
    if (const auto* j = dynamic_cast<const JType*>(term)) {
        target   = bb.endAddr + static_cast<uint32_t>(j->imm.value);
        fallthru = 0;
        return true;
    }

    // JALR — indirect, target = rs1 + imm resolved at runtime only.
    // The block IS a terminator block; we just can't add edges yet.
    // Leave target and fallthru as 0; a later indirect-call analysis pass handles this.
    return true;
}