#include "native_mesh_api.h"
#include "native_mesh_frame_adapter.h"
#include "mixed_clip_partition.h"
#include "held_raster_key.h"
#include "native_mesh_shader.h"
#include "unified_shader.h"
#include <atomic>
#include "platform/3ds/gx_geometry_resources.h"
#include "shadow_stencil_plan.h"
#include "native_readback.h"
#include "native_blit.h"
#include "ntr/host_allocator.h"
// libctru / citro3d side of sm64ds_viewer. Kept apart from viewer.cpp because
// the game's types.h and libctru disagree on u32 (unsigned int vs unsigned long).
//
// The DS geometry engine runs on the CPU (port/ntr/gx.cpp) and hands over
// clip-space triangles; the PICA200 rasterises them here -- full 400x240 top
// screen, stereoscopic when the 3D slider is up.
#include <3ds.h>
#include <citro3d.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "viewer_3ds.h"
#include "frame_profile.h"
#include "platform/3ds/geometry_arena_viewer.h"
#include "platform/3ds/gx_geometry_arena.h"
#include "cpu_mode.h"
#include "viewer_shaders_shbin.h"
#ifdef SM64DS_GPU_COMPOSITE_CONFIG
#include "gpu_composite_build_config.h"
#endif
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
#include "gpu_composite_viewer.h"
#include "gpu_composite.h"
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
#include "native_obj_gpu.h"
#include "native_obj_merge.h"
#endif
#ifdef SM64DS_GPU_COMPOSITE_BUILD
#include "gpu_composite_shaders_shbin.h"
#else
#include "fps_gpu_composite_shader.h"
#endif
#endif

#ifndef PLAYER_SHARED_GFX
extern "C" { u32 __stacksize__ = 1024 * 1024; }
#endif

extern char **environ;
static char *g_empty_env[] = {nullptr};
static u64 g_last_tick;
static float g_fps;
static bool g_auto;
static V3dsDirectLease g_direct_lease;
static uint64_t g_direct_counter;
static bool g_direct_valid,g_direct_open,g_direct_flushed,g_direct_committed;
static ntr::gx_geometry::Prepared g_direct_prepared;

static size_t command_capacity_bytes=C3D_DEFAULT_CMDBUF_SIZE*2;
static size_t command_peak_bytes=0;
// citro3d panics (svcBreak) on C3D_RenderTargetDelete while a frame is open.
// Resources released inside a frame (screen_scale::release on leaving the
// menu layout, e.g. entering a mini-game) are queued here and deleted right
// after that frame's C3D_FrameEnd, on the thread that owns the frame.
// RenderTargetDelete itself waits for the GPU queue before freeing.
struct DeferredRelease{C3D_RenderTarget* target;C3D_Tex texture;};
static DeferredRelease g_deferred_release[16];
static unsigned g_deferred_release_count;
static void deferred_release_flush(){
 for(unsigned i=0;i<g_deferred_release_count;++i){
  auto& r=g_deferred_release[i];
  if(r.target)C3D_RenderTargetDelete(r.target);
  if(r.texture.data)C3D_TexDelete(&r.texture);
  r={};
 }
 g_deferred_release_count=0;
}
static void defer_release(C3D_RenderTarget* target,C3D_Tex* texture){
 if(g_deferred_release_count==16){std::fprintf(stderr,"FATAL: deferred GPU release queue full\n");std::abort();}
 auto& r=g_deferred_release[g_deferred_release_count++];
 r.target=target;r.texture=texture?*texture:C3D_Tex{};
 if(texture)*texture=C3D_Tex{};
}
static void tracked_frame_end(u32 flags){
 C3D_FrameEnd(flags);
 if(g_deferred_release_count)deferred_release_flush();
 const size_t used=size_t(C3D_GetCmdBufUsage()*command_capacity_bytes);
 if(used>command_peak_bytes){
  if(used/(128*1024)>command_peak_bytes/(128*1024))std::printf("GPU command highwater bytes=%u capacity=%u\n",unsigned(used),unsigned(command_capacity_bytes));
  command_peak_bytes=used;
 }
}

static void end_gpu_frame()
{
    // All CPU-written GPU resources are flushed where they change: texture
    // uploads, geometry VBO, shadow-clear quad and native-screen blit VBO.
    // Ask GSP to flush the command list, avoiding Citro3D's full linear-heap
    // flush. Interactive viewer console pixels need their own LCD flush.
    if(!g_auto)gfxFlushBuffers();
    const char* full=std::getenv("PLAYER_FULL_GPU_FLUSH");
    tracked_frame_end(full && std::strcmp(full,"1")==0?0:GX_CMDLIST_FLUSH);
    g_direct_open=false;
}

// ---- citro3d state -------------------------------------------------------------
static C3D_RenderTarget *g_left, *g_right;
static C3D_RenderTarget *g_native_left, *g_native_right;
static u32* g_native_readback;
static bool g_native_stereo;
static C3D_RenderTarget* g_blit_bottom;
static C3D_Tex g_blit_textures[3];
static bool g_blit_ready;
static V3dsVert* g_blit_vertices;
static V3dsVert* g_shadow_clear;
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
static C3D_Tex g_resident_geometry[2];
static C3D_RenderTarget* g_resident_geometry_target[2];
static ds_gpu::Composite g_resident_compositor[2];
static ds_gpu::Surface g_resident_panels[3];
static C3D_RenderTarget* g_resident_bottom;
static V3dsVert* g_resident_vertices;
static uint32_t* g_resident_diagnostic;
static bool g_resident_ready;
static double g_resident_wait;
static double g_resident_previous_gpu;
static unsigned g_resident_vblanks[2];
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
static ds_gpu::NativeObjects g_resident_objects[2];
static ds_gpu::NativeObjMerge g_resident_object_merge[2];
static ds_gpu::Surface g_resident_object_layer[2];
static bool g_resident_objects_ready[2];
#endif
static bool resident_destroy();
#endif
static ds_clear::State g_native_clear;
static held_raster::Lease g_held_raster;
static uint64_t g_held_raster_hits=0,g_held_raster_misses=0;
static bool held_raster_enabled(){static const bool value=[](){const char* s=std::getenv("PLAYER_RETAIN_HELD_RASTER");return s && std::atoi(s)!=0;}();return value;}
void v3ds_set_native_clear(ds_clear::State state){g_native_clear=state;}
static ds_shadow::FramePlan g_shadow_plan;
static ntr::HostVector<ds_shadow::Batch> g_shadow_batches;
static DVLB_s *g_dvlb;
static shaderProgram_s g_prog;
static int g_u_fix, g_u_stereo, g_u_snap=-1, g_u_fogp=-1;
static C3D_AttrInfo g_attr;
static V3dsVert *g_vbo[2];
static int g_vbo_cap = 0x8000;   // vertices per buffer
static int g_frame_parity;
static shaderProgram_s mesh_program;
static int unified_mode_location=-1;
static bool unified_shader_enabled(){static const bool value=[](){const char* s=std::getenv("PLAYER_MIXED_UNIFIED_SHADER");return s&&std::atoi(s)!=0;}();return value;}
static bool unified_draw_scope=false;
static shaderProgram_s* unified_bound_program=nullptr;
static int unified_bound_mode=-1;
static unsigned unified_palette_run=UINT32_MAX;
static int mesh_env_loaded=-2; // gpu-geometry-ingame: lighting state uploaded in this draw scope (-2 none)
static uint64_t unified_program_binds=0,unified_mode_sets=0,unified_palette_hits=0;
struct UnifiedDrawScope {
 UnifiedDrawScope(){unified_draw_scope=true;unified_bound_program=nullptr;unified_bound_mode=-1;unified_palette_run=UINT32_MAX;mesh_env_loaded=-2;}
 ~UnifiedDrawScope(){unified_draw_scope=false;unified_bound_program=nullptr;unified_bound_mode=-1;unified_palette_run=UINT32_MAX;}
};
#include "gx_gpu_backend.inc" // GPU geometry mode (gpu-geometry-menu)
static void shader_bind(bool native){
 gpugeo::bound=false;
 const bool one=unified_shader_enabled();auto* program=one?&g_prog:native?&mesh_program:&g_prog;
 // External compositors may bind independently. Cache only the uninterrupted
 // draw_batches scope, never a FrameBegin boundary or a resident commit.
 const bool bind=!one||!unified_draw_scope||unified_bound_program!=program;
 if(bind){C3D_BindProgram(program);++unified_program_binds;unified_bound_program=program;}
 if(one&&(bind||unified_bound_mode!=int(native))){C3D_BoolUnifSet(GPU_VERTEX_SHADER,unified_mode_location,native);++unified_mode_sets;unified_bound_mode=int(native);}
}
#include "mesh_backend.inc"
namespace v3ds_arena {
bool acquire(V3dsDirectLease& out){
    out={};
    if(g_direct_open || !g_vbo[0] || !g_vbo[1])return false;
    // Retire previous GPU commands before supplying writable linear memory.
    if(!C3D_FrameBegin(0))return false;
    if(++g_direct_counter==0)++g_direct_counter;
    g_direct_lease={g_vbo[g_frame_parity^=1],size_t(g_vbo_cap),g_direct_counter};
    g_direct_valid=g_direct_open=true;g_direct_flushed=g_direct_committed=false;
    out=g_direct_lease;return true;
}
bool abort(uint64_t token){
    if(!g_direct_valid || token!=g_direct_lease.frame_token)return false;
    if(g_direct_open)end_gpu_frame();
    g_direct_valid=false;return true;
}
static bool accepts(const ntr::gx_geometry::Prepared& p){
    if(!g_direct_valid || !p.valid || p.frame_token!=g_direct_lease.frame_token ||
       p.vertices!=g_direct_lease.vertices || p.vertex_count>g_direct_lease.vertex_capacity ||
       p.coordinates!=ntr::gx_geometry::CoordinateConvention::NativePixelCentered ||
       p.batch_count>size_t(g_vbo_cap) || (p.batch_count && !p.batches) ||
       (p.vertex_count && !p.batch_count) || p.vertex_count%3)return false;
    if(g_direct_committed && (p.vertices!=g_direct_prepared.vertices ||
       p.vertex_count!=g_direct_prepared.vertex_count || p.batches!=g_direct_prepared.batches ||
       p.batch_count!=g_direct_prepared.batch_count ||
       p.geometry_generation!=g_direct_prepared.geometry_generation))return false;
    for(size_t i=0;i<p.batch_count;++i){const auto& b=p.batches[i];
        if(b.first<0 || b.count<0 || b.first%3 || b.count%3 || size_t(b.first)>p.vertex_count ||
           size_t(b.count)>p.vertex_count-size_t(b.first))return false;
    }
    return true;
}
bool valid(const ntr::gx_geometry::Prepared& p){return accepts(p);}
}


static const u32 CLEAR = 0x101820FF;   // RGBA
static const u32 DISPLAY_TRANSFER_FLAGS =
    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

// DS textures (decoded 0xAARRGGBB, power-of-two 8..1024) -> PICA RGBA8, tiled.
struct GpuTex {
    C3D_Tex tex;
    bool has_partial_alpha = false,has_opaque_alpha = false;
    uint64_t frame=0,last_use=0;
};
// Immutable DS decodes carry a never-recycled version. External buffers still
// use content hashing; dynamic buffers upload each frame. Keep these identity
// domains separate, and dimensions/alpha explicit rather than XOR-only keys.
struct GpuTextureKey {
    uint64_t identity;
    int w,h;
    unsigned alpha,kind;
    bool operator==(const GpuTextureKey& other) const {
        return identity==other.identity && w==other.w && h==other.h &&
               alpha==other.alpha && kind==other.kind;
    }
};
struct GpuTextureKeyHash {
    size_t operator()(const GpuTextureKey& key) const {
        return std::hash<uint64_t>{}(key.identity) ^ (size_t(key.w)<<5) ^
               (size_t(key.h)<<16) ^ (key.alpha<<1) ^ (key.kind<<27);
    }
};
using GpuCacheEntry=std::pair<const GpuTextureKey,GpuTex*>;
static std::unordered_map<GpuTextureKey,GpuTex*,GpuTextureKeyHash,std::equal_to<GpuTextureKey>,
                          ntr::HostAllocator<GpuCacheEntry>> g_texcache;
static size_t g_texture_bytes;
static uint64_t g_texture_frame,g_texture_use,g_texture_evictions;
static constexpr size_t texture_budget=8*1024*1024;
// Called only after blocking FrameBegin has finished the previous GPU queue.
// Current-frame textures are pinned until both eyes finish. If one frame needs
// more than the retention budget, keep its working set rather than drop draws.
static void trim_gpu_textures(size_t needed,bool all_unused=false) {
    while(all_unused || g_texture_bytes+needed>texture_budget){
        auto oldest=g_texcache.end();
        for(auto it=g_texcache.begin();it!=g_texcache.end();++it)
            if(it->second->frame!=g_texture_frame &&
               (oldest==g_texcache.end() || it->second->last_use<oldest->second->last_use))oldest=it;
        if(oldest==g_texcache.end())break;
        auto* texture=oldest->second;
        g_texture_bytes-=texture->tex.size;
        C3D_TexDelete(&texture->tex);delete texture;g_texcache.erase(oldest);++g_texture_evictions;
    }
}
void v3ds_texture_cache_usage(size_t& entries,size_t& bytes,uint64_t& evictions){
    entries=g_texcache.size();bytes=g_texture_bytes;evictions=g_texture_evictions;
}
// Decoded texels are stable during a submitted frame. Resolve each buffer
// once before either eye draws; hashing per batch and per eye is expensive
// on ARM11 and redundant for materials sharing a texture.
struct FrameTex { const uint32_t *pixels; int w, h; bool dynamic; unsigned alpha; uint64_t version; GpuTex *gpu; };
static ntr::HostVector<FrameTex> g_frame_textures;
static ntr::HostVector<GpuTex *> g_batch_textures;


static uint64_t tex_key(const uint32_t *rgba, int w, int h)
{
    uint64_t k = 1469598103934665603ull ^ (uint64_t)(uintptr_t)rgba ^ ((uint64_t)w << 40) ^ ((uint64_t)h << 52);
    for (int i = 0; i < w * h; ++i) k = (k ^ rgba[i]) * 1099511628211ull;
    return k;
}

static inline u32 morton8(u32 x, u32 y)
{
    // interleave the low 3 bits of x and y: PICA 8x8 tile order
    u32 m = 0;
    for (int i = 0; i < 3; ++i) m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
    return m;
}

static GpuTex *gpu_texture(const uint32_t *rgba, int w, int h, bool dynamic, unsigned polygon_alpha, uint64_t version)
{
    const GpuTextureKey key{dynamic ? uint64_t(uintptr_t(rgba)) : version ? version : tex_key(rgba,w,h),
                            w,h,polygon_alpha,dynamic?2u:version?1u:0u};
    auto it = g_texcache.find(key);
    GpuTex *t;
    if (it != g_texcache.end()) {
        it->second->frame=g_texture_frame;it->second->last_use=++g_texture_use;
        if (!dynamic) return it->second;
        t = it->second;
    } else {
        trim_gpu_textures(size_t(w)*h*4);
        t = new(ntr::HostAllocator<GpuTex>{}.allocate(1)) GpuTex;
        if (!C3D_TexInit(&t->tex, w, h, GPU_RGBA8)) {
            trim_gpu_textures(0,true);
            if(!C3D_TexInit(&t->tex,w,h,GPU_RGBA8)){delete t;return nullptr;}
        }
        t->frame=g_texture_frame;t->last_use=++g_texture_use;
        g_texture_bytes+=t->tex.size;
        g_texcache[key] = t;
    }
    t->has_partial_alpha = false;t->has_opaque_alpha=false;
    u32 *dst = (u32 *)t->tex.data;
    // DS combines 5-bit texture and polygon alpha before rasterization.
    // Store the resulting blend weight, not merely texture alpha, in
    // the PICA texture. This also preserves A3I5/A5I3 alpha ramps.
    u32 alpha_of[32];
    for (unsigned ta = 0; ta < 32; ++ta) {
        const unsigned a5 = ((ta + 1) * (polygon_alpha + 1) - 1) >> 5;
        alpha_of[ta] = a5 == 0 ? 0 : a5 == 31 ? 255 : ((a5 + 1) * 255 + 16) / 32;
    }
    // Same layout as morton8 per texel: x bits at even, y bits at odd positions.
    static const u8 morton_x[8]={0,1,4,5,16,17,20,21};
    u32 seen = 0; // texture alpha values present (bit ta)
    // PICA's t axis starts at the bottom row: storing image row y at PICA row
    // h-1-y lets the DS convention (v = 0 at the top) use t = v/h unchanged.
    for (int y = 0; y < h; ++y) {
        const int py = h - 1 - y;
        u32 *row = dst + (py >> 3) * (w >> 3) * 64 + morton8(0, py & 7);
        const uint32_t *src = rgba + y * w;
        for (int x = 0; x < w; x += 8, src += 8, row += 64)
            for (int k = 0; k < 8; ++k) {
                const uint32_t c = src[k];
                const unsigned ta = c >> 27;
                seen |= 1u << ta;
                row[morton_x[k]] = (c << 8) | alpha_of[ta];
            }
    }
    for (unsigned ta = 0; ta < 32; ++ta) if (seen >> ta & 1) {
        t->has_partial_alpha |= alpha_of[ta] != 0 && alpha_of[ta] != 255;
        t->has_opaque_alpha |= alpha_of[ta] == 255;
    }
    C3D_TexFlush(&t->tex);
    C3D_TexSetFilter(&t->tex, GPU_NEAREST, GPU_NEAREST);
    return t;
}

static GPU_TEXTURE_WRAP_PARAM wrap_mode(bool repeat, bool flip)
{
    if (!repeat) return GPU_CLAMP_TO_EDGE;
    return flip ? GPU_MIRRORED_REPEAT : GPU_REPEAT;
}

