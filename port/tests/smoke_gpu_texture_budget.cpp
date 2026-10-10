#include "platform/3ds/viewer_3ds.h"
#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){printf("GPU budget FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static ntr::GxTriangle texture(unsigned revision,bool blue){
 auto* raw=static_cast<uint16_t*>(ntr::host_ptr(0x06800000));
 for(unsigned i=0;i<256*256;++i)raw[i]=blue?0xfc00:0x801f;
 raw[0]=0x8000|(revision&0x7fff);
 ntr::gx_teximage_param((7u<<26)|(5u<<20)|(5u<<23));
 ntr::gx_write_port(0x04000500,0);
 for(unsigned v:{0xf000f000u,0u,0xf0001000u,0u,0x10000000u,0u})ntr::gx_write_port(0x0400048c,v);
 ntr::gx_write_port(0x04000504,0);
 size_t count=0;const auto* tris=ntr::gx_polygons(count);
 if(!count || !tris[count-1].texture_version)std::abort();
 return tris[count-1];
}
static void quad(V3dsVert* v,float x0,float x1){
 const V3dsVert q[4]={{x0,-1,0,1,1,1,1,1,0,1},{x1,-1,0,1,1,1,1,1,1,1},
                     {x1,1,0,1,1,1,1,1,1,0},{x0,1,0,1,1,1,1,1,0,0}};
 const unsigned indices[]={0,1,2,0,2,3};for(unsigned i=0;i<6;++i)v[i]=q[indices[i]];
}
int main(){
 CHECK(ntr::io_init());auto* root=Heap::SetupRootHeap();CHECK(root);ntr::gx_reset();
 setenv("PLAYER_GPU_SHADOWS","1",1);setenv("SM64DS_FORCE_3D","0",1);v3ds_renderer_init();
 const auto available=root->VMaxAllocatableSize();CHECK(available>65536);
 void* pressure=root->Allocate(available-65536,4);CHECK(pressure);
 const auto game_free=root->VMemoryLeft();CHECK(game_free<70000);
 static ntr::Framebuffer left,right;V3dsVert vertices[33*6];V3dsBatch batches[33];
 size_t entries=0,bytes=0;uint64_t evictions=0;
 for(unsigned frame=0;frame<41;++frame){
  ntr::gx_begin_frame();const auto t=texture(frame==40?0:frame,frame%2);
  quad(vertices,-1,1);batches[0]={t.tex,256,256,0,0,6,false,0x001f00c0,t.texture_version};
  CHECK(v3ds_gpu_geometry(vertices,6,batches,1,&left.px[0][0],&right.px[0][0],1,false,false));
  const uint32_t expected=frame%2?0xff0000ff:0xffff0000;
  for(unsigned y=16;y<176;++y)for(unsigned x=16;x<240;++x)CHECK(left.px[y][x]==expected);
  v3ds_texture_cache_usage(entries,bytes,evictions);CHECK(bytes<=8*1024*1024);
  CHECK(root->VMemoryLeft()==game_free);
  if(frame%10==0)printf("GPU budget frame%u entries%u bytes%u evictions%llu\n",frame,unsigned(entries),unsigned(bytes),(unsigned long long)evictions);
 }
 CHECK(evictions>=9);
 // One frame exceeds the retention budget: pin every batch through rendering,
 // rather than freeing earlier textures while resolving later materials.
 ntr::gx_begin_frame();
 for(unsigned panel=0;panel<33;++panel){
  const auto t=texture(100+panel,panel%2);
  quad(vertices+panel*6,-1+2.f*panel/33,-1+2.f*(panel+1)/33);
  batches[panel]={t.tex,256,256,0,int(panel*6),6,false,0x001f00c0,t.texture_version};
 }
 setenv("SM64DS_FORCE_3D","0.5",1);
 CHECK(v3ds_gpu_geometry(vertices,33*6,batches,33,&left.px[0][0],&right.px[0][0],1,false,true));
 CHECK(v3ds_native_stereo());
 for(unsigned panel=0;panel<33;++panel){
  const unsigned x=unsigned((panel+.5f)*256/33);const uint32_t expected=panel%2?0xff0000ff:0xffff0000;
  CHECK(left.px[96][x]==expected && right.px[96][x]==expected);
 }
 v3ds_texture_cache_usage(entries,bytes,evictions);CHECK(entries==33 && bytes==33*256*256*4);
 CHECK(root->VMemoryLeft()==game_free);
 printf("GPU active working set bytes%u, all33 materials preserved\n",unsigned(bytes));
 setenv("SM64DS_FORCE_3D","0",1);
 ntr::gx_begin_frame();const auto t=texture(132,false);
 quad(vertices,-1,1);batches[0]={t.tex,256,256,0,0,6,false,0x001f00c0,t.texture_version};
 CHECK(v3ds_gpu_geometry(vertices,6,batches,1,&left.px[0][0],&right.px[0][0],1,false,false));
 CHECK(left.px[96][128]==0xffff0000);
 v3ds_texture_cache_usage(entries,bytes,evictions);CHECK(bytes<=8*1024*1024);
 CHECK(root->VMemoryLeft()==game_free);root->Deallocate(pressure);
 printf("GPU cache metadata preserved game heap free=%u\n",unsigned(game_free));
 printf("GPU budget recovered bytes%u evictions%llu\n",unsigned(bytes),(unsigned long long)evictions);
 puts("smoke_gpu_texture_budget: PASS");return 0;
}
