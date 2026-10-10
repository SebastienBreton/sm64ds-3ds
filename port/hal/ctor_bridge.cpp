// Constructor-variant bridges: the C1/C2 pair collapses on a host compiler.
//
// mwccarm targets the Itanium ABI, which emits TWO constructor symbols per
// class -- C1 for a complete object and C2 for a base subobject -- and this
// decomp enrolls each variant in its own file, objisolate stripping the
// sibling the file does not own. That is correct for the cartridge and it is
// why src/_ZN9AnimationC1Ev.cpp and src/_ZN9AnimationC2Ev.cpp both exist with
// the same body.
//
// MSVC has ONE constructor symbol. Compile both files for the host and the
// pair lands on the same decorated name -- LNK2005, a duplicate definition
// that says nothing about the port and everything about the ABI difference.
// So the slice manifests carry exactly one file per pair, and the OTHER
// variant's raw Itanium name is defined here.
//
// The same seam catches the reverse case: a constructor that migrated from
// `extern "C" void _ZN5ModelC1Ev(void *)` to a real `Model::Model()` keeps
// callers -- still-flat C TUs, and local shadow classes -- that spell the raw
// _ZN...C1Ev name. Those callers are cdecl with `this` as an ordinary first
// argument, while the real method is __thiscall with `this` in ECX, so these
// have to be genuine forwarding functions. A /alternatename alias would link
// and then corrupt the stack.
//
// common.h comes FIRST deliberately. It and math/Matrix.h share one include
// guard for two spellings of Matrix4x3 (flat `s32 m[12]` versus structured
// `Matrix3x3 r; Vector3 t;`), and whichever a TU sees first stands. Model
// embeds one, so a TU that saw the other spelling could disagree about
// sizeof(Model) -- and placement new here would construct against the wrong
// layout. src/_ZN5ModelC1Ev.cpp orders its includes the same way, for the
// same reason.
#include "common.h"

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


#include <new>
#include <stdio.h>
#include <stdlib.h>

#include "Model.h"
#include "ModelBase.h"


// ModelBase and dBgW are abstract -- their headers declare pure virtuals
// because the ROM leaves those vtable slots null. Placement new cannot name
// an abstract type, so the base-subobject constructor is reached through a
// trivial concrete heir: no members, no new virtuals, so single inheritance
// gives it the base's exact layout and the base constructor runs over the
// same bytes. The vptr it leaves behind is the heir's, which is harmless
// precisely because these are C2 base-subobject constructors -- every caller
// is a derived constructor that stores its own vptr on the next line. The
// asserts below are what keeps that claim honest.
namespace {
struct ModelBaseHeir : ModelBase {
    int DoSetFile(char *, int, int) { return 0; }
};
}

static_assert(sizeof(ModelBaseHeir) == sizeof(ModelBase), "ModelBase heir changed the layout");
// ::new, not new: several of these classes declare a class-scope operator new
// that routes through the DS heap, and class-scope lookup would hide the
// placement form.
#define CTOR_BRIDGE(sym, cls) \
    void sym(void *self) { ::new (self) cls(); }

extern "C" {

// Model is special: the port dispatches it through the SYNTHETIC _ZTV5Model,
// which hal/cxxname_bridge.cpp deliberately fills at both slot 4 and slot 5 so
// that Model.h-compiled callers (MSVC numbering, one destructor slot) and
// local-shadow callers (ROM/Itanium numbering, two destructor slots) can share
// one object. Placement new installs MSVC's own five-slot vtable instead, and
// smoke_actor's `vcall0(actor, 9)` -- Itanium numbering for Render -- then
// reads off the end of it and calls null. So the constructor runs for its
// member initialization and the vptr is put back to the table the port
// dispatches through, which is exactly what src/_ZN5ModelC2Ev.cpp does on the
// line after its own base-constructor call.
extern "C" void *_ZTV5Model[8];
void _ZN5ModelC1Ev(void *self)
{
    ::new (self) Model();
    *(void **)self = _ZTV5Model;
}
CTOR_BRIDGE(_ZN9ModelBaseC2Ev, ModelBaseHeir)


// The DS global operator delete routes through the Memory layer. This lived
// in cxxname_bridge.cpp, which only smoke_actor links; every target that
// builds a migrated model-family constructor needs it now, because the
// synthesized ModelBase::operator delete references it.
void _ZN6Memory10DeallocateEPv(void *p);
void _ZN6Memory16operator_delete2EPv(void *p) { _ZN6Memory10DeallocateEPv(p); }

}

// Model::~Model() is NOT bridged here: this branch promotes
// src/_ZN5ModelD1Ev.cpp is a real C++ destructor and the gate-4b slice
// compiles it, so MSVC takes slot 0 from src/ directly. Defining it here as
// well would be a duplicate definition.

// ModelBase destruction is now provided by the recovered native destructor.

// src/func_0203cbc0.c is a tail-call veneer to _ZdlPv, the DS global operator
// delete, and Model's destructor calls it. The veneer is sliced now, so its
// target routes through the Memory layer here -- matching how _Znwj is
// handled in hal/cxxname_bridge.cpp.
#if defined(_MSC_VER)
extern "C" void _ZdlPv(void *p) { _ZN6Memory10DeallocateEPv(p); }
#else
/* Under GCC this symbol IS the program-wide ::operator delete, so the host's
   own containers (std::vector in ntr/gx.cpp) free through it too. Route by
   address: game-arena blocks go back to the game heap, the rest to libc --
   which is also what ::operator new (libstdc++, malloc) handed out. The sized
   form C++14 emits for host code lands here as well. */
extern "C" int hal_arena_contains(const void *p);
extern "C" __attribute__((weak)) bool sm64ds_on_present_worker() { return false; }
// Game-heap blocks released on the core-1 worker wait for the game side.
static void *deferred_frees[256];
static unsigned deferred_count;
static int deferred_lock;
static bool defer_free(void *p)
{
    while (__sync_lock_test_and_set(&deferred_lock, 1)) {}
    const bool queued = deferred_count < 256;
    if (queued) deferred_frees[deferred_count++] = p;
    __sync_lock_release(&deferred_lock);
    return queued;
}
// Call while the game thread is parked.
extern "C" void hal_flush_deferred_frees()
{
    void *pending[256];
    while (__sync_lock_test_and_set(&deferred_lock, 1)) {}
    const unsigned count = deferred_count;
    for (unsigned i = 0; i < count; ++i) pending[i] = deferred_frees[i];
    deferred_count = 0;
    __sync_lock_release(&deferred_lock);
    for (unsigned i = 0; i < count; ++i) _ZN6Memory10DeallocateEPv(pending[i]);
}
extern "C" void _ZdlPv(void *p)
{
    if (!p)
        return;
    if (hal_arena_contains(p)) {
        if (sm64ds_on_present_worker()) {
            if (defer_free(p)) return;
            fprintf(stderr, "FATAL: worker game-heap free queue full\n");
            abort();
        }
        _ZN6Memory10DeallocateEPv(p);
    }
    else
        free(p);
}
extern "C" void _ZdlPvj(void *p, unsigned int) { _ZdlPv(p); }
#endif