static void gpu_init()
{
    const char* size_option=std::getenv("PLAYER_GPU_COMMAND_BUFFER_KIB");
    const int kib=size_option?std::atoi(size_option):512;
    if(kib!=512 && kib!=1024 && kib!=2048 && kib!=4096){std::fprintf(stderr,"FATAL unsupported GPU command capacity %d\n",kib);std::abort();}
    command_capacity_bytes=size_t(kib)*1024;
    if(!C3D_Init(command_capacity_bytes)){std::fprintf(stderr,"FATAL GPU command buffer allocation %u\n",unsigned(command_capacity_bytes));std::abort();}
    std::printf("GPU command capacity bytes=%u\n",unsigned(command_capacity_bytes));
    g_left = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    g_right = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(g_left, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    C3D_RenderTargetSetOutput(g_right, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

    g_dvlb = unified_shader_enabled()?DVLB_ParseFile(reinterpret_cast<u32*>(const_cast<unsigned char*>(unified_shader)),sizeof(unified_shader)):DVLB_ParseFile((u32 *)viewer_shaders_shbin, viewer_shaders_shbin_size);
    shaderProgramInit(&g_prog);
    shaderProgramSetVsh(&g_prog, &g_dvlb->DVLE[0]);
    if(unified_shader_enabled()){unified_mode_location=shaderInstanceGetUniformLocation(g_prog.vertexShader,"native_mode");if(unified_mode_location<0)std::abort();}
    shader_bind(false);
    g_u_fix = shaderInstanceGetUniformLocation(g_prog.vertexShader, "fix");
    g_u_stereo = shaderInstanceGetUniformLocation(g_prog.vertexShader, "stereo");
    g_u_snap = shaderInstanceGetUniformLocation(g_prog.vertexShader, "ds_snap");
    g_u_fogp = unified_shader_enabled()?shaderInstanceGetUniformLocation(g_prog.vertexShader, "fogp"):-1;

    AttrInfo_Init(&g_attr);
    AttrInfo_AddLoader(&g_attr, 0, GPU_FLOAT, 4);   // clip position
    AttrInfo_AddLoader(&g_attr, 1, GPU_FLOAT, 4);   // colour
    AttrInfo_AddLoader(&g_attr, 2, GPU_FLOAT, 2);   // texcoord
    C3D_SetAttrInfo(&g_attr);

    for (int i = 0; i < 2; ++i) g_vbo[i] = (V3dsVert *)linearAlloc(g_vbo_cap * sizeof(V3dsVert));

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    gfxSet3D(true);
}

void v3ds_init()
{
    environ = g_empty_env;
    setenv("SM64DS_ASSET_ROOT", "sdmc:/sm64ds/repo", 1);
    // Debug knobs: one KEY=VALUE per line in sdmc:/sm64ds/env.txt.
    if (FILE *ef = fopen("sdmc:/sm64ds/env.txt", "r")) {
        char line[256];
        while (fgets(line, sizeof line, ef)) {
            line[strcspn(line, "\r\n")] = 0;
            if (char *eq = strchr(line, '=')) {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        fclose(ef);
    }
    chdir("sdmc:/sm64ds");
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    sm64ds_configure_cpu_mode();
    printf("\x1b[3;1HSM64DS on 3DS - the game's own code\n"
           " A: model / level   L/R: animation / zoom\n"
           " X: characters <-> levels\n"
           " pad: orbit the level camera\n"
           " SELECT: GPU / CPU raster   START: quit\n"
           " 3D slider: stereoscopic depth\n");
    gpu_init();
}

void v3ds_renderer_init() { g_auto=true; gpu_init(); C3D_CullFace(GPU_CULL_BACK_CCW); }

void v3ds_exit()
{
    if(g_direct_open && !v3ds_arena::abort(g_direct_lease.frame_token))return;
    g_direct_valid=false;
#ifndef SM64DS_GPU_COMPOSITE_CANDIDATE
    // FrameBegin waits for command processing and LCD transfers. An empty
    // frame does not reference resources that are about to be released.
    if(!C3D_FrameBegin(0))return;
    tracked_frame_end(GX_CMDLIST_FLUSH);
#endif
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(!resident_destroy())return;
#endif
    // The context and g_prog are still alive while the mesh program drains.
    if(!mesh_destroy())return;
    mixed_frame.reset();mixed_adapter.reset();if(mixed_cpu)linearFree(mixed_cpu);mixed_cpu=nullptr;
    if(g_shadow_clear)linearFree(g_shadow_clear);g_shadow_clear=nullptr;
    if(g_blit_ready)for(auto& texture:g_blit_textures)C3D_TexDelete(&texture);
    if(g_blit_bottom)C3D_RenderTargetDelete(g_blit_bottom);
    if(g_blit_vertices)linearFree(g_blit_vertices);
    g_blit_ready=false;g_blit_bottom=nullptr;g_blit_vertices=nullptr;
    if(g_native_left)C3D_RenderTargetDelete(g_native_left);
    if(g_native_right)C3D_RenderTargetDelete(g_native_right);
    if(g_native_readback)linearFree(g_native_readback);
    g_native_left=g_native_right=nullptr;g_native_readback=nullptr;
    for (auto &kv : g_texcache) { C3D_TexDelete(&kv.second->tex); delete kv.second; }
    g_texcache.clear();g_texture_bytes=0;
    for (int i = 0; i < 2; ++i) linearFree(g_vbo[i]);
    // The context still refers to g_prog until Fini; release it afterwards.
    C3D_Fini();
    shaderProgramFree(&g_prog);
    DVLB_Free(g_dvlb);
    #ifndef PLAYER_SHARED_GFX
    gfxExit();
    #endif
}

static void tick_fps(const char *status, const char *mode)
{
    const u64 now = svcGetSystemTick();
    if (g_last_tick) {
        const float dt = (float)(now - g_last_tick) / SYSCLOCK_ARM11;
        if (dt > 0) g_fps = g_fps ? g_fps * 0.9f + 0.1f / dt : 1.0f / dt;
    }
    g_last_tick = now;
    // SD logging every frame distorts the benchmark and floods the log.
    static unsigned frames;
    if (!g_auto || ++frames % 60 == 0)
        printf(g_auto ? "fps scene=%s mode=%s value=%.2f\n" : "\x1b[1;1H%-26s %s %5.1f fps ", status, mode, g_fps);
}

// Column-major-free helper: uniform rows for C3D_FVUnifSet (x,y,z,w per row).
static void set_fix(float aspect_x, float sep, float conv, bool perspective)
{
    // DS clip (x right, y up) -> PICA clip for a 240x400 target mounted rotated:
    // panel x (400 px, left->right) is PICA -y, panel y (240 px, up) is PICA x.
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_u_fix + 0, 0.0f, 1.0f, 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_u_fix + 1, -aspect_x, 0.0f, 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_u_fix + 2, 0.0f, 0.0f, 1.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_u_fix + 3, 0.0f, 0.0f, 0.0f, 1.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_u_stereo,
                  perspective ? sep : 0.0f, perspective ? -sep * conv : 0.0f,
                  perspective ? 0.0f : sep, sep<0?-sep:sep);
    if(g_u_snap>=0)C3D_BoolUnifSet(GPU_VERTEX_SHADER,g_u_snap,false);
}

// 3D slider position (SM64DS_FORCE_3D overrides it for headless tests).
static float stereo_slider(){
    float slider=osGet3DSliderState();
    if(const char* forced=std::getenv("SM64DS_FORCE_3D"))slider=float(std::atof(forced));
    return slider<0.f?0.f:slider>1.f?1.f:slider;
}

// DS-faithful LCD presentation of the two composed 256x192 DS screens.
// While the game marks the title/main menu active (v3ds_set_menu_presentation),
// both screens use the same uniform 1.25 scale (320x240), the upper one centred
// in the 400-pixel LCD exactly above the lower LCD. This keeps the original
// 4:3 aspect and makes objects that move between screens (the title star) keep
// their size and horizontal position. "Sharp" filtering first doubles the DS
// image with nearest sampling into a 512x384 intermediate, then samples that
// bilinearly: every DS pixel keeps a uniform 1.25-pixel footprint with only its
// edges blended, instead of nearest-neighbour's irregular doubled columns/rows.
// Other scenes keep the previous full-width nearest presentation unchanged.
// SM64DS_TOP_LAYOUT=ds forces this for every scene, =stretch disables it;
// SM64DS_SCREEN_FILTER=nearest keeps the DS layout without the sharp filter.
// Written by the game thread at scene changes, read by the presentation worker.
static std::atomic<bool> g_menu_presentation{false};
namespace screen_scale {
static int layout_override(){
    static const int value=[](){const char* v=std::getenv("SM64DS_TOP_LAYOUT");
        return !v?0:std::strcmp(v,"ds")==0?1:std::strcmp(v,"stretch")==0?-1:0;}();
    return value;
}
static bool ds_layout(){
    const int forced=layout_override();
    return forced>0 || (forced==0 && g_menu_presentation.load(std::memory_order_relaxed));
}
static bool sharp_option(){
    static const bool value=[](){const char* v=std::getenv("SM64DS_SCREEN_FILTER");return !(v&&std::strcmp(v,"nearest")==0);}();
    return value;
}
// The doubled intermediate is six 512x64 VRAM render targets, DS rows
// 32k..32k+31 (6 x 128 KiB: fits a fragmented VRAM where one 1 MiB 512x512
// block may not, e.g. back in the menu after a stage; render targets cannot
// live in linear memory). 32*1.25=40 is an LCD row boundary, so the slices
// add no seam. When VRAM is short the layout still applies, nearest-sampled.
constexpr unsigned kSlices=6; // 512x64 targets, DS rows 32k..32k+31
static C3D_Tex g_slice_tex[kSlices];
static C3D_RenderTarget* g_slice_target[kSlices];
static V3dsVert* g_vertices;
static bool g_ready,g_failed,g_sharp_ok;
static bool sharp(){return g_sharp_ok;}
// Vertex groups of six: 0/1 pass-1 (resident / CPU-upload source convention),
// 2/3 pass-2 top/bottom, 4/5 nearest resident top/bottom, 6/7 nearest upload.
static void quad(V3dsVert* out,float half_width,float u0,float v_top,float v_bottom){
    const V3dsVert q[4]={{-half_width,1,0,1,1,1,1,1,u0,v_top},{half_width,1,0,1,1,1,1,1,u0+1,v_top},
        {half_width,-1,0,1,1,1,1,1,u0+1,v_bottom},{-half_width,-1,0,1,1,1,1,1,u0,v_bottom}};
    const unsigned indices[]={0,1,2,0,2,3};
    for(unsigned i=0;i<6;++i)out[i]=q[indices[i]];
}
static bool init(){
    if(g_ready)return true;
    if(g_failed)return false;
    g_failed=true;
    auto fail=[](const char* step){
        std::printf("Screen layout resources failed at %s: VRAM free=%u linear free=%u\n",step,unsigned(vramSpaceFree()),unsigned(linearSpaceFree()));
        return false;
    };
    // Vertices persist; VRAM targets are released whenever the menu layout is
    // left (release()), so stages and mini-games keep their VRAM.
    if(!g_vertices)g_vertices=static_cast<V3dsVert*>(linearAlloc(216*sizeof(V3dsVert)));
    if(!g_vertices)return fail("vertices");
    g_sharp_ok=false;
    if(sharp_option()){
        auto target=[](C3D_Tex& tex,C3D_RenderTarget*& rt,unsigned height){
            if(!C3D_TexInitVRAM(&tex,512,height,GPU_RGBA8))return false;
            C3D_TexSetFilter(&tex,GPU_LINEAR,GPU_LINEAR);
            C3D_TexSetWrap(&tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
            rt=C3D_RenderTargetCreateFromTex(&tex,GPU_TEXFACE_2D,0,-1);
            if(!rt){C3D_TexDelete(&tex);return false;}
            return true;
        };
        unsigned made=0;
        while(made<kSlices && target(g_slice_tex[made],g_slice_target[made],64))++made;
        if(made==kSlices)g_sharp_ok=true;
        // init() runs inside a frame (from draw): a partial allocation must be
        // released after C3D_FrameEnd, never with RenderTargetDelete here.
        else while(made){--made;defer_release(g_slice_target[made],&g_slice_tex[made]);g_slice_target[made]=nullptr;}
        if(!g_sharp_ok)fail("sharp intermediate (using nearest sampling)");
    }
    const float top=320.f/400.f,top_width=320.f;
    quad(g_vertices+0,1,0,.75f,0);
    quad(g_vertices+6,1,0,0,.75f);
    quad(g_vertices+12,top,0,.75f,0);
    quad(g_vertices+18,1,0,.75f,0);
    // Integer nearest-neighbour coverage, as in the previous direct quads.
    const float rv=.75f+.5f*.75f/240.f-1.f/65536.f,uv=-.5f*.75f/240.f+1.f/65536.f;
    quad(g_vertices+24,top,-.5f/top_width+1.f/65536.f,rv,rv-.75f);
    quad(g_vertices+30,1,-.5f/320.f+1.f/65536.f,rv,rv-.75f);
    quad(g_vertices+36,top,-.5f/top_width+1.f/65536.f,uv,uv+.75f);
    quad(g_vertices+42,1,-.5f/320.f+1.f/65536.f,uv,uv+.75f);
    // Sky bars: x -1..-0.8 (LCD 0..40) and 0.8..1 (360..400), each half of
    // the 64-column sky texture (sharp: its 128x384 doubled copy; nearest:
    // the CPU texture directly, DS top row at v=0).
    auto bar=[&](V3dsVert* out,float x0,float x1,float u0,float u1,float vt,float vb){
        const V3dsVert q[4]={{x0,1,0,1,1,1,1,1,u0,vt},{x1,1,0,1,1,1,1,1,u1,vt},{x1,-1,0,1,1,1,1,1,u1,vb},{x0,-1,0,1,1,1,1,1,u0,vb}};
        const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)out[i]=q[indices[i]];
    };
    bar(g_vertices+48,-1.f,-.8f,0.f,.5f,.75f,0.f);
    bar(g_vertices+54,.8f,1.f,.5f,1.f,.75f,0.f);
    bar(g_vertices+60,-1.f,-.8f,-.25f/40.f+1.f/65536.f,.5f-.25f/40.f+1.f/65536.f,uv,uv+.75f);
    bar(g_vertices+66,.8f,1.f,.5f-.25f/40.f+1.f/65536.f,1.f-.25f/40.f+1.f/65536.f,uv,uv+.75f);
    // Sliced sharp path (72..215): pass 1 DS rows 32k..32k+32 of the resident
    // (top v=0.75) or CPU-upload (top v=0) source, each filling its whole
    // target; pass 2 LCD rows 40k..40k+40 of the upper or lower panel.
    auto box=[&](V3dsVert* out,float x0,float x1,float y0,float y1,float vt,float vb){
        const V3dsVert q[4]={{x0,y0,0,1,1,1,1,1,0,vt},{x1,y0,0,1,1,1,1,1,1,vt},{x1,y1,0,1,1,1,1,1,1,vb},{x0,y1,0,1,1,1,1,1,0,vb}};
        const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)out[i]=q[indices[i]];
    };
    for(unsigned k=0;k<kSlices;++k){
        const float y0=1.f-k/3.f,y1=1.f-(k+1)/3.f;
        box(g_vertices+72+6*k,-1,1,1,-1,.75f-.125f*k,.75f-.125f*(k+1));
        box(g_vertices+108+6*k,-1,1,1,-1,.125f*k,.125f*(k+1));
        box(g_vertices+144+6*k,-top,top,y0,y1,1.f,0.f);
        box(g_vertices+180+6*k,-1,1,y0,y1,1.f,0.f);
    }
    GSPGPU_FlushDataCache(g_vertices,216*sizeof(V3dsVert));
    std::printf("Screen layout resources ready (sharp=%u) VRAM free=%u\n",unsigned(g_sharp_ok),unsigned(vramSpaceFree()));
    g_failed=false;g_ready=true;
    return true;
}
// Full-width sky behind the 4:3 upper image. The DS upper screen's full-screen
// background BG layer (the menu's dithered gradient sky) wraps every 256
// pixels, so the 40-pixel LCD bars show its continuation: DS x -32..-1 is BG
// x 224..255 and DS x 256..287 is BG x 0..31. Only that layer is used (taken
// from the BG candidate planes, never from the final image), with the same
// brightness/master effects, so logo, text, arrows, sparkles and the star
// (other BG layers and OBJs) are never repeated or stretched. When no single
// BG layer (or the backdrop) covers every source pixel, or windows/capture
// views/affine layers are active, the bars stay black. SM64DS_TOP_SKY=0
// restores the black bars.
static bool sky_option(){
    static const bool value=[](){const char* v=std::getenv("SM64DS_TOP_SKY");return !(v&&std::strcmp(v,"0")==0);}();
    return value;
}
static C3D_Tex g_ext_tex,g_ext2_tex;
static C3D_RenderTarget* g_ext2_target;
static bool g_ext_ready,g_ext_failed,g_ext_valid,g_ext_have_key,g_ext_up,g_ext_master_up,g_ext2_ok;
static unsigned g_ext_factor,g_ext_master_factor;
struct ExtKey{const void* identity;uint64_t generation;const void* front;uint8_t layer;};
static ExtKey g_ext_key;
static bool ext_init(){
    if(g_ext_ready)return true;
    if(g_ext_failed)return false;
    g_ext_failed=true;
    if(!C3D_TexInit(&g_ext_tex,64,256,GPU_RGBA8))return false;
    C3D_TexSetFilter(&g_ext_tex,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&g_ext_tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    // Doubled copy for sharp sampling: optional (VRAM only); without it the
    // bars sample the CPU texture with nearest filtering.
    g_ext2_ok=false;
    if(C3D_TexInitVRAM(&g_ext2_tex,128,512,GPU_RGBA8)){
        C3D_TexSetFilter(&g_ext2_tex,GPU_LINEAR,GPU_LINEAR);
        C3D_TexSetWrap(&g_ext2_tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        g_ext2_target=C3D_RenderTargetCreateFromTex(&g_ext2_tex,GPU_TEXFACE_2D,0,-1);
        if(g_ext2_target)g_ext2_ok=true;else C3D_TexDelete(&g_ext2_tex);
    }
    g_ext_failed=false;g_ext_ready=true;
    return true;
}
static unsigned ext_brighten(unsigned q,unsigned factor,bool up,bool master){
    factor=factor>16?16:factor;
    const unsigned bias=master?(up?0:15):(up?8:7);
    return up?q+((63-q)*factor+bias)/16:q-(q*factor+bias)/16;
}
// Game/presentation worker, after the frame's FrameBegin (previous GPU reads
// of the texture have drained). CPU work only when the planes or the effect
// registers change (fades), 64x192 pixels.
static void prepare_extension(const ntr::PpuPreparedCandidates& c,bool allowed){
    g_ext_valid=false;
    if(!allowed || !ds_layout() || !sky_option()){g_ext_have_key=false;return;}
    const auto& st=c.state;
    const uint8_t* fl=c.front_layer();const uint8_t* bl=c.back_layer();
    if(!fl || !bl || st.has_windows || st.blank || c.gpu_affine){g_ext_have_key=false;return;}
    const unsigned mode=(st.blend>>6)&3;
    if(mode==1 && (st.blend&0x3f) && (st.blend&0x3f00)){g_ext_have_key=false;return;}
    // The layer present (front or back) at every source pixel; decided on
    // every 8th row, then verified for every pixel while copying.
    static uint8_t source_x[64];static uint16_t dst_index[192*64];static bool indexed=false;
    if(!indexed){
        for(unsigned i=0;i<64;++i)source_x[i]=uint8_t(i<32?224+i:i-32);
        for(unsigned y=0;y<192;++y)for(unsigned i=0;i<64;++i){
            const unsigned py=255-y,tx=i&7,ty=py&7;
            const unsigned m=(tx&1)|((ty&1)<<1)|((tx&2)<<1)|((ty&2)<<2)|((tx&4)<<2)|((ty&4)<<3);
            dst_index[y*64+i]=uint16_t(((py>>3)*8+(i>>3))*64+m);
        }
        indexed=true;
    }
    const unsigned master_mode=st.master>>14;
    auto set_effects=[&](unsigned layer){
        const bool effect=(mode==2 || mode==3) && (st.blend&(1u<<layer));
        g_ext_up=mode==2;g_ext_factor=effect?std::min(16u,unsigned(st.brightness)):0;
        g_ext_master_up=master_mode==1;g_ext_master_factor=(master_mode==1 || master_mode==2)?std::min(16u,unsigned(st.master&31)):0;
    };
    // Effects are applied by the GPU (see bar_env), so the texture only follows
    // the raw layer pixels. The planes' generation also changes when other
    // layers (logo, text) move; then one sparse pass (every 16th row) checks
    // that the previous layer still covers the strips with unchanged RGB.
    // minigames-30fps-min: every source row is kept and compared (the 1/16
    // sparse check reused stale strips: history-dependent margins, ON != OFF).
    static uint32_t samples[192*64];
    if(g_ext_have_key && c.background_identity && c.background_generation){
        const unsigned layer=g_ext_key.layer;bool same=true;
        // Planes are double-buffered: identity alternates between two snapshots.
        if(g_ext_key.identity!=c.background_identity || g_ext_key.generation!=c.background_generation || g_ext_key.front!=&c.front.px[0][0]){
            const uint32_t* sample=samples;
            for(unsigned y=0;y<192 && same;++y){
                const uint8_t* f=fl+y*256;const uint8_t* b=bl+y*256;
                const uint32_t* front=&c.front.px[y][0];const uint32_t* back=&c.back.px[y][0];
                for(unsigned i=0;i<64;++i){
                    const unsigned x=source_x[i];
                    // RGB only: the plane alpha encodes front/back roles.
                    const unsigned fx=f[x]&7,bx=b[x]&7;
                    if(fx!=layer && bx!=layer){same=false;break;}
                    if(((fx==layer?front[x]:back[x])&0xffffffu)!=*sample++){same=false;break;}
                }
            }
        }
        if(same){g_ext_key.identity=c.background_identity;g_ext_key.generation=c.background_generation;g_ext_key.front=&c.front.px[0][0];set_effects(layer);g_ext_valid=true;return;}
    }
    // Full path: the layer present (front or back) at every source pixel,
    // decided on every 8th row and verified for every pixel while copying.
    unsigned present=0x3f;
    for(unsigned y=0;y<192;y+=8){
        const uint8_t* f=fl+y*256;const uint8_t* b=bl+y*256;
        for(unsigned i=0;i<64;++i){const unsigned x=source_x[i];present&=(1u<<(f[x]&7))|(1u<<(b[x]&7));}
    }
    unsigned layer=8;
    for(unsigned l=0;l<4;++l)if(present&(1u<<l)){layer=l;break;}
    if(layer==8 && (present&(1u<<5)))layer=5;
    if(layer==8 || !ext_init()){g_ext_have_key=false;return;}
    set_effects(layer);
    ExtKey key;std::memset(&key,0,sizeof key);
    key.identity=c.background_identity;key.generation=c.background_generation;key.front=&c.front.px[0][0];key.layer=uint8_t(layer);
    g_ext_have_key=false;
    auto* dst=static_cast<uint32_t*>(g_ext_tex.data);
    const uint16_t* index=dst_index;uint32_t* sample=samples;
    for(unsigned y=0;y<192;++y){
        const uint8_t* f=fl+y*256;const uint8_t* b=bl+y*256;
        const uint32_t* front=&c.front.px[y][0];const uint32_t* back=&c.back.px[y][0];
        for(unsigned i=0;i<64;++i,++index){
            const unsigned x=source_x[i];uint32_t color;
            if((f[x]&7)==layer)color=front[x];
            else if((b[x]&7)==layer)color=back[x];
            else return; // not a full-coverage layer after all: black bars
            *sample++=color&0xffffffu;
            dst[*index]=(color<<8)|0xffu;
        }
    }
    C3D_TexFlush(&g_ext_tex);
    g_ext_key=key;g_ext_have_key=true;g_ext_valid=true;
}
// DS brightness up/down (layer effect, then master) as combiner lerps toward
// white/black: within one six-bit level of the composite's exact tables.
static void bar_env(bool on){
    for(unsigned stage=1;stage<3;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    if(!on)return;
    auto lerp=[](unsigned stage,unsigned factor,bool up){
        if(!factor)return;
        auto* env=C3D_GetTexEnv(stage);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_CONSTANT);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvColor(env,((factor*255+8)/16)<<24|(up?0x00ffffffu:0u));
    };
    lerp(1,g_ext_factor,g_ext_up);
    lerp(2,g_ext_master_factor,g_ext_master_up);
}
// Sky bars for one upper-screen panel. Same sharp/nearest sampling as the
// 4:3 image; vertices 48.. (see init).
static void draw_extension(){
    if(sharp()){
        C3D_FrameDrawOn(g_ext2_target);C3D_SetViewport(0,0,128,384);
        set_fix(1,0,1,false);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,1.f,0.f,0.f,0.f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,1.f,0.f,0.f);
        C3D_TexBind(0,&g_ext_tex);C3D_DrawArrays(GPU_TRIANGLES,6,6);
    }
}
// ---- Menu stereo (menu-stereo) -----------------------------------------------
// In the title/menu scene the upper LCD shows engine B's purely 2D image (the
// 3D star/face is drawn by engine A on the lower LCD, which is never
// stereoscopic), so the geometry parallax of gpu_frame has nothing to move.
// With the 3D slider up that 2D image is shown as depth planes, each drawn
// once per eye with its own horizontal disparity (no doubled pixels), back to
// front:
//   * sky: the full-coverage BG layer that the sky bars already extend
//     (BG2 on the title), deepest;
//   * affine OBJ behind the logo (the flying star and its trail on the back
//     half of their loop, see layer_sprite_add);
//   * BG: every other BG layer (the SUPER MARIO 64 DS logo, BG3), slightly
//     behind the screen;
//   * affine OBJ in front of the logo (star/trail on the front half, the
//     logo sparkles), popping out;
//   * other OBJ (TOUCH TO START, arrows) at the screen plane.
// CPU work only when the BG planes change (identity/generation): the sky
// layer for all 256 columns and the other BG layers' front pixels (alpha 0
// elsewhere), both from the BG-only candidate planes (OBJ are not in them, so
// the logo under a sparkle is complete). The panel is composed once (BG+OBJ,
// mono composition); per frame its alpha marks the OBJ pixels outside the
// affine-OBJ boxes, and panel 1 (unused: no second compose) holds the same
// colours with the affine-OBJ pixels marked. Per eye: sky shifted (the
// texture repeats every 256 DS pixels like the BG, so the bars and edges need
// no fill), then each plane through the sharp 2x intermediate, alpha-tested.
// Brightness/fade effects on the sky and BG planes use the sky-bar combiner
// lerps (within one six-bit level); OBJ colours are the composed ones.
// Slider 0 never enters this path (mono output unchanged). No VRAM is
// allocated (linear textures only); allocation failure keeps the flat image.
// SM64DS_MENU_STEREO=0 disables it. SM64DS_MENU_STEREO_DEPTH=
// "sky,bg,obj,starFront,starBack": total disparity in LCD pixels at full
// slider (positive = behind the screen), default "12,4,0,10,7" (the star pops
// out to -6 in front of the logo and recedes to +11 behind it).
static bool layer_option(){
    static const bool value=[](){const char* v=std::getenv("SM64DS_MENU_STEREO");return !(v&&std::strcmp(v,"0")==0);}();
    return value;
}
struct LayerDepths{float sky=12.f,bg=4.f,obj=0.f,star_front=10.f,star_back=7.f;};
static const LayerDepths& layer_depths(){
    static const LayerDepths value=[](){
        LayerDepths d;
        if(const char* v=std::getenv("SM64DS_MENU_STEREO_DEPTH")){
            float x[5]={d.sky,d.bg,d.obj,d.star_front,d.star_back};
            for(unsigned i=0;i<5 && *v;++i){char* end=nullptr;const float f=std::strtof(v,&end);if(end==v)break;
                x[i]=f<-16.f?-16.f:f>24.f?24.f:f;v=*end==','?end+1:end;}
            d.sky=x[0];d.bg=x[1];d.obj=x[2];d.star_front=x[3];d.star_back=x[4];
        }
        return d;
    }();
    return value;
}
// Affine OBJ (the title star, its trail and the logo sparkles) get their own
// depth. The game flies the star in a vertical loop around the logo: down in
// front of it (OBJ priority <= the logo BG priority), back up behind it
// (priority 3 > 2), switching at the top and bottom of the loop. Its depth
// follows that loop: s = sqrt(1-(dy/h)^2), dy = sprite centre - centre of the
// logo rows, h = half the logo height (0 above/below the logo, where the star
// switches sides, 1 halfway), disparity = bg - star_front*s in front,
// bg + star_back*s behind, so it is continuous at the switch and always
// consistent with what the logo occludes. Each trail sprite keeps the
// priority and position it was emitted with, so the trail curves in depth.
struct LayerSprite{int x0,y0,x1,y1;float depth;bool front;};
constexpr unsigned kLayerSprites=128;
static LayerSprite g_layer_sprites[kLayerSprites];
static unsigned g_layer_sprite_count,g_layer_front_sprites,g_layer_back_sprites;
static unsigned g_layer_bg_priority=4; // frontmost priority of the drawn BG layers (sampled)
static float g_layer_fg_top=0.f,g_layer_fg_bottom=192.f; // rows spanned by the drawn BG layers (the logo)
static C3D_Tex g_layer_sky,g_layer_fg;
// 0 panel alpha clear, 6 OBJ mark (panel space); 12/18 sky per eye; 24/60 BG
// slices per eye; 96/132 OBJ slices per eye; 168/174 BG nearest; 180/186 OBJ nearest.
constexpr unsigned kLayerVertices=192;
static V3dsVert* g_layer_vertices;
static bool g_layer_ready,g_layer_failed,g_layer_have_key,g_layer_frame;
static ExtKey g_layer_key;
static float g_layer_slider=-1.f,g_layer_frame_slider=0.f;
static unsigned g_layer_fg_mask; // BG layers drawn in the BG plane (bits 0-3)
static unsigned g_fg_factor,g_fg_master_factor;static bool g_fg_up,g_fg_master_up;
static uint64_t g_layer_builds,g_layer_sky_builds,g_layer_frames,g_layer_build_ticks;
static bool g_layer_sky_valid,g_layer_fg_valid;
static uint64_t g_layer_fg_builds,g_layer_flush_ticks;
static bool layers_init(){
    if(g_layer_ready)return true;
    if(g_layer_failed)return false;
    g_layer_failed=true;
    if(!g_layer_vertices)g_layer_vertices=static_cast<V3dsVert*>(linearAlloc(kLayerVertices*sizeof(V3dsVert)));
    if(!g_layer_vertices || !C3D_TexInit(&g_layer_sky,256,256,GPU_RGBA8)){
        std::printf("Stereo menu layers unavailable (linear free=%u): flat upper screen\n",unsigned(linearSpaceFree()));return false;
    }
    if(!C3D_TexInit(&g_layer_fg,256,256,GPU_RGBA8)){
        C3D_TexDelete(&g_layer_sky);g_layer_sky={};
        std::printf("Stereo menu BG plane unavailable (linear free=%u): flat upper screen\n",unsigned(linearSpaceFree()));return false;
    }
    C3D_TexSetFilter(&g_layer_sky,GPU_LINEAR,GPU_LINEAR);
    C3D_TexSetWrap(&g_layer_sky,GPU_REPEAT,GPU_CLAMP_TO_EDGE);
    C3D_TexSetFilter(&g_layer_fg,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(&g_layer_fg,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    // Panel-space quads (identity fix, 256x192 viewport as Composite::setup);
    // the OBJ layer is resident (DS top row at v=0.75).
    auto quad=[](V3dsVert* out,float v_top,float v_bottom){
        const V3dsVert q[4]={{-1,1,0,1,1,1,1,1,0,v_top},{1,1,0,1,1,1,1,1,1,v_top},
            {1,-1,0,1,1,1,1,1,1,v_bottom},{-1,-1,0,1,1,1,1,1,0,v_bottom}};
        const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)out[i]=q[indices[i]];
    };
    quad(g_layer_vertices+0,0.f,.75f);
    quad(g_layer_vertices+6,.75f,0.f);
    GSPGPU_FlushDataCache(g_layer_vertices,12*sizeof(V3dsVert));
    g_layer_slider=-1.f;g_layer_have_key=false;g_layer_sky_valid=false;g_layer_fg_valid=false;
    g_layer_failed=false;g_layer_ready=true;
    std::printf("Stereo menu layers ready (depth sky=%.1f bg=%.1f obj=%.1f) linear free=%u\n",
        layer_depths().sky,layer_depths().bg,layer_depths().obj,unsigned(linearSpaceFree()));
    return true;
}
// Worker, right after prepare_extension (same frame, after FrameBegin: the
// previous frame no longer samples the textures). Decides whether this frame's
// upper panel is presented as layered stereo.
static void prepare_layers(const ntr::PpuPreparedCandidates& c,float slider,bool objects){
    g_layer_frame=false;
    if(!(slider>0.f) || !layer_option() || !g_ext_valid || c.state.has_geometry || !c.background_identity)return;
    const uint8_t* fl=c.front_layer();const uint8_t* bl=c.back_layer();
    if(!fl || !bl)return;
    const unsigned layer=g_ext_key.layer;
    if(!objects){
        // Only the sky (e.g. the black title backdrop before the logo): no
        // depth to show, keep the flat image and do no work.
        bool any=false;
        for(unsigned i=0;!any && i<24*32;++i){const unsigned fx=fl[(i>>5)*8*256+3*256+(i&31)*8]&7;any=fx!=layer && fx<4;}
        if(!any)return;
    }
    if(!layers_init())return;
    const bool same=g_layer_have_key && g_layer_key.identity==c.background_identity &&
        g_layer_key.generation==c.background_generation && g_layer_key.front==&c.front.px[0][0] && g_layer_key.layer==layer;
    {   // Frontmost priority among the drawn BG layers (768 samples): OBJ at
        // or below it are in front of the logo.
        const uint8_t* fp=c.front_priority();unsigned best=4;
        if(fp)for(unsigned i=0;i<24*32;++i){const unsigned s=(i>>5)*8*256+3*256+(i&31)*8;
            const unsigned fx=fl[s]&7;if(fx!=layer && fx<4 && fp[s]<best)best=fp[s];}
        g_layer_bg_priority=best;
    }
    if(!same){
        const uint64_t build_start=svcGetSystemTick();
        const uint32_t* front=&c.front.px[0][0];const uint32_t* back=&c.back.px[0][0];
        // Sky: rebuilt only when it changed. Typically only the logo moves
        // (title transition): 768 samples of the visible sky are compared
        // with the previous reconstruction first.
        static uint32_t sky_samples[24*32];
        bool sky_same=g_layer_sky_valid && g_layer_key.layer==layer;
        for(unsigned i=0;sky_same && i<24*32;++i){
            const unsigned s=(i>>5)*8*256+3*256+(i&31)*8; // rows 3+8k, every 8th column
            const unsigned fx=fl[s]&7,bx=bl[s]&7;
            if(fx==layer){if(((front[s]<<8)|0xffu)!=sky_samples[i])sky_same=false;}
            else if(bx==layer){if(((back[s]<<8)|0xffu)!=sky_samples[i])sky_same=false;}
        }
        if(!sky_same){
            static uint16_t index[192*256];static bool indexed=false;
            if(!indexed){ // tiled layout of g_ext_tex / CPU uploads: DS row y at texture row 255-y
                for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
                    const unsigned py=255-y,tx=x&7,ty=py&7;
                    const unsigned m=(tx&1)|((ty&1)<<1)|((tx&2)<<1)|((ty&2)<<2)|((tx&4)<<2)|((ty&4)<<3);
                    index[y*256+x]=uint16_t(((py>>3)*32+(x>>3))*64+m);
                }
                indexed=true;
            }
            auto* sky=static_cast<uint32_t*>(g_layer_sky.data);
            const uint16_t* at=index;
            for(unsigned y=0;y<192;++y,at+=256){
                const uint8_t* f=fl+y*256;const uint8_t* b=bl+y*256;
                const uint32_t* fr=front+y*256;const uint32_t* bk=back+y*256;
                // Sky hidden behind two other layers: repeat the nearest known
                // column (the sky gradient is smooth along a row).
                uint32_t last=0;bool known=false;
                for(unsigned x=0;x<256;++x){
                    const unsigned fx=f[x]&7,bx=b[x]&7;
                    if(fx==layer){last=fr[x];known=true;}
                    else if(bx==layer){last=bk[x];known=true;}
                    else if(!known){for(unsigned k=x+1;k<256;++k){
                        if((f[k]&7)==layer){last=fr[k];break;}if((b[k]&7)==layer){last=bk[k];break;}}known=true;}
                    sky[at[x]]=(last<<8)|0xffu;
                }
            }
            for(unsigned i=0;i<24*32;++i){const unsigned s=(i>>5)*8*256+3*256+(i&31)*8;sky_samples[i]=sky[index[s]];}
            C3D_TexFlush(&g_layer_sky);g_layer_sky_valid=true;++g_layer_sky_builds;
        }
        g_layer_build_ticks+=svcGetSystemTick()-build_start;
        g_layer_key={c.background_identity,c.background_generation,&c.front.px[0][0],uint8_t(layer)};
        g_layer_have_key=true;++g_layer_builds;
    }
    if(!same){
        const uint64_t build_start=svcGetSystemTick();
        const uint32_t* front=&c.front.px[0][0];
        // BG plane: every other BG layer's front pixel, alpha 0 elsewhere.
        // Written in texture (tile/Morton) order: sequential stores.
        static uint16_t tile_source[64];static bool tiled=false;
        if(!tiled){
            for(unsigned k=0;k<64;++k){
                const unsigned tx=(k&1)|((k>>1)&2)|((k>>2)&4),ty=((k>>1)&1)|((k>>2)&2)|((k>>3)&4);
                tile_source[k]=uint16_t((7-ty)*256+tx); // texture row 255-y: tile row ty is DS row 7-ty
            }
            tiled=true;
        }
        // Tiles showing only the sky (most of the screen) are skipped when
        // they were already empty; only the changed tile rows are flushed.
        static uint8_t tile_used[24*32];
        if(!g_layer_fg_valid)std::memset(tile_used,1,sizeof tile_used);
        auto* fg=static_cast<uint32_t*>(g_layer_fg.data);unsigned fg_mask=0,lo=24,hi=0,fg_top=24,fg_bottom=0;
        const uint32_t sky_word=layer*0x01010101u;
        for(unsigned ty=0;ty<24;++ty)for(unsigned tx=0;tx<32;++tx){
            const unsigned base=ty*8*256+tx*8;const uint8_t* f=fl+base;
            bool empty=true;
            for(unsigned yy=0;yy<8 && empty;++yy){
                uint32_t w[2];std::memcpy(w,f+yy*256,8);
                empty=((w[0]&0x07070707u)==sky_word) && ((w[1]&0x07070707u)==sky_word);
            }
            uint8_t& used=tile_used[ty*32+tx];
            uint32_t* out=fg+((31-ty)*32+tx)*64;
            if(empty){
                if(used){std::memset(out,0,64*4);used=0;lo=std::min(lo,ty);hi=std::max(hi,ty+1);}
                continue;
            }
            const uint32_t* fr=front+base;
            fg_top=std::min(fg_top,ty);fg_bottom=std::max(fg_bottom,ty+1);
            for(unsigned k=0;k<64;++k){
                const unsigned s=tile_source[k],fx=f[s]&7;
                if(fx!=layer && fx<4){out[k]=(fr[s]<<8)|0xffu;fg_mask|=1u<<fx;}
                else out[k]=0;
            }
            used=1;lo=std::min(lo,ty);hi=std::max(hi,ty+1);
        }
        // Tile row ty sits at texture tile row 31-ty (8 KiB each).
        const uint64_t flush_start=svcGetSystemTick();
        if(lo<hi)GSPGPU_FlushDataCache(fg+(32-hi)*32*64,(hi-lo)*32*64*4);
        g_layer_flush_ticks+=svcGetSystemTick()-flush_start;
        g_layer_fg_valid=true;
        g_layer_fg_top=fg_top<fg_bottom?float(fg_top*8):0.f;g_layer_fg_bottom=fg_top<fg_bottom?float(fg_bottom*8):192.f;
        g_layer_fg_mask=fg_mask;++g_layer_fg_builds;
        g_layer_build_ticks+=svcGetSystemTick()-build_start;
    }
    // BG-plane effects: the sky-bar rule (prepare_extension set_effects)
    // applied to the drawn BG layers.
    const auto& st=c.state;const unsigned mode=(st.blend>>6)&3,master_mode=st.master>>14;
    const bool effect=(mode==2 || mode==3) && (st.blend&g_layer_fg_mask);
    g_fg_up=mode==2;g_fg_factor=effect?std::min(16u,unsigned(st.brightness)):0;
    g_fg_master_up=master_mode==1;g_fg_master_factor=(master_mode==1 || master_mode==2)?std::min(16u,unsigned(st.master&31)):0;
    g_layer_frame=true;
    if(++g_layer_frames%300==1)std::printf("Stereo menu layers frames=%llu planeChanges=%llu skyBuilds=%llu cpuPlaneBuilds=%llu buildMsTotal=%.3f flushMsTotal=%.3f sky=%u bg=%x slider=%.2f\n",
        (unsigned long long)g_layer_frames,(unsigned long long)g_layer_builds,(unsigned long long)g_layer_sky_builds,(unsigned long long)g_layer_fg_builds,
        double(g_layer_build_ticks)/double(SYSCLOCK_ARM11/1000),double(g_layer_flush_ticks)/double(SYSCLOCK_ARM11/1000),layer,g_layer_fg_mask,slider);
    g_layer_frame_slider=slider;
}
static void layer_buffer(V3dsVert* vertices){
    // Always a fresh C3D_GetBufInfo: it marks the state dirty, and a draw
    // consumes that flag (reusing an old pointer would keep the previous buffer).
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,vertices,sizeof(V3dsVert),3,0x210);
}
// Per-frame quads: sprite marks (panel space) and per-eye sprite slices.
constexpr unsigned kLayerDynamic=6*kLayerSprites*(2+2*2*2);
static V3dsVert* g_layer_dynamic;
static unsigned g_layer_mark_first,g_layer_eye_count[2][2]; // [eye][front]
static unsigned g_layer_slice_first[2][2][kSlices],g_layer_slice_count[2][2][kSlices]; // per slice: one bind + one draw
// Game side of the frame (presentation worker): the upper engine's OBJ plan.
void layer_sprites_begin(){g_layer_sprite_count=g_layer_front_sprites=g_layer_back_sprites=0;}
void layer_sprite_add(int x,int y,int width,int height,unsigned priority){
    if(!g_layer_frame || g_layer_sprite_count==kLayerSprites)return;
    const int x0=std::max(x,0),y0=std::max(y,0),x1=std::min(x+width,256),y1=std::min(y+height,192);
    if(x0>=x1 || y0>=y1)return;
    const auto& d=layer_depths();
    // Loop around the logo's rows: s=0 above/below the logo (where the star
    // switches sides), 1 at its middle.
    const float half=std::max(8.f,.5f*(g_layer_fg_bottom-g_layer_fg_top));
    const float yc=(y+height*.5f-.5f*(g_layer_fg_top+g_layer_fg_bottom))/half,s=std::sqrt(std::max(0.f,1.f-yc*yc));
    const bool front=priority<=g_layer_bg_priority;
    g_layer_sprites[g_layer_sprite_count++]={x0,y0,x1,y1,front?d.bg-d.star_front*s:d.bg+d.star_back*s,front};
    ++(front?g_layer_front_sprites:g_layer_back_sprites);
}
// Inside the frame, after the upper panel is composed (BG + OBJ merge) and
// with the pass-through program bound. Panel 0 alpha: 255 on OBJ pixels
// outside the depth sprites, 0 elsewhere. Panel 1 (free: the right eye
// reuses panel 0): panel 0's colours, alpha 255 on OBJ pixels inside the
// depth sprites. objects: the upper engine's OBJ layer drawn this frame.
static void mark_layers(ds_gpu::Surface& panel,ds_gpu::Surface& sprites,C3D_Tex* objects){
    if(!g_layer_frame)return;
    if(objects && !g_layer_dynamic)g_layer_dynamic=static_cast<V3dsVert*>(linearAlloc(kLayerDynamic*sizeof(V3dsVert)));
    if(!objects || !g_layer_dynamic)g_layer_sprite_count=g_layer_front_sprites=g_layer_back_sprites=0;
    // Panel-space sprite boxes: position, constant uv; then OBJ-layer uv (resident).
    g_layer_mark_first=0;
    for(unsigned i=0;i<g_layer_sprite_count;++i){
        const auto& r=g_layer_sprites[i];
        const float X0=r.x0/128.f-1,X1=r.x1/128.f-1,Y0=1-r.y0/96.f,Y1=1-r.y1/96.f;
        const float u0=r.x0/256.f,u1=r.x1/256.f,v0=.75f-r.y0/256.f,v1=.75f-r.y1/256.f;
        const V3dsVert q[4]={{X0,Y0,0,1,1,1,1,1,u0,v0},{X1,Y0,0,1,1,1,1,1,u1,v0},{X1,Y1,0,1,1,1,1,1,u1,v1},{X0,Y1,0,1,1,1,1,1,u0,v1}};
        const unsigned indices[]={0,1,2,0,2,3};
        for(unsigned k=0;k<6;++k)g_layer_dynamic[i*6+k]=q[indices[k]];
    }
    auto pass_env=[](bool constant_alpha){
        for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
        auto* env=C3D_GetTexEnv(0);
        if(constant_alpha){
            C3D_TexEnvSrc(env,C3D_RGB,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvSrc(env,C3D_Alpha,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);
            C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);C3D_TexEnvColor(env,0);
        }else{ // OBJ alpha 16*(priority+1) (0 = hole), scaled to 255 by two x4 stages
            C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);C3D_TexEnvScale(env,C3D_Alpha,GPU_TEVSCALE_4);
            auto* next=C3D_GetTexEnv(1);
            C3D_TexEnvSrc(next,C3D_Both,GPU_PREVIOUS,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(next,C3D_Both,GPU_REPLACE);C3D_TexEnvScale(next,C3D_Alpha,GPU_TEVSCALE_4);
        }
        C3D_AlphaTest(!constant_alpha,GPU_GREATER,0);
    };
    set_fix(1,0,1,false);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,1.f,0.f,0.f,0.f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,1.f,0.f,0.f);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    if(g_layer_sprite_count){
        GSPGPU_FlushDataCache(g_layer_dynamic,g_layer_sprite_count*6*sizeof(V3dsVert));
        // Panel 1 = panel 0 colours with alpha 0, then OBJ alpha inside the boxes.
        C3D_FrameDrawOn(sprites.target);C3D_SetViewport(0,0,256,192);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
        layer_buffer(g_layer_vertices);pass_env(true);
        C3D_TexBind(0,&panel.texture);C3D_DrawArrays(GPU_TRIANGLES,6,6);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALPHA);
        layer_buffer(g_layer_dynamic);pass_env(false);
        C3D_TexBind(0,objects);C3D_DrawArrays(GPU_TRIANGLES,0,6*g_layer_sprite_count);
    }
    // Panel 0: alpha 0, OBJ alpha 255, then alpha 0 again inside the boxes.
    // Constant-alpha passes bind the sky texture (RGB is write-masked): a
    // texture must never be sampled while it is the render target (real PICA).
    C3D_FrameDrawOn(panel.target);C3D_SetViewport(0,0,256,192);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALPHA);
    layer_buffer(g_layer_vertices);pass_env(true);
    C3D_TexBind(0,&g_layer_sky);C3D_DrawArrays(GPU_TRIANGLES,0,6);
    if(objects){
        pass_env(false);C3D_TexBind(0,objects);C3D_DrawArrays(GPU_TRIANGLES,6,6);
        if(g_layer_sprite_count){layer_buffer(g_layer_dynamic);pass_env(true);C3D_DrawArrays(GPU_TRIANGLES,0,6*g_layer_sprite_count);}
    }
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* env=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
}
// DS brightness lerps on stages 1-2 (bar_env with explicit factors).
static void lerp_env(unsigned factor,bool up,unsigned master_factor,bool master_up){
    for(unsigned stage=1;stage<3;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto lerp=[](unsigned stage,unsigned f,bool u){
        if(!f)return;
        auto* env=C3D_GetTexEnv(stage);
        C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_CONSTANT);
        C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
        C3D_TexEnvColor(env,((f*255+8)/16)<<24|(u?0x00ffffffu:0u));
    };
    lerp(1,factor,up);lerp(2,master_factor,master_up);
}
// Shifted copies of the LCD quads for the current slider (vertices 12..191).
static void layer_vertices(float slider){
    if(slider==g_layer_slider)return;
    const auto& d=layer_depths();
    auto copy=[](unsigned dst,unsigned src,unsigned n,float shift){
        for(unsigned i=0;i<n;++i){V3dsVert v=g_vertices[src+i];v.x+=shift*(2.f/400.f);g_layer_vertices[dst+i]=v;}
    };
    for(unsigned eye=0;eye<2;++eye){
        // Content displacement in LCD pixels: left eye -D/2, right eye +D/2
        // (positive disparity = behind the screen).
        const float sign=eye?.5f*slider:-.5f*slider;
        const float du=-sign*d.sky/1.25f/256.f; // 4:3 image: 320 LCD pixels = 256 DS pixels = u 1
        const V3dsVert q[4]={{-1,1,0,1,1,1,1,1,-.125f+du,0},{1,1,0,1,1,1,1,1,1.125f+du,0},
            {1,-1,0,1,1,1,1,1,1.125f+du,.75f},{-1,-1,0,1,1,1,1,1,-.125f+du,.75f}};
        const unsigned indices[]={0,1,2,0,2,3};
        for(unsigned i=0;i<6;++i)g_layer_vertices[12+6*eye+i]=q[indices[i]];
        copy(24+36*eye,144,36,sign*d.bg);
        copy(96+36*eye,144,36,sign*d.obj);
        copy(168+6*eye,36,6,sign*d.bg);
        copy(180+6*eye,24,6,sign*d.obj);
    }
    GSPGPU_FlushDataCache(g_layer_vertices+12,(kLayerVertices-12)*sizeof(V3dsVert));
    g_layer_slider=slider;
}
// Per-eye LCD quads of the depth sprites over the sharp intermediate: each box
// split at the 32-row slice boundaries, displaced by its own disparity, and
// grouped by slice so each pass is one texture bind and one draw per slice.
// sliced=false (nearest presentation, no sharp intermediate: VRAM short, e.g.
// back in the menu after a stage): one quad per box sampling the resident
// sprite panel directly (DS top row at v=0.75), all under slice 0.
static void sprite_quads(float slider,bool sliced){
    unsigned n=6*g_layer_sprite_count;
    for(unsigned eye=0;eye<2;++eye)for(unsigned front=0;front<2;++front){
        const float sign=eye?.5f*slider:-.5f*slider;
        const unsigned first=n;
        if(!sliced){
            for(unsigned k=0;k<kSlices;++k)g_layer_slice_first[eye][front][k]=n,g_layer_slice_count[eye][front][k]=0;
            for(unsigned i=0;i<g_layer_sprite_count;++i){
                const auto& r=g_layer_sprites[i];
                if(r.front!=bool(front) || n+6>kLayerDynamic)continue;
                const float dx=sign*r.depth*(2.f/400.f);
                const float X0=(40.f+1.25f*r.x0)/200.f-1+dx,X1=(40.f+1.25f*r.x1)/200.f-1+dx;
                const float Y0=1-r.y0/96.f,Y1=1-r.y1/96.f,u0=r.x0/256.f,u1=r.x1/256.f,v0=.75f-r.y0/256.f,v1=.75f-r.y1/256.f;
                const V3dsVert q[4]={{X0,Y0,0,1,1,1,1,1,u0,v0},{X1,Y0,0,1,1,1,1,1,u1,v0},{X1,Y1,0,1,1,1,1,1,u1,v1},{X0,Y1,0,1,1,1,1,1,u0,v1}};
                const unsigned indices[]={0,1,2,0,2,3};
                for(unsigned m=0;m<6;++m)g_layer_dynamic[n+m]=q[indices[m]];
                n+=6;
            }
            g_layer_slice_count[eye][front][0]=n-first;g_layer_eye_count[eye][front]=n-first;
            continue;
        }
        for(unsigned k=0;k<kSlices;++k){
            g_layer_slice_first[eye][front][k]=n;
            for(unsigned i=0;i<g_layer_sprite_count;++i){
                const auto& r=g_layer_sprites[i];
                if(r.front!=bool(front) || r.y1<=int(32*k) || r.y0>=int(32*k+32) || n+6>kLayerDynamic)continue;
                const float dx=sign*r.depth*(2.f/400.f);
                const float X0=(40.f+1.25f*r.x0)/200.f-1+dx,X1=(40.f+1.25f*r.x1)/200.f-1+dx;
                const float u0=r.x0/256.f,u1=r.x1/256.f;
                const int a=std::max(r.y0,int(32*k)),b=std::min(r.y1,int(32*k+32));
                const float Y0=1-a/96.f,Y1=1-b/96.f,v0=1-(a-32.f*k)/32.f,v1=1-(b-32.f*k)/32.f;
                const V3dsVert q[4]={{X0,Y0,0,1,1,1,1,1,u0,v0},{X1,Y0,0,1,1,1,1,1,u1,v0},{X1,Y1,0,1,1,1,1,1,u1,v1},{X0,Y1,0,1,1,1,1,1,u0,v1}};
                const unsigned indices[]={0,1,2,0,2,3};
                for(unsigned m=0;m<6;++m)g_layer_dynamic[n+m]=q[indices[m]];
                n+=6;
            }
            g_layer_slice_count[eye][front][k]=n-g_layer_slice_first[eye][front][k];
        }
        g_layer_eye_count[eye][front]=n-first;
    }
    GSPGPU_FlushDataCache(g_layer_dynamic+6*g_layer_sprite_count,(n-6*g_layer_sprite_count)*sizeof(V3dsVert));
}
// Both upper-LCD eyes (targets 0/1) from the layer textures, the marked panel
// and the sprite panel (resident). Caller: draw(), pass-through program
// bound, single-texture replace environment, blending/depth/stencil/alpha off.
// Per eye, back to front: sky, sprites behind the logo, BG (logo), sprites in
// front of it, other OBJ.
static void draw_layered(C3D_RenderTarget* const targets[3],C3D_Tex* panel,C3D_Tex* sprites){
    layer_vertices(g_layer_frame_slider);
    auto identity_fix=[](){
        set_fix(1,0,1,false);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,1.f,0.f,0.f,0.f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,1.f,0.f,0.f);
    };
    auto sky=[&](unsigned eye){
        // No clear: the opaque full-target sky quad (blend ONE/ZERO, no depth
        // test) overwrites every pixel, and each in-frame clear costs two GX
        // queue entries (FrameSplit + MemoryFill).
        C3D_FrameDrawOn(targets[eye]);
        set_fix(1,0,1,false);layer_buffer(g_layer_vertices);
        lerp_env(g_ext_factor,g_ext_up,g_ext_master_factor,g_ext_master_up);
        C3D_TexBind(0,&g_layer_sky);C3D_DrawArrays(GPU_TRIANGLES,12+6*eye,6);
        lerp_env(0,false,0,false);
    };
    // Depth sprites (affine OBJ) live only in the sprite panel: both the
    // sharp and the nearest presentation must draw them.
    auto sprite_pass=[&](unsigned eye,bool front){
        if(!g_layer_eye_count[eye][front])return;
        layer_buffer(g_layer_dynamic);C3D_AlphaTest(true,GPU_GREATER,127);
        for(unsigned k=0;k<kSlices;++k)if(g_layer_slice_count[eye][front][k]){
            C3D_TexBind(0,sharp()?&g_slice_tex[k]:sprites);
            C3D_DrawArrays(GPU_TRIANGLES,g_layer_slice_first[eye][front][k],g_layer_slice_count[eye][front][k]);
        }
        C3D_AlphaTest(false,GPU_ALWAYS,0);
    };
    if(g_layer_sprite_count)sprite_quads(g_layer_frame_slider,sharp());
    else for(auto& e:g_layer_eye_count)e[0]=e[1]=0;
    if(!sharp()){
        // Nearest presentation (no sharp intermediate): same planes, sampled
        // directly from the textures and the resident panels.
        for(unsigned eye=0;eye<2;++eye){
            sky(eye);
            sprite_pass(eye,false);
            layer_buffer(g_layer_vertices);
            C3D_AlphaTest(true,GPU_GREATER,127);
            lerp_env(g_fg_factor,g_fg_up,g_fg_master_factor,g_fg_master_up);
            C3D_TexBind(0,&g_layer_fg);C3D_DrawArrays(GPU_TRIANGLES,168+6*eye,6);
            lerp_env(0,false,0,false);
            C3D_AlphaTest(false,GPU_ALWAYS,0);
            sprite_pass(eye,true);
            layer_buffer(g_layer_vertices);
            C3D_AlphaTest(true,GPU_GREATER,127);
            C3D_TexBind(0,panel);C3D_DrawArrays(GPU_TRIANGLES,180+6*eye,6);
            C3D_AlphaTest(false,GPU_ALWAYS,0);
        }
        layer_buffer(g_vertices);return;
    }
    // Source -> sharp 2x intermediate (6 slices), shared by both eyes.
    auto fill=[&](C3D_Tex* source,unsigned group,bool effects){
        identity_fix();layer_buffer(g_vertices);
        if(effects)lerp_env(g_fg_factor,g_fg_up,g_fg_master_factor,g_fg_master_up);
        C3D_TexBind(0,source);
        for(unsigned k=0;k<kSlices;++k){
            C3D_FrameDrawOn(g_slice_target[k]);C3D_SetViewport(0,0,512,64);
            C3D_DrawArrays(GPU_TRIANGLES,group+6*k,6);
        }
        if(effects)lerp_env(0,false,0,false);
    };
    bool sky_done=false;
    if(g_layer_back_sprites){
        fill(sprites,72,false);
        for(unsigned eye=0;eye<2;++eye){sky(eye);sprite_pass(eye,false);}
        sky_done=true;
    }
    fill(&g_layer_fg,108,true);
    for(unsigned eye=0;eye<2;++eye){
        if(!sky_done)sky(eye);
        else{C3D_FrameDrawOn(targets[eye]);set_fix(1,0,1,false);}
        layer_buffer(g_layer_vertices);C3D_AlphaTest(true,GPU_GREATER,127);
        for(unsigned k=0;k<kSlices;++k){C3D_TexBind(0,&g_slice_tex[k]);C3D_DrawArrays(GPU_TRIANGLES,24+36*eye+6*k,6);}
        C3D_AlphaTest(false,GPU_ALWAYS,0);
    }
    if(g_layer_front_sprites){
        fill(sprites,72,false);
        for(unsigned eye=0;eye<2;++eye){C3D_FrameDrawOn(targets[eye]);set_fix(1,0,1,false);sprite_pass(eye,true);}
    }
    // Composed panel (OBJ pixels outside the sprite boxes carry alpha 255).
    fill(panel,72,false);
    layer_buffer(g_layer_vertices);
    for(unsigned eye=0;eye<2;++eye){
        C3D_FrameDrawOn(targets[eye]);set_fix(1,0,1,false);
        C3D_AlphaTest(true,GPU_GREATER,127);
        for(unsigned k=0;k<kSlices;++k){C3D_TexBind(0,&g_slice_tex[k]);C3D_DrawArrays(GPU_TRIANGLES,96+36*eye+6*k,6);}
        C3D_AlphaTest(false,GPU_ALWAYS,0);
    }
    layer_buffer(g_vertices);
}
static void layers_release(){
    if(g_layer_ready){defer_release(nullptr,&g_layer_sky);defer_release(nullptr,&g_layer_fg);g_layer_ready=false;}
    g_layer_failed=false;g_layer_have_key=false;g_layer_frame=false;g_layer_slider=-1.f;g_layer_sky_valid=false;g_layer_fg_valid=false;
}
// Caller has bound the pass-through program, attributes and single-texture
// replace environment with blending/depth/stencil disabled. resident=true for
// GPU-composed panels (DS top row at v=0.75), false for CPU-uploaded textures.
// Leaving the menu layout: give the VRAM back. Called inside a frame after
// C3D_FrameBegin; the current frame does not use these targets. Deleting a
// render target inside a frame is a citro3d panic (svcBreak), so they are
// queued and freed after this frame's C3D_FrameEnd (defer_release).
static void release(){
    if(g_sharp_ok){
        for(unsigned k=0;k<kSlices;++k){defer_release(g_slice_target[k],&g_slice_tex[k]);g_slice_target[k]=nullptr;}
        g_sharp_ok=false;
    }
    if(g_ext_ready){
        if(g_ext2_ok){defer_release(g_ext2_target,&g_ext2_tex);g_ext2_target=nullptr;g_ext2_ok=false;}
        defer_release(nullptr,&g_ext_tex);g_ext_ready=false;
    }
    g_ext_failed=false;g_ext_have_key=false;g_ext_valid=false;
    layers_release(); // menu-stereo
    g_ready=false;g_failed=false;
    std::printf("Screen layout resources released (deferred to frame end) VRAM free=%u\n",unsigned(vramSpaceFree()));
}
static bool draw(C3D_RenderTarget* const targets[3],C3D_Tex* const sources[3],bool stereo,bool resident){
    const bool layout=ds_layout(),ready=layout && init();
    {   // One line per change: which presentation the LCDs get, and why.
        static int last=-1;const int state=layout?(ready?2:1):0;
        if(state!=last){last=state;std::printf("Screen layout %s (menu=%u ready=%u failed=%u resident=%u)\n",
            state==2?"DS 4:3 + sky":state==1?"STRETCHED (DS layout resources unavailable)":"stretched",
            unsigned(g_menu_presentation.load(std::memory_order_relaxed)),unsigned(g_ready),unsigned(g_failed),unsigned(resident));}
    }
    if(!layout && (g_ready || g_ext_ready))release();
    if(!ready)return false;
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,g_vertices,sizeof(V3dsVert),3,0x210);
    // menu-stereo: layered upper eyes (both from panel 0), then the lower LCD.
    const bool layered=stereo && resident && g_layer_frame;
    if(layered)draw_layered(targets,sources[0],sources[1]);
    for(unsigned panel=layered?2:0;panel<3;++panel){
        if(panel==1 && !stereo)continue;
        const bool lower=panel==2;
        const bool bars=!lower && g_ext_valid;
        if(sharp()){
            // The sky's doubled copy is shared by both eyes: build it once.
            if(bars && panel==0 && g_ext2_ok)draw_extension();
            set_fix(1,0,1,false);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+0,1.f,0.f,0.f,0.f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,1.f,0.f,0.f);
            C3D_TexBind(0,sources[panel]);
            for(unsigned k=0;k<kSlices;++k){
                C3D_FrameDrawOn(g_slice_target[k]);C3D_SetViewport(0,0,512,64);
                C3D_DrawArrays(GPU_TRIANGLES,(resident?72:108)+6*k,6);
            }
            C3D_RenderTargetClear(targets[panel],C3D_CLEAR_ALL,0,0);C3D_FrameDrawOn(targets[panel]);
            set_fix(1,0,1,false);
            for(unsigned k=0;k<kSlices;++k){C3D_TexBind(0,&g_slice_tex[k]);C3D_DrawArrays(GPU_TRIANGLES,(lower?180:144)+6*k,6);}
            if(bars){
                bar_env(true);
                if(g_ext2_ok){C3D_TexBind(0,&g_ext2_tex);C3D_DrawArrays(GPU_TRIANGLES,48,12);}
                else{C3D_TexBind(0,&g_ext_tex);C3D_DrawArrays(GPU_TRIANGLES,60,12);}
                bar_env(false);
            }
        }else{
            C3D_RenderTargetClear(targets[panel],C3D_CLEAR_ALL,0,0);C3D_FrameDrawOn(targets[panel]);
            set_fix(1,0,1,false);
            C3D_TexBind(0,sources[panel]);C3D_DrawArrays(GPU_TRIANGLES,(resident?24:36)+(lower?6:0),6);
            if(bars){bar_env(true);C3D_TexBind(0,&g_ext_tex);C3D_DrawArrays(GPU_TRIANGLES,60,12);bar_env(false);}
        }
    }
    return true;
}
}

