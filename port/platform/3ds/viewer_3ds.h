// Narrow interface between the game-side viewer and libctru/citro3d.
#ifndef VIEWER_3DS_H
#define VIEWER_3DS_H
#include <stdint.h>
#include <stddef.h>

enum { V3DS_QUIT = 1, V3DS_A = 2, V3DS_L = 4, V3DS_R = 8, V3DS_SELECT = 16, V3DS_X = 32,
       V3DS_UP = 64, V3DS_DOWN = 128, V3DS_LEFT = 256, V3DS_RIGHT = 512 };

// Pressed-this-frame and held bits, plus the circle pad in [-1, 1].
struct V3dsPad { int down, held; float cx, cy; };

// One GPU vertex: DS clip-space position, RGBA colour 0..1, normalised texcoord.
struct V3dsVert { float x, y, z, w; float r, g, b, a; float u, v; };

// A run of triangles sharing one texture state. tex = decoded 0xAARRGGBB
// (null = untextured); wrap = TEXIMAGE_PARAM bits 16..19. `dynamic` textures
// are re-uploaded every frame (the CPU-raster comparison image).
struct V3dsBatch {
    const uint32_t *tex;
    int tw, th;
    uint32_t wrap;
    int first, count;
    bool dynamic;
    uint32_t polygon_attr = 0x001f00c0; // synthetic quads default to both faces
    uint64_t texture_version = 0; // Nonzero only for an immutable GX-owned decode.
};

double v3ds_time_ms();
void v3ds_profile(double game_ms, double batch_ms);
void v3ds_init();
void v3ds_renderer_init(); // existing gfx/log environment owned by caller
void v3ds_exit();
// aspect_x: horizontal clip scale -- 0.8 maps 4:3 DS content onto the 5:3
// panel without stretching; 1.0 for scenes projected for 400x240 natively.
void v3ds_gpu_frame(const V3dsVert *verts, int nverts, const V3dsBatch *batches, int nb,
                    const char *status, const char *mode, float aspect_x = 0.8f, float convergence = 1.0f,
                    bool perspective = false, bool stereo = true);
// Render the DS grid offscreen for native PPU composition. Buffers are
// 256x192 row-major 0xAARRGGBB. The caller composes each eye before presenting.
bool v3ds_gpu_geometry(const V3dsVert*,int,const V3dsBatch*,int,
                      uint32_t* left,uint32_t* right,float convergence,bool perspective,bool stereo);
#ifdef SM64DS_GEOMETRY_DIAGNOSTICS
// Call count only; querying it performs no GPU operation/readback.
unsigned v3ds_gpu_geometry_calls();
#endif
void v3ds_present_native(const uint32_t* upper_left,const uint32_t* upper_right,
                        const uint32_t* lower,bool stereo);
V3dsPad v3ds_pad();
void v3ds_read_top(uint32_t *out400x240, bool right = false);
void v3ds_read_bottom(uint32_t *out320x240);
int v3ds_input();
void v3ds_log(const char *msg);
bool v3ds_auto();   // true: log to sdmc:/sm64ds/log.txt and run headless

// State of the last native geometry frame, after physical slider/override gating.
bool v3ds_native_stereo();
// Title/main-menu scene: present both DS screens at the same 4:3 scale with
// the DS-faithful sharp filter (see screen_scale in viewer_3ds.cpp).
void v3ds_set_menu_presentation(bool active);
// Texture payload only, excluding render targets and native-screen blit textures.
void v3ds_texture_cache_usage(size_t& entries,size_t& bytes,uint64_t& evictions);

#include "native_geometry_clear.h"
void v3ds_set_native_clear(ds_clear::State state);

#endif
