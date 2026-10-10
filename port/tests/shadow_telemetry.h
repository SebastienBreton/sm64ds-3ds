// Read-only shadow diagnostics; original models/matrices are never modified.
#pragma once
#include "ShadowModel.h"
#include "player_telemetry.h"
#include "ntr/gx.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdint>
extern "C" { extern ShadowModel* data_0209cef4;extern unsigned char data_0209ceec;extern BMD_File data_020ad560;extern BMD_File data_020ad524; }
namespace shadow_telemetry {
static const char* checkpoint=nullptr;
static size_t range_begin=0,range_end=0;
static unsigned calls=0;
inline void before_render(){if(checkpoint){ntr::gx_polygons(range_begin);++calls;}}
inline void after_render(){if(checkpoint)ntr::gx_polygons(range_end);}
inline void matrix(FILE* f,const Matrix4x3* m){std::fprintf(f,"[");for(unsigned i=0;i<12;++i)std::fprintf(f,i?",%d":"%d",player_telemetry::read<std::int32_t>(m,i*4));std::fprintf(f,"]");}
inline uint32_t fnv(const void* bytes,size_t size){uint32_t result=2166136261u;const auto* p=static_cast<const uint8_t*>(bytes);while(size--)result=(result^*p++)*16777619u;return result;}
inline bool capture(const char* label,const void* player){
 char path[128];std::snprintf(path,sizeof(path),"shadow-%s.json",label);FILE* f=std::fopen(path,"w");if(!f)return false;
 std::fprintf(f,"{\"schema\":\"sm64ds-shadow-telemetry-v1\",\"checkpoint\":\"%s\",\"freeze\":%u,\"shadow_render_calls\":%u,\"shadow_range\":[%u,%u],\"registers\":{",label,unsigned(data_0209ceec),calls,unsigned(range_begin),unsigned(range_end));
 const unsigned regs[]={0x04000000,0x04001000,0x04000060,0x04000304,0x04000050,0x04001050,0x04000340,0x04000350,0x04000354,0x04000540};
 for(unsigned i=0;i<sizeof(regs)/sizeof(*regs);++i){const unsigned a=regs[i];const bool wide=a==0x04000000||a==0x04001000||a==0x04000350||a==0x04000354||a==0x04000540;std::fprintf(f,i?",\"%08x\":%u":"\"%08x\":%u",a,unsigned(ntr::io_read(a,wide?4:2)));}
 std::fprintf(f,"},\"player_shadow_controls\":{\"0x6f5\":%u,\"0x6fb\":%u,\"0x717\":%u},\"nodes\":[",unsigned(player_telemetry::read<uint8_t>(player,0x6f5)),unsigned(player_telemetry::read<uint8_t>(player,0x6fb)),unsigned(player_telemetry::read<uint8_t>(player,0x717)));
 unsigned count=0;
 for(auto* node=data_0209cef4;node;node=node->next){
  if(++count>512||!node->data||!node->mat){std::fclose(f);return false;}
  const auto* model=node->data->modelFile;const char* kind=model==&data_020ad560?"builtin_cylinder":model==&data_020ad524?"builtin_cuboid":"other";
  if(!model||model->numMaterials>256||model->numDisplayListGroups>256){std::fclose(f);return false;}
  std::fprintf(f,count>1?",{":"{");std::fprintf(f,"\"is_player\":%s,\"model_kind\":\"%s\",\"scale_fx12\":[%d,%d,%d],\"opacity\":%u,\"matrix_fx12\":",reinterpret_cast<const char*>(node)==static_cast<const char*>(player)+0x2ac?"true":"false",kind,node->scale.x,node->scale.y,node->scale.z,unsigned(node->opacity));matrix(f,node->mat);
  std::fprintf(f,",\"scale_shift\":%u,\"num_bones\":%u,\"num_materials\":%u,\"num_display_list_groups\":%u,\"materials\":[",model->scaleShift,model->numBones,model->numMaterials,model->numDisplayListGroups);
  for(unsigned j=0;j<model->numMaterials;++j){auto* m=node->data->materials+j;std::fprintf(f,j?",{":"{");std::fprintf(f,"\"tex_param\":%u,\"palette_base\":%u,\"polygon_attr\":%u,\"dif_amb\":%u,\"spe_emi\":%u}",player_telemetry::read<uint32_t>(m,0x1c),player_telemetry::read<uint32_t>(m,0x20),m->flags,player_telemetry::read<uint32_t>(m,0x28),player_telemetry::read<uint32_t>(m,0x2c));}
  std::fprintf(f,"],\"display_lists\":[");unsigned lists=0;
  for(unsigned j=0;j<model->numDisplayListGroups;++j){const auto& group=model->displayListGroups[j];if(group.numLists>256){std::fclose(f);return false;}for(unsigned k=0;k<group.numLists;++k){const auto& list=group.lists[k];if(list.size>1024*1024){std::fclose(f);return false;}std::fprintf(f,lists++?",{":"{");std::fprintf(f,"\"size\":%u,\"fnv1a\":%u}",list.size,fnv(list.data,list.size));}}
  std::fprintf(f,"]}");
 }
 std::fprintf(f,"],\"triangles\":[");size_t total=0;const auto* triangles=ntr::gx_polygons(total);
 for(size_t i=0;i<total;++i){const auto& t=triangles[i];std::fprintf(f,i?",{":"{");std::fprintf(f,"\"index\":%u,\"polygon_attr\":%u,\"texture_size\":[%d,%d],\"wrap\":%u,\"vertices\":[",unsigned(i),t.polygon_attr,t.tw,t.th,t.wrap);
  for(unsigned j=0;j<3;++j){const auto& v=t.v[j];std::fprintf(f,j?",{":"{");std::fprintf(f,"\"clip\":[%.9g,%.9g,%.9g,%.9g],\"screen\":[%.9g,%.9g],\"z\":%.9g,\"w\":%.9g,\"uv\":[%.9g,%.9g],\"color\":%u}",double(v.cx),double(v.cy),double(v.cz),double(v.cw),double(v.x),double(v.y),double(v.z),double(v.w),double(v.u),double(v.v),v.color);}
  std::fprintf(f,"]}");
 }
 std::fprintf(f,"]}\n");return std::fclose(f)==0;
}
}
