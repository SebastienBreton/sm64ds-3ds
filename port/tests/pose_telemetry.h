// Read-only original ModelAnim2 playback and bones; no renderer modifications.
#pragma once
#include "player_telemetry.h"
#include <cstdio>
#include <cstdint>
namespace pose_telemetry {
static const char* checkpoint=nullptr;
static unsigned behavior_calls=0,render_calls=0;
using player_telemetry::read;
inline void integers(FILE* f,const void* p,unsigned off,unsigned count){std::fprintf(f,"[");for(unsigned i=0;i<count;++i)std::fprintf(f,i?",%d":"%d",read<int32_t>(p,off+i*4));std::fprintf(f,"]");}
inline void bca(FILE* f,const void* p){if(!p){std::fprintf(f,"null");return;}std::fprintf(f,"{\"header_words\":[%u,%u],\"table_relative_offsets\":[",read<uint32_t>(p,0),read<uint32_t>(p,4));for(unsigned i=0;i<4;++i)std::fprintf(f,i?",%d":"%d",int(read<uint32_t>(p,8+i*4)-uint32_t(reinterpret_cast<uintptr_t>(p))));std::fprintf(f,"]}");}
inline bool capture(const void* player,const char* phase){
 if(!checkpoint)return true;
 char path[160];std::snprintf(path,sizeof(path),"pose-%s-%s.json",checkpoint,phase);FILE* f=std::fopen(path,"w");if(!f)return false;
 std::fprintf(f,"{\"schema\":\"sm64ds-pose-telemetry-v1\",\"checkpoint\":\"%s\",\"phase\":\"%s\",\"behavior_calls\":%u,\"render_calls\":%u,\"body_model_id\":%u,\"pose_controls\":{\"hex_742\":%u,\"hex_743\":%u,\"hex_75c_766\":[",checkpoint,phase,behavior_calls,render_calls,unsigned(read<uint8_t>(player,0x6db)),unsigned(read<uint8_t>(player,0x742)),unsigned(read<uint8_t>(player,0x743)));
 for(unsigned i=0;i<6;++i)std::fprintf(f,i?",%d":"%d",int(read<int16_t>(player,0x75c+i*2)));std::fprintf(f,"]},\"models\":[");
 for(unsigned slot=0;slot<4;++slot){
  if(slot)std::fprintf(f,",");const void* m=read<const void*>(player,0xdc+slot*4);if(!m){std::fprintf(f,"null");continue;}
  const void* file=read<const void*>(m,8);const void* bones=read<const void*>(m,0x10);const void* transforms=read<const void*>(m,0x14);unsigned count=read<uint32_t>(file,4);if(!count||count>256||!bones||!transforms){std::fclose(f);return false;}
  std::fprintf(f,"{\"slot\":%u,\"num_bones\":%u,\"world_matrix_fx12\":",slot,count);integers(f,m,0x1c,12);
  std::fprintf(f,",\"num_frames_and_flags\":%u,\"cursor_fx12\":%d,\"speed_fx12\":%d,\"animation_file\":",read<uint32_t>(m,0x54),read<int32_t>(m,0x58),read<int32_t>(m,0x5c));bca(f,read<const void*>(m,0x60));std::fprintf(f,",\"secondary_file\":");bca(f,read<const void*>(m,0x64));
  std::fprintf(f,",\"secondary_num_frames_and_flags\":%u,\"secondary_cursor_fx12\":%d,\"secondary_speed_fx12\":%d,\"local_bones\":[",read<uint32_t>(m,0x6c),read<int32_t>(m,0x70),read<int32_t>(m,0x74));
  for(unsigned i=0;i<count;++i){const char* b=static_cast<const char*>(bones)+i*0x34;std::fprintf(f,i?",{\"scale_fx12\":" : "{\"scale_fx12\":");integers(f,b,0xc,3);std::fprintf(f,",\"flags\":%u,\"rotation_u16\":[%u,%u,%u],\"translation_fx12\":",unsigned(read<uint16_t>(b,0x18)),unsigned(read<uint16_t>(b,0x1a)),unsigned(read<uint16_t>(b,0x1c)),unsigned(read<uint16_t>(b,0x1e)));integers(f,b,0x20,3);std::fprintf(f,",\"override_file\":");bca(f,read<const void*>(b,0x2c));std::fprintf(f,",\"override_frame\":%u}",unsigned(read<uint16_t>(b,0x30)));}
  std::fprintf(f,"],\"bone_matrices_fx12\":[");for(unsigned i=0;i<count;++i){if(i)std::fprintf(f,",");integers(f,transforms,i*48,12);}std::fprintf(f,"]}");
 }
 std::fprintf(f,"]}\n");return std::fclose(f)==0;
}
}
