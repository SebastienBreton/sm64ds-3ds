#include "unmatched/actor_registry.h"
// Native Player integration gate: construction and actual resource initialization.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <array>
#include <cstring>
#include <cmath>
#include "Player.h"
#include "Model.h"
#include "LVL_Overlay.h"
#include "FaderWipe.h"
#include "Camera.h"
#include "Stage.h"
void LoadObjects(LVL_Overlay::ObjTable&,int,unsigned);
#include "dBgW_Kc.h"
#include "dBgW_KcMbg.h"
#include "dBgCh_Gnd.h"
#include "dBgCh_Lin.h"
#include "dBgCh_SphCrr.h"
#include "SharedFilePtr.h"
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "audio/sound_native.h"
#include "platform/3ds/player_input.h"
#include "platform/3ds/viewer_3ds.h"
bool player_present(bool capture, bool perspective=false);
#include "fault_probe.h"
static_assert(sizeof(Player) == 0x768, "Player must retain the DS layout");
extern "C" {
Player *daPly_c_classInit();
void *dCamera_c_classInit();
Player *daDoor_c_classInit();
Player *daChScene_c_classInit();
Player *daMcFlag_c_classInit();
Player *daTree_c_classInit();
Player *daSBird_c_classInit();
Player *daSetSE_c_classInit();
Player *daBtfly_c_classInit();
Player *daCnn_c_classInit();
Player *daObjMc_Metalnet_c_classInit();
Player *daObjTatefuda_c_classInit();
Player *daObjMcWater_c_classInit();
Player *daObjHatenaBlock_c_classInit_HATENA_BLOCK();
void __sinit_ov102_0214d908();
Player *da1up_c_classInit_ONEUPKINOKO();
void __sinit_ov002_02100adc();
Player *daMip_c_classInit();
void __sinit_ov085_0212f5ec();
Player *daC_Jugem_c_classInit();
void __sinit_ov085_0212fa40();
Player *daObjBlockL_c_classInit_BLOCK_LL();
void __sinit_ov002_02100d44();
Player *daFish_c_classInit();
void __sinit_ov100_02147bc0();
void __sinit_ov102_0214de70();
void __sinit_0207511c();
extern unsigned char g_profile_WATERFALL[];
void __sinit_ov009_02112524();
void __sinit_ov002_02101738();
void __sinit_ov009_02112a5c();
void __sinit_ov098_0213c214();
void __sinit_ov098_0213c2b4();
extern unsigned char g_profile_WATERBOMB[],g_profile_YAJIRUSI_L[],g_profile_YAJIRUSI_R[];
void __sinit_ov098_0213bf9c();
void __sinit_ov098_0213c058();
extern unsigned char g_profile_BLOCK_S[];
void __sinit_ov100_021473bc();
void sm64ds_init_butterfly_shared_model();
void __sinit_ov009_02112458();
extern unsigned char *data_ov002_02110a48[5];
void __sinit_ov009_02112ac8();
extern signed char data_0209f2f8,data_02092110;
void __sinit_ov100_02147698();
extern unsigned char data_0209fc5c[];
uint64_t func_02059650();
void *_ZN4Heap13SetupRootHeapEv();
void __sinit_02075154();
void __sinit_0207501c();
void _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x35Fix12IiEsR10CLPS_Block(void*,void*,void*,int,short,void*);
void _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(void*,const Vector3*,int,void*);
void func_02039e48(void*,const Vector3*,Vector3*);
void func_02037464(void*);
void __sinit_02073a24();
void __sinit_02074da8();
void __sinit_02074e84();
void func_0201a4e4();
void func_02059bc0();
void sm64ds_init_key_resources();
void sm64ds_init_level_scripts();
void __sinit_ov002_021019d0();
void __sinit_ov002_02106e40();
void __sinit_ov002_021071f4();
void __sinit_ov002_02100560();
void __sinit_ov002_02107298();
void __sinit_ov002_0210804c();
int func_02043098(int, int, int, int);
int func_02043288(void*);
int func_0204322c(void*);
void func_02044120();
Stage *dScStage_c_classInit();
int func_0203b438(void*,void*,void*);
extern unsigned char data_020a4b6c[];
extern int (*data_020a4b58)(void*);
extern void *data_0209f324;
extern void *data_0209f5c0;
extern Camera *data_0209f318;
extern unsigned char data_0209ee90[];
void _ZN7fBase_c9SceneNodeC1Ev(void*);
extern unsigned char data_0209f220;
extern void *data_020a0eac_c;
extern void *data_020a0ea0;
extern void *data_0209b460, *data_0209f314;
extern signed char data_0209b44c;
extern short data_0208e378;
extern int data_0209b3ec[12];
extern char data_0209f43c[];
extern short data_0209f4a0, data_0209f4a6;
extern unsigned short data_0209f49c,data_0209f49e;
extern unsigned short data_020a0e58[];
extern unsigned char data_0209caa0[],data_0209cab4[],data_0209cad2[],data_0209cae4[],data_0209caf4[],data_0209f21c;
extern unsigned char data_ov009_02112bdc[];
void _ZN5Stage10CheckInputEv(void*);
void _ZN7Clipper13Func_020156DCEitii(void*,int,unsigned short,int,int);
}

struct Profile { Player *(*create)(); unsigned short behavior, render;
                 unsigned flags; int offsetY, radius, clip, far; };
static_assert(sizeof(Profile) == 0x1c, "Player profile ABI");

static void initialize_player_profile()
{
    // Original ov002 g_profile_PLAYER, with its native factory binding.
    static Profile profile = {daPly_c_classInit, 0xbf, 0x12, 2,
                              0xfa000, 0xfa000, 0x2ee0000, 0x2ee0000};
    data_020a4bb8[0xbf] = &profile;
}

extern "C" int sm64ds_set_overlay_scope(int);
extern "C" {
extern unsigned char g_profile_KINOPIO[],g_profile_PEACH_PRINCESS[],g_profile_KANBAN[],g_profile_OBJ_MIP_KEY[];
void __sinit_ov085_0212f9bc();
void __sinit_ov085_0212fdb0();
void __sinit_ov085_0212f3a0();
void __sinit_ov085_0212f2a8();
}
extern "C" void LoadOverlay(int);
static void run_overlay_initializer(int overlay,void (*initialize)()) {
    const int previous=sm64ds_set_overlay_scope(overlay);
    initialize();
    sm64ds_set_overlay_scope(previous);
}

