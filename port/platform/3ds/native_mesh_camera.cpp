#include "native_mesh_api.h"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
// ABI wrappers; original LookAt/camera state transitions are called
// unchanged. The inputs are already the exact camera-scaled DS coordinates.
struct MeshVec3 {int32_t x,y,z;};
static unsigned camera_depth=0,camera_lookats=0,max_camera_lookats=0;
static uint64_t camera_calls=0,camera_publications=0,object_lookats=0;
static void* camera_owner=nullptr;
static float camera_distance=0.f;
static float camera_scale=0.f;
static uint64_t update_start=0,updates=0;
static unsigned max_target_per_update=0;
extern "C" void native_mesh_camera_begin_update(){
 const auto count=camera_publications-update_start;if(count>max_target_per_update)max_target_per_update=unsigned(count);
 update_start=camera_publications;++updates;
}
extern "C" int __real__ZN6Camera6RenderEv(void*);
extern "C" int __wrap__ZN6Camera6RenderEv(void* camera){
 if(camera!=camera_owner){camera_owner=camera;ntr::gx_native_mesh_camera_cut();}
 ++camera_depth;++camera_calls;camera_lookats=0;
 const int result=__real__ZN6Camera6RenderEv(camera);
 if(camera_lookats>max_camera_lookats)max_camera_lookats=camera_lookats;
 --camera_depth;return result;
}
extern "C" void __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(const MeshVec3*,const MeshVec3*,const MeshVec3*,bool,void*);
extern "C" void __wrap__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(const MeshVec3* camera,const MeshVec3* up,const MeshVec3* target,bool draw,void* matrix){
 if(draw&&camera&&target&&camera_depth){
  const float dx=float(int64_t(camera->x)-target->x)/4096.f;
  const float dy=float(int64_t(camera->y)-target->y)/4096.f;
  const float dz=float(int64_t(camera->z)-target->z)/4096.f;
  camera_distance=std::sqrt(dx*dx+dy*dy+dz*dz);
  camera_scale=ntr::gx_native_mesh_projection_w_scale();
  ntr::gx_native_mesh_camera_target(camera_distance);++camera_lookats;++camera_publications;
 }
 else if(draw)++object_lookats;
 __real__ZN3G3i7LookAt_EPK7Vector3S2_S2_bP9Matrix4x3(camera,up,target,draw,matrix);
}
extern "C" int __real__ZN6Camera11ChangeStateEPNS_5StateE(void*,void*);
extern "C" int __wrap__ZN6Camera11ChangeStateEPNS_5StateE(void* camera,void* state){
 // Original Camera.h gives mState@138; compare the actual installed state so
 // unchanged/vetoed calls do not reset temporal convergence each update.
 void* before=nullptr;std::memcpy(&before,static_cast<const char*>(camera)+0x138,sizeof(before));
 const int result=__real__ZN6Camera11ChangeStateEPNS_5StateE(camera,state);
 void* after=nullptr;std::memcpy(&after,static_cast<const char*>(camera)+0x138,sizeof(after));
 if(after!=before&&camera==camera_owner)ntr::gx_native_mesh_camera_cut();
 return result;
}
extern "C" void native_mesh_camera_stats(){
 const auto count=camera_publications-update_start;if(count>max_target_per_update)max_target_per_update=unsigned(count);
 std::printf("Native mesh camera originalRenderCalls=%llu targetPublications=%llu otherLookAtIgnored=%llu originalUpdates=%llu maxLookAtPerCameraRender=%u maxTargetPerUpdate=%u distance=%.9f projectionWScale=%.9f convergence=%.9f\n",(unsigned long long)camera_calls,(unsigned long long)camera_publications,(unsigned long long)object_lookats,(unsigned long long)updates,max_camera_lookats,max_target_per_update,camera_distance,camera_scale,ntr::gx_native_mesh_convergence());
}
