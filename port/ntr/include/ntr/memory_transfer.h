#pragma once
#include "ntr/mmio.h"
#include "ntr/vram_bus.h"
#include "ntr/memory_byte_primitives.h"
#include <cstring>

namespace ntr {
inline bool memory_bus_required(const void* source, const void* destination) {
    return io_vram_banks() &&
        (vram_cpu_address(reinterpret_cast<uintptr_t>(source)) ||
         vram_cpu_address(reinterpret_cast<uintptr_t>(destination)));
}

// Inspect the DS pointer before translating it: a flat pointer cannot express
// multiple selected banks. Non-VRAM memory keeps the existing translation.
struct MemoryTransferBus {
    uint32_t read(uintptr_t address, unsigned width) const {
        if (vram_cpu_address(address)) return vram_cpu_read(uint32_t(address), width);
        if (address >= IO_BASE && address < IO_BASE+IO_SIZE)
            return uint32_t(io_read(uint32_t(address), width));
        uint32_t value = 0;
        std::memcpy(&value, xlate(reinterpret_cast<void*>(address)), width);
        return value;
    }
    void write(uintptr_t address, uint32_t value, unsigned width) const {
        if (vram_cpu_address(address)) vram_cpu_write(uint32_t(address), value, width);
        else if (address >= IO_BASE && address < IO_BASE+IO_SIZE)
            io_write(uint32_t(address), value, width);
        else std::memcpy(xlate(reinterpret_cast<void*>(address)), &value, width);
    }
};

inline void memory_copy_units(const void* source, void* destination,
                              unsigned count, unsigned width, bool fill = false) {
    MemoryTransferBus bus;
    uintptr_t src = reinterpret_cast<uintptr_t>(source);
    uintptr_t dst = reinterpret_cast<uintptr_t>(destination);
    for (unsigned i = 0; i < count; ++i, dst += width) {
        const auto value = bus.read(src, width);
        bus.write(dst, value, width);
        if (!fill) src += width;
    }
}
}
