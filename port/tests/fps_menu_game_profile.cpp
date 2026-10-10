// Isolated phase profiler derived from fps_menu_benchmark.cpp; separate binary.
// The original Behavior/Render/graph callback advance at fixed frame counts.
// Timers instrument only the final 120 updates; their overhead is included.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "dScDSMT_c.h"
#include "unmatched/native_overlay.h"
#include "audio/sound_native.h"
#include "platform/3ds/viewer_3ds.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace Sound {void LoadInitialGroup(int);}
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void __sinit_02073a24();void __sinit_02074da8();void func_020133bc();void func_0201fe08();
void func_020132d8();void func_020190b8();void func_02019144();void func_02019100();
void func_02019390();void func_02019404();
int func_0205b070(int);void** func_0205b274(int);
unsigned short* func_0203dabc(int);int func_0203da9c();
dScDSMT_c* dScDSMT_c_classInit();
extern void* data_020a0ea0;extern void* data_020a0eac;
extern unsigned short data_020a4b54;
extern unsigned char data_0209ee90[];
extern unsigned char* data_ov007_0210342c;
extern int data_020a0db0;
}
bool player_present(bool,bool);
#define CHECK(c) do{if(!(c)){std::printf("FATAL: menu FPS probe line=%d %s\n",__LINE__,#c);return 1;}}while(0)
static const char* phase_names[]={"GX begin","GraphCallback0/reset","Behavior","Render/GX decode","GraphCallback1","frame advance","VBlank begin","GraphCallback2/OAM commit","VBlank end","sound queue","native audio","presentation"};
static double phase_ms[12]{},phase_stamp;
static unsigned long long queue_events,triangle_total,texture_entries_max,texture_bytes_max;
static void phase_begin(unsigned frame){if(frame>=840)phase_stamp=v3ds_time_ms();}
static void phase_end(unsigned frame,unsigned phase){if(frame>=840){double now=v3ds_time_ms();phase_ms[phase]+=now-phase_stamp;phase_stamp=now;}}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/fps-menu-validation.sav",1);
    setenv("PLAYER_NATIVE_COMPOSITE","1",1);setenv("PLAYER_PREPARED_PPU","1",1);
    setenv("PLAYER_PROFILE_PPU","1",1);setenv("PLAYER_PROFILE_GPU","1",1);setenv("PLAYER_PROFILE_BLIT","1",1);
    setenv("SM64DS_PROFILE","1",1); // Existing conversion/batching GPU profile.
    CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
    *reinterpret_cast<unsigned*>(data_0209ee90+0x44)=0x100;
    func_02059bc0();__sinit_02075154();__sinit_0207501c();data_020a0eac=data_020a0ea0;
    __sinit_02073a24();__sinit_02074da8();func_020133bc();Sound::LoadInitialGroup(0);func_0201fe08();
    CHECK(sm64ds_try_load_overlay(7)==OVERLAY_OK);data_020a4b54=1;
    auto* scene=dScDSMT_c_classInit();CHECK(scene);CHECK(scene->InitResources()==1);
    v3ds_renderer_init();
    std::int16_t pcm[2*1093];double game_ms=0,audio_ms=0,present_ms=0;
    std::printf("Menu FPS configuration: BGcache=%u GPUblit=%u; render warmup=120 measured=120 stereo=%s\n",
        unsigned(!std::getenv("PLAYER_UNCACHED_BACKGROUNDS")),unsigned(std::getenv("PLAYER_GPU_BLIT")!=nullptr),
        std::getenv("SM64DS_FORCE_3D")?std::getenv("SM64DS_FORCE_3D"):"slider");
    for(unsigned frame=0;frame<960;++frame){
        const double start=v3ds_time_ms();phase_begin(frame);ntr::gx_begin_frame();phase_end(frame,0);func_02019390();phase_end(frame,1);
        auto* touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
        if(frame>=120 && frame<135){touch[0]=128;touch[1]=86;touch[2]=1;}
        CHECK(scene->Behavior()==1);phase_end(frame,2);CHECK(scene->Render()==1);phase_end(frame,3);func_02019404();phase_end(frame,4);
        ++data_020a0db0;func_020132d8();phase_end(frame,5);func_020190b8();phase_end(frame,6);func_02019144();phase_end(frame,7);func_02019100();phase_end(frame,8);
        CHECK(func_0205b070(1));while(func_0205b274(0)){if(frame>=840)++queue_events;}phase_end(frame,9);
        const double game_end=v3ds_time_ms();CHECK(sm64ds_native_sound_render(pcm,1092));
        const double audio_end=v3ds_time_ms();phase_end(frame,10);
        if(frame%120==119 || frame==720){
            std::printf("Menu FPS phase frame=%u state=%d substate=%d\n",frame,
                **reinterpret_cast<short**>(data_ov007_0210342c+4),**reinterpret_cast<short**>(data_ov007_0210342c+8));
        }
        if(frame>=720){
            const auto state=**reinterpret_cast<short**>(data_ov007_0210342c+4);
            const auto substate=**reinterpret_cast<short**>(data_ov007_0210342c+8);
            CHECK(state==0 && substate==0);
            CHECK(player_present(false,false));
        }
        const double end=v3ds_time_ms();phase_end(frame,11);
        if(frame>=840){size_t triangles,entries,bytes;ntr::gx_polygons(triangles);ntr::gx_texture_cache_usage(entries,bytes);triangle_total+=triangles;if(entries>texture_entries_max)texture_entries_max=entries;if(bytes>texture_bytes_max)texture_bytes_max=bytes;}
        if(frame>=840){game_ms+=game_end-start;audio_ms+=audio_end-game_end;present_ms+=end-audio_end;}
        if(frame==839)std::puts("Menu FPS measured window begins at frame840; verified state0 substate0 animated menu");
    }
    std::printf("Menu FPS measured120 ms/frame: game=%.3f audio=%.3f presentation=%.3f total=%.3f fps=%.3f\n",
        game_ms/120,audio_ms/120,present_ms/120,(game_ms+audio_ms+present_ms)/120,
        120000/(game_ms+audio_ms+present_ms));
    for(unsigned i=0;i<12;++i)std::printf("Menu game phase measured120 ms/frame: %s=%.6f\n",phase_names[i],phase_ms[i]/120);
    std::printf("Menu game workloads: sound_queue_events=%llu triangles/frame=%.3f texture_entries_max=%llu texture_bytes_max=%llu\n",queue_events,double(triangle_total)/120,texture_entries_max,texture_bytes_max);
    CHECK(player_present(true,false));
    scene->CleanupResources();delete scene;CHECK(sm64ds_try_unload_overlay(7)==OVERLAY_OK);
    v3ds_exit();std::puts("PASS deterministic retail menu benchmark; compare archived images separately");
    return 0;
}
