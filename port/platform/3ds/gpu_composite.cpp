#include "ntr/ppu_plane_version.h"
#include "gpu_composite.h"
#include "frame_profile.h"
#include "native_blit.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

extern "C" {
// castle-min30: GPU affine surface handshake with the PPU (ntr/ppu.cpp).
volatile int sm64ds_gpu_affine_ready=0;
volatile int sm64ds_gpu_affine_wanted=0;
}
namespace ds_gpu {
bool Surface::init(){
    if(!C3D_TexInitVRAM(&texture,256,256,GPU_RGBA8))return false;
    C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    target=C3D_RenderTargetCreateFromTex(&texture,GPU_TEXFACE_2D,0,-1);
    return target!=nullptr;
}
void Surface::destroy(){
    if(target)C3D_RenderTargetDelete(target);
    target=nullptr;
    if(texture.data)C3D_TexDelete(&texture);
    texture={};
}
bool Composite::init(const uint32_t* shader,unsigned bytes){
    dvlb_=DVLB_ParseFile(const_cast<uint32_t*>(shader),bytes);
    if(!dvlb_)return false;
    scratch_pool_reserve(); // before optional VRAM users (see g_pool)
    shaderProgramInit(&program_);
    shaderProgramSetVsh(&program_,&dvlb_->DVLE[0]);
    AttrInfo_Init(&attributes_);
    AttrInfo_AddLoader(&attributes_,0,GPU_FLOAT,4);
    AttrInfo_AddLoader(&attributes_,1,GPU_FLOAT,2);
    AttrInfo_AddLoader(&attributes_,2,GPU_FLOAT,2);
    vertices_=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6*4));
    if(!vertices_)return false;
    for(unsigned variant=0;variant<4;++variant){
        const float lower0=(variant&1)?0.f:.75f,upper0=(variant&1)?.75f:0.f;
        const float lower1=(variant&2)?0.f:.75f,upper1=(variant&2)?.75f:0.f;
        const Vertex quad[]={
            {{-1,-1,-.5f,1},{0,lower0},{0,lower1}},
            {{ 1,-1,-.5f,1},{1,lower0},{1,lower1}},
            {{ 1, 1,-.5f,1},{1,upper0},{1,upper1}},
            {{-1,-1,-.5f,1},{0,lower0},{0,lower1}},
            {{ 1, 1,-.5f,1},{1,upper0},{1,upper1}},
            {{-1, 1,-.5f,1},{0,upper0},{0,upper1}},
        };
        std::memcpy(vertices_+variant*6,quad,sizeof(quad));
    }
    GSPGPU_FlushDataCache(vertices_,sizeof(Vertex)*6*4);
    if(!C3D_TexInit(&front_,256,256,GPU_RGBA8) || !C3D_TexInit(&mask_,256,256,GPU_RGBA8))return false;
    for(auto* texture:{&front_,&mask_}){
        C3D_TexSetFilter(texture,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    }
    mask_pixels_=static_cast<uint32_t*>(std::malloc(256*192*4));
    front_snapshot_=static_cast<uint32_t*>(std::malloc(256*192*4));
    mask_snapshot_=static_cast<uint8_t*>(std::malloc(256*192*3));
    if(!mask_pixels_ || !front_snapshot_ || !mask_snapshot_)return false;
    C3D_LightEnvInit(&environment_);
    C3D_Material material{};
    for(auto& component:material.specular0)component=1;
    C3D_LightEnvMaterial(&environment_,&material);
    C3D_LightEnvAmbient(&environment_,0,0,0);
    C3D_LightEnvBumpMode(&environment_,GPU_BUMP_AS_BUMP);
    C3D_LightEnvBumpSel(&environment_,0);
    C3D_LightEnvBumpNormalZ(&environment_,true);
    C3D_LightEnvClampHighlights(&environment_,false);
    for(unsigned axis=0;axis<3;++axis){
        auto& light=lights_[axis];
        C3D_LightInit(&light,&environment_);
        C3D_LightAmbient(&light,0,0,0);
        C3D_LightDiffuse(&light,0,0,0);
        C3D_LightSpecular0(&light,axis==0,axis==1,axis==2);
        C3D_LightSpecular1(&light,0,0,0);
        C3D_FVec position=FVec4_New(axis==0,axis==1,axis==2,0);
        C3D_LightPosition(&light,&position);
        C3D_LightGeoFactor(&light,0,false);
        C3D_LightGeoFactor(&light,1,false);
        C3D_LightSpotEnable(&light,false);
        C3D_LightDistAttnEnable(&light,false);
    }
    return true;
}
void Composite::destroy(){
    if(vertices_)linearFree(vertices_);
    vertices_=nullptr;
    if(cut_vertices_)linearFree(cut_vertices_);
    cut_vertices_=nullptr;
    if(front_.data)C3D_TexDelete(&front_);
    if(mask_.data)C3D_TexDelete(&mask_);
    if(back_.data)C3D_TexDelete(&back_);
    front_={};mask_={};back_={};
    std::free(mask_pixels_);mask_pixels_=nullptr;
    std::free(mask_lut_);mask_lut_=nullptr;mask_lut_key_=~0u;
    std::free(front_snapshot_);front_snapshot_=nullptr;
    std::free(mask_snapshot_);mask_snapshot_=nullptr;
    front_valid_=mask_valid_=false;
    if(dvlb_){shaderProgramFree(&program_);DVLB_Free(dvlb_);}
    dvlb_=nullptr;
}
void Composite::channel_table(const uint8_t color[256]){
    for(int index=-128;index<128;++index){
        // Constant entries disable interpolation; the packed value is /4095.
        table_.data[index&255]=(unsigned(color[index+128])*4095+127)/255;
    }
    C3D_LightEnvLut(&environment_,GPU_LUT_D0,GPU_LUTINPUT_LN,true,&table_);
    // The address is unchanged but the table bytes changed: force re-upload.
    environment_.flags|=C3DF_LightEnv_LutDirty(GPU_LUT_D0);
}
void Composite::setup(Surface& output,C3D_Tex& input,bool resident,bool secondary){
    C3D_FrameDrawOn(output.target);
    C3D_SetViewport(0,0,256,192);
    C3D_BindProgram(&program_);
    C3D_SetAttrInfo(&attributes_);
    // The 192-row viewport in a 256-row framebuffer occupies physical rows
    // 64..255; CPU uploads store their top row at 255, our quad at 64.
    // Resident sampling reverses v, without transfer or CPU rearrangement.
    auto* buffers=C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers,vertices_+6*(unsigned(resident)+2*unsigned(secondary)),sizeof(Vertex),3,0x210);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_TexBind(0,&input);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
}
void Composite::map(Surface& output,C3D_Tex& input,bool resident){
    setup(output,input,resident);
    C3D_LightEnvBind(&environment_);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_RGB,GPU_FRAGMENT_SECONDARY_COLOR,GPU_FRAGMENT_SECONDARY_COLOR,GPU_FRAGMENT_SECONDARY_COLOR);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_REPLACE);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
void Composite::copy(Surface& output,C3D_Tex& input,bool resident){
    setup(output,input,resident);C3D_LightEnvBind(nullptr);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
void Composite::add(Surface& output,C3D_Tex& first,C3D_Tex& second){
    setup(output,first,true);C3D_LightEnvBind(nullptr);C3D_TexBind(1,&second);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE1,GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_ADD);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_REPLACE);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
