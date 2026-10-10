// sm64ds_viewer: the game's model + animation pipeline, live on the 3DS top
// screen. Same seam as tests/smoke_frames.cpp -- the game poses, skins and
// submits its display list on the game fiber; the host fiber rasters it -- but
// instead of BMPs every frame is blitted to the top framebuffer.
//
//   A        next model (Mario / Mad Piano)
//   L / R    previous / next animation
//   START    quit
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include "viewer_3ds.h"
#include "ds_gpu_viewport.h"
#include "viewer_clsn.h"

#include "Model.h"
#include "Animation.h"
#include "TextureSequence.h"

#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "ntr/ppu.h"
#include "ntr/rt.h"

extern "C" {
void _ZN5ModelC1Ev(void *self);
struct SharedFilePtrC { u16 fileID; u8 numRefs; void *filePtr; };
SharedFilePtrC *_ZN13SharedFilePtr9ConstructEj(SharedFilePtrC *self, u32 ov0FileID);
void *_ZN4Heap13SetupRootHeapEv(void);
void _ZN15ModelComponents11UpdateBonesEP8BCA_Filei(void *components, void *bca, int frame);
void _ZN15ModelComponents21UpdateVertsUsingBonesEv(void *components);
void func_02046d50(void *bmd, void *btp);                    // TextureSequence::Prepare body
void func_02046bbc(void *components, void *btp, int frame);  // TextureSequence::Update body
extern Matrix4x3 data_0209b3ec;
}

// ---- scenes: ov0 handles from build/assets/handles.tsv ----------------------
struct Clip { u32 handle; const char *name; };
struct Scene {
    u32 model;
    u32 head;                // separate head model, 0 = none (see HEAD_BONE)
    u32 btp;                 // head texture-pattern animation (eyes, faces), 0 = none
    // Player-state BTPs, held at frame 0 (mPlayerTexFrame = 0: normal, 1 =
    // vanish-cap dither). Luigi's BMD materials default to the dithered
    // variant, so without these his head and body look like noise.
    u32 head_state_btp, body_state_btp;
    u32 head_bca;            // pose for a skinned head (Yoshi's mouth/tongue), 0 = bind pose
    const char *name;
    Clip clips[4];
    int nclips;
};
static const Scene kScenes[] = {
    // archive members (ar0): the in-game player models and gameplay animations
    {0x8083, 0x8081, 0x80b2, 0, 0, 0, "Mario", {{0x80b3, "wait"}, {0x80b4, "walk"}, {0x8091, "run"}, {0x8078, "jump"}}, 4},
    {0x8080, 0x807e, 0x8028, 0x8025, 0x8022, 0, "Luigi", {{0x8029, "L_wait"}, {0x802a, "L_walk"}, {0x8020, "L_run"}}, 3},
    {0x80ba, 0x80b8, 0x803a, 0, 0, 0, "Wario", {{0x803b, "W_wait"}, {0x803c, "W_walk"}, {0x8034, "W_run"}}, 3},
    {0x80c4, 0x80c2, 0x804f, 0, 0, 0x8043, "Yoshi", {{0x8050, "Y_wait"}, {0x8051, "Y_walk"}, {0x8048, "Y_run"}}, 3},
    {1034, 0, 0, 0, 0, 0, "Mad Piano", {{1036, "piano_attack"}}, 1},
};
enum { NSCENES = sizeof kScenes / sizeof kScenes[0] };

// The player skeletons put the head on bone 15 ("face_1"); the game draws the
// head as its own model on that bone's matrix (cap / no-cap variants swap).
enum { HEAD_BONE = 15 };

struct Loaded {
    alignas(8) char storage[0x50];
    alignas(8) char head_storage[0x50];
    Model *model;
    Model *head;
    void *btp;
    void *bca[4];
};
static Loaded g_loaded[NSCENES];

// Shared between the two fibers; only one runs at a time.
static int g_scene, g_clip;
static bool g_auto;   // headless capture run (sdmc:/sm64ds/autoexit)
static double g_game_ms;
static int g_frame;   // advanced by game_main; drives BTP playback
static int32_t g_fit[16];
static bool g_refit = true;

static void ident_fx(void *m)
{
    memset(m, 0, 48);
    ((int *)m)[0] = ((int *)m)[4] = ((int *)m)[8] = 0x1000;
}

static void *load(u32 handle)
{
    static SharedFilePtrC ptrs[16];
    static int n;
    SharedFilePtrC *p = &ptrs[n++ & 15];
    _ZN13SharedFilePtr9ConstructEj(p, handle);
    return p;
}

