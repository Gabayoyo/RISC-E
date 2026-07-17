#include "src/interpreter/physical_memory.hpp"

#include <limits>

namespace {
constexpr uint32_t kByteBits = 8;
}

// Helper to get a writable pointer to the page that contains 'addr'.
// Allocates a zeroed page on first touch.
uint8_t* PhysicalMemory::getPagePtr(uint32_t addr) {
    uint32_t page_idx = addr >> PAGE_SHIFT;
    if (pages[page_idx] == nullptr) {
        pages[page_idx] = new uint8_t[PAGE_SIZE](); // zero-initialised
    }
    return pages[page_idx];
}

const uint8_t* PhysicalMemory::getPagePtr(uint32_t addr) const {
    return pages[addr >> PAGE_SHIFT];
}

bool PhysicalMemory::checkAlignment(uint32_t addr, uint32_t size, TrapCause cause) {
    if (addr & (size - 1)) {
        return raiseFault(cause, addr);
    }
    return true;
}

bool PhysicalMemory::checkRange(uint32_t addr, uint32_t size, TrapCause cause) {
    const uint64_t end = static_cast<uint64_t>(addr) + static_cast<uint64_t>(size) - 1u;
    if (end > std::numeric_limits<uint32_t>::max()) {
        return raiseFault(cause, addr);
    }
    return true;
}

bool PhysicalMemory::raiseFault(TrapCause cause, uint32_t addr) {
    if (trapSink_) {
        trapSink_->raiseTrap(cause, addr);
    }
    return false;
}

bool PhysicalMemory::readByte(uint32_t addr, uint8_t& value) {
    const uint8_t* page = static_cast<const PhysicalMemory*>(this)->getPagePtr(addr);
    if (!page) {
        return raiseFault(TrapCause::LOAD_FAULT, addr);
    }

    value = page[addr & PAGE_MASK];
    return true;
}

void PhysicalMemory::writeByte(uint32_t addr, uint8_t value) {
    uint8_t* page = getPagePtr(addr);
    page[addr & PAGE_MASK] = value;
}

uint8_t PhysicalMemory::load8(uint32_t addr) {
    uint8_t value = 0;
    if (!readByte(addr, value)) {
        return 0;
    }
    return value;
}

void PhysicalMemory::store8(uint32_t addr, uint8_t value) {
    writeByte(addr, value);
}

uint16_t PhysicalMemory::load16(uint32_t addr) {
    if (!checkAlignment(addr, 2, TrapCause::LOAD_MISALIGNED)) {
        return 0;
    }
    if (!checkRange(addr, 2, TrapCause::LOAD_FAULT)) {
        return 0;
    }

    uint8_t bytes[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
        if (!readByte(addr + i, bytes[i])) {
            return 0;
        }
    }

    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << kByteBits);
}

void PhysicalMemory::store16(uint32_t addr, uint16_t value) {
    if (!checkAlignment(addr, 2, TrapCause::STORE_MISALIGNED)) {
        return;
    }
    if (!checkRange(addr, 2, TrapCause::STORE_FAULT)) {
        return;
    }

    writeByte(addr, static_cast<uint8_t>(value & 0xFFu));
    writeByte(addr + 1, static_cast<uint8_t>((value >> kByteBits) & 0xFFu));
}

uint32_t PhysicalMemory::load32(uint32_t addr) {
    if (!checkAlignment(addr, 4, TrapCause::LOAD_MISALIGNED)) {
        return 0;
    }
    if (!checkRange(addr, 4, TrapCause::LOAD_FAULT)) {
        return 0;
    }

    uint8_t bytes[4] = {};
    for (uint32_t i = 0; i < 4; ++i) {
        if (!readByte(addr + i, bytes[i])) {
            return 0;
        }
    }

    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << kByteBits) |
           (static_cast<uint32_t>(bytes[2]) << (kByteBits * 2u)) |
           (static_cast<uint32_t>(bytes[3]) << (kByteBits * 3u));
}

void PhysicalMemory::store32(uint32_t addr, uint32_t value) {
    if (!checkAlignment(addr, 4, TrapCause::STORE_MISALIGNED)) {
        return;
    }
    if (!checkRange(addr, 4, TrapCause::STORE_FAULT)) {
        return;
    }

    writeByte(addr, static_cast<uint8_t>(value & 0xFFu));
    writeByte(addr + 1, static_cast<uint8_t>((value >> kByteBits) & 0xFFu));
    writeByte(addr + 2, static_cast<uint8_t>((value >> (kByteBits * 2u)) & 0xFFu));
    writeByte(addr + 3, static_cast<uint8_t>((value >> (kByteBits * 3u)) & 0xFFu));
}