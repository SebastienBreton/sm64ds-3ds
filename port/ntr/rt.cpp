#include "ntr/rt.h"

#include "ntr/mmio.h"
#include "ntr/scanline_windows.h"
#include <initializer_list>


#include <cstdio>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__3DS__)
// No fibers on the 3DS: the game runs on its own thread and the two sides hand
// control back and forth with one-shot events, so exactly one runs at a time --
// the same cooperative contract as SwitchToFiber.
#include <3ds.h>
#endif

// Optional game alarm service; invoked on the game thread after each yield.
#if defined(__GNUC__)
extern "C" void sm64ds_alarm_poll() __attribute__((weak));
#endif

// Weak: gate targets link the runtime without the IRQ registry (runtime.cpp).
extern "C" void *_ZN3IRQ13GetIRQHandlerEj(unsigned mask) __attribute__((weak));
namespace ntr {
namespace {

constexpr uint32_t REG_IME = 0x04000208;
constexpr uint32_t REG_IE = 0x04000210;
constexpr uint32_t REG_IF = 0x04000214;
constexpr uint32_t REG_VCOUNT = 0x04000006;

inline volatile uint32_t &reg32(uint32_t a) {
    return *static_cast<volatile uint32_t *>(host_ptr(a));
}
inline volatile uint16_t &reg16(uint32_t a) {
    return *static_cast<volatile uint16_t *>(host_ptr(a));
}

struct Runtime {
    void *host_fiber = nullptr;
    void *game_fiber = nullptr;
    void (*entry)() = nullptr;
    FrameHook hook = nullptr;
    uint64_t frame = 0;
    uint64_t max_frames = 0;
    bool game_done = false;
    bool stop = false;
    uint32_t cpsr_i = 0;     // 0x80 when interrupts are masked
};

Runtime g;

#if defined(__3DS__)
LightEvent g_to_game, g_to_host;
Thread g_thread;

void game_thread(void *) {
    LightEvent_Wait(&g_to_game);
    g.entry();
    g.game_done = true;
    LightEvent_Signal(&g_to_host);
}

// Host side: run the game until it blocks in VBlank (or returns).
void switch_to_game() {
    LightEvent_Signal(&g_to_game);
    LightEvent_Wait(&g_to_host);
}
#endif

#if defined(_WIN32)
void CALLBACK game_trampoline(void *) {
    g.entry();
    g.game_done = true;
    // The game returned. Hand control back for good.
    for (;;) SwitchToFiber(g.host_fiber);
}
#endif

// H-blank IRQ handlers (dWipe_c star/iris scene wipes, Wanted's iris) rewrite
// WIN0H/WIN1H of both engines every scanline from a precomputed table. The PPU
// rasterizes a whole frame at once, so record the per-line values here, where
// the handler really runs once per line, into a native shadow that the window
// masks consume (ntr/include/ntr/scanline_windows.h). Without it only the last
// value applied and the wipe showed as a 1-pixel white column at x=127.
bool hblank_irq_armed() {
    return (reg16(0x04000004) & 0x10) && (reg32(REG_IE) & IRQ_HBLANK) && (reg32(REG_IME) & 1) &&
           _ZN3IRQ13GetIRQHandlerEj && _ZN3IRQ13GetIRQHandlerEj(IRQ_HBLANK) != nullptr;
}
// Advance the scanline counter across a frame. Anything spinning on VCOUNT --
// the decomp does, in func_02013f4c -- needs this to actually move.
void run_scanlines() {
    const bool hblank = hblank_irq_armed();
    for (uint16_t line = 0; line < 263; ++line) {
        reg16(REG_VCOUNT) = line;
        if(line==0)io_unmapped_capture_timing_begin();
        if(line==192)io_unmapped_capture_timing_vblank();
        if(!hblank) continue;
        if(line < SCANLINE_WINDOW_LINES)
            for (uint32_t engine : {0x04000000u, 0x04001000u})
                reg32(engine + SCANLINE_WINDOW_SHADOW + 4 + line * 4) = reg32(engine + 0x40);
        // DISPSTAT bit 1 is the H-blank flag the handlers test.
        reg16(0x04000004) |= 2;
        io_set_vcount_override(line);
        rt_raise_irq(IRQ_HBLANK);
        io_set_vcount_override(-1);
        reg16(0x04000004) &= ~2;
        reg32(REG_IF) &= ~IRQ_HBLANK; // one delivery per line, never deferred
    }
    for (uint32_t engine : {0x04000000u, 0x04001000u})
        reg32(engine + SCANLINE_WINDOW_SHADOW) = hblank ? 1u : 0u;
}

}  // namespace

void rt_vblank_wait() {
#if defined(__3DS__)
    if (!g.game_fiber) return;
    LightEvent_Signal(&g_to_host);
    LightEvent_Wait(&g_to_game);
#elif defined(_WIN32)
    if (!g.game_fiber) return;   // not running under rt_run; nothing to yield to
    SwitchToFiber(g.host_fiber);
#endif
    rt_raise_irq(IRQ_VBLANK);
#if defined(__GNUC__)
    if (sm64ds_alarm_poll) sm64ds_alarm_poll();
#endif
}

uint64_t rt_frame() { return g.frame; }
bool rt_running() { return g.game_fiber != nullptr; }

uint32_t rt_irq_disable() {
    const uint32_t prev = g.cpsr_i;
    g.cpsr_i = 0x80;
    return prev;
}

uint32_t rt_irq_enable() {
    const uint32_t prev = g.cpsr_i;
    g.cpsr_i = 0;
    rt_dispatch_pending_irqs();
    return prev;
}

uint32_t rt_irq_restore(uint32_t prev) {
    const uint32_t was = g.cpsr_i;
    g.cpsr_i = prev & 0x80;
    if (!g.cpsr_i) rt_dispatch_pending_irqs();
    return was;
}

bool rt_irq_masked() { return g.cpsr_i != 0; }

uint64_t rt_run(void (*game)(), FrameHook hook, uint64_t max_frames) {
#if defined(__3DS__)
    if (!io_init()) {
        std::fprintf(stderr, "rt_run: io_init failed\n");
        return 0;
    }
    g = Runtime{};
    g.entry = game;
    g.hook = hook;
    g.max_frames = max_frames;
    LightEvent_Init(&g_to_game, RESET_ONESHOT);
    LightEvent_Init(&g_to_host, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    g_thread = threadCreate(game_thread, nullptr, 256 * 1024, prio, -2, false);
    if (!g_thread) {
        std::fprintf(stderr, "rt_run: threadCreate failed\n");
        return 0;
    }
    g.game_fiber = g_thread;
    reg32(REG_IME) = 1;
    reg32(REG_IE) = IRQ_VBLANK;
    while (!g.game_done && !g.stop) {
        switch_to_game();
        if (g.game_done) break;
        run_scanlines();
        ++g.frame;
        if (g.hook && !g.hook(g.frame)) g.stop = true;
        if (g.max_frames && g.frame >= g.max_frames) g.stop = true;
    }
    // A game still parked in VBlank stays parked; process exit reclaims it.
    g.game_fiber = nullptr;
    return g.frame;
#elif !defined(_WIN32)
    (void)game; (void)hook; (void)max_frames;
    std::fprintf(stderr, "rt_run: fiber backend is Windows-only so far\n");
    return 0;
#else
    if (!io_init()) {
        std::fprintf(stderr, "rt_run: io_init failed\n");
        return 0;
    }

    g = Runtime{};
    g.entry = game;
    g.hook = hook;
    g.max_frames = max_frames;

    g.host_fiber = ConvertThreadToFiber(nullptr);
    if (!g.host_fiber) {
        std::fprintf(stderr, "rt_run: ConvertThreadToFiber failed\n");
        return 0;
    }
    g.game_fiber = CreateFiber(256 * 1024, game_trampoline, nullptr);
    if (!g.game_fiber) {
        std::fprintf(stderr, "rt_run: CreateFiber failed\n");
        return 0;
    }

    // The DS comes up with interrupts enabled and IME set by the CRT0.
    reg32(REG_IME) = 1;
    reg32(REG_IE) = IRQ_VBLANK;

    while (!g.game_done && !g.stop) {
        SwitchToFiber(g.game_fiber);          // run until the game blocks
        if (g.game_done) break;

        run_scanlines();
        ++g.frame;
        if (g.hook && !g.hook(g.frame)) g.stop = true;
        if (g.max_frames && g.frame >= g.max_frames) g.stop = true;

    }

    DeleteFiber(g.game_fiber);
    g.game_fiber = nullptr;
    ConvertFiberToThread();
    return g.frame;
#endif
}

}  // namespace ntr
