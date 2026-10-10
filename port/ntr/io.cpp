// The host I/O bus: a real memory window at the DS I/O addresses, plus side-effect
// dispatch for the registers whose writes do something.
//
// Register semantics here are from GBATEK. No SDK source is reproduced.

#include "ntr/mmio.h"

#include "ntr/gx.h"
#include "ntr/rt.h"
#include "ntr/vram_bus.h"
#include "ntr/display_capture.h"
#include "unmapped_capture_clock.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(__3DS__)
#include <3ds.h>
#include <cstdlib>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace ntr {
#if defined(__3DS__)
uint8_t *g_region[8] = {};
bool g_region_identity[8] = {};
#endif

namespace {

uint8_t *g_io = nullptr;
VramBanks physical_vram;
DisplayCapture physical_capture;
DisplayCapture unmapped_capture_timing;
bool unmapped_capture_enabled;
bool unmapped_frame_eligible;

inline uint8_t *slot(uint32_t addr) { return static_cast<uint8_t *>(host_ptr(addr)); }

inline uint64_t raw_read(uint32_t addr, unsigned width) {
    uint64_t v = 0;
    std::memcpy(&v, slot(addr), width);
    return v;
}

inline void raw_write(uint32_t addr, uint64_t value, unsigned width) {
    std::memcpy(slot(addr), &value, width);
}

// --- CP: divider and square root -----------------------------------------------
// GBATEK: 0x4000280 DIVCNT, 0x4000290 NUMER(64), 0x4000298 DENOM(64),
//         0x40002A0 RESULT(64), 0x40002A8 REMAINDER(64),
//         0x40002B0 SQRTCNT, 0x40002B4 SQRT_RESULT(32), 0x40002B8 SQRT_PARAM(64).
// The busy bit (0x8000) is never set: the host computes synchronously, so any
// `while (DIVCNT & 0x8000);` spin-wait falls straight through.

constexpr uint32_t DIVCNT = 0x4000280, DIV_NUMER = 0x4000290, DIV_DENOM = 0x4000298;
constexpr uint32_t DIV_RESULT = 0x40002A0, DIV_REM = 0x40002A8;
constexpr uint32_t SQRTCNT = 0x40002B0, SQRT_RESULT = 0x40002B4, SQRT_PARAM = 0x40002B8;

void run_divide() {
    const uint16_t mode = static_cast<uint16_t>(raw_read(DIVCNT, 2)) & 3;
    const int64_t numer = static_cast<int64_t>(raw_read(DIV_NUMER, 8));
    const int64_t denom = static_cast<int64_t>(raw_read(DIV_DENOM, 8));

    int64_t n = numer, d = denom;
    if (mode == 0) {                       // 32 / 32
        n = static_cast<int32_t>(numer);
        d = static_cast<int32_t>(denom);
    } else if (mode == 1) {                // 64 / 32
        d = static_cast<int32_t>(denom);
    }

    int64_t quot, rem;
    if (d != 0 && n == int32_t(n) && d == int32_t(d) && !(n == INT32_MIN && d == -1)) {
        // Same quotient/remainder as the 64-bit path, without libgcc's 64-bit divide.
        quot = int32_t(n) / int32_t(d);
        rem = int32_t(n) % int32_t(d);
    } else if (d == 0) {
        // GBATEK: division by zero leaves remainder = numerator and a sign-based
        // quotient. Mirror that rather than trapping.
        rem = n;
        quot = (n < 0) ? 1 : -1;
        if (mode == 0) quot = (n < 0) ? 1 : -1;
    } else {
        quot = n / d;
        rem = n % d;
    }
    raw_write(DIV_RESULT, static_cast<uint64_t>(quot), 8);
    raw_write(DIV_REM, static_cast<uint64_t>(rem), 8);
}

uint32_t isqrt64(uint64_t v) {
    // Integer floor(sqrt(v)) by restoring bit-by-bit; matches the DS unit's result.
    uint64_t rem = 0, root = 0;
    for (int i = 0; i < 32; ++i) {
        root <<= 1;
        rem = (rem << 2) | (v >> 62);
        v <<= 2;
        if (rem > root) {
            rem -= root | 1;
            root += 2;
        }
    }
    return static_cast<uint32_t>(root >> 1);
}

void run_sqrt() {
    const uint16_t mode = static_cast<uint16_t>(raw_read(SQRTCNT, 2)) & 1;
    uint64_t param = raw_read(SQRT_PARAM, 8);
    if (mode == 0) param &= 0xFFFFFFFFull;      // 32-bit mode
    raw_write(SQRT_RESULT, isqrt64(param), 4);
}

// The units restart on every operand/control write, but only their results
// are observable. Compute once, when a result register is first read.
bool div_pending = false, sqrt_pending = false;
void settle_math(uint32_t addr, unsigned width) {
    if (div_pending && addr < DIV_REM + 8 && addr + width > DIV_RESULT) { div_pending = false; run_divide(); }
    if (sqrt_pending && addr < SQRT_RESULT + 4 && addr + width > SQRT_RESULT) { sqrt_pending = false; run_sqrt(); }
}


}  // namespace

