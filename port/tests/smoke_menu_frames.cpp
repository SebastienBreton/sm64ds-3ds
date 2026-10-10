// Diagnostic execution of the original menu frames. Captures expose renderer
// gaps; successful execution alone does not establish DS pixel parity.
#include "ntr/mmio.h"
#include "Heap.h"
#include "dScDSMT_c.h"
#include "menu/menu.h"
#include "unmatched/native_overlay.h"
#include "ntr/ppu.h"
#include "ntr/gx.h"
#include "audio/sound_native.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
namespace Sound { void LoadInitialGroup(int); }
extern "C" {
void func_02059bc0();
void __sinit_02075154();
void __sinit_0207501c();
void __sinit_02073a24();
void __sinit_02074da8();
void func_0201fe08();
void func_020133bc();
void __sinit_02074e84();
void func_0201a4e4();
int func_02043098(int,int,int,int);
void func_02019390();
void func_02044120();
void func_02019404();
void func_020132d8();
void func_02019144();
void func_02019100();
void func_020190b8();
void func_ov007_020bc02c(void*,void*);
void func_020134d8();
void func_02051f1c();
int func_0205b070(int);
void** func_0205b274(int);
unsigned short* func_0203dabc(int);
int func_0203da9c();
dScDSMT_c* dScDSMT_c_classInit();
extern unsigned short data_020a4b54;
extern unsigned short data_02092664;
extern signed char data_02092110;
extern unsigned char data_0209f268,data_0209f2f4;
extern unsigned short data_02092144[];
extern void* data_020a0ea0;
extern void* data_020a0eac;
extern unsigned char* data_ov007_0210342c;
}
int main() {
 setvbuf(stdout,nullptr,_IONBF,0);
 // A dedicated cart path prevents testing from modifying the user's gameplay save.
 if(!std::getenv("SM64DS_SAVE_PATH"))setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/menu-validation.sav",1);
 if(!ntr::io_init())return 1;
 auto* rootHeap=Heap::SetupRootHeap();if(!rootHeap)return 1;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 data_020a0eac=data_020a0ea0;
 __sinit_02073a24();__sinit_02074da8();
 std::printf("runtime addresses: bc02c=%p 134d8=%p 51f1c=%p\n",(void*)func_ov007_020bc02c,(void*)func_020134d8,(void*)func_02051f1c);
 std::puts("menu boot: original sound initialization");func_020133bc();
 Sound::LoadInitialGroup(0);
 std::puts("menu boot: original message bank");func_0201fe08();
 if(!(sm64ds_try_load_overlay(7)==OVERLAY_OK))return 2;
 const bool scheduled=std::getenv("SM64DS_MENU_SCHEDULER")!=nullptr;
 data_020a4b54=1;
 if(scheduled){__sinit_02074e84();func_0201a4e4();}
 auto* scene=scheduled?reinterpret_cast<dScDSMT_c*>(func_02043098(1,0,0,2)):dScDSMT_c_classInit();if(!scene)return 3;
 if(scheduled)std::printf("menu factory scheduled scene=%p; InitResources is driven by lifecycle\n",scene);
 else {
   const int result=scene->InitResources();
   std::printf("menu InitResources returned=%d scene=%p\n",result,scene);
   if(result!=1)return 4;
 }
 auto* callback=reinterpret_cast<dScDSMT_c::graphCallback_c*>(scene->unk_050);
 std::int16_t pcm[2*1093];
 const bool journey=std::getenv("SM64DS_MENU_JOURNEY")!=nullptr;
 const bool gxDiagnostic=std::getenv("SM64DS_MENU_GX_TRACE")!=nullptr;
 const char* transaction=std::getenv("SM64DS_MENU_TRANSACTION");
 const bool startFile=std::getenv("SM64DS_MENU_START_FILE")!=nullptr;
 bool requestedStage=false;
 const bool occupied=transaction!=nullptr;
 const unsigned totalFrames=occupied?1800:((journey||startFile)?1100:240);
 for(unsigned frame=0;frame<totalFrames;++frame) {
   ntr::gx_begin_frame();
   if(frame<3 || frame%60==59)std::printf("menu frame-start=%u heap-free=%u\n",frame,rootHeap->VMemoryLeft());
   // No input for this initial draw probe. Real Behavior still reads its original
   // session record, and the original graph callback commits sub-screen state.
   auto* touch=func_0203dabc(func_0203da9c());std::memset(touch,0,8);
   if(startFile) {
     if(frame>=700 && frame<705){touch[0]=122;touch[1]=165;touch[2]=1;}
     if(frame>=900 && frame<905){touch[0]=44;touch[1]=70;touch[2]=1;}
   } else if(occupied) {
     if(frame>=700 && frame<705){touch[0]=122;touch[1]=165;touch[2]=1;}
     if(frame>=900 && frame<905){touch[0]=90;touch[1]=161;touch[2]=1;}
     const bool copy=std::strncmp(transaction,"copy",4)==0;
     if(frame>=1100 && frame<1105){touch[0]=copy?131:209;touch[1]=110;touch[2]=1;}
     if(frame>=1250 && frame<1255){touch[0]=44;touch[1]=90;touch[2]=1;}
     if(copy && frame>=1400 && frame<1405){touch[0]=128;touch[1]=90;touch[2]=1;}
     if(frame>=1550 && frame<1555){touch[0]=std::strstr(transaction,"yes")?90:165;touch[1]=127;touch[2]=1;}
   } else if(journey) {
     if(frame>=120 && frame<135){touch[0]=128;touch[1]=86;touch[2]=1;}
     if(frame>=700 && frame<705){touch[0]=122;touch[1]=165;touch[2]=1;}
     if(frame>=900 && frame<905){touch[0]=90;touch[1]=161;touch[2]=1;}
   }
   if(scheduled){
     func_02019390();func_02044120();func_02019404();func_020132d8();
     // Original VBlank commit phases, once per simulated display frame.
     func_020190b8();func_02019144();func_02019100();
   } else {
   if(frame<3)std::printf("menu frame=%u Behavior begin\n",frame);
   if(scene->Behavior()!=1)return 6;
   if(frame<3)std::printf("menu frame=%u Render begin GXSTAT=%08x\n",frame,unsigned(NTR_MMIO(unsigned,0x04000600)));
   if(scene->Render()!=1)return 6;
   if(frame<3)std::printf("menu frame=%u callback begin\n",frame);
   callback->GraphCallback2();
   }
   if(startFile && data_02092664==3){
     std::printf("menu StartFile requested Stage at frame=%u\n",frame);
     if(data_02092110!=1 || data_0209f268!=0 || data_0209f2f4!=4)return 11;
     for(unsigned i=0;i<4;++i)if(data_02092144[i]!=0x880)return 12;
     std::puts("menu StartFile level1 entrance0 lives4 health4x0880 PASS");
     requestedStage=true;break;
   }
   if(!func_0205b070(1))return 7;
   while(func_0205b274(0)){}
   if(!sm64ds_native_sound_render(pcm,1092))return 8;
   if(frame<3 || (!journey && !occupied && frame%60==59) || ((journey || occupied) && (frame==119 || frame==699 || frame==899 || frame==1099 || frame==1249 || frame==1399 || frame==1549 || frame==1799))) {
     size_t count=0;const auto* tris=ntr::gx_polygons(count);
     float xmin=1e30f,xmax=-1e30f,ymin=1e30f,ymax=-1e30f,wmin=1e30f,wmax=-1e30f;
     unsigned positiveW=0,front=0,onscreen=0;
     for(size_t i=0;i<count;++i){
       const auto& t=tris[i];bool good=true;
       for(const auto& v:t.v){xmin=std::min(xmin,v.x);xmax=std::max(xmax,v.x);ymin=std::min(ymin,v.y);ymax=std::max(ymax,v.y);wmin=std::min(wmin,v.w);wmax=std::max(wmax,v.w);good&=v.w>1e-4f;}
       positiveW+=good;
       const auto&a=t.v[0];const auto&b=t.v[1];const auto&c=t.v[2];
       front+=((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))<0;
       onscreen+=std::max({a.x,b.x,c.x})>=0 && std::min({a.x,b.x,c.x})<256 && std::max({a.y,b.y,c.y})>=0 && std::min({a.y,b.y,c.y})<192;
     }
     std::printf("menu GX frame=%u bounds=%g,%g..%g,%g w=%g..%g positive=%u front=%u intersect=%u\n",frame,xmin,ymin,xmax,ymax,wmin,wmax,positiveW,front,onscreen);
     if(gxDiagnostic) {
     static ntr::Framebuffer gx;std::memset(&gx,0,sizeof(gx));ntr::gx_render(gx);
     char gxname[64];std::snprintf(gxname,sizeof(gxname),"menu-gx-%03u.bmp",frame);ntr::ppu_write_bmp(gxname,gx);
     }
     for(unsigned slot:{0x30u,0x34u}) {
       const auto* camera=*reinterpret_cast<unsigned char**>(data_ov007_0210342c+slot);
       if(camera)std::printf("menu camera slot=%x aspect=%08x:%08x proj=%08x/%08x/%08x/%08x scale=%08x\n",slot,*(unsigned*)(camera+0xc0),*(unsigned*)(camera+0xbc),*(unsigned*)(camera+0x74),*(unsigned*)(camera+0x88),*(unsigned*)(camera+0x9c),*(unsigned*)(camera+0xac),*(unsigned*)(camera+0xb4));
     }
     auto* state=*reinterpret_cast<short**>(data_ov007_0210342c+4);
     auto* substate=*reinterpret_cast<short**>(data_ov007_0210342c+8);
     std::printf("menu state=%d substate=%d\n",*state,*substate);
     std::printf("menu frame=%u triangles=%u display=%08x/%08x powcnt=%04x\n",frame,(unsigned)count,
       unsigned(NTR_MMIO(unsigned,0x04000000)),unsigned(NTR_MMIO(unsigned,0x04001000)),unsigned(NTR_MMIO(unsigned short,0x04000304)));
     for(unsigned engine=0;engine<2;++engine) {
       const unsigned base=0x04000000+engine*0x1000;
       std::printf("menu engine=%u bg=%04x/%04x/%04x/%04x blend=%04x/%04x/%04x master=%04x\n",engine,
         unsigned(NTR_MMIO(unsigned short,base+8)),unsigned(NTR_MMIO(unsigned short,base+10)),
         unsigned(NTR_MMIO(unsigned short,base+12)),unsigned(NTR_MMIO(unsigned short,base+14)),
         unsigned(NTR_MMIO(unsigned short,base+0x50)),unsigned(NTR_MMIO(unsigned short,base+0x52)),
         unsigned(NTR_MMIO(unsigned short,base+0x54)),unsigned(NTR_MMIO(unsigned short,base+0x6c)));

       static ntr::Framebuffer fb;ntr::ppu_scanout_scene(ntr::Engine(engine),fb);
       char name[64];std::snprintf(name,sizeof(name),"menu-frame-%03u-%u.bmp",frame,engine);
       if(!ntr::ppu_write_bmp(name,fb))return 9;
     }
   }
 }
 if(startFile && !requestedStage){std::puts("menu StartFile FAILED: no Stage request");return 10;}
 std::puts("menu boot: cleanup");scene->CleanupResources();delete scene;
 if(!(sm64ds_try_unload_overlay(7)==OVERLAY_OK))return 5;
 std::puts("smoke_menu_frames: PASS execution only; inspect captures for rendering parity");return 0;
}
