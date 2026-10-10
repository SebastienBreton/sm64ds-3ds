#include "ntr/mmio.h"
#include "unmatched/actor_registry.h"
#include "unmatched/native_overlay.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
extern "C" {
bool _ZN4Heap13SetupRootHeapEv();
void sm64ds_init_common_scenery_profiles();
void sm64ds_init_common_level_actor_profiles();
int sm64ds_set_overlay_scope(int);
unsigned sm64ds_check_common_profiles();
extern unsigned char g_profile_TREE[],g_profile_CH_SCENE[],g_profile_SET_SE[],g_profile_MAP[],g_profile_DEMO[];
extern unsigned char data_02099e94[8],data_02099ebc[8],data_02099ec4[8];
}
int main(){
 if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
 if(data_020a4bb8!=data_020a4bb8_storage)return 3;
 sm64ds_init_common_scenery_profiles();
 if(sm64ds_check_common_profiles()!=24)return 11;
 const int scope=sm64ds_set_overlay_scope(2);sm64ds_init_common_level_actor_profiles();sm64ds_set_overlay_scope(scope);
 if(sm64ds_check_common_profiles()!=46)return 13;
 std::puts("common ov2 profiles: 46 complete ROM-checked ABI records and 17 coin/egg PMFs installed");
 if(data_020a4bb8_storage[352]!=g_profile_DEMO || data_020a4bb8_storage[335]!=g_profile_MAP || data_020a4bb8_storage[286]!=g_profile_TREE || data_020a4bb8_storage[349]!=g_profile_CH_SCENE || data_020a4bb8_storage[350]!=g_profile_SET_SE)return 10;
 void* initial[512];std::memcpy(initial,data_020a4bb8_storage,sizeof(initial));
 unsigned char pmfs[24];std::memcpy(pmfs,data_02099e94,8);std::memcpy(pmfs+8,data_02099ebc,8);std::memcpy(pmfs+16,data_02099ec4,8);
 struct Row {unsigned overlay,count,ids[14];};
 const Row rows[]={
  {62,6,{190,203,204,188,205,239}},
  {85,6,{185,186,187,229,235,183}},
  {89,2,{282,283}},
  {94,1,{234}},
  {98,5,{299,300,194,207,208}},
  {100,7,{336,220,337,353,354,344,31}},
  {102,14,{61,59,49,298,340,341,22,20,21,23,25,24,206,285}},
};
 for(unsigned cycle=0;cycle<3;++cycle){
  bool installed[512]={};LoadOverlay(9);
  for(unsigned id:{343u,338u,339u,342u}){
   installed[id]=true;
   auto* profile=static_cast<unsigned char*>(data_020a4bb8_storage[id]);
   if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=id)return 9;
  }
  for(const auto& row:rows){
   LoadOverlay(row.overlay);
   for(unsigned j=0;j<row.count;++j){
    const unsigned id=row.ids[j];installed[id]=true;
    auto* profile=static_cast<unsigned char*>(data_020a4bb8_storage[id]);
    if(!profile || !*reinterpret_cast<void**>(profile) || *reinterpret_cast<unsigned short*>(profile+4)!=id)return 4;
   }
   if(sm64ds_check_common_profiles()!=46)return 12;
   if(data_020a4bb8!=data_020a4bb8_storage)return 5;
   if(std::memcmp(pmfs,data_02099e94,8)||std::memcmp(pmfs+8,data_02099ebc,8)||std::memcmp(pmfs+16,data_02099ec4,8))return 6;
   for(unsigned i=0;i<512;++i)if(!installed[i] && data_020a4bb8_storage[i]!=initial[i])return 7;
  }
  for(int i=6;i>=0;--i)UnloadOverlay(rows[i].overlay);
  UnloadOverlay(9);
  if(std::memcmp(initial,data_020a4bb8_storage,sizeof(initial)))return 8;
  if(sm64ds_check_common_profiles()!=46)return 12;
  std::printf("registry cycle=%u eight overlays including castle, actual512-slot storage, PMFs intact\n",cycle);
 }
 std::puts("smoke_actor_registry: PASS actual registry installation and restoration");return 0;
}
