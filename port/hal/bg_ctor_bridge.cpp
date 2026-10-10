// Constructor-variant bridges and vtable fillers for the collision/background
// family, gate 8.
//
// Split from hal/ctor_bridge.cpp because only the gate-8 and gate-9 targets
// slice these classes; naming them from the shared bridge would drag the whole
// collision tree into smoke_model, which never builds it.
//
// See hal/ctor_bridge.cpp for why these are forwarding functions rather than
// /alternatename aliases, and why an abstract base goes through a heir.
//
// ---- why the virtuals below are traps, not slices ----------------------
// A real C++ constructor makes its TU the vtable's key function for MSVC too,
// so MSVC emits the class vtable there and demands every virtual be defined.
// dBgW declares thirteen; gate 8 exercises the surface queries and one ray
// overload. The rest have real implementations in src/, but slicing them pulls
// in dBgCh_SphCrr, dBgPi, DotVec3, four data_020a0xxx tables and a dozen
// func_0203xxxx -- adopting the collision system into the port, which is a
// piece of work in its own right and not something a constructor migration
// should decide.
//
// So they take the shape hal/clsn_vtable.cpp already established for exactly
// this situation: a definition that exists so the vtable can be emitted, and
// aborts loudly if anything ever dispatches through it. A gate that starts
// needing one of these fails with its name on stderr rather than silently
// returning a plausible zero.
#include "common.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>

#include "dBgPc.h"
#include "dBgW.h"
#include "dBgW_Kc.h"

static void bg_trap(const char *who)
{
    fprintf(stderr, "FATAL: %s dispatched, but the port defines it only so "
                    "MSVC can emit the vtable (hal/bg_ctor_bridge.cpp).\n", who);
    abort();
}

// These fallbacks are weakened after compilation. Keep their abort behavior
// out of callers that will ultimately link to a real strong implementation.
#if defined(__GNUC__) && !defined(__clang__)
#define BG_FALLBACK __attribute__((noipa))
#else
#define BG_FALLBACK
#endif
#define BG_TRAP(sig, who) BG_FALLBACK sig { bg_trap(who); }

// Native base behavior, including the callback's actor/result argument swap.
#include "src/_ZN4dBgW10DetectClsnER9dBgCh_Gnd.cpp"
#include "src/_ZN4dBgW10DetectClsnER9dBgCh_Lin.cpp"
#include "src/_ZN4dBgW10DetectClsnER12dBgCh_SphCrr.cpp"
#include "src/_ZN4dBgW12TransformPosERK7Vector3RS0_.cpp"
#include "src/_ZN4dBgW14GetAngularVelYEv.cpp"
#include "src/_ZN4dBgW11GetVelocityER7Vector3.cpp"
#include "src/_ZN4dBgW9Virtual08Ev.cpp"
#include "src/_ZN4dBgW10BeforeClsnER5dBgPiP8dActor_cR7Vector3P10Vector3_16S7_.cpp"

// The destructors are still flat extern "C" Itanium functions in src/; the
// bridge runs the other way here, so MSVC's slot 0 reaches the ROM body.
/* The ROM destructor releases resources through the collision layer, and
   slicing it pulls that whole tree in (DotVec3, data_020a0cec, cstd::fdiv,
   a dozen func_0203xxxx). MSVC still needs the destructor defined to emit
   the vtable, so it takes hal/clsn_vtable.cpp's shape: present for the
   vtable's sake, loud if anything ever dispatches through it. A gate that
   starts destroying one of these fails with its name on stderr instead of
   leaking quietly. */
BG_TRAP(dBgW::~dBgW(), "dBgW::~dBgW")
BG_TRAP(dBgW_Kc::~dBgW_Kc(), "dBgW_Kc::~dBgW_Kc")

namespace {
// dBgW is abstract: the ROM leaves the surface-query slots null, so the header
// declares them pure. A trivial concrete heir shares its layout exactly.
struct dBgWHeir : dBgW {
    void GetSurfaceInfo(s16, SurfaceInfo &) {}
    void GetNormal(s16, Vector3 &) {}
    void GetTriangleOrigin(s16, Vector3 &) {}
};
}
static_assert(sizeof(dBgWHeir) == sizeof(dBgW), "dBgW heir changed the layout");

extern "C" {
void _ZN4dBgWC2Ev(void *self)    { ::new (self) dBgWHeir(); }
void _ZN7dBgW_KcC1Ev(void *self) { ::new (self) dBgW_Kc(); }
void _ZN5dBgPcC1Ev(void *self)   { ::new (self) dBgPc(); }
void _ZN5dBgPcD1Ev(void *self)   { ((dBgPc *)self)->~dBgPc(); }
}

// dBgW_Kc inherits three DetectClsn overloads; gate 8 dispatches the dBgCh_Lin
// one through the synthetic table and that body is sliced. The other two drag
// in dBgCh_SphCrr and dBgPi, so they take the same shape as the rest here.
BG_TRAP(int dBgW_Kc::DetectClsn(dBgCh_Gnd &), "dBgW_Kc::DetectClsn(dBgCh_Gnd&)")
BG_TRAP(int dBgW_Kc::DetectClsn(dBgCh_SphCrr &), "dBgW_Kc::DetectClsn(dBgCh_SphCrr&)")
