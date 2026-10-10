// Native seams for CPU fill/copy primitives whose original C implementations
// otherwise bypass the physical VRAM bus. Matched sources remain untouched.
#include "ntr/memory_transfer.h"

extern "C" void __real_MultiStore16(unsigned short value, char* destination, int bytes);
extern "C" void __wrap_MultiStore16(unsigned short value, char* destination, int bytes)
{
    const auto address = reinterpret_cast<uintptr_t>(destination);
    if (!ntr::io_vram_banks() || !ntr::vram_cpu_address(address)) {
        __real_MultiStore16(value, destination, bytes);
        return;
    }
    // The retail loop stores two bytes while offset < bytes, including the
    // last halfword for an odd positive length. Zero/negative lengths do nothing.
    if (bytes <= 0) return;
    ntr::MemoryTransferBus bus;
    for (unsigned offset = 0; offset < unsigned(bytes); offset += 2)
        bus.write(address + offset, value, 2);
}

extern "C" void __real_MultiCopyHalf(const void* source, void* destination, int bytes);
extern "C" void __wrap_MultiCopyHalf(const void* source, void* destination, int bytes)
{
    if (!ntr::memory_bus_required(source, destination)) {
        __real_MultiCopyHalf(source, destination, bytes);
        return;
    }
    if (bytes > 0) ntr::memory_copy_units(source, destination, (unsigned(bytes)+1)/2, 2);
}

extern "C" void __real_MultiStore32Bytes(unsigned value, int* destination, int bytes);
extern "C" void __wrap_MultiStore32Bytes(unsigned value, int* destination, int bytes)
{
    if (!ntr::memory_bus_required(nullptr, destination)) {
        __real_MultiStore32Bytes(value, destination, bytes);
        return;
    }
    if (bytes <= 0) return;
    ntr::MemoryTransferBus bus;
    const auto address = reinterpret_cast<uintptr_t>(destination);
    for (unsigned offset = 0; offset < unsigned(bytes); offset += 4)
        bus.write(address + offset, value, 4);
}