void Composite::blend(Surface& output,C3D_Tex& first,C3D_Tex& second,
                      unsigned eva,unsigned evb,Surface (&scratch)[3],
                      bool first_resident,bool second_resident){
    eva=eva>16?16:eva;evb=evb>16?16:evb;
    uint8_t table[256];
    for(unsigned c=0;c<256;++c)table[c]=((c>>2)*eva)>>4;
    channel_table(table);map(scratch[0],first,first_resident);
    for(unsigned c=0;c<256;++c)table[c]=((c>>2)*evb)>>4;
    channel_table(table);map(scratch[1],second,second_resident);
    add(scratch[2],scratch[0].texture,scratch[1].texture);
    for(unsigned c=0;c<256;++c)table[c]=((c>>2)*eva)&15;
    channel_table(table);map(scratch[0],first,first_resident);
    for(unsigned c=0;c<256;++c)table[c]=((c>>2)*evb)&15;
    channel_table(table);map(scratch[1],second,second_resident);
    add(output,scratch[0].texture,scratch[1].texture);
    // Rounding at +8 precedes division, and can produce carry2 at sum>=24.
    for(unsigned c=0;c<256;++c)table[c]=((c>30?30:c)+8)>>4;
    channel_table(table);map(scratch[0],output.texture,true);
    add(scratch[1],scratch[2].texture,scratch[0].texture);
    for(unsigned c=0;c<256;++c){const unsigned q=c>63?63:c;table[c]=(q<<2)|(q>>4);}
    channel_table(table);map(output,scratch[1].texture,true);
}
void Composite::masked(Surface& output,C3D_Tex& input,bool resident,unsigned channel,bool lookup,bool blend){
    setup(output,input,resident,false);C3D_TexBind(1,&mask_);
    C3D_LightEnvBind(lookup?&environment_:nullptr);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_RGB,lookup?GPU_FRAGMENT_SECONDARY_COLOR:GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_TEXTURE0,GPU_TEXTURE1);
    const GPU_TEVOP_A operands[]={GPU_TEVOP_A_SRC_R,GPU_TEVOP_A_SRC_G,GPU_TEVOP_A_SRC_B,GPU_TEVOP_A_SRC_ALPHA};
    C3D_TexEnvOpAlpha(tev,GPU_TEVOP_A_SRC_ALPHA,operands[channel]);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_MODULATE);
    C3D_AlphaTest(true,GPU_GREATER,11);
    // Alpha above controls coverage; final display is opaque RGB.
    C3D_DepthTest(false,GPU_ALWAYS,static_cast<GPU_WRITEMASK>(7));
    // minigames-2d-gpu: native-3D blend over a second target, weight = the
    // geometry alpha ((alpha5+1)/32 encoded); 255 keeps the geometry exact.
    if(blend)C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
// Replace only the output alpha by the input's alpha (geometry coverage).
void Composite::copy_alpha(Surface& output,C3D_Tex& input,bool resident){
    setup(output,input,resident);C3D_LightEnvBind(nullptr);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALPHA);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
// minigames-2d-gpu: RGB copy with opaque alpha (the front texture alpha may
// carry the priority encoding; panels stay alpha 255 as before).
void Composite::copy_rgb(Surface& output,C3D_Tex& input,bool resident){
    setup(output,input,resident);C3D_LightEnvBind(nullptr);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_REPLACE);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
