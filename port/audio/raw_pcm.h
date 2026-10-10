#pragma once
#include <cstdint>
#include <cstring>
// DS PCM8/PCM16 playback with a live source and a 32-byte DMA FIFO. Timing is
// expressed in ARM7 half-clock cycles, not host callback invocations.
struct Sm64dsRawPcm {
 const std::uint8_t* source=nullptr;
 unsigned format=0,repeat=0,loopBytes=0,lengthBytes=0,period=0,volume=0,divider=0,pan=0;
 bool enabled=false,keyOn=false;
 std::uint8_t fifo[32]{};
 unsigned read=0,write=0,available=0,sourceOffset=0,elapsed=0;
 int position=-3,sample=0;
 void refill(){
  const unsigned end=loopBytes+lengthBytes;
  if(sourceOffset>=end){if(repeat==1)sourceOffset=loopBytes;else return;}
  const unsigned bytes=end-sourceOffset<16?end-sourceOffset:16;
  for(unsigned i=0;i<bytes;++i){fifo[write]=source[sourceOffset++];write=(write+1)&31;}
  available+=bytes;
 }
 void start(){
  if(!source)return;
  if(!enabled)keyOn=true;
  enabled=true;
 }
 void stop(){enabled=false;sample=0;}
 int run(unsigned cycles,int volumeOverride=-1){
  if(!enabled)return 0;
  if(keyOn){
   keyOn=false;read=write=available=sourceOffset=elapsed=0;position=-3;sample=0;
   refill();refill();
  }
  elapsed+=cycles;
  while(elapsed>=period && enabled){
   elapsed-=period;
   if(++position<0)continue;
   const unsigned width=format?2:1;
   if(unsigned(position)*width>=loopBytes+lengthBytes){
    if(repeat==1)position=loopBytes/width;else {stop();break;}
   }
   unsigned value=fifo[read];read=(read+1)&31;
   if(format){value|=unsigned(fifo[read])<<8;read=(read+1)&31;sample=static_cast<std::int16_t>(value);}
   else sample=int(static_cast<std::int8_t>(value))*256;
   available-=width;if(available<=16)refill();
  }
  const unsigned shift=divider==3?4:divider;
  const unsigned effective=volumeOverride<0?volume:unsigned(volumeOverride);
  return ((sample*int(effective==127?128:effective))>>7)>>shift;
 }
};