// ARM9 profile02086d78: factory0200e444, behavior332, render0; registry slot332.
static void initialize_camera_profile() {
    struct BaseProfile { void *(*create)(); unsigned short behavior,render; };
    static BaseProfile camera_profile={dCamera_c_classInit,0x14c,0};
    data_020a4bb8[0x14c]=&camera_profile;
}

// Shared native startup for Stage and the object diagnostic. Resource
// initializers run once per process; overlay teardown/reload remains separate.
static void initialize_castle_actor_profiles()
{
    static bool initialized=false;
    if(initialized) return;
    initialized=true;
    initialize_camera_profile();
    static Profile door_profile={daDoor_c_classInit,0x161,0x161,0x60800002,
                                 0x7d000,0x7d000,0x1f40000,0};
    data_020a4bb8[0x161]=&door_profile;
    run_overlay_initializer(100,__sinit_ov100_02147698);
    static Profile exit_profile={daChScene_c_classInit,0x15d,0x15d,0,0,0,0,0};
    data_020a4bb8[0x15d]=&exit_profile;
    static Profile flag_profile={daMcFlag_c_classInit,0x156,0x156,0x20000003,
                                 0xc8000,0xc8000,0x3000000,0};
    data_020a4bb8[0x156]=&flag_profile;
    static Profile tree_profile={daTree_c_classInit,0x11e,0x98,0,0,0,0,0};
    data_020a4bb8[0x11e]=&tree_profile;
    static Profile bird_profile={daSBird_c_classInit,0x157,0x157,0x10002,
                                 0,0x28000,0x1000000,0x320000};
    data_020a4bb8[0x157]=&bird_profile;
    static Profile sound_profile={daSetSE_c_classInit,0x15e,0x15e,0x64800000,
                                  0,0x1000,0,0};
    data_020a4bb8[0x15e]=&sound_profile;
    static Profile butterfly_profile={daBtfly_c_classInit,0x150,0x150,0x10002,
                                      0x32000,0x32000,0x1000000,0x320000};
    data_020a4bb8[0x150]=&butterfly_profile;
    run_overlay_initializer(100,__sinit_ov100_021473bc);
    run_overlay_initializer(100,sm64ds_init_butterfly_shared_model);
    LoadOverlay(98);
    static Profile metalnet_profile={daObjMc_Metalnet_c_classInit,0x153,0x153,2,
                                     0,0x500000,0x1000000,0};
    data_020a4bb8[0x153]=&metalnet_profile;
    static Profile signpost_profile={daObjTatefuda_c_classInit,0xb8,0xa7,0x82,
                                     0x40000,0x80000,0x1000000,0x1000000};
    data_020a4bb8[0xb8]=&signpost_profile;
    run_overlay_initializer(2,__sinit_ov002_02101738);
    static Profile water_profile={daObjMcWater_c_classInit,0x152,0x152,0,
                                  0,0x320000,0x1f40000,0x50000};
    data_020a4bb8[0x152]=&water_profile;
    data_020a4bb8[0xc5]=g_profile_WATERFALL;
    static Profile hatena_profile={daObjHatenaBlock_c_classInit_HATENA_BLOCK,20,38,2,
                                  0x60000,0x100000,0x1000000,0x1000000};
    data_020a4bb8[20]=&hatena_profile;
    static Profile oneup_profile={da1up_c_classInit_ONEUPKINOKO,276,140,3,
        0x32000,0x12c000,0x1000000,0x1000000};
    data_020a4bb8[276]=&oneup_profile;
    run_overlay_initializer(2,__sinit_ov002_02100adc);
    static Profile block_ll_profile={daObjBlockL_c_classInit_BLOCK_LL,17,34,2,
        0x96000,0x12c000,0x1800000,0x1800000};
    data_020a4bb8[17]=&block_ll_profile;
    run_overlay_initializer(2,__sinit_ov002_02100d44);
    static Profile fish_profile={daFish_c_classInit,344,344,0x10002,
        0,0x1e000,0x1000000,0x4b0000};
    data_020a4bb8[344]=&fish_profile;
    run_overlay_initializer(100,__sinit_ov100_02147bc0);
    run_overlay_initializer(102,__sinit_ov102_0214d908);
    run_overlay_initializer(102,__sinit_ov102_0214de70);
    __sinit_0207511c();
}

extern "C" void func_0201fe08();
extern "C" void sm64ds_init_common_scenery_profiles();
extern "C" void sm64ds_init_common_level_actor_profiles();
extern "C" void __sinit_ov002_02107370();
extern "C" void func_020133bc();
extern "C" void _ZN5Sound16LoadInitialGroupEi(int);
extern "C" void SetNumPlayers(int);
extern "C" void func_020132d8();
extern "C" void func_02019404();
extern "C" void func_02019390();
extern "C" void func_020190b8();
extern "C" void func_02019144();
extern "C" void func_02019100();
extern "C" unsigned char data_0209f20c, data_0209f294, data_0209f2c4;
extern "C" int data_0209fc48, data_0209b454;
extern "C" unsigned short data_0209b274;
extern "C" unsigned char data_0208751c[];
extern "C" int func_0205b070(int);
extern "C" void** func_0205b274(int);