static void load_scenes()
{
    for (int s = 0; s < NSCENES; ++s) {
        Loaded &L = g_loaded[s];
        L.model = (Model *)L.storage;
        _ZN5ModelC1Ev(L.storage);
        void *file = Model::LoadFile(*(SharedFilePtr *)load(kScenes[s].model));
        L.model->Model::DoSetFile((char *)file, 0, -1);
        ident_fx(&L.model->mat4x3);
        if (kScenes[s].body_state_btp) {
            void *b = TextureSequence::LoadFile(*(SharedFilePtr *)load(kScenes[s].body_state_btp));
            func_02046d50(file, b);
            func_02046bbc(&L.model->data, b, 0);
        }
        if (kScenes[s].head) {
            L.head = (Model *)L.head_storage;
            _ZN5ModelC1Ev(L.head_storage);
            void *hf = Model::LoadFile(*(SharedFilePtr *)load(kScenes[s].head));
            L.head->Model::DoSetFile((char *)hf, 0, -1);
            ident_fx(&L.head->mat4x3);
            if (kScenes[s].head_state_btp) {
                void *b = TextureSequence::LoadFile(*(SharedFilePtr *)load(kScenes[s].head_state_btp));
                func_02046d50(hf, b);
                func_02046bbc(&L.head->data, b, 0);
            }
            if (kScenes[s].head_bca) {
                void *a = Animation::LoadFile(*(SharedFilePtr *)load(kScenes[s].head_bca));
                _ZN15ModelComponents11UpdateBonesEP8BCA_Filei(&L.head->data, a, 0);
                _ZN15ModelComponents21UpdateVertsUsingBonesEv(&L.head->data);
            }
            if (kScenes[s].btp) {
                L.btp = TextureSequence::LoadFile(*(SharedFilePtr *)load(kScenes[s].btp));
                func_02046d50(hf, L.btp);   // bind the BTP's texture names to this head
            }
        }
        for (int c = 0; c < kScenes[s].nclips; ++c)
            L.bca[c] = Animation::LoadFile(*(SharedFilePtr *)load(kScenes[s].clips[c].handle));
    }
}

static void scene_setup(const int32_t *proj)
{
    ntr::gx_reset();
    NTR_MMIO(uint32_t, 0x04000440) = 0;
    for (int i = 0; i < 16; ++i)
        NTR_MMIO(uint32_t, 0x04000458) = (uint32_t)proj[i];
    NTR_MMIO(uint32_t, 0x04000440) = 1;
    NTR_MMIO(uint32_t, 0x04000454) = 0;
    NTR_MMIO(uint32_t, 0x04000580) = 0u | (0u << 8) | (255u << 16) | (191u << 24);
    ntr::gx_set_light(0, -0.4f, -0.6f, -0.7f, 0x7FFF);
    ntr::gx_enable_lights(0x1);
}

static int clip_frames(void *bca) { return ((const unsigned short *)bca)[1]; }

// Body, then the head on the body's head-bone matrix. The per-bone model-space
// matrices live in the ModelComponents transforms buffer (Matrix4x3, fx12).
static void render_scene()
{
    Loaded &L = g_loaded[g_scene];
    L.model->Model::Render(NULL);
    if (L.head) {
        const Matrix4x3 *xf = *((Matrix4x3 **)&L.model->data + 3);
        L.head->mat4x3 = xf[HEAD_BONE];
        // Compose the head bone with the complete actor matrix. Native DS
        // actors also rotate; using only bm[0] turns a 180-degree facing into
        // a negative uniform scale and flips the head through the floor.
        int *hm = (int *)&L.head->mat4x3;
        const int *bm = (const int *)&L.model->mat4x3;
        int composed[12];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c) {
                long long sum = 0;
                for (int k = 0; k < 3; ++k) sum += (long long)hm[r*3+k] * bm[k*3+c];
                composed[r*3+c] = (int)(sum >> 12) + (r == 3 ? bm[9+c] : 0);
            }
        memcpy(hm, composed, sizeof composed);
        if (L.btp) {
            const int n = ((const unsigned short *)L.btp)[0];   // BTP numFrames
            int *mats = *((int **)&L.head->data + 1);   // runtime material table, 0x30 each
            const int before = mats ? mats[12 + 7] : 0;  // material 1 (+0x30), TEXIMAGE at +0x1c
            func_02046bbc(&L.head->data, L.btp, n ? g_frame % n : 0);
            if (getenv("SM64DS_TRACE_BTP") && g_frame < 3)
                printf("btp %s f%d n%d mat1 teximage %08x -> %08x pltt %04x\n", kScenes[g_scene].name,
                       g_frame, n, before, mats ? mats[12 + 7] : 0, mats ? mats[12 + 8] : 0);
        }
        L.head->Model::Render(NULL);
    }
}

