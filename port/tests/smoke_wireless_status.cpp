#include "ntr/mmio.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
extern "C" {
int func_020618b8();int func_0206152c();int func_0203d9f4();
extern unsigned short data_020a89a8,data_020a0f1c[];
extern void* data_020a89ac;
}
int main() {
    if(!ntr::io_init())return 2;
    if(data_020a89a8 || data_020a89ac)return 3;
    if(func_0206152c()!=3 || func_020618b8()!=0)return 4;
    data_020a0f1c[0]=0x8000;if(func_0203d9f4()!=0)return 5;
    alignas(4) unsigned short status[0xa00/2]={};
    struct Work {unsigned unused;void* status;} work={0,status};
    data_020a89ac=&work;data_020a89a8=1;
    if(func_0206152c()!=0)return 6;
    unsigned checked=0;
    for(unsigned state=0;state<16;++state)for(unsigned enabled=0;enabled<2;++enabled)
    for(unsigned value:{0u,1u,0x8000u,0xffffu}) {
        status[0]=state;status[0x17e/2]=enabled;status[0xb8/2]=value;
        const unsigned expected=(state==10 || state==11 || (state==9 && enabled))?value:0;
        if(func_020618b8()!=int(expected))return 7;
        data_020a0f1c[0]=0;if(func_0203d9f4()!=0)return 8;
        data_020a0f1c[0]=0x8000;if(func_0203d9f4()!=int(expected))return 9;
        ++checked;
    }
    data_020a89a8=0;data_020a89ac=nullptr;data_020a0f1c[0]=0;
    if(func_020618b8()!=0)return 10;
    std::printf("smoke_wireless_status: PASS %u controlled status cases and uninitialized guard; no wireless connection exercised\n",checked);
    return 0;
}
