// Process entry for the interactive build; no smoke console or START-to-exit.
#undef main
#include <3ds.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "cpu_mode.h"
#include "renderer_defaults.h"
#include "enhance_settings.h"

int smoke_main(); // game_main.cpp, compiled with the shared target's flags
extern "C" {
u32 __stacksize__=1024*1024;
// libctru's default split gives the linear (GPU/DSP) heap 32 MiB, of which
// the game uses ~17 MiB at most (full gameplay route), while the newlib heap
// (game RAM regions, port caches, mixer, GPU snapshots) ran out at ~48 MiB
// on scene changes (std::bad_alloc -> abort, full regression route). 24 MiB
// of linear keeps a 6+ MiB margin and gives the newlib heap 8 MiB more.
u32 __ctru_linear_heap_size=24*1024*1024;
int sm64ds_backlight_init();
}
extern char** environ;
static char* empty_environment[]={nullptr};

// Retail's fatal stop loops forever in the key-controlled diagnostic screen.
// Report its original native caller before stopping, so an unsupported path
// cannot disappear into a VCOUNT loop with no useful test verdict.
extern "C" void ShowCrashScreen();
extern "C" unsigned char data_0209d574[];
static void* active_watchdog_callback=nullptr;
extern "C" void __real_func_0201a8b4(void*);
extern "C" __attribute__((noinline)) void __wrap_func_0201a8b4(void* guard){
 active_watchdog_callback=guard;
 if(std::getenv("SM64DS_TRACE_WATCHDOG"))
  std::printf("Watchdog alarm fired: guard=%p\n",guard);
 __real_func_0201a8b4(guard);
 active_watchdog_callback=nullptr;
}
extern "C" unsigned char __real_func_0201a96c(void*,unsigned char);
extern "C" unsigned char __wrap_func_0201a96c(void* guard,unsigned char requested){
 const bool trace=std::getenv("SM64DS_TRACE_WATCHDOG")!=nullptr;
 const unsigned char previous=static_cast<unsigned char*>(guard)[0x40];
 if(trace)std::printf("Watchdog toggle: caller=%p guard=%p flag=%u requested=%u\n",
  __builtin_return_address(0),guard,unsigned(previous),unsigned(requested));
 const unsigned char result=__real_func_0201a96c(guard,requested);
 if(trace)std::printf("Watchdog toggle complete: flag=%u returned=%u\n",
  unsigned(static_cast<unsigned char*>(guard)[0x40]),unsigned(result));
 return result;
}
extern "C" __attribute__((noinline,noreturn)) void __wrap_func_02013f28(){
 std::fprintf(stderr,"FATAL: original DS stop reached; caller=%p\n",__builtin_return_address(0));
 std::fprintf(stderr,"Fatal active watchdog callback: %p\n",active_watchdog_callback);
 // Preserve the guard state at the stop, before diagnostic drawing changes it.
 // A tail call can preserve an outer caller; the active callback identifies
 // whether the stop occurred inside the original watchdog handler.
 uint64_t limit=0,base=0;
 std::memcpy(&limit,data_0209d574+0x30,sizeof limit);
 std::memcpy(&base,data_0209d574+0x38,sizeof base);
 std::fprintf(stderr,"Fatal watchdog state: flag=%u limit=%llu base=%llu\n",
  unsigned(data_0209d574[0x40]),(unsigned long long)limit,(unsigned long long)base);
 ShowCrashScreen();std::fflush(stderr);std::abort();
}

int main(){
 environ=empty_environment;
 setenv("SM64DS_ASSET_ROOT","sdmc:/sm64ds/repo",1);
#ifdef SM64DS_USER_BUILD
 // Keep manual play independent of the automated probes' env.txt and saves.
 setenv("SM64DS_SAVE_PATH","sdmc:/sm64ds/user-test.sav",1);
 setenv("SM64DS_GAME_TRACE_INPUT","1",1);
#else
 if(FILE* settings=std::fopen("sdmc:/sm64ds/env.txt","r")){
  char line[256];
  while(std::fgets(line,sizeof line,settings)){
   line[std::strcspn(line,"\r\n")]=0;
   if(char* equal=std::strchr(line,'=')){*equal=0;setenv(line,equal+1,1);}
  }
  std::fclose(settings);
 }
#endif
 chdir("sdmc:/sm64ds");
#ifdef SM64DS_USER_BUILD
 std::freopen("sdmc:/sm64ds/user-test.log","w",stdout);stderr=stdout;
#else
 std::freopen("sdmc:/sm64ds/log.txt","w",stdout);stderr=stdout;
#endif
 setvbuf(stdout,nullptr,_IONBF,0);
 // visual-enhancements: sdmc:/sm64ds/settings.ini (absent = current defaults);
 // boot-only options become env switches unless env.txt already set them.
 enhance::boot_apply();
 if(!sm64ds_configure_renderer_defaults()){
  std::fprintf(stderr,"FATAL: renderer settings initialization failed\n");return 1;
 }
 gfxInitDefault();sm64ds_configure_cpu_mode();
 int result=sm64ds_backlight_init();
 if(result)std::fprintf(stderr,"FATAL: backlight initialization: %08x\n",result);
 else result=smoke_main();
 // Bounded runs use the same diagnostic verdict protocol as run.py.
 std::printf("Native game exit code: %d\n%s\n",result,result?"FAIL":"PASS");
 std::fflush(stdout);gfxExit();return result;
}
