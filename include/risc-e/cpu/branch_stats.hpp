#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// One recorded branch execution (only kept when tracing is enabled).
struct BranchRecord {
    uint64_t inst_count;  // instruction count at the time the branch executed
    uint32_t pc;          // address of the branch instruction
    uint32_t raw;         // raw instruction word
    uint8_t  funct3;      // branch type (BEQ=0, BNE=1, BLT=4, BGE=5, BLTU=6, BGEU=7)
    bool     taken;       // actual outcome
    uint32_t target;      // pc after the branch (taken or fall-through)
};

// Cumulative branch statistics. Counts are always updated; the trace is only
// recorded when trace_enabled is true (and is capped at kMaxTrace records).
struct BranchStats {
    static constexpr std::size_t kMaxTrace = 100000;

    uint64_t total     = 0;
    uint64_t taken     = 0;
    uint64_t not_taken = 0;

    // Predictor comparison (only meaningful when a predictor is attached).
    uint64_t hits   = 0;
    uint64_t misses = 0;

    // Per-funct3 (branch type) counters, indexed 0..7.
    std::array<uint64_t, 8> type_total{};
    std::array<uint64_t, 8> type_taken{};

    bool trace_enabled = false;
    std::vector<BranchRecord> trace;

    void reset();
    double hit_rate() const;
};

// Simulated branch predictor contract. The interpreter compares each
// prediction against the actual branch outcome to compute hit/miss rates.
class BranchPredictor {
public:
    virtual ~BranchPredictor() = default;
    virtual bool predict(uint32_t pc) const = 0;
    virtual void update(uint32_t pc, bool taken) = 0;
    virtual const char* name() const = 0;
};

// Classic 2-bit saturating counter predictor indexed by a PC hash.
class TwoBitSaturatingPredictor : public BranchPredictor {
public:
    static constexpr std::size_t kDefaultTableSize = 1024;

    // table_size must be a power of two (the index uses a mask, not a division).
    explicit TwoBitSaturatingPredictor(std::size_t table_size = kDefaultTableSize);

    bool predict(uint32_t pc) const override;
    void update(uint32_t pc, bool taken) override;
    const char* name() const override { return "2-bit saturating"; }

private:
    std::vector<std::uint8_t> counters_;  // 0..3, >= 2 means "taken"
    std::size_t index(uint32_t pc) const;
};

// Trivial baseline predictor: never predicts taken.
class AlwaysNotTakenPredictor : public BranchPredictor {
public:
    bool predict(uint32_t pc) const override {
        (void)pc;
        return false;
    }
    void update(uint32_t pc, bool taken) override {
        (void)pc;
        (void)taken;
    }
    const char* name() const override { return "always not-taken"; }
};