namespace {

// Reserve one DS region at its real address. Fixed low bases keep (u32)&x exact.
void *map_fixed(uintptr_t base, size_t size) {
#if defined(__3DS__)
    // Preferred: the region really mapped at its DS address, so every raw
    // pointer the game computes (VRAM bases, DMA register macros) just works.
    // Heap pages are committed, then mirrored at `base`: Azahar's
    // svcControlMemory(MAP) accepts any destination; a real 3DS needs
    // svcControlProcessMemory on a duplicated process handle (Luma3DS lifts
    // the svc permission check, as PicoDrive's 3DS build relies on).
    // Fallback: a plain heap buffer reached through host_ptr's translation.
    const u32 len = (size + 0xFFF) & ~u32(0xFFF);
    // Source pages come from the newlib heap (libctru already committed the
    // whole svc heap to it), page-aligned so they can be mirrored.
    void *heap = std::aligned_alloc(0x1000, len);
    void *p = nullptr;
    if (heap) {
        const MemPerm rw = static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE);
        u32 out = 0;
        Handle self = 0;
        Result r1 = svcControlMemory(&out, base, reinterpret_cast<u32>(heap), len, MEMOP_MAP, rw);
        Result r2 = 0;
        if (R_SUCCEEDED(r1)) {
            p = reinterpret_cast<void *>(base);
        } else if (R_SUCCEEDED(svcDuplicateHandle(&self, CUR_PROCESS_HANDLE)) &&
                   R_SUCCEEDED(r2 = svcControlProcessMemory(self, base, reinterpret_cast<u32>(heap), len, MEMOP_MAP, rw))) {
            p = reinterpret_cast<void *>(base);
        }
        if (self) svcCloseHandle(self);
        if (!p) {
            std::fprintf(stderr, "ntr io: %08x not mappable (%08lx/%08lx), translating\n",
                         (unsigned)base, (unsigned long)r1, (unsigned long)r2);
            p = heap;
        }
    }
    if (p) {
        g_region[base >> 24] = static_cast<uint8_t *>(p);
        g_region_identity[base >> 24] = (p == reinterpret_cast<void *>(base));
    }
#elif defined(_WIN32)
    void *p = VirtualAlloc(reinterpret_cast<void *>(base), size,
                           MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *p = mmap(reinterpret_cast<void *>(base), size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) p = nullptr;
#endif
    if (p) std::memset(p, 0, size);
    return p;
}

}  // namespace

static int g_vcount_override=-1;
void io_set_vcount_override(int line){g_vcount_override=line;}
bool io_ready() { return g_io != nullptr; }
DisplayCapture* io_display_capture(){return io_vram_banks()?&physical_capture:nullptr;}
bool io_complete_unmapped_capture_frame(bool outside){
    if(!unmapped_capture_enabled || io_vram_banks())return false;
    uint8_t controls[4];for(unsigned b=0;b<4;++b)controls[b]=uint8_t(raw_read(0x04000240+b,1));
    const auto result=complete_unmapped_capture_frame(unmapped_capture_timing,uint32_t(raw_read(0x04000000,4)),controls,outside);
    if(result==UnmappedCaptureCompletion::NeedsMappedDriver)return false;
    raw_write(0x04000064,unmapped_capture_timing.control(),4);
    return result==UnmappedCaptureCompletion::Completed;
}

