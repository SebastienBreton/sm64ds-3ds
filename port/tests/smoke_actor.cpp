#include "unmatched/actor_registry.h"
extern "C" void Crash();
extern "C" void InitCrashScreen();
extern "C" void ShowCrashScreen();
extern "C" unsigned char data_0209cdd0[];
// Gate-9 smoke: a real actor lives its lifecycle on host.
//
// daObjYajirusi_c_classInit_YAJIRUSI_R allocates from the game heap and runs the ctor
// chain (dBgActor_c -> dActor_c -> fBase_c, Model and dBgW_KcMbg
// subobjects, ShadowModel); then every lifecycle step dispatches THROUGH
// THE VTABLE exactly as the game's processing lists do: InitResources
// loads the sign's model and collider through the full asset pipeline,
// Behavior ticks, Render walks the model into the GX. This is the vtable
// law's first full-class workout -- the rung before Player.
#include <cmath>

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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "ntr/ppu.h"

#include "fault_probe.h"


typedef unsigned int u32;

extern "C" {
int *daObjYajirusi_c_classInit_YAJIRUSI_R(void);
int func_02043098(int actorID, int parentNode, int param, int kind);
int func_02043288(void *actor); // before/behavior/after
int func_0204322c(void *actor); // before/render/after
void _ZN7Clipper13Func_020156DCEitii(void *, int, unsigned short, int, int);
extern unsigned int data_0209f43c[];
extern void *data_0209f314;
void __sinit_02075154();
void func_02044120();
extern int data_02099f24[];
void *_ZN7dBase_c5SpawnEjP7fBase_cii(unsigned int, void *, int, int);
extern unsigned char data_020a4b4c;
extern unsigned short data_020a4b50;
void *_ZN4Heap13SetupRootHeapEv(void);
extern int data_0209b3ec[12];       /* camera matrix */
/* the spawn context dBase_c::Spawn would have staged */
struct SharedFilePtrC { unsigned short fileID; unsigned char numRefs;
                        unsigned char pad; void *filePtr; };
SharedFilePtrC *_ZN13SharedFilePtr9ConstructEj(SharedFilePtrC *s, u32 id);
extern unsigned short data_020a4b54;    /* pending actor ID */
extern void *data_ov098_0213c380[6];    /* {model,kcl,?} entry table */
extern char data_ov098_0213c384[0x18];  /* column-b view of the same */
extern void *data_020a0eac_c;           /* actor heap = root heap */
extern void *data_020a0ea0;             /* defaultHeapPtr (gate 3a) */
extern void *data_0209f394[];           /* the player array */
extern unsigned char data_0209f21c;     /* player count */
void hal_fill_model_vtable(void);
void hal_fill_shadow_vtable(void);
void hal_fill_mmc_vtable(void);
}

static int *trace_sign_spawn()
{
    int *p = daObjYajirusi_c_classInit_YAJIRUSI_R();
    printf("  constructed actor id=%#x param=%#x\n",
           p ? *(unsigned short *)((char *)p + 0xc) : 0, p ? p[2] : 0);
    return p;
}

/* the fBase_c virtual surface, MSVC view: dispatch helpers */
typedef int (__thiscall *Fn0)(void *);
static int vcall0(void *actor, int slot)
{
    void **vt = *(void ***)actor;
    return ((Fn0)vt[slot])(actor);
}

static int g_failures;
#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void ident_fx(void *m)
{
    memset(m, 0, 48);
    ((int *)m)[0] = ((int *)m)[4] = ((int *)m)[8] = 0x1000;
}

static void reset_scene()
{
    ntr::gx_reset();
    NTR_MMIO(uint32_t, 0x04000440) = 0;
    NTR_MMIO(uint32_t, 0x04000454) = 0;
    NTR_MMIO(uint32_t, 0x04000440) = 1;
    NTR_MMIO(uint32_t, 0x04000454) = 0;
    NTR_MMIO(uint32_t, 0x04000580) = 0u | (0u << 8) | (255u << 16) | (191u << 24);
    ntr::gx_set_light(0, -0.4f, -0.6f, -0.7f, 0x7FFF);
    ntr::gx_enable_lights(0x1);
}

