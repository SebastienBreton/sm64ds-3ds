#pragma once
#include "ntr/host_allocator.h"
#include <array>
#include <cstdint>

namespace ds_gpu {
struct ObjInput {
    uint32_t display=0;
    const uint8_t* oam=nullptr;       //1024 bytes for one engine
    const uint8_t* vram=nullptr;      //immutable coherent AObj/BObj read view
    const uint8_t* palette=nullptr;  //512 bytes, ordinary OBJ palette
    unsigned vram_mask=0;
};
enum class ObjFallback {None,MissingViews,DisplayMode,Windows,Shape,Affine,Mosaic,Alpha,Bitmap,ExtendedPalette};
struct ObjSnapshot {
    unsigned index=0,priority=0,width=0,height=0,palette_bytes=0;
    int x=0,y=0;
    bool hflip=false,vflip=false,bpp8=false,affine=false;
    int pa=256,pd=256,pb=0,pc=0; // pb/pc!=0: rotated/sheared affine OBJ
    unsigned bound_width=0,bound_height=0,clip_x0=0,clip_x1=0,clip_y0=0,clip_y1=0;
    unsigned clip_y_second0=0,clip_y_second1=0;
    //Tile-major bytes and relevant palette bytes are exact immutable inputs.
    ntr::HostVector<uint8_t> encoded;
    std::array<uint8_t,512> palette{};
};
// Pure REG/OAM preflight: does not read texture/palette bytes or allocate.
ObjFallback obj_admission(uint32_t display,const uint8_t* oam);
class ObjPlan {
public:
    bool prepare(const ObjInput& input);
    ObjFallback fallback=ObjFallback::None;
    std::array<ObjSnapshot,128> sprites;
    std::array<uint8_t,128> order{}; //back to front: priority3..0, OAM127..0
    unsigned count=0;
};
//Called only for a changed immutable texture input, never to rasterize a frame.
void decode_obj_texture(const ObjSnapshot& sprite,ntr::HostVector<uint32_t>& pixels);
}
