#pragma once
#include <cstdint>

namespace ntr {
// LDM reads a whole register group before STM writes any of that group.
// Callers supply the original group size (at most eight words).
template<class Bus>
void memory_copy_register_group(Bus& bus, uintptr_t src, uintptr_t dst,
                                unsigned words, bool fifo = false) {
    uint32_t values[8];
    for (unsigned i = 0; i < words; ++i) values[i] = bus.read(src + 4*i, 4);
    for (unsigned i = 0; i < words; ++i) bus.write(dst + (fifo ? 0 : 4*i), values[i], 4);
}

// SDK byte transfers use halfword/word transactions even for odd edges.
// A byte store to ARM9 VRAM would disappear, while a flat memcpy would miss
// overlapping-bank OR reads and broadcast writes. Bus retains address identity.
template<class Bus>
void memory_copy8(Bus& bus, uintptr_t src, uintptr_t dst, unsigned bytes) {
    if (!bytes) return;
    if (dst & 1) {
        const unsigned edge = bus.read(dst - 1, 2) & 0xff;
        const unsigned first = bus.read(src & ~uintptr_t(1), 2) >> ((src & 1) * 8);
        bus.write(dst - 1, edge | (first << 8), 2);
        ++src; ++dst;
        if (!--bytes) return;
    }
    if ((src ^ dst) & 1) {
        src &= ~uintptr_t(1);
        unsigned carry = bus.read(src, 2) >> 8;
        src += 2;
        while (bytes >= 2) {
            const unsigned next = bus.read(src, 2);
            bus.write(dst, carry | (next << 8), 2);
            carry = next >> 8;
            src += 2; dst += 2; bytes -= 2;
        }
        if (bytes) {
            const unsigned edge = bus.read(dst, 2) & 0xff00;
            bus.write(dst, edge | carry, 2);
        }
        return;
    }
    if ((src ^ dst) & 2) {
        while (bytes >= 2) {
            const unsigned value = bus.read(src, 2);
            bus.write(dst, value, 2);
            src += 2; dst += 2; bytes -= 2;
        }
    } else {
        if (bytes >= 2 && (dst & 2)) {
            const unsigned value = bus.read(src, 2);
            bus.write(dst, value, 2);
            src += 2; dst += 2; bytes -= 2;
        }
        while (bytes >= 4) {
            const unsigned value = bus.read(src, 4);
            bus.write(dst, value, 4);
            src += 4; dst += 4; bytes -= 4;
        }
        if (bytes >= 2) {
            const unsigned value = bus.read(src, 2);
            bus.write(dst, value, 2);
            src += 2; dst += 2; bytes -= 2;
        }
    }
    if (bytes) {
        const unsigned edge = bus.read(dst, 2) & 0xff00;
        const unsigned value = bus.read(src, 2) & 0xff;
        bus.write(dst, edge | value, 2);
    }
}

template<class Bus>
void memory_fill8(Bus& bus, uintptr_t dst, unsigned value, unsigned bytes) {
    if (!bytes) return;
    if (dst & 1) {
        const unsigned edge = bus.read(dst - 1, 2) & 0xff;
        bus.write(dst - 1, edge | (value << 8), 2);
        ++dst;
        if (!--bytes) return;
    }
    if (bytes >= 2) {
        value |= value << 8;
        if (dst & 2) {
            bus.write(dst, value, 2);
            dst += 2; bytes -= 2;
            if (!bytes) return;
        }
        value |= value << 16;
        while (bytes >= 4) {
            bus.write(dst, value, 4);
            dst += 4; bytes -= 4;
        }
        if (bytes >= 2) {
            bus.write(dst, value, 2);
            dst += 2; bytes -= 2;
        }
    }
    if (bytes) {
        const unsigned edge = bus.read(dst, 2) & 0xff00;
        bus.write(dst, edge | (value & 0xff), 2);
    }
}

// DecompressLZ16's two-byte output latch matters for VRAM. Backreferences
// read committed halfwords, and an unmatched last byte stays in the latch.
// The stream header and bounds are the caller's contract, as in the ROM.
template<class Bus>
void memory_decompress_lz16(Bus& bus, uintptr_t src, uintptr_t dst) {
    int remaining = int(bus.read(src, 4) >> 8);
    src += 4;
    unsigned pending = 0, shift = 0;
    auto output = [&](unsigned value) {
        pending |= value << shift;
        shift ^= 8;
        if (!shift) {
            bus.write(dst, pending, 2);
            dst += 2;
            pending = 0;
        }
    };
    while (remaining > 0) {
        unsigned flags = bus.read(src++, 1);
        for (unsigned bit = 0; bit < 8 && remaining > 0; ++bit, flags <<= 1) {
            if (!(flags & 0x80)) {
                output(bus.read(src++, 1));
                --remaining;
            } else {
                const unsigned first = bus.read(src, 1);
                unsigned length = (first >> 4) + 3;
                // Keep the second read: the original stream transaction order
                // reads this byte once for length, once for displacement.
                const unsigned high = bus.read(src++, 1) & 15;
                const unsigned displacement = ((high << 8) | bus.read(src++, 1)) + 1;
                unsigned select = (8 - shift) ^ ((displacement & 1) << 3);
                remaining -= length;
                while (length--) {
                    select ^= 8;
                    const unsigned offset = ((displacement + ((8 - shift) >> 3)) >> 1) << 1;
                    output((bus.read(dst - offset, 2) >> select) & 0xff);
                }
            }
        }
    }
}
}
