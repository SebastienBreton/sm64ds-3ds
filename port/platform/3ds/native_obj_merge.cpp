#include "native_obj_merge.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace ds_gpu {
bool NativeObjMerge::init(){
    if(!C3D_TexInit(&priority_,256,256,GPU_RGBA8))return false;
    C3D_TexSetFilter(&priority_,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&priority_,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    snapshot_=static_cast<uint8_t*>(std::malloc(256*192));
    pixels_=static_cast<uint32_t*>(std::malloc(256*192*4));
    return snapshot_ && pixels_;
}
void NativeObjMerge::destroy(){
    if(priority_.data)C3D_TexDelete(&priority_);priority_={};
    std::free(snapshot_);std::free(pixels_);snapshot_=nullptr;pixels_=nullptr;
    valid_=false;
}
bool native_obj_merge_supported(ntr::PpuPreparedCandidates candidates){return NativeObjMerge::supported(candidates);} // minigames-30fps-min
bool NativeObjMerge::supported(ntr::PpuPreparedCandidates candidates){
    const auto& scene=candidates.state;
    if(scene.has_geometry || scene.has_windows || !composite_blend_free(scene))return false;
    if(scene.blank)return true;
    // Only independently owned BG cache planes have a trusted generation.
    // Recheck current effect guards above, but reuse the metadata admission
    // after its complete validation for this exact immutable generation.
    struct Validated {const ntr::PpuBackgroundPlanes* identity;uint64_t generation;};
    static Validated validated[2]{};static unsigned replacement=0;
    const bool immutable=candidates.background_identity && candidates.background_generation;
    if(immutable)for(const auto& entry:validated)
        if(entry.identity==candidates.background_identity &&
           entry.generation==candidates.background_generation)return true;
    // Planes carrying a background identity come only from the PPU's BG-only
    // cache (scanout_bg/row shifts of BG layers 0-3 and backdrop): they omit
    // OBJ by construction. PLAYER_OBJ_MERGE_VALIDATE keeps the explicit scan.
    static const bool validate=std::getenv("PLAYER_OBJ_MERGE_VALIDATE")!=nullptr;
    if(immutable && !validate)return true;
    // The view must genuinely omit OBJ, rather than merely hiding its colour.
    for(unsigned i=0;i<256*192;++i){
        const unsigned layer=candidates.front_layer()[i],priority=candidates.front_priority()[i];
        if(layer==4 || layer>5 || priority>4 ||
           (layer==5?priority!=4:priority>3))return false;
    }
    if(immutable)validated[replacement++%2]={candidates.background_identity,candidates.background_generation};
    return true;
}
void NativeObjMerge::update(ntr::PpuPreparedCandidates candidates){
    const auto& scene=candidates.state;
    gpu_priority_=candidates.gpu_affine!=nullptr;
    front_priority_=false;pending_.reset();
    if(scene.blank || gpu_priority_)return;
    if(Composite::front_alpha_eligible(scene) && candidates.background_identity){front_priority_=true;pending_.emplace(candidates);return;}
    update_cpu(candidates);
}
void NativeObjMerge::update_cpu(ntr::PpuPreparedCandidates candidates){
    if(valid_ && candidates.background_identity && candidates.background_generation &&
       identity_==candidates.background_identity && generation_==candidates.background_generation)return;
    identity_=candidates.background_identity;generation_=candidates.background_generation;
    if(valid_ && !std::memcmp(snapshot_,candidates.front_priority(),256*192))return;
    for(unsigned i=0;i<256*192;++i)
        pixels_[i]=(32u*(unsigned(candidates.front_priority()[i])+2))<<24;
    Composite::upload_changed(priority_,pixels_,bands_,bands_valid_);
    std::memcpy(snapshot_,candidates.front_priority(),256*192);valid_=true;
}
void NativeObjMerge::compose(Composite& compositor,Surface& output,
                             C3D_Tex& objects,const ntr::PpuCompositionState& scene){
    if(scene.blank)return;
    const unsigned mode=(scene.blend>>6)&3,master_mode=scene.master>>14;
    const bool effect=(mode==2 || mode==3) && (scene.blend&16);
    const bool master=master_mode==1 || master_mode==2;
    const bool lookup=effect || master;
    if(lookup){
        uint8_t table[256];
        for(unsigned c=0;c<256;++c){
            unsigned q=c>>2;
            if(effect){
                const unsigned factor=std::min(unsigned(scene.brightness),16u);
                q=mode==2?q+((63-q)*factor+8)/16:q-(q*factor+7)/16;
            }
            if(master){
                const unsigned factor=std::min(unsigned(scene.master&31),16u);
                q=master_mode==1?q+((63-q)*factor)/16:q-(q*factor+15)/16;
            }
            table[c]=(q<<2)|(q>>4);
        }
        compositor.channel_table(table);
    }
    // Resident OBJ coordinates and CPU-uploaded priority coordinates use
    // independent orientations, already covered by the composite oracle.
    C3D_Tex* gpu_priority=gpu_priority_?compositor.affine_priority():nullptr;
    if(gpu_priority_ && !gpu_priority)return; // affine layer was not drawn this frame
    C3D_Tex* front=front_priority_?compositor.front_alpha_texture():nullptr;
    if(front_priority_ && !front && pending_){update_cpu(*pending_);front_priority_=false;}
    compositor.setup(output,objects,true,gpu_priority!=nullptr);
    C3D_TexBind(1,gpu_priority?gpu_priority:front?front:&priority_);
    C3D_LightEnvBind(lookup?&compositor.environment_:nullptr);
    C3D_AlphaTest(true,GPU_GREATER,0);
    // Keep the already opaque BG output alpha while replacing its RGB.
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ZERO,GPU_ONE);
    auto* stage=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(stage,C3D_RGB,lookup?GPU_FRAGMENT_SECONDARY_COLOR:GPU_TEXTURE0);
    C3D_TexEnvFunc(stage,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(stage,C3D_Alpha,GPU_TEXTURE0);
    C3D_TexEnvFunc(stage,C3D_Alpha,GPU_REPLACE);
    C3D_TexEnvScale(stage,C3D_Alpha,GPU_TEVSCALE_2);
    stage=C3D_GetTexEnv(1);
    C3D_TexEnvSrc(stage,C3D_RGB,GPU_PREVIOUS);C3D_TexEnvFunc(stage,C3D_RGB,GPU_REPLACE);
    C3D_TexEnvSrc(stage,C3D_Alpha,GPU_TEXTURE1,GPU_PREVIOUS);
    C3D_TexEnvFunc(stage,C3D_Alpha,GPU_SUBTRACT);
    stage=C3D_GetTexEnv(2);
    C3D_TexEnvSrc(stage,C3D_RGB,GPU_PREVIOUS);C3D_TexEnvFunc(stage,C3D_RGB,GPU_REPLACE);
    // BG32*(p+2) minus doubled OBJ16*(p+1) admits equal-priority OBJ.
    // Multiply by the original OBJ alpha to reject holes, then double. The
    // smallest admitted alpha code is4 after quantization, avoiding the
    // fragile code1 alpha-test conversion exposed by the native layer probe.
    C3D_TexEnvSrc(stage,C3D_Alpha,GPU_PREVIOUS,GPU_TEXTURE0);
    C3D_TexEnvFunc(stage,C3D_Alpha,GPU_MODULATE);
    C3D_TexEnvScale(stage,C3D_Alpha,GPU_TEVSCALE_2);
    C3D_DrawArrays(GPU_TRIANGLES,0,6);
}
}
