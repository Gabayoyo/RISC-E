#pragma once

#include "risc-e/memory/memory_interface.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class PhysicalMemory : public MemoryInterface {
    static constexpr uint32_t PAGE_SHIFT = 12;
    static constexpr uint32_t PAGE_SIZE  = 1u << PAGE_SHIFT;
    static constexpr uint32_t PAGE_MASK  = PAGE_SIZE - 1;
    static constexpr uint32_t NUM_PAGES  = (1ull << 32) / PAGE_SIZE; // 1M pages

    std::vector<std::unique_ptr<uint8_t[]>> pages;  // size = NUM_PAGES, initially all nullptr

    uint8_t* get_page_ptr(uint32_t addr);
    const uint8_t* get_page_ptr(uint32_t addr) const;

    bool check_alignment(uint32_t addr, uint32_t size, TrapCause cause);
    bool check_range(uint32_t addr, uint32_t size, TrapCause cause);
    bool check_mapped(uint32_t addr, uint32_t size, TrapCause cause);
    void raise_fault(TrapCause cause, uint32_t addr);
    bool read_byte(uint32_t addr, uint8_t& value);
    bool write_byte(uint32_t addr, uint8_t value);

public:
    PhysicalMemory();
    ~PhysicalMemory() = default;

    PhysicalMemory(const PhysicalMemory&) = delete;
    PhysicalMemory& operator=(const PhysicalMemory&) = delete;
    PhysicalMemory(PhysicalMemory&& other) noexcept;
    PhysicalMemory& operator=(PhysicalMemory&& other) noexcept;

    // Explicitly allocate zeroed pages for [vaddr, vaddr + size).
    void map_region(uint32_t vaddr, uint32_t size);

    uint8_t  load8(uint32_t addr) override;
    void     store8(uint32_t addr, uint8_t value) override;
    uint16_t load16(uint32_t addr) override;
    void     store16(uint32_t addr, uint16_t value) override;
    uint32_t load32(uint32_t addr) override;
    void     store32(uint32_t addr, uint32_t value) override;
    uint32_t fetch32(uint32_t addr) override;
};
