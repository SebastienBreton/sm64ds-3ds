#include "ntr/mmio.h"
#include "Heap.h"
#include "audio/sound_commands.h"
#include "audio/sound_native.h"
#include <cstdio>
#include <initializer_list>
extern "C" {
void func_020133bc();
void _ZN5Sound16LoadInitialGroupEi(int);
void _ZN5Sound19LoadGroupAndSetBankEii(int,int);
extern unsigned char *data_020a5bb8;
int func_0205b070(int);
void** func_0205b274(int);
int func_0205179c(unsigned,int,void*);
unsigned* _ZN5Sound17InfoSequenceEntry9GetWithIDEj(unsigned);
unsigned* _ZN5Sound23InfoInstrumentBankEntry9GetWithIDEj(unsigned);
void* func_020509b0(unsigned);
void Snd_SendCommand(unsigned,unsigned,unsigned,unsigned,unsigned);
void func_02050798();
void func_02049c48(int);
void func_020512f0(void*);
int FS_CloseFile(void*);
void _ZN6Memory10DeallocateEPv(void*);
extern void* data_0209b498;
extern unsigned char data_0209b4b4[],data_0209baa0[];
extern unsigned data_020a4c64[3],data_020a5600[5],data_020a5634[20];
extern unsigned* data_020a7fc0;
extern Sm64dsSoundNode* data_020a648c;
extern unsigned char data_0209b480;
}
static unsigned callbacks=0,errors=0;
static void observe(unsigned left,unsigned right,unsigned bytes,int format,unsigned context){
 unsigned offset=(callbacks&1)*1024;
 if(left!=reinterpret_cast<unsigned>(data_0209baa0+offset)||right!=reinterpret_cast<unsigned>(data_0209baa0+2048+offset)||bytes!=1024||format||context!=reinterpret_cast<unsigned>(&callbacks))++errors;
 ++callbacks;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 const auto before=root->VMemoryLeft();
 std::puts("starting original complete sound boot");std::fflush(stdout);
 func_020133bc();
 // Original BootScene initializes group0 before any menu/level group restore.
 _ZN5Sound16LoadInitialGroupEi(0);
 void* info=*reinterpret_cast<void**>(data_020a5bb8+0x84);
 void* fat=*reinterpret_cast<void**>(data_020a5bb8+0x7c);
 if(!info||!fat)return 16;
 for(int group:{3,37,2}) {
  _ZN5Sound19LoadGroupAndSetBankEii(group,0x36);
  if(*reinterpret_cast<void**>(data_020a5bb8+0x84)!=info ||
     *reinterpret_cast<void**>(data_020a5bb8+0x7c)!=fat)return 17;
 }
 std::printf("sound boot returned ready=%u\n",data_0209b480);
 unsigned count=0;
 for(auto* node=data_020a648c;node;node=reinterpret_cast<Sm64dsSoundNode*>(node->next)){
  if(++count>256)return 3;
  std::printf("pending sound command %02x args=%08x,%08x,%08x,%08x\n",node->command,node->args[0],node->args[1],node->args[2],node->args[3]);
 }
 std::fflush(stdout);
 if(!func_0205b070(1))return 4;
 // Group changes may already have flushed and recycled the boot batch.
 while(func_0205b274(0)){}
 data_020a4c64[1]=reinterpret_cast<unsigned>(observe);data_020a4c64[2]=reinterpret_cast<unsigned>(&callbacks);
 if(!func_0205179c(55,7,data_0209b498))return 6;
 auto* seqInfo=_ZN5Sound17InfoSequenceEntry9GetWithIDEj(55);
 auto* bankInfo=_ZN5Sound23InfoInstrumentBankEntry9GetWithIDEj(*reinterpret_cast<unsigned short*>(reinterpret_cast<char*>(seqInfo)+4));
 auto* seq=static_cast<unsigned char*>(func_020509b0(seqInfo[0]));auto* bank=func_020509b0(bankInfo[0]);
 if(!seq||!bank)return 7;
 Snd_SendCommand(0,3,reinterpret_cast<unsigned>(seq+*reinterpret_cast<unsigned*>(seq+24)),0,reinterpret_cast<unsigned>(bank));
 if(!func_0205b070(1)||!func_0205b274(1))return 8;
 std::int16_t pcm[1024];unsigned audible=0;
 for(unsigned block=0;block<64;++block){
  if(block && block%16==0){func_02049c48((block/16)%3);if(!func_0205b070(1))return 15;while(func_0205b274(0)){} }
  if(!sm64ds_native_sound_render(pcm,512))return 9;for(auto value:pcm)audible+=value!=0;
 }
 const std::uint64_t cycles=std::uint64_t(32768)*512; // DS-native: 512 timer ticks per frame
 const unsigned expected=1+(cycles-0x2100u*32)/(0x2000u*32);
 auto* status=reinterpret_cast<Sm64dsSoundStatus*>(data_020a7fc0);
 std::printf("full sound boot playback: callbacks=%u expected=%u errors=%u audible=%u captures=%u channels=%04x\n",callbacks,expected,errors,audible,status->activeCaptures,status->enabledChannels);
 if(callbacks!=expected||errors||!audible||status->activeCaptures!=3||(status->enabledChannels&10)!=10||data_020a5600[4]||data_020a5634[6]!=(callbacks&1))return 10;
 func_02050798();if(!func_0205b070(1))return 11;while(func_0205b274(0)){}
 if(status->activeCaptures || data_020a5634[0])return 12;
 if(!FS_CloseFile(data_0209b4b4+0x30))return 13;
 void* heap=data_0209b498;func_020512f0(heap);_ZN6Memory10DeallocateEPv(heap);data_0209b498=nullptr;
 sm64ds_native_sound_shutdown();
 if(root->VMemoryLeft()!=before){std::printf("sound boot heap remaining %u expected %u\n",root->VMemoryLeft(),before);return 14;}
 std::puts("smoke_sound_boot: PASS original complete boot,SDAT title playback,capture/PCM/alarm/worker chain,shutdown and exact heap recovery");return 0;
}
