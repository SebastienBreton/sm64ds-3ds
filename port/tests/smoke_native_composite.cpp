#include "ntr/mmio.h"
#include "ntr/ppu.h"
#include "Heap.h"
#include "platform/3ds/viewer_3ds.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do{if(!(c)){printf("native composite FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
 CHECK(ntr::io_init()&&Heap::SetupRootHeap());
 setenv("SM64DS_FORCE_3D","0",1);
 v3ds_renderer_init();
 // Actual DS letter outline, with flat red replacing its texture.
 const int x[4]={4,135,136,4},y[4]={187,188,18,18};
 V3dsVert corners[4],vertices[6];
 for(int i=0;i<4;++i)corners[i]={x[i]/128.f-1.f,1.f-y[i]/96.f,0.f,1.f,1.f,0.f,0.f,1.f,0.f,0.f};
 const int indices[6]={0,1,2,0,2,3};for(int i=0;i<6;++i)vertices[i]=corners[indices[i]];
 const V3dsBatch batch{nullptr,0,0,0,0,6,false,0x001f0080};
 static ntr::Framebuffer left,right,upper,lower;
 ntr::io_write(0x04000000,0x10108,4);ntr::io_write(0x04001000,0x10000,4);
 *static_cast<uint16_t*>(ntr::host_ptr(0x05000000))=0x7c00;
 *static_cast<uint16_t*>(ntr::host_ptr(0x05000400))=0x03e0;
 auto hidden=batch;hidden.polygon_attr=0x001f0040;
 CHECK(v3ds_gpu_geometry(vertices,6,&hidden,1,&left.px[0][0],&right.px[0][0],1,false,false));
 for(const auto& row:left.px)for(auto pixel:row)CHECK(pixel==0);
 for(int frame=0;frame<3;++frame){
  CHECK(v3ds_gpu_geometry(vertices,6,&batch,1,&left.px[0][0],&right.px[0][0],1,false,false));
  CHECK(ntr::ppu_write_bmp("player-native-geometry.bmp",left));
  int minx=256,maxx=-1,miny=192,maxy=-1;unsigned visible=0;
  for(int py=0;py<192;++py)for(int px=0;px<256;++px)if(left.px[py][px]>>24){++visible;if(px<minx)minx=px;if(px>maxx)maxx=px;if(py<miny)miny=py;if(py>maxy)maxy=py;}
  printf("native geometry visible=%u bbox=%d,%d..%d,%d centre=%08lx\n",visible,minx,miny,maxx,maxy,(unsigned long)left.px[80][80]);
  for(int row=18;row<187;++row){
   int first=256,last=-1;for(int col=0;col<256;++col)if(left.px[row][col]>>24){if(col<first)first=col;last=col;}
   if(first!=4||last!=135)printf("native outline row=%d first=%d last=%d\n",row,first,last);
   CHECK(first==4&&last==135);
  }
  CHECK(left.px[80][80]==0xffff0000);CHECK(left.px[0][0]==0);
  ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_A,upper,left);
  ntr::ppu_scanout_scene_with_geometry(ntr::ENGINE_B,lower,left);
  printf("native composition upper centre=%08lx backdrop=%08lx lower=%08lx\n",(unsigned long)upper.px[80][80],(unsigned long)upper.px[0][0],(unsigned long)lower.px[80][80]);
  // The DS 2D palette expands 5-bit31 through 6-bit62, yielding byte251.
  CHECK(upper.px[80][80]==0xffff0000);CHECK(upper.px[0][0]==0xff0000fb);
  CHECK(lower.px[80][80]==0xff00fb00);
  v3ds_present_native(&upper.px[0][0],&upper.px[0][0],&lower.px[0][0],false);
 }
 static uint32_t physical[400*240];v3ds_read_top(physical,false);
 CHECK(physical[100*400+100]==0xffff0000);CHECK(physical[0]==0xff0000fb);
 CHECK(ntr::ppu_write_bmp("player-native-geometry.bmp",left));
 CHECK(ntr::ppu_write_bmp("player-native-upper.bmp",upper));
 // Per-pixel colour pattern detects any flipped axis or nearest-sample shift.
 for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x){
  upper.px[y][x]=0xff000000u|((x*17&255)<<16)|((y*13&255)<<8)|((x^y)&255);
  lower.px[y][x]=upper.px[y][x]^0x00ffffffu;
 }
 for(unsigned frame=0;frame<3;++frame)v3ds_present_native(&upper.px[0][0],&upper.px[0][0],&lower.px[0][0],false);
 v3ds_read_top(physical,false);static uint32_t physical_bottom[320*240];v3ds_read_bottom(physical_bottom);
 unsigned bad_top=0,bad_bottom=0;
 for(unsigned y=0;y<240;++y){
  for(unsigned x=0;x<400;++x)if(physical[y*400+x]!=upper.px[y*192/240][x*256/400]){
   if(bad_top<3)printf("top sampling %u,%u got%08lx expected%08lx\n",x,y,(unsigned long)physical[y*400+x],(unsigned long)upper.px[y*192/240][x*256/400]);++bad_top;
  }
  for(unsigned x=0;x<320;++x)bad_bottom+=physical_bottom[y*320+x]!=lower.px[y*192/240][x*256/320];
 }
 printf("fullscreen sampling mismatches: upper=%u lower=%u\n",bad_top,bad_bottom);CHECK(!bad_top&&!bad_bottom);
 setenv("SM64DS_FORCE_3D","1",1);
 CHECK(v3ds_gpu_geometry(vertices,6,&batch,1,&left.px[0][0],&right.px[0][0],1,false,true));
 CHECK(v3ds_native_stereo());
 for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)right.px[y][x]=upper.px[y][x]^0x0000ffffu;
 for(unsigned frame=0;frame<3;++frame)v3ds_present_native(&upper.px[0][0],&right.px[0][0],&lower.px[0][0],true);
 v3ds_read_top(physical,true);unsigned bad_right=0;
 for(unsigned y=0;y<240;++y)for(unsigned x=0;x<400;++x)bad_right+=physical[y*400+x]!=right.px[y*192/240][x*256/400];
 printf("fullscreen stereo right mismatches: %u\n",bad_right);CHECK(!bad_right);
 puts("smoke_native_composite: PASS native letter coverage, transparent holes, PPU priority, colour order and fullscreen transfer");
 return 0;
}
