// Regression for the retail menu SDK reset clearing emulated FIFO-ready bits.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "Heap.h"
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
int main() {
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
 constexpr std::uint32_t a=0x04000600u;
 const auto ready=ntr::io_read(a,4)&0x3fffffffu;
 if((ready&0x06000000u)!=0x06000000u)return 2;
 for(std::uint32_t v:{0x80008000u,0xffffffffu,0u,0x40000000u,0x8000u}) {
   ntr::io_write(a,v,4);
   const auto expected=(ready&~0xa000u)|(v&0xc0000000u);
   if(ntr::io_read(a,4)!=expected)return 3;
 }
 ntr::io_write(a+3,0xc0,1);
 if(ntr::io_read(a,4)!=(ready|0xc0000000u))return 4;
 ntr::io_write(a,0xffff,2);
 if(ntr::io_read(a,4)!=(ready|0xc0000000u))return 5;
 ntr::io_write(a+2,0x4000,2);
 if(ntr::io_read(a,4)!=(ready|0x40000000u))return 6;
 ntr::io_write(a+1,0xff,1);
 ntr::io_write(a,0xff,1);
 if(ntr::io_read(a,4)!=(ready|0x40000000u))return 7;
 ntr::gx_reset();
 static const std::uint32_t texture[4]={0xffffffffu,0xff00ff00u,0xffff0000u,0xff0000ffu};
 ntr::gx_bind_texture(texture,2,2);
 ntr::gx_write_port(0x04000440u,1);
 for(unsigned v:{1024u,512u,0u})ntr::gx_write_port(0x04000470u,v);
 ntr::gx_write_port(0x04000480u,0x03ff);
 auto draw=[](){
   ntr::gx_write_port(0x04000500u,0);
   for(unsigned v:{0u,0u,2048u,0u,0x08000000u,0u})ntr::gx_write_port(0x0400048cu,v);
   ntr::gx_write_port(0x04000504u,0);
 };
 draw();size_t count=0;auto* triangles=ntr::gx_polygons(count);if(count!=1)return 8;
 const auto first=triangles[0];const auto state=ntr::gx_state_hash();
 ntr::gx_begin_frame();ntr::gx_polygons(count);
 if(count || ntr::gx_state_hash()!=state)return 9;
 draw();triangles=ntr::gx_polygons(count);
 if(count!=1 || std::memcmp(&first,triangles,sizeof(first)))return 10;
 std::puts("smoke_menu_gxstat: PASS SDK reset, readonly FIFO fields, IRQ controls byte/halfword writes and frame output reset preserving geometry state");
 return 0;
}