// ---- DS fog (levels-render) ---------------------------------------------------
// SM64DS renders with W-buffering; DS fog density is a piecewise-linear table of
// that W (melonDS SoftRenderer::CalculateFogDensity). The unified vertex shader
// emits u = (W*4096-FOG_OFFSET*0x200)*2^(shift-19)/64 + 0.5/64 in texcoord1, and
// texture unit 1 holds the 34-entry density table (linear filtering reproduces
// the DS interpolation per pixel; perspective-correct interpolation of u is
// exact because W is affine in object space). TexEnv stage 1 then mixes the
// fog colour for polygons with attribute bit 15. Colour only: translucency
// decisions are made before DS fog, so alpha is left untouched.
static C3D_Tex g_fog_tex;
static bool g_fog_tex_init=false,g_fog_rows_valid=false,g_fog_active=false;
static uint8_t g_fog_rows[34];
static uint32_t g_fog_constant=0;
// Hardware experiment (SM64DS_HW_STENCIL_DEPTHWRITE=1): passes that write the
// stencil (translucent once-per-ID marks, DS shadow volumes) also enable depth
// writes, in case the real PICA only updates the stencil with depth writes on.
static GPU_WRITEMASK stencil_pass_mask(unsigned mask){
    static const bool force=[](){const char* v=std::getenv("SM64DS_HW_STENCIL_DEPTHWRITE");return v&&v[0]=='1';}();
    return static_cast<GPU_WRITEMASK>(force?(mask|GPU_WRITE_DEPTH):mask);
}
static bool fog_option(){static const bool value=[](){const char* s=std::getenv("SM64DS_FOG");return !(s&&std::strcmp(s,"0")==0);}();return value;}
static void fog_prepare(bool native){
    g_fog_active=false;
    if(g_u_fogp<0)return;
    const auto& c=g_native_clear;
    if(!native || !c.fog_enabled() || c.fog_alpha_only() || !fog_option()){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fogp,0.f,0.f,.5f,0.f);return;
    }
    uint8_t rows[34];rows[0]=c.fog_table[0];std::memcpy(rows+1,c.fog_table,32);rows[33]=c.fog_table[31];
    if(!g_fog_tex_init){
        if(!C3D_TexInit(&g_fog_tex,64,8,GPU_A8))return;
        C3D_TexSetFilter(&g_fog_tex,GPU_LINEAR,GPU_LINEAR);
        C3D_TexSetWrap(&g_fog_tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        g_fog_tex_init=true;g_fog_rows_valid=false;
    }
    if(!g_fog_rows_valid || std::memcmp(rows,g_fog_rows,sizeof rows)){
        // Called after FrameBegin: the previous frame no longer samples it.
        auto* dst=static_cast<u8*>(g_fog_tex.data);
        for(int y=0;y<8;++y)for(int x=0;x<64;++x){
            const unsigned d=rows[x<34?x:33];
            const unsigned v=d>=127?255u:(d*255u+64u)/128u;
            const int py=7-y;
            dst[((py>>3)*8+(x>>3))*64+morton8(x&7,py&7)]=u8(v);
        }
        C3D_TexFlush(&g_fog_tex);std::memcpy(g_fog_rows,rows,sizeof rows);g_fog_rows_valid=true;
    }
    const int shift=int(c.fog_shift());
    const double scale=std::ldexp(1.0,shift-13);
    const double offset=double(c.fog_offset)*512.0*std::ldexp(1.0,shift-25);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fogp,float(scale),float(1.0/128.0-offset),.5f,0.f);
    const unsigned r=ds_clear::State::channel(c.fog_color&31),g=ds_clear::State::channel((c.fog_color>>5)&31),
                   b=ds_clear::State::channel((c.fog_color>>10)&31);
    g_fog_constant=0xff000000u|(b<<16)|(g<<8)|r;
    C3D_TexBind(1,&g_fog_tex);
    g_fog_active=true;
}
static void fog_stage(unsigned polygon_attr){
    C3D_TexEnv* env=C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    if(!g_fog_active || !(polygon_attr&0x8000u))return;
    C3D_TexEnvSrc(env,C3D_RGB,GPU_CONSTANT,GPU_PREVIOUS,GPU_TEXTURE1);
    C3D_TexEnvOpRgb(env,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(env,C3D_RGB,GPU_INTERPOLATE);
    C3D_TexEnvColor(env,g_fog_constant);
}

// ---- DS shadow volumes (levels-render) -----------------------------------------
// melonDS SoftRenderer semantics: mask polygons (mode 3, ID 0) mark pixels where
// they FAIL the depth test; shadow polygons (mode 3, ID!=0) draw only on marked
// pixels, in front of the scene, never over pixels whose opaque (or translucent)
// polygon ID equals theirs (no self-shadowing), once per ID. Marks reset at the
// start of every mask group. Stencil bits 0-5 hold the pixel's polygon ID, bit 6
// the "translucent ID" flag, bit 7 occupancy; the mark itself lives in the 3D
// layer alpha (written as 1-shadow alpha, so ONE_MINUS_DST_ALPHA/DST_ALPHA
// blending applies the shadow colour only on marked pixels). Levels clear with
// alpha 31, so every 3D pixel is opaque and alpha is restored to 255 afterwards.
static bool g_shadow_marks=false;
static bool ds_shadows_enabled(){
    static const bool option=[](){const char* s=std::getenv("SM64DS_DS_SHADOWS");return !(s&&std::strcmp(s,"0")==0);}();
    return option && g_native_clear.alpha()==31;
}
static bool ensure_shadow_clear_quad(){
    if(g_shadow_clear)return true;
    g_shadow_clear=static_cast<V3dsVert*>(linearAlloc(6*sizeof(V3dsVert)));
    if(!g_shadow_clear)return false;
    const V3dsVert corners[4]={{-2,-2,0,1,1,1,1,1,0,0},{2,-2,0,1,1,1,1,1,0,0},
        {2,2,0,1,1,1,1,1,0,0},{-2,2,0,1,1,1,1,1,0,0}};
    const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)g_shadow_clear[i]=corners[indices[i]];
    GSPGPU_FlushDataCache(g_shadow_clear,6*sizeof(V3dsVert));
    return true;
}