static void pose(int frame)
{
    Loaded &L = g_loaded[g_scene];
    void *bca = L.bca[g_clip];
    _ZN15ModelComponents11UpdateBonesEP8BCA_Filei(&L.model->data, bca, frame % clip_frames(bca));
    _ZN15ModelComponents21UpdateVertsUsingBonesEv(&L.model->data);
}

// Fit an orthographic-style projection to the union of the clip's poses, so
// the whole motion stays on screen without re-centering every frame.
static void refit()
{
    static const int32_t ident[16] = {4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096};
    float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
    float minz = 1e30f, maxz = -1e30f;
    const int n = clip_frames(g_loaded[g_scene].bca[g_clip]);
    for (int f = 0; f < n; f += (n > 16 ? n / 16 : 1)) {
        pose(f);
        scene_setup(ident);
        render_scene();
        size_t nt = 0;
        const ntr::GxTriangle *t = ntr::gx_polygons(nt);
        for (size_t i = 0; i < nt; ++i)
            for (int v = 0; v < 3; ++v) {
                minx = fminf(minx, t[i].v[v].x); maxx = fmaxf(maxx, t[i].v[v].x);
                miny = fminf(miny, t[i].v[v].y); maxy = fmaxf(maxy, t[i].v[v].y);
                minz = fminf(minz, t[i].v[v].cz); maxz = fmaxf(maxz, t[i].v[v].cz);
            }
    }
    const float cx0 = 2.0f * minx / ntr::SCREEN_W - 1.0f, cx1 = 2.0f * maxx / ntr::SCREEN_W - 1.0f;
    const float cy0 = 1.0f - 2.0f * maxy / ntr::SCREEN_H, cy1 = 1.0f - 2.0f * miny / ntr::SCREEN_H;
    const float w = fmaxf(cx1 - cx0, 1e-6f), h = fmaxf(cy1 - cy0, 1e-6f);
    const float s = 0.9f * fminf(2.0f / w, 2.0f / h);
    const float tx = -0.5f * (cx0 + cx1) * s, ty = -0.5f * (cy0 + cy1) * s;
    // Depth into [-0.9, 0.9]: a GPU clips outside [-w, w] (the CPU raster never
    // did). Model z under the identity pass is clip z; flip so nearer = smaller.
    const float sz = 1.8f / fmaxf(maxz - minz, 1e-6f);
    const float tz = 0.5f * (minz + maxz) * sz;
    const int32_t m[16] = {(int32_t)(s * 4096), 0, 0, 0,
                           0, (int32_t)(s * 4096), 0, 0,
                           0, 0, (int32_t)(-sz * 4096), 0,   // view down -Z: nearer = smaller depth
                           (int32_t)(tx * 4096), (int32_t)(ty * 4096), (int32_t)(tz * 4096), 4096};
    memcpy(g_fit, m, sizeof m);
}

// ---- levels: stage geometry under a real perspective camera ------------------
struct Level { u32 model; u32 kcl; const char *name; };
static const Level kLevels[] = {
    {1765, 1763, "Bob-omb Battlefield"},
    {1943, 1941, "Castle grounds"},
    {1776, 0, "Castle 1F"},
};
enum { NLEVELS = sizeof kLevels / sizeof kLevels[0] };

static bool g_cpu_raster;
static std::vector<ntr::GxTriangle> g_level_geometry;
static std::map<const uint32_t *, std::vector<uint32_t>> g_level_textures;
static bool g_level_mode;
static int g_level;
static Model *g_level_models[NLEVELS];
alignas(8) static char g_level_storage[NLEVELS][0x50];
static float g_cam_yaw = 0.6f, g_cam_pitch = 0.45f, g_cam_dist = 1.0f;
static float g_lvl_center[3], g_lvl_radius = 1.0f;
static bool g_level_measured;
static float g_convergence = 1.0f;

// Stage::RenderModel passes data_020755d4 = {0x7d000,0x7d000,0x7d000}
// (125.0 Fix12) to Model::Render; this viewer renders the stage with NULL scale.
// Player's model translation uses actor world position >> 3 (world / 8).
// Therefore world -> viewer model is / (8 * 125), and actor meshes are / 125.
// KCL's file coordinates are world / 64; viewer_clsn converts its box to world.
static const float WORLD_PER_MODEL = 1000.0f;
static const float MARIO_IN_LEVEL_SCALE = 1.0f / 125.0f;
static bool g_reference_ds;
static bool g_reference_mario;
static int g_level_character;
static bool g_mario_in_level;
static float g_mario_pos[3];     // model units

static void load_levels()
{
    for (int i = 0; i < NLEVELS; ++i) {
        g_level_models[i] = (Model *)g_level_storage[i];
        _ZN5ModelC1Ev(g_level_storage[i]);
        void *f = Model::LoadFile(*(SharedFilePtr *)load(kLevels[i].model));
        g_level_models[i]->Model::DoSetFile((char *)f, 0, -1);
        ident_fx(&g_level_models[i]->mat4x3);
    }
}

