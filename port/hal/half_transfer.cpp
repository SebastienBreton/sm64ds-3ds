// Synchronous DS halfword copies, translated to native mapped buffers.
#include "ntr/mmio.h"
#include "ntr/memory_transfer.h"
#include <cstdint>
#include <cstring>

// SDK byte-fill primitive. DS halfword read/modify/write protects adjacent
// bytes on VRAM; the native backing buffer permits byte stores with the same
// result. A zero-size fill must not dereference the supplied address.
extern "C" void func_0205a588(void* dst, unsigned value, unsigned bytes) {
    if (ntr::memory_bus_required(nullptr, dst)) {
        ntr::MemoryTransferBus bus;
        ntr::memory_fill8(bus, reinterpret_cast<uintptr_t>(dst), value, bytes);
        return;
    }
    if (bytes) std::memset(ntr::xlate(dst), value & 0xff, bytes);
}

extern "C" void Copy32Bytes(const void* src, void* dst) {
    if (ntr::memory_bus_required(src, dst)) {
        ntr::MemoryTransferBus bus;
        const auto s = reinterpret_cast<uintptr_t>(src), d = reinterpret_cast<uintptr_t>(dst);
        ntr::memory_copy_register_group(bus, s, d, 3);
        ntr::memory_copy_register_group(bus, s+12, d+12, 3);
        ntr::memory_copy_register_group(bus, s+24, d+24, 2);
        return;
    }
    std::memcpy(ntr::xlate(dst), ntr::xlate(src), 32);
}

// The SDK transfer is synchronous, source-fixed and word-sized. Discard the
// trailing 0..3 bytes as the original DMA count (size >> 2) does. Translate
// VRAM/OAM destinations before storing; never send DS addresses to libc.
extern "C" void DMASyncFillTransfer(unsigned, void* dst, unsigned value,
                                      unsigned bytes) {
    if (!bytes) return;
    if (ntr::memory_bus_required(nullptr, dst)) {
        ntr::MemoryTransferBus bus;
        for (unsigned i = 0; i < (bytes >> 2); ++i)
            bus.write(reinterpret_cast<uintptr_t>(dst) + 4*i, value, 4);
        return;
    }
    const auto address = reinterpret_cast<uintptr_t>(dst);
    if (address >= ntr::IO_BASE && address < ntr::IO_BASE + ntr::IO_SIZE) {
        // Register writes may trigger graphics effects; translating the backing
        // address alone would silently bypass the native I/O dispatcher.
        for (unsigned i = 0; i < (bytes >> 2); ++i)
            ntr::io_write(static_cast<uint32_t>(address + 4*i), value, 4);
        return;
    }
    auto* d = ntr::xlate<uint32_t>(dst);
    for (unsigned i = 0; i < (bytes >> 2); ++i) d[i] = value;
}

// SDK asynchronous fill: r1 is destination and r2 is the repeated value.
// Native DMA completes synchronously, like the existing copy/FIFO seam;
// completion is delivered once, after all stores, including a zero-size call.
extern "C" void func_0205a064(unsigned channel, void* dst, unsigned value,
                               unsigned bytes, void (*done)(unsigned), unsigned arg) {
    DMASyncFillTransfer(channel, dst, value, bytes);
    if (done) done(arg);
}
extern "C" void MultiCopyHalf(const void* src, void* dst, int size) {
    if (ntr::memory_bus_required(src, dst)) {
        if (size > 0) ntr::memory_copy_units(src, dst, (unsigned(size)+1)/2, 2);
        return;
    }
    const auto* s = ntr::xlate<const uint16_t>(src);
    auto* d = ntr::xlate<uint16_t>(dst);
    for (int offset = 0; offset < size; offset += 2) *d++ = *s++;
}
extern "C" void DMASyncHalfTransfer(unsigned, const void* src, void* dst, unsigned bytes) {
    if (ntr::memory_bus_required(src, dst)) {
        ntr::memory_copy_units(src, dst, bytes >> 1, 2);
        return;
    }
    const auto* s = ntr::xlate<const uint16_t>(src);
    auto* d = ntr::xlate<uint16_t>(dst);
    for (unsigned i = 0; i < (bytes >> 1); ++i) *d++ = *s++;
}
