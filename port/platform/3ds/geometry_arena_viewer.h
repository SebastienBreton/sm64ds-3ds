#pragma once
// Native viewer lease API. acquire drains the GPU BEFORE Builder writes.
#include "viewer_3ds.h"
namespace ntr::gx_geometry {struct Prepared;}
#include "ntr/ppu.h"
namespace ds_gpu {struct ObjPlan;}
struct V3dsDirectLease {
    V3dsVert* vertices=nullptr;
    size_t vertex_capacity=0;
    uint64_t frame_token=0;
};
namespace v3ds_arena {
bool acquire(V3dsDirectLease& lease);
// Pure CPU query; no acquire, submit, wait, readback or mutation.
bool valid(const ntr::gx_geometry::Prepared&);
// Ends an open command list and invalidates this token. Does not retire submitted
// GPU readers or free memory: mutate/destroy resources only after acquire or exit drain.
bool abort(uint64_t token);
// Descriptor must refer to the acquired VBO with the matching token. Builder
// has already corrected the native pixel centre. Texture/batch snapshots must
// stay immutable until next acquire retires all centre/eye commands.
bool geometry(const ntr::gx_geometry::Prepared&,uint32_t* left,uint32_t* right,
              float convergence,bool perspective,bool stereo);
bool present_candidates(const ntr::gx_geometry::Prepared&,
    ntr::PpuPreparedCandidates a,ntr::PpuPreparedCandidates b,
    bool a_upper,float convergence,bool perspective,bool stereo,
    const ds_gpu::ObjPlan* a_objects=nullptr,const ds_gpu::ObjPlan* b_objects=nullptr);
}