static bool mixed_defer_initial_palette(){
 static const bool enabled=[](){const char* option=std::getenv("PLAYER_MIXED_DEFER_INITIAL_PALETTE");return option&&std::atoi(option)!=0;}();return enabled;
}
static bool gpugeo_draw=false; // this gpu_frame draws a GPU geometry mode frame
static void draw_batches(const V3dsVert *verts, int nverts, const V3dsBatch *b, int nb, V3dsVert *vbo, bool native, const ds_shadow::FramePlan* shadow_plan=nullptr)
{
    UnifiedDrawScope unified_scope;
    ds_shadow::Sequence shadow_sequence;
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, vbo, sizeof(V3dsVert), 3, 0x210);
    if(mesh_active)mesh_buffers();
    if(gpugeo_draw)gpugeo::bind();
    (void)verts; (void)nverts;
    // DS renders every opaque polygon before any translucent one (alpha<31,
    // A3I5/A5I3 textures or shadow volumes), whatever the submission order
    // (melonDS GPU3D sort keys). Draw in two stable phases so a translucent
    // batch submitted early cannot be overwritten by later opaque geometry.
    const bool fog_flags=native && g_fog_active && g_native_clear.alpha()==31;
    static const bool ds_order=[](){const char* s=std::getenv("SM64DS_DS_TRANSLUCENT_ORDER");return !(s&&std::strcmp(s,"0")==0);}();
    auto translucent_batch=[&](int i){
        const unsigned attr=b[i].polygon_attr;
        if(((attr>>16)&31)<31 || ((attr>>4)&3)==3)return true;
        return b[i].tex && g_batch_textures[i] && g_batch_textures[i]->has_partial_alpha;
    };
    for (int pass_phase = 0; pass_phase < (ds_order&&native?2:1); ++pass_phase)
    for (int i = 0; i < nb; ++i) {
        if(ds_order && native && translucent_batch(i)!=(pass_phase==1))continue;
        // Fully rejected source runs have no CPU geometry or shadow effects.
        if(mesh_active&&mixed_frame&&!mixed_frame->run_spans.at(i).count)continue;
        if(mesh_active&&(!mixed_frame||!mixed_defer_initial_palette()))mesh_bind_batch(i);
        const unsigned faces=b[i].polygon_attr & 0xc0;
        if(!faces)continue;
        // Native render targets reflect Y; unlike the rotated LCD transform,
        // that reverses projected winding and therefore the visible face.
        const bool back_ccw=(faces==0x80)!=native;
        C3D_CullFace(faces==0xc0 ? GPU_CULL_NONE : back_ccw ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW);
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        if (b[i].tex) {
            GpuTex *cached = g_batch_textures[i];
            if (!cached) continue;
            C3D_Tex *t = &cached->tex;
            C3D_TexSetWrap(t, wrap_mode(b[i].wrap & 1, b[i].wrap & 4), wrap_mode(b[i].wrap & 2, b[i].wrap & 8));
            C3D_TexBind(0, t);
            C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE); // combined DS alpha is in texture
        } else {
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            const unsigned pa = (b[i].polygon_attr >> 16) & 31;
            const unsigned alpha = pa == 0 ? 0 : pa == 31 ? 255 : ((pa + 1) * 255 + 16) / 32;
            C3D_TexEnvColor(env, 0x00ffffffu | (alpha << 24));
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
        }
        fog_stage(b[i].polygon_attr);
        const unsigned pa = (b[i].polygon_attr >> 16) & 31;
        if (!pa) continue; // alpha-zero wireframe is not implemented by this renderer
        const bool partial = b[i].tex ? g_batch_textures[i]->has_partial_alpha : pa < 31;
        const bool write_translucent_depth = (b[i].polygon_attr & (1u << 11)) != 0;
        auto draw=[&](){if(mesh_active&&mixed_frame){
            // EACH original alpha/stencil pass traverses the same ordered span.
            // Shader switches never rerun material/shadow state setup.
            const auto& span=mixed_frame->run_spans.at(i);
            for(unsigned j=span.first;j<span.first+span.count;++j){const auto& d=mixed_frame->draws[j];
                if(d.stream==mixed_clip::Stream::NativeIndexed){mesh_bind_batch(i);mesh_buffers();C3D_DrawElements(GPU_TRIANGLES,d.count,C3D_UNSIGNED_SHORT,mesh_indices+d.first);++mesh_stats.draws;++mixed_native_draws;}
                else{shader_bind(false);C3D_SetAttrInfo(&g_attr);auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);BufInfo_Add(buffers,mixed_cpu,sizeof(V3dsVert),3,0x210);C3D_DrawArrays(GPU_TRIANGLES,d.first,d.count);++mixed_cpu_draws;}
            }
        }else if(mesh_active){C3D_DrawElements(GPU_TRIANGLES,b[i].count,C3D_UNSIGNED_SHORT,mesh_indices+b[i].first);++mesh_stats.draws;}else if(gpugeo_draw)gpugeo::draw_batch(b[i],i);else C3D_DrawArrays(GPU_TRIANGLES,b[i].first,b[i].count);};
        if(shadow_plan){
            auto operation=[](ds_shadow::Op op)->GPU_STENCILOP{
                switch(op){case ds_shadow::Op::Zero:return GPU_STENCIL_ZERO;case ds_shadow::Op::Replace:return GPU_STENCIL_REPLACE;
                    case ds_shadow::Op::Invert:return GPU_STENCIL_INVERT;default:return GPU_STENCIL_KEEP;}
            };
            shadow_plan->emit(g_shadow_batches[i],shadow_sequence,[&](const ds_shadow::Pass& pass){
                C3D_StencilTest(true,pass.test==ds_shadow::Test::Equal?GPU_EQUAL:GPU_ALWAYS,
                    pass.reference,pass.input_mask,pass.write_mask);
                C3D_StencilOp(operation(pass.stencil_fail),operation(pass.depth_fail),operation(pass.depth_pass));
                C3D_DepthTest(true,pass.depth==ds_shadow::Depth::SceneStrict?GPU_GREATER:GPU_ALWAYS,
                    pass.depth_pass==ds_shadow::Op::Keep&&pass.depth_fail==ds_shadow::Op::Keep&&pass.stencil_fail==ds_shadow::Op::Keep?
                    static_cast<GPU_WRITEMASK>((pass.color_write?GPU_WRITE_COLOR:0)|(pass.depth_write?GPU_WRITE_DEPTH:0)):
                    stencil_pass_mask((pass.color_write?GPU_WRITE_COLOR:0)|(pass.depth_write?GPU_WRITE_DEPTH:0)));
                if(pass.alpha==ds_shadow::Alpha::Disabled)C3D_AlphaTest(false,GPU_ALWAYS,0);
                else C3D_AlphaTest(true,pass.alpha==ds_shadow::Alpha::OpaqueOnly?GPU_EQUAL:GPU_GREATER,
                    pass.alpha==ds_shadow::Alpha::OpaqueOnly?255:0);
                if(pass.blend==ds_shadow::Blend::Replace)
                    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
                else C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_MAX,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE);
                if(pass.fullscreen){
                    // GetBufInfo marks the state dirty. Reusing the pointer alone
                    // leaves the previous VBO bound after a draw.
                    C3D_CullFace(GPU_CULL_NONE);bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,g_shadow_clear,sizeof(V3dsVert),3,0x210);
                    if(mesh_active){shader_bind(false);C3D_SetAttrInfo(&g_attr);}
                    C3D_DrawArrays(GPU_TRIANGLES,0,6);
                    bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,vbo,sizeof(V3dsVert),3,0x210);
                    if(mesh_active){mesh_bind_batch(i);mesh_buffers();}
                    C3D_CullFace(faces==0xc0?GPU_CULL_NONE:back_ccw?GPU_CULL_BACK_CCW:GPU_CULL_FRONT_CCW);
                }else draw();
            });
        } else if(native && ((b[i].polygon_attr>>4)&3)==3 && ds_shadows_enabled()) {
            const unsigned id=(b[i].polygon_attr>>24)&63;
            auto fullscreen_alpha=[&](unsigned alpha){
                if(!ensure_shadow_clear_quad())return;
                C3D_TexEnv* reset=C3D_GetTexEnv(0);C3D_TexEnvInit(reset);
                C3D_TexEnvSrc(reset,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(reset,C3D_Both,GPU_REPLACE);
                C3D_TexEnvColor(reset,alpha<<24);fog_stage(0);
                C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);C3D_AlphaTest(false,GPU_ALWAYS,0);
                C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALPHA);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
                C3D_CullFace(GPU_CULL_NONE);
                shader_bind(false);C3D_SetAttrInfo(&g_attr);
                bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,g_shadow_clear,sizeof(V3dsVert),3,0x210);
                C3D_DrawArrays(GPU_TRIANGLES,0,6);
                bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,vbo,sizeof(V3dsVert),3,0x210);
                if(mesh_active){mesh_bind_batch(i);mesh_buffers();}
                C3D_CullFace(faces==0xc0?GPU_CULL_NONE:back_ccw?GPU_CULL_BACK_CCW:GPU_CULL_FRONT_CCW);
            };
            if(id==0){
                if(!shadow_sequence.previous_mask){fullscreen_alpha(255);g_shadow_marks=true;}
                shadow_sequence.previous_mask=true;
                // Mark where the mask FAILS the DS depth test (behind or equal).
                C3D_TexEnv* mark=C3D_GetTexEnv(0);C3D_TexEnvInit(mark);
                C3D_TexEnvSrc(mark,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(mark,C3D_Both,GPU_REPLACE);
                const unsigned pa8=pa==31?255u:((pa+1)*255+16)/32;
                C3D_TexEnvColor(mark,(255u-pa8)<<24);fog_stage(0);
                C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);C3D_AlphaTest(false,GPU_ALWAYS,0);
                C3D_DepthTest(true,GPU_LEQUAL,GPU_WRITE_ALPHA);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
                draw();
            }else{
                shadow_sequence.previous_mask=false;
                // Once per ID, never over the same opaque/translucent ID.
                C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
                C3D_StencilTest(true,GPU_NOTEQUAL,0xc0|id,0x3f,0x7f);
                C3D_AlphaTest(true,GPU_GREATER,0);
                C3D_DepthTest(true,GPU_GREATER,stencil_pass_mask(GPU_WRITE_RED|GPU_WRITE_GREEN|GPU_WRITE_BLUE|(write_translucent_depth?GPU_WRITE_DEPTH:0)));
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE_MINUS_DST_ALPHA,GPU_DST_ALPHA,GPU_ZERO,GPU_ONE);
                draw();
            }
        } else if(native) {
            shadow_sequence.previous_mask=false;
            C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE);
            if(!partial || pa==31) {
                // Opaque texels initialize color/depth and mark an occupied pixel
                // carrying their (opaque) polygon ID. With per-pixel fog flags
                // (levels: opaque clear), bit 7 is the DS attribute fog flag.
                const unsigned occupied=fog_flags?(b[i].polygon_attr&0x8000u?0x80u:0u):0x80u;
                C3D_StencilTest(true,GPU_ALWAYS,occupied|((b[i].polygon_attr>>24)&63),0xff,0xff);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
                C3D_AlphaTest(true,partial?GPU_EQUAL:GPU_GREATER,partial?255:0);
                C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
                draw();
            }
            if(partial && fog_flags) {
                // Every pixel is occupied (opaque clear); bit 7 = fog flag F.
                // DS: blend once per translucent ID; fog iff F; F &= own bit.
                const unsigned id=(b[i].polygon_attr>>24)&63;
                const bool own_fog=(b[i].polygon_attr&0x8000u)!=0;
                C3D_AlphaTest(true,GPU_GREATER,0);
                C3D_DepthTest(true,GPU_GREATER,stencil_pass_mask(write_translucent_depth?GPU_WRITE_ALL:GPU_WRITE_COLOR));
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_MAX,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE);
                if(own_fog){
                    // F==0 pixels first, unfogged; they become (0,1,id) so the
                    // fogged pass below skips them exactly like same-ID pixels.
                    fog_stage(0);
                    C3D_StencilTest(true,GPU_EQUAL,0x40|id,0x80,0x7f);
                    draw();
                    fog_stage(b[i].polygon_attr);
                    C3D_StencilTest(true,GPU_NOTEQUAL,0x40|id,0x7f,0x7f);
                    draw();
                }else{
                    C3D_StencilTest(true,GPU_NOTEQUAL,0x40|id,0x7f,0xff);
                    draw();
                }
            } else if(partial) {
                const unsigned reference=0xc0|((b[i].polygon_attr>>24)&63);
                C3D_AlphaTest(true,GPU_GREATER,0);
                C3D_DepthTest(true,GPU_GREATER,stencil_pass_mask(write_translucent_depth?GPU_WRITE_ALL:GPU_WRITE_COLOR));
                // DS keeps straight RGB for the first fragment on transparent
                // clear. Mark its ID so the blending pass cannot apply it twice.
                C3D_StencilTest(true,GPU_NOTEQUAL,reference,0x80,0xff);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
                // DS fogs a translucent pixel only if the pixel below carries
                // the fog flag; an unoccupied (clear) pixel never does here.
                if(g_fog_active)fog_stage(0);
                draw();
                if(g_fog_active)fog_stage(b[i].polygon_attr);
                // Occupied pixels blend once per translucent polygon ID. Alpha
                // is the maximum source/destination coverage, never alpha^2.
                C3D_StencilTest(true,GPU_NOTEQUAL,reference,0xff,0xff);
                C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_MAX,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ONE,GPU_ONE);
                draw();
            }
        } else {
            C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
            C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,
                          GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA);
            if (partial && pa == 31) {
                C3D_AlphaTest(true, GPU_EQUAL, 255);
                C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
                draw();
            }
            C3D_AlphaTest(true, GPU_GREATER, 0);
            C3D_DepthTest(true, GPU_GREATER, !partial || write_translucent_depth ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
            draw();
        }
    }
    fog_stage(0);
    if(g_shadow_marks && native && ensure_shadow_clear_quad()){
        // Restore the opaque 3D layer alpha consumed by the shadow marks.
        C3D_TexEnv* reset=C3D_GetTexEnv(0);C3D_TexEnvInit(reset);
        C3D_TexEnvSrc(reset,C3D_Both,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT);C3D_TexEnvFunc(reset,C3D_Both,GPU_REPLACE);
        C3D_TexEnvColor(reset,0xff000000u);
        C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);C3D_AlphaTest(false,GPU_ALWAYS,0);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALPHA);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        C3D_CullFace(GPU_CULL_NONE);
        shader_bind(false);C3D_SetAttrInfo(&g_attr);
        bi=C3D_GetBufInfo();BufInfo_Init(bi);BufInfo_Add(bi,g_shadow_clear,sizeof(V3dsVert),3,0x210);
        C3D_DrawArrays(GPU_TRIANGLES,0,6);
        g_shadow_marks=false;
    }
}