// Row-vector 4x4 helpers (the DS convention: v' = v * M).
static void mat_mul(const float *a, const float *b, float *o)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += a[r * 4 + k] * b[k * 4 + c];
            o[r * 4 + c] = v;
        }
}

// Bounding sphere of the level from one identity-projection pass.
static void measure_level()
{
    // A stage without a collider must not inherit the previous stage spawn.
    g_mario_in_level = false;
    g_level_character = (g_reference_ds && !g_reference_mario && g_level == 1) ? 3 : 0;
    static const int32_t ident[16] = {4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096};
    scene_setup(ident);
    g_level_models[g_level]->Model::Render(NULL);
    size_t n = 0;
    const ntr::GxTriangle *t = ntr::gx_polygons(n);
    // The viewer stages are static. Retain their world-space geometry so
    // camera motion does not decode every stage display list again.
    g_level_geometry.assign(t, t + n);
    // Own retained texels: the decoder is allowed to evict its texture cache.
    g_level_textures.clear();
    for (auto &tri : g_level_geometry) {
        if (!tri.tex) continue;
        auto &pixels = g_level_textures[tri.tex];
        if (pixels.empty()) pixels.assign(tri.tex, tri.tex + tri.tw * tri.th);
        tri.tex = pixels.data();
    }
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < n; ++i)
        for (int v = 0; v < 3; ++v) {
            const float p[3] = {t[i].v[v].cx, t[i].v[v].cy, t[i].v[v].cz};
            for (int k = 0; k < 3; ++k) { lo[k] = fminf(lo[k], p[k]); hi[k] = fmaxf(hi[k], p[k]); }
        }
    float r2 = 0;
    for (int k = 0; k < 3; ++k) {
        g_lvl_center[k] = 0.5f * (lo[k] + hi[k]);
        r2 += 0.25f * (hi[k] - lo[k]) * (hi[k] - lo[k]);
    }
    g_lvl_radius = sqrtf(r2) > 1e-3f ? sqrtf(r2) : 1.0f;
    if (!g_auto) goto skip_log;
    printf("level %s: %u tris, radius %.1f\n", kLevels[g_level].name, (unsigned)n, g_lvl_radius);
    printf("  model box x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
skip_log:
    if (kLevels[g_level].kcl) {
        ClsnBox b;
        if (clsn_load(kLevels[g_level].kcl, &b)) {
            if (g_auto) printf("  kcl box x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f (fx/4096)\n",
                   b.x0 / 4096.0f, (b.x0 + b.ex) / 4096.0f, b.y0 / 4096.0f, (b.y0 + b.ey) / 4096.0f,
                   b.z0 / 4096.0f, (b.z0 + b.ez) / 4096.0f);
            // drop Mario at the first floor hit on a spiral out from the
            // level centre (stage spawn points live in the level overlay's
            // actor list, which the viewer does not parse yet)
            g_mario_in_level = false;
            const float mx = 0.5f * (lo[0] + hi[0]), mz = 0.5f * (lo[2] + hi[2]);
            // Native DS new-game resting spawn, observed from player +0x5c
            // in supplied ASMP ROM: (-1200,254,6800), all in world units.
            if (g_level == 1) {
                int floorY;
                const bool found = clsn_floor(-1200 * 4096, 6800 * 4096,
                                             354 * 4096, 154 * 4096, &floorY);
                if (g_auto) printf("  DS spawn floor: hit=%d y=%.6f expected=254.0\n",
                                   found, found ? floorY / 4096.0f : 0.0f);
                if (found) {
                    g_mario_pos[0] = -1.2f;
                    g_mario_pos[1] = floorY / (4096.0f * WORLD_PER_MODEL);
                    g_mario_pos[2] = 6.8f;
                    g_mario_in_level = true;
                    if (g_reference_ds) {
                        g_cam_yaw = 0.0f;
                        g_cam_pitch = g_reference_mario ? 0.2283820417f : 0.2061120315f;
                        g_cam_dist = g_reference_mario ? 1.4019464460f : 1.5416996865f;
                    }
                }
            }
            for (int ring = 0; ring < 12 && !g_mario_in_level; ++ring)
                for (int a = 0; a < 8 && !g_mario_in_level; ++a) {
                    const float px = mx + ring * 0.5f * cosf(a * 0.785f);
                    const float pz = mz + ring * 0.5f * sinf(a * 0.785f);
                    const int fx = (int)(px * WORLD_PER_MODEL * 4096), fz = (int)(pz * WORLD_PER_MODEL * 4096);
                    int y;
                    if (clsn_floor(fx, fz, b.y0 + b.ey, b.y0 - 0x4000, &y)) {
                        g_mario_pos[0] = px;
                        g_mario_pos[1] = y / 4096.0f / WORLD_PER_MODEL;
                        g_mario_pos[2] = pz;
                        g_mario_in_level = true;
                    }
                }
            {   // Mario's own extent in model units, for the scale check
                scene_setup(ident);
                const int sc = g_scene, cl = g_clip;
                g_scene = g_level_character; g_clip = 0;
                pose(0);
                render_scene();
                g_scene = sc; g_clip = cl;
                size_t mn = 0;
                const ntr::GxTriangle *mt = ntr::gx_polygons(mn);
                float ylo = 1e30f, yhi = -1e30f;
                for (size_t i = 0; i < mn; ++i)
                    for (int v = 0; v < 3; ++v) { ylo = fminf(ylo, mt[i].v[v].cy); yhi = fmaxf(yhi, mt[i].v[v].cy); }
                if (g_auto) printf("  mario model height %.3f (y %.3f..%.3f), %u tris\n", yhi - ylo, ylo, yhi, (unsigned)mn);
            }
            if (g_mario_in_level && g_auto)
                printf("  mario on the floor at (%.2f, %.2f, %.2f)\n",
                       g_mario_pos[0], g_mario_pos[1], g_mario_pos[2]);
            else if (g_auto)
                printf("  no floor found for mario\n");
        }
    }
}

