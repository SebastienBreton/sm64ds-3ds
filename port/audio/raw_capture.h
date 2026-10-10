#pragma once
#include <cstdint>
// DS capture writes become visible in 16-byte DMA bursts. The destination
// remains live so the matching PCM channel can read the same ring buffer.
struct Sm64dsRawCapture {
 std::uint8_t* destination=nullptr;
 unsigned bytes=4,period=65536;
 bool pcm8=false,oneShot=false,enabled=false;
 unsigned elapsed=0,position=0,offset=0,read=0,write=0,available=0;
 std::uint8_t fifo[16]{};
 void start(){
  if(!destination)return;
  if(!enabled)elapsed=position=offset=read=write=available=0;
  enabled=true;
 }
 void stop(){enabled=false;}
 void flush(){
  for(unsigned i=0;i<4 && available>=4;++i){
   for(unsigned b=0;b<4;++b){destination[offset++]=fifo[read];read=(read+1)&15;}
   available-=4;
   if(offset>=bytes){offset=0;break;}
  }
 }
 void run(unsigned cycles,int sample){
  if(!enabled)return;
  elapsed+=cycles;
  while(elapsed>=period && enabled){
   elapsed-=period;
   const unsigned width=pcm8?1:2;
   const unsigned value=pcm8?unsigned(sample>>8):unsigned(sample);
   for(unsigned b=0;b<width;++b){fifo[write]=value>>(8*b);write=(write+1)&15;}
   available+=width;if(available>=16)flush();
   position+=width;
   if(position>=bytes){
    if(available>=4)flush();
    if(oneShot)stop();else position=0;
   }
  }
 }
};