// minigames-2d-gpu: geometry over the encoded front plane. The CPU mask's
// red channel was 255 exactly where geometry is in front of the front pixel
// (geometry_priority<p, or == p and the pixel is not OBJ), i.e. where
// k=2p+(layer!=OBJ) > 2g, i.e. front alpha 16*(k+3) >= 16*(2g+4). Here
// (alpha-threshold)*4*4*4, threshold=16*(2g+3)+8, saturates to exactly 255
// there and 0 elsewhere; the final product geometry.a*step uses the same
// operand order and values as geometry.a*mask.R, so the alpha test admits
// the same pixels.
void Composite::masked_front(Surface& output,C3D_Tex& geometry,bool resident,bool lookup,unsigned threshold){
    setup(output,geometry,resident,false);C3D_TexBind(1,&front_);
    C3D_LightEnvBind(lookup?&environment_:nullptr);
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_RGB,lookup?GPU_FRAGMENT_SECONDARY_COLOR:GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_SUBTRACT);
    C3D_TexEnvScale(tev,C3D_Alpha,GPU_TEVSCALE_4);
    C3D_TexEnvColor(tev,(threshold&255u)<<24);
    for(unsigned stage=1;stage<3;++stage){
        tev=C3D_GetTexEnv(stage);
        C3D_TexEnvSrc(tev,C3D_RGB,GPU_PREVIOUS);C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
        C3D_TexEnvSrc(tev,C3D_Alpha,GPU_PREVIOUS);C3D_TexEnvFunc(tev,C3D_Alpha,GPU_REPLACE);
        C3D_TexEnvScale(tev,C3D_Alpha,GPU_TEVSCALE_4);
    }
    tev=C3D_GetTexEnv(3);
    C3D_TexEnvSrc(tev,C3D_RGB,GPU_PREVIOUS);C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_TEXTURE0,GPU_PREVIOUS);
    C3D_TexEnvFunc(tev,C3D_Alpha,GPU_MODULATE);
    C3D_AlphaTest(true,GPU_GREATER,11);
    C3D_DepthTest(false,GPU_ALWAYS,static_cast<GPU_WRITEMASK>(7));
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
bool Composite::front_alpha_eligible(const ntr::PpuCompositionState& s){
    static const bool enabled=std::getenv("PLAYER_COMPOSITE_CPU_MASK")==nullptr &&
        std::getenv("PLAYER_COMPOSITE_REFERENCE_MASK")==nullptr;
    if(!enabled || s.has_windows || s.has_obj_alpha || s.obj_first_blend || s.bg_first_blend || composite_native_blend(s))return false;
    const unsigned mode=(s.blend>>6)&3;
    const bool layer_effect=mode==2 || mode==3;
    const bool uniform=layer_effect && (s.blend&63)==63;
    return !layer_effect || uniform;
}
// Shared scratch for semi-transparent OBJ blending (VRAM, else linear).
// minigames-2d-gpu: one pool of four VRAM render surfaces, reserved with
// the resident compositor (citro3d render targets must be VRAM; reserving
// late failed once optional VRAM textures and the CPU fallback's readback
// targets had taken it: semi OBJ blending was then lost for the session).
// Transient users: the semi-OBJ nine-pass blend (all four). Capture users:
// two bank surfaces, the delayed geometry and an unmastered capture source;
// while capture content is live (used within the last 600 presentations)
// the semi blend is refused rather than overwriting it.
static Surface g_pool[4];static int g_pool_state=0;
static int g_bank_slot[4]={-1,-1,-1,-1};
static unsigned g_capture_idle=~0u;
bool scratch_pool_reserve(){
    if(!g_pool_state){
        g_pool_state=1;
        for(auto& s:g_pool)if(!s.init()){g_pool_state=-1;break;}
        std::printf("GPU scratch pool surfaces=%d remainingVRAM=%u\n",g_pool_state,vramSpaceFree());
    }
    return g_pool_state>0;
}
void capture_pool_frame(bool capture_used){
    if(capture_used)g_capture_idle=0;
    else if(g_capture_idle!=~0u && ++g_capture_idle>600){
        g_capture_idle=~0u;for(auto& slot:g_bank_slot)slot=-1;
    }
}
static bool capture_live(){return g_capture_idle!=~0u;}
Surface* capture_bank_surface(unsigned bank,bool assign){
    if(bank>=4 || g_pool_state<=0)return nullptr;
    if(g_bank_slot[bank]<0){
        if(!assign)return nullptr;
        bool used[2]{};for(int slot:g_bank_slot)if(slot>=0)used[slot]=true;
        const int slot=!used[0]?0:!used[1]?1:-1;
        if(slot<0){std::printf("GPU capture: no surface left for bank %u\n",bank);return nullptr;}
        g_bank_slot[bank]=slot;
        C3D_RenderTargetClear(g_pool[slot].target,C3D_CLEAR_COLOR,0x000000ffu,0);
    }
    g_capture_idle=0;
    return &g_pool[g_bank_slot[bank]];
}
Surface* delayed_geometry_surface(){if(g_pool_state<=0)return nullptr;g_capture_idle=0;return &g_pool[2];}
Surface* capture_scratch_surface(){if(g_pool_state<=0)return nullptr;g_capture_idle=0;return &g_pool[3];}
static bool semi_surfaces(Surface*& out){
    out=g_pool;
    if(g_pool_state<=0){
        static bool reported=false;
        if(!reported){reported=true;std::puts("Semi-transparent OBJ blend skipped: scratch pool unavailable");}
        return false;
    }
    if(capture_live()){
        static bool reported=false;
        if(!reported){reported=true;std::puts("Semi-transparent OBJ blend skipped: scratch pool holds a display capture");}
        return false;
    }
    return true;
}
static unsigned brighten(unsigned q,unsigned factor,bool up,bool master){
    factor=std::min(factor,16u);
    const unsigned bias=master?(up?0:15):(up?8:7);
    return up?q+((63-q)*factor+bias)/16:q-(q*factor+bias)/16;
}
CaptureFramePlan g_capture_plan;
// VRAM render target when available, otherwise linear memory.
bool capture_bank_readback_rgb555(unsigned bank,uint16_t* out){
    Surface* surface=capture_bank_surface(bank,false);
    if(!surface)return false;
    auto* scratch=static_cast<uint32_t*>(linearAlloc(256*256*4));
    auto* argb=static_cast<uint32_t*>(std::malloc(256*192*4));
    if(!scratch || !argb){if(scratch)linearFree(scratch);std::free(argb);return false;}
    // No dirty CPU line may later overwrite the transferred pixels.
    GSPGPU_InvalidateDataCache(scratch,256*256*4);
    // C3D_SyncDisplayTransfer waits for the queued GPU work first.
    Composite::readback(*surface,argb,scratch);
    // The surface holds re-expanded five-bit channels: r5 = c8>>3 exactly.
    for(unsigned i=0;i<256*192;++i){
        const uint32_t c=argb[i];
        out[i]=uint16_t(0x8000u|((c>>19)&31)|(((c>>11)&31)<<5)|(((c>>3)&31)<<10));
    }
    linearFree(scratch);std::free(argb);
    return true;
}
void Composite::capture_into(Surface& bank,C3D_Tex& source,bool resident){
    // DS capture stores (c>>3) per channel; BG/OBJ bitmap display expands a
    // five-bit channel like bgr555(): (r<<3)|(r>>3). 254 and 255 agree.
    uint8_t table[256];
    for(unsigned c=0;c<256;++c)table[c]=uint8_t(((c>>3)<<3)|(c>>6));
    channel_table(table);map(bank,source,resident);
}
void Composite::view_capture(Surface& output,C3D_Tex& bank,uint16_t master){
    const unsigned mode=master>>14;
    if(mode==1 || mode==2){
        uint8_t table[256];
        for(unsigned c=0;c<256;++c){const unsigned q=brighten(c>>2,master&31,mode==1,true);table[c]=uint8_t((q<<2)|(q>>4));}
        channel_table(table);map(output,bank,true);
    }else copy_rgb(output,bank,true);
}
bool Composite::opaque(Surface& output,ntr::PpuPreparedCandidates candidates,
                       C3D_Tex& geometry,bool resident,bool master){
    const auto& scene=candidates.state;
    const unsigned mode=(scene.blend>>6)&3;
    // Native 3D forces its 5-bit-alpha blend whenever an eligible second
    // target is behind it, even with effects disabled by BLDCNT/windows.
    // This opaque path has not implemented that independent DS rule yet.
    if(!composite_blend_free(scene))return false;
    front_alpha_frame_=false;
    if(candidates.gpu_affine)return affine_opaque(output,*candidates.gpu_affine,scene,master);
    affine_drawn_=false;
    // Only geometry selection and the OBJ merge (BG-only views) read it.
    const bool encode=front_alpha_eligible(scene) && (scene.has_geometry || candidates.background_identity);
    const bool cache=std::getenv("PLAYER_RESIDENT_UNCACHED_INPUTS")==nullptr;
    const char* flag=std::getenv("PLAYER_PPU_PLANE_CERTIFICATE");
    const bool enabled=!flag || *flag!='0';
    const auto version=enabled?ntr::ppu_candidate_plane_version(candidates):ntr::CandidatePlaneVersion{};
    const void* identity=candidates.background_identity?static_cast<const void*>(candidates.background_identity):version.identity;
    const uint64_t generation=candidates.background_identity?candidates.background_generation:version.generation;
    const bool immutable=identity && generation;
    const bool same_front=immutable && front_identity_==identity && front_generation_==generation;
    static const bool trust_versions=std::getenv("PLAYER_COMPOSITE_SNAPSHOT_ALWAYS")==nullptr;
    uint64_t op_tick=frame_profile::ticks();
    // The PPU stores the code in the front alpha byte of its own planes: the
    // ordinary upload then carries it (no CPU encoding here). Other views are
    // encoded while uploading. Band hashes of the two methods differ.
    static const bool ppu_alpha=std::getenv("PLAYER_COMPOSITE_NO_PPU_ALPHA")==nullptr;
    const bool ppu_encoded=encode && ppu_alpha && candidates.front_alpha_encoded;
    const unsigned method=encode?(ppu_encoded?2u:1u):0u;
    if(method!=front_method_){front_bands_valid_=false;front_valid_=false;front_method_=method;}
    if(ppu_encoded){
        static const bool verify=std::getenv("PLAYER_VERIFY_FRONT_ALPHA")!=nullptr;
        if(verify){
            static unsigned long long frames=0,bad_frames=0,bad_pixels=0;
            unsigned bad=0;
            for(unsigned i=0;i<256*192;++i)
                bad+=((&candidates.front.px[0][0])[i]>>24)!=ntr::ppu_front_alpha_code(candidates.front_layer()[i],candidates.front_priority()[i]);
            ++frames;bad_frames+=bad!=0;bad_pixels+=bad;
            if(bad || frames%600==0)std::printf("FRONT_ALPHA_VERIFY frames=%llu badFrames=%llu badPixels=%llu last=%u\n",frames,bad_frames,bad_pixels,bad);
        }
    }
    if(encode && !ppu_encoded && !(trust_versions && cache && immutable)){
        // Unversioned planes: the byte snapshot covers RGB only, so the
        // encoded upload is unconditional (its band hashes cover alpha too).
        upload_encoded(front_,&candidates.front.px[0][0],candidates.front_layer(),candidates.front_priority(),
                       front_bands_,front_bands_valid_);
        front_valid_=true;front_versioned_=false;
    }else if(trust_versions && cache && immutable){
        // A new exact-snapshot generation is new content: upload without the
        // byte comparison; the byte snapshot only serves unversioned views.
        if(!same_front || !front_valid_ || !front_versioned_){
            if(encode && !ppu_encoded)upload_encoded(front_,&candidates.front.px[0][0],candidates.front_layer(),candidates.front_priority(),
                                     front_bands_,front_bands_valid_);
            else upload_changed(front_,&candidates.front.px[0][0],front_bands_,front_bands_valid_);
            front_valid_=true;
        }
        front_versioned_=true;
    }else if(!cache || !front_valid_ || front_versioned_ || (!same_front && std::memcmp(front_snapshot_,&candidates.front.px[0][0],256*192*4))){
        upload(front_,&candidates.front.px[0][0]);front_bands_valid_=false;
        std::memcpy(front_snapshot_,&candidates.front.px[0][0],256*192*4);
        front_valid_=true;front_versioned_=false;
    }
    front_encoded_=encode;
    front_identity_=identity;front_generation_=generation;
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[32]+=now-op_tick;op_tick=now;}
    if(scene.blank){if(encode)copy_rgb(output,front_);else copy(output,front_);front_alpha_frame_=encode;return true;}
    affine_drawn_=false;
    const unsigned master_mode=master?scene.master>>14:0;
    const bool master_effect=master_mode==1 || master_mode==2;
    const bool layer_effect=mode==2 || mode==3;
    const bool uniform=layer_effect && !scene.has_windows && (scene.blend&63)==63;
    uint8_t base_table[256],effect_table[256];
    for(unsigned c=0;c<256;++c){
        unsigned raw=c>>2,q=raw;
        if(uniform)q=brighten(q,scene.brightness,mode==2,false);
        if(master_effect)q=brighten(q,scene.master&31,master_mode==1,true);
        base_table[c]=(q<<2)|(q>>4);
        q=brighten(raw,scene.brightness,mode==2,false);
        if(master_effect)q=brighten(q,scene.master&31,master_mode==1,true);
        effect_table[c]=(q<<2)|(q>>4);
    }
    const bool base_lookup=master_effect || uniform;
    // No selection mask is sampled by a geometry-independent opaque image or
    // uniform fade. Omit the CPU loop and texture upload in those cases.
    // minigames-30fps-min: a 2D-only mode1 pair blend still needs obj_first.
    if(scene.bg_first_blend && !scene.has_geometry)return obj_first(output,candidates,geometry,resident,base_lookup,base_table,2);
    if(!scene.has_geometry && (!layer_effect || uniform)){
        if(base_lookup){channel_table(base_table);map(output,front_);}
        else if(encode)copy_rgb(output,front_);
        else copy(output,front_);
        front_alpha_frame_=encode;
        return true;
    }
    if(scene.obj_first_blend)return obj_first(output,candidates,geometry,resident,base_lookup,base_table);
    if(scene.semi_back)return obj_first(output,candidates,geometry,resident,base_lookup,base_table,1);
    if(scene.bg_first_blend)return obj_first(output,candidates,geometry,resident,base_lookup,base_table,2);
    if(encode){
        // minigames-2d-gpu: no CPU mask plane (see masked_front).
        if(base_lookup){channel_table(base_table);map(output,front_);}
        else copy_rgb(output,front_);
        masked_front(output,geometry,resident,base_lookup,16u*(2u*scene.geometry_priority+3u)+8u);
        front_alpha_frame_=true;
        sm64ds_frame_prof[35]+=frame_profile::ticks()-op_tick;
        return true;
    }
    // Only exact-snapshot PPU caches supply trusted plane generations.
    // Legacy views can mutate in place, so their byte snapshots remain authority.
    constexpr unsigned pixels=256*192;
    const bool semi=scene.semi_geometry && scene.has_obj_alpha && scene.has_geometry;
    const bool native=composite_native_blend(scene) && !semi;
    const uint32_t key=(scene.blend&255)|(uint32_t(scene.geometry_priority)<<8)|
        (uint32_t(scene.has_geometry)<<16)|(uint32_t(scene.has_windows)<<17)|(uint32_t(semi)<<18)|
        ((semi||native)?uint32_t((scene.blend>>8)&63)<<19:0)|(uint32_t(native)<<25);
    // The semi mask also reads back planes: trust only an immutable identity.
    const bool match=cache && mask_valid_ && mask_key_==key &&
        ((immutable && mask_identity_==identity && mask_generation_==generation) || (!semi &&
        !std::memcmp(mask_snapshot_,candidates.front_layer(),pixels) &&
        !std::memcmp(mask_snapshot_+pixels,candidates.front_priority(),pixels) &&
        (!scene.has_windows || !std::memcmp(mask_snapshot_+pixels*2,candidates.window_mask(),pixels))));
    static const bool mask_lut=std::getenv("PLAYER_COMPOSITE_REFERENCE_MASK")==nullptr;
    if(!match && !semi && mask_lut){
        // The mask is a function of (layer, priority, window) only: tabulate
        // it once (8x8x64) and index per pixel, same values as the loop below.
        // castle-30fps: the table depends only on these inputs; rebuild it
        // when they change, and recompute only rows whose layer/priority/
        // window bytes changed (mask_pixels_ keeps the previous rows).
        // castle-min30: one table per compositor. With a single static table
        // shared by both engines, two engines with different keys (pause
        // menu: darkened 3D on A, darkened map on B) rebuilt it on every call
        // and never reused rows: full CPU masks + uploads on both screens each
        // frame. The table is a pure function of table_key, so rows computed
        // earlier under the same key (mask_table_key_) stay exact.
        // PLAYER_COMPOSITE_SHARED_LUT=1 restores the shared table for A/B.
        static uint32_t shared_lut[8*8*64];static uint32_t shared_key=~0u;
        static const bool shared=std::getenv("PLAYER_COMPOSITE_SHARED_LUT")!=nullptr;
        if(!shared && !mask_lut_)mask_lut_=static_cast<uint32_t*>(std::malloc(sizeof(uint32_t)*8*8*64));
        uint32_t* lut=(!shared && mask_lut_)?mask_lut_:shared_lut;uint32_t& lut_key=(!shared && mask_lut_)?mask_lut_key_:shared_key;
        const uint32_t table_key=(uint32_t(scene.geometry_priority)&7)|(uint32_t(scene.has_geometry)<<3)|(uint32_t(layer_effect)<<4)|(uint32_t(scene.blend&63)<<5)|
            (native?(uint32_t((scene.blend>>8)&63)<<11)|(1u<<17):0u);
        const bool table_same=lut_key==table_key;
        if(!table_same){
        for(unsigned layer=0;layer<8;++layer)for(unsigned priority=0;priority<8;++priority)for(unsigned window=0;window<64;++window){
            const bool before=scene.geometry_priority<priority || (scene.geometry_priority==priority && layer!=4);
            const bool geom=scene.has_geometry && (window&1) && before;
            const bool front_effect=layer_effect && (window&32) && (scene.blend&(1u<<layer));
            const bool geom_effect=geom && layer_effect && (window&32) && (scene.blend&1);
            // Native blend: R = opaque geometry, B = geometry over a second target.
            const bool second=native && ((scene.blend>>(8+layer))&1);
            lut[(layer<<9)|(priority<<6)|window]=0xff000000u|(geom&&!second?0xff0000:0)|(front_effect?0xff00:0)|((geom_effect||(geom&&second))?255:0);
        }
        lut_key=table_key;
        }
        static const bool row_updates=std::getenv("PLAYER_COMPOSITE_FULL_MASK")==nullptr;
        // Row reuse needs the previous rows to come from this same table and
        // window mode (the static table is shared by both compositors).
        // minigames-30fps: after consecutive updates in which every row
        // changed (scrolling), skip the per-row comparisons for a while and
        // recompute all rows (same mask values), then probe again.
        static const bool adaptive_rows=std::getenv("PLAYER_COMPOSITE_NO_ADAPTIVE_BANDS")==nullptr;
        bool skip_compare=false;
        if(adaptive_rows && mask_skip_){--mask_skip_;skip_compare=true;}
        const bool rows_ok=!skip_compare && row_updates && (table_same || !shared) && mask_valid_ && mask_key_==key && mask_table_key_==table_key;
        const uint8_t* layers=candidates.front_layer();const uint8_t* priorities=candidates.front_priority();
        const uint8_t* windows=scene.has_windows?candidates.window_mask():nullptr;
        static uint8_t dirty_rows[192];
        static const bool full_uploads=std::getenv("PLAYER_COMPOSITE_FULL_UPLOADS")!=nullptr;
        for(unsigned y=0;y<192;++y){
            const unsigned at=y*256;dirty_rows[y]=1;
            if(rows_ok && !std::memcmp(mask_snapshot_+at,layers+at,256) && !std::memcmp(mask_snapshot_+pixels+at,priorities+at,256) &&
               (!windows || !std::memcmp(mask_snapshot_+pixels*2+at,windows+at,256))){dirty_rows[y]=0;continue;}
            if(windows)for(unsigned i=at;i<at+256;++i)mask_pixels_[i]=lut[((layers[i]&7)<<9)|((priorities[i]&7)<<6)|(windows[i]&63)];
            else for(unsigned i=at;i<at+256;++i)mask_pixels_[i]=lut[((layers[i]&7)<<9)|((priorities[i]&7)<<6)|63];
        }
        mask_table_key_=table_key;
        if(adaptive_rows && rows_ok){
            unsigned dirty=0;for(unsigned y=0;y<192;++y)dirty+=dirty_rows[y];
            mask_full_streak_=dirty>=176?mask_full_streak_+1:0;
            if(mask_full_streak_>=2){mask_skip_=16;mask_full_streak_=0;}
        }
        // Rows not recomputed hold exactly what the texture already holds.
        if(rows_ok && !full_uploads)upload_rows(mask_,mask_pixels_,dirty_rows);else upload(mask_,mask_pixels_);
        obj_mask_sig_=0;
        std::memcpy(mask_snapshot_,candidates.front_layer(),pixels);
        std::memcpy(mask_snapshot_+pixels,candidates.front_priority(),pixels);
        if(scene.has_windows)std::memcpy(mask_snapshot_+pixels*2,candidates.window_mask(),pixels);
        mask_key_=key;mask_valid_=true;
    }else if(!match){
        mask_table_key_=~0u;
        for(unsigned i=0;i<pixels;++i){
            const unsigned layer=candidates.front_layer()[i]&127;
            const unsigned window=scene.has_windows?candidates.window_mask()[i]:63;
            const bool before=scene.geometry_priority<candidates.front_priority()[i] ||
                (scene.geometry_priority==candidates.front_priority()[i] && layer!=4);
            const bool geom=scene.has_geometry && (window&1) && before;
            const bool front_effect=layer_effect && (window&32) && (scene.blend&(1u<<layer));
            const bool geom_effect=geom && layer_effect && (window&32) && (scene.blend&1);
            // Alpha: semi-transparent OBJ pixel that blends with geometry
            // wherever geometry covers it (it is behind the OBJ, in front of
            // the back plane, a second target, and visible in the window).
            bool semi_blend=false;
            if(semi && (candidates.front_layer()[i]&0x80)){
                const unsigned back_layer=candidates.back_layer()[i]&127,back_priority=candidates.back_priority()[i];
                const bool behind_ok=scene.geometry_priority<back_priority ||
                    (scene.geometry_priority==back_priority && back_layer!=4);
                semi_blend=(window&1) && behind_ok && (scene.blend&0x100);
            }
            mask_pixels_[i]=(semi_blend?0xff000000u:(semi?0u:0xff000000u))|(geom?0xff0000:0)|(front_effect?0xff00:0)|(geom_effect?255:0);
        }
        upload(mask_,mask_pixels_);obj_mask_sig_=0;
        std::memcpy(mask_snapshot_,candidates.front_layer(),pixels);
        std::memcpy(mask_snapshot_+pixels,candidates.front_priority(),pixels);
        if(scene.has_windows)std::memcpy(mask_snapshot_+pixels*2,candidates.window_mask(),pixels);
        mask_key_=key;mask_valid_=true;
    }
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[33]+=now-op_tick;op_tick=now;}
    if(base_lookup){channel_table(base_table);map(output,front_);}
    else copy(output,front_);
    if(layer_effect && !uniform){channel_table(effect_table);masked(output,front_,false,1,true);}
    mask_identity_=identity;mask_generation_=generation;
    if(scene.has_geometry){
        if(base_lookup)channel_table(base_table);
        masked(output,geometry,resident,0,base_lookup);
        if(native)masked(output,geometry,resident,2,base_lookup,true);
        if(layer_effect && !uniform){channel_table(effect_table);masked(output,geometry,resident,2,true);}
    }
    Surface* scratch=nullptr;
    if(semi && semi_surfaces(scratch)){
        // DS: blended = min(63,(obj*EVA+geometry*EVB+8)/16) per six-bit
        // channel (exact nine-pass blend), then master brightness; only where
        // geometry covers the pixel (its alpha) and the mask alpha admits it.
        Surface (&passes)[3]=*reinterpret_cast<Surface(*)[3]>(scratch+1);
        blend(scratch[0],front_,geometry,scene.alpha&31,(scene.alpha>>8)&31,passes,false,resident);
        copy_alpha(scratch[0],geometry,resident);
        if(base_lookup)channel_table(base_table);
        masked(output,scratch[0].texture,true,3,base_lookup);
        const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[34]+=now-op_tick;op_tick=now;
    }
    sm64ds_frame_prof[35]+=frame_profile::ticks()-op_tick;
    return true;
}
// minigames-2d-gpu: BLDCNT mode1, first target OBJ only, BG0 a second target,
// with geometry (exact compose_candidates rules, nine-pass DS blends):
//  front pixel not OBJ : geometry in front -> opaque (R) or native blend (B)
//  OBJ, geometry behind it and in front of the back pixel -> blend(OBJ, 3D)
//      where the 3D covers (A, weighted by geometry coverage),
//  otherwise blend(OBJ, back) when the back pixel is a second target (G).
// minigames-30fps-min: semi_only (state.semi_back): only semi-transparent OBJ
// pixels (front layer bit7) blend; where the 3D lies between such a pixel and
// its back pixel, it blends with the 3D only if BG0 is a second target, else
// the OBJ colour stays (copy of the front plane, weighted by 3D coverage).
bool Composite::obj_first(Surface& output,ntr::PpuPreparedCandidates candidates,C3D_Tex& geometry,bool resident,
                          bool base_lookup,const uint8_t* base_table,int variant){
    // variant 1 (semi_back) and 2 (bg_first_blend) share one exact rule set:
    // the blending pixels are semi OBJ (1, 2) and first-target layers under
    // the window effect bit (2); geometry selection is drawn last.
    const bool semi_only=variant!=0;
    const uint8_t* windows=candidates.state.has_windows?candidates.window_mask():nullptr;
    const auto& scene=candidates.state;
    Surface* scratch=nullptr;
    if(!semi_surfaces(scratch))return false;
    if(!back_.data){
        if(!C3D_TexInit(&back_,256,256,GPU_RGBA8))return false;
        C3D_TexSetFilter(&back_,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&back_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    }
    const uint8_t* fl=candidates.front_layer();const uint8_t* fp=candidates.front_priority();
    const uint8_t* bl=candidates.back_layer();const uint8_t* bp=candidates.back_priority();
    const unsigned g=scene.geometry_priority;
    auto second=[&](unsigned layer){return ((scene.blend>>(8+(layer&7)))&1)!=0;};
    bool any_back=false,any_geometry=false;
    // minigames-30fps-min: the mask is a function of the immutable planes
    // (front_identity_/front_generation_ of this opaque() call) and the
    // state: re-presenting the same scene (stereo eye, held 30 Hz frames,
    // static intro letter) reuses the uploaded mask texture.
    const bool immutable_planes=front_versioned_ && front_identity_ && front_generation_;
    uint64_t sig=0;
    if(immutable_planes && variant!=0){
        sig=0x9e3779b97f4a7c15ull;
        for(uint64_t v:{uint64_t(reinterpret_cast<uintptr_t>(front_identity_)),front_generation_,uint64_t(variant),
                        uint64_t(scene.blend),uint64_t(scene.geometry_priority)|(uint64_t(scene.has_geometry)<<8)|(uint64_t(scene.has_windows)<<9)}){
            sig^=v;sig*=0x100000001b3ull;sig^=sig>>29;
        }
        sig|=1;
    }
    const bool mask_cached=sig && sig==obj_mask_sig_;
    if(mask_cached){any_back=obj_mask_any_back_;any_geometry=obj_mask_any_geometry_;}
    if(!mask_cached && variant==2 && !scene.has_geometry && !windows){
        // minigames-30fps-min: 2D-only pair blend (no 3D, no window): the mask
        // is G = (front blends) && second(back), a function of the two layer
        // bytes only (same values as the general loop below).
        uint32_t lut[256];
        for(unsigned f=0;f<256;++f){
            const unsigned layer=f&7;const bool blending=(f&0x80) || ((scene.blend>>layer)&1);
            lut[f]=blending?1u:0u;
        }
        uint32_t back_lut[8];for(unsigned l=0;l<8;++l)back_lut[l]=second(l)?0xff00u:0u;
        uint32_t any=0;
        for(unsigned i=0;i<256*192;++i){const uint32_t m=lut[fl[i]]?back_lut[bl[i]&7]:0u;mask_pixels_[i]=m;any|=m;}
        any_back=any!=0;
    }else
    for(unsigned i=0;!mask_cached && i<256*192;++i){
        const unsigned layer=fl[i]&7;
        const bool geom=scene.has_geometry && (g<fp[i] || (g==fp[i] && layer!=4));
        uint32_t m=0;
        if(semi_only){
            // A semi OBJ pixel blends with its back pixel wherever the 3D in
            // front of it (priority) does not cover it: G is independent of
            // the geometry selection R, which is drawn last (passes below).
            const unsigned window=windows?windows[i]:63u;
            const bool visible=scene.has_geometry && (window&1);
            const bool front_geom=visible && (g<fp[i] || (g==fp[i] && layer!=4));
            if(front_geom)m|=second(layer)?0xffu:0xff0000u;
            const bool blending=(fl[i]&0x80) || (variant==2 && (window&32) && (layer!=0 || !scene.has_geometry) && ((scene.blend>>layer)&1));
            if(blending){
                if(!front_geom && visible && (g<bp[i] || (g==bp[i] && (bl[i]&7)!=4))){m|=0xff000000u;any_geometry=true;}
                if(second(bl[i])){m|=0xff00u;any_back=true;}
            }
            mask_pixels_[i]=m;continue;
        }
        if(geom)m|=second(layer)?0xffu:0xff0000u;
        else if(layer==4 || (fl[i]&0x80)){
            const bool behind=scene.has_geometry && (g<bp[i] || (g==bp[i] && (bl[i]&7)!=4));
            if(behind){m|=0xff000000u;any_geometry=true;}
            if(second(bl[i])){m|=0xff00u;any_back=true;}
        }
        mask_pixels_[i]=m;
    }
    if(!mask_cached){
        upload(mask_,mask_pixels_);mask_valid_=false;mask_table_key_=~0u;
        obj_mask_sig_=sig;obj_mask_any_back_=any_back;obj_mask_any_geometry_=any_geometry;
    }
    if(base_lookup){channel_table(base_table);map(output,front_);}else copy_rgb(output,front_);
    auto geometry_passes=[&]{
        if(!scene.has_geometry)return; // minigames-30fps-min: 2D-only pair blend (no 3D selection)
        if(base_lookup)channel_table(base_table);
        masked(output,geometry,resident,0,base_lookup);
        masked(output,geometry,resident,2,base_lookup,true);
    };
    if(!semi_only)geometry_passes();
    Surface (&passes)[3]=*reinterpret_cast<Surface(*)[3]>(scratch+1);
    const unsigned eva=scene.alpha&31,evb=(scene.alpha>>8)&31;
    if(any_back){
        if(!sig || sig!=obj_back_sig_)upload(back_,&candidates.back.px[0][0]);
        obj_back_sig_=sig;
        blend(scratch[0],front_,back_,eva,evb,passes,false,false);
        if(base_lookup)channel_table(base_table);
        masked(output,scratch[0].texture,true,1,base_lookup);
    }
    if(any_geometry){
        if(semi_only && !second(0))copy(scratch[0],front_,false);
        else blend(scratch[0],front_,geometry,eva,evb,passes,false,resident);
        copy_alpha(scratch[0],geometry,resident);
        if(base_lookup)channel_table(base_table);
        masked(output,scratch[0].texture,true,3,base_lookup);
    }
    if(semi_only)geometry_passes(); // 3D in front of a semi OBJ wins where it covers
    return true;
}
void Composite::upload(C3D_Tex& input,const uint32_t* argb){
    upload_native_image(static_cast<uint32_t*>(input.data),argb);
    GSPGPU_FlushDataCache(input.data,256*256*4);
}
void Composite::upload_rows(C3D_Tex& input,const uint32_t* argb,const uint8_t* dirty_rows){
    auto* tiled=static_cast<uint32_t*>(input.data)+8*32*64;
    unsigned lo=24,hi=0; // one cache flush for the changed band range (each is a GSP IPC)
    for(unsigned band=0;band<24;++band){
        const unsigned top=184-band*8;bool dirty=false;
        for(unsigned r=0;r<8;++r)dirty|=dirty_rows[top+r]!=0;
        if(!dirty)continue;
        uint32_t* destination=tiled+band*32*64;
        const uint32_t* source=argb+(191-band*8)*256;
        for(unsigned column=0;column<32;++column){
            for(unsigned pixel=0;pixel<64;++pixel){
                const uint32_t color=source[native_blit_offsets[pixel]];
                destination[pixel]=(color<<8)|(color>>24);
            }
            source+=8;destination+=64;
        }
        if(band<lo)lo=band;hi=band+1;
    }
    if(lo<hi)GSPGPU_FlushDataCache(tiled+lo*32*64,(hi-lo)*32*64*4);
}
void Composite::upload_changed(C3D_Tex& input,const uint32_t* argb,uint64_t* band_hashes,bool& valid){
    static const bool enabled=std::getenv("PLAYER_COMPOSITE_FULL_UPLOADS")==nullptr;
    // castle-30fps: a 64-bit hash per 8-row band instead of a 192 KiB image
    // snapshot (the 3DS heap is nearly full in large levels).
    auto hash_band=[&](unsigned top){
        const uint32_t* p=argb+top*256;uint64_t h=0x9e3779b97f4a7c15ull;
        for(unsigned i=0;i<8*256;i+=2){h^=(uint64_t(p[i])<<32)|p[i+1];h*=0x100000001b3ull;h^=h>>29;}
        return h;
    };
    if(!enabled || !band_hashes){upload(input,argb);valid=false;return;}
    // minigames-30fps: when (nearly) every band changes on consecutive frames
    // (scrolling planes), hashing costs as much as the upload itself. Upload
    // in full for a while without hashing, then probe again. Same texels.
    struct Adaptive {const uint64_t* key;unsigned streak,skip;};
    static Adaptive adaptive[8]{};static unsigned next_slot=0;
    static const bool adaptive_enabled=std::getenv("PLAYER_COMPOSITE_NO_ADAPTIVE_BANDS")==nullptr;
    Adaptive* state=nullptr;
    if(adaptive_enabled){
        for(auto& a:adaptive)if(a.key==band_hashes){state=&a;break;}
        if(!state){state=&adaptive[next_slot++%8];*state={band_hashes,0,0};}
        if(state->skip){--state->skip;upload(input,argb);valid=false;return;}
    }
    if(!valid){upload(input,argb);for(unsigned band=0;band<24;++band)band_hashes[band]=hash_band(184-band*8);valid=true;return;}
    auto* tiled=static_cast<uint32_t*>(input.data)+8*32*64;
    unsigned lo=24,hi=0,changed=0; // one cache flush for the changed band range (each is a GSP IPC)
    for(unsigned band=0;band<24;++band){
        const unsigned top=184-band*8;const uint64_t h=hash_band(top);
        if(h==band_hashes[band])continue;
        band_hashes[band]=h;
        uint32_t* destination=tiled+band*32*64;
        const uint32_t* source=argb+(191-band*8)*256;
        for(unsigned column=0;column<32;++column){
            for(unsigned pixel=0;pixel<64;++pixel){
                const uint32_t color=source[native_blit_offsets[pixel]];
                destination[pixel]=(color<<8)|(color>>24);
            }
            source+=8;destination+=64;
        }
        if(band<lo)lo=band;hi=band+1;++changed;
    }
    if(lo<hi)GSPGPU_FlushDataCache(tiled+lo*32*64,(hi-lo)*32*64*4);
    if(state){
        state->streak=changed>=20?state->streak+1:0;
        if(state->streak>=2){state->skip=16;state->streak=0;}
    }
}
void Composite::upload_encoded(C3D_Tex& input,const uint32_t* argb,const uint8_t* layers,
                               const uint8_t* priorities,uint64_t* band_hashes,bool& valid){
    static uint8_t code[64];static bool code_ready=false;
    if(!code_ready){
        for(unsigned layer=0;layer<8;++layer)for(unsigned priority=0;priority<8;++priority){
            const unsigned k=2*priority+(layer==4?0u:1u),a=16*(k+3);
            code[(layer<<3)|priority]=uint8_t(a>255?255:a);
        }
        code_ready=true;
    }
    static const bool hashing=std::getenv("PLAYER_COMPOSITE_FULL_UPLOADS")==nullptr;
    // Band hash over the exact inputs of the encoded texels: colours (two per
    // step, as upload_changed) and the layer/priority bytes (four per word).
    auto hash_band=[&](unsigned top){
        const uint32_t* p=argb+top*256;uint64_t h=0x9e3779b97f4a7c15ull;
        for(unsigned i=0;i<8*256;i+=2){h^=(uint64_t(p[i])<<32)|p[i+1];h*=0x100000001b3ull;h^=h>>29;}
        const uint32_t* l=reinterpret_cast<const uint32_t*>(layers+top*256);
        const uint32_t* q=reinterpret_cast<const uint32_t*>(priorities+top*256);
        for(unsigned i=0;i<8*256/4;++i){h^=(uint64_t(l[i])<<32)|q[i];h*=0x100000001b3ull;h^=h>>29;}
        return h;
    };
    auto* tiled=static_cast<uint32_t*>(input.data)+8*32*64;
    // Encode the band's 8 rows linearly into an L1-resident scratch, then
    // scatter tiles from it exactly like upload_native_image.
    static uint32_t scratch[8*256];
    auto write_band=[&](unsigned band){
        const unsigned top=184-band*8;
        const uint32_t* c=argb+top*256;const uint8_t* l=layers+top*256;const uint8_t* q=priorities+top*256;
        for(unsigned i=0;i<8*256;++i)scratch[i]=(c[i]<<8)|code[((l[i]&7u)<<3)|(q[i]&7u)];
        uint32_t* destination=tiled+band*32*64;
        const uint32_t* source=scratch+7*256;
        for(unsigned column=0;column<32;++column){
            for(unsigned pixel=0;pixel<64;++pixel)destination[pixel]=source[native_blit_offsets[pixel]];
            source+=8;destination+=64;
        }
    };
    // Same adaptive policy as upload_changed: scrolling planes change every
    // band, so stop hashing for a while after two full-change updates.
    struct Adaptive {const uint64_t* key;unsigned streak,skip;};
    static Adaptive adaptive[8]{};static unsigned next_slot=0;
    static const bool adaptive_enabled=std::getenv("PLAYER_COMPOSITE_NO_ADAPTIVE_BANDS")==nullptr;
    Adaptive* state=nullptr;
    if(hashing && band_hashes && adaptive_enabled){
        for(auto& a:adaptive)if(a.key==band_hashes){state=&a;break;}
        if(!state){state=&adaptive[next_slot++%8];*state={band_hashes,0,0};}
    }
    if(!hashing || !band_hashes || !valid || (state && state->skip)){
        if(state && state->skip)--state->skip;
        for(unsigned band=0;band<24;++band)write_band(band);
        GSPGPU_FlushDataCache(tiled,24*32*64*4);
        valid=false;
        if(hashing && band_hashes && !(state && state->skip)){
            for(unsigned band=0;band<24;++band)band_hashes[band]=hash_band(184-band*8);
            valid=true;
        }
        return;
    }
    unsigned lo=24,hi=0,changed=0;
    for(unsigned band=0;band<24;++band){
        const uint64_t h=hash_band(184-band*8);
        if(h==band_hashes[band])continue;
        band_hashes[band]=h;write_band(band);
        if(band<lo)lo=band;hi=band+1;++changed;
    }
    if(lo<hi)GSPGPU_FlushDataCache(tiled+lo*32*64,(hi-lo)*32*64*4);
    if(state){
        state->streak=changed>=20?state->streak+1:0;
        if(state->streak>=2){state->skip=16;state->streak=0;}
    }
}
void Composite::readback(Surface& input,uint32_t* argb,uint32_t* scratch){
    const uint32_t flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer(static_cast<uint32_t*>(input.target->frameBuf.colorBuf),GX_BUFFER_DIM(256,256),scratch,GX_BUFFER_DIM(256,256),flags);
    GSPGPU_InvalidateDataCache(scratch,256*256*4);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
        const uint32_t rgba=scratch[(64+y)*256+x];
        argb[y*256+x]=(rgba>>8)|(rgba<<24);
    }
}
bool Composite::fill_cut(Surface& output,const ntr::PpuWindowCut& cut){
    if(!cut.count)return true;
    if(!cut_vertices_)cut_vertices_=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6*ntr::PpuWindowCut::kMaxRects));
    if(!cut_vertices_)return false;
    // Same pixel mapping as affine_opaque: DS (x,y) -> clip (x/128-1, 1-y/96).
    Vertex* v=cut_vertices_;
    for(unsigned r=0;r<cut.count;++r){
        const auto& rect=cut.rects[r];
        const float x0=rect.x0/128.f-1.f,x1=rect.x1/128.f-1.f,y0=1.f-rect.y0/96.f,y1=1.f-rect.y1/96.f;
        const Vertex q[4]={{{x0,y0,-.5f,1},{0,0},{0,0}},{{x1,y0,-.5f,1},{0,0},{0,0}},
                           {{x1,y1,-.5f,1},{0,0},{0,0}},{{x0,y1,-.5f,1},{0,0},{0,0}}};
        for(unsigned k:{0u,1u,2u,0u,2u,3u})*v++=q[k];
    }
    GSPGPU_FlushDataCache(cut_vertices_,sizeof(Vertex)*6*cut.count);
    C3D_FrameDrawOn(output.target);C3D_SetViewport(0,0,256,192);
    C3D_BindProgram(&program_);C3D_SetAttrInfo(&attributes_);C3D_LightEnvBind(nullptr);
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);BufInfo_Add(buffers,cut_vertices_,sizeof(Vertex),3,0x210);
    C3D_CullFace(GPU_CULL_NONE);C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* tev=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(tev,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);
    const uint32_t c=cut.color; // 0xAARRGGBB -> constant 0xAABBGGRR
    C3D_TexEnvColor(tev,0xff000000u|((c&0xffu)<<16)|(c&0xff00u)|((c>>16)&0xffu));
    C3D_DrawArrays(GPU_TRIANGLES,0,int(6*cut.count));
    return true;
}
// castle-min30: the affine surface is a 256 KiB VRAM render target. It used
// to be created lazily on first use and a failure latched for the session:
// the boot menus run while the screen-layout resources hold ~1 MiB of VRAM,
// so the first attempt failed and every later GPU-affine frame returned
// false, leaving the bottom panel stale. Now the PPU only hands out affine
// candidates once sm64ds_gpu_affine_ready is set; it asks for the surface
// (sm64ds_gpu_affine_wanted) and the presentation worker creates it when
// enough VRAM is free (ensure_affine, keeping 256 KiB spare for others).
bool Composite::ensure_affine(bool reserve_check){
    if(affine_ready_)return true;
    if(reserve_check && vramSpaceFree()<2u*256u*256u*4u)return false;
    if(!affine_surface_.init()){affine_surface_.destroy();return false;}
    if(!affine_vertices_)affine_vertices_=static_cast<Vertex*>(linearAlloc(sizeof(Vertex)*6));
    if(!affine_vertices_){affine_surface_.destroy();return false;}
    affine_ready_=true;
    return true;
}
bool Composite::affine_opaque(Surface& output,const ntr::PpuGpuAffineLayer& layer,const ntr::PpuCompositionState& scene,bool master){
    affine_drawn_=false;
    if(!ensure_affine())return false;
    if(layer.width<8 || layer.height<8 || layer.width>1024 || layer.height>1024)return false;
    if(affine_width_!=layer.width || affine_height_!=layer.height){
        if(affine_texture_.data)C3D_TexDelete(&affine_texture_);
        affine_texture_={};affine_width_=affine_height_=0;affine_source_=nullptr;affine_generation_=~0ull;
        if(!C3D_TexInit(&affine_texture_,layer.width,layer.height,GPU_RGBA8))return false;
        C3D_TexSetFilter(&affine_texture_,GPU_NEAREST,GPU_NEAREST);
        affine_width_=layer.width;affine_height_=layer.height;
    }
    if(affine_generation_!=layer.raster_generation){
        frame_profile::Scope upload_scope(36);
        // Same texel conversion as upload_native_image; PICA row t=0 is the
        // bottom, so raster row y is stored at row height-1-y. When this
        // texture holds the descriptor's base generation, only the changed
        // 8-row bands (one PICA tile row each) are converted and flushed.
        auto* dst=static_cast<uint32_t*>(affine_texture_.data);const int w=layer.width,h=layer.height;
        static const bool incremental=std::getenv("PLAYER_AFFINE_FULL_UPLOADS")==nullptr;
        const bool partial=incremental && layer.base_generation && affine_generation_==layer.base_generation;
        int lo=h/8,hi=0;
        for(int band=0;band*8<h;++band){
            if(partial && !((layer.dirty_bands[band>>6]>>(band&63))&1))continue;
            for(int y=band*8;y<band*8+8;++y){
                const uint32_t* src=layer.raster+size_t(y)*w;const int py=h-1-y;
                uint32_t* tile_row=dst+size_t(py>>3)*(w>>3)*64;
                const unsigned yy=py&7,ybits=((yy&1)<<1)|((yy&2)<<2)|((yy&4)<<3);
                for(int x=0;x<w;++x){
                    const unsigned xx=x&7,morton=(xx&1)|((xx&2)<<1)|((xx&4)<<2)|ybits;
                    const uint32_t c=src[x];tile_row[(x>>3)*64+morton]=(c<<8)|(c>>24);
                }
            }
            const int tile_row=h/8-1-band;if(tile_row<lo)lo=tile_row;if(tile_row+1>hi)hi=tile_row+1;
        }
        if(lo<hi)GSPGPU_FlushDataCache(dst+size_t(lo)*(w>>3)*64,size_t(hi-lo)*(w>>3)*64*4);
        affine_source_=layer.raster;affine_generation_=layer.raster_generation;
    }
    // Outside a non-wrapping map the DS shows the backdrop: transparent border.
    C3D_TexSetWrap(&affine_texture_,layer.wrap?GPU_REPEAT:GPU_CLAMP_TO_BORDER,layer.wrap?GPU_REPEAT:GPU_CLAMP_TO_BORDER);
    affine_texture_.border=0;
    // DS: texel=(ref+P*(X,Y))>>8 at integer pixel X,Y; the GPU samples pixel
    // centres, so evaluate the map at the corners shifted by -0.5 (+1/512
    // keeps exact multiples of 1/256 on the DS side of the floor).
    auto corner=[&](Vertex& v,float sx,float sy){
        v.position[0]=sx/128.f-1.f;v.position[1]=1.f-sy/96.f;v.position[2]=-.5f;v.position[3]=1.f;
        const double dx=sx-.5,dy=sy-.5;
        v.uv0[0]=float(((layer.ref_x+layer.pa*dx+layer.pb*dy)/256.0+1.0/512.0)/layer.width);
        v.uv0[1]=float(((layer.ref_y+layer.pc*dx+layer.pd*dy)/256.0+1.0/512.0)/layer.height);
        v.uv1[0]=v.uv1[1]=0;
    };
    Vertex q[4];corner(q[0],0,0);corner(q[1],256,0);corner(q[2],256,192);corner(q[3],0,192);
    const unsigned order[]={0,1,2,0,2,3};for(unsigned k=0;k<6;++k)affine_vertices_[k]=q[order[k]];
    GSPGPU_FlushDataCache(affine_vertices_,sizeof(Vertex)*6);
    // castle-min30: BLDCNT brightness on the affine layer and/or backdrop
    // (same six-bit rule and table as opaque()'s layer effect).
    const bool effect=layer.effect_mode==2 || layer.effect_mode==3;
    uint8_t effect_table[256];
    if(effect)for(unsigned c=0;c<256;++c){const unsigned q=brighten(c>>2,layer.effect_factor,layer.effect_mode==2,false);effect_table[c]=uint8_t((q<<2)|(q>>4));}
    uint32_t backdrop=layer.backdrop;
    if(effect && layer.effect_backdrop)
        backdrop=(backdrop&0xff000000u)|(uint32_t(effect_table[(backdrop>>16)&255])<<16)|(uint32_t(effect_table[(backdrop>>8)&255])<<8)|effect_table[backdrop&255];
    C3D_RenderTargetClear(affine_surface_.target,C3D_CLEAR_COLOR,(backdrop<<8)|192u,0);
    C3D_FrameDrawOn(affine_surface_.target);C3D_SetViewport(0,0,256,192);
    const bool layer_effect=effect && layer.effect_layer;
    if(layer_effect)channel_table(effect_table);
    C3D_BindProgram(&program_);C3D_SetAttrInfo(&attributes_);C3D_LightEnvBind(layer_effect?&environment_:nullptr);
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);BufInfo_Add(buffers,affine_vertices_,sizeof(Vertex),3,0x210);
    C3D_CullFace(GPU_CULL_NONE);C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALL);C3D_AlphaTest(true,GPU_GREATER,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_TexBind(0,&affine_texture_);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* tev=C3D_GetTexEnv(0);
    if(layer_effect){C3D_TexEnvSrc(tev,C3D_RGB,GPU_FRAGMENT_SECONDARY_COLOR,GPU_FRAGMENT_SECONDARY_COLOR,GPU_FRAGMENT_SECONDARY_COLOR);}
    else C3D_TexEnvSrc(tev,C3D_RGB,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);
    C3D_TexEnvFunc(tev,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(tev,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT,GPU_TEXTURE0);C3D_TexEnvFunc(tev,C3D_Alpha,GPU_MODULATE);
    C3D_TexEnvColor(tev,(32u*(unsigned(layer.priority)+2u))<<24);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
    if(layer.overlay){
        // castle-min30: front text layers (CPU plane, alpha = priority code or 0).
        if(!affine_overlay_.data){
            if(!C3D_TexInit(&affine_overlay_,256,256,GPU_RGBA8))return false;
            C3D_TexSetFilter(&affine_overlay_,GPU_NEAREST,GPU_NEAREST);
            C3D_TexSetWrap(&affine_overlay_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
            affine_overlay_bands_valid_=false;affine_overlay_generation_=0;
        }
        if(affine_overlay_generation_!=layer.overlay_generation){
            frame_profile::Scope upload_scope(36);
            upload_changed(affine_overlay_,layer.overlay,affine_overlay_bands_,affine_overlay_bands_valid_);
            affine_overlay_generation_=layer.overlay_generation;
        }
        setup(affine_surface_,affine_overlay_,false);C3D_LightEnvBind(nullptr);
        C3D_AlphaTest(true,GPU_GREATER,0);
        tev=C3D_GetTexEnv(0);
        C3D_TexEnvSrc(tev,C3D_Both,GPU_TEXTURE0,GPU_TEXTURE0,GPU_TEXTURE0);C3D_TexEnvFunc(tev,C3D_Both,GPU_REPLACE);
        C3D_DrawArrays(GPU_TRIANGLES,0,6);
    }
    // Panel colour: identical to the CPU path's opaque(): optional master
    // brightness lookup on the composed BG image.
    const unsigned master_mode=master?scene.master>>14:0;
    if(master_mode==1 || master_mode==2){
        uint8_t table[256];
        for(unsigned c=0;c<256;++c){const unsigned q6=brighten(c>>2,scene.master&31,master_mode==1,true);table[c]=(q6<<2)|(q6>>4);}
        channel_table(table);map(output,affine_surface_.texture,true);
    }else copy(output,affine_surface_.texture,true);
    affine_drawn_=true;
    return true;
}
}
