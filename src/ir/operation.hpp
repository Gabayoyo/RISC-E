#pragma once

#include "operands.hpp"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <cassert>

// base operation class
class Operation {
public:
    virtual ~Operation();

    size_t operand_count() const noexcept { return num_ops_; }

    // Const for read access
    const Operand& operand(size_t idx) const;

    // Non-const for write access
    Operand& operand(size_t idx);

    // range-based helpers for loops
    auto operands_begin() const { return operands_.cbegin(); }
    auto operands_end()   const { return operands_.cbegin() + num_ops_; }

    virtual bool is_terminator() const { return false; }

protected:
    Operation() = default;

    void add_operand(Operand op);
    void set_operands(std::initializer_list<Operand> list);

private:
    static constexpr size_t max_operands = 3;
    std::array<Operand, max_operands> operands_;
    uint8_t num_ops_ = 0;
};