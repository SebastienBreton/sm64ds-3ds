#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
int main() {
 if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 const unsigned cases[][3]={{1,34,6},{1,92,7},{1,98,7},{1,162,8},{1,226,9},{2,57,21},{2,102,21}};
 for(const auto& c:cases){
  Sm64dsBankState state;
  auto* prior=sm64ds_ssar_open(path,c[0],c[1],32768,c[2],&state);
  if(!prior){std::printf("prime: %s\n",sm64ds_sseq_error());return 3;}
  sm64ds_sseq_close(prior);
  const unsigned file=state.fileId;
  auto* seq=sm64ds_ssar_open(path,c[0],c[1],32768,0xffffffffU,&state);
  if(!seq || state.fileId!=file)return 4;
  uint32_t hash=2166136261u;unsigned nonzero=0,peak=0;std::int16_t pcm[2048];
  for(unsigned i=0;i<32;++i){
   if(!sm64ds_sseq_render(seq,pcm,1024)){std::printf("render: %s\n",sm64ds_sseq_error());return 5;}
   for(auto v:pcm){if(v)++nonzero;unsigned m=v<0?-int(v):v;if(m>peak)peak=m;}
   for(auto b:reinterpret_cast<const unsigned char(&)[4096]>(pcm)){hash^=b;hash*=16777619u;}
  }
  sm64ds_sseq_close(seq);
  std::printf("binding archive=%u entry=%u prior=%u file=%u hash=%08lx nonzero=%u peak=%u\n",c[0],c[1],c[2],file,(unsigned long)hash,nonzero,peak);
  if(!nonzero)return 6;
  // Reusing a caller state for a different instrument file must drop old slots.
  seq=sm64ds_ssar_open(path,4,0,32768,0xffffffffU,&state);
  if(!seq || state.fileId!=93 || state.waveArchives[0]!=0 || state.waveArchives[1]!=0xffff || state.waveArchives[2]!=0xffff || state.waveArchives[3]!=0xffff)return 7;
  sm64ds_sseq_close(seq);
 }
 std::puts("smoke_ssar_bindings: PASS seven inherited bindings and cross-file reset");return 0;
}