// PPU-presentation readback channel order. A clear + display transfer
// reads back identically on both, but CPU readback of GPU-*rendered* pixels
// differs: Azahar delivers DS red in the PICA blue byte, the New 3DS in the
// red byte (Mario's head was blue on hardware after an Azahar-validated R/B
// exchange). Azahar answers the Citra system-info query 0x20000; the console
// does not. SM64DS_NATIVE_SWAP_RB=0/1 forces the choice.
static bool native_readback_swap(){
    static int decided=-1;
    if(decided>=0)return decided;
    if(const char* forced=std::getenv("SM64DS_NATIVE_SWAP_RB")){decided=std::atoi(forced)!=0;}
    else{s64 emulator=0;const Result result=svcGetSystemInfo(&emulator,0x20000,0);decided=R_SUCCEEDED(result) && emulator!=0;}
    std::printf("Native readback R/B exchange=%d (Azahar-only)\n",decided);
    return decided;
}
static bool gpu_frame(const V3dsVert *verts, int nverts, const V3dsBatch *batches, int nb,
                    const char *status, const char *mode, float aspect_x,
                    float convergence, bool perspective, bool stereo,
                    uint32_t* native_left=nullptr,uint32_t* native_right=nullptr
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
                    ,bool resident=false
#endif
                    ,uint64_t direct_token=0)
{
    const bool native=native_left!=nullptr
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        || resident
#endif
        ;
    if(native && !g_native_clear.supported()){
        std::fprintf(stderr,"Native GX clear requires unsupported clear-image/fog input: %08x/%08x/%04x\n",g_native_clear.color,g_native_clear.depth,unsigned(g_native_clear.display));return false;
    }
    gpugeo_draw=native && gpugeo::active_for(direct_token);
    const bool profile=native && std::getenv("PLAYER_PROFILE_GPU");
    double phase[5]{},texture_calls_ms=0;if(profile)phase[0]=v3ds_time_ms();
    if(direct_token){
        if(!native || !g_direct_valid || direct_token!=g_direct_lease.frame_token ||
           verts!=g_direct_lease.vertices || nverts<0 || nverts>g_vbo_cap)return false;
    }else{
        if(g_direct_open)return false;
        g_direct_valid=false;
    }
    if (nverts > g_vbo_cap) nverts = g_vbo_cap;
    V3dsVert *vbo=direct_token?g_direct_lease.vertices:g_vbo[g_frame_parity ^= 1];
    if(!direct_token&&!mesh_active)memcpy(vbo, verts, nverts * sizeof(V3dsVert));
    if(native && !direct_token&&!mesh_active)for(int i=0;i<nverts;++i){
        // DS samples integer pixel positions; PICA samples pixel centres.
        vbo[i].x+=vbo[i].w/256.f;
        vbo[i].y-=vbo[i].w/192.f;
    }
    if(!mesh_active&&(!direct_token || !g_direct_flushed)){
        GSPGPU_FlushDataCache(vbo,nverts*sizeof(V3dsVert));
        if(direct_token)g_direct_flushed=true;
    }

    float slider = osGet3DSliderState();
    if (const char *f = getenv("SM64DS_FORCE_3D")) slider = (float)atof(f);   // headless stereo tests
    if (!stereo) slider = 0.0f;
    slider = slider < 0.0f ? 0.0f : slider > 1.0f ? 1.0f : slider;
    gfxSet3D(slider > 0.0f);
    if(native)g_native_stereo=slider>0.0f;
    // An empty GX list still exposes its configured constant clear plane.
    // No polygon observes the depth/stencil values in this shortcut.
    // The presentation pass waits for any preceding frame as usual.
    if(native && !mesh_active && nverts==0 && !std::getenv("PLAYER_EMPTY_GEOMETRY_GPU")
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
       && !resident
#endif
       ){
        if(!native_right)return false;
        if(direct_token && g_direct_open)end_gpu_frame();
        std::fill_n(native_left,256*192,g_native_clear.argb());
        std::fill_n(native_right,256*192,g_native_clear.argb());
        return true;
    }
    const bool read_native=native_left!=nullptr;
    if(read_native && !g_native_left){
        // Keep DS axes here: rotating changes which boundary edges the
        // hardware top-left coverage rule includes. LCD rotation is in blit.
        g_native_left=C3D_RenderTargetCreate(256,192,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        g_native_right=C3D_RenderTargetCreate(256,192,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        g_native_readback=static_cast<u32*>(linearAlloc(256*192*4));
        std::printf("Native readback targets created remainingVRAM=%u\n",vramSpaceFree());
    }
    if(read_native && (!g_native_left || !g_native_right || !g_native_readback || !native_right))return false;
    const float sep = 0.03f * slider;        // clip-space units at w = 1
    const float conv = convergence;

#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    const double wait_start=v3ds_time_ms();
    const unsigned counter_before[]={C3D_FrameCounter(0),C3D_FrameCounter(1)};
#endif
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    // Acquisition drained the GPU with flags0 before CPU writes. Normal
    // resident presentation must still honor its original LCD-sync flags.
    // End this empty acquisition list, retain the immutable lease, then begin
    // with SYNCDRAW at the same submission boundary as the legacy backend.
    if(direct_token && g_direct_open && resident &&
       !std::getenv("PLAYER_RESIDENT_NO_VBLANK_WAIT"))end_gpu_frame();
#endif
    if(!(direct_token && g_direct_open)){
        frame_profile::Scope begin_scope(frame_profile::WorkerFrameBegin);
        if(!C3D_FrameBegin(
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
            resident?(std::getenv("PLAYER_RESIDENT_NO_VBLANK_WAIT")?0:C3D_FRAME_SYNCDRAW):
#endif
            native?0:C3D_FRAME_SYNCDRAW))return false;
        if(direct_token)g_direct_open=true;
    }
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(resident)g_resident_wait=v3ds_time_ms()-wait_start;
    if(resident){g_resident_previous_gpu=C3D_GetDrawingTime();
        for(unsigned i=0;i<2;++i)g_resident_vblanks[i]=C3D_FrameCounter(i)-counter_before[i];}
    shader_bind(false);C3D_SetAttrInfo(&g_attr);C3D_LightEnvBind(nullptr);
#endif
    // Reuse only resident geometry. Current OBJ/PPU/LCD work still follows.
    const held_raster::Parameters raster_parameters{g_native_clear,slider,convergence,aspect_x,perspective,std::getenv("PLAYER_GPU_SHADOWS")!=nullptr};
    bool raster_candidate=false;
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(resident){
        raster_candidate=held_raster_enabled() && mesh_active && bool(mesh_frame);
        if(raster_candidate && g_held_raster.matches(mesh_frame,raster_parameters)){++g_held_raster_hits;return true;}
        g_held_raster.invalidate();
        if(raster_candidate)++g_held_raster_misses;
    }
#endif
    const uint64_t textures_start=frame_profile::ticks();
    ++g_texture_frame;
    trim_gpu_textures(0);
    if(profile)phase[1]=v3ds_time_ms();
    g_frame_textures.clear();
    g_batch_textures.resize(nb);
    for (int i = 0; i < nb; ++i) {
        const auto &b = batches[i];
        GpuTex *texture = nullptr;
        const unsigned alpha = (b.polygon_attr >> 16) & 31;
        if (b.tex) {
            bool found = false;
            for (const auto &cached : g_frame_textures) {
                if (cached.pixels == b.tex && cached.w == b.tw && cached.h == b.th && cached.dynamic == b.dynamic && cached.alpha == alpha && cached.version==b.texture_version) {
                    texture = cached.gpu;
                    found = true;
                    break;
                }
            }
            if (!found) {
                const double texture_start=profile?v3ds_time_ms():0;
                texture = gpu_texture(b.tex, b.tw, b.th, b.dynamic, alpha, b.texture_version);
                if(!texture){
                    std::fprintf(stderr,"GPU texture allocation failed: %dx%d, retained=%u bytes (no eye drawn)\n",
                        b.tw,b.th,unsigned(g_texture_bytes));
                    end_gpu_frame();return false;
                }
                if(profile)texture_calls_ms+=v3ds_time_ms()-texture_start;
                g_frame_textures.push_back({b.tex, b.tw, b.th, b.dynamic, alpha, b.texture_version, texture});
            }
        }
        g_batch_textures[i] = texture;
    }
    if(profile)phase[2]=v3ds_time_ms();
    sm64ds_frame_prof[frame_profile::WorkerTextures]+=frame_profile::ticks()-textures_start;
    frame_profile::Scope draw_scope(frame_profile::WorkerDraw);
    fog_prepare(native);
    const ds_shadow::FramePlan* shadow_plan=nullptr;
    if(native && std::getenv("PLAYER_GPU_SHADOWS")){
        g_shadow_batches.clear();g_shadow_batches.reserve(nb);
        for(int i=0;i<nb;++i){
            const auto attr=mesh_active&&mixed_frame&&!mixed_frame->run_spans.at(i).count?0u:batches[i].polygon_attr;const unsigned alpha=(attr>>16)&31;
            const auto* texture=g_batch_textures[i];
            g_shadow_batches.push_back({attr,texture?texture->has_opaque_alpha:alpha==31,
                texture?texture->has_partial_alpha:alpha>0&&alpha<31});
        }
        if(!g_shadow_plan.prepare(g_shadow_batches.data(),g_shadow_batches.size(),g_native_clear.polygon_id(),g_native_clear.alpha()!=0)){
            std::fprintf(stderr,"GPU shadow preflight unsupported: reason=%u (no eye drawn)\n",unsigned(g_shadow_plan.failure()));
            end_gpu_frame();return false;
        }
        if(!g_shadow_clear){
            g_shadow_clear=static_cast<V3dsVert*>(linearAlloc(6*sizeof(V3dsVert)));
            if(!g_shadow_clear){end_gpu_frame();return false;}
            const V3dsVert corners[4]={{-2,-2,0,1,1,1,1,1,0,0},{2,-2,0,1,1,1,1,1,0,0},
                {2,2,0,1,1,1,1,1,0,0},{-2,2,0,1,1,1,1,1,0,0}};
            const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)g_shadow_clear[i]=corners[indices[i]];
            GSPGPU_FlushDataCache(g_shadow_clear,6*sizeof(V3dsVert));
        }
        shadow_plan=&g_shadow_plan;
    }
    auto* left=native?g_native_left:g_left;
    auto* right=native?g_native_right:g_right;
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(resident){left=g_resident_geometry_target[0];right=g_resident_geometry_target[1];}
#endif
    const uint32_t clear_color=native?g_native_clear.rgba():CLEAR;
    const bool clear_fog_flags=native && g_fog_active && g_native_clear.alpha()==31;
    const unsigned clear_stencil=native?(shadow_plan?shadow_plan->initial_tag():
        clear_fog_flags?((g_native_clear.color&0x8000u)?0x80u:0u)|g_native_clear.polygon_id():
        (g_native_clear.alpha()?(0x80|g_native_clear.polygon_id()):0)):0;
    const uint32_t clear_depth=native?(g_native_clear.reversed_depth()|(clear_stencil<<24)):0;
    C3D_RenderTargetClear(left, C3D_CLEAR_ALL, clear_color, clear_depth);
    C3D_FrameDrawOn(left);
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    if(resident)C3D_SetViewport(0,0,256,192);
#endif
    set_fix(aspect_x, -sep, conv, perspective);
    if(native){
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix,1.f,0.f,0.f,0.f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,-1.f,0.f,0.f);
    }
    // Menu: snap DS-space vertices to the DS integer viewport grid (see
    // vshader.v.pica). Reset below so LCD/composite quads are never snapped.
    static const bool snap_allowed=[](){const char* v=std::getenv("SM64DS_DS_SNAP");return !(v&&std::strcmp(v,"0")==0);}();
    const bool ds_snap=native && g_menu_presentation.load(std::memory_order_relaxed) && g_u_snap>=0 && snap_allowed;
    if(ds_snap)C3D_BoolUnifSet(GPU_VERTEX_SHADER,g_u_snap,true);
    if(mesh_active){shader_bind(true);C3D_SetAttrInfo(&mesh_attributes);}
    draw_batches(verts, nverts, batches, nb, vbo, native, shadow_plan);
    if (slider > 0.0f) {
        C3D_RenderTargetClear(right, C3D_CLEAR_ALL, clear_color, clear_depth);
        C3D_FrameDrawOn(right);
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        if(resident)C3D_SetViewport(0,0,256,192);
#endif
        set_fix(aspect_x, sep, conv, perspective);
        if(native){
            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix,1.f,0.f,0.f,0.f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER,g_u_fix+1,0.f,-1.f,0.f,0.f);
        }
        if(ds_snap)C3D_BoolUnifSet(GPU_VERTEX_SHADER,g_u_snap,true);
        if(mesh_active){shader_bind(true);C3D_SetAttrInfo(&mesh_attributes);}
    draw_batches(verts, nverts, batches, nb, vbo, native, shadow_plan);
    }
    if(ds_snap)C3D_BoolUnifSet(GPU_VERTEX_SHADER,g_u_snap,false);
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
    // Continue composition and LCD draws in this same command list.
    if(resident){if(raster_candidate)g_held_raster.commit(mesh_frame,raster_parameters);return true;}
