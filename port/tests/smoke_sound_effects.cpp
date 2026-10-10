#include "ntr/mmio.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../../../build3ds/validation/sound-effects/reference.h"
extern "C" {
void func_020499bc(void*,void*,unsigned,void*);
void func_020497a0(void*,void*,unsigned,void*);
}
static unsigned hash(unsigned h,const void* data,unsigned size){
 auto* b=static_cast<const unsigned char*>(data);
 for(unsigned i=0;i<size;++i)h=(h^b[i])*16777619u;
 return h;
}
int main(){
 if(!ntr::io_init())return 2;
 for(unsigned mode=1;mode<=2;++mode){
  alignas(4) unsigned char state[0xcc]{};
  std::int16_t left[512],right[512];
  for(unsigned block=0;block<32;++block){
   const unsigned sizes[]={2,24,25,512};unsigned count=sizes[block%4];
   for(unsigned i=0;i<count;++i){
    left[i]=int((i*7919+block*1733)&65535)-32768;
    right[i]=int((i*7919+block*1733+19001)&65535)-32768;
   }
   (mode==1?func_020499bc:func_020497a0)(left,right,count*2,state);
   unsigned h=hash(2166136261u,left,count*2);h=hash(h,right,count*2);h=hash(h,state,sizeof state);
   unsigned expected=(mode==1?mode1:mode2)[block];
   if(h!=expected){std::printf("effect %u block %u mismatch %08x != %08x\n",mode,block,h,expected);return 3;}
  }
 }
 std::puts("smoke_sound_effects: PASS both original effects,64 blocks,ARM9 PCM/state hashes including saturation and partial blocks");return 0;
}
