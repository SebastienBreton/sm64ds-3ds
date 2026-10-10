// Host side of the Model pipeline (gate 4b).
//
// The game's own code does the loading, rebasing, VRAM upload and render
// walk; this file provides only what hardware/asm provided on the DS:
//
// - the two asm copy/matrix primitives the walk calls,
// - the VRAM window globals GX::LoadTex/LoadTexPltt resolve against
//   (on hardware these come from bank assignment at GX init; here they
//   point into the ntr mapped VRAM so a slot offset IS a host address),
// - the texture-VRAM budget globals Model::GetVramOffset bump-allocates
//   from,
// - storage for the render context globals the walk stores through.
#include "ntr/mmio.h"
#include "ntr/memory_transfer.h"
#include <string.h>

typedef unsigned int u32;

extern "C" {

// ---- asm primitives ------------------------------------------------------
/* 4x3 fx32 scale matrix from (sx,sy,sz): rows {sx,0,0},{0,sy,0},{0,0,sz},0 */
void func_020527e8(int *m, int sx, int sy, int sz)
{
    memset(m, 0, 12 * sizeof(int));
    m[0] = sx;
    m[4] = sy;
    m[8] = sz;
}

/* word copy primitive used by the VRAM upload CPU path */
void MultiCopy_Int(int *src, int *dst, int len)
{
    if (ntr::memory_bus_required(src, dst)) {
        if (len > 0) ntr::memory_copy_units(src, dst, (unsigned(len)+3)/4, 4);
        return;
    }
    auto *read = reinterpret_cast<unsigned char *>(ntr::xlate(src));
    auto *write = reinterpret_cast<unsigned char *>(ntr::xlate(dst));
    // ARM LDM/STM copies forward one word, including intentional overlap.
    for (int offset = 0; offset < len; offset += 4) {
        u32 word;
        memcpy(&word, read + offset, 4);
        memcpy(write + offset, &word, 4);
    }
}

/* 32-byte-block copy primitive (asm on the DS) */
void MultiCopy32Bytes(int *src, int *dst, int len)
{
    if (ntr::memory_bus_required(src, dst)) {
        ntr::MemoryTransferBus bus;
        const auto s = reinterpret_cast<uintptr_t>(src), d = reinterpret_cast<uintptr_t>(dst);
        int offset = 0;
        for (; offset+32 <= len; offset += 32)
            ntr::memory_copy_register_group(bus, s+offset, d+offset, 8);
        for (; offset < len; offset += 4)
            ntr::memory_copy_register_group(bus, s+offset, d+offset, 1);
        return;
    }
    auto *read = reinterpret_cast<unsigned char *>(ntr::xlate(src));
    auto *write = reinterpret_cast<unsigned char *>(ntr::xlate(dst));
    int offset = 0;
    // Eight registers are loaded before any store, then trailing words.
    for (; offset + 32 <= len; offset += 32) {
        u32 words[8];
        memcpy(words, read + offset, 32);
        memcpy(write + offset, words, 32);
    }
    for (; offset < len; offset += 4) {
        u32 word;
        memcpy(&word, read + offset, 4);
        memcpy(write + offset, &word, 4);
    }
}

// ---- OAM shadow state (BSS on the DS) -------------------------------------
// Legacy isolated gates use direct-fill mode with separate backing arrays.
// The full Stage target overrides these weak definitions with strong contiguous
// BSS aliases from stage_state_bss.py, including the original zero mode flag.
// Reset's forward copies then expand the seed through both native buffers.
unsigned char data_0209e660 = 1;
int data_0209e664, data_0209e668, data_0209e66c, data_0209e670;
unsigned int data_0209e674[0x100];   /* main OAM shadow, 0x400 bytes */
unsigned int data_0209ea74[0x100];   /* sub OAM shadow */
int data_0209e67c[0x20];
int data_0209e694[0x100];

/* asm primitive: 4x3 fx32 identity */
void Matrix4x3_LoadIdentity(int *m)
{
    memset(m, 0, 12 * sizeof(int));
    m[0] = m[4] = m[8] = 0x1000;
}

/* asm primitive: expand a 3x3 fx32 matrix (read from a) into a 4x4 (b):
 * fourth column zero, last row {0,0,0,1.0}. The decomp declares the args
 * (dst,src) but r0 is the READ side; register semantics win here. */
void func_02052514(int *a, int *b)
{
    for (int r = 0; r < 3; ++r) {
        b[r * 4 + 0] = a[r * 3 + 0];
        b[r * 4 + 1] = a[r * 3 + 1];
        b[r * 4 + 2] = a[r * 3 + 2];
        b[r * 4 + 3] = 0;
    }
    b[12] = b[13] = b[14] = 0;
    b[15] = 0x1000;
}

/* The original ITCM submit is synchronous. The GX IRQ pump has a different
 * contract: it can return between chunks while the display list is pending.
 * Keep the original chunk order and completion waits so the next matrix or
 * material cannot become part of an unfinished packed command. */
void DMAStartTransfer(int ch, int src, int dst, int ctrl);
void func_01ffde98(int ch, int src, int size)
{
    if (size == 0)
        return;

    const u32 controlAddress = 0x040000b8u + u32(ch) * 12u;
    while (ntr::io_read(controlAddress, 4) & 0x80000000u) {}

    u32 remaining = u32(size);
    u32 cursor = u32(src);
    while (remaining) {
        const u32 chunk = remaining > 0x1d8u ? 0x1d8u : remaining;
        DMAStartTransfer(ch, int(cursor), 0x04000400,
                         int(0x84400000u | (chunk >> 2)));
        remaining -= chunk;
        cursor += chunk;
    }

    while (ntr::io_read(controlAddress, 4) & 0x80000000u) {}
}

// ---- GX bank plumbing ----------------------------------------------------
// Begin/End LoadTex unmap the destination banks to LCDC and remap after; the
// legacy flat host VRAM window is always CPU-visible, so they are no-ops.
// The complete 3DS target overrides these weak HAL entry points with the
// banked SDK implementation in platform/3ds/gx_upload_physical.cpp.
void _ZN2GX12BeginLoadTexEv(void) {}
void _ZN2GX10EndLoadTexEv(void) {}
void _ZN2GX16BeginLoadTexPlttEv(void) {}
void _ZN2GX14EndLoadTexPlttEv(void) {}

// GX::LoadTex destination resolution (see src/_ZN2GX7LoadTexEPKvjj.cpp):
//   data_020a60ac  base window address        -> texture slots at 0x06800000
//   data_020a60bc  secondary bank address     -> 0 = single flat window
//   data_020a60c0  where the window splits    -> unused while _bc is 0
//   data_02099fd0  preferred DMA channel      -> -1 = CPU copy path
u32 data_020a60ac = 0x06800000u;
u32 data_020a60bc = 0;
u32 data_020a60c0 = 0;
int data_02099fd0 = -1;

// LoadTexPltt destination base (palette slots, same convention)
u32 data_020a60b0 = 0x06880000u;

// ---- Model texture-VRAM budget -------------------------------------------
// Two shared arenas (semantics recovered from LoadCompressedTextureToVram +
// GetVramOffset together):
//   arena A (slot 0): compressed 4x4 BLOCKS rise from _bc8, plain textures
//     fall from _be8 (the ceiling); the loader crashes when they meet.
//   arena B (slot 1): compressed INDEX words rise from _be0 in lockstep
//     (half the block bytes), plain textures fall from _bdc.
// Starting _bc8 at 0 and _be0 at slot 1 (0x20000) keeps the hardware rule
// "index address = slot1 + block_offset/2" true for every compressed
// texture, which is exactly what the GX-side decoder assumes.
u32 data_020a4bc8 = 0;
u32 data_020a4be8 = 0x20000u;
u32 data_020a4be0 = 0x20000u;
u32 data_020a4bdc = 0x40000u;
u32 data_020a4be4 = 0;      /* running total of uploaded texture bytes */
// Palette window: small palettes bump UP from _bcc, large ones DOWN from
// _bd8 (the ceiling); LoadTexAndPal crashes when they meet. 96K = banks
// E+F+G, the full DS texture-palette budget.
u32 data_020a4bcc = 0;
u32 data_020a4bd8 = 0x18000u;

// ---- render context globals ----------------------------------------------
u32 data_020a4bd4;          /* 1 << (scaleShift + 12), set by Render */
void *data_020a4bd0;        /* current model matrix */
unsigned char data_020a4bbc;    /* texture-matrix mode of the bound material */
short data_020a4bc0;
short data_020a4bc4;

/* processing-list heads the actor lifecycle links into */
int data_020a4b78[4];
int data_020a4b88[4];
int data_020a4b98[4];
int data_02099f24[4];

// ---- common model data array (BSS on the DS) ------------------------------
// ROM BSS span 0209cefc..0209d3ac is 0x4b0 bytes: 100 records.
// A menu-to-Stage transition can exceed 64 and previously overwrote the
// adjacent native actor scheduler's callback words.
int data_0209cefc[3 * 100];
static_assert(sizeof(data_0209cefc)==0x4b0,"common model data ROM capacity");
int data_0209cef8[1];       /* record count */
int data_0208e738[1] = { 1 };   /* nonzero: upload textures on registration */
int _ZTV9ModelBase[8];       /* ModelBase vtable storage; never dispatched */
int data_0209b3ec[12];      /* camera Matrix4x3 the render walk composes */

// Model's Itanium vtable storage: installed by the C ctor. The gates call
// every method non-virtually (qualified), so plain zero slots are fine; if
// anything ever dispatches through them it faults immediately and loudly.
void *_ZTV5Model[8];
// Same story for the animated-model hierarchy: primary vtables plus the
// multiple-inheritance thunk table the ModelAnim ctor installs at +0x50.
void *_ZTV9Animation[8];
void *_ZTV9ModelAnim[10];
void *VTable_Animation_ModelAnimThunk[8];
void *_ZTV4dBgW[13];   /* base: never dispatched in the gates */
unsigned char data_020a0c78[8]; /* the default CLPS ENTRY (8-byte storage,
                                   func_02037e9c fills it on first lookup) */

// Data constants of the render/animation walk:
// - data_02099f80/f84: 3-byte weight tables for the BCA keyframe samplers.
//   The interpolation is (lo*frac + hi*rem) over 2^shift steps and frac
//   starts at b[shift] minus the step remainder, so b[shift] = 1 << shift.
//   b[0] is unreachable (shift==0 early-returns).
// - data_02099f88: default unit scale, including Render(NULL) billboards.
// - data_02099f94: texture-matrix dimension lookup. Both are already present
//   in the ARM9 ROM data, not zero-initialized storage awaiting GX startup.
unsigned char data_02099f80[4] = { 1, 2, 4, 0 };
unsigned char data_02099f84[4] = { 1, 2, 4, 0 };
int data_02099f88[3] = {4096, 4096, 4096};
unsigned short data_02099f94[8] = {4, 8, 16, 32, 64, 128, 256, 512};

} /* extern "C" */

