#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

// A return address stack (RAS): a small LIFO of return addresses used to
// predict the target of call/return instructions. Depth 0 disables it.
class ReturnAddressStack {
public:
    static constexpr std::size_t kDefaultDepth = 16;
    static constexpr std::size_t kMaxDepth = 64;

    explicit ReturnAddressStack(std::size_t depth = kDefaultDepth) : depth_(depth) {}

    bool enabled() const { return depth_ != 0; }

    void push(std::uint32_t addr) {
        if (depth_ == 0) return;
        if (entries_.size() == depth_) entries_.pop_front();  // overflow: drop the oldest entry
        entries_.push_back(addr);
    }

    std::optional<std::uint32_t> peek() const {
        if (entries_.empty()) return std::nullopt;
        return entries_.back();
    }

    std::optional<std::uint32_t> pop() {
        if (entries_.empty()) return std::nullopt;
        const std::uint32_t top = entries_.back();
        entries_.pop_back();
        return top;
    }

    void reset() { entries_.clear(); }

    void resize(std::size_t depth) {
        depth_ = depth;
        entries_.clear();
    }

    std::size_t depth() const { return depth_; }

private:
    std::size_t depth_;
    std::deque<std::uint32_t> entries_;
};