// Orbit camera -> view * perspective, packed as the DS 4x4 projection.
// Projected for the 400x240 panel directly: the GPU path then uses aspect 1.
static void level_camera()
{
    float center[3] = {g_lvl_center[0], g_lvl_center[1], g_lvl_center[2]};
    float radius = g_lvl_radius;
    if (g_mario_in_level) {      // third-person: orbit Mario, not the stage
        center[0] = g_mario_pos[0];
        center[1] = g_mario_pos[1] + ((g_reference_ds && g_level == 1) ? (g_reference_mario ? 0.1610666504f : 0.1558720703f) : 0.08f);
        center[2] = g_mario_pos[2];
        radius = 0.35f;
    }
    const float g_stage_radius = g_lvl_radius;            // before the macro below
    const float g_lvl_center_[3] = {center[0], center[1], center[2]};
    const float g_lvl_radius_ = radius;
#define g_lvl_center g_lvl_center_
#define g_lvl_radius g_lvl_radius_
    const float d = g_lvl_radius * 1.6f * g_cam_dist;
    const float cy = cosf(g_cam_yaw), sy = sinf(g_cam_yaw), cp = cosf(g_cam_pitch), sp = sinf(g_cam_pitch);
    // eye on a sphere around the centre, looking at it
    float eye[3] = {g_lvl_center[0] + d * cp * sy, g_lvl_center[1] + d * sp, g_lvl_center[2] + d * cp * cy};
    if (g_mario_in_level) {
        int start[3], end[3];
        const float units = WORLD_PER_MODEL * 4096.0f;
        for (int i = 0; i < 3; ++i) {
            start[i] = (int)(center[i] * units);
            end[i] = (int)(eye[i] * units);
        }
        if (clsn_segment(start, end)) {
            float hit[3], distance2 = 0;
            for (int i = 0; i < 3; ++i) {
                hit[i] = end[i] / units - center[i];
                distance2 += hit[i] * hit[i];
            }
            const float distance = sqrtf(distance2);
            // Keep the eye slightly in front of the surface, including
            // the near plane, instead of letting the orbit enter a wall.
            const float scale = fmaxf(distance - 0.025f, 0.005f) / d;
            for (int i = 0; i < 3; ++i) eye[i] = center[i] + (eye[i] - center[i]) * scale;
        }
    }
    float f[3] = {g_lvl_center[0] - eye[0], g_lvl_center[1] - eye[1], g_lvl_center[2] - eye[2]};
    const float fl = fmaxf(0.0001f, sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]));
    g_convergence = fl;
    for (float &x : f) x /= fl;
    float r[3] = {f[2], 0.0f, -f[0]};                       // f x up(0,1,0), normalised
    const float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl; r[2] /= rl;
    r[0] = -r[0]; r[2] = -r[2];
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    // view: columns are right, up, -forward (row-vector form)
    const float V[16] = {
        r[0], u[0], -f[0], 0,
        r[1], u[1], -f[1], 0,
        r[2], u[2], -f[2], 0,
        -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]),
        -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]),
        (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]), 1};
    // Far plane from the STAGE size, not the orbit radius: with the camera on
    // Mario the orbit radius is tiny and a far plane derived from it clipped
    // most of the level away on the GPU (the CPU raster never clips).
    const float n = d * 0.02f, fa = d + g_stage_radius * 2.5f;
    // Native Camera::Render interpolates half-FOV4187 toward2730 using
    // camera+0x168=1132. Captured GX projection has Pyy=675, Pwz=-256,
    // so its effective vertical focal length is675/256 (half-angle3784).
    // Separate native Mario reference has Pyy605 with the same Pwz=-256.
    const float fy = (g_reference_ds && g_level == 1) ? ((g_reference_mario ? 605.0f : 675.0f) / 256.0f)
                                                        : 1.0f / tanf(0.5f * 0.85f);
    const float aspect = 400.0f / 240.0f;
    const float P[16] = {
        fy / aspect, 0, 0, 0,
        0, fy, 0, 0,
        0, 0, (fa + n) / (n - fa), -1,
        0, 0, 2 * fa * n / (n - fa), 0};
    float VP[16];
    mat_mul(V, P, VP);
    for (int i = 0; i < 16; ++i) g_fit[i] = (int32_t)(VP[i] * 4096.0f);
