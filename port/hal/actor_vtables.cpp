#include "unmatched/actor_registry.h"
// dActor_c-hierarchy vtables (gate 9), per the vtable law (clsn_vtable.cpp):
// MSVC slot order. include/fBase_c.h declares the dtor LAST, so MSVC and
// the ROM agree on slots 0..15 and diverge only at the tail -- the header
// was built for exactly this. Lifecycle slots forward to the class's own
// overrides where they exist and to the fBase_c/dActor_c defaults where
// they do not; the tail traps.
//
// Base-class vtable symbols the ctor chain installs and then overwrites
// (never dispatched between installs) are plain storage.
#include <stdio.h>
#include <string.h>

/* x86 __thiscall emulation: `this` in ecx via __fastcall, with a dummy edx
   parameter (PORT_EDX). On ARM `this` is simply r0, so both vanish. */
#ifndef PORT_EDX
#ifdef _MSC_VER
#define PORT_EDX , void *
#else
#define PORT_EDX
#define __fastcall
#define __thiscall
#endif
#endif

#include <stdlib.h>

#include "fBase_c.h"
#include "daObjYajirusi_c.h"


// The lifecycle definitions are MSVC methods (daObjYajirusi_c.h/fBase_c.h
// real classes). Every shim calls QUALIFIED -- never virtual.
extern "C" int _ZN15daObjYajirusi_c13InitResourcesEv(char *self)
{ return ((daObjYajirusi_c *)self)->daObjYajirusi_c::InitResources(); }

static int __fastcall sl_init(void *self PORT_EDX)
{ return _ZN15daObjYajirusi_c13InitResourcesEv((char *)self); }
static int __fastcall sl_cleanup(void *self PORT_EDX)
{ return ((daObjYajirusi_c *)self)->daObjYajirusi_c::CleanupResources(); }
static int __fastcall sl_behavior(void *self PORT_EDX)
{ return ((daObjYajirusi_c *)self)->daObjYajirusi_c::Behavior(); }
static int __fastcall sl_render(void *self PORT_EDX)
{ return ((daObjYajirusi_c *)self)->daObjYajirusi_c::Render(); }
static int __fastcall sl_binit(void *self PORT_EDX)
{ return ((fBase_c *)self)->fBase_c::BeforeInitResources(); }
static void __fastcall sl_ainit(void *self PORT_EDX, u32 a)
{ ((fBase_c *)self)->fBase_c::AfterInitResources(a); }
static int __fastcall sl_bclean(void *self PORT_EDX)
{ return ((fBase_c *)self)->fBase_c::BeforeCleanupResources(); }
static void __fastcall sl_aclean(void *self PORT_EDX, u32 a)
{ ((fBase_c *)self)->fBase_c::AfterCleanupResources(a); }
static int __fastcall sl_bbeh(void *self PORT_EDX)
{ return ((fBase_c *)self)->fBase_c::BeforeBehavior(); }
static void __fastcall sl_abeh(void *self PORT_EDX, u32 a)
{ ((fBase_c *)self)->fBase_c::AfterBehavior(a); }
static int __fastcall sl_bren(void *self PORT_EDX)
{ return ((fBase_c *)self)->fBase_c::BeforeRender(); }
static void __fastcall sl_aren(void *self PORT_EDX, u32 a)
{ ((fBase_c *)self)->fBase_c::AfterRender(a); }
static int __fastcall sl_pdes(void *self PORT_EDX)
{ ((fBase_c *)self)->fBase_c::OnPendingDestroy(); return 0; }
static int __fastcall sl_heap(void *self PORT_EDX)
{ return ((fBase_c *)self)->fBase_c::OnHeapCreated(); }

#define ATRAP(n) \
    static void __fastcall a_trap##n(void * PORT_EDX) { \
        fprintf(stderr, "FATAL: daObjYajirusi_c vtable slot %d trap\n", n); \
        abort(); }
ATRAP(13) ATRAP(14) ATRAP(16) ATRAP(17)

extern "C" void *_ZTV15daObjYajirusi_c[20] = {
    (void *)sl_init,     /* 0  InitResources */
    (void *)sl_binit,    /* 1  BeforeInitResources */
    (void *)sl_ainit,    /* 2  AfterInitResources */
    (void *)sl_cleanup,  /* 3  CleanupResources */
    (void *)sl_bclean,   /* 4  BeforeCleanupResources */
    (void *)sl_aclean,   /* 5  AfterCleanupResources */
    (void *)sl_behavior, /* 6  Behavior */
    (void *)sl_bbeh,     /* 7  BeforeBehavior */
    (void *)sl_abeh,     /* 8  AfterBehavior */
    (void *)sl_render,   /* 9  Render */
    (void *)sl_bren,     /* 10 BeforeRender */
    (void *)sl_aren,     /* 11 AfterRender */
    (void *)sl_pdes,     /* 12 OnPendingDestroy */
    (void *)a_trap13, (void *)a_trap14,
    (void *)sl_heap,     /* 15 OnHeapCreated */
    (void *)a_trap16, (void *)a_trap17,
    0, 0,
};

