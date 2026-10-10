// Opt-in physical VRAM integration probe. Original boot, scenes and HID remain
// game_main; the focused linker wraps presentation only to observe capture.
#include "ntr/display_capture.h"
#include "ntr/vram_bus.h"
#include "ntr/rt.h"
#include <initializer_list>
namespace ntr {uint64_t jump_probe_rt_run(void(*)(),FrameHook,uint64_t);}
#undef main
#define main jump_original_main
#define rt_run jump_probe_rt_run
#include "../platform/3ds/game_main.cpp"
#undef main
#undef rt_run

static unsigned jump_capture_count, jump_capture_banks, jump_bitmap_count[2];
static bool jump_capture_failed,jump_late_observed;
static void jump_dump_state(const char* prefix){
 auto* bus=ntr::io_vram_banks();
 auto dump=[&](const char* part,const void* bytes,unsigned count){
  char name[96];std::snprintf(name,sizeof name,"%s-%s.bin",prefix,part);
  if(auto* file=std::fopen(name,"wb")){std::fwrite(bytes,1,count,file);std::fclose(file);}
 };
 for(unsigned b=0;b<9;++b){char part[24];std::snprintf(part,sizeof part,"bank%u",b);dump(part,bus->physical(b),bus->bank_size(b));}
 dump("io",ntr::host_ptr(0x04000000),0x2000);
 dump("palette",ntr::host_ptr(0x05000000),0x800);
 dump("oam",ntr::host_ptr(0x07000000),0x800);
 std::printf("Jump snapshot %s update=%u scene=%u\n",prefix,updates,current_scene);
}
extern bool __real_player_present(bool,bool) asm("__real__Z14player_presentbb");
extern bool __wrap_player_present(bool capture,bool perspective) asm("__wrap__Z14player_presentbb");
bool __wrap_player_present(bool capture,bool perspective){
 auto* bus=ntr::io_vram_banks();auto* controller=ntr::io_display_capture();
 if(!bus || !controller){std::puts("FATAL: Jump probe physical bus/controller absent");return false;}
 const unsigned display=ntr::io_read(0x04000000,4),control=controller->control();
 const auto sources=controller->sources(0,false,bus->lcdc_mask());
 const unsigned bank=(control>>16)&3;
 const auto before=bus->bank_generation(bank);
 const unsigned retained=bank==2?3:2;
 const auto retained_before=bus->bank_generation(retained);
 const unsigned display_b=ntr::io_read(0x04001000,4),bg2=ntr::io_read(0x0400100c,2);
 const unsigned power=ntr::io_read(0x04000304,2);
 const bool unfaded=ntr::io_read(0x0400006c,2)==0 && ntr::io_read(0x0400106c,2)==0 &&
   ntr::io_read(0x04000050,2)==0 && ntr::io_read(0x04001050,2)==0;
 const bool jump=current_scene==372;
 static bool bootstrap_observed;
 const bool bootstrap=current_scene==1 && updates>=850 && !bootstrap_observed;
 bootstrap_observed|=bootstrap;
 // Capture diagnostics once the real original scene has begun rendering.
 static unsigned room_frames;const bool room_snapshot=current_scene==5 && ++room_frames==180;
 const bool snapshot=jump && !jump_late_observed && jump_capture_count>=384 && jump_bitmap_count[0]>=64 && jump_bitmap_count[1]>=64 && bank==3;
 if(room_snapshot)jump_dump_state("jump-recroom-pre");
 if(snapshot)jump_dump_state("jump-gameplay-pre");
 if(!__real_player_present(capture||snapshot||bootstrap||room_snapshot,perspective))return false;
 if(room_snapshot){
  unsigned map_nonzero=0;for(unsigned offset=0;offset<0x1000;offset+=2)
   map_nonzero+=(bus->physical(2)[offset]|(unsigned(bus->physical(2)[offset+1])<<8))!=0;
  const unsigned map_first=bus->physical(2)[0]|(unsigned(bus->physical(2)[1])<<8);
  std::printf("RecRoom physical BG0 map: nonzeroHalfwords=%u first=%04x bankcnt=%02x\n",map_nonzero,map_first,bus->control(2));
  if(map_first!=0x1f || map_nonzero<512){std::puts("FATAL: RecRoom BG0 physical map remains absent");return false;}
  jump_dump_state("jump-recroom-post");
  for(const auto* name:{"geometry","geometry-right","upper","bottom"}){
   char from[80],to[80];std::snprintf(from,sizeof from,"player-native-%s.bmp",name);
   std::snprintf(to,sizeof to,"jump-recroom-%s.bmp",name);std::rename(from,to);
  }
 }
 if(snapshot){jump_dump_state("jump-gameplay-post");jump_late_observed=true;}
 if(bootstrap){
  for(unsigned b=0;b<9;++b){
   unsigned nonzero=0;for(unsigned i=0;i<bus->bank_size(b);++i)nonzero+=bus->physical(b)[i]!=0;
   std::printf("Jump bootstrap physical bank%u cnt=%02x nonzero=%u\n",b,unsigned(bus->control(b)),nonzero);
   char name[64];std::snprintf(name,sizeof name,"jump-bootstrap-bank%u.bin",b);
   if(auto* file=std::fopen(name,"wb")){std::fwrite(bus->physical(b),1,bus->bank_size(b),file);std::fclose(file);}
  }
  for(const auto* name:{"geometry","geometry-right","upper","bottom"}){
   char from[80],to[80];std::snprintf(from,sizeof from,"player-native-%s.bmp",name);
   std::snprintf(to,sizeof to,"jump-bootstrap-%s.bmp",name);std::rename(from,to);
  }
 }
 if(jump && sources.active){
  ++jump_capture_count;jump_capture_banks|=1u<<bank;
  const bool wrote=bus->bank_generation(bank)!=before;
  const bool cleared=!(controller->control()&0x80000000u) && !controller->latched();
  unsigned alpha=0;const unsigned offset=((control>>18)&3)*0x8000;
  unsigned varied=0;const auto* pixels=bus->physical(bank);
  const unsigned first=pixels[offset]|(unsigned(pixels[offset+1])<<8);
  for(unsigned i=0;i<49152;++i){const unsigned at=((offset+i*2)&0x1ffff);alpha+=(pixels[at+1]>>7);
   varied+=(pixels[at]|(unsigned(pixels[at+1])<<8))!=first;
  }
  // Retail Jump uses composite sourceA, size256x192 and alternating C/D.
  const bool retail=((control&0x6f000000u)==0x00000000u) &&
    ((control>>20)&3)==3 && (bank==2 || bank==3);
  if(!wrote || !cleared || !retail || alpha!=49152){
   std::printf("FATAL: Jump capture control=%08x wrote=%u cleared=%u retail=%u alpha=%u\n",
    control,unsigned(wrote),unsigned(cleared),unsigned(retail),alpha);
   jump_capture_failed=true;return false;
  }
  // The two retained images use different retail consumers: C supplies BG2,
  // D supplies twelve64x64 bitmapOBJ. Reference consumed-obj.oam fixes attrs.
  static const unsigned short expected_obj[12][4]={
   {0x0c00,0xc000,0xf000,0},{0x0c00,0xc040,0xf008,0},{0x0c00,0xc080,0xf010,0},{0x0c00,0xc0c0,0xf018,0},
   {0x0c40,0xc000,0xf100,0},{0x0c40,0xc040,0xf108,0},{0x0c40,0xc080,0xf110,0},{0x0c40,0xc0c0,0xf118,0},
   {0x0c80,0xc000,0xf200,0},{0x0c80,0xc040,0xf208,0},{0x0c80,0xc080,0xf210,0},{0x0c80,0xc0c0,0xf218,0}};
  const bool consumer=bank==3?display_b==0x10425 && bg2==0x4284:
    display_b==0x11025 && !std::memcmp(ntr::host_ptr(0x07000400),expected_obj,sizeof expected_obj);
  const bool bitmap=unfaded && varied>512 && display==0x11108 && consumer &&
    bus->control(bank)==0x80 && bus->control(retained)==0x84 &&
    retained_before==bus->bank_generation(retained) && ((power>>15)&1)==unsigned(bank==3);
  if(bitmap)++jump_bitmap_count[bank==3?0:1];
  if(jump_capture_count<=4 || jump_capture_count%60==0)
   std::printf("Jump physical capture=%u bank=%u control=%08x display=%08x/%08x bg2=%04x pow=%04x alpha=%u bitmap=%u\n",
    jump_capture_count,bank,control,display,display_b,bg2,power,alpha,unsigned(bitmap));
  if(snapshot){
   for(unsigned b=0;b<9;++b){char name[64];std::snprintf(name,sizeof name,"jump-physical-bank%u.bin",b);
    if(auto* file=std::fopen(name,"wb")){std::fwrite(bus->physical(b),1,bus->bank_size(b),file);std::fclose(file);}}
  }
 }
 return true;
}
namespace ntr {
static FrameHook jump_game_frame_hook;
static bool jump_probe_frame_hook(uint64_t frame){
 if(jump_bitmap_count[0]>=64 && jump_bitmap_count[1]>=64 && jump_late_observed){std::puts("Jump probe completed 64 real SubBG and SubOBJ bitmap captured frames");return false;}
 return jump_game_frame_hook(frame);
}
uint64_t jump_probe_rt_run(void(*entry)(),FrameHook hook,uint64_t limit){
 jump_game_frame_hook=hook;return rt_run(entry,jump_probe_frame_hook,limit);
}
}
int smoke_main(){
 setenv("PLAYER_PHYSICAL_VRAM","1",1);
 setenv("SM64DS_GAME_REQUIRE_INPUT","1",1);
 setenv("SM64DS_GAME_REQUIRE_TITLE_MENU","1",1);
 setenv("SM64DS_GAME_REQUIRE_FINAL_SCENE","372",1);
 const int result=jump_original_main();
 std::printf("Jump physical summary: captures=%u banks=%x SubBG_bitmap=%u SubOBJ_bitmap=%u failed=%u\n",
  jump_capture_count,jump_capture_banks,jump_bitmap_count[0],jump_bitmap_count[1],unsigned(jump_capture_failed));
 return result || jump_capture_failed || jump_capture_count<64 || jump_capture_banks!=0xc ||
   jump_bitmap_count[0]<64 || jump_bitmap_count[1]<64 || !jump_late_observed;
}
