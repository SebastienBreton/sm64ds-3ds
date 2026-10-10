// Gate-8 smoke: the game's collision answers "what is under this point".
//
// The castle grounds KCL loads through the card seam; LoadFile rebases it
// (the same in-place pointer math as BMDs), SetFile wires the CLPS block,
// and the octree walk -- the port/unmatched adoption of the banked draft,
// algorithm confirmed against the ITCM disassembly -- casts vertical rays
// down through the mesh. Floor hits must come back with upward normals,
// shortened rays, and hit points inside the probe column.
//
// The ray object is harness-built (fields the walk reads, zeroed rest);
// the game ctor's vtable/list wiring belongs to the actor gates.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dBgW_Kc.h"
#include "dBgCh_SphCrr.h"

#include "ntr/mmio.h"

#include "fault_probe.h"

struct SurfaceInfoS { unsigned char clps[8]; int nx, ny, nz; };
struct RayS {
    unsigned char head[0x10];
    unsigned char result[0x1c];     /* 0x10 */
    unsigned char pad2c[0xc];
    int sx, sy, sz;                 /* 0x38 lineStart */
    unsigned char pad44[0xc];
    unsigned char hasClsn;          /* 0x50 */
    unsigned char pad51[3];
    int ex, ey, ez;                 /* 0x54 lineEnd */
    int clsnDist;                   /* 0x60 */
    unsigned char tail[0x10];
};

extern "C" {
extern int g_walk_dbg[16];
void* _ZN12dBgCh_SphCrrC1Ev(void*);
void _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(void*,const Vector3*,int,void*);
void func_02037a38(void*);
struct SharedFilePtrC { u16 fileID; u8 numRefs; void *filePtr; };
SharedFilePtrC *_ZN13SharedFilePtr9ConstructEj(SharedFilePtrC *self, u32 ov0FileID);
void *_ZN4Heap13SetupRootHeapEv(void);
void *_ZN7dBgW_KcC1Ev(void *self);
}

static int probe_one(dBgW_Kc *mc, RayS *ray)
{
    return mc->dBgW_Kc::DetectClsn(*(dBgCh_Lin *)ray);
}
#if defined(_WIN32)
static int probe_filter(EXCEPTION_POINTERS *ep)
{
    char *base = (char *)GetModuleHandleA(0);
    printf("  [probe fault %08x at +0x%08x]\n",
           (unsigned)ep->ExceptionRecord->ExceptionCode,
           (unsigned)((char *)ep->ExceptionRecord->ExceptionAddress - base));
    return EXCEPTION_EXECUTE_HANDLER;
}
static int probe_seh(dBgW_Kc *mc, RayS *ray)
{
    __try {
        return probe_one(mc, ray);
    } __except (probe_filter(GetExceptionInformation())) {
        return -1;
    }
}
#else
static int probe_seh(dBgW_Kc *mc, RayS *ray) { return probe_one(mc, ray); }
#endif

static int g_failures;
#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; \
        fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(void)
{
    PORT_INSTALL_FAULT_PROBE();
    setvbuf(stdout, NULL, _IONBF, 0);   /* crashes must not eat the trail */
    if (!ntr::io_init()) { fprintf(stderr, "io_init failed\n"); return 2; }
    CHECK(_ZN4Heap13SetupRootHeapEv() != NULL);

    /* the castle grounds KCL, handle 1941: real level floors
       (the piano's KCL turned out to be four wall prisms and nothing
       horizontal -- an actor's push-out shell, not a floor) */
    SharedFilePtrC ptr;
    _ZN13SharedFilePtr9ConstructEj(&ptr, 1941);
    static char mc_storage[0x60];
    dBgW_Kc *mc = (dBgW_Kc *)mc_storage;
    _ZN7dBgW_KcC1Ev(mc_storage);
    char *kcl = dBgW_Kc::LoadFile(*(SharedFilePtr *)&ptr);
    CHECK(kcl != NULL);

    /* CLPS_Block is opaque; a zeroed buffer takes the lookup's
       wrong-version default path, which is what stages without a CLPS do */
    static char clps_storage[0x100];
    mc->dBgW_Kc::SetFile((KCL_File *)kcl, *(CLPS_Block *)clps_storage);

    {
        KCL_File *f = (KCL_File *)kcl;
        (void)f;
        printf("  kcl: origin (%d,%d,%d) raw, masks %08x %08x %08x, "
               "shifts %u/%u/%u\n", f->origin.x, f->origin.y, f->origin.z,
               f->xMask, f->yMask, f->zMask, f->coordShift, f->yShift, f->zShift);
    }

    // Measured native DS Player floor after 180 frames, diagnostic position only.
    struct Reference { const char* name; int x,y,z,nx,ny,nz,centerY,dispY; };
    const Reference points[]={
        {"west_bank",-16384000,1382513,24576000,1148,3860,-748,1538149,49164},
        {"castle_lawn",-8192000,2785137,16384000,-88,4060,528,2940729,49208},
        {"opposing_bank",-12288000,1596188,18432000,-1188,3892,476,1751834,49154},
    };
    for(const auto& point:points){
        RayS ray{};ray.head[4]=1;
        ray.sx=ray.ex=point.x;ray.sz=ray.ez=point.z;
        ray.sy=point.y+409600;ray.ey=point.y-409600;ray.clsnDist=0x7FFFFFF;
        int result=probe_seh(mc,&ray);int normal[3];std::memcpy(normal,ray.result+0xc,12);
        std::printf("floor-reference %s hit=%d y=%d ds_y=%d delta=%d normal=(%d,%d,%d) ds_normal=(%d,%d,%d)\n",
            point.name,result&&ray.hasClsn,ray.ey,point.y,ray.ey-point.y,
            normal[0],normal[1],normal[2],point.nx,point.ny,point.nz);
        CHECK(result&&ray.hasClsn);
        CHECK(normal[0]==point.nx&&normal[1]==point.ny&&normal[2]==point.nz);
        // Player resting height comes from the sphere correction, not this ray.
        // Replay its measured query center/radius and compare the actual result.
        alignas(8) unsigned char storage[0x110]{};
        _ZN12dBgCh_SphCrrC1Ev(storage);
        auto* sphere=reinterpret_cast<dBgCh_SphCrr*>(storage);
        Vector3 center;center.x=point.x;center.y=point.centerY;center.z=point.z;
        _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(sphere,&center,204800,nullptr);
        sphere->unk_0ec=163840; // Player's measured wall-slab extent (40 world units).
        int hit=mc->dBgW_Kc::DetectClsn(*sphere);func_02037a38(sphere);
        const int correctedY=point.centerY-204800+sphere->disp.y;
        std::printf("sphere-reference %s hit=%d flags=%u disp=(%d,%d,%d) ds_dispY=%d correctedY=%d ds_y=%d delta=%d\n",
            point.name,hit,sphere->flags,sphere->disp.x,sphere->disp.y,sphere->disp.z,
            point.dispY,correctedY,point.y,correctedY-point.y);
        CHECK(hit&&(sphere->flags&4));
        CHECK(sphere->disp.x==0&&sphere->disp.z==0&&sphere->disp.y==point.dispY);
        CHECK(correctedY==point.y);
    }
    if(g_failures){std::printf("smoke_floor_reference: FAIL %d checks\n",g_failures);return 1;}
    std::puts("smoke_floor_reference: PASS native DS slope sphere correction/normal reference");return 0;
}
