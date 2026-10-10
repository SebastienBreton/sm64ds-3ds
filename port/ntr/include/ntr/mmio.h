// Host-side DS memory-mapped I/O.
//
// Two mechanisms, deliberately:
//
//   1. Real memory is mapped at the DS I/O addresses (see port/io.cpp). Any access
//      pattern the transform does not rewrite -- a pointer bound to a register and
//      dereferenced later, a struct overlay, DMA -- still lands somewhere valid and
//      latches correctly. Most DS registers really are just latches.
//
//   2. Registers whose *write* triggers hardware behaviour (the divider starting a
//      division, the geometry FIFO consuming a command) are routed through this
//      proxy by tools/hostgen.py, so the side effect actually happens.
//
// Mechanism 1 keeps the port from crashing on the long tail; mechanism 2 is what
// makes it correct. See notes/assessment.md section 2a.

#ifndef NTR_MMIO_H
#define NTR_MMIO_H

#include <stdint.h>

namespace ntr {

// DS regions, mapped at their real addresses on the host. All below 2GB so that
// truncating 32-bit round-trips -- `int base = (int)&file`, which the BMD/BCA/KCL
// loaders do in place -- stay exact.
constexpr uintptr_t MAIN_BASE = 0x02000000u, MAIN_SIZE = 0x00400000u;  // main RAM
constexpr uintptr_t IO_BASE   = 0x04000000u, IO_SIZE   = 0x00002000u;  // 2D/3D/system
constexpr uintptr_t PLTT_BASE = 0x05000000u, PLTT_SIZE = 0x00000800u;  // palettes
constexpr uintptr_t VRAM_BASE = 0x06000000u, VRAM_SIZE = 0x00900000u;  // all banks
constexpr uintptr_t OAM_BASE  = 0x07000000u, OAM_SIZE  = 0x00000800u;  // sprites

// Bring every region up. Idempotent; false if any fixed mapping failed.
bool io_init();
bool io_ready();
class DisplayCapture;
// Null unless the opt-in physical VRAM bus is active. Presentation owns the
// scanline driver; synchronize the raw register mirror when advancing VBlank.
DisplayCapture* io_display_capture();
void io_display_capture_vblank();
// Advance captures targeting non-LCDC banks even without a physical backend.
// Valid LCDC captures remain owned by their memory/scanline driver. These
// timing hooks do not expose a capture backend or request source readback.
// rt_run calls begin at drawingline0 and vblank atline192 before FrameHook.
void io_unmapped_capture_timing_begin(bool outside_drawing_range=false);
void io_unmapped_capture_timing_vblank();
// Fixed-register manual probes complete the same cycle outside rt_run.
bool io_complete_unmapped_capture_frame(bool outside_drawing_range=false);


// How the regions came up, for logs: "mapped" or "translated" per region.
const char *io_mode();

// Host pointer to a DS address. Valid only inside a mapped region.
#if defined(__3DS__)
// The 3DS cannot map memory at the DS addresses from a stock homebrew process
// (0x02000000-0x07FFFFFF is code / IPC space), so each region lives in a heap
// buffer and is reached by its top address byte. 3DS code, heap and linear
// addresses never fall in 0x02-0x07, so anything else is already a host pointer
// and passes through unchanged.
extern uint8_t *g_region[8];
extern bool g_region_identity[8];   // true when mapped at the real DS address
inline void *host_ptr(uint32_t addr) {
    const uint32_t r = addr >> 24;
    // Retail DS main RAM repeats every 4 MiB; SDK shared input is at
    // 0x027fffa8, an alias of 0x023fffa8, outside the base mapping.
    if (r == 2 && g_region[r]) return g_region[r] + (addr & (MAIN_SIZE-1));
    if (r >= 3 && r <= 7 && g_region[r]) return g_region[r] + (addr & 0x00FFFFFFu);
    return reinterpret_cast<void *>(addr);
}
#else
inline void *host_ptr(uint32_t addr) {
    if ((addr >> 24) == 2) addr = MAIN_BASE + (addr & (MAIN_SIZE-1));
    return reinterpret_cast<void *>(addr);
}
#endif

// The same translation for a value that arrives already typed as a pointer
// (DMA/CpuSet destinations the game computed from a DS base address).
template <class T = void>
inline T *xlate(const volatile void *p) {
    return static_cast<T *>(host_ptr(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))));
}

uint64_t io_read(uint32_t addr, unsigned width);
// rt.cpp: while an H-blank IRQ handler runs for scanline `line`, VCOUNT reads
// return that line instead of the free-running native clock (-1 = clock).
void io_set_vcount_override(int line);
void     io_write(uint32_t addr, uint64_t value, unsigned width);

// Proxy standing in for `*(volatile T *)addr`. Reads and writes are dispatched so
// that write-triggered registers run their side effect.
template <class T>
struct Reg {
    uint32_t addr;
    explicit Reg(uint32_t a) : addr(a) {}

    operator T() const { return static_cast<T>(io_read(addr, sizeof(T))); }

    // Non-const on purpose: these are called on temporaries produced by the macro.
    Reg &operator=(T v) { io_write(addr, static_cast<uint64_t>(v), sizeof(T)); return *this; }
    Reg &operator|=(T v) { return *this = static_cast<T>(static_cast<T>(*this) | v); }
    Reg &operator&=(T v) { return *this = static_cast<T>(static_cast<T>(*this) & v); }
    Reg &operator^=(T v) { return *this = static_cast<T>(static_cast<T>(*this) ^ v); }
};

}  // namespace ntr

// What tools/hostgen.py rewrites `*(volatile T *)0xADDR` into.
#define NTR_MMIO(T, A) (::ntr::Reg<T>((A)))

#endif  // NTR_MMIO_H
