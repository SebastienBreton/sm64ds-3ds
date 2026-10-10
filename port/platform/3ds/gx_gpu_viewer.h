#pragma once
// GPU geometry mode, presentation side (gpu-geometry-menu). Expands a recorded
// ntr::gx_gpu::Frame into the acquired lease (triangle list, 32-byte vertices,
// per-run matrix palettes) and fills batches and the Prepared descriptor that
// v3ds_arena::geometry/present_candidates consume. gpu_frame then draws those
// batches with gx_gpu.v.pica instead of the CPU-projected vertex shader.
#include "platform/3ds/geometry_arena_viewer.h"
#include "platform/3ds/gx_geometry_arena.h"
#include "ntr/gx_gpu_geometry.h"
namespace gx_gpu_viewer {
bool build(const ntr::gx_gpu::Frame& frame,const V3dsDirectLease& lease,
           ntr::HostVector<V3dsBatch>& batches,ntr::gx_geometry::Prepared& prepared,
           float& convergence,bool stereo_target=true);
// menu-stereo: stereo_target=false (3D engine on the lower LCD, e.g. the
// title/menu face) skips the per-frame CPU convergence median: that geometry
// is never shown stereoscopically.
void print_stats();
}
