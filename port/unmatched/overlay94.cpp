#include "unmatched/actor_registry.h"
// Native lifecycle for the owl actor. Actors must be gone
// before unloading; shared-handle destructors do not release live actor refs.
#include <cstring>
#include <cstdint>
extern "C" {
extern unsigned char sm64ds_overlay94_bss_begin[],sm64ds_overlay94_bss_end[];
extern unsigned char g_profile_OWL[];
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_destroy_overlay_resources(int);
void __sinit_ov094_021367e8();
}
namespace {
bool loaded=false;
const unsigned ids[]={234};
void* const profiles[]={g_profile_OWL};
void* previous_profiles[1];
}
extern "C" bool sm64ds_load_overlay94() {
    if(loaded)return false;
    const auto bytes=reinterpret_cast<uintptr_t>(sm64ds_overlay94_bss_end)-reinterpret_cast<uintptr_t>(sm64ds_overlay94_bss_begin);
    if(bytes!=160)return false;
    std::memset(sm64ds_overlay94_bss_begin,0,bytes);
    const int previous=sm64ds_set_overlay_scope(94);
    __sinit_ov094_021367e8();
    sm64ds_set_overlay_scope(previous);
    for(unsigned i=0;i<1;++i) {previous_profiles[i]=data_020a4bb8[ids[i]];data_020a4bb8[ids[i]]=profiles[i];}
    loaded=true;return true;
}
extern "C" bool sm64ds_unload_overlay94() {
    if(!loaded)return false;
    sm64ds_destroy_overlay_resources(94);
    for(unsigned i=0;i<1;++i)data_020a4bb8[ids[i]]=previous_profiles[i];
    loaded=false;return true;
}
