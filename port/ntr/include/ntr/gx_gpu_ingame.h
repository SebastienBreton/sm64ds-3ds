#pragma once
// gpu-geometry-ingame: DS geometry work of gameplay stages on the 3DS GPU.
// The native mesh path already transforms compiled-BMD vertices on the PICA
// (matrix palette, unified.v.pica). These options move the remaining
// per-vertex CPU work:
//   Bulk     (1): a compiled occurrence is evaluated per command, not per
//                 vertex (bmd_instance_evaluator.inc); exact, verified by
//                 GI_VERIFY=1 against the per-command replay.
//   GpuLight (2): DS lighting of compiled lit vertices in the vertex shader:
//                 the attribute colour carries the transformed normal
//                 (requires Bulk); CPU consumers (CPU-clipped triangles, CPU
//                 fallback) decode it with the exact integer formula.
//   WideRange(4): clip matrices up to +-0x8000000 (32768.0) are admitted by the
//                 native mesh path (default +-0x1000000): with |vertex| < 2^15
//                 the DS fixed12 product cannot wrap below 2^27, so the float
//                 transform stays faithful; far sky objects no longer force
//                 the whole frame to the CPU path.
//   GpuClip  (8): no CPU near/far classification/clipping on the presentation
//                 worker: every run is drawn natively, the PICA clips, and
//                 only vertices inside the DS volume snap to the DS grid.
// SM64DS_GX_GPU_INGAME=<bitmask>, default 0 (all off).
#include "ntr/gx_gpu_geometry.h"
#include <cstdint>
#include <cstdlib>
namespace gx_gpu_ingame {
enum : unsigned { Bulk=1u, GpuLight=2u, WideRange=4u, GpuClip=8u };
inline unsigned options(){
    static const unsigned value=[]{const char* s=std::getenv("SM64DS_GX_GPU_INGAME");return s?unsigned(std::strtoul(s,nullptr,0)):0u;}();
    return value;
}
inline bool enabled(unsigned bit){return (options()&bit)!=0;}
inline bool gpu_light(){return enabled(Bulk) && enabled(GpuLight);}
inline std::int64_t matrix_limit(){return enabled(WideRange)?0x8000000:0x1000000;}
inline bool matrix_in_range(const std::int32_t* fixed){
    const std::int64_t limit=matrix_limit();
    for(unsigned i=0;i<16;++i)if(std::int64_t(fixed[i])>limit || std::int64_t(fixed[i])<-limit)return false;
    return true;
}
// Lit-vertex encoding in native_mesh::Attribute::color: the transformed normal
// (each component in [-512, 511]) packed 10:10:10 with +512 bias. The top byte
// is then < 64, while every CPU vertex colour (bgr555_to_argb) has A = 255.
inline bool pack_normal(const int n[3],std::uint32_t& out){
    for(int k=0;k<3;++k)if(n[k]<-512 || n[k]>511)return false;
    out=std::uint32_t(n[0]+512)|(std::uint32_t(n[1]+512)<<10)|(std::uint32_t(n[2]+512)<<20);return true;
}
inline bool packed_normal(std::uint32_t color){return (color>>24)<0xffu;}
inline void unpack_normal(std::uint32_t c,int n[3]){n[0]=int(c&1023)-512;n[1]=int((c>>10)&1023)-512;n[2]=int((c>>20)&1023)-512;}
// Exact CPU colour (gx.cpp NORMAL, ARGB like bgr555_to_argb) of a packed vertex.
inline std::uint32_t lit_argb(const ntr::gx_gpu::Env& env,std::uint32_t packed){
    int n[3];unpack_normal(packed,n);const std::uint16_t c=ntr::gx_gpu::light_color(env,n);
    const std::uint32_t r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    return 0xFF000000u|((r<<3|r>>2)<<16)|((g<<3|g>>2)<<8)|(b<<3|b>>2);
}
}