void io_unmapped_capture_timing_begin(bool outside){
    unmapped_frame_eligible=false;
    if(!unmapped_capture_enabled || io_vram_banks())return;
    uint8_t controls[4];for(unsigned b=0;b<4;++b)controls[b]=uint8_t(raw_read(0x04000240+b,1));
    if(display_capture_lcdc_mask(controls)&(1u<<((unmapped_capture_timing.control()>>16)&3)))return;
    DisplayCaptureMemory empty{};
    unmapped_capture_timing.scanline(0,outside,uint32_t(raw_read(0x04000000,4)),empty,nullptr,nullptr,nullptr);
    unmapped_frame_eligible=true;
}
void io_unmapped_capture_timing_vblank(){
    if(!unmapped_frame_eligible || io_vram_banks())return;
    unmapped_frame_eligible=false;
    uint8_t controls[4];for(unsigned b=0;b<4;++b)controls[b]=uint8_t(raw_read(0x04000240+b,1));
    // No valid capture may be silently discarded if mapping changes.
    if(display_capture_lcdc_mask(controls)&(1u<<((unmapped_capture_timing.control()>>16)&3)))return;
    unmapped_capture_timing.vblank();raw_write(0x04000064,unmapped_capture_timing.control(),4);
}

void io_display_capture_vblank(){
    if(auto* capture=io_display_capture()){
        capture->vblank();raw_write(0x04000064,capture->control(),4);
    }
}

bool io_init() {
    if (g_io) return true;
    struct { uintptr_t base; size_t size; } regions[] = {
        {MAIN_BASE, MAIN_SIZE}, {IO_BASE, IO_SIZE}, {PLTT_BASE, PLTT_SIZE},
        {VRAM_BASE, VRAM_SIZE}, {OAM_BASE, OAM_SIZE},
    };
    void *io = nullptr;
    for (const auto &r : regions) {
        void *p = map_fixed(r.base, r.size);
        // Main RAM is only needed by the ROM-image fs backend; in the port
        // build the game's globals are host-linked symbols and file images
        // live on the game heap, so a lost race for 0x02000000 (the host
        // allocator can land anything there first) is not fatal.
        if (!p && r.base != MAIN_BASE) return false;
        if (r.base == IO_BASE) io = p;
    }
    g_io = static_cast<uint8_t *>(io);
#if defined(__3DS__)
    std::fprintf(stderr, "ntr io: %s\n", io_mode());
#endif
    // Reset state: the geometry FIFO starts empty. GXSTAT bit 26 (empty) and
    // bit 25 (less than half full) are set on hardware at boot; the game's
    // display-list pump spin-waits on bit 25 before touching the FIFO, so the
    // latch must come up idle or the wait never falls through.
    raw_write(0x04000600u, 0x06000000u, 4);
    const char* capture_clock=std::getenv("PLAYER_UNMAPPED_CAPTURE_TIMING");
    unmapped_capture_enabled=!capture_clock || std::strcmp(capture_clock,"0")!=0;
    unmapped_capture_timing.reset();unmapped_frame_eligible=false;
    // Candidate physical bus remains opt-in until all SDK transfer routes and
    // GPU texture consumers are verified with coherent views.
    if(std::getenv("PLAYER_PHYSICAL_VRAM")){
        uint8_t* banks[9]{};
        for(unsigned bank=0;bank<9;++bank){
            banks[bank]=static_cast<uint8_t*>(std::calloc(1,VramBanks::bank_size(bank)));
            if(!banks[bank]){for(auto p:banks)std::free(p);return false;}
        }
        physical_vram.reset(banks);vram_attach_cpu_bus(&physical_vram);
        physical_capture.reset();
        std::fprintf(stderr,"ntr io: physical VRAM bus candidate enabled\n");
    }
    return true;
}

