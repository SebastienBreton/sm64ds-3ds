#include "native_overlay.h"
#include "native_actor_lifecycle.h"
#include "menu/menu.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
bool sm64ds_load_star_select_overlay();
bool sm64ds_unload_star_select_overlay();
bool sm64ds_load_castle_overlay();
bool sm64ds_load_overlay80();
bool sm64ds_unload_overlay80();
bool sm64ds_load_overlay63();
bool sm64ds_unload_overlay63();
bool sm64ds_load_overlay10();
bool sm64ds_unload_overlay10();
bool sm64ds_load_overlay85();
bool sm64ds_load_overlay89();
bool sm64ds_load_overlay94();
bool sm64ds_load_overlay100();
bool sm64ds_load_overlay102();
bool sm64ds_unload_overlay102();
bool sm64ds_unload_overlay100();
bool sm64ds_unload_overlay94();
bool sm64ds_unload_overlay89();
bool sm64ds_load_overlay62();
bool sm64ds_unload_overlay62();
bool sm64ds_load_overlay98();
bool sm64ds_unload_overlay98();
bool sm64ds_unload_overlay85();
bool sm64ds_unload_castle_overlay();
bool sm64ds_load_minigame_menu_overlay();
bool sm64ds_unload_minigame_menu_overlay();
bool sm64ds_load_minigame_base_overlay();
bool sm64ds_unload_minigame_base_overlay();
bool sm64ds_load_wanted_overlay();
bool sm64ds_unload_wanted_overlay();
struct Region { int id; unsigned start,size; };
Region data_0209d3c4[12]={
    {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},
    {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}};
}
namespace {
const NativeOverlayInfo* info(unsigned id) {
    for(unsigned i=0;i<sm64ds_native_overlay_count;++i)
        if(sm64ds_native_overlays[i].id==id)return &sm64ds_native_overlays[i];
    return nullptr;
}
[[noreturn]] void fail(const char* operation,int id,NativeOverlayResult result) {
    std::fprintf(stderr,"FATAL: native overlay %s id=%d result=%u (0=ok,1=unknown,2=unsupported,3=overlap,4=no-slot,5=init-failed,6=not-loaded)\n",operation,id,unsigned(result));
    std::fflush(stderr);std::abort();
}
}
extern "C" NativeOverlayResult sm64ds_try_load_overlay(unsigned id) {
    const auto* ov=info(id);
    if(!ov)return OVERLAY_UNKNOWN;
    if(!ov->kind)return OVERLAY_UNSUPPORTED;
    Region* slot=nullptr;
    for(auto& r:data_0209d3c4) {
        if(r.id==-1) {if(!slot)slot=&r;continue;}
        if(uint64_t(r.start)+r.size>ov->original_start &&
           uint64_t(ov->original_start)+ov->original_size>r.start)return OVERLAY_OVERLAP;
    }
    if(!slot)return OVERLAY_NO_SLOT;
    bool ok=false;
    if(ov->kind==1)ok=ov->numeric_bytes && sm64ds_reset_level_data(id)==ov->numeric_bytes;
    if(ov->kind==2)ok=sm64ds_load_castle_overlay();
    if(ov->kind==3)ok=sm64ds_load_overlay85();
    if(ov->kind==4)ok=sm64ds_load_overlay98();
    if(ov->kind==5)ok=sm64ds_load_overlay62();
    if(ov->kind==6)ok=true; // ROM-verified empty placeholder, still occupies a region.
    if(ov->kind==7)ok=sm64ds_load_overlay89();
    if(ov->kind==8)ok=sm64ds_load_overlay94();
    if(ov->kind==9)ok=sm64ds_load_overlay100();
    if(ov->kind==10)ok=sm64ds_load_overlay102();
    if(ov->kind==11)ok=sm64ds_load_menu_overlay();
    if(ov->kind==12)ok=sm64ds_load_minigame_menu_overlay();
    if(ov->kind==13)ok=sm64ds_load_minigame_base_overlay();
    if(ov->kind==14)ok=sm64ds_load_wanted_overlay();
    if(ov->kind==15)ok=sm64ds_load_overlay10();
    if(ov->kind==16)ok=sm64ds_load_overlay63();
    if(ov->kind==17)ok=sm64ds_load_overlay80();
    if(ov->kind==18)ok=sm64ds_load_star_select_overlay();
    if(sm64ds_has_actor_lifecycle(id,ov->kind))
        ok=sm64ds_load_actor_lifecycle(id,ov->kind);
    if(!ok)return OVERLAY_INIT_FAILED;
    *slot={static_cast<int>(id),ov->original_start,ov->original_size};
    return OVERLAY_OK;
}
extern "C" NativeOverlayResult sm64ds_try_unload_overlay(unsigned id) {
    const auto* ov=info(id);
    if(!ov)return OVERLAY_UNKNOWN;
    for(auto& r:data_0209d3c4)if(r.id==static_cast<int>(id)) {
        if(ov->kind==2 && !sm64ds_unload_castle_overlay())return OVERLAY_INIT_FAILED;
        if(ov->kind==3 && !sm64ds_unload_overlay85())return OVERLAY_INIT_FAILED;
        if(ov->kind==4 && !sm64ds_unload_overlay98())return OVERLAY_INIT_FAILED;
        if(ov->kind==5 && !sm64ds_unload_overlay62())return OVERLAY_INIT_FAILED;
        if(ov->kind==7 && !sm64ds_unload_overlay89())return OVERLAY_INIT_FAILED;
        if(ov->kind==8 && !sm64ds_unload_overlay94())return OVERLAY_INIT_FAILED;
        if(ov->kind==9 && !sm64ds_unload_overlay100())return OVERLAY_INIT_FAILED;
        if(ov->kind==10 && !sm64ds_unload_overlay102())return OVERLAY_INIT_FAILED;
        if(ov->kind==11 && !sm64ds_unload_menu_overlay())return OVERLAY_INIT_FAILED;
        if(ov->kind==12 && !sm64ds_unload_minigame_menu_overlay())return OVERLAY_INIT_FAILED;
        if(ov->kind==13 && !sm64ds_unload_minigame_base_overlay())return OVERLAY_INIT_FAILED;
        if(ov->kind==14 && !sm64ds_unload_wanted_overlay())return OVERLAY_INIT_FAILED;
        if(ov->kind==15 && !sm64ds_unload_overlay10())return OVERLAY_INIT_FAILED;
        if(ov->kind==16 && !sm64ds_unload_overlay63())return OVERLAY_INIT_FAILED;
        if(ov->kind==17 && !sm64ds_unload_overlay80())return OVERLAY_INIT_FAILED;
        if(ov->kind==18 && !sm64ds_unload_star_select_overlay())return OVERLAY_INIT_FAILED;
        if(sm64ds_has_actor_lifecycle(id,ov->kind) &&
           !sm64ds_unload_actor_lifecycle(id,ov->kind))return OVERLAY_INIT_FAILED;
        r={-1,0,0};return OVERLAY_OK;
    }
    return OVERLAY_NOT_LOADED;
}
extern "C" void LoadOverlay(int id) {
    if(std::getenv("NATIVE_OVERLAY_TRACE"))std::fprintf(stderr,"native overlay load id=%d caller=%p\n",id,__builtin_return_address(0));
    const auto result=sm64ds_try_load_overlay(id);
    if(result!=OVERLAY_OK)fail("load",id,result);
}
extern "C" void UnloadOverlay(int id) {
    if(std::getenv("NATIVE_OVERLAY_TRACE"))std::fprintf(stderr,"native overlay unload id=%d caller=%p\n",id,__builtin_return_address(0));
    const auto result=sm64ds_try_unload_overlay(id);
    if(result!=OVERLAY_OK)fail("unload",id,result);
}

extern "C" int sm64ds_current_level_overlay() {
    int level=-1;
    for(const auto& region:data_0209d3c4)if(region.id>=8 && region.id<60) {
        if(level!=-1)return -1;
        level=region.id;
    }
    return level;
}
