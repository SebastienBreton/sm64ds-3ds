// Private queue-drain experiment: preserve current targets and GPU registers.
#include <3ds.h>
#include <citro3d.h>
#include <cstdio>
#include <cstdlib>
#include "command_budget.h"
extern "C" void __real_GX_BindQueue(gxCmdQueue_s*);
extern "C" bool __real_C3D_FrameBegin(u8);
extern "C" void __real_C3D_FrameEnd(u8);
extern "C" void __real_GPUCMD_Add(u32,const u32*,u32);
extern "C" void __real_GPUCMD_AddRawCommands(const u32*,u32);
extern "C" void __real_C3D_DrawArrays(GPU_Primitive_t,int,int);
extern "C" void __real_C3D_DrawElements(GPU_Primitive_t,int,int,const void*);
extern "C" bool __real_C3D_FrameDrawOn(C3D_RenderTarget*);
extern "C" void __real_C3D_TexBind(int,C3D_Tex*);
extern "C" Result __real_GX_ProcessCommandList(u32*,u32,u8);
namespace {
gxCmdQueue_s* queue=nullptr;
u32* base=nullptr;u32 capacity=0;
bool active=false,draining=false;
uint64_t drains=0,submitted_words=0;
bool enabled(){static bool b=[](){const char* s=std::getenv("PLAYER_GPU_COMMAND_DRAIN");return s&&std::atoi(s)!=0;}();return b;}
[[noreturn]] void fail(const char* reason){std::printf("Command budget ERROR %s offset=%lu size=%lu capacity=%lu\n",reason,(unsigned long)gpuCmdBufOffset,(unsigned long)gpuCmdBufSize,(unsigned long)capacity);std::abort();}
void ensure(uint64_t needed){
 if(!active||draining)return;
 if(command_budget::fits(gpuCmdBufSize,gpuCmdBufOffset,needed))return;
 if(!queue||!base||needed+command_budget::reserve_words>capacity)fail("single command exceeds allocation");
 if(gpuCmdBufOffset>gpuCmdBufSize||gpuCmdBufSize-gpuCmdBufOffset<16)fail("finalization reserve lost");
 draining=true;
 C3D_FrameSplit(GX_CMDLIST_FLUSH);
 submitted_words+=uint64_t(gpuCmdBuf-base);
 // All references into the existing buffer must complete before it is reused.
 GSPGPU_FlushDataCache(base,capacity*sizeof(u32));
 gxCmdQueueRun(queue);
 if(!gxCmdQueueWait(queue,-1))fail("queue wait failed");
 gxCmdQueueStop(queue);gxCmdQueueClear(queue);
 GPUCMD_SetBuffer(base,capacity,0);
 ++drains;
 if(drains<=4||drains%128==0)std::printf("Command budget drained count=%llu submittedWords=%llu capacityBytes=%lu\n",(unsigned long long)drains,(unsigned long long)submitted_words,(unsigned long)(capacity*4));
 draining=false;
}
}
// ---- GPU per-pass timing (hardware bring-up) -------------------------------
// Every 120th presented frame is split into one command list per render
// target switch (C3D_FrameSplit, no CPU wait); the GSP P3D completion events
// of those lists give the GPU time of each pass. One summary line about every
// 2 s: "GPU passes ms ...". SM64DS_GPU_PASS_TIMING=0 disables it.
namespace gpu_passes {
constexpr unsigned MaxPasses=48;
struct Pass {u16 w=0,h=0;u8 depth=0,bank=0,tex=0;};
// Memory region of an address: 1 FCRAM (linear), 2 VRAM bank A, 4 VRAM bank B.
u8 bound[3]={0,0,0}; // region of the texture currently bound per unit
u8 region(const void* p){const u32 a=osConvertVirtToPhys(p);return a>=0x18000000&&a<0x18300000?2:a>=0x18300000&&a<0x18600000?4:a?1:0;}
Pass passes[MaxPasses];unsigned pass_count=0;bool drew=false,sampling=false;
// list k closes passes [list_first[k], list_last[k]]
int list_first[MaxPasses],list_last[MaxPasses];unsigned covered=0;
volatile unsigned lists_submitted=0,lists_done=0;volatile uint64_t done_tick[MaxPasses];uint64_t run_tick=0;
unsigned frames=0;uint64_t sampled=0;bool callback_set=false;
// Off by default: on the New 3DS, builds with it (and the enlarged GX queue)
// froze in heavy scenes (diag m3, 2026-10-10). SM64DS_GPU_PASS_TIMING=1 enables it.
bool enabled(){static const bool b=[](){const char* s=std::getenv("SM64DS_GPU_PASS_TIMING");return s&&s[0]=='1';}();return b;}
void on_p3d(void*){
    const unsigned done=lists_done;
    if(done<lists_submitted && done<MaxPasses){done_tick[done]=svcGetSystemTick();lists_done=done+1;}
}
void report(){
    if(!lists_submitted || lists_done<lists_submitted)return;
    char line[600];int n=std::snprintf(line,sizeof line,"GPU passes ms sample=%llu lists=%u:",(unsigned long long)sampled,lists_submitted);
    double total=0;uint64_t start=run_tick;
    for(unsigned k=0;k<lists_submitted && n<int(sizeof line)-48;++k){
        const double ms=(done_tick[k]>start?double(done_tick[k]-start):0.0)*1000.0/SYSCLOCK_ARM11;start=done_tick[k];total+=ms;
        const int a=list_first[k],b=list_last[k];
        static const char* const names[8]={"-","F","A","FA","B","FB","AB","FAB"};
        if(a>=0 && b>=a && a<int(pass_count))n+=std::snprintf(line+n,sizeof line-n," %ux%u%s%s<%s%s=%.2f",passes[a].w,passes[a].h,passes[a].depth?"d":"",
            names[passes[a].bank&7],names[passes[a].tex&7],b>a?"+":"",ms);
        else n+=std::snprintf(line+n,sizeof line-n," ?=%.2f",ms);
    }
    std::printf("%s total=%.2f\n",line,total);
}
void frame_begin(){
    if(!enabled())return;
    if(!callback_set){gspSetEventCallback(GSPGPU_EVENT_P3D,on_p3d,nullptr,false);callback_set=true;}
    sampling=false;
    if(++frames%120)return;
    if(lists_submitted && lists_done<lists_submitted)return; // previous sample still in flight
    report();
    pass_count=0;covered=0;drew=false;lists_done=0;lists_submitted=0;run_tick=0;++sampled;
    sampling=true;
}
void draw_on(C3D_RenderTarget* target){
    if(!sampling)return;
    if(drew && pass_count){drew=false;C3D_FrameSplit(0);}
    if(pass_count<MaxPasses && target){auto& p=passes[pass_count++];p.w=target->frameBuf.width;p.h=target->frameBuf.height;p.depth=target->frameBuf.depthBuf!=nullptr;
        p.bank=region(target->frameBuf.colorBuf);p.tex=bound[0]|bound[1];}
}
void list_submitted(){
    if(!sampling || lists_submitted>=MaxPasses)return;
    const unsigned k=lists_submitted;
    list_first[k]=int(covered);list_last[k]=int(pass_count)-1;covered=pass_count;
    __sync_synchronize();lists_submitted=k+1;
}
}
// Pass timing: memory regions sampled by each pass (summary "WxH<target<textures").
extern "C" void __wrap_C3D_TexBind(int unit,C3D_Tex* tex){
    const u8 r=tex&&tex->data?gpu_passes::region(tex->data):0;
    if(unit>=0 && unit<3)gpu_passes::bound[unit]=r;
    if(gpu_passes::sampling && r && gpu_passes::pass_count)gpu_passes::passes[gpu_passes::pass_count-1].tex|=r;
    __real_C3D_TexBind(unit,tex);
}
extern "C" bool __wrap_C3D_FrameDrawOn(C3D_RenderTarget* target){gpu_passes::draw_on(target);return __real_C3D_FrameDrawOn(target);}
namespace ntr { uint64_t rt_frame(); }
// Diagnostics: SM64DS_DUMP_CL_FRAMES="a,b,..." writes every command list
// submitted during those frames to cl-<frame>-<n>.bin (decode with
// build3ds/validation/hardware-bringup/cmdlist/decode.py).
static void dump_command_list(const u32* buffer,u32 size){
    static const char* frames=std::getenv("SM64DS_DUMP_CL_FRAMES");
    if(!frames)return;
    const unsigned long long frame=ntr::rt_frame();
    for(const char* p=frames;*p;){
        char* end=nullptr;const unsigned long long v=std::strtoull(p,&end,10);if(end==p)break;
        if(v==frame){static unsigned seq=0;char name[48];std::snprintf(name,sizeof name,"cl-%llu-%03u.bin",frame,seq++);
            if(FILE* f=std::fopen(name,"wb")){std::fwrite(buffer,1,size,f);std::fclose(f);}return;}
        p=*end?end+1:end;
    }
}
// Optional observer (hardware diagnostic builds keep the last lists for a hang dump).
extern "C" { void (*sm64ds_command_list_observer)(const u32*,u32)=nullptr; }
extern "C" Result __wrap_GX_ProcessCommandList(u32* buffer,u32 size,u8 flags){gpu_passes::list_submitted();dump_command_list(buffer,size);
    if(sm64ds_command_list_observer)sm64ds_command_list_observer(buffer,size);return __real_GX_ProcessCommandList(buffer,size,flags);}
