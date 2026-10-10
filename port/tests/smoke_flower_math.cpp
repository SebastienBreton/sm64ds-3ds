#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdint>
extern "C" {
int func_020531a4(int);
int func_02053130(int);
void Quaternion_FromVector3(int*,int*,int*);
void Quaternion_Normalize(int*);
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 2;
 unsigned fail=0;
 ntr::Reg<std::uint16_t>(0x040002b0)=1;
 ntr::Reg<std::uint32_t>(0x040002b8)=0;
 ntr::Reg<std::uint32_t>(0x040002bc)=1;
 if(std::uint32_t(ntr::Reg<std::uint32_t>(0x040002b4))!=65536)++fail;
 ntr::Reg<std::uint32_t>(0x040002bc)=4;
 if(std::uint32_t(ntr::Reg<std::uint32_t>(0x040002b4))!=131072)++fail;
 ntr::Reg<std::uint16_t>(0x040002b0)=0;
 ntr::Reg<std::uint32_t>(0x040002b8)=81;
 ntr::Reg<std::uint32_t>(0x040002bc)=16;
 if(std::uint32_t(ntr::Reg<std::uint32_t>(0x040002b4))!=9)++fail;
 const int values[]={4096,8192,16384,65536};
 const int roots[]={4096,5793,8192,16384};
 for(unsigned i=0;i<4;++i){
  ntr::Reg<std::uint16_t>(0x040002b0)=1;
  ntr::Reg<std::uint64_t>(0x040002b8)=std::uint64_t(777)*777;
  const int actual=func_020531a4(values[i]);
  std::printf("sqrt input=%d actual=%d expected=%d\n",values[i],actual,roots[i]);
  if(actual!=roots[i])++fail;
 }
 int up[]={0,4096,0},axis[]={4096,0,0},q[4];
 Quaternion_FromVector3(q,up,axis);
 std::printf("rotation quaternion=%d,%d,%d,%d\n",q[0],q[1],q[2],q[3]);
 if(q[0]!=0 || q[1]!=0 || q[2]!=-2896 || q[3]!=2896)++fail;
 int normalize[]={4096,4096,4096,4096};Quaternion_Normalize(normalize);
 for(int n:normalize)if(n!=2048)++fail;
 std::printf("normalization=%d,%d,%d,%d inverse4096=%d\n",normalize[0],normalize[1],normalize[2],normalize[3],func_02053130(4096));
 std::printf("smoke_flower_math: %s failures=%u\n",fail?"FAIL":"PASS",fail);
 return fail?1:0;
}