#endif
    end_gpu_frame();
    if(profile)phase[3]=v3ds_time_ms();
    if(native){
        const bool swap=native_readback_swap();
        auto read=[&](C3D_RenderTarget* target,uint32_t* output){
            const u32 flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|
                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|
                GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
            C3D_SyncDisplayTransfer(static_cast<u32*>(target->frameBuf.colorBuf),GX_BUFFER_DIM(256,192),
                g_native_readback,GX_BUFFER_DIM(256,192),flags);
            GSPGPU_InvalidateDataCache(g_native_readback,256*192*4);
            for(int y=0;y<192;++y)for(int x=0;x<256;++x){
                const u32 rgba=g_native_readback[(191-y)*256+x];
                // Texture alpha stores the 8-bit approximation of (DSalpha+1)/32.
                // Invert that monotonic encoding after MAX blending.
                // Channel order of this readback differs between Azahar and
                // the console (Azahar: DS red arrives in the PICA blue byte;
                // hardware: in the red byte), see native_readback_swap().
                const u32 argb=decode_native_pixel(rgba);
                output[y*256+x]=swap?(argb&0xff00ff00u)|((argb>>16)&0xffu)|((argb&0xffu)<<16):argb;
            }
        };
        read(left,native_left);
        if(slider>0)read(right,native_right);
        else std::memcpy(native_right,native_left,256*192*4);
    }else tick_fps(status, mode);
    if(profile){
        phase[4]=v3ds_time_ms();static double totals[4]{},first[4]{},texture_total=0;static unsigned samples=0;
        texture_total+=texture_calls_ms;
        for(unsigned i=0;i<4;++i){const double elapsed=phase[i+1]-phase[i];totals[i]+=elapsed;if(!samples)first[i]=elapsed;}
        if(++samples==60){
            std::printf("Native GPU parts ms: setup=%.3f textures=%.3f submit=%.3f readback=%.3f\n",
                totals[0]/samples,totals[1]/samples,totals[2]/samples,totals[3]/samples);
            std::printf("Native GPU first ms: setup=%.3f textures=%.3f submit=%.3f readback=%.3f\n",first[0],first[1],first[2],first[3]);
            std::printf("Native GPU following59 ms: setup=%.3f textures=%.3f submit=%.3f readback=%.3f\n",
                (totals[0]-first[0])/59,(totals[1]-first[1])/59,(totals[2]-first[2])/59,(totals[3]-first[3])/59);
            std::printf("Native GPU texture calls ms=%.3f\n",texture_total/samples);
            texture_total=0;
            samples=0;for(auto& value:totals)value=0;
        }
    }
    return true;
}

