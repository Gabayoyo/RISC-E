#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// we use forward declaration here as we only need pointers in the header
class Operation;

/**
 * @brief A basic block; a maximal sequence of operations with a single entry
 * and a single exit (the terminator).
 * 
 * The block guarantees that the last operation is always a terminator.
 */
class BasicBlock {
public:
    // Construct a block with a known start address and all its operations.
    // The last operation MUST be a terminator.
    BasicBlock(uint32_t start_addr, std::vector<std::unique_ptr<Operation>> ops);

    ~BasicBlock();

    uint32_t start_address = 0;

    const std::vector<std::unique_ptr<Operation>>& operations() const noexcept;
    size_t size() const noexcept;
    bool   empty() const noexcept;

    Operation*       terminator() noexcept;
    const Operation* terminator() const noexcept;

    // indices into the CFG's block list
    std::vector<size_t> successors;

    // Replace the operation at index `idx`.
    // If replacing the terminator, the new operation must also be a terminator.
    void replace_op(size_t idx, std::unique_ptr<Operation> new_op);

    // Insert a new operation just before the terminator.
    void insert_before_terminator(std::unique_ptr<Operation> op);

    // Remove the operation at index `idx`.
    // The terminator can only be removed if it is the sole operation.
    void remove_op(size_t idx);

private:
    std::vector<std::unique_ptr<Operation>> ops_;
};