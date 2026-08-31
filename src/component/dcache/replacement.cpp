#include "risc-e/component/dcache/replacement.hpp"

ReplacementState::ReplacementState(std::size_t ways, ReplacementPolicy policy)
    : policy_(policy), ways_(ways) {
    if (policy_ == ReplacementPolicy::LRU || policy_ == ReplacementPolicy::NMRU) {
        lru_.assign(ways_, 0);
    } else {
        plru_.assign(ways_, 0);
    }
}

void ReplacementState::touch(std::size_t way) {
    ++tick_;
    if (policy_ == ReplacementPolicy::LRU || policy_ == ReplacementPolicy::NMRU) {
        lru_[way] = tick_;
        return;
    }

    // PLRU: set the bits along the path to `way` so eviction prefers the
    // subtrees away from it (marking it most-recently-used).
    std::size_t node = 1;
    std::size_t mask = ways_ >> 1;
    while (mask != 0) {
        if (way & mask) {
            plru_[node] = 0;  // eviction prefers the left subtree
            node = node * 2 + 1;
        } else {
            plru_[node] = 1;  // eviction prefers the right subtree
            node = node * 2;
        }
        mask >>= 1;
    }
}

std::size_t ReplacementState::victim() const {
    if (policy_ == ReplacementPolicy::LRU || policy_ == ReplacementPolicy::NMRU) {
        std::size_t victim = 0;
        for (std::size_t i = 1; i < ways_; ++i) {
            if (lru_[i] < lru_[victim]) victim = i;
        }
        return victim;
    }

    // PLRU: walk the tree, following each node's preferred eviction child.
    std::size_t node = 1;
    while (node < ways_) {
        node = plru_[node] ? node * 2 + 1 : node * 2;
    }
    return node - ways_;
}
