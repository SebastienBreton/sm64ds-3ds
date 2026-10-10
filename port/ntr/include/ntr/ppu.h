// DS 2D engine scan-out.
//
// The 2D engines are pure register state: DISPCNT and BGxCNT say where the
// tilemaps and tiles live, and VRAM holds them. Nothing about drawing is
// write-triggered, so the host can simply read the register file once per frame
// and rasterise -- no interception needed, which is why 68% of the decomp's
// hardware accesses need no more than a working memory map to behave correctly.
//
// See notes/assessment.md section 2a and docs/mmio-inventory.md.

#ifndef NTR_PPU_H
#define NTR_PPU_H

#include <stdint.h>
#include <optional>

namespace ntr {

constexpr int SCREEN_W = 256;
constexpr int SCREEN_H = 192;

enum Engine { ENGINE_A = 0, ENGINE_B = 1 };

struct Framebuffer {
    uint32_t px[SCREEN_H][SCREEN_W];   // 0xAARRGGBB
};

// Rasterise supported tiled backgrounds with the current window layer masks.
void ppu_scanout(Engine eng, Framebuffer &fb);

// Rasterise one engine's OBJ layer (sprites) from its OAM + OBJ VRAM over
// whatever fb already holds. Plain and affine sprites, 16- and 256-color
// tiles and 1D/2D mapping, plus direct-color bitmap OBJs; mosaic remains unsupported.
// Window objects produce masks rather than colour pixels.
void ppu_scanout_obj(Engine eng, Framebuffer &fb);

// Compose supported text/extended-affine BGs, 3D BG0 and OBJ by priority,
// then window layer/effect masks and alpha/brightness effects. Extended bitmap
// BG2/3 and bitmap OBJ are supported; large mode6 and mosaic remain unsupported.
// Scanline register changes must be supplied separately.
void ppu_scanout_scene(Engine eng, Framebuffer& fb);

// Same composition, using a supplied 256x192 GX layer instead of gx_render.
// Geometry uses Framebuffer's 0xAARRGGBB layout; alpha zero is a BG0 hole.
// Its alpha is the native five-bit value expanded to eight bits. A front3D
// pixel blends with its own alpha when the immediate second layer is eligible,
// independently of the normal effect mode/window bit. Final output is opaque.
// It is used only when Engine A has 3D BG0 enabled; Engine B/blank modes ignore
// it. No GX commands, rendering, or framebuffer readback occur here. The input
// is not modified and must be separate from fb. Existing PPU limitations apply.
void ppu_scanout_scene_with_geometry(Engine eng, Framebuffer& fb, const Framebuffer& geometry);

// Geometry-independent 2D candidates. Prepare once and compose either eye.
// These are large buffers: allocate statically or outside the game's DS heap.
struct PpuPreparedScene {
    Framebuffer front, back;
    // Low bits identify BG0..3/OBJ/backdrop (0..5). Front-layer bit7 marks
    // a winning semi-transparent OBJ; it forces alpha to a second target.
    uint8_t front_layer[SCREEN_W*SCREEN_H],front_priority[SCREEN_W*SCREEN_H];
    uint8_t back_layer[SCREEN_W*SCREEN_H],back_priority[SCREEN_W*SCREEN_H];
    uint8_t window_mask[SCREEN_W*SCREEN_H];
    // Winning OBJ alpha:0 normal,17 mode1 using BLDALPHA,2..16 mode3 using
    // its own alpha and16-alpha. Meaningful when front_layer has bit7 set.
    uint8_t front_obj_alpha[SCREEN_W*SCREEN_H];
    uint16_t blend,alpha,brightness,master;
    uint8_t geometry_priority;
    bool blank,has_geometry,resolved,has_windows,has_obj_alpha;
    // minigames-2d-gpu: the (unresolved) front plane's alpha byte holds
    // ppu_front_alpha_code(layer,priority) at every pixel.
    bool front_alpha_encoded=false;
};
// minigames-2d-gpu: alpha byte stored in front planes for the GPU compositor:
// 16*(k+3), k=2*priority+(layer!=OBJ). BG priority p gives 32*(p+2), the OBJ
// merge format; geometry (BG0 priority g) is in front iff code>=32*(g+2).
// Colour consumers only ever test alpha!=0, which still holds.
inline uint32_t ppu_front_alpha_code(unsigned layer,unsigned priority){
    const unsigned a=16u*(2u*(priority&7u)+((layer&7u)==4u?0u:1u)+3u);
    return a>255u?255u:a;
}
void ppu_prepare_scene(Engine eng,PpuPreparedScene& scene);
// Exact input snapshots invalidate the per-engine cache; no hash-only reuse.
const PpuPreparedScene& ppu_prepare_scene_cached(Engine eng);
// Explicit view of the cache's candidates before layer/master effects. The
// front buffer may differ from the legacy scene.front when the ordinary API
// pre-resolved a geometry-independent scene. Use this front and ignore
// state.resolved. Legacy references stay valid until this engine is prepared again.
// Composition state is owned by the view, never read back from cached planes.
// An OAM-only update can reuse immutable BG pixels while these registers change.
// minigames-30fps-min: "binary" windows (scene wipes, iris): every active
// window region (WIN0/WIN1 inside, outside; no OBJ window, no display
// capture) either shows the whole windowless composition ("full": all
// enabled layers and, when an effect is active, the effect bit) or nothing
// but the backdrop ("empty"). The candidates are then prepared WITHOUT
// windows (window registers and per-line shadows are not cache keys) and the
// empty pixels are overwritten at the end of the composition by these
// rectangles of one final colour (backdrop, its brightness effect, master).
// PPU_NO_WINDOW_CUT=1 restores the per-pixel window masks.
struct PpuWindowCut {
    static constexpr unsigned kMaxRects=192*6;
    struct Rect {uint16_t x0,x1,y0,y1;}; // half-open DS pixels
    uint32_t color=0;                     // 0xAARRGGBB (alpha 0xff)
    unsigned count=0;
    uint64_t signature=0;
    Rect rects[kMaxRects];
};
struct PpuCompositionState {
    uint16_t blend,alpha,brightness,master;
    uint8_t geometry_priority;
    bool blank,has_geometry,resolved,has_windows,has_obj_alpha;
    // Set by ppu_prepare_candidates_cached when every semi-transparent OBJ
    // pixel uses BLDALPHA, lies in front of geometry, and can only blend with
    // geometry (never with the back plane): GPU composition may blend it.
    bool semi_geometry=false;
    // minigames-2d-gpu: BLDCNT mode1 whose only first target is OBJ (BG0 a
    // second target) over geometry: every OBJ pixel (semi ones use BLDALPHA)
    // blends with the 3D where the 3D lies between it and the back pixel,
    // else with the back pixel (Bob-omb Battlefield cannon transition).
    bool obj_first_blend=false;
    // minigames-30fps-min: semi-transparent OBJ (BLDALPHA, alpha 17) over a
    // geometry engine with no other first target (BLDCNT mode0, or mode1
    // without first targets) and no windows: each semi OBJ pixel blends
    // with the 3D where the 3D lies between it and the back pixel (if BG0 is
    // a second target; else it stays opaque there), otherwise with the back
    // pixel if that is a second target (Mix-a-Mug). Composite::obj_first.
    bool semi_back=false;
    // minigames-30fps-min (game intro, Peach's letter BLDCNT 0148): BLDCNT
    // mode1 with BG/OBJ first targets (none also a second target) over a
    // geometry engine, windows allowed: every first-target pixel (and semi
    // OBJ, BLDALPHA only) blends with the 3D where the 3D lies between it and
    // the back pixel (if BG0 is a second target), else with the back pixel if
    // that is a second target. Composite::obj_first (mode First).
    bool bg_first_blend=false;
    // minigames-30fps-min: non-null only on candidate views of a frame with
    // binary windows and at least one empty pixel (see PpuWindowCut). The
    // planes ignore windows; every consumer must paint these rectangles over
    // its final output. Valid until four more candidate preparations of the
    // same engine.
    const PpuWindowCut* window_cut=nullptr;
};
struct PpuBackgroundPlanes {
    Framebuffer front,back;
    uint8_t front_layer[SCREEN_W*SCREEN_H],front_priority[SCREEN_W*SCREEN_H];
    uint8_t back_layer[SCREEN_W*SCREEN_H],back_priority[SCREEN_W*SCREEN_H];
};
// castle-30fps: one extended-affine BG and nothing else (rotating
// bottom-screen maps). The GPU samples this whole-map raster with the DS
// affine map (texel = (ref + P*(x,y))>>8) instead of the CPU rasterizing
// the planes. raster stays immutable until generation changes.
struct PpuGpuAffineLayer {
    const uint32_t* raster;int width,height;uint64_t raster_generation;
    int32_t ref_x,ref_y;int16_t pa,pb,pc,pd;bool wrap;
    uint32_t backdrop;uint8_t layer,priority;
    // Rows bands (8 rows each, up to 1024 rows) that differ from the raster
    // of base_generation; a consumer holding exactly that generation may
    // update only these bands. base_generation==0: no incremental basis.
    uint64_t base_generation;uint64_t dirty_bands[2];
    // castle-min30: text BG layers that lie in front of the affine layer at
    // every pixel (DS priority order), merged on the CPU into one 256x192
    // plane: ARGB of the front-most opaque overlay texel with the OBJ-merge
    // priority code 32*(priority+2) in alpha, 0 where every overlay is
    // transparent. Immutable while overlay_generation is unchanged.
    const uint32_t* overlay=nullptr;uint64_t overlay_generation=0;
    // BLDCNT brightness effect (mode 2 up / 3 down, factor BLDY) on the
    // affine texels (effect_layer) and/or the backdrop (effect_backdrop);
    // overlays are never first targets on this path.
    uint8_t effect_mode=0,effect_factor=0;bool effect_layer=false,effect_backdrop=false;
};
struct PpuPreparedCandidates {
    PpuCompositionState state;
    const Framebuffer& front;
    const Framebuffer& back;
    // Non-null only for the independently owned, exact-snapshot BG cache.
    // Identity+generation authorizes reuse of both images and four metadata planes;
    // it says nothing about the separately owned composition state.
    const PpuBackgroundPlanes* background_identity;
    uint64_t background_generation;
    // Non-null: the BG planes are NOT rasterized; draw this layer on the GPU.
    const PpuGpuAffineLayer* gpu_affine=nullptr;
    // minigames-2d-gpu: front.px alpha == ppu_front_alpha_code(front_layer,
    // front_priority) everywhere (set only by the PPU for such views).
    bool front_alpha_encoded=false;
    PpuPreparedCandidates(const PpuPreparedScene& s,const Framebuffer& f)
        : state{s.blend,s.alpha,s.brightness,s.master,s.geometry_priority,
                s.blank,s.has_geometry,s.resolved,s.has_windows,s.has_obj_alpha},
          front(f),back(s.back),background_identity(nullptr),background_generation(0),
          front_layer_(s.front_layer),front_priority_(s.front_priority),
          back_layer_(s.back_layer),back_priority_(s.back_priority),
          window_mask_(s.window_mask),front_obj_alpha_(s.front_obj_alpha) {}
    // castle-30fps: BG-only planes with a separately owned (pre-blended)
    // front image; identity is a distinct token for that image.
    PpuPreparedCandidates(PpuCompositionState s,const PpuBackgroundPlanes& p,uint64_t generation,
                          const Framebuffer& front_image,const void* identity)
        : state(s),front(front_image),back(p.back),background_identity(static_cast<const PpuBackgroundPlanes*>(identity)),
          background_generation(generation),
          front_layer_(p.front_layer),front_priority_(p.front_priority),
          back_layer_(p.back_layer),back_priority_(p.back_priority),
          window_mask_(nullptr),front_obj_alpha_(nullptr) {}
    PpuPreparedCandidates(PpuCompositionState s,const PpuBackgroundPlanes& p,uint64_t generation)
        : state(s),front(p.front),back(p.back),background_identity(&p),background_generation(generation),
          front_layer_(p.front_layer),front_priority_(p.front_priority),
          back_layer_(p.back_layer),back_priority_(p.back_priority),
          window_mask_(nullptr),front_obj_alpha_(nullptr) {}
    const uint8_t* front_layer() const {return front_layer_;}
    const uint8_t* front_priority() const {return front_priority_;}
    const uint8_t* back_layer() const {return back_layer_;}
    const uint8_t* back_priority() const {return back_priority_;}
    // Null only for a BG-only view, whose state.has_windows and
    // state.has_obj_alpha are false. Test those flags before sampling.
    const uint8_t* window_mask() const {return window_mask_;}
    const uint8_t* front_obj_alpha() const {return front_obj_alpha_;}
private:
    const uint8_t *front_layer_,*front_priority_,*back_layer_,*back_priority_;
    const uint8_t *window_mask_,*front_obj_alpha_;
};
PpuPreparedCandidates ppu_prepare_candidates_cached(Engine eng);
// Read-only, worker-safe: true when the next ppu_prepare_candidates_cached(eng)
// would return the previously prepared scene unchanged (castle-30fps).
bool ppu_prepared_inputs_unchanged(Engine eng);
// minigames-30fps-min: the binary window cut (or its absence) equals the one
// last handed out with a candidate view of this engine.
bool ppu_window_cut_unchanged(Engine eng);
// minigames-30fps-min: the engine's windows are binary (see PpuWindowCut): the
// GPU OBJ path may ignore them, the cut is painted over its final panel.
bool ppu_windows_binary(Engine eng);
// minigames-30fps-min: early preparation. While a presentation job reads the
// candidate views it was given, pin their cache ways; a preparation of an
// engine whose active way is pinned then writes the other way (allocated on
// host memory on first need) unless it is a pure read-only cache hit.
void ppu_pin_candidate_ways(bool pinned);
// True when a candidate preparation of eng cannot touch a pinned way.
bool ppu_unpinned_way_available(Engine eng);
// BG/backdrop candidates only: no OBJ raster and no effect resolution. Rejects
// all windows and display2/3. Pure pixel planes allocate lazily and are borrowed
// without copying on a hit. Their independent lifetime survives ordinary
// preparations, until the next BG-only preparation for this engine. The view
// owns the composition registers of its frame; they never come from the cache.
// An unsupported GPU consumer must re-prepare normal candidates for fallback.
std::optional<PpuPreparedCandidates> ppu_prepare_background_candidates_cached(Engine eng);
// castle-min30: engine-B GPU-affine attempt only (thread-safe against engine
// A preparation; see ppu.cpp). Call ppu_sync_vram_views() before and
// ppu_apply_window_cut() on the result after (main thread).
void ppu_sync_vram_views();
std::optional<PpuPreparedCandidates> ppu_prepare_background_affine(Engine eng);
void ppu_apply_window_cut(Engine eng,PpuPreparedCandidates& candidates);
// Engine A's capture source is regular BGOBJ even when final display is off.
// Returns unresolved candidates; display-off uses a separate uncached scene.
// Other display modes share the ordinary cache and its reference lifetime.
PpuPreparedCandidates ppu_prepare_capture_candidates();
void ppu_prepared_cache_stats(uint64_t& hits,uint64_t& misses);
// BG planes can be reused when only OBJ state changes; exact input snapshots
// and the resolved OBJ-window masks guard this independent cache.
void ppu_background_cache_stats(uint64_t& hits,uint64_t& misses);
struct PpuBackgroundTransferStats {
    uint64_t input_compared,input_snapshotted,planes_copied,planes_avoided;
    uint64_t immutable_hits,immutable_misses;
};
PpuBackgroundTransferStats ppu_background_transfer_stats(bool reset=false);
// Optional caller-supplied monotonic clock, in milliseconds. Null disables it.
struct PpuPrepareProfile { double windows,bg,objects,resolve; uint64_t calls; double raster=0; uint64_t raster_calls=0; double cached=0,bg_only=0,obj_plan=0; double detail[4]{}; };
// Diagnostics: add externally measured OBJ-plan time to an engine profile.
void ppu_prepare_profile_add_obj_plan(Engine eng,double ms);
double (*ppu_prepare_profile_clock_get())();
void ppu_prepare_profile_clock(double (*clock)());
PpuPrepareProfile ppu_prepare_profile_read(Engine eng,bool reset);
// Whole-map text BG raster cache counters (diagnostics).
void ppu_layer_cache_stats(uint64_t& full,uint64_t& tiles,uint64_t& hits,uint64_t& direct);
// rows reused, dirty rows, full rasters, scroll reuses, refusal counters[6] (diagnostics).
void ppu_bg_reuse_stats(uint64_t* out);
void ppu_compose_prepared(const PpuPreparedScene& scene,const Framebuffer& geometry,Framebuffer& output);
// master=false produces BGOBJ before master brightness, for display capture.
// This shares selection/effect arithmetic with the current CPU path.
void ppu_compose_candidates(PpuPreparedCandidates candidates,const Framebuffer& geometry,
                            Framebuffer& output,bool master=true);

// minigames-2d-gpu: true when the whole visible output of the engine is a
// 1:1 view of a 256x192 direct-colour bitmap stored in one physical VRAM
// bank (DS dual-screen 3D: the previous frame's display capture shown by
// BG2 in mode5, or by a grid of 2D-mapped bitmap OBJs that covers the screen
// and hides every other OBJ). No window, no effect on that layer, no wrap.
// Only engine B (banks C/D) is recognised. The caller still needs pixels
// with bit15 set everywhere (true for a source-A display capture).
struct PpuCaptureView { unsigned bank,byte_offset; uint16_t master; };
bool ppu_capture_view(Engine eng,PpuCaptureView& view);

// Debug output, so a frame can be inspected without a window yet.
bool ppu_write_bmp(const char *path, const Framebuffer &fb);

}  // namespace ntr

#endif  // NTR_PPU_H