#undef g_lvl_center
#undef g_lvl_radius
}

// The game-shaped loop on the game fiber: pose, skin, submit, block at VBlank.
static void game_main()
{
    for (int frame = 0;; ++frame) {
        const double started = v3ds_time_ms();
        if (g_level_mode) {
            if (!g_level_measured) { measure_level(); g_level_measured = true; }
            g_frame = frame;
            level_camera();
            scene_setup(g_fit);
            if (g_cpu_raster) g_level_models[g_level]->Model::Render(NULL);
            if (g_mario_in_level) {
                g_scene = g_level_character; // Mario, or native Yoshi reference
                g_clip = 0;                  // wait
                pose(frame);
                int *m = (int *)&g_loaded[g_level_character].model->mat4x3;
                ident_fx(m);
                m[0] = m[4] = m[8] = (int)(MARIO_IN_LEVEL_SCALE * 4096);
                if (g_reference_ds && g_level == 1) { m[0] = -m[0]; m[8] = -m[8]; }
                m[9] = (int)(g_mario_pos[0] * 4096);
                m[10] = (int)(g_mario_pos[1] * 4096);
                m[11] = (int)(g_mario_pos[2] * 4096);
                render_scene();
                ident_fx(m);                 // character mode expects identity
            }
            g_game_ms = v3ds_time_ms() - started;
            ntr::rt_vblank_wait();
            continue;
        }
        if (g_refit) {
            refit();
            g_refit = false;
            frame = 0;
        }
        g_frame = frame;
        pose(frame);
        scene_setup(g_fit);
        render_scene();
        g_game_ms = v3ds_time_ms() - started;
        ntr::rt_vblank_wait();
    }
}

// ---- host side: raster, present, input --------------------------------------
static ntr::Framebuffer g_fb;

// Auto mode (sdmc:/sm64ds/autoexit, set by build3ds/run.sh): walk every
// scene/clip, save one capture each as shot_<scene>_<clip>.bmp, then quit.
static int g_auto_frames;

// SELECT toggles the comparison path.
static uint32_t g_cpu_tex[256 * 256];           // CPU raster padded to a power of two
static V3dsVert g_verts[0x8000];
static V3dsBatch g_batches[1024];

static void put(V3dsVert &o, const ntr::GxVertex &v, float tw, float th)
{
    o.x = v.cx; o.y = v.cy; o.z = v.cz; o.w = v.cw;
    o.r = ((v.color >> 16) & 0xFF) / 255.0f;
    o.g = ((v.color >> 8) & 0xFF) / 255.0f;
    o.b = (v.color & 0xFF) / 255.0f;
    o.a = (v.color >> 24) / 255.0f;
    o.u = tw > 0 ? v.u / tw : 0.0f;
    o.v = th > 0 ? v.v / th : 0.0f;   // rows are flipped at upload instead
}

