#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
int main(){
 if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
 char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
 std::int16_t pcm[2048];
 auto* finite=sm64ds_ssar_open(path,4,0,32768);if(!finite || !sm64ds_sseq_playing(finite))return 3;
 unsigned chunks=0,active=0;
 while(chunks<640 && (sm64ds_sseq_playing(finite)||sm64ds_sseq_channels(finite))){
  if(!sm64ds_sseq_render(finite,pcm,1024))return 4;
  active|=sm64ds_sseq_channels(finite);++chunks;
 }
 std::printf("finite sequence: chunks=%u ever_active=%04x playing=%u channels=%04x\n",chunks,active,sm64ds_sseq_playing(finite),sm64ds_sseq_channels(finite));
 if(!active || sm64ds_sseq_playing(finite) || sm64ds_sseq_channels(finite))return 5;
 sm64ds_sseq_close(finite);
 for(unsigned mode=0;mode<2;++mode){
  auto* seq=sm64ds_sseq_open(path,55,32768);if(!seq)return 6;
  for(unsigned i=0;i<32;++i)if(!sm64ds_sseq_render(seq,pcm,1024))return 7;
  if(!sm64ds_sseq_playing(seq)||!sm64ds_sseq_channels(seq))return 8;
  sm64ds_sseq_stop(seq,false);
  const unsigned release=sm64ds_sseq_channels(seq);
  if(sm64ds_sseq_playing(seq)||!release)return 9;
  if(mode)sm64ds_sseq_stop(seq,true);
  unsigned drain=0;
  while(sm64ds_sseq_channels(seq) && drain<640){if(!sm64ds_sseq_render(seq,pcm,1024))return 10;++drain;}
  if(sm64ds_sseq_channels(seq))return 11;
  if(!sm64ds_sseq_render(seq,pcm,1024))return 12;
  for(auto value:pcm)if(value)return 13;
  std::printf("stop mode=%u initial_release=%04x drain_chunks=%u silent=1\n",mode,release,drain);
  sm64ds_sseq_close(seq);
 }
 std::puts("smoke_sseq_state: PASS natural completion, release tails, immediate kill and silence");return 0;
}
