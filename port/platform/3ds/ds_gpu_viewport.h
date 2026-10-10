// Preserve the DS viewport grid before high-resolution PICA rasterization.
#ifndef DS_GPU_VIEWPORT_H
#define DS_GPU_VIEWPORT_H
#include "viewer_3ds.h"
#include <cmath>
struct DsGpuVertex { V3dsVert gpu; float screen_x, screen_y; };

// The caller supplies original viewport coordinates, including viewport offsets.
// At most five vertices result from clipping a triangle against two planes.
inline int ds_gpu_clip_viewport(DsGpuVertex (&polygon)[8]) {
    int n=3;
    unsigned outside_union=0,outside_intersection=3;
    for(int k=0;k<3;++k) {
        const auto& v=polygon[k].gpu;
        const unsigned outside=(v.z < -v.w ? 1u : 0u)|(v.z > v.w ? 2u : 0u);
        outside_union|=outside;outside_intersection&=outside;
    }
    if(outside_intersection)return 0;
    if(outside_union) {
        DsGpuVertex output[8];
        for(int plane=0;plane<2 && n;++plane) {
            auto distance=[plane](const DsGpuVertex& v){return plane?v.gpu.w-v.gpu.z:v.gpu.w+v.gpu.z;};
            int m=0;auto a=polygon[n-1];float da=distance(a);
            for(int k=0;k<n;++k) {
                const auto& b=polygon[k];const float db=distance(b);
                if((da>=0)!=(db>=0)) {
                    const float f=da/(da-db);DsGpuVertex v{};
#define DS_GPU_MIX(field) (a.gpu.field+(b.gpu.field-a.gpu.field)*f)
                    v.gpu={DS_GPU_MIX(x),DS_GPU_MIX(y),DS_GPU_MIX(z),DS_GPU_MIX(w),
                           DS_GPU_MIX(r),DS_GPU_MIX(g),DS_GPU_MIX(b),DS_GPU_MIX(a),DS_GPU_MIX(u),DS_GPU_MIX(v)};
#undef DS_GPU_MIX
                    if(v.gpu.w>1e-8f) {
                        v.screen_x=(a.screen_x*a.gpu.w+(b.screen_x*b.gpu.w-a.screen_x*a.gpu.w)*f)/v.gpu.w;
                        v.screen_y=(a.screen_y*a.gpu.w+(b.screen_y*b.gpu.w-a.screen_y*a.gpu.w)*f)/v.gpu.w;
                    }
                    output[m++]=v;
                }
                if(db>=0)output[m++]=b;
                a=b;da=db;
            }
            n=m;for(int k=0;k<n;++k)polygon[k]=output[k];
        }
    }
    for(int k=0;k<n;++k) {
        auto& v=polygon[k];
        // Match gx_render's integer DS viewport positions AFTER clipping.
        // Depth, w, colour and UV are untouched. Stereo is added by PICA later.
        v.gpu.x=(std::floor(v.screen_x)/128.f-1.f)*v.gpu.w;
        v.gpu.y=(1.f-std::floor(v.screen_y)/96.f)*v.gpu.w;
    }
    return n;
}
#endif
