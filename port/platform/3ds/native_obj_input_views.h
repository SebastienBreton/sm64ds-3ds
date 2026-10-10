#pragma once
#include "native_obj_plan.h"
#include "ntr/ppu.h"

namespace ds_gpu {
// Lazy host-owned OBJ read views, independent of CPU LCDC aliases. A null bus
// preserves the legacy flat views. Returned snapshots are immutable until the
// next preparation of the same engine; byte comparisons remain cache identity.
// Call before BG-only preparation; no VRAM publication or texture snapshots.
bool native_obj_admit(ntr::Engine engine);
const ObjPlan* native_obj_prepare(ntr::Engine engine);
ObjFallback native_obj_fallback(ntr::Engine engine);
// Read-only, worker-safe: would native_obj_prepare(engine) yield the last plan?
bool native_obj_unchanged(ntr::Engine engine);
}