const char *io_mode() {
#if defined(__3DS__)
    static char buf[96];
    snprintf(buf, sizeof buf, "main %s, io %s, pltt %s, vram %s, oam %s",
             g_region_identity[2] ? "mapped" : "translated",
             g_region_identity[4] ? "mapped" : "translated",
             g_region_identity[5] ? "mapped" : "translated",
             g_region_identity[6] ? "mapped" : "translated",
             g_region_identity[7] ? "mapped" : "translated");
    return buf;
#else
    return "mapped";
#endif
}

// castle-min30: the divider/square-root block (0x4000280-0x40002BF) is hit
// thousands of times per update by camera/collision code right after a room
// entry; serve it before the generic VRAM/capture/IRQ dispatch, with
// constant-width register copies. Same values and side effects as below.
static inline uint64_t math_read(uint32_t addr, unsigned width){
    switch(width){
    case 1:{uint8_t v;std::memcpy(&v,slot(addr),1);return v;}
    case 2:{uint16_t v;std::memcpy(&v,slot(addr),2);return v;}
    case 4:{uint32_t v;std::memcpy(&v,slot(addr),4);return v;}
    default:return raw_read(addr,width);
    }
}
static inline void math_write(uint32_t addr, uint64_t value, unsigned width){
    switch(width){
    case 1:{const uint8_t v=uint8_t(value);std::memcpy(slot(addr),&v,1);break;}
    case 2:{const uint16_t v=uint16_t(value);std::memcpy(slot(addr),&v,2);break;}
    case 4:{const uint32_t v=uint32_t(value);std::memcpy(slot(addr),&v,4);break;}
    default:raw_write(addr,value,width);break;
    }
    if (addr == DIV_DENOM || addr == DIV_NUMER || addr == DIVCNT) div_pending = true;
    else if (addr == SQRT_PARAM || addr == SQRT_PARAM+4 || addr == SQRTCNT) sqrt_pending = true;
}
uint64_t io_read(uint32_t addr, unsigned width) {
    if (!g_io && !io_init()) return 0;
    if (addr - DIVCNT < 0x40u && addr + width <= DIVCNT + 0x40u) {
        if (addr >= DIV_RESULT && addr < SQRT_PARAM) settle_math(addr, width);
        return math_read(addr, width);
    }
    if (addr >= DIV_RESULT && addr < SQRT_PARAM) settle_math(addr, width);
    if(io_vram_banks() && vram_cpu_address(addr)){
        if(width==8)return uint64_t(vram_cpu_read(addr,4))|(uint64_t(vram_cpu_read(addr+4,4))<<32);
        return vram_cpu_read(addr,width);
    }
    if(addr>=0x04000064u && addr+width<=0x04000068u && !io_vram_banks()){
        if(width==1)return 0;
        return (unmapped_capture_timing.control()>>((addr-0x04000064u)*8))&(width==4?0xffffffffu:0xffffu);
    }
    if(auto* capture=io_display_capture()){
        if(addr>=0x04000064u && addr+width<=0x04000068u){
            if(width==1)return 0; // CaptureCnt has no ARM9 byte-read port.
            return (capture->control()>>((addr-0x04000064u)*8)) & (width==4?0xffffffffu:0xffffu);
        }
    }
    if (addr == 0x04000006u && width == 2 && g_vcount_override >= 0) return uint64_t(g_vcount_override);
#if defined(__3DS__)
    if (addr == 0x04000006u && width == 2) {
        // The cooperative game thread can poll VCOUNT without yielding.
        // rt_run's instantaneous scanline sweep cannot advance during that
        // poll. Use the independent native clock: DS lines are355*6 ARM7
        // cycles,263 lines/frame (melonDS GPU.cpp); CTR ticks are8x ARM7.
        return (svcGetSystemTick() / (355u * 6u * 8u)) % 263u;
    }
#endif
    return raw_read(addr, width);
}

