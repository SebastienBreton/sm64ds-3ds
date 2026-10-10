// Regression: original water-grid emitters must deliver their typedef-based
// register writes to GX. Before adaptation both emitted zero native polygons.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#include <utility>
struct V {int x,y,z;};struct N {short x,y,z;};
extern "C" void func_ov007_020ca86c(V*,N*,int,int,int);
extern "C" void func_ov007_020caeac(V*,N*,int*,int*,int,int,int,int,int,int,int,int,int);
int main(){
 if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
 V vertices[9];N normals[9];int u[3]{},v[3]{};
 for(int y=0;y<3;y++)for(int x=0;x<3;x++){vertices[y*3+x]={(x-1)*3072,(y-1)*3072,0};normals[y*3+x]={0,0,4095};}
 for(int textured=0;textured<2;textured++){
  ntr::gx_reset();
  if(textured)func_ov007_020caeac(vertices,normals,u,v,2,2,0,0,4096,4096,0,0,0);
  else func_ov007_020ca86c(vertices,normals,2,2,0);
  size_t count;const auto*t=ntr::gx_polygons(count);
  if(count!=8){std::printf("grid mode%d lost polygons: %u\n",textured,(unsigned)count);return 2;}
  unsigned corners=0;
  for(size_t i=0;i<count;i++)for(const auto&p:t[i].v){
   if(p.w!=1 || p.x<32 || p.x>224 || p.y<24 || p.y>168)return 3;
   if(p.x==32 && p.y==24)corners|=1;if(p.x==224 && p.y==24)corners|=2;
   if(p.x==32 && p.y==168)corners|=4;if(p.x==224 && p.y==168)corners|=8;
  }
  if(corners!=15)return 4;
 }
 // POLYGON_ATTR light enables latch at BEGIN, as in the retail GX pipeline.
 ntr::gx_reset();
 auto write=[](unsigned command,unsigned value){ntr::gx_write_port(0x04000400+command*4,value);};
 write(0x30,0x7fff);write(0x31,0);write(0x32,0x200u<<20);write(0x33,31);
 for(unsigned mode=0;mode<3;mode++){
  ntr::gx_begin_frame();write(0x29,mode==2?0:1);write(0x40,0);
  if(mode==1)write(0x29,0); // pending mask must not affect current primitive
  write(0x21,0x1ffu<<20);
  write(0x23,0);write(0x23,0);write(0x23,4096);write(0x23,0);write(0x23,4096u<<16);write(0x23,0);
  size_t count;const auto*t=ntr::gx_polygons(count);if(count!=1)return 5;
  const unsigned red=(t[0].v[0].color>>16)&255;
  if(mode<2 ? red<250 : red!=0)return 6;
 }
 size_t count;auto face=ntr::gx_polygons(count)[0];
 for(auto&v:face.v)v.color=0xffffffff;
 for(unsigned reverse=0;reverse<2;reverse++){
  for(unsigned faces:{0u,0x40u,0x80u,0xc0u}){
   ntr::gx_begin_frame();face.polygon_attr=faces;ntr::gx_append_polygons(&face,1);
   static ntr::Framebuffer fb;std::memset(&fb,0,sizeof(fb));ntr::gx_render(fb);
   const bool expected=faces&(reverse?0x40:0x80);
   if((fb.px[64][160]!=0)!=expected)return 7;
  }
  std::swap(face.v[1],face.v[2]);
 }
 puts("PASS POLYGON_ATTR light mask/BEGIN latch and front/back/both/neither raster");
 puts("PASS original textured/untextured grid emitters: 8 triangles, all corners, positive W");return 0;
}
