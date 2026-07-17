#pragma once

#include "src/interpreter/memory_interface.hpp"
#include <vector>

class PhysicalMemory : public MemoryInterface {
    static constexpr uint32_t PAGE_SHIFT = 12;
    static constexpr uint32_t PAGE_SIZE  = 1u << PAGE_SHIFT;
    static constexpr uint32_t PAGE_MASK  = PAGE_SIZE - 1;
    static constexpr uint32_t NUM_PAGES  = (1ull << 32) / PAGE_SIZE; // 1M pages

    std::vector<uint8_t*> pages;  // size = NUM_PAGES, initially all nullptr

    uint8_t* getPagePtr(uint32_t addr);
    const uint8_t* getPagePtr(uint32_t addr) const;

    bool checkAlignment(uint32_t addr, uint32_t size, TrapCause cause);
    bool checkRange(uint32_t addr, uint32_t size, TrapCause cause);
    bool raiseFault(TrapCause cause, uint32_t addr);
    bool readByte(uint32_t addr, uint8_t& value);
    void writeByte(uint32_t addr, uint8_t value);

public:
    PhysicalMemory() : pages(NUM_PAGES, nullptr) {}
    ~PhysicalMemory() {
        for (auto p : pages) delete[] p;
    }

    uint8_t  load8(uint32_t addr) override;
    void     store8(uint32_t addr, uint8_t value) override;
    uint16_t load16(uint32_t addr) override;
    void     store16(uint32_t addr, uint16_t value) override;
    uint32_t load32(uint32_t addr) override;
    void     store32(uint32_t addr, uint32_t value) override;
};