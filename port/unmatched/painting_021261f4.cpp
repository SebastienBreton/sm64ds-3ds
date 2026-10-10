// Original ov080 render handler021261f4, recovered from the supplied ROM.
// The PMF table passes the painting receiver in r0 (AAPCS, no return value).
#include "ntr/mmio.h"
#include <cstdint>
#include <cstring>
extern "C" {
void MulMat4x3Mat4x3(const int*,const int*,int*);
void func_02055998(int*);
void func_02055388(int*);
void func_020553a4(int*);
extern int data_0209b3ec[];
extern int data_ov080_0212771c[];
}
namespace {
std::uint32_t word(const unsigned char* p,unsigned offset){
 std::uint32_t v;std::memcpy(&v,p+offset,4);return v;
}
void writeGX(unsigned address,std::uint32_t value){NTR_MMIO(std::uint32_t,address)=value;}
std::uint16_t vertexComponent(std::uint32_t value){
 // ROM LSL9/ASR16 then truncation to16bits, including wrapping large inputs.
 return static_cast<std::uint16_t>((value<<9)>>16);
}
void emit(const unsigned char* vertex){
 writeGX(0x04000484,word(vertex,0x10));
 const auto x=vertexComponent(word(vertex,0));
 const auto y=vertexComponent(word(vertex,4));
 const auto z=vertexComponent(word(vertex,8));
 writeGX(0x0400048c,std::uint32_t(x)|(std::uint32_t(y)<<16));
 writeGX(0x0400048c,z);
}
}
extern "C" void func_ov080_021261f4(void* receiver){
 auto* self=static_cast<unsigned char*>(receiver);
 int matrix[12];
 MulMat4x3Mat4x3(reinterpret_cast<const int*>(self+0xd4),data_0209b3ec,matrix);
 writeGX(0x04000444,0);
 func_02055998(data_ov080_0212771c);
 func_02055388(matrix);
 writeGX(0x04000440,2);
 func_020553a4(matrix);
 writeGX(0x04000488,0x04000400);
 writeGX(0x040004c8,0xe0000000);
 writeGX(0x040004cc,0xc0007fff);
 auto* texture=reinterpret_cast<const unsigned char*>(std::uintptr_t(word(self,0x1a8)));
 writeGX(0x040004c0,word(texture,8)|0x7fff0000);
 writeGX(0x040004c4,word(texture,12));
 const auto params=word(texture,16);
 writeGX(0x040004a8,(word(texture,0)>>3)|0x80000000|
  (params&0x3ff00000));
 const unsigned format=(params>>26)&7;
 writeGX(0x040004ac,word(texture,4)>>(format==2?3:4));
 const unsigned alpha=((word(self,8)>>8)&31)==7?20:31;
 writeGX(0x040004a4,0x01000088|(alpha<<16));
 writeGX(0x0400046c,0x10000);writeGX(0x0400046c,0x10000);writeGX(0x0400046c,0x10000);
 for(int col=0;col<int(self[0x1ba])-1;++col){
  writeGX(0x04000500,2);
  for(unsigned row=0;row<self[0x1bb];++row){
   auto* cells=reinterpret_cast<const unsigned char*>(std::uintptr_t(word(self,0x1a0)));
   const unsigned index=col+row*self[0x1bb];
   emit(cells+index*24);emit(cells+(index+1)*24);
  }
  writeGX(0x04000504,0);
 }
 writeGX(0x04000448,1);
}
