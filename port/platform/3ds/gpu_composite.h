#pragma once
#include <citro3d.h>
#include <cstdint>
#include <cstdlib>
#include "ntr/ppu.h"
#include "gpu_capture_plan.h"

// Isolated candidate: no production presentation uses this backend yet.
// Targets remain GPU-resident. The readback method exists solely for oracles
// and DS display capture; render operations never read target pixels on CPU.
namespace ds_gpu {
// True when no pixel of this state can alpha-blend: BLDCNT mode1 needs both a
// first and a second target, and the forced native-3D blend needs a second
// target that can lie behind geometry (BG1-3/OBJ/backdrop; BG0 is the 3D
// layer itself). PLAYER_COMPOSITE_STRICT_BLEND restores the older, broader
// refusal (any mode1 / any second target with geometry) for A/B checks.
// minigames-2d-gpu: the native-3D blend alone (geometry over a BLDCNT
// second target, no 2D alpha pair, no brightness effect, no semi OBJ) is
// drawn by Composite::opaque with the geometry alpha (exact for opaque 3D).
inline bool composite_native_blend(const ntr::PpuCompositionState& s){
    return s.has_geometry && (s.blend&0x3e00);
}
inline bool composite_blend_free(const ntr::PpuCompositionState& s){
    static const bool strict=std::getenv("PLAYER_COMPOSITE_STRICT_BLEND")!=nullptr;
    static const bool native_gpu=std::getenv("PLAYER_NO_GPU_NATIVE_BLEND")==nullptr &&
        std::getenv("PLAYER_COMPOSITE_REFERENCE_MASK")==nullptr;
    const unsigned mode=(s.blend>>6)&3;
    if(s.obj_first_blend && !strict)return true; // Composite::opaque obj_first path
    if(s.semi_back && !strict)return true;       // minigames-30fps-min: obj_first(semi_only)
    if(s.bg_first_blend && !strict)return true;  // minigames-30fps-min: obj_first(First)
    if(s.has_obj_alpha && (!s.semi_geometry || !s.has_geometry))return false;
    if(strict)return mode!=1 && !(s.has_geometry && (s.blend&0x3f00));
    if(composite_native_blend(s) && (!native_gpu || s.has_obj_alpha || mode==2 || mode==3))return false;
    // minigames-30fps-min: mode1 whose only first target is the 3D BG0 is the
    // native-3D blend itself (compose_candidates: a BG0 pixel is always a
    // front geometry pixel, which takes the native branch; no other layer is
    // a first target). Trampoline Time/Terror. PLAYER_NO_MODE1_BG0_NATIVE=1 refuses.
    static const bool bg0_native=std::getenv("PLAYER_NO_MODE1_BG0_NATIVE")==nullptr;
    if(bg0_native && mode==1 && (s.blend&0x3f)==0x01 && s.has_geometry && native_gpu && !s.has_obj_alpha)return true;
    return !(mode==1 && (s.blend&0x3f) && (s.blend&0x3f00));
}
struct Surface {
    C3D_Tex texture{};
    C3D_RenderTarget* target=nullptr;
    bool init();
    void destroy();
};
// minigames-2d-gpu: shared VRAM scratch pool (see gpu_composite.cpp g_pool).
bool scratch_pool_reserve();
void capture_pool_frame(bool capture_used);
Surface* capture_bank_surface(unsigned bank,bool assign=true);
Surface* delayed_geometry_surface();
Surface* capture_scratch_surface();
class Composite {
    friend class NativeObjMerge;
public:
    bool init(const uint32_t* shader, unsigned bytes);
    void destroy();
    // color[c] must agree for c=254 and 255 (signed LUT endpoint alias).
    // This holds for DS six-bit conversion and intermediate integers <128.
    void channel_table(const uint8_t color[256]);
    void map(Surface& output, C3D_Tex& input, bool resident_input=false);
    void copy(Surface& output, C3D_Tex& input, bool resident_input=false);
    void add(Surface& output, C3D_Tex& first, C3D_Tex& second);
    // Nine GPU passes for DS 2D coefficients (round +8, divide16), all integer
    // RGBA8 intermediates; scratch0..2 are reused. This does not implement
    // the independent native3D alpha5 rule (round +16, divide32).
    void blend(Surface& output, C3D_Tex& first, C3D_Tex& second,
               unsigned eva,unsigned evb,Surface (&scratch)[3],
               bool first_resident=true,bool second_resident=true);
    // Opaque/fade slice. Returns false for alpha effects, semi/bitmap OBJ,
    // and native3D with any eligible second target.
    // Presentation RGB is exact; output alpha is always255. DS raw3D capture
    // must use the native geometry target and its own alpha decoding.
    bool opaque(Surface& output,ntr::PpuPreparedCandidates candidates,
                C3D_Tex& geometry,bool resident_geometry=false,bool master=true);
    static void upload(C3D_Tex& input, const uint32_t* argb);
    // castle-30fps: upload only the 8-row tile bands that differ from the
    // previously uploaded image (snapshot, 256x192 ARGB). Exact same texels
    // as upload(); snapshot==nullptr or !valid uploads everything.
    static void upload_changed(C3D_Tex& input,const uint32_t* argb,uint64_t* band_hashes,bool& valid);
    // Same, with the caller's per-row change flags instead of a snapshot.
    static void upload_rows(C3D_Tex& input,const uint32_t* argb,const uint8_t* dirty_rows);
    // castle-30fps: GPU-drawn single affine BG (ntr::PpuGpuAffineLayer).
    // Its colour+priority surface (alpha 32*(priority+2), backdrop 192)
    // doubles as the OBJ merge priority input (resident orientation).
    bool affine_opaque(Surface& output,const ntr::PpuGpuAffineLayer& layer,const ntr::PpuCompositionState& scene,bool master);
    bool ensure_affine(bool reserve_check=false); // castle-min30
    C3D_Tex* affine_priority(){return affine_drawn_?&affine_surface_.texture:nullptr;}
    static void readback(Surface& input, uint32_t* argb, uint32_t* scratch);
    // minigames-2d-gpu: bank <- RGB555(source) re-expanded like bgr555().
    void capture_into(Surface& bank,C3D_Tex& source,bool resident=true);
    // output <- bank (resident), with engine master brightness.
    void view_capture(Surface& output,C3D_Tex& bank,uint16_t master);
    // output <- input RGBA (resident orientation), for the delayed geometry.
    void copy_resident(Surface& output,C3D_Tex& input){copy(output,input,true);}
    // minigames-2d-gpu: true when opaque() stores the front plane's DS
    // priority/layer in the front texture alpha (16*(k+3), k=2*priority+
    // (layer!=OBJ)) and derives the geometry selection on the GPU instead of
    // a CPU mask plane: no windows, no semi OBJ, no masked layer effect.
    // PLAYER_COMPOSITE_CPU_MASK=1 restores the CPU mask/priority planes.
    static bool front_alpha_eligible(const ntr::PpuCompositionState& s);
    // The front texture of the last opaque() call when it carried that
    // encoding (BG alpha 32*(priority+2), the OBJ-merge priority format).
    C3D_Tex* front_alpha_texture(){return front_alpha_frame_?&front_:nullptr;}
    // Encoded-alpha twin of upload_changed (exact same RGB texels).
    static void upload_encoded(C3D_Tex& input,const uint32_t* argb,const uint8_t* layers,
                               const uint8_t* priorities,uint64_t* band_hashes,bool& valid);
    // minigames-30fps-min: paint a binary window cut (ntr::PpuWindowCut) over
    // a composed panel (resident orientation): constant-colour rectangles.
    bool fill_cut(Surface& output,const ntr::PpuWindowCut& cut);
private:
    struct Vertex { float position[4], uv0[2], uv1[2]; };
    void setup(Surface& output, C3D_Tex& input, bool resident_input,bool resident_secondary=true);
    void masked(Surface& output,C3D_Tex& input,bool resident,unsigned channel,bool lookup,bool blend=false);
    void copy_alpha(Surface& output,C3D_Tex& input,bool resident);
    void copy_rgb(Surface& output,C3D_Tex& input,bool resident=false);
    void masked_front(Surface& output,C3D_Tex& geometry,bool resident,bool lookup,unsigned threshold);
    DVLB_s* dvlb_=nullptr;
    shaderProgram_s program_{};
    C3D_AttrInfo attributes_{};
    Vertex* vertices_=nullptr;
    C3D_LightEnv environment_{};
    C3D_Light lights_[3]{};
    C3D_LightLut table_{};
    C3D_Tex front_{},mask_{};
    C3D_Tex back_{}; // minigames-2d-gpu: back plane colours (obj_first_blend frames only)
    bool obj_first(Surface& output,ntr::PpuPreparedCandidates candidates,C3D_Tex& geometry,bool resident,
                   bool base_lookup,const uint8_t* base_table,int variant=0); // 0 OBJ first, 1 semi_back, 2 bg_first
    uint64_t front_bands_[24]{};bool front_bands_valid_=false;
    Surface affine_surface_{};C3D_Tex affine_texture_{};Vertex* affine_vertices_=nullptr;
    // castle-min30: CPU overlay plane (text layers in front of the affine BG).
    C3D_Tex affine_overlay_{};uint64_t affine_overlay_generation_=0;uint64_t affine_overlay_bands_[24]{};bool affine_overlay_bands_valid_=false;
    const uint32_t* affine_source_=nullptr;uint64_t affine_generation_=~0ull;int affine_width_=0,affine_height_=0;
    bool affine_ready_=false,affine_drawn_=false;
    uint32_t* mask_pixels_=nullptr;
    uint32_t* front_snapshot_=nullptr;
    uint8_t* mask_snapshot_=nullptr;
    const void* front_identity_=nullptr;
    const void* mask_identity_=nullptr;
    uint64_t front_generation_=0,mask_generation_=0;
    uint32_t mask_key_=0,mask_table_key_=~0u;
    uint32_t* mask_lut_=nullptr;uint32_t mask_lut_key_=~0u; // castle-min30: per-compositor mask table
    bool front_valid_=false,mask_valid_=false,front_versioned_=false;
    unsigned mask_full_streak_=0,mask_skip_=0; // minigames-30fps: adaptive row-compare
    bool front_encoded_=false,front_alpha_frame_=false; // minigames-2d-gpu
    unsigned front_method_=0; // 0 plain, 1 encoded while uploading, 2 PPU-encoded alpha
    Vertex* cut_vertices_=nullptr; // minigames-30fps-min: PpuWindowCut::kMaxRects*6
    uint64_t obj_mask_sig_=0;bool obj_mask_any_back_=false,obj_mask_any_geometry_=false; // obj_first mask reuse
    uint64_t obj_back_sig_=0;
};
}
