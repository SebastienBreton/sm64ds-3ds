#pragma once
#include "native_obj_plan.h"
#include "gpu_composite.h"
#include <memory>
#include <cstdlib>
#include <new>

namespace ds_gpu {
//Standalone candidate. Target alpha encodes 16*(priority+1), zero is transparent;
//RGB is the exact six-bit-expanded ordinary OBJ palette. All admissible OBJ
//compositor attributes are opaque OBJ, so the layer identifier is implicit.
class NativeObjects {
public:
    struct DrawStats {unsigned sprites=0,affine=0,draws=0,vertices=0;};
    bool init(const uint32_t* shader,unsigned bytes);
    void destroy();
    //Must be called inside a begun frame, after waiting for the previous GPU
    //queue. Texture inputs/VBO remain immutable until the frame completes.
    bool draw(Surface& output,const ObjPlan& plan);
    uint64_t decoded_textures()const{return decoded_;}
    DrawStats draw_stats()const{return draw_stats_;}
private:
    struct Vertex{float position[4],uv[2],unused[2];};
    struct Texture {
        // The game's global new targets its DS heap. Renderer resources belong
        // to the host heap, just like the immutable HostVector snapshots.
        static void* operator new(size_t bytes){
            if(auto* p=std::malloc(bytes))return p;
            throw std::bad_alloc();
        }
        static void operator delete(void* p){std::free(p);}
        C3D_Tex gpu{};
        ntr::HostVector<uint8_t> encoded,palette;
        unsigned width=0,height=0;
        uint64_t used=0;
        ~Texture(){if(gpu.data)C3D_TexDelete(&gpu);}
    };
    Texture* texture(const ObjSnapshot& sprite);
    ntr::HostVector<std::unique_ptr<Texture>> textures_;
    DVLB_s* dvlb_=nullptr;
    shaderProgram_s program_{};
    C3D_AttrInfo attributes_{};
    Vertex* vertices_=nullptr;
    uint64_t frame_=0,decoded_=0;
    size_t texture_bytes_=0;
    DrawStats draw_stats_{};
};
}