// The game's triangles, as the DS geometry engine emitted them, grouped into
// runs that share a texture state.
static void build_gpu(int &nv, int &nb)
{
    size_t n = 0;
    const ntr::GxTriangle *t = ntr::gx_polygons(n);
    nv = nb = 0;
    float projection[16];
    for (int i = 0; i < 16; ++i) projection[i] = g_fit[i] / 4096.0f;
    auto append = [&](const ntr::GxTriangle *triangles, size_t count, bool world) {
        for (size_t i = 0; i < count && nv + 9 <= (int)(sizeof g_verts / sizeof g_verts[0]); ++i) {
            const ntr::GxTriangle &tri = triangles[i];
            const bool same = nb > 0 && g_batches[nb - 1].tex == tri.tex &&
                              g_batches[nb - 1].wrap == tri.wrap &&
                              g_batches[nb - 1].polygon_attr == tri.polygon_attr &&
                              g_batches[nb - 1].texture_version == tri.texture_version;
            if (!same) {
                if (nb == (int)(sizeof g_batches / sizeof g_batches[0])) break;
                g_batches[nb++] = V3dsBatch{tri.tex, tri.tw, tri.th, tri.wrap, nv, 0, false, tri.polygon_attr, tri.texture_version};
            }
            DsGpuVertex polygon[8];
            for (int k = 0; k < 3; ++k) {
                V3dsVert &out = polygon[k].gpu;
                put(out, tri.v[k], (float)tri.tw, (float)tri.th);
                polygon[k].screen_x=tri.v[k].x;
                polygon[k].screen_y=tri.v[k].y;
                if (world) {
                    const float x = out.x, y = out.y, z = out.z, w = out.w;
                    out.x = x * projection[0] + y * projection[4] + z * projection[8] + w * projection[12];
                    out.y = x * projection[1] + y * projection[5] + z * projection[9] + w * projection[13];
                    out.z = x * projection[2] + y * projection[6] + z * projection[10] + w * projection[14];
                    out.w = x * projection[3] + y * projection[7] + z * projection[11] + w * projection[15];
                    const float iw=std::fabs(out.w)>1e-6f ? 1.f/out.w : 0.f;
                    polygon[k].screen_x=(out.x*iw+1.f)*128.f;
                    polygon[k].screen_y=(1.f-(out.y*iw+1.f)*.5f)*192.f;
                }
            }
            const int clipped=ds_gpu_clip_viewport(polygon);
            for(int k=1;k+1<clipped;++k) {
                g_verts[nv++]=polygon[0].gpu;
                g_verts[nv++]=polygon[k].gpu;
                g_verts[nv++]=polygon[k+1].gpu;
                g_batches[nb-1].count+=3;
            }
        }
    };
    if (g_level_mode) append(g_level_geometry.data(), g_level_geometry.size(), true);
    append(t, n, false);
}

// CPU raster (the original ntr path) shown as one textured quad, for comparison.
static void build_cpu(int &nv, int &nb)
{
    for (int y = 0; y < ntr::SCREEN_H; ++y)
        for (int x = 0; x < ntr::SCREEN_W; ++x) g_fb.px[y][x] = 0xFF101820u;
    ntr::gx_render(g_fb);
    for (int y = 0; y < ntr::SCREEN_H; ++y)
        memcpy(&g_cpu_tex[y * 256], g_fb.px[y], 256 * 4);
    const float vmax = ntr::SCREEN_H / 256.0f;
    // clip-space quad covering the DS viewport, image row 0 at the top (v = 0)
    const float q[6][4] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {1, -1, 1, vmax},
                           {-1, 1, 0, 0}, {1, -1, 1, vmax}, {-1, -1, 0, vmax}};
    for (int i = 0; i < 6; ++i)
        g_verts[i] = V3dsVert{q[i][0], q[i][1], 0.0f, 1.0f, 1, 1, 1, 1, q[i][2], q[i][3]};
    g_batches[0] = V3dsBatch{g_cpu_tex, 256, 256, 0, 0, 6, true};
    nv = 6;
    nb = 1;
}