void v3ds_gpu_frame(const V3dsVert* v,int nv,const V3dsBatch* b,int nb,const char* status,
                    const char* mode,float aspect,float convergence,bool perspective,bool stereo){
    gpu_frame(v,nv,b,nb,status,mode,aspect,convergence,perspective,stereo);
}
#ifdef SM64DS_GEOMETRY_DIAGNOSTICS
static unsigned g_geometry_calls;
unsigned v3ds_gpu_geometry_calls(){return g_geometry_calls;}
#endif
bool v3ds_gpu_geometry(const V3dsVert* v,int nv,const V3dsBatch* b,int nb,
                      uint32_t* left,uint32_t* right,float convergence,bool perspective,bool stereo){
#ifdef SM64DS_GEOMETRY_DIAGNOSTICS
    ++g_geometry_calls;
#endif
    return gpu_frame(v,nv,b,nb,"Native scene","PPU",1.f,convergence,perspective,stereo,left,right);
}
namespace v3ds_arena {
bool geometry(const ntr::gx_geometry::Prepared& p,uint32_t* left,uint32_t* right,
              float convergence,bool perspective,bool stereo){
    if(!accepts(p))return false;
    if(!g_direct_committed){g_direct_prepared=p;g_direct_committed=true;}
    const bool result=gpu_frame(p.vertices,int(p.vertex_count),p.batches,int(p.batch_count),
        "Direct arena","PPU",1.f,convergence,perspective,stereo,left,right
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
        ,false
#endif
        ,p.frame_token);
    if(!result && g_direct_open)end_gpu_frame();
    return result;
}
}
#ifdef SM64DS_GPU_COMPOSITE_CANDIDATE
static bool resident_destroy(){
    g_held_raster.invalidate();
    // This function is only entered outside a frame (init failure or exit).
    // BindProgram inspects the previous program's DVLP, so all candidate
    // programs must remain alive until rebinding the permanent viewer shader.
    if(!C3D_FrameBegin(0)){
        std::printf("Resident GPU release refused: frame still active\n");return false;
    }
    shader_bind(false);
    tracked_frame_end(GX_CMDLIST_FLUSH);
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
    for(unsigned engine=0;engine<2;++engine){
        g_resident_objects[engine].destroy();
        g_resident_object_merge[engine].destroy();
        g_resident_object_layer[engine].destroy();
        g_resident_objects_ready[engine]=false;
    }
#endif
    for(auto& compositor:g_resident_compositor)compositor.destroy();
    for(auto& panel:g_resident_panels)panel.destroy();
    for(unsigned eye=0;eye<2;++eye){
        if(g_resident_geometry_target[eye])C3D_RenderTargetDelete(g_resident_geometry_target[eye]);
        g_resident_geometry_target[eye]=nullptr;
        if(g_resident_geometry[eye].data)C3D_TexDelete(&g_resident_geometry[eye]);
        g_resident_geometry[eye]={};
    }
    if(g_resident_bottom)C3D_RenderTargetDelete(g_resident_bottom);
    g_resident_bottom=nullptr;
    if(g_resident_vertices)linearFree(g_resident_vertices);
    g_resident_vertices=nullptr;
    if(g_resident_diagnostic)linearFree(g_resident_diagnostic);
    g_resident_diagnostic=nullptr;g_resident_ready=false;
    return true;
}
static bool resident_init(){
    if(g_resident_ready)return true;
    const unsigned vram_before=vramSpaceFree(),linear_before=linearSpaceFree();
    for(unsigned eye=0;eye<2;++eye){
        auto& texture=g_resident_geometry[eye];
        if(!C3D_TexInitVRAM(&texture,256,256,GPU_RGBA8)){resident_destroy();return false;}
        C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
        C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        g_resident_geometry_target[eye]=C3D_RenderTargetCreateFromTex(&texture,GPU_TEXFACE_2D,0,GPU_RB_DEPTH24_STENCIL8);
        if(!g_resident_geometry_target[eye]){resident_destroy();return false;}
    }
    for(auto& compositor:g_resident_compositor)
#ifdef SM64DS_GPU_COMPOSITE_BUILD
        if(!compositor.init(reinterpret_cast<const uint32_t*>(gpu_composite_shaders_shbin),gpu_composite_shaders_shbin_size)){resident_destroy();return false;}
#else
        if(!compositor.init(fps_gpu_composite_shader,sizeof(fps_gpu_composite_shader))){resident_destroy();return false;}
#endif
    for(auto& panel:g_resident_panels)if(!panel.init()){resident_destroy();return false;}
    g_resident_bottom=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,-1);
    g_resident_vertices=static_cast<V3dsVert*>(linearAlloc(18*sizeof(V3dsVert)));
    if(!g_resident_bottom || !g_resident_vertices){resident_destroy();return false;}
    C3D_RenderTargetSetOutput(g_resident_bottom,GFX_BOTTOM,GFX_LEFT,DISPLAY_TRANSFER_FLAGS);
    for(unsigned panel=0;panel<3;++panel){
        const float width=panel==2?320.f:400.f;
        const float u0=-.5f/width+1.f/65536.f;
        const float v0=.75f+.5f*.75f/240.f-1.f/65536.f;
        const V3dsVert quad[4]={
            {-1,1,0,1,1,1,1,1,u0,v0},{1,1,0,1,1,1,1,1,u0+1,v0},
            {1,-1,0,1,1,1,1,1,u0+1,v0-.75f},{-1,-1,0,1,1,1,1,1,u0,v0-.75f}};
        const unsigned indices[]={0,1,2,0,2,3};
        for(unsigned i=0;i<6;++i)g_resident_vertices[panel*6+i]=quad[indices[i]];
    }
    GSPGPU_FlushDataCache(g_resident_vertices,18*sizeof(V3dsVert));
    g_resident_ready=true;
    std::printf("Resident GPU allocation bytes: VRAM=%u linear=%u remainingVRAM=%u remainingLinear=%u\n",
        vram_before-vramSpaceFree(),linear_before-linearSpaceFree(),vramSpaceFree(),linearSpaceFree());
    return true;
}
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
static bool resident_objects_init(unsigned engine){
    if(g_resident_objects_ready[engine])return true;
#ifdef SM64DS_GPU_COMPOSITE_BUILD
    const auto* shader=reinterpret_cast<const uint32_t*>(gpu_composite_shaders_shbin);
    const unsigned bytes=gpu_composite_shaders_shbin_size;
#else
    const auto* shader=fps_gpu_composite_shader;
    const unsigned bytes=sizeof(fps_gpu_composite_shader);
#endif
    if(!g_resident_objects[engine].init(shader,bytes) ||
       !g_resident_object_merge[engine].init() ||
       !g_resident_object_layer[engine].init()){
        resident_destroy();return false;
    }
    g_resident_objects_ready[engine]=true;
    std::printf("Resident OBJ GPU ready engine=%u remainingVRAM=%u remainingLinear=%u\n",
        engine,vramSpaceFree(),linearSpaceFree());
    return true;
}
#endif
const char* g_present_refusal="";
extern "C" volatile int sm64ds_gpu_affine_ready,sm64ds_gpu_affine_wanted; // castle-min30 (gpu_composite.cpp)
static bool present_candidates_core(const V3dsVert* v,int nv,const V3dsBatch* b,int nb,
    ntr::PpuPreparedCandidates a,ntr::PpuPreparedCandidates other,
    bool a_upper,float convergence,bool perspective,bool stereo,
    const ds_gpu::ObjPlan* a_objects,const ds_gpu::ObjPlan* b_objects,uint64_t direct_token){
    // minigames-2d-gpu: one-shot DS display-capture plan (gpu_composite.h).
    const ds_gpu::CaptureFramePlan plan=ds_gpu::g_capture_plan;ds_gpu::g_capture_plan={};
    for(const auto* scene:{&a.state,&other.state})
        if(!ds_gpu::composite_blend_free(*scene)){g_present_refusal="core1";return false;}
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
    const ds_gpu::ObjPlan* objects[]={a_objects,b_objects};
    const ntr::PpuPreparedCandidates scenes[]={a,other};
    for(unsigned engine=0;engine<2;++engine)if(objects[engine]){
        if(objects[engine]->fallback!=ds_gpu::ObjFallback::None ||
           !ds_gpu::NativeObjMerge::supported(scenes[engine])){g_present_refusal="core2";return false;}
    }
#else
    if(a_objects || b_objects){g_present_refusal="core3";return false;}
#endif
    if(!resident_init()){g_present_refusal="core4";return false;}
    {   // castle-min30: create the engine-B affine surface once the PPU asks
        // for it and VRAM allows (see gpu_composite.cpp ensure_affine).
        if(sm64ds_gpu_affine_wanted && !sm64ds_gpu_affine_ready && g_resident_compositor[1].ensure_affine(true))sm64ds_gpu_affine_ready=1;
    }
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
    for(unsigned engine=0;engine<2;++engine)
        if(objects[engine] && objects[engine]->count && !resident_objects_init(engine)){g_present_refusal="core5";return false;}
#endif
    frame_profile::Scope core_scope(frame_profile::WorkerCore);
    const double started=v3ds_time_ms();
    static bool delayed_valid=false;
    ds_gpu::capture_pool_frame(plan.capture || plan.view || plan.delay_3d);
    // Capture/scanline-feedback cases are rejected by the caller before this
    // entry point. They need monoscopic source pixels rather than one eye.
    if(!gpu_frame(v,nv,b,nb,"Native scene","Resident candidate",1.f,convergence,perspective,
                  stereo&&a_upper&&!plan.delay_3d,nullptr,nullptr,true,direct_token)){g_present_refusal="core6";return false;}
    frame_profile::Scope compose_scope(frame_profile::WorkerCompose);
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
    // FrameBegin above drained the previous queue. Immutable texture inputs,
    // priority mask and VBO can now be updated once per engine, not per eye.
    static unsigned obj_samples[2]{},obj_draws[2]{},obj_affine[2]{},obj_vertices[2]{};
    static double obj_submit[2]{};
    uint64_t compose_tick=frame_profile::ticks();
    try{
        for(unsigned engine=0;engine<2;++engine)if(objects[engine] && objects[engine]->count){
            const double obj_started=v3ds_time_ms();
            {frame_profile::Scope s(31);g_resident_object_merge[engine].update(scenes[engine]);}
            if(!g_resident_objects[engine].draw(g_resident_object_layer[engine],*objects[engine])){
                shader_bind(false);end_gpu_frame();return false;
            }
            const auto stats=g_resident_objects[engine].draw_stats();
            obj_submit[engine]+=v3ds_time_ms()-obj_started;
            obj_draws[engine]+=stats.draws;obj_affine[engine]+=stats.affine;obj_vertices[engine]+=stats.vertices;
            if(++obj_samples[engine]==60){
                std::printf("Resident OBJ actual engine=%u draws=%.3f affine=%.3f vertices=%.3f prepareSubmitMs=%.6f decoded=%llu\n",
                    engine,double(obj_draws[engine])/60,double(obj_affine[engine])/60,double(obj_vertices[engine])/60,
                    obj_submit[engine]/60,(unsigned long long)g_resident_objects[engine].decoded_textures());
                obj_samples[engine]=obj_draws[engine]=obj_affine[engine]=obj_vertices[engine]=0;obj_submit[engine]=0;
            }
        }
    }catch(const std::bad_alloc&){shader_bind(false);end_gpu_frame();return false;}
#endif
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[27]+=now-compose_tick;compose_tick=now;}
    const auto upper=a_upper?a:other,lower=a_upper?other:a;
    auto& upper_compositor=g_resident_compositor[a_upper?0:1];
    auto& lower_compositor=g_resident_compositor[a_upper?1:0];
    // minigames-2d-gpu: engine A geometry (current, or previous frame's) and
    // engine B as a 1:1 view of a GPU-resident capture bank.
    ds_gpu::Surface* delayed=plan.delay_3d?ds_gpu::delayed_geometry_surface():nullptr;
    C3D_Tex& geometry_a=(delayed && delayed_valid)?delayed->texture:g_resident_geometry[0];
    ds_gpu::Surface* view_bank=plan.view?ds_gpu::capture_bank_surface(plan.view_bank):nullptr;
    if(plan.view && !view_bank){shader_bind(false);end_gpu_frame();g_present_refusal="capture";return false;}
    auto compose_panel=[&](ds_gpu::Composite& compositor,unsigned panel,const ntr::PpuPreparedCandidates& scene,bool engine_a,C3D_Tex& eye_geometry){
        if(!engine_a && plan.view)compositor.view_capture(g_resident_panels[panel],view_bank->texture,plan.view_master);
        else compositor.opaque(g_resident_panels[panel],scene,engine_a?geometry_a:eye_geometry);
    };
    compose_panel(upper_compositor,0,upper,a_upper,g_resident_geometry[0]);
    screen_scale::prepare_extension(upper,!plan.view && !upper.state.window_cut);
    {   // menu-stereo: a 2D-only upper screen gets layered depth (see prepare_layers).
        bool upper_objects=false;
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
        {const unsigned engine=a_upper?0u:1u;upper_objects=objects[engine] && objects[engine]->count;}
#endif
        screen_scale::prepare_layers(upper,(!plan.view && !upper.state.window_cut)?stereo_slider():0.f,upper_objects);
    }
    if(g_native_stereo)compose_panel(upper_compositor,1,upper,a_upper,g_resident_geometry[1]);
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[28]+=now-compose_tick;compose_tick=now;}
    compose_panel(lower_compositor,2,lower,!a_upper,g_resident_geometry[0]);
    if(plan.capture){
        // Source A is BG+OBJ+3D before master brightness: the panel itself
        // unless engine A applies master brightness.
        auto& a_compositor=g_resident_compositor[0];
        ds_gpu::Surface* bank=ds_gpu::capture_bank_surface(plan.capture_bank);
        const unsigned a_master=a.state.master>>14;
        C3D_Tex* source=&g_resident_panels[a_upper?0:2].texture;
        if(a_master==1 || a_master==2){
            ds_gpu::Surface* unmastered=ds_gpu::capture_scratch_surface();
            if(unmastered){a_compositor.opaque(*unmastered,a,geometry_a,true,false);source=&unmastered->texture;}
            else source=nullptr;
        }
        if(bank && source)a_compositor.capture_into(*bank,*source,true);
    }
    // Physical identity copy: the delayed texture keeps the geometry layout.
    if(delayed){g_resident_compositor[0].copy_resident(*delayed,g_resident_geometry[0]);delayed_valid=true;}
    else delayed_valid=false;
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[29]+=now-compose_tick;compose_tick=now;}
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
    const unsigned panel_engine[]={a_upper?0u:1u,a_upper?0u:1u,a_upper?1u:0u};
    for(unsigned panel=0;panel<3;++panel){
        if(panel==1 && !g_native_stereo)continue;
        const unsigned engine=panel_engine[panel];
        if(objects[engine] && objects[engine]->count)
            g_resident_object_merge[engine].compose(g_resident_compositor[engine],
                g_resident_panels[panel],g_resident_object_layer[engine].texture,scenes[engine].state);
    }
#endif
    // minigames-30fps-min: binary window cut (wipes) over the final panels.
    {
        const ntr::PpuPreparedCandidates* engine_scene[2]={&a,&other};
        for(unsigned panel=0;panel<3;++panel){
            if(panel==1 && !g_native_stereo)continue;
            const unsigned engine=(panel<2)==a_upper?0u:1u;
            if(engine==1 && plan.view)continue; // B shows a capture bank
            if(const auto* cut=engine_scene[engine]->state.window_cut)
                g_resident_compositor[engine].fill_cut(g_resident_panels[panel],*cut);
        }
    }
    {const uint64_t now=frame_profile::ticks();sm64ds_frame_prof[30]+=now-compose_tick;compose_tick=now;}
    shader_bind(false);C3D_SetAttrInfo(&g_attr);C3D_LightEnvBind(nullptr);
    set_fix(1,0,1,false);
    C3D_CullFace(GPU_CULL_NONE);C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    for(unsigned stage=0;stage<6;++stage)C3D_TexEnvInit(C3D_GetTexEnv(stage));
    auto* environment=C3D_GetTexEnv(0);
    C3D_TexEnvSrc(environment,C3D_Both,GPU_TEXTURE0);
    C3D_TexEnvFunc(environment,C3D_Both,GPU_REPLACE);
    C3D_RenderTarget* targets[]={g_left,g_right,g_resident_bottom};
    C3D_Tex* const panel_textures[]={&g_resident_panels[0].texture,&g_resident_panels[1].texture,&g_resident_panels[2].texture};
    if(screen_scale::g_layer_frame){
        // menu-stereo: depth classes into the upper panel's alpha.
        C3D_Tex* upper_objects=nullptr;
        screen_scale::layer_sprites_begin();
#ifdef SM64DS_NATIVE_OBJ_CANDIDATE
        {const unsigned engine=a_upper?0u:1u;
         if(objects[engine] && objects[engine]->count){
             upper_objects=&g_resident_object_layer[engine].texture;
             // Affine OBJ (title star, trail, sparkles): their own depth.
             const auto& plan=*objects[engine];
             for(unsigned i=0;i<plan.count;++i){const auto& sprite=plan.sprites[plan.order[i]];
                 if(sprite.affine)screen_scale::layer_sprite_add(sprite.x,sprite.y,int(sprite.bound_width),int(sprite.bound_height),sprite.priority);
                 static const bool trace=std::getenv("SM64DS_MENU_STEREO_TRACE")!=nullptr; // evidence: OAM 10 = title star
                 if(trace && sprite.index==10 && sprite.affine && screen_scale::g_layer_sprite_count)
                     std::printf("STEREO_STAR x=%d y=%d prio=%u bgPrio=%u depth=%.2f\n",sprite.x,sprite.y,sprite.priority,
                         screen_scale::g_layer_bg_priority,screen_scale::g_layer_sprites[screen_scale::g_layer_sprite_count-1].depth);}
         }}
#endif
        screen_scale::mark_layers(g_resident_panels[0],g_resident_panels[1],upper_objects);
        environment=C3D_GetTexEnv(0); // fresh pointer: marks the stage dirty again
        C3D_TexEnvInit(environment);
        C3D_TexEnvSrc(environment,C3D_Both,GPU_TEXTURE0);
        C3D_TexEnvFunc(environment,C3D_Both,GPU_REPLACE);
        gfxSet3D(true);
    }
    if(!screen_scale::draw(targets,panel_textures,g_native_stereo||screen_scale::g_layer_frame,true)){
    auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,g_resident_vertices,sizeof(V3dsVert),3,0x210);
    const bool layered=screen_scale::g_layer_frame; // menu-stereo: flat right eye = panel 0
    for(unsigned panel=0;panel<3;++panel){
        if(panel==1 && !g_native_stereo && !layered)continue;
        C3D_RenderTargetClear(targets[panel],C3D_CLEAR_ALL,0,0);
        C3D_FrameDrawOn(targets[panel]);C3D_TexBind(0,&g_resident_panels[layered&&panel==1?0:panel].texture);
        C3D_DrawArrays(GPU_TRIANGLES,panel*6,6);
    }
    }
    end_gpu_frame();tick_fps("Native scene","Resident candidate");
    static double wait_total=0,submit_total=0,gpu_total=0;static unsigned samples=0,counter_total[2]{};
    wait_total+=g_resident_wait;submit_total+=v3ds_time_ms()-started-g_resident_wait;
    gpu_total+=g_resident_previous_gpu;
    for(unsigned i=0;i<2;++i)counter_total[i]+=g_resident_vblanks[i];
    if(++samples==60){
        std::printf("Resident GPU parts ms: wait=%.6f submit=%.6f previousGPU=%.6f vblankWait=%u stereo=%u counterAdvance=%.3f/%.3f\n",
            wait_total/samples,submit_total/samples,gpu_total/samples,
            unsigned(!std::getenv("PLAYER_RESIDENT_NO_VBLANK_WAIT")),unsigned(g_native_stereo),
            double(counter_total[0])/samples,double(counter_total[1])/samples);
        wait_total=submit_total=gpu_total=0;samples=0;
        counter_total[0]=counter_total[1]=0;
    }
    return true;
}
bool v3ds_gpu_present_candidates(const V3dsVert* v,int nv,const V3dsBatch* b,int nb,
    ntr::PpuPreparedCandidates a,ntr::PpuPreparedCandidates other,bool a_upper,
    float convergence,bool perspective,bool stereo,const ds_gpu::ObjPlan* ao,const ds_gpu::ObjPlan* bo){
    return present_candidates_core(v,nv,b,nb,a,other,a_upper,convergence,perspective,stereo,ao,bo,0);
}
namespace v3ds_arena {
bool present_candidates(const ntr::gx_geometry::Prepared& p,
    ntr::PpuPreparedCandidates a,ntr::PpuPreparedCandidates b,bool a_upper,
    float convergence,bool perspective,bool stereo,const ds_gpu::ObjPlan* ao,const ds_gpu::ObjPlan* bo){
    if(!accepts(p))return false;
    if(!g_direct_committed){g_direct_prepared=p;g_direct_committed=true;}
    const bool result=present_candidates_core(p.vertices,int(p.vertex_count),p.batches,int(p.batch_count),
        a,b,a_upper,convergence,perspective,stereo,ao,bo,p.frame_token);
    if(!result && g_direct_open)end_gpu_frame();
    return result;
}
}
void v3ds_gpu_candidate_geometry(uint32_t* left,uint32_t* right){
    if(!g_resident_ready)return;
    if(!g_resident_diagnostic)g_resident_diagnostic=static_cast<uint32_t*>(linearAlloc(256*256*4));
    if(!g_resident_diagnostic)return;
    for(unsigned eye=0;eye<(g_native_stereo?2u:1u);++eye){
        C3D_SyncDisplayTransfer(static_cast<u32*>(g_resident_geometry[eye].data),GX_BUFFER_DIM(256,256),
            g_resident_diagnostic,GX_BUFFER_DIM(256,256),DISPLAY_TRANSFER_FLAGS&~GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
        GSPGPU_InvalidateDataCache(g_resident_diagnostic,256*256*4);
        auto* output=eye?right:left;
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
            output[y*256+x]=decode_native_pixel(g_resident_diagnostic[(255-y)*256+x]);
    }
    if(!g_native_stereo)std::memcpy(right,left,256*192*4);
}
void v3ds_gpu_candidate_panel(unsigned panel,uint32_t* output){
    if(panel>=3 || !g_resident_ready)return;
    if(!g_resident_diagnostic)g_resident_diagnostic=static_cast<uint32_t*>(linearAlloc(256*256*4));
    if(g_resident_diagnostic)ds_gpu::Composite::readback(g_resident_panels[panel],output,g_resident_diagnostic);
}
#if defined(SM64DS_RESIDENT_LIFETIME_DIAGNOSTICS) && defined(SM64DS_NATIVE_OBJ_CANDIDATE)
void v3ds_gpu_candidate_reset(){
    if(g_direct_open && !v3ds_arena::abort(g_direct_lease.frame_token))return;
    g_direct_valid=false;
    for(unsigned engine=0;engine<2;++engine)if(g_resident_objects_ready[engine]){
        if(!C3D_FrameBegin(0))return;
        static const ds_gpu::ObjPlan empty;
        g_resident_objects[engine].draw(g_resident_object_layer[engine],empty);
        end_gpu_frame();break;
    }
    resident_destroy();
}
#endif
#endif
static bool present_gpu_blit(const uint32_t* left,const uint32_t* right,const uint32_t* bottom,bool stereo){
    const bool profile=std::getenv("PLAYER_PROFILE_BLIT");
    double phase[4]{};if(profile)phase[0]=v3ds_time_ms();
    if(!g_blit_ready){
        unsigned initialized=0;
        for(auto& texture:g_blit_textures){
            if(!C3D_TexInit(&texture,256,256,GPU_RGBA8)){
                for(unsigned i=0;i<initialized;++i)C3D_TexDelete(&g_blit_textures[i]);
                return false;
            }
            ++initialized;C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);
            C3D_TexSetWrap(&texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
        }
        g_blit_bottom=C3D_RenderTargetCreate(240,320,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
        g_blit_vertices=static_cast<V3dsVert*>(linearAlloc(18*sizeof(V3dsVert)));
        if(!g_blit_bottom || !g_blit_vertices){
            for(auto& texture:g_blit_textures)C3D_TexDelete(&texture);
            if(g_blit_bottom)C3D_RenderTargetDelete(g_blit_bottom);
            if(g_blit_vertices)linearFree(g_blit_vertices);
            g_blit_bottom=nullptr;g_blit_vertices=nullptr;return false;
        }
        C3D_RenderTargetSetOutput(g_blit_bottom,GFX_BOTTOM,GFX_LEFT,DISPLAY_TRANSFER_FLAGS);
        g_blit_ready=true;
    }
    if(!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))return false;
    if(profile)phase[1]=v3ds_time_ms();
    static uint16_t tiled_index[256*192];static bool indexed=false;
    if(!indexed){
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
            const unsigned py=255-y;
            tiled_index[y*256+x]=((py>>3)*32+(x>>3))*64+morton8(x&7,py&7);
        }
        indexed=true;
    }
    const uint32_t* images[]={left,right,bottom};
    for(unsigned panel=0;panel<3;++panel){
        if(panel==1 && !stereo)continue;
        auto* dst=static_cast<uint32_t*>(g_blit_textures[panel].data);
        if(!std::getenv("PLAYER_TILED_BLIT")){
            for(unsigned i=0;i<256*192;++i){const auto color=images[panel][i];dst[tiled_index[i]]=(color<<8)|(color>>24);}
        }else upload_native_image(dst,images[panel]);
        C3D_TexFlush(&g_blit_textures[panel]);
        const float width=panel==2?320.f:400.f;
        // Match integer nearest-neighbour CPU coverage, not pixel-centre
        // resampling. The tiny positive bias stabilizes exact texel boundaries.
        const float u0=-.5f/width+1.f/65536.f,v0=-.5f*.75f/240.f+1.f/65536.f;
        V3dsVert q[4]={{-1,1,0,1,1,1,1,1,u0,v0},{1,1,0,1,1,1,1,1,u0+1,v0},
            {1,-1,0,1,1,1,1,1,u0+1,v0+.75f},{-1,-1,0,1,1,1,1,1,u0,v0+.75f}};
        const unsigned indices[]={0,1,2,0,2,3};
        for(unsigned i=0;i<6;++i)g_blit_vertices[panel*6+i]=q[indices[i]];
    }
    if(profile)phase[2]=v3ds_time_ms();
    GSPGPU_FlushDataCache(g_blit_vertices,18*sizeof(V3dsVert));
    C3D_BufInfo* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);
    BufInfo_Add(buffers,g_blit_vertices,sizeof(V3dsVert),3,0x210);
    shader_bind(false);C3D_SetAttrInfo(&g_attr);set_fix(1,0,1,false);
    C3D_CullFace(GPU_CULL_NONE);C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0xff);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    auto* environment=C3D_GetTexEnv(0);C3D_TexEnvInit(environment);
    C3D_TexEnvSrc(environment,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(environment,C3D_Both,GPU_REPLACE);
    C3D_RenderTarget* targets[]={g_left,g_right,g_blit_bottom};
    C3D_Tex* const blit_sources[]={&g_blit_textures[0],&g_blit_textures[1],&g_blit_textures[2]};
    if(!screen_scale::draw(targets,blit_sources,stereo,false))
    for(unsigned panel=0;panel<3;++panel){
        if(panel==1 && !stereo)continue;
        C3D_RenderTargetClear(targets[panel],C3D_CLEAR_ALL,0,0);C3D_FrameDrawOn(targets[panel]);
        C3D_TexBind(0,&g_blit_textures[panel]);C3D_DrawArrays(GPU_TRIANGLES,panel*6,6);
    }
    end_gpu_frame();tick_fps("Native scene","GPU blit");
    if(profile){
        phase[3]=v3ds_time_ms();
        static double totals[3]{};static unsigned samples=0;
        for(unsigned i=0;i<3;++i)totals[i]+=phase[i+1]-phase[i];
        if(++samples==60){
            std::printf("Native blit parts ms: wait=%.3f upload=%.3f submit=%.3f\n",
                totals[0]/samples,totals[1]/samples,totals[2]/samples);
            samples=0;for(auto& value:totals)value=0;
        }
    }
    return true;
}

