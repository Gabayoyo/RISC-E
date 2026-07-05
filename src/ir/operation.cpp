#include "operation.hpp"

#include <cassert>

Operation::~Operation() = default;

void Operation::add_operand(Operand op) {
    assert(num_ops_ < max_operands);
    operands_[num_ops_++] = std::move(op);
}

void Operation::set_operands(std::initializer_list<Operand> list) {
    assert(list.size() <= max_operands);
    size_t i = 0;
    for (const auto& op : list)
        operands_[i++] = op;
    num_ops_ = static_cast<uint8_t>(i);
}

const Operand& Operation::operand(size_t idx) const {
    assert(idx < num_ops_);
    return operands_[idx];
}

Operand& Operation::operand(size_t idx) {
    assert(idx < num_ops_);
    return operands_[idx];
}