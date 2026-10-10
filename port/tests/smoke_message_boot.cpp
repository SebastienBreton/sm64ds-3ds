#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
extern "C" {
void func_02059bc0();void __sinit_02075154();void __sinit_0207501c();
void func_0201fe08();void func_0201b6f8(int);void Deallocate(void*);
int func_0203d974();
extern unsigned char data_020a0f00,data_020a0f10;
extern unsigned char *data_0209d6fc,*data_0209d708,*data_0209d70c,*data_0209d6f4;
extern unsigned char data_0209d6bc,data_0209d6a4,data_0209d6b0;
}
int main(){
 if(!ntr::io_init())return 2;auto* root=Heap::SetupRootHeap();if(!root)return 2;
 func_02059bc0();__sinit_02075154();__sinit_0207501c();
 auto* mode=ntr::xlate<unsigned short>((void*)0x027ffc40u);
 for(unsigned v=0;v<3;++v){*mode=v;data_020a0f10=0;if(func_0203d974()!=(v==2))return 3;}
 *mode=0;data_020a0f10=1;if(func_0203d974()!=1)return 4;data_020a0f10=0;
 const auto before=root->VMemoryLeft();
 for(unsigned language=1;language<=5;++language){
  data_020a0f00=language;func_0201fe08();
  auto* b=data_0209d6fc;if(!b || std::memcmp(b,"GSEM1gmb",8))return 5;
  if(data_0209d708!=b+0x30 || data_0209d70c!=b+0x20 || std::memcmp(b+0x20,"1FNI",4))return 6;
  const unsigned size=*reinterpret_cast<unsigned*>(b+8);
  for(unsigned id=0x280;id<=0x282;++id){
   const unsigned offset=0x28+*reinterpret_cast<unsigned*>(b+0x24)+*reinterpret_cast<unsigned*>(data_0209d708+id*8);
   if(offset>=size)return 7;
   unsigned end=offset;while(end<size && b[end]!=0xff && b[end]!=0xfd)++end;
   if(end==size)return 8;
   data_0209d6f4=b+offset;func_0201b6f8(1);
   if(data_0209d6bc!=(b[end]==0xff?8:2))return 9;
   std::printf("message language=%u id=%u bytes=%u glyphs=%u width=%u\n",language,id,end-offset,data_0209d6a4,data_0209d6b0);
  }
  Deallocate(b);data_0209d6fc=nullptr;data_0209d708=nullptr;data_0209d70c=nullptr;data_0209d6f4=nullptr;
  if(root->VMemoryLeft()!=before)return 10;
 }
 std::puts("smoke_message_boot: PASS five language banks, 15 controller labels, DS boot-mode mirror");return 0;
}
