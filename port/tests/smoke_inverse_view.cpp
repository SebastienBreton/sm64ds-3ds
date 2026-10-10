#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
extern "C" int InvMat4x3(const int*,int*);
int main() {
    if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
    const int view[12]={-1225,-3898,-86,0,90,-4092,3902,-1224,-27,666716,-2155807,4497342};
    const int expected[12]={-1229,2,3914,-3916,90,-1228,-87,-4097,-28,-1765497,4545484,-1252668};
    int inverse[12]{};
    const int result=InvMat4x3(view,inverse);
    printf("Inverse result=%d mode=%lu quotient=%llx\n",result,(unsigned long)ntr::io_read(0x04000280,2),(unsigned long long)ntr::io_read(0x040002a0,8));
    bool same=result==0;
    for(unsigned i=0;i<12;++i){printf("inverse[%u]=%d expected=%d\n",i,inverse[i],expected[i]);same&=inverse[i]==expected[i];}
    puts(same?"PASS exact original DS camera inverse":"FAIL camera inverse");
    return same?0:1;
}