int main()
{
    PORT_INSTALL_FAULT_PROBE();
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv()) return 1;
    func_02059bc0();
    __sinit_02075154();
    __sinit_0207501c();
    data_020a0eac_c = data_020a0ea0;
    run_overlay_initializer(2,__sinit_ov002_02100560);
    // Direct scene probes must reproduce the graphics baseline established by
    // original boot func_0201a054. Stage deliberately skips Scene's graphics
    // reset, so its lit models otherwise inherit zero light colors.
    *(unsigned*)(data_0209ee90+0x44)=0x100;
    ntr::io_write(0x040004c8,0x296a5800,4);
    ntr::io_write(0x040004cc,0x7fff,4);
    ntr::io_write(0x040004c0,0x7fff,4);
    ntr::io_write(0x040004c4,0,4);
    run_overlay_initializer(2,__sinit_ov002_02107298);
    run_overlay_initializer(2,__sinit_ov002_021019d0);
    run_overlay_initializer(2,__sinit_ov002_02106e40);
    run_overlay_initializer(2,__sinit_ov002_021071f4);
    run_overlay_initializer(2,sm64ds_init_common_level_actor_profiles);
    run_overlay_initializer(2,__sinit_ov002_0210804c);
    __sinit_02073a24();
    __sinit_02074da8();
    if(!std::getenv("PLAYER_STAGE_CONSTRUCT"))sm64ds_init_key_resources();
    sm64ds_init_level_scripts();
    if (data_0209cab4-data_0209caa0!=0x14 || data_0209cad2-data_0209caa0!=0x32 ||
        data_0209cae4-data_0209caa0!=0x44 || data_0209caf4-data_0209caa0!=0x54) {
        fprintf(stderr,"FAIL save-state interior aliases\n");return 1;
    }
    // Native relocated level record supplies collision, surfaces and model IDs.
    const auto *level=data_ov009_02112bdc;
    const unsigned model_id=*(const unsigned short*)(level+8);
    const unsigned kcl_id=*(const unsigned short*)(level+10);
    auto *clps=*(CLPS_Block*const*)level;
    if(model_id!=1943 || kcl_id!=1941 || std::memcmp(clps,"CLPS",4)) {
        fprintf(stderr,"FAIL castle level record\n"); return 1;
    }
    printf("Castle level: model=%u KCL=%u native CLPS\n",model_id,kcl_id);
    if(std::getenv("PLAYER_STAGE_CONSTRUCT")) {
        printf("Native Stage: original complete sound boot\n");
        func_020133bc();
        // BootScene's func_0201a2f8 preserves INFO/FAT and common audio before
        // later scenes restore the group heap. func_020133bc alone is not it.
        _ZN5Sound16LoadInitialGroupEi(0);
        if(!func_0205b070(1))return 1;
        while(func_0205b274(0)){}
        // This diagnostic enters castle grounds directly. The normal boot/menu
        // sets the destination; its initial -1 sentinel is not a valid level.
        data_02092110=1;
        // StartFile normally sets this before creating Stage. This direct
        // construction probe must retain its entrance Player for the camera.
        SetNumPlayers(1);
        printf("Native Stage: loading original boot message bank\n");
        func_0201fe08();
        initialize_player_profile();
        initialize_camera_profile();
        run_overlay_initializer(2,__sinit_ov002_02107370);
        sm64ds_init_common_scenery_profiles();
        // Stage owns object-overlay loading, including fixed overlay98.
        __sinit_02074e84();
        func_0201a4e4();
        struct SceneProfile { Stage*(*create)();unsigned short behavior,render; };
        static SceneProfile stage_profile={dScStage_c_classInit,3,6};
        data_020a4bb8[3]=&stage_profile;
        printf("Native Stage: constructing through original factory\n");fflush(stdout);
        auto *stage=(Stage*)func_02043098(3,0,0,2);
        printf("Native Stage: object=%p\n",(void*)stage);
        if(stage && std::getenv("PLAYER_STAGE_FRAMES")) {
            const int frames=std::atoi(std::getenv("PLAYER_STAGE_FRAMES"));
            if(frames<1 || frames>3600)return 1;
            std::int16_t pcm[2*1093];
            auto trace_camera=[](const char* phase) {
                if(!data_0209f318)return;
                const auto* c=reinterpret_cast<const int*>(data_0209f318);
                printf("Camera phase %s look_at=%x,%x,%x eye=%x,%x,%x\n",phase,c[0x80/4],c[0x84/4],c[0x88/4],c[0x8c/4],c[0x90/4],c[0x94/4]);
            };
            for(int frame=0;frame<frames;++frame) {
                const bool report=frame<3 || frame%60==0 || frame==frames-1;
                if(frame<3)trace_camera("start");
                ntr::gx_begin_frame();
                player_input::submit(0);
                if(report)printf("Stage frame=%d prepare\n",frame);
                func_02019390();
                if(frame<3)trace_camera("prepared");
                if(report)printf("Stage frame=%d scheduler begin\n",frame);
                func_02044120();
                if(frame<3)trace_camera("scheduled");
                if(report)printf("Stage frame=%d graph callback\n",frame);
                func_02019404();
                if(frame<3)trace_camera("graph");
                // Frame submission plus the original VBlank upload/completion
                // callbacks. func_02019404 is GraphCallback1; it does not
                // publish OAM, scrolls, or GraphCallback2's display changes.
                func_020190b8();
                func_02019144();
                func_02019100();
                func_020132d8();
                if(!func_0205b070(1))return 1;
                while(func_0205b274(0)){}
                // Initial diagnostic follows the original 30 Hz simulation.
                const unsigned samples=((frame+1)*32768/30)-(frame*32768/30);
                if(!sm64ds_native_sound_render(pcm,samples))return 1;
                size_t triangles=0;ntr::gx_polygons(triangles);
                if(report)printf("Stage frame=%d done triangles=%u script=%x timer=%u\n",frame,(unsigned)triangles,data_0209fc48,data_0209b274);
                if(frame==0 || frame==frames-1 || (std::getenv("PLAYER_STAGE_REFERENCE") && data_0209b274==120)) {
                    size_t count=0;const auto* tris=ntr::gx_polygons(count);
                    unsigned positive=0,finite=0,onscreen=0;
                    float minw=1e30f,maxw=-1e30f,minx=1e30f,maxx=-1e30f,miny=1e30f,maxy=-1e30f;
                    for(size_t t=0;t<count;++t)for(const auto& v:tris[t].v) {
                        positive+=v.w>0;finite+=std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.w);
                        onscreen+=v.x>=0&&v.x<256&&v.y>=0&&v.y<192&&v.w>0;
                        if(v.w<minw)minw=v.w;if(v.w>maxw)maxw=v.w;
                        if(v.x<minx)minx=v.x;if(v.x>maxx)maxx=v.x;
                        if(v.y<miny)miny=v.y;if(v.y>maxy)maxy=v.y;
                    }
                    printf("Stage geometry: positive=%u finite=%u onscreen=%u w=%g..%g x=%g..%g y=%g..%g\n",positive,finite,onscreen,minw,maxw,minx,maxx,miny,maxy);
                    if(std::getenv("PLAYER_STAGE_MATERIALS")) {
                        for(size_t t=0;t<count;++t)if((tris[t].polygon_attr>>24)==25)for(unsigned v=0;v<3;++v)
                            printf("Stage letter %u/%u: xy=%g,%g clip=%g,%g,%g,%g uv=%g,%g\n",(unsigned)t,v,tris[t].v[v].x,tris[t].v[v].y,tris[t].v[v].cx,tris[t].v[v].cy,tris[t].v[v].cz,tris[t].v[v].cw,tris[t].v[v].u,tris[t].v[v].v);
                        for(size_t t=0;t<count && t<12;++t)for(unsigned v=0;v<3;++v)
                            printf("Stage sky %u/%u: xy=%g,%g w=%g uv=%g,%g\n",(unsigned)t,v,tris[t].v[v].x,tris[t].v[v].y,tris[t].v[v].w,tris[t].v[v].u,tris[t].v[v].v);
                        for(size_t t=0;t<count;++t) {
                            const auto& tri=tris[t];
                            bool seen=false;
                            for(size_t j=0;j<t;++j)if(tris[j].tex==tri.tex && tris[j].polygon_attr==tri.polygon_attr){seen=true;break;}
                            if(seen)continue;
                            unsigned partial=0,opaque=0,transparent=0;
                            if(tri.tex)for(int p=0;p<tri.tw*tri.th;++p){unsigned a=tri.tex[p]>>24;partial+=a>0&&a<255;opaque+=a==255;transparent+=a==0;}
                            printf("Stage material: attr=%08x texture=%p size=%dx%d alpha=%u/%u/%u first=%g,%g,%g\n",tri.polygon_attr,tri.tex,tri.tw,tri.th,transparent,partial,opaque,tri.v[0].x,tri.v[0].y,tri.v[0].w);
                            if(tri.polygon_attr==0x131f8080)for(unsigned v=0;v<3;++v)
                                printf("Stage cloud %u: xy=%g,%g clip=%g,%g,%g,%g uv=%g,%g color=%08x\n",v,tri.v[v].x,tri.v[v].y,tri.v[v].cx,tri.v[v].cy,tri.v[v].cz,tri.v[v].cw,tri.v[v].u,tri.v[v].v,tri.v[v].color);
                            if(tri.tex) {
                                static ntr::Framebuffer texture;
                                for(int y=0;y<ntr::SCREEN_H;++y)for(int x=0;x<ntr::SCREEN_W;++x)
                                    texture.px[y][x]=tri.tex[(y%tri.th)*tri.tw+(x%tri.tw)];
                                char name[100];std::snprintf(name,sizeof name,"stage-material-%03d-%04u.bmp",frame,(unsigned)t);
                                ntr::ppu_write_bmp(name,texture);
                            }
                        }
                    }
                    if(data_0209f318) {
                        const auto* camera=reinterpret_cast<const unsigned char*>(data_0209f318);
                        const auto* projection=reinterpret_cast<const int*>(camera+0xf8);
                        printf("Stage camera: proj=%x,%x,%x viewport=%u,%u,%u,%u\n",projection[0],projection[1],projection[2],camera[0x10c],camera[0x10d],camera[0x10e],camera[0x10f]);
                        printf("Stage camera: scaleW=%x fov=%x\n",*(unsigned*)(data_0209ee90+0x44),*(const unsigned short*)(camera+0x17a));
                        const auto* view=reinterpret_cast<const int*>(camera+0x50);
                        for(int row=0;row<4;++row)printf("Stage view[%d]=%x,%x,%x\n",row,view[row*3],view[row*3+1],view[row*3+2]);
                        const auto* eye=reinterpret_cast<const int*>(camera+0x80);
                        printf("Stage look_at=%x,%x,%x eye=%x,%x,%x\n",eye[0],eye[1],eye[2],eye[3],eye[4],eye[5]);
                        printf("Stage camera flags=%x state=%p mode=%p player=%p\n",*(const unsigned*)(camera+0x154),*(void*const*)(camera+0x138),*(void*const*)(camera+0x13c),*(void*const*)(camera+0x110));
                        printf("Stage camera gates=%u,%u,%u demo=%x script=%x\n",data_0209f20c,data_0209f294,data_0209f2c4,data_0209b454,data_0209fc48);
                        const auto* state=*(const unsigned*const*)(camera+0x138);
                        printf("Stage camera callbacks=%x,%x,%x,%x\n",state[0],state[1],state[2],state[3]);
                        char camera_name[80];std::snprintf(camera_name,sizeof camera_name,"stage-frame-%03d-camera.json",frame);
                        FILE* camera_file=std::fopen(camera_name,"w");
                        if(!camera_file)return 1;
                        // Camera stores look-at first (+0x80), eye second (+0x8c).
                        const unsigned script_offset=(unsigned)data_0209fc48-(unsigned)data_0208751c;
                        const unsigned script_source=script_offset<0x2bc4?0x0208751c+script_offset:0;
                        std::fprintf(camera_file,"{\"frame\":%d,\"timer\":%u,\"script_native\":%u,\"script_source\":%u,\"look_at\":[%d,%d,%d],\"eye\":[%d,%d,%d],\"view\":[",frame,data_0209b274,(unsigned)data_0209fc48,script_source,eye[0],eye[1],eye[2],eye[3],eye[4],eye[5]);
                        for(int i=0;i<12;++i)std::fprintf(camera_file,"%s%d",i?",":"",view[i]);
                        extern int data_0209b41c[];
                        std::fprintf(camera_file,"],\"inverse_view\":[");
                        for(int i=0;i<12;++i)std::fprintf(camera_file,"%s%d",i?",":"",data_0209b41c[i]);
                        std::fprintf(camera_file,"],\"ppu_registers\":[");
                        for(unsigned engine=0;engine<2;++engine) {
                            std::fprintf(camera_file,"%s[",engine?",":"");
                            for(unsigned offset=0;offset<0x70;offset+=4)
                                std::fprintf(camera_file,"%s%lu",offset?",":"",static_cast<unsigned long>(ntr::io_read(0x04000000+engine*0x1000+offset,4)));
                            std::fprintf(camera_file,"]");
                        }
                        if(std::fprintf(camera_file,"]}\n")<0 || std::fclose(camera_file)!=0)return 1;
                    }
                    static ntr::Framebuffer geometry;
                    std::memset(&geometry,0,sizeof geometry);
                    ntr::gx_render(geometry);
                    char gx_name[80];std::snprintf(gx_name,sizeof gx_name,"stage-frame-%03d-gx.bmp",frame);
                    if(!ntr::ppu_write_bmp(gx_name,geometry))return 1;
                    for(unsigned engine=0;engine<2;++engine) {
                        static ntr::Framebuffer fb;
                        ntr::ppu_scanout_scene(ntr::Engine(engine),fb);
                        char name[80];std::snprintf(name,sizeof name,"stage-frame-%03d-%u.bmp",frame,engine);
                        if(!ntr::ppu_write_bmp(name,fb))return 1;
                    }
                }
            }
        }
        return stage?0:1;
    }
    // Register castle grounds collision before the spawn state probes its floor.
    static SharedFilePtr ground_file;
    ground_file.Construct(kcl_id);
    static dBgW_Kc *ground = new dBgW_Kc;
    auto *kcl = (KCL_File *)dBgW_Kc::LoadFile(ground_file);
    if (!kcl) { fprintf(stderr,"FAIL castle grounds KCL load\n"); return 1; }
    ground->SetFile(kcl, *clps);
    if (!ground->Enable(nullptr)) { fprintf(stderr,"FAIL ground registration\n"); return 1; }
    if(std::getenv("PLAYER_MOVING_MESH_TEST")) {
        // Exercise the real moving-background virtuals against the castle KCL,
        // once unchanged and once translated100 world units upwards.
        static_assert(sizeof(dBgCh_Lin)==0x78,"moving-mesh scratch query ABI");
        Vector3 reference_start;reference_start.x=-1200*4096;reference_start.y=800*4096;reference_start.z=6800*4096;
        Vector3 reference_end=reference_start;reference_end.y=-1200*4096;
        dBgCh_Lin reference;reference.SetObjAndLine(reference_start,reference_end,nullptr);
        if(!ground->DetectClsn(reference)) { fprintf(stderr,"FAIL reference line missed castle\n");return 1; }
        const int reference_y=reference.lineEnd.y;
        printf("Static line reference: y=%d (vertical floor reference=%d)\n",reference_y,254*4096);
        Vector3 sphere_center=reference_start;sphere_center.y=270*4096;
        dBgCh_SphCrr reference_sphere;
        _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(&reference_sphere,&sphere_center,50*4096,nullptr);
        if(!ground->DetectClsn(reference_sphere) || !(reference_sphere.flags&4)) {
            fprintf(stderr,"FAIL reference sphere missed castle floor\n");return 1;
        }
        for(int lift : {0,100*4096}) {
            int matrix[12]={4096,0,0,0,4096,0,0,0,4096,0,lift,0};
            dBgW_KcMbg moving;
            _ZN10dBgW_KcMbg7SetFileEP8KCL_FileRK9Matrix4x35Fix12IiEsR10CLPS_Block(
                &moving,kcl,matrix,4096,0,clps);
            Vector3 point=reference_start;point.y+=lift;
            Vector3 local;func_02039e48(&moving,&point,&local);
            auto *inverse=(int*)((unsigned char*)&moving+0x168);
            printf("Moving inverse: diag=(%d,%d,%d) t=(%d,%d,%d) local=(%d,%d,%d)\n",
                inverse[0],inverse[4],inverse[8],inverse[9],inverse[10],inverse[11],local.x,local.y,local.z);
            dBgCh_Gnd ray;ray.SetObjAndPos(point,nullptr);
            // Direct collider dispatch bypasses the query's public DetectClsn
            // wrapper, which normally resets its previous hit before the walk.
            func_02037464(&ray);
            ray.mProbeHeight=2000*4096;
            dBgW_Kc *dispatch=&moving;
            int hit=dispatch->DetectClsn(ray);
            printf("Moving mesh ground: lift=%d hit=%d y=%d expected=%d\n",lift,hit,ray.clsnY,reference_y+lift);
            if(!hit || ray.clsnY!=reference_y+lift) return 1;
            Vector3 end=reference_end;end.y+=lift;
            dBgCh_Lin line;line.SetObjAndLine(point,end,nullptr);
            hit=dispatch->DetectClsn(line);
            printf("Moving mesh line: lift=%d hit=%d y=%d\n",lift,hit,line.lineEnd.y);
            if(!hit || line.lineEnd.y!=reference_y+lift) return 1;
            Vector3 center=sphere_center;center.y+=lift;
            dBgCh_SphCrr sphere;
            _ZN12dBgCh_SphCrr15SetObjAndSphereERK7Vector35Fix12IiEP8dActor_c(&sphere,&center,50*4096,nullptr);
            hit=dispatch->DetectClsn(sphere);
            printf("Moving mesh sphere: lift=%d hit=%d flags=%x disp=(%d,%d,%d)\n",
                   lift,hit,sphere.flags,sphere.disp.x,sphere.disp.y,sphere.disp.z);
            if(!hit || !(sphere.flags&4) || sphere.disp.x!=reference_sphere.disp.x ||
                sphere.disp.y!=reference_sphere.disp.y || sphere.disp.z!=reference_sphere.disp.z) return 1;
        }
        printf("PASS moving mesh ground/line/sphere translation and destruction\n");
        return 0;
    }
    const bool stage_test=std::getenv("PLAYER_STAGE")!=nullptr;
    Model *stage_model=nullptr;
    if (stage_test) {
        static SharedFilePtr stage_file;
        stage_file.Construct(model_id);
        auto *file=Model::LoadFile(stage_file);
        stage_model=new Model;
        if (!file || !stage_model->DoSetFile((char*)file,0,-1)) {
            fprintf(stderr,"FAIL castle grounds model load\n"); return 1;
        }
    }
    initialize_player_profile();

    static int position[3] = {-1200 * 4096, 254 * 4096, 6800 * 4096};
    static unsigned char area[12] = {};
    area[4] = 1;
    data_0209f314 = area;
    data_0209b460 = position;
    data_0209b44c = 0;
    data_0208e378 = -1;
    if (std::getenv("PLAYER_OBJECT_TEST")) {
        // Probe fixture only: a parent node is required for dActor_c::Spawn.
        // Full Stage ownership/lifecycle must replace this before gameplay.
        alignas(4) static unsigned char scene_root[0x50]={};
        _ZN7fBase_c9SceneNodeC1Ev(scene_root+0x14);
        *(void**)(scene_root+0x24)=scene_root;
        data_0209f5c0=scene_root;
        // Connect the diagnostic root so the original graph reconciliation
        // can reach its children. Full Stage ownership still replaces this.
        if(!func_0203b438(data_020a4b6c,scene_root+0x14,nullptr)) return 1;
        data_0209f21c=1; data_0209f220=1;
        data_0209f2f8=1; data_02092110=-1;
        // An empty save starts with the intro flag clear. A resumed session
        // requires the full sound-heap startup before Player voice loading.
        if(std::getenv("PLAYER_ACTIVE_SESSION")) data_0209caa0[8]|=0x80;
        data_0209fc5c[0]=1; // keep the spawned entrance Player as camera target
        LoadOverlay(9);
        LoadOverlay(85);
        // Stage owns object-overlay loading, including fixed overlay98.
        if(std::getenv("PLAYER_RABBIT_UNLOCKED")) {
            // Diagnostic save progression: unlock normal and later rabbits.
            ((unsigned*)data_0209caa0)[1]|=0x40000000u;
            ((unsigned*)data_0209caa0)[2]|=0x80000u;
            printf("Rabbit fixture: progression flags unlocked\n");
        }
        static FaderWipe entrance_faders[7];
        data_0209f324=entrance_faders;
        data_020a4b58=[](void *value) -> int {
            unsigned id=(uintptr_t)value;
            printf("Level actor factory id=%u (0x%x)\n",id,id);
            if(id==187 && std::getenv("PLAYER_RABBIT_UNLOCKED")) {
                // Initialization-only fixture: first castle rabbits belong to
                // character1/3. Select1 without rerunning Player audio startup.
                for(dActor_c *a=dActor_c::Next(nullptr);a;a=dActor_c::Next(a)) {
                    if(a->actorID==191) {
                        ((Player*)a)->mCharacter=1;
                        a->param1=1;
                    }
                }
            }
            if(id>=512 || !data_020a4bb8[id]) {
                auto *root=(fBase_c*)data_0209f5c0;
                unsigned count=0;
                for(auto *node=root->manager.sceneNode.child;node && count<512;node=node->next,++count) {
                    auto *actor=node->owner;
                    printf("Loaded actor id=%u alive=%u param=%x\n",actor->actorID,actor->aliveState,actor->param1);
                }
                if(data_0209f318) {
                    const auto *camera=data_0209f318;
                    printf("Native Camera: alive=%u target=%p state=%p pos=(%d,%d,%d) look=(%d,%d,%d)\n",
                        camera->aliveState,(void*)camera->mTargetPlayer,(void*)camera->mState,
                        camera->pos.x,camera->pos.y,camera->pos.z,
                        camera->lookAt.x,camera->lookAt.y,camera->lookAt.z);
                }
                for(unsigned variant=0;variant<5;++variant) {
                    unsigned trees=0;
                    auto *node=data_ov002_02110a48[variant];
                    while(node && trees<512) {
                        ++trees;
                        node=*(unsigned char**)(node+0x48);
                    }
                    printf("Native trees: variant=%u nodes=%u%s\n",variant,trees,node?" INVALID LIST":"");
                    if(node) std::abort();
                }
                fprintf(stderr,"FATAL: level actor profile not integrated id=%u (0x%x)\n",id,id);
                std::abort();
            }
            return 2;
        };
        printf("Castle: entering original global object loader\n");
        LoadObjects(**(LVL_Overlay::ObjTable*const*)(level+4),-1,0);
        const auto *sub=*(const unsigned char*const*)(level+16);
        for(unsigned i=0;i<level[20];++i,sub+=12) {
            auto *table=*(LVL_Overlay::ObjTable*const*)sub;
            if(table) LoadObjects(*table,i,0);
        }
        unsigned loaded=0,active=0;
        for(dActor_c *a=dActor_c::Next(nullptr);a;a=dActor_c::Next(a)) {
            ++loaded;if(a->aliveState==1) ++active;
            printf("Castle loaded id=%u alive=%u param=%x\n",a->actorID,a->aliveState,a->param1);
        }
        printf("Castle original object loading complete: total=%u active=%u\n",loaded,active);
        if(std::getenv("PLAYER_NATIVE_SCHEDULER")) {
            for(int frame=0;frame<120;++frame) {
                player_input::submit(0);
                ntr::gx_reset();
                if(frame<3 || frame%30==0) {printf("Native scheduler frame=%d begin\n",frame);fflush(stdout);}
                func_02044120();
                if(frame<3 || frame%30==0 || frame==119) {
                    size_t triangles=0;ntr::gx_polygons(triangles);
                    unsigned count=0,alive=0;
                    for(dActor_c *a=dActor_c::Next(nullptr);a;a=dActor_c::Next(a)) {++count;alive+=a->aliveState==1;}
                    printf("Native scheduler frame=%d end triangles=%u actors=%u active=%u camera=(%d,%d,%d)\n",
                           frame,(unsigned)triangles,count,alive,data_0209f318->pos.x,data_0209f318->pos.y,data_0209f318->pos.z);
                }
                if(frame==119) {
                    static ntr::Framebuffer fb;ntr::gx_render(fb);
                    if(!ntr::ppu_write_bmp("castle-scheduler-actors.bmp",fb)) return 1;
                }
            }
        }
        if(std::getenv("PLAYER_OBJECT_STEP")) {
            // Dependency probe, not the final priority-ordered Stage scheduler.
            // Snapshot: actors created by a behavior are eligible next frame.
            for(int frame=0;frame<2;++frame) {
                std::vector<dActor_c*> actors;
                for(dActor_c *a=dActor_c::Next(nullptr);a;a=dActor_c::Next(a))
                    if(a->aliveState==1) actors.push_back(a);
                player_input::submit(0);
                for(auto *a:actors) {
                    if(a->aliveState!=1) continue;
                    printf("Castle step frame=%d id=%u uid=%u\n",frame,a->actorID,a->uniqueID);
                    fflush(stdout);
                    int result=func_02043288(a);
                    printf("Castle step result=%d\n",result);
                }
                if(std::getenv("PLAYER_OBJECT_RENDER")) {
                    ntr::gx_reset();
                    // Camera derives from View/fBase, not dActor: it is not
                    // present in dActor_c::Next's actor-only list.
                    if(!data_0209f318) return 1;
                    printf("Castle camera render frame=%d\n",frame);fflush(stdout);
                    printf("Castle camera render result=%d\n",func_0204322c(data_0209f318));
                    printf("Castle camera matrix:");for(int k=0;k<12;++k)printf(" %d",data_0209b3ec[k]);puts("");
                    if(stage_model) {
                        const Vector3 scale={125*4096,125*4096,125*4096};
                        stage_model->Render(&scale);
                    }
                    for(auto *a:actors) if(a->aliveState==1 && a->actorID!=332) {
                        printf("Castle render frame=%d id=%u\n",frame,a->actorID);fflush(stdout);
                        printf("Castle render result=%d\n",func_0204322c(a));
                    }
                    size_t count=0;ntr::gx_polygons(count);printf("Castle render triangles=%u\n",(unsigned)count);
                    static ntr::Framebuffer fb;
                    ntr::gx_render(fb);
                    if(!ntr::ppu_write_bmp("castle-actors-native.bmp",fb)) return 1;
                }
            }
            printf("Castle two-frame behavior dependency probe complete\n");
        }
        fprintf(stderr,"FAIL object-loading probe reached end; full scene lifecycle still required\n");
        return 1;
    }

    printf("Player: constructing and initializing native actor at DS reference spawn\n");
    Player *player = (Player *)func_02043098(0xbf, 0, 0, 2);
    if (!player || player->aliveState != 1) {
        fprintf(stderr, "Player initialization failed\n"); return 1;
    }
    printf("Player: initialized, world=(%d,%d,%d)\n",
           player->mPosX, player->mPosY, player->mPosZ);
    if (player->mPosY == (-2147483647 - 1)) {
        fprintf(stderr, "FAIL Player has no ground: stage collision must be initialized before spawn\n");
        return 1;
    }
    if (player->mPosY != 254*4096) {
        fprintf(stderr,"FAIL grounded Player differs from DS reference height\n"); return 1;
    }
    data_0209b3ec[0]=data_0209b3ec[4]=data_0209b3ec[8]=4096;
    data_0209b3ec[9]=-position[0]/8;
    data_0209b3ec[10]=-position[1]/8;
    data_0209b3ec[11]=-position[2]/8-200*4096;
    if (stage_test) {
        const int c=3913,s=1210; // fixed camera pitched down approximately17 degrees
        data_0209b3ec[4]=c; data_0209b3ec[5]=s;
        data_0209b3ec[7]=-s; data_0209b3ec[8]=c;
        const int cy=position[1]/8,cz=position[2]/8;
        data_0209b3ec[10]=-int(((long long)c*cy-(long long)s*cz)/4096);
        data_0209b3ec[11]=-int(((long long)s*cy+(long long)c*cz)/4096)-200*4096;
        // Row-vector view rotation: inverse places the eye above the target.
        const long long eye_y=-((long long)c*data_0209b3ec[10]+(long long)s*data_0209b3ec[11])/4096;
        if (eye_y<=cy) { fprintf(stderr,"FAIL reference camera is below the ground target\n"); return 1; }
    }
    _ZN7Clipper13Func_020156DCEitii(data_0209f43c,0x1555,0xe38,0x1000,0x1388000);
    const bool gpu_test=std::getenv("PLAYER_GPU")!=nullptr;
    if (gpu_test) v3ds_renderer_init();
    const bool movement_test=std::getenv("PLAYER_MOVE_TEST")!=nullptr;
    const bool jump_test=std::getenv("PLAYER_JUMP_TEST")!=nullptr;
    int peak_y=player->mPosY;
    int max_step=0;
    data_0209caa0[8]|=0x80; // original input gate: active normal-play session
    data_0209f21c=1;
    player_input::reset();
    const bool live_input=std::getenv("PLAYER_LIVE_INPUT")!=nullptr;
    const bool circle_test=std::getenv("PLAYER_CIRCLE_TEST")!=nullptr;
    // Immutable castle geometry snapshot, scoped to this fixed-light test scene.
    // Camera/projection changes invalidate it; texels are owned independently of GX.
    std::vector<ntr::GxTriangle> stage_geometry;
    std::map<const uint32_t*,std::vector<uint32_t>> stage_textures;
    std::array<int,28> stage_key{};
    bool stage_cached=false;
    int stage_cache_builds=0;
    const bool camera_test=std::getenv("PLAYER_CAMERA_TEST")!=nullptr;
    const bool cache_stage=std::getenv("PLAYER_STAGE_UNCACHED")==nullptr;
    size_t total_triangles=0;
    uint64_t stage_ticks=0,player_ticks=0,present_ticks=0;
    for (int frame=0; frame<120; ++frame) {
        if (camera_test && frame==60) data_0209b3ec[9]+=50*4096;
        if (live_input) {
            if (!gpu_test) player_input::wait_frame();
            player_input::poll();
        } else {
            const uint32_t buttons=((movement_test && frame>=60)?(circle_test?(1u<<31):0x80):0)
                | ((jump_test && frame>=60 && frame<75)?2:0);
            player_input::submit(buttons);
            const uint16_t expected_held=((movement_test && frame>=60)?0x80:0)
                | ((jump_test && frame>=60 && frame<75)?2:0);
            const uint16_t expected_pressed=frame==60?expected_held:0;
            if (data_0209f49c!=expected_held || data_0209f49e!=expected_pressed) {
                fprintf(stderr,"FAIL processed held/pressed at frame %d: %x/%x\n",frame,data_0209f49c,data_0209f49e); return 1;
            }
        }
        if (frame<5 || frame%30==0) printf("Player frame %d: world=(%d,%d,%d), state=%p, step=%u, opacity=%u, flags=%x\n",frame,player->mPosX,player->mPosY,player->mPosZ,(void*)player->mState,player->mStateStep,player->mOpacity,player->mFlags);
        const int old_x=player->mPosX,old_z=player->mPosZ;
        if (func_02043288(player)!=1) { fprintf(stderr,"FAIL Player behavior\n"); return 1; }
        int step=std::abs(player->mPosX-old_x)+std::abs(player->mPosZ-old_z);
        if (step>max_step) max_step=step;
        if (step>100*4096) { fprintf(stderr,"FAIL Player discontinuous movement\n"); return 1; }
        if (player->mPosY>peak_y) peak_y=player->mPosY;
        if (!live_input && !jump_test && player->mPosY!=254*4096) { fprintf(stderr,"FAIL Player left reference flat ground\n"); return 1; }
        if (stage_test && frame==119) {
            for (int i=0;i<4;++i) if(player->mBodyModels[i]) {
                const int *m=(const int*)((const char*)player->mBodyModels[i]+0x1c);
                printf("body %d matrix:",i);for(int j=0;j<12;++j)printf(" %d",m[j]);puts("");
            }
            const int *m=(const int*)&stage_model->mat4x3;
            printf("stage matrix:");for(int j=0;j<12;++j)printf(" %d",m[j]);puts("");
            printf("camera matrix:");for(int j=0;j<12;++j)printf(" %d",data_0209b3ec[j]);puts("");
        }
        ntr::gx_reset();
        NTR_MMIO(uint32_t,0x04000440)=0;
        int projection[16]={gpu_test?(jump_test?54:82):(jump_test?68:102),0,0,0, 0,jump_test?91:137,0,0, 0,0,-4,0, 0,-2048,0,4096};
        if (stage_test) {
            const int perspective[16]={4257,0,0,0, 0,7094,0,0, 0,0,-4100,-4096, 0,0,-8196,0};
            for (int i=0;i<16;++i) projection[i]=perspective[i];
        }
        for (int word:projection) NTR_MMIO(uint32_t,0x04000458)=word;
        NTR_MMIO(uint32_t,0x04000440)=1;
        NTR_MMIO(uint32_t,0x04000454)=0;
        NTR_MMIO(uint32_t,0x04000580)=0xbfff0000;
        ntr::gx_set_light(0,-0.4f,-0.6f,-0.7f,0x7fff);
        ntr::gx_enable_lights(1);
        const uint64_t before_stage=func_02059650();
        if (stage_model) {
            const Vector3 scale={125*4096,125*4096,125*4096};
            std::array<int,28> key;
            std::memcpy(key.data(),data_0209b3ec,12*sizeof(int));
            std::memcpy(key.data()+12,projection,16*sizeof(int));
            if (cache_stage && stage_cached && key==stage_key) {
                ntr::gx_append_polygons(stage_geometry.data(),stage_geometry.size());
            } else {
                stage_model->Render(&scale);
                if (cache_stage) {
                    size_t count=0; const auto *triangles=ntr::gx_polygons(count);
                    stage_geometry.assign(triangles,triangles+count);
                    stage_textures.clear();
                    for (auto &tri:stage_geometry) if (tri.tex) {
                        auto &pixels=stage_textures[tri.tex];
                        if(pixels.empty()) pixels.assign(tri.tex,tri.tex+tri.tw*tri.th);
                        tri.tex=pixels.data();
                    }
                    stage_key=key;stage_cached=true;++stage_cache_builds;
                }
            }
        }
        const uint64_t before_player=func_02059650();
        stage_ticks+=before_player-before_stage;
        if (player->Render()!=1) { fprintf(stderr,"FAIL Player render\n"); return 1; }
        size_t triangles=0; ntr::gx_polygons(triangles); total_triangles+=triangles;
        const uint64_t before_present=func_02059650();
        player_ticks+=before_present-before_player;
        if (gpu_test && !player_present(frame==119,stage_test)) return 1;
        present_ticks+=func_02059650()-before_present;
        if (frame==119 || (jump_test && frame==68)) {
            static ntr::Framebuffer fb;
            ntr::gx_render(fb);
            if (!ntr::ppu_write_bmp(frame==68?"player-jump.bmp":"player-native.bmp",fb)) { fprintf(stderr,"FAIL Player capture\n"); return 1; }
        }
        if (player->mPosY == (-2147483647-1)) { fprintf(stderr,"FAIL Player lost ground\n"); return 1; }
    }
    printf("Player: 120 behavior/render iterations, triangles=%zu, final=(%d,%d,%d), max horizontal step=%d\n",total_triangles,player->mPosX,player->mPosY,player->mPosZ,max_step);
    if (movement_test && player->mPosX==position[0] && player->mPosZ==position[2]) {
        fprintf(stderr,"FAIL Player did not respond to movement input\n"); return 1;
    }
    if (jump_test) {
        printf("Player jump: peak Y=%d, landing Y=%d\n",peak_y,player->mPosY);
        if (peak_y<=254*4096 || player->mPosY!=254*4096) {
            fprintf(stderr,"FAIL Player jump/landing\n"); return 1;
        }
    }
    printf("Render timings mean ms: stage=%.3f player=%.3f present=%.3f (includes synchronization/capture)\n",
        stage_ticks*64000.0/33513982.0/120,player_ticks*64000.0/33513982.0/120,present_ticks*64000.0/33513982.0/120);
    if (stage_test && cache_stage) {
        printf("Stage cache rebuilds=%d\n",stage_cache_builds);
        if(stage_cache_builds!=(camera_test?2:1)) { fprintf(stderr,"FAIL stage cache invalidation\n"); return 1; }
    }
    if (gpu_test) v3ds_exit();
    if (!total_triangles) { fprintf(stderr,"FAIL Player rendered no geometry\n"); return 1; }
    return 0;
}