// The LoadTex TU declares its globals at C++ linkage; alias them to the
// C-named storage above (same mechanism as gate 3a's data aliases).
#pragma comment(linker, "/alternatename:?data_020a60ac@@3IA=_data_020a60ac")
#pragma comment(linker, "/alternatename:?data_020a60bc@@3IA=_data_020a60bc")
#pragma comment(linker, "/alternatename:?data_020a60c0@@3IA=_data_020a60c0")
#pragma comment(linker, "/alternatename:?data_02099fd0@@3HA=_data_02099fd0")

// ---- upload-chain bridges -------------------------------------------------
// C name -> the real namespace-style GX::LoadTex from src/.
namespace GX {
void LoadTex(const void *src, unsigned offset, unsigned size);
}
extern "C" void _ZN2GX7LoadTexEPKvjj(const void *s, unsigned o, unsigned z)
{
    GX::LoadTex(s, o, z);
}

// DMA fallback DMASyncWordTransfer uses; same synchronous copy semantics.
extern "C" void DMAStartTransferFB(unsigned char ch, u32 src, u32 dst, u32 ctrl)
{
    DMAStartTransfer(ch, (int)src, (int)dst, (int)ctrl);
}

// operator_new2 on the DS is a register-passthrough tail-call veneer to
// func_0203cc0c; that shape breaks on cdecl (the callee would read the
// wrong stack slot), so the host bridge passes the argument explicitly.
extern "C" void *func_0203cc0c(unsigned size);
extern "C" void *_ZN6Memory13operator_new2Ej(unsigned size)
{
    return func_0203cc0c(size);
}

// OAM::Reset declares its globals at C++ linkage; alias them onto the
// C-named storage above (same mechanism as the LoadTex globals).
#pragma comment(linker, "/alternatename:?data_0209e660@@3EA=_data_0209e660")
#pragma comment(linker, "/alternatename:?data_0209e664@@3HC=_data_0209e664")
#pragma comment(linker, "/alternatename:?data_0209e668@@3HC=_data_0209e668")
#pragma comment(linker, "/alternatename:?data_0209e66c@@3HC=_data_0209e66c")
#pragma comment(linker, "/alternatename:?data_0209e670@@3HC=_data_0209e670")
#pragma comment(linker, "/alternatename:?data_0209e674@@3PAUOAMEntry@@A=_data_0209e674")
#pragma comment(linker, "/alternatename:?data_0209ea74@@3PAHA=_data_0209ea74")
#pragma comment(linker, "/alternatename:?data_0209e67c@@3PAHA=_data_0209e67c")
#pragma comment(linker, "/alternatename:?data_0209e694@@3PAHA=_data_0209e694")
