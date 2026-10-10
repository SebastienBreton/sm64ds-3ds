#include "native_obj_plan.h"
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <array>
int main(){
    void* protected_page=mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(protected_page==MAP_FAILED)return 1;
    std::array<uint8_t,1024> oam{};
    auto put=[&](unsigned index,unsigned a0,unsigned a1){oam[index*8]=a0;oam[index*8+1]=a0>>8;oam[index*8+2]=a1;oam[index*8+3]=a1>>8;};
    for(unsigned i=0;i<128;++i)put(i,0x0200,0);
    for(unsigned i=0;i<120;++i)put(i,40,40);
    // Affine diagonal is an admitted capability. A nonzero cross term must
    // still reject before reading any protected texture or palette bytes.
    oam[14]=1;
    ds_gpu::ObjInput input{0x00011000,oam.data(),static_cast<uint8_t*>(protected_page),static_cast<uint8_t*>(protected_page),0x1ffff};
    ds_gpu::ObjPlan plan;
    const unsigned attributes[]={0x100,0x1000,0x400,0x800,0xc00,0xc000};
    const ds_gpu::ObjFallback reasons[]={ds_gpu::ObjFallback::Affine,ds_gpu::ObjFallback::Mosaic,ds_gpu::ObjFallback::Alpha,ds_gpu::ObjFallback::Windows,ds_gpu::ObjFallback::Bitmap,ds_gpu::ObjFallback::Shape};
    unsigned checks=0;
    for(unsigned k=0;k<6;++k){
        put(120,40|attributes[k],40);
        if(ds_gpu::obj_admission(input.display,input.oam)!=reasons[k] || plan.prepare(input) || plan.fallback!=reasons[k])return 2;
        for(const auto& sprite:plan.sprites)if(!sprite.encoded.empty())return 3;
        ++checks;
    }
    // REG rejection happens before OAM is read as well.
    input.oam=static_cast<uint8_t*>(protected_page);input.display=0x00013000;
    if(ds_gpu::obj_admission(input.display,input.oam)!=ds_gpu::ObjFallback::Windows || plan.prepare(input))return 4;++checks;
    input.display=0x00021000;
    if(ds_gpu::obj_admission(input.display,input.oam)!=ds_gpu::ObjFallback::DisplayMode || plan.prepare(input))return 5;++checks;
    for(unsigned disp:{0u,0x00010000u,0x00011080u}){
        input.display=disp;
        if(ds_gpu::obj_admission(disp,input.oam)!=ds_gpu::ObjFallback::None || !plan.prepare(input) || plan.count)return 6;++checks;
    }
    munmap(protected_page,4096);
    std::printf("PASS %u early-admission cases; PROT_NONE texture/palette on late OAM rejection, PROT_NONE OAM on REG rejection/blank/OBJ disabled, no encoded allocations\n",checks);
}