static void write_bmp400(const char *path, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint32_t img = 400 * 240 * 3, total = 54 + img, off = 54, dib = 40;
    const int32_t w = 400, h = 240;
    const uint16_t planes = 1, bpp = 24;
    uint8_t hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &total, 4); memcpy(hdr + 10, &off, 4); memcpy(hdr + 14, &dib, 4);
    memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4); memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2); memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, 54, f);
    static uint8_t row[400 * 3];
    for (int y = 239; y >= 0; --y) {
        for (int x = 0; x < 400; ++x) {
            const uint32_t c = px[y * 400 + x];
            row[x * 3] = c & 0xFF; row[x * 3 + 1] = (c >> 8) & 0xFF; row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

static bool frame_hook(uint64_t)
{
    const double started = v3ds_time_ms();
    int nv = 0, nb = 0;
    if (g_cpu_raster) build_cpu(nv, nb);
    else build_gpu(nv, nb);
    const double batch_ms = v3ds_time_ms() - started;
    const Scene &sc = kScenes[g_scene];
    char status[64];
    if (g_level_mode) snprintf(status, sizeof status, "%s", kLevels[g_level].name);
    else snprintf(status, sizeof status, "%s / %s", sc.name, sc.clips[g_clip].name);
    v3ds_gpu_frame(g_verts, nv, g_batches, nb, status, g_cpu_raster ? "CPU" : "GPU",
                   g_level_mode && !g_cpu_raster ? 1.0f : 0.8f,
                   g_level_mode ? g_convergence : 1.0f, g_level_mode, !g_cpu_raster);
    v3ds_profile(g_game_ms, batch_ms);

    if (g_auto && g_level_mode) {
        // headless levels: four orbit angles per level
        const char *bench = getenv("SM64DS_BENCHMARK_FRAMES");
        const int frame_limit = bench ? (atoi(bench) > 6 ? atoi(bench) : 6) : 6;
        if (++g_auto_frames == frame_limit) {
            static uint32_t top[400 * 240];
            static int shot;
            char name[64];
            v3ds_read_top(top);
            snprintf(name, sizeof name, "lvl_%d_%d.bmp", g_level, shot);
            write_bmp400(name, top);
            if (!g_cpu_raster && getenv("SM64DS_FORCE_3D")) {
                v3ds_read_top(top, true);
                char right[64];
                snprintf(right, sizeof right, "lvl_%d_%d_right.bmp", g_level, shot);
                write_bmp400(right, top);
            }
            printf("captured %s (%s, %d tris, %d batches)\n", name, status, nv / 3, nb);
            g_auto_frames = 0;
            g_cam_yaw += 1.5708f;
            if (++shot == 4) {
                shot = 0;
                if (++g_level == NLEVELS) { printf("PASS\n"); return false; }
                g_level_measured = false;
            }
        }
        return true;
    }

    if (g_auto) {
        // headless: every scene/clip, GPU then CPU, one capture each
        if (++g_auto_frames == 20) {
            static uint32_t top[400 * 240];
            char name[64];
            v3ds_read_top(top);
            snprintf(name, sizeof name, "gpu_%d_%d_%s.bmp", g_scene, g_clip, g_cpu_raster ? "cpu" : "gpu");
            write_bmp400(name, top);
            if (!g_cpu_raster && getenv("SM64DS_FORCE_3D")) {
                v3ds_read_top(top, true);
                snprintf(name, sizeof name, "gpu_%d_%d_right.bmp", g_scene, g_clip);
                write_bmp400(name, top);
            }
            printf("captured %s (%s, %d tris, %d batches)\n", name, status, nv / 3, nb);
            g_auto_frames = 0;
            if (!g_cpu_raster) { g_cpu_raster = true; return true; }
            g_cpu_raster = false;
            if (++g_clip == sc.nclips) { g_clip = 0; ++g_scene; }
            if (g_scene == NSCENES) { printf("PASS\n"); return false; }
            g_refit = true;
        }
        return true;
    }
    const V3dsPad pad = v3ds_pad();
    const int k = pad.down;
    if (k & V3DS_QUIT) return false;
    if (k & V3DS_SELECT) g_cpu_raster = !g_cpu_raster;
    if (k & V3DS_X) { g_level_mode = !g_level_mode; g_level_measured = false; g_refit = true; return true; }
    if (g_level_mode) {
        if (k & V3DS_A) { g_level = (g_level + 1) % NLEVELS; g_level_measured = false; }
        float dx = pad.cx, dy = pad.cy;
        if (pad.held & V3DS_LEFT) dx = -1;
        if (pad.held & V3DS_RIGHT) dx = 1;
        if (pad.held & V3DS_UP) dy = 1;
        if (pad.held & V3DS_DOWN) dy = -1;
        g_cam_yaw += dx * 0.04f;
        g_cam_pitch = fminf(1.45f, fmaxf(-0.2f, g_cam_pitch + dy * 0.03f));
        if (pad.held & V3DS_L) g_cam_dist = fminf(3.0f, g_cam_dist * 1.02f);
        if (pad.held & V3DS_R) g_cam_dist = fmaxf(0.15f, g_cam_dist / 1.02f);
        return true;
    }
    if (k & V3DS_A) { g_scene = (g_scene + 1) % NSCENES; g_clip = 0; g_refit = true; }
    if (k & V3DS_R) { g_clip = (g_clip + 1) % sc.nclips; g_refit = true; }
    if (k & V3DS_L) { g_clip = (g_clip + sc.nclips - 1) % sc.nclips; g_refit = true; }
    return true;
}

int main()
{
    v3ds_init();
    g_auto = v3ds_auto();
    g_reference_ds = getenv("SM64DS_REFERENCE_DS") != NULL;
    g_reference_mario = g_reference_ds && getenv("SM64DS_REFERENCE_MARIO") != NULL;
    if (!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv()) {
        v3ds_log("init failed");
    } else {
        ident_fx(&data_0209b3ec);
        load_scenes();
        load_levels();
        if (g_auto && getenv("SM64DS_AUTO_LEVELS")) g_level_mode = true;
        if (g_auto && getenv("SM64DS_AUTO_CPU")) g_cpu_raster = true;
        ntr::rt_run(game_main, frame_hook, 0);
    }
    v3ds_exit();
    return 0;
}
