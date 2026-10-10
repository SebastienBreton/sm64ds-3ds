#include "platform/3ds/viewer_3ds.h"
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
int main(){
 if(!ntr::io_init()||!Heap::SetupRootHeap())return 2;
 setenv("PLAYER_GPU_SHADOWS","1",1);setenv("SM64DS_FORCE_3D","0",1);v3ds_renderer_init();
 V3dsVert quad[6]={{-1,-1,0,1,1,1,1,1,0,1},{1,-1,0,1,1,1,1,1,1,1},{1,1,0,1,1,1,1,1,1,0},
                 {-1,-1,0,1,1,1,1,1,0,1},{1,1,0,1,1,1,1,1,1,0},{-1,1,0,1,1,1,1,1,0,0}};
 static uint32_t texture[128];static ntr::Framebuffer left,right;
 const uint32_t colors[]={0xffff0000,0xffff0000,0xff0000ff,0xff0000ff,0xffff0000,0xffff0000,0xffff0000,0xff00ff00,0xff0000ff,0xffff0000,0xff00ff00,0xffff0000,0xff00ff00};
 bool pass=true;
 for(unsigned frame=0;frame<13;++frame){
  for(auto& p:texture)p=colors[frame];
  const unsigned alpha=frame==5?15:31;const int width=frame>=7 && frame!=11?16:8;
  V3dsBatch batch{texture,width,8,0,0,6,frame==8||frame==9||frame>=11,(alpha<<16)|0xc0};
  if(!v3ds_gpu_geometry(quad,6,&batch,1,&left.px[0][0],&right.px[0][0],1,false,false))return 3;
  const uint32_t expected=(alpha==15?0x7b000000:0xff000000)|(colors[frame]&0xffffff);
  bool ok=true;for(unsigned y=16;y<176;++y)for(unsigned x=16;x<240;++x)ok&=left.px[y][x]==expected;
  printf("GPU texture reuse frame%u width%d alpha%u dynamic%u expected%08x %s\n",frame,width,alpha,unsigned(batch.dynamic),expected,ok?"PASS":"FAIL");pass&=ok;
 }
 // Exercise identities originating in the real VRAM cache, not fabricated IDs.
 ntr::gx_reset();uint64_t versions[8]{};
 for(unsigned frame=0;frame<8;++frame){
  ntr::gx_begin_frame();
  const int width=frame==2||frame==3||frame==6||frame==7?16:8;
  const unsigned alpha=frame==4?15:31;
  const uint16_t color=frame==1||frame==3||frame==7?0xfc00:0x801f;
  auto* raw=static_cast<uint16_t*>(ntr::host_ptr(0x06800000));
  for(int i=0;i<128;++i)raw[i]=color;
  ntr::gx_teximage_param((7u<<26)|(width==16?1u<<20:0));
  ntr::gx_write_port(0x04000500,0);
  for(unsigned v:{0xf000f000u,0u,0xf0001000u,0u,0x10000000u,0u})ntr::gx_write_port(0x0400048c,v);
  ntr::gx_write_port(0x04000504,0);
  size_t count=0;const auto* tris=ntr::gx_polygons(count);if(count!=1)return 4;
  const auto& t=tris[0];versions[frame]=t.texture_version;if(!t.texture_version)return 5;
  V3dsBatch batch{t.tex,t.tw,t.th,0,0,6,false,(alpha<<16)|0xc0,t.texture_version};
  if(!v3ds_gpu_geometry(quad,6,&batch,1,&left.px[0][0],&right.px[0][0],1,false,false))return 6;
  const uint32_t expected=(alpha==15?0x7b000000:0xff000000)|(color==0x801f?0xff0000:0xff);
  bool ok=true;for(unsigned y=16;y<176;++y)for(unsigned x=16;x<240;++x)ok&=left.px[y][x]==expected;
  printf("GPU immutable texture frame%u version%llu width%d alpha%u %s\n",frame,(unsigned long long)t.texture_version,width,alpha,ok?"PASS":"FAIL");pass&=ok;
 }
 pass&=versions[0]==versions[4] && versions[0]==versions[5] && versions[2]==versions[6] && versions[3]==versions[7];
 puts(pass?"smoke_gpu_texture_reuse: PASS":"smoke_gpu_texture_reuse: FAIL");return pass?0:1;
}