// ExclamationSwitch still uses a literal ROM-vtable boundary. The dBase_c and
// dActor_c installs are now compiler-owned by the native constructor sources.
extern "C" {
void *_ZTV17ExclamationSwitch[20];
}

// ---- fBase_c::fBase_c() transcription ---------------------------------
// The ROM ctor is a hand-asm block (src/actors/ActorBase.cpp); this is its
// C transcription, field for field against the disassembly there. The spawn
// CONTEXT globals it reads (pending actor ID, area byte, the spawn-info
// pointer table for the two processing-list priorities) are storage here;
// the smoke seeds them the way func_02010e78/dBase_c::Spawn would.
extern "C" {
void _ZN7fBase_c9SceneNodeC1Ev(void *node);
int func_0203b438(void *a, void *b, void *c);
int func_02043810(void *p);

int data_02099edc[8];           /* the transient fBase_c vtable install */
int data_02099e70[1];           /* next unique actor id */
int data_020a4b60[1];
unsigned short data_020a4b54;   /* PENDING ACTOR ID (the spawn context) */
unsigned char data_020a4b48;    /* pending area byte */
int data_020a4b64[1];
void *data_020a4b68;            /* active list iterator (ROM BSS, 4 bytes) */
int data_020a4ba8[4];          /* behavior list storage, 0x10-byte ROM BSS extent */
int data_020a4b6c[8];           /* the scene tree root the ctor links into */
void *data_020a4bb8_storage[512];
void **data_020a4bb8 = data_020a4bb8_storage;  /* actorID -> SpawnInfo* */

void *_ZN7fBase_cC2Ev(char *self)
{
    *(void **)self = data_02099edc;
    _ZN7fBase_c9SceneNodeC1Ev(self + 0x14);
    *(void **)(self + 0x24) = self;             /* sceneNode.actor */
    for (int off = 0x28; off <= 0x38; off += 0x10) {
        *(void **)(self + off) = 0;
        *(void **)(self + off + 4) = 0;
        *(void **)(self + off + 8) = self;
        *(unsigned short *)(self + off + 0xc) = 0;
        *(unsigned short *)(self + off + 0xe) = 0;
    }
    int id = data_02099e70[0];
    *(int *)(self + 4) = id;
    data_02099e70[0] = id + 1;
    *(int *)(self + 8) = data_020a4b60[0];
    *(unsigned short *)(self + 0xc) = data_020a4b54;
    *(unsigned char *)(self + 0x12) = data_020a4b48;
    func_0203b438(data_020a4b6c, self + 0x14, (void *)(size_t)data_020a4b64[0]);
    {
        unsigned short *info = (unsigned short *)data_020a4bb8[
            *(unsigned short *)(self + 0xc)];
        *(unsigned short *)(self + 0x28 + 0xc) = info[2];   /* prio at +4 */
        *(unsigned short *)(self + 0x28 + 0xe) = info[2];
        *(unsigned short *)(self + 0x38 + 0xc) = info[3];   /* prio at +6 */
        *(unsigned short *)(self + 0x38 + 0xe) = info[3];
    }
    {
        char *parent = (char *)(size_t)func_02043810(data_020a4b6c);
        if (parent) {
            unsigned char pf = *(unsigned char *)(parent + 0x13);
            if (pf & 3)
                *(unsigned char *)(self + 0x13) |= 2;
            if (pf & 0xC)
                *(unsigned char *)(self + 0x13) |= 8;
        }
    }
    return self;
}
} /* extern "C" */

// ---- gate-9 storage and bridges -------------------------------------------
extern "C" {
#ifndef SM64DS_NATIVE_SHADOW
void *_ZTV11ShadowModel[8];
#endif

// ov098's SharedFilePtr entry table for the arrow signs: three-pointer
// entries {model, kcl, ?}. The smoke seeds the pointers with its own
// SharedFilePtr objects (on DS these live prebuilt in the overlay's data).
void *data_ov098_0213c380[6];
} /* extern "C" */

extern "C" {
/* asm primitive: plain 48-byte block copy (with writeback, unlike the
   FIFO-fixed variant) */
void Copy48Bytes(int *src, int *dst) { for (int i = 0; i < 12; ++i) dst[i] = src[i]; }
// Matrix4x3 scratch used by dBgW_KcMbg::SetFile: 12 words, not 4.
int data_020a0e68[12];
short data_0208e378;
short *data_0209b45c;      /* spawn default rotation ptr (null = none) */
short *data_0209b460;      /* spawn default position ptr */
signed char data_0209b44c_c;
int data_0209b468[4];      /* actor list head the ctor links into */
}
#pragma comment(linker, "/alternatename:?data_0209b44c@@3CA=_data_0209b44c_c")
#pragma comment(linker, "/alternatename:_data_0209b44c=_data_0209b44c_c")