bool v3ds_native_stereo(){return g_native_stereo;}
void v3ds_set_menu_presentation(bool active){g_menu_presentation.store(active,std::memory_order_relaxed);}
void v3ds_present_native(const uint32_t* upper_left,const uint32_t* upper_right,
                        const uint32_t* lower,bool stereo){
    gfxSet3D(stereo&&g_native_stereo);
    if(std::getenv("PLAYER_GPU_BLIT") && present_gpu_blit(upper_left,upper_right,lower,stereo&&g_native_stereo))return;
    auto blit=[](gfxScreen_t screen,gfx3dSide_t side,const uint32_t* image){
        u16 height,width;auto* dst=gfxGetFramebuffer(screen,side,&height,&width);
        // Both LCDs have 240 rows. Resolve the nearest-neighbour source maps
        // once instead of doing two software integer divisions per pixel.
        static unsigned source_y[240],source_x[3][400];
        static bool mapped=false;
        if(!mapped){
            for(unsigned y=0;y<240;++y)source_y[y]=(y*192/240)*256;
            for(unsigned x=0;x<400;++x)source_x[0][x]=x*256/400;
            for(unsigned x=0;x<320;++x)source_x[1][x]=x*256/320;
            // DS layout: same 1.25 scale as the lower LCD, centred (40-pixel
            // black bars). ~0u marks a bar column.
            for(unsigned x=0;x<400;++x)source_x[2][x]=(x>=40 && x<360)?(x-40)*256/320:~0u;
            mapped=true;
        }
        if(screen==GFX_TOP){static int last=-1;const int state=screen_scale::ds_layout();
            if(state!=last){last=state;std::printf("Screen layout CPU %s\n",state?"DS 4:3 (black bars)":"stretched");}}
        const auto* columns=source_x[screen!=GFX_TOP?1:screen_scale::ds_layout()?2:0];
        for(unsigned x=0;x<width;++x){
            auto* pixel=dst+(x*height+height-1)*3;
            if(columns[x]==~0u){std::memset(dst+x*height*3,0,height*3);continue;}
            for(unsigned y=0;y<height;++y){
                const auto color=image[source_y[y]+columns[x]];
                pixel[0]=color;pixel[1]=color>>8;pixel[2]=color>>16;
                if(y+1<height)pixel-=3;
            }
        }
    };
    blit(GFX_TOP,GFX_LEFT,upper_left);
    if(stereo&&g_native_stereo)blit(GFX_TOP,GFX_RIGHT,upper_right);
    blit(GFX_BOTTOM,GFX_LEFT,lower);
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    tick_fps("Native scene","PPU");
}

// Top framebuffer after the display transfer: 240x400 BGR8, column-major.
void v3ds_read_top(uint32_t *out400x240, bool right)
{
    u16 fw, fh;
    const u8 *src = gfxGetFramebuffer(GFX_TOP, right ? GFX_RIGHT : GFX_LEFT, &fw, &fh);
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 400; ++x) {
            const u8 *p = src + (x * fw + (fw - 1 - y)) * 3;
            out400x240[y * 400 + x] = 0xFF000000u | p[2] << 16 | p[1] << 8 | p[0];
        }
}

void v3ds_read_bottom(uint32_t* output){
    u16 height,width;const auto* src=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&height,&width);
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){
        const auto* p=src+(x*height+height-1-y)*3;
        output[y*320+x]=0xff000000u|(p[2]<<16)|(p[1]<<8)|p[0];
    }
}

V3dsPad v3ds_pad()
{
    V3dsPad p{};
    if (!aptMainLoop()) { p.down = V3DS_QUIT; return p; }
    hidScanInput();
    auto map = [](u32 k) {
        int r = 0;
        if (k & KEY_START) r |= V3DS_QUIT;
        if (k & KEY_A) r |= V3DS_A;
        if (k & KEY_X) r |= V3DS_X;
        if (k & KEY_L) r |= V3DS_L;
        if (k & KEY_R) r |= V3DS_R;
        if (k & KEY_SELECT) r |= V3DS_SELECT;
        if (k & KEY_DUP) r |= V3DS_UP;
        if (k & KEY_DDOWN) r |= V3DS_DOWN;
        if (k & KEY_DLEFT) r |= V3DS_LEFT;
        if (k & KEY_DRIGHT) r |= V3DS_RIGHT;
        return r;
    };
    p.down = map(hidKeysDown());
    p.held = map(hidKeysHeld());
    circlePosition c;
    hidCircleRead(&c);
    p.cx = c.dx / 156.0f;
    p.cy = c.dy / 156.0f;
    return p;
}

int v3ds_input()
{
    if (!aptMainLoop()) return V3DS_QUIT;
    hidScanInput();
    const u32 k = hidKeysDown();
    int r = 0;
    if (k & KEY_START) r |= V3DS_QUIT;
    if (k & KEY_A) r |= V3DS_A;
    if (k & KEY_L) r |= V3DS_L;
    if (k & KEY_R) r |= V3DS_R;
    if (k & KEY_SELECT) r |= V3DS_SELECT;
    return r;
}

bool v3ds_auto()
{
    if (access("sdmc:/sm64ds/autoexit", F_OK) != 0) return false;
    freopen("sdmc:/sm64ds/log.txt", "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
    stderr = stdout;
    g_auto = true;
    return true;
}

void v3ds_log(const char *msg) { printf("\x1b[12;1H%s\n", msg); }


double v3ds_time_ms()
{
    return svcGetSystemTick() * (1000.0 / SYSCLOCK_ARM11);
}

void v3ds_profile(double game_ms, double batch_ms)
{
    if (!getenv("SM64DS_PROFILE")) return;
    static unsigned count;
    static double game, batch, cpu, gpu;
    game += game_ms;
    batch += batch_ms;
    cpu += C3D_GetProcessingTime();
    gpu += C3D_GetDrawingTime();
    if (++count == 60) {
        printf("profile frames=60 game_ms=%.3f batch_ms=%.3f submit_ms=%.3f gpu_ms=%.3f\n",
               game / count, batch / count, cpu / count, gpu / count);
        count = 0;
        game = batch = cpu = gpu = 0;
    }
}

bool v3ds_native_mesh_geometry(const native_mesh::Packet& packet,uint32_t* left,uint32_t* right,float convergence,bool stereo){
 if(!mixed_prepare(packet))return false;mesh_active=true;
 const bool result=gpu_frame(nullptr,int(mesh_frame->topology->positions.size()),mesh_batches.data(),int(mesh_batches.size()),
  "Native mesh gate","PICA",1.f,convergence,true,stereo,left,right);
 if(!result)g_held_raster.invalidate();mesh_active=false;return result;
}
bool v3ds_native_mesh_present(const native_mesh::Packet& packet,const ntr::PpuPreparedCandidates& a,
 const ntr::PpuPreparedCandidates& b,bool a_upper,float convergence,bool stereo,
 const ds_gpu::ObjPlan* oa,const ds_gpu::ObjPlan* ob){
 if(!a_upper||!a.state.has_geometry||b.state.has_geometry||!packet.admitted())return false;
 for(const auto* state:{&a.state,&b.state})if(state->has_windows||state->has_obj_alpha||
   (state->has_geometry&&(state->blend&0x3f00)))return false;
 if(!mixed_prepare(packet))return false;mesh_active=true;
 const bool result=present_candidates_core(nullptr,int(mesh_frame->topology->positions.size()),mesh_batches.data(),int(mesh_batches.size()),
  a,b,a_upper,convergence,true,stereo,oa,ob,0);
 if(!result)g_held_raster.invalidate();mesh_active=false;return result;
}
bool v3ds_prepared_mesh_geometry(std::shared_ptr<const prepared_mesh::Frame> frame,uint32_t* left,uint32_t* right,float convergence,bool stereo){
 if(!mixed_prepare(std::move(frame)))return false;mesh_active=true;
 const bool result=gpu_frame(nullptr,int(mesh_frame->topology->positions.size()),mesh_batches.data(),int(mesh_batches.size()),
  "Prepared mesh gate","PICA",1.f,convergence,true,stereo,left,right);
 if(!result)g_held_raster.invalidate();mesh_active=false;return result;
}
static uint64_t prepared_refuse_display=0,prepared_refuse_blend=0,prepared_refuse_mixed=0,prepared_refuse_commit=0;
// The PPU-state admission of v3ds_prepared_mesh_present, without side effects,
// so the pipelined caller can route refused states to the synchronous path.
// castle-30fps: window masks are applied by the resident composition only
// (Composite::opaque), independently of how the geometry was rasterized, so
// windowed scenes (dialog boxes: WIN0 + brightness effect) no longer force
// the synchronous direct-geometry path. SM64DS_MESH_NO_WINDOWS=1 restores it.
static bool mesh_windows_refused(){static const bool value=std::getenv("SM64DS_MESH_NO_WINDOWS")!=nullptr;return value;}
bool v3ds_prepared_mesh_admits(const ntr::PpuPreparedCandidates& a,const ntr::PpuPreparedCandidates& b,bool a_upper){
 if(!a_upper||!a.state.has_geometry||b.state.has_geometry)return false;
 // Windows only on the geometry engine (dialog boxes); the composition must
 // also accept the effects, or the worker would refuse the frame later.
 if(b.state.has_windows || ((a.state.has_windows||b.state.has_windows)&&mesh_windows_refused()))return false;
 // minigames-30fps-min: the 2D-over-3D alpha blend (state.bg_first_blend,
 // game intro letter) only reads the geometry target like the direct path;
 // SM64DS_MESH_NO_BG_FIRST=1 keeps such frames on the direct path.
 static const bool bg_first=std::getenv("SM64DS_MESH_NO_BG_FIRST")==nullptr;
 for(const auto* state:{&a.state,&b.state}){
  if(bg_first && state->bg_first_blend && !state->has_obj_alpha){if(!ds_gpu::composite_blend_free(*state))return false;continue;}
  if(state->has_obj_alpha||(state->has_geometry&&(state->blend&0x3f00))||!ds_gpu::composite_blend_free(*state))return false;
 }
 return true;
}
bool v3ds_prepared_mesh_present(std::shared_ptr<const prepared_mesh::Frame> frame,const ntr::PpuPreparedCandidates& a,
 const ntr::PpuPreparedCandidates& b,bool a_upper,float convergence,bool stereo,
 const ds_gpu::ObjPlan* oa,const ds_gpu::ObjPlan* ob){
 if(!a_upper||!a.state.has_geometry||b.state.has_geometry){++prepared_refuse_display;return false;}
 if(!v3ds_prepared_mesh_admits(a,b,a_upper)){++prepared_refuse_blend;g_present_refusal="admits";return false;}
 if(!mixed_prepare(std::move(frame))){++prepared_refuse_mixed;g_present_refusal="mixed";return false;}mesh_active=true;
 const bool result=present_candidates_core(nullptr,int(mesh_frame->topology->positions.size()),mesh_batches.data(),int(mesh_batches.size()),
  a,b,a_upper,convergence,true,stereo,oa,ob,0);
 if(!result){++prepared_refuse_commit;g_held_raster.invalidate();}
 if(!result)g_held_raster.invalidate();mesh_active=false;return result;
}
extern "C" void gx_gpu_ingame_stats();
extern "C" void native_mesh_print_stats(){
 std::printf("GPU command peak bytes=%u capacity=%u\n",unsigned(command_peak_bytes),unsigned(command_capacity_bytes));
 if(gx_gpu_ingame::options()){gx_gpu_ingame_stats();std::printf("GX GPU ingame envUploads=%llu\n",(unsigned long long)mesh_env_uploads);}
 std::printf("Unified shader enabled=%u binds=%llu modeSets=%llu paletteHits=%llu preparedRefuseDisplay=%llu blend=%llu mixed=%llu commit=%llu\n",unsigned(unified_shader_enabled()),(unsigned long long)unified_program_binds,(unsigned long long)unified_mode_sets,(unsigned long long)unified_palette_hits,(unsigned long long)prepared_refuse_display,(unsigned long long)prepared_refuse_blend,(unsigned long long)prepared_refuse_mixed,(unsigned long long)prepared_refuse_commit);
 std::printf("Mixed static clip plan builds=%llu hits=%llu enabled=%u\n",(unsigned long long)mixed_builder.static_plan_builds,(unsigned long long)mixed_builder.static_plan_hits,unsigned(std::getenv("PLAYER_MIXED_STATIC_CLIP_PLAN")&&std::atoi(std::getenv("PLAYER_MIXED_STATIC_CLIP_PLAN"))!=0));
 std::printf("Mixed partition builds=%llu held=%llu nativeDraws=%llu cpuDraws=%llu classifierMs=%.9f cpuUploadBytes=%llu inside=%llu crossing=%llu rejected=%llu nonpositiveW=%llu\n",(unsigned long long)mixed_builds,(unsigned long long)mixed_held,(unsigned long long)mixed_native_draws,(unsigned long long)mixed_cpu_draws,mixed_classifier_ms,(unsigned long long)mixed_upload_bytes,(unsigned long long)mixed_inside,(unsigned long long)mixed_crossing,(unsigned long long)mixed_rejected,(unsigned long long)mixed_nonpositive);
 std::printf("Mixed whole-run certificate runs=%llu triangles=%llu\n",(unsigned long long)mixed_bulk_runs,(unsigned long long)mixed_bulk_triangles);
 std::printf("Held raster enabled=%u hits=%llu misses=%llu\n",unsigned(held_raster_enabled()),(unsigned long long)g_held_raster_hits,(unsigned long long)g_held_raster_misses);
 std::printf("Native mesh totals frames=%llu builds=%llu reuses=%llu staticHits=%llu staticMisses=%llu positionsBytes=%llu indicesBytes=%llu dynamicBytes=%llu uniformVectors=%llu draws=%llu runs=%llu sourceVertices=%llu sourceTriangles=%llu matrices=%llu drainMs=%.9f prepareMs=%.9f uploadMs=%.9f\n",
 (unsigned long long)mesh_stats.frames,(unsigned long long)mesh_stats.builds,(unsigned long long)mesh_stats.reuses,(unsigned long long)mesh_stats.static_hits,(unsigned long long)mesh_stats.static_misses,
 (unsigned long long)mesh_stats.position_bytes,(unsigned long long)mesh_stats.index_bytes,(unsigned long long)mesh_stats.dynamic_bytes,(unsigned long long)mesh_stats.uniform_vectors,(unsigned long long)mesh_stats.draws,(unsigned long long)mesh_stats.runs,
 (unsigned long long)mesh_stats.vertices,(unsigned long long)mesh_stats.triangles,(unsigned long long)mesh_stats.matrices,mesh_stats.drain_ms,mesh_stats.prepare_ms,mesh_stats.upload_ms);
}
