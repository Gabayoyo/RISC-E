#include "basic_block.hpp"
#include "operation.hpp"

#include <cassert>

BasicBlock::BasicBlock(uint32_t start_addr, std::vector<std::unique_ptr<Operation>> ops)
    : start_address(start_addr), ops_(std::move(ops))
{
    // Enforce that a non-empty block ends with a terminator
    assert(ops_.empty() || ops_.back()->is_terminator());
}

// Destructor must be defined after Operation is complete, so that unique_ptr
// can properly destroy the polymorphic objects. The default is fine.
BasicBlock::~BasicBlock() = default;

const std::vector<std::unique_ptr<Operation>>& BasicBlock::operations() const noexcept {
    return ops_;
}

size_t BasicBlock::size() const noexcept {
    return ops_.size();
}

bool BasicBlock::empty() const noexcept {
    return ops_.empty();
}

Operation* BasicBlock::terminator() noexcept {
    return ops_.empty() ? nullptr : ops_.back().get();
}

const Operation* BasicBlock::terminator() const noexcept {
    return ops_.empty() ? nullptr : ops_.back().get();
}

void BasicBlock::replace_op(size_t idx, std::unique_ptr<Operation> new_op) {
    assert(idx < ops_.size());
    // If replacing the last operation (the terminator), ensure the new one is a terminator
    assert(idx != ops_.size() - 1 || new_op->is_terminator());
    ops_[idx] = std::move(new_op);
}

void BasicBlock::insert_before_terminator(std::unique_ptr<Operation> op) {
    assert(!ops_.empty());            // must have a terminator to stay last
    ops_.insert(ops_.end() - 1, std::move(op));
}

void BasicBlock::remove_op(size_t idx) {
    assert(idx < ops_.size());

    // The terminator can only be removed if it's the only operation (block becomes empty)
    assert(idx != ops_.size() - 1 || ops_.size() == 1);
    ops_.erase(ops_.begin() + idx);
}