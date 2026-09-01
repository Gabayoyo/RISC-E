#include "risc-e/memory/physical_memory.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

namespace {

constexpr uint32_t kByteBits = 8;

} // namespace

PhysicalMemory::PhysicalMemory() : pages(NUM_PAGES) {}

uint8_t* PhysicalMemory::get_page_ptr(uint32_t addr) {
    return pages[addr >> PAGE_SHIFT].get();
}

const uint8_t* PhysicalMemory::get_page_ptr(uint32_t addr) const {
    return pages[addr >> PAGE_SHIFT].get();
}

void PhysicalMemory::map_region(uint32_t addr, uint32_t size) {
    if (size == 0) return;

    const uint64_t end = std::min<uint64_t>(
        static_cast<uint64_t>(addr) + static_cast<uint64_t>(size),
        (1ull << 32));

    const uint32_t first = static_cast<uint32_t>(addr >> PAGE_SHIFT);
    const uint32_t last  = static_cast<uint32_t>((end - 1u) >> PAGE_SHIFT);

    for (uint32_t page_idx = first; page_idx <= last; ++page_idx) {
        if (!pages[page_idx]) {
            pages[page_idx] = std::make_unique<uint8_t[]>(PAGE_SIZE);
        }
    }
}

bool PhysicalMemory::check_alignment(uint32_t addr, uint32_t size, TrapCause cause) {
    if ((addr & (size - 1)) != 0) {
        raise_fault(cause, addr);
        return false;
    }
    return true;
}

bool PhysicalMemory::check_range(uint32_t addr, uint32_t size, TrapCause cause) {
    if (addr > std::numeric_limits<uint32_t>::max() - (size - 1)) {
        raise_fault(cause, addr);
        return false;
    }
    return true;
}

bool PhysicalMemory::check_mapped(uint32_t addr, uint32_t size, TrapCause cause) {
    for (uint32_t i = 0; i < size; ++i) {
        if (get_page_ptr(addr + i) == nullptr) {
            raise_fault(cause, addr + i);
            return false;
        }
    }
    return true;
}

void PhysicalMemory::raise_fault(TrapCause cause, uint32_t addr) {
    if (trap_sink_ != nullptr) {
        trap_sink_->raise_trap(cause, addr);
    }
}

bool PhysicalMemory::read_byte(uint32_t addr, uint8_t& value) {
    const uint8_t* page = get_page_ptr(addr);
    if (page == nullptr) {
        raise_fault(TrapCause::LOAD_FAULT, addr);
        return false;
    }
    value = page[addr & PAGE_MASK];
    return true;
}

bool PhysicalMemory::write_byte(uint32_t addr, uint8_t value) {
    uint8_t* page = get_page_ptr(addr);
    if (page == nullptr) {
        raise_fault(TrapCause::STORE_FAULT, addr);
        return false;
    }
    page[addr & PAGE_MASK] = value;
    return true;
}

uint8_t PhysicalMemory::load8(uint32_t addr) {
    uint8_t value = 0;
    read_byte(addr, value);
    return value;
}

void PhysicalMemory::store8(uint32_t addr, uint8_t value) {
    write_byte(addr, value);
}

uint16_t PhysicalMemory::load16(uint32_t addr) {
    if (!check_alignment(addr, 2, TrapCause::LOAD_MISALIGNED)) return 0;
    if (!check_range(addr, 2, TrapCause::LOAD_FAULT)) return 0;

    uint8_t b0 = 0, b1 = 0;
    if (!read_byte(addr, b0)) return 0;
    if (!read_byte(addr + 1, b1)) return 0;
    return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << kByteBits);
}

void PhysicalMemory::store16(uint32_t addr, uint16_t value) {
    if (!check_alignment(addr, 2, TrapCause::STORE_MISALIGNED)) return;
    if (!check_range(addr, 2, TrapCause::STORE_FAULT)) return;
    if (!check_mapped(addr, 2, TrapCause::STORE_FAULT)) return;

    write_byte(addr, static_cast<uint8_t>(value & 0xFF));
    write_byte(addr + 1, static_cast<uint8_t>((value >> kByteBits) & 0xFF));
}

uint32_t PhysicalMemory::load32(uint32_t addr) {
    if (!check_alignment(addr, 4, TrapCause::LOAD_MISALIGNED)) return 0;
    if (!check_range(addr, 4, TrapCause::LOAD_FAULT)) return 0;

    uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (!read_byte(addr, b0)) return 0;
    if (!read_byte(addr + 1, b1)) return 0;
    if (!read_byte(addr + 2, b2)) return 0;
    if (!read_byte(addr + 3, b3)) return 0;

    return static_cast<uint32_t>(b0)
         | (static_cast<uint32_t>(b1) << 8)
         | (static_cast<uint32_t>(b2) << 16)
         | (static_cast<uint32_t>(b3) << 24);
}

void PhysicalMemory::store32(uint32_t addr, uint32_t value) {
    if (!check_alignment(addr, 4, TrapCause::STORE_MISALIGNED)) return;
    if (!check_range(addr, 4, TrapCause::STORE_FAULT)) return;
    if (!check_mapped(addr, 4, TrapCause::STORE_FAULT)) return;

    write_byte(addr,     static_cast<uint8_t>(value & 0xFF));
    write_byte(addr + 1, static_cast<uint8_t>((value >> 8) & 0xFF));
    write_byte(addr + 2, static_cast<uint8_t>((value >> 16) & 0xFF));
    write_byte(addr + 3, static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t PhysicalMemory::fetch32(uint32_t addr) {
    if (!check_alignment(addr, 4, TrapCause::INSTRUCTION_ADDRESS_MISALIGNED)) return 0;
    if (!check_range(addr, 4, TrapCause::INSTRUCTION_ACCESS_FAULT)) return 0;

    uint32_t result = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint8_t* page = get_page_ptr(addr + i);
        if (page == nullptr) {
            raise_fault(TrapCause::INSTRUCTION_ACCESS_FAULT, addr + i);
            return 0;
        }
        result |= static_cast<uint32_t>(page[(addr + i) & PAGE_MASK]) << (kByteBits * i);
    }
    return result;
}

PhysicalMemory::PhysicalMemory(PhysicalMemory&& other) noexcept
    : pages(std::move(other.pages))
{
    trap_sink_ = other.trap_sink_;
}

PhysicalMemory& PhysicalMemory::operator=(PhysicalMemory&& other) noexcept {
    if (this != &other) {
        pages     = std::move(other.pages);
        trap_sink_ = other.trap_sink_;
    }
    return *this;
}