// citro3d's GX queue holds 32 commands per frame; a full queue is a
// svcBreak panic (a freeze on the console). Layered/blended presentations
// (bg-first blend, stereo layers, sampled pass timing) exceed it, so the
// bound queue is enlarged once, while empty. SM64DS_GX_QUEUE_ENTRIES overrides.
static u16 queue_peak=0;
static void enlarge_queue(gxCmdQueue_s* q){
    // Stock citro3d queue by default (the enlarged queue was part of the
    // hardware freeze bisected in diag m3); SM64DS_GX_QUEUE_ENTRIES=N enlarges.
    static const unsigned want=[](){const char* s=std::getenv("SM64DS_GX_QUEUE_ENTRIES");const unsigned v=s?unsigned(std::atoi(s)):0u;return v>4096?4096u:v;}();
    if(!q || q->maxEntries>=want || q->numEntries)return;
    auto* entries=static_cast<gxCmdEntry_s*>(std::malloc(sizeof(gxCmdEntry_s)*want));
    if(!entries)return;
    q->entries=entries;q->maxEntries=u16(want);
    std::printf("GX command queue enlarged to %u entries\n",want);
}
extern "C" void __wrap_GX_BindQueue(gxCmdQueue_s* q){enlarge_queue(q);queue=q;__real_GX_BindQueue(q);}
extern "C" { extern volatile int sm64ds_shadow_probe_pending; void sm64ds_shadow_probe_readback(); }
extern "C" bool __wrap_C3D_FrameBegin(u8 flags){
 if(sm64ds_shadow_probe_pending)sm64ds_shadow_probe_readback(); // outside any frame
 const bool ok=__real_C3D_FrameBegin(flags);
 if(ok)gpu_passes::frame_begin();
 if(ok){
 base=gpuCmdBuf;capacity=gpuCmdBufSize;active=enabled();
 if(active){
  const char* limit=std::getenv("PLAYER_GPU_COMMAND_DRAIN_LIMIT_KIB");
  const unsigned kib=limit?unsigned(std::atoi(limit)):0;
  if(kib){if(kib<16||uint64_t(kib)*256>capacity)fail("invalid diagnostic limit");capacity=kib*256;GPUCMD_SetBuffer(base,capacity,0);}
 }
}
 return ok;
}
extern "C" void __wrap_C3D_FrameEnd(u8 flags){
 active=false;
 if(gpu_passes::sampling)gpu_passes::run_tick=svcGetSystemTick(); // the queue starts running in FrameEnd
 __real_C3D_FrameEnd(flags);
 if(queue && queue->numEntries>queue_peak){queue_peak=queue->numEntries;if(queue_peak>24)std::printf("GX command queue peak entries=%u capacity=%u\n",unsigned(queue_peak),unsigned(queue->maxEntries));}
 gpu_passes::sampling=false;
}
extern "C" void __wrap_GPUCMD_Add(u32 header,const u32* values,u32 count){if(active&&!draining)ensure(command_budget::encoded_words(count));__real_GPUCMD_Add(header,values,count);}
extern "C" void __wrap_GPUCMD_AddRawCommands(const u32* values,u32 count){if(active&&!draining&&values&&count)ensure(count);__real_GPUCMD_AddRawCommands(values,count);}
// Real PICA (New 3DS, 2026-10-09 bring-up): a draw with zero vertices never
// completes, so the whole command list hangs (P3D never signalled; the game
// froze on entering the castle). Azahar ignores it. Empty batches are common
// (culled runs, empty sprite lists), so every draw goes through this guard.
extern "C" void __wrap_C3D_DrawArrays(GPU_Primitive_t primitive,int first,int size){if(size>0){gpu_passes::drew=true;__real_C3D_DrawArrays(primitive,first,size);}}
extern "C" void __wrap_C3D_DrawElements(GPU_Primitive_t primitive,int count,int type,const void* indices){if(count>0){gpu_passes::drew=true;__real_C3D_DrawElements(primitive,count,type,indices);}}