void io_write(uint32_t addr, uint64_t value, unsigned width) {
    if (!g_io && !io_init()) return;
    if (addr - DIVCNT < 0x40u && addr + width <= DIVCNT + 0x40u) { math_write(addr, value, width); return; }
    if (width == 4 && (addr == 0x04000400u ||
        (addr >= 0x04000440u && addr <= 0x040005fcu && (addr & 3u) == 0))) {
        // Geometry cannot address VRAM/capture/IRQ/math registers. Keep the
        // register mirror and original dispatch order; constant-width memcpy
        // compiles to a word store rather than the generic MMIO copy path.
        raw_write(addr, static_cast<uint32_t>(value), 4);
        if (addr == 0x04000400u) gx_write_fifo(static_cast<uint32_t>(value));
        else gx_write_port(addr, static_cast<uint32_t>(value));
        return;
    }
    if(io_vram_banks() && vram_cpu_address(addr)){
        if(width==8){vram_cpu_write(addr,uint32_t(value),4);vram_cpu_write(addr+4,uint32_t(value>>32),4);}
        else vram_cpu_write(addr,uint32_t(value),width);
        return;
    }
    if(addr>=0x04000064u && addr+width<=0x04000068u && !io_vram_banks()){
        unmapped_capture_timing.write_control(addr-0x04000064u,uint32_t(value),width);
        raw_write(0x04000064u,unmapped_capture_timing.control(),4);return;
    }
    if(auto* banks=io_vram_banks()){
        if(addr>=0x04000064u && addr+width<=0x04000068u){
            physical_capture.write_control(addr-0x04000064u,uint32_t(value),width);
            raw_write(0x04000064u,physical_capture.control(),4);return;
        }
        if(addr>=0x04000240u && addr<0x0400024au && width<=4){
            // Packed stores include WRAMCNT at247, which is not a VRAM bank.
            for(unsigned i=0;i<width;++i){
                const unsigned at=addr+i;uint8_t byte=uint8_t(value>>(i*8));
                if(at<0x0400024au && at!=0x04000247u)byte=banks->map(at<0x04000247u?at-0x04000240u:at-0x04000241u,byte);
                raw_write(at,byte,1);
            }
            return;
        }
    }
    // The ARM7 command consumer is not ported yet. Never acknowledge a
    // sound IPC request as though it executed, then spin awaiting completion.
    if (addr == 0x04000188u) {
        std::fprintf(stderr, "FATAL: DS ARM7 IPC command %#x has no native backend\n",
                     static_cast<unsigned>(value));
        std::abort();
    }
    if (addr == 0x04000214u) {
        raw_write(addr,raw_read(addr,width)&~value,width); // IRQ IF: write-one-to-clear.
        return;
    }
    // GXSTAT status/FIFO fields are read-only. Geometry is consumed
    // synchronously, so the empty/less-than-half status set at init must
    // survive SDK writes of IRQ mode and the stack-error acknowledge bit.
    // Matches melonDS GPU3D Write8/16/32 at 0x04000600..603.
    if (addr >= 0x04000600u && addr + width <= 0x04000604u) {
        const unsigned shift=(addr-0x04000600u)*8;
        const uint32_t mask=width==4 ? 0xffffffffu : ((1u<<(width*8))-1u)<<shift;
        const uint32_t incoming=static_cast<uint32_t>(value)<<shift;
        uint32_t status=static_cast<uint32_t>(raw_read(0x04000600u,4));
        const uint32_t control=mask&0xc0000000u;
        status=(status&~control)|(incoming&control);
        if ((mask&incoming&0x8000u)!=0)status&=~0xa000u;
        raw_write(0x04000600u,status,4);
        return;
    }
    raw_write(addr, value, width);
    if (addr == 0x04000208u || addr == 0x04000210u) rt_dispatch_pending_irqs();

    // SDK routines may write the 64-bit SQRT operand as low/high words.
    // Both words restart the unit (melonDS NDS::ARM9IOWrite32, B8 and BC).
    if (addr == DIV_DENOM || addr == DIV_NUMER || addr == DIVCNT) div_pending = true;
    else if (addr == SQRT_PARAM || addr == SQRT_PARAM+4 || addr == SQRTCNT) sqrt_pending = true;
    // Geometry: 0x4000400 is the packed FIFO, 0x4000440.. are the command ports.
    // These execute on write and leave nothing readable behind, so unlike every
    // other block they cannot be served by the memory window alone.
    else if (addr == 0x04000400u) gx_write_fifo(static_cast<uint32_t>(value));
    else if (addr >= 0x04000440u && addr <= 0x040005FFu)
        gx_write_port(addr, static_cast<uint32_t>(value));
}

}  // namespace ntr