int main(void)
{
    PORT_INSTALL_FAULT_PROBE();
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!ntr::io_init()) { fprintf(stderr, "io_init failed\n"); return 2; }
    CHECK(_ZN4Heap13SetupRootHeapEv() != NULL);
    __sinit_02075154();
    static unsigned char test_area[12] = {};
    test_area[4] = 1; // IsAreaShowing reads byte4 of the12-byte area record.
    data_0209f314 = test_area;
    ident_fx(data_0209b3ec);
    // Place the test camera behind the origin: native visibility tests must
    // see the sign in front of the near plane, not at the camera itself.
    data_0209b3ec[11] = -200 * 4096;
    _ZN7Clipper13Func_020156DCEitii(data_0209f43c, 0x1555, 0xe38, 0x1000, 0x1388000);

    /* stage the spawn context: actor 0x12b = daObjYajirusi_c, its SpawnInfo
       priorities, the actor heap, and the ov098 file entries (handle 1177 =
       yajirusi_r.bmd, 1178 = its kcl) in both column views */
    hal_fill_model_vtable();
    hal_fill_shadow_vtable();
    hal_fill_mmc_vtable();
    data_020a4b54 = 0x12b;
    struct SpawnInfo {
        int *(*create)(); unsigned short behaviorPriority, renderPriority;
        unsigned int flags; int offsetY, radius, clipDistance, farDistance;
    };
    static_assert(sizeof(SpawnInfo) == 0x1c, "native actor profile layout");
    // YAJIRUSI_L profile at overlay98:0213c398. Replace only the ROM code
    // pointer; retain the native priorities, flags and visibility ranges.
    static SpawnInfo spawn_info = { trace_sign_spawn, 0x12b, 0xbc,
                                    2, 0xc0000, 0xe0000, 0x1000000, 0x1000000 };
    data_020a4bb8[0x12b] = &spawn_info;
    data_020a0eac_c = data_020a0ea0;
    static SharedFilePtrC sign_model, sign_kcl;
    _ZN13SharedFilePtr9ConstructEj(&sign_model, 1177);
    _ZN13SharedFilePtr9ConstructEj(&sign_kcl, 1178);
    data_ov098_0213c380[0] = &sign_model;
    data_ov098_0213c380[1] = &sign_kcl;
    *(void **)(data_ov098_0213c384 + 0) = &sign_kcl;

    /* a stand-in player: the actor world assumes at least one exists
       (ClosestPlayer callers deref without a null check -- on the DS
       Mario always exists). Position at +0x5c, near the sign. */
    static char fake_player[0x800];
    data_0209f394[0] = fake_player;
    *(unsigned char *)&data_0209f21c = 1;

    const bool factory = getenv("SM64DS_ACTOR_DIRECT") == NULL;
    int *actor = factory ? (int *)func_02043098(0x12b, 0, 0x1234, 2)
                         : daObjYajirusi_c_classInit_YAJIRUSI_R();
    CHECK(actor != NULL);
    if (!actor) return 1;
    printf("  spawned at %p, vtable %p\n", (void *)actor, *(void **)actor);

    /* the lifecycle, dispatched as the processing lists would */
    int init_ok = factory ? ((unsigned char *)actor)[0xe] : vcall0(actor, 0);
    if (factory) {
        CHECK(actor[2] == 0x1234);
        CHECK(*(unsigned short *)((char *)actor + 0xc) == 0x12b);
        CHECK(data_020a4b4c == 0);
        CHECK(data_020a4b50 == 0xffff);
        printf("  factory param=%#x alive=%d\n", actor[2], init_ok);
        CHECK(_ZN7dBase_c5SpawnEjP7fBase_cii(0x12b, NULL, 7, 2) == NULL);
        printf("  shared model id=%u refs=%u, kcl id=%u refs=%u\n",
               sign_model.fileID, sign_model.numRefs, sign_kcl.fileID, sign_kcl.numRefs);
        printf("  shared handles model=%p kcl=%p, table=%p/%p\n", (void *)&sign_model,
               (void *)&sign_kcl, data_ov098_0213c380[0],
               *(void **)(data_ov098_0213c384 + 0));
        int *child = (int *)_ZN7dBase_c5SpawnEjP7fBase_cii(0x12b, actor, 0x5678, 2);
        CHECK(child != NULL);
        if (!child) return 1;
        CHECK(child[2] == 0x5678);
        CHECK(((unsigned char *)child)[0xe] == 1);
        CHECK(*(void **)((char *)child + 0x14) == (char *)actor + 0x14);
        CHECK(*(void **)((char *)actor + 0x18) == (char *)child + 0x14);
        CHECK(data_ov098_0213c380[0] == &sign_model);
        CHECK(*(void **)(data_ov098_0213c384 + 0) == &sign_kcl);
        CHECK(sign_model.numRefs == 2 && sign_kcl.numRefs == 2);
        printf("  child param=%#x linked to parent scene node\n", child[2]);
    }
    printf("  InitResources -> %d\n", init_ok);
    CHECK(init_ok == 1);

    printf("  actor status kill=%d pause=%d area=%d radius=%d distance=%d cam=(%d,%d,%d)\n",
           ((unsigned char *)actor)[0xf], ((unsigned char *)actor)[0x13],
           ((signed char *)actor)[0xcc], actor[0xb8/4],actor[0xbc/4],actor[0x74/4],actor[0x78/4],actor[0x7c/4]);
    reset_scene();
    for (int f = 0; f < 4; ++f) {
        printf("  tick %d\n", f);
        int result = factory ? func_02043288(actor) : vcall0(actor, 6);
        printf("  behavior result=%d flags=%#x\n", result, *(unsigned int *)((char *)actor + 0xb0));
        CHECK(result == 1);
    }
    if (factory) {
        unsigned char &pause = ((unsigned char *)actor)[0x13];
        pause |= 2;
        CHECK(func_02043288(actor) == 0);
        pause &= ~2;
        CHECK(func_02043288(actor) == 1);

        reset_scene();
        pause |= 8;
        CHECK(func_0204322c(actor) == 0);
        size_t hidden_tris = 0;
        ntr::gx_polygons(hidden_tris);
        CHECK(hidden_tris == 0);
        pause &= ~8;

        test_area[4] = 0;
        CHECK(func_02043288(actor) == 0);
        CHECK(func_0204322c(actor) == 0);
        test_area[4] = 1;
        CHECK(func_02043288(actor) == 1);

        data_0209b3ec[11] = 2000 * 4096; // actor behind the camera
        func_02043288(actor);
        CHECK((actor[0xb0 / 4] & 8) != 0);
        CHECK(func_0204322c(actor) == 0);
        data_0209b3ec[11] = -200 * 4096;
        CHECK(func_02043288(actor) == 1);
        CHECK((actor[0xb0 / 4] & 0x38) == 0);
        printf("  native pause, hidden area and frustum gates checked\n");
    }
    reset_scene();
    printf("  render\n");
    int render_ok = factory ? func_0204322c(actor) : vcall0(actor, 9);       /* Render */
    printf("  Render -> %d\n", render_ok);

    size_t tris = 0;
    ntr::gx_polygons(tris);
    printf("  triangles from the actor's render: %zu\n", tris);
    CHECK(tris > 0);

    if (factory) {
        size_t scheduled_tris = 0;
        for (int frame = 0; frame < 120; ++frame) {
            reset_scene();
            func_02044120();
            ntr::gx_polygons(scheduled_tris);
            CHECK(scheduled_tris == 38);
            CHECK(data_02099f24[0] == 0);
        }
        printf("  scheduler:120 frames, %zu triangles/frame for two actors\n", scheduled_tris);
        CHECK(sign_model.numRefs == 2 && sign_kcl.numRefs == 2);
    }

    if (g_failures) {
        fprintf(stderr, "smoke_actor: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    if (getenv("SM64DS_CRASH_DIAGNOSTIC_TEST")) {
        InitCrashScreen();
        ShowCrashScreen(); // unchanged snapshot should not duplicate the line
        if (data_0209cdd0[0] != 1) return 1;
    }
    if (getenv("SM64DS_TEST_FATAL_CRASH")) {
        printf("Intentional fatal-path test: the runner must report FAIL\n");
        fflush(stdout);
        Crash();
        return 1;
    }
    printf("smoke_actor: all checks passed (an actor spawned, initialized, "
           "behaved and rendered through vtable dispatch)\n");
    return 0;
}
