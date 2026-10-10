#pragma once
#include "native_mesh_packet.h"
#include "prepared_mesh_view.h"
#include "native_mesh_convergence.h"
#include "ntr/ppu.h"
namespace ntr {
bool gx_native_mesh_capture_raw(const char* path); // Optional capture-only raw32 witness.
void gx_native_mesh_allow(bool gameplay_nonfeedback);
const native_mesh::Packet& gx_native_mesh_packet();
void gx_native_mesh_camera_target(float distance);
void gx_native_mesh_camera_cut();
bool gx_native_mesh_camera_valid();
float gx_native_mesh_convergence();
float gx_native_mesh_convergence_or(float fallback);
float gx_native_mesh_projection_w_scale();
}
namespace ds_gpu {struct ObjPlan;}
// Full pipeline entry: owned resources, drain before writes, same
// geometry for centre/eyes, resident composition, normal SYNCDRAW.
bool v3ds_native_mesh_present(const native_mesh::Packet&,const ntr::PpuPreparedCandidates& a,
 const ntr::PpuPreparedCandidates& b,bool a_upper,float camera_convergence,bool stereo,
 const ds_gpu::ObjPlan* objects_a=nullptr,const ds_gpu::ObjPlan* objects_b=nullptr);
bool v3ds_native_mesh_geometry(const native_mesh::Packet&,uint32_t* left,uint32_t* right,float camera_convergence,bool stereo);
bool v3ds_prepared_mesh_admits(const ntr::PpuPreparedCandidates& a,const ntr::PpuPreparedCandidates& b,bool a_upper);
bool v3ds_prepared_mesh_present(std::shared_ptr<const prepared_mesh::Frame>,const ntr::PpuPreparedCandidates& a,
 const ntr::PpuPreparedCandidates& b,bool a_upper,float camera_convergence,bool stereo,
 const ds_gpu::ObjPlan* objects_a=nullptr,const ds_gpu::ObjPlan* objects_b=nullptr);
bool v3ds_prepared_mesh_geometry(std::shared_ptr<const prepared_mesh::Frame>,uint32_t* left,uint32_t* right,float camera_convergence,bool stereo);
