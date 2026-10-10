#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdint>
extern "C" int _ZN4cstd4fdivEii(int,int);
extern "C" void _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(int,int,int,int,int,int,int,int*);
int main() {
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
 ntr::io_write(0x04000280,0,2);
 int quotient=_ZN4cstd4fdivEii(4096,4096);
 auto mode=ntr::io_read(0x04000280,2)&3;
 std::printf("division=%d mode=%u\n",quotient,mode);
 if(quotient!=4096 || mode!=1)return 2;
 if(ntr::io_read(0x040002a0,4)!=0 || ntr::io_read(0x040002a4,4)!=1)return 3;
 int matrix[16];for(auto& x:matrix)x=0x55555555;
 _ZN3G3i13PerspectiveW_E5Fix12IiES1_S1_S1_S1_S1_bP9Matrix4x3(2896,2896,0x1555,0x1000,0x1388000,0x100,1,matrix);
 const int expected[16]={192,0,0,0,0,256,0,0,0,0,-256,-256,0,0,-512,0};
 for(int i=0;i<16;++i){std::printf("matrix[%d]=%d expected=%d\n",i,matrix[i],expected[i]);if(matrix[i]!=expected[i])return 4;}
 std::puts("smoke_projection_divider: PASS original fixed division retains hardware mode and PerspectiveW matrix");
 return 0;
}
