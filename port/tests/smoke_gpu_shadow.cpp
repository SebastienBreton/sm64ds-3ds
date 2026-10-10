#include "platform/3ds/viewer_3ds.h"
#include "ntr/ppu.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>
static std::vector<V3dsVert> vertices;
static std::vector<V3dsBatch> batches;
static void quad(unsigned panel,float z,unsigned color,unsigned attr){
 const float x0=-1+(panel%4)*.5f,x1=x0+.5f,y1=1-(panel/4)*(2.f/3),y0=y1-2.f/3;
 const float r=((color>>16)&255)/255.f,g=((color>>8)&255)/255.f,b=(color&255)/255.f;
 V3dsVert q[4]={{x0,y0,z,1,r,g,b,1,0,0},{x1,y0,z,1,r,g,b,1,0,0},{x1,y1,z,1,r,g,b,1,0,0},{x0,y1,z,1,r,g,b,1,0,0}};
 unsigned first=vertices.size();for(unsigned k:{0u,1u,2u,0u,2u,3u})vertices.push_back(q[k]);
 batches.push_back({nullptr,0,0,0,int(first),6,false,attr});
}
static void mask(unsigned p,float z=.5f){quad(p,z,0,0x000f00f0);}
static void fill(unsigned p,unsigned attr=0x010f00f0){quad(p,-.5f,0,attr);}
int main(){
 if(!ntr::io_init()||!Heap::SetupRootHeap())return 2;
 setenv("PLAYER_GPU_SHADOWS","1",1);setenv("SM64DS_FORCE_3D","0",0);v3ds_renderer_init();
 for(unsigned p=0;p<12;++p)quad(p,0,0x0000ff,((p==4||p==9||p==11?1u:2u)<<24)|0x001f00c0);
 mask(0);fill(1);mask(2,-.75f);fill(2);mask(3);fill(3);mask(4);fill(4);
 mask(5);fill(5);fill(5);mask(6);fill(6);mask(6,-.75f);fill(6,0x030f00f0);
 mask(7);fill(7);quad(7,-.25f,0xff0000,0x041f00c0);
 mask(8);fill(8,0x010f08f0);quad(8,-.25f,0xff0000,0x041f00c0);
 quad(9,-.5f,0,0x010f00c0);
 quad(10,-.5f,0xff0000,0x010f00c0);quad(10,-.75f,0x00ff00,0x031f00c0);quad(10,-.875f,0,0x010f00c0);
 quad(11,-.25f,0xff0000,0x020f00c0);mask(11);fill(11);
 static ntr::Framebuffer left,right;
 for(unsigned frame=0;frame<3;++frame)if(!v3ds_gpu_geometry(vertices.data(),vertices.size(),batches.data(),batches.size(),&left.px[0][0],&right.px[0][0],1,false,true))return 3;
 const unsigned expected[]={0x0000ff,0x0000ff,0x0000ff,0x00007f,0x0000ff,0x00007f,0x00007f,0xff0000,0x00007f,0x00007f,0x007f00,0x40003f};
 bool pass=true;for(unsigned eye=0;eye<(v3ds_native_stereo()?2u:1u);++eye)for(unsigned panel=0;panel<12;++panel){
  unsigned got=(eye?right:left).px[32+(panel/4)*64][32+(panel%4)*64];bool ok=(got>>24)==255;
  for(unsigned shift:{0u,8u,16u})ok&=std::abs(int((got>>shift)&255)-int((expected[panel]>>shift)&255))<=1;
  printf("GPU shadow eye%u panel%u got%08x expectedff%06x %s\n",eye,panel,got,expected[panel],ok?"PASS":"FAIL");pass&=ok;
 }
 ntr::ppu_write_bmp("player-gpu-shadow-panels.bmp",left);
 puts(pass?"smoke_gpu_shadow: PASS 12 hardware mask/ID/depth cases":"smoke_gpu_shadow: FAIL");return pass?0:1;
}
