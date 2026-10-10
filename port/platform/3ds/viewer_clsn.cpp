// Level collision for sm64ds_viewer: the game's own KCL octree walk
// (dBgW_Kc::DetectClsn), driven exactly like tests/smoke_clsn.cpp drives it.
// Coordinates are the game's world units: Fix12 (4096 = 1.0).
#include <cstdio>
#include <cstring>

#include "dBgW_Kc.h"
#include "viewer_clsn.h"

namespace {

struct RayS {
    unsigned char head[0x10];
    unsigned char result[0x1c];     /* 0x10: +0x4 CLPS, +0xC normal, +0x18 triIdx */
    unsigned char pad2c[0xc];
    int sx, sy, sz;                 /* 0x38 lineStart */
    unsigned char pad44[0xc];
    unsigned char hasClsn;          /* 0x50 */
    unsigned char pad51[3];
    int ex, ey, ez;                 /* 0x54 lineEnd (shortened to the hit) */
    int clsnDist;                   /* 0x60 */
    unsigned char tail[0x10];
};

struct SharedFilePtrC { u16 fileID; u8 numRefs; void *filePtr; };

alignas(8) char g_mc_storage[0x60];
alignas(8) char g_clps_storage[0x100];
dBgW_Kc *g_mc;
KCL_File *g_kcl;

}  // namespace

extern "C" {
SharedFilePtrC *_ZN13SharedFilePtr9ConstructEj(SharedFilePtrC *self, u32 ov0FileID);
void *_ZN7dBgW_KcC1Ev(void *self);
}

bool clsn_load(unsigned handle, ClsnBox *box)
{
    static SharedFilePtrC ptr;
    _ZN13SharedFilePtr9ConstructEj(&ptr, handle);
    g_mc = (dBgW_Kc *)g_mc_storage;
    _ZN7dBgW_KcC1Ev(g_mc_storage);
    g_kcl = (KCL_File *)dBgW_Kc::LoadFile(*(SharedFilePtr *)&ptr);
    if (!g_kcl) return false;
    /* zeroed CLPS: the lookup's default path, as on stages without one */
    g_mc->dBgW_Kc::SetFile(g_kcl, *(CLPS_Block *)g_clps_storage);
    if (box) {
        /* KCL stores positions at world / 64. DetectClsn converts input
           world coordinates with >> 6 and returns hits with << 6, so
           the public bounding box must use the same world units. Masks
           count cells of 0x40 stored units (0x1000 world Fix12 units). */
        box->x0 = g_kcl->origin.x * 64;
        box->y0 = g_kcl->origin.y * 64;
        box->z0 = g_kcl->origin.z * 64;
        box->ex = (int)(~g_kcl->xMask + 1) * 4096;
        box->ey = (int)(~g_kcl->yMask + 1) * 4096;
        box->ez = (int)(~g_kcl->zMask + 1) * 4096;
    }
    return true;
}

bool clsn_segment(const int start[3], int end[3])
{
    if (!g_mc) return false;
    RayS ray{};
    ray.head[4] = 1;
    ray.sx = start[0]; ray.sy = start[1]; ray.sz = start[2];
    ray.ex = end[0]; ray.ey = end[1]; ray.ez = end[2];
    ray.clsnDist = 0x7FFFFFF;
    if (!g_mc->dBgW_Kc::DetectClsn(*(dBgCh_Lin *)&ray) || !ray.hasClsn) return false;
    end[0] = ray.ex; end[1] = ray.ey; end[2] = ray.ez;
    return true;
}

bool clsn_floor(int x, int z, int ytop, int ybot, int *y)
{
    const int start[3] = {x, ytop, z};
    int end[3] = {x, ybot, z};
    if (!clsn_segment(start, end)) return false;
    *y = end[1];
    return true;
}
