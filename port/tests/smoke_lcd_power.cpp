#include <3ds.h>
#include <cstdio>
#include "ntr/mmio.h"
extern "C" int func_0205f68c(int,int,int,int);
int main() {
    if (!ntr::io_init()) return 1;
    ntr::io_write(0x04000304,0x820f,2);
    if (func_0205f68c(0,0,0,1)!=1) return 2;
    if (ntr::io_read(0x04000304,2)!=0x820e) return 3;
    if (func_0205f68c(1,0,0,1)!=0) return 4;
    svcSleepThread(150000000);
    if (func_0205f68c(1,0,0,1)!=1) return 5;
    if (ntr::io_read(0x04000304,2)!=0x820f) return 6;
    if (func_0205f68c(0,0,0,1)!=1) return 7;
    if (func_0205f68c(1,0,1,1)!=1) return 8;
    std::puts("LCD power: service dispatch, delay, bypass and DS register agreement verified");
    std::puts("Pixel output not measured by this probe");
    return 0;
}
