// Original ARM9 initialization/guard transaction, with the real alarm queue.
#include "ntr/mmio.h"
#include "hal/alarm_layout.h"
#include <cstdio>
#include <cstring>
extern "C" {
void func_02059bc0();void __sinit_02074e0c();
unsigned char func_0201a9ec(void*);
unsigned char func_0201a96c(void*,unsigned char);
void func_0201a9fc(void*);
void sm64ds_alarm_poll();
extern unsigned char data_0209d574[];
}
#define CHECK(c) do{if(!(c)){std::printf("FAIL watchdog line=%d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
 CHECK(ntr::io_init());func_02059bc0();
 __sinit_02074e0c();
 uint64_t limit,base;
 std::memcpy(&limit,data_0209d574+0x30,8);std::memcpy(&base,data_0209d574+0x38,8);
 CHECK(limit==0 && base==0 && data_0209d574[0x40]==1);
 for(unsigned i=0;i<48;++i)CHECK(data_0209d574[i]==0);
 for(unsigned i=0;i<32;++i){
  const auto previous=func_0201a9ec(data_0209d574);
  CHECK(previous==1);func_0201a9fc(data_0209d574);
  CHECK(func_0201a96c(data_0209d574,previous)==1);
  CHECK(data_0209d574[0x40]==1);
  CHECK(!data_020a6444[0] && !data_020a6444[1]);
  sm64ds_alarm_poll();
 }
 std::puts("PASS original watchdog ctor and 32 real guard transactions: no zero-limit alarm");
 return 0;
}
