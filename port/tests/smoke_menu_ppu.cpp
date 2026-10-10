#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define CHECK(c) do{if(!(c)){std::printf("FAIL PPU line=%d\n",__LINE__);return 1;}}while(0)
static void reg(unsigned a,unsigned v,unsigned size=2){ntr::io_write(a,v,size);}
static void word(unsigned a,unsigned short v){*static_cast<unsigned short*>(ntr::host_ptr(a))=v;}
int main(){
 CHECK(ntr::io_init());CHECK(Heap::SetupRootHeap());
 static ntr::Framebuffer fb;
 std::memset(ntr::host_ptr(0x06200000),0,0x20000);
 std::memset(ntr::host_ptr(0x06600000),0,0x20000);
 std::memset(ntr::host_ptr(0x05000400),0,0x400);
 word(0x05000400,0x7c00);word(0x05000402,0x001f);
 std::memset(ntr::host_ptr(0x06214000),1,64);
 reg(0x04001000,0x10100,4);reg(0x04001008,(5<<2)|(31<<8)|0x80|2);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[40][40]==0xfffb0000u);
 reg(0x04001050,0xc1);reg(0x04001054,16);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[40][40]==0xff000000u);
 reg(0x04001050,0x81);reg(0x04001054,8);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[40][40]==0xffff8282u);
 reg(0x04001050,0);reg(0x0400106c,0x8010);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[40][40]==0xff000000u);
 reg(0x0400106c,0);
 for(unsigned i=0;i<128;++i)word(0x07000400+i*8,0x200);
 word(0x07000400,0);word(0x07000402,1<<14);word(0x07000404,3<<10);
 word(0x05000602,0x03e0);
 std::memset(ntr::host_ptr(0x06600000),0x11,64);
 std::memset(ntr::host_ptr(0x06600400),0x11,64);
 reg(0x04001000,0x11100,4);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[0][0]==0xfffb0000u);
 word(0x07000404,1<<10);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[0][0]==0xff00fb00u && fb.px[8][0]==0xff00fb00u);
 reg(0x04001000,0x10100,4);
 ntr::ppu_scanout_scene(ntr::ENGINE_B,fb);CHECK(fb.px[0][0]==0xfffb0000u);
 // 3D BG0 must replace its tile interpretation and obey BG priorities.
 std::memset(ntr::host_ptr(0x06000000),0,0x20000);
 word(0x05000000,0x7c00);word(0x05000002,0x03e0);
 std::memset(ntr::host_ptr(0x06004000),1,64);
 reg(0x04000000,0x10108,4);reg(0x04000008,2);
 ntr::gx_reset();ntr::ppu_scanout_scene(ntr::ENGINE_A,fb);CHECK(fb.px[96][128]==0xff0000fbu);
 ntr::gx_write_port(0x04000480,0x1f);ntr::gx_write_port(0x04000500,0);
 for(unsigned v:{0xf000f000u,0u,0xf0001000u,0u,0x10000000u,0u})ntr::gx_write_port(0x0400048c,v);
 ntr::gx_write_port(0x04000504,0);
 reg(0x04000000,0x10308,4);reg(0x0400000a,(1<<2)|(31<<8)|0x80);
 ntr::ppu_scanout_scene(ntr::ENGINE_A,fb);CHECK(fb.px[96][128]==0xff00fb00u);
 reg(0x0400000a,(1<<2)|(31<<8)|0x80|3);
 ntr::ppu_scanout_scene(ntr::ENGINE_A,fb);CHECK(fb.px[96][128]==0xffff0000u);
 std::puts("smoke_menu_ppu: PASS DS 4-bit charbase, layer brightness, master brightness, OBJ enable/2D mapping/priority, 3D BG0 priority");return 0;
}
