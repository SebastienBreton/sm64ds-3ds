#pragma once
#include "viewer_3ds.h"
#include "ntr/ppu.h"
namespace ds_gpu {class ObjPlan;}

// Available only in isolated builds with SM64DS_GPU_COMPOSITE_CANDIDATE.
// Preflight false leaves presentation untouched so callers can use legacy.
// Caller must reject active capture and EngineA display modes2/3 before entry:
// physical scanline feedback/FIFO and monoscopic capture need their driver.
bool v3ds_gpu_present_candidates(const V3dsVert*,int,const V3dsBatch*,int,
    ntr::PpuPreparedCandidates engine_a,ntr::PpuPreparedCandidates engine_b,
    bool engine_a_upper,float convergence,bool perspective,bool stereo,
    const ds_gpu::ObjPlan* engine_a_objects=nullptr,
    const ds_gpu::ObjPlan* engine_b_objects=nullptr);
// OBJ plans require BG-only candidates for the corresponding engine. A false
// result requires ordinary CPU candidates again, including the OBJ raster.
// Diagnostics explicitly request readback, separate from presentation. These
// are eye images, not a monoscopic DS capture source; never substitute left
// for centre while native stereo is active.
void v3ds_gpu_candidate_geometry(uint32_t* left,uint32_t* right);
void v3ds_gpu_candidate_panel(unsigned panel,uint32_t* output);
#ifdef SM64DS_RESIDENT_LIFETIME_DIAGNOSTICS
// Oracle only: leave an OBJ shader bound, submit its empty target, drain and
// release the resident backend. The next presentation must recreate safely.
void v3ds_gpu_candidate_reset();
#endif