// The C base constructor stores this address before the derived spawn replaces
// it with daObjYajirusi_c's manual host vtable. No base virtual dispatch occurs
// during that interval, but the storage must exist for the raw relocation.
extern "C" void *_ZTV10dBgActor_c[32] = {};

extern "C" {
unsigned char data_0209f2d8_c;   /* mega-char state byte: none */
int data_0209ceec[4];            /* shadow scale globals */
int data_0209cef4[4];
}
#pragma comment(linker, "/alternatename:_data_0209f2d8=_data_0209f2d8_c")
#pragma comment(linker, "/alternatename:?data_020a0e68@@3UMatrix4x3@@A=_data_020a0e68")

extern "C" {
void Matrix4x3_FromRotationY(void *m, int a);
void hal_m43_roty(void *m, int a) { Matrix4x3_FromRotationY(m, a); }
}

extern "C" {
/* asm primitive: 4x3 fx32 Y-rotation from (sin, cos):
   rows {c,0,-s},{0,1,0},{s,0,c},{0,0,0} */
void func_02052820(int *m, int s, int c)
{
    m[0] = c;      m[1] = 0; m[2] = -s;
    m[3] = 0;      m[4] = 0x1000; m[5] = 0;
    m[6] = s;      m[7] = 0; m[8] = c;
    m[9] = 0;      m[10] = 0; m[11] = 0;
}
}

// Cleanup-path heap wrappers: unnamed on the DS side here; the semantics
// are the gate-3a Memory layer's. Destroy is never reached in the gate
// (the smoke does not tear its heap-owning actor down), so it traps.
extern "C" {
void Memory_Deallocate(void *p);
void Heap_Destroy(void *h)
{
    (void)h;
    fprintf(stderr, "FATAL: Heap_Destroy reached (unwired)\n");
    abort();
}
}

extern "C" {
int func_0204424c(char *c);
int hal_f0204424c(char *c) { return func_0204424c(c); }
}

// Runtime-owned camera and particle manager roots (initialized by scene setup).
extern "C" { void *data_0209f318; void *data_0209ee74; }

// SDK matrix primitives, transcribed from their Thumb store sequences.
extern "C" void func_02052800(int *m, int s, int c)
{
    const int rows[12] = {4096, 0, 0, 0, c, s, 0, -s, c, 0, 0, 0};
    memcpy(m, rows, sizeof rows);
}
extern "C" void func_0205283c(int *m, int s, int c)
{
    const int rows[12] = {c, s, 0, -s, c, 0, 0, 0, 4096, 0, 0, 0};
    memcpy(m, rows, sizeof rows);
}

// Original zero-initialized state. Scene/audio initialization must populate
// these roots before the corresponding recovered runtime is executed.
extern "C" {
unsigned char data_0209b480; // sound enabled, Player_PlaySoundEffect
int data_0209b4a4[2];       // sound handle + distance, func_02048908
signed char data_0209f2f4;  // lives, GiveLives
void *data_0209f5c0;        // parent actor root
void *data_020a4bec;        // audio owner, func_02048f34
int data_020a4bf8[2][2];    // two 8-byte handles, func_02048f34
int data_020a4c18[6][2];    // six 8-byte handles, func_02048f34
void *data_020a5bb8;        // sound archive root, func_02050f34
}

extern "C" { unsigned char data_020a4b4c; unsigned short data_020a4b50; }

extern "C" { int (*data_020a4b58)(void *); } // optional factory preflight callback

// Sound player lists: func_0204fc40 initializes two 12-byte iterators and
// 32 entries, each containing two iterators plus a 32-bit limit (0x1c).
extern "C" {
unsigned int data_020a4d54[3];
unsigned int data_020a4d60[3];
unsigned int data_020a4d6c[32][7];
// Audio command/status storage is generated by stage_state_bss.py so the
// original initializer's interior symbols address the same command pool.
}

// Death table: 3 parts ×512 actor bits; ROM BSS 0209f4f8..0209f5b8.
extern "C" { signed char data_0209f2f8; unsigned int data_0209f4f8[3 * 16]; }

// Actor visibility state; scene setup owns initialization.
extern "C" {
unsigned char data_0209f20c, data_0209f274, data_0209f294, data_0209f2c4;
unsigned int data_0209b464;
int data_0209fc68;
unsigned int data_0209f43c[0x5c / 4]; // Clipper instance, ROM BSS extent
void *data_0209f314; // area array, 12-byte entries
}

extern "C" { void (*data_020a4b5c)(void *); } // optional post-cleanup hook
