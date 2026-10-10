#include "ntr/gx.h"
#include "ntr/mmio.h"
#include "Heap.h"
#include <cstdio>
#include <cstring>
#define CHECK(c) do{if(!(c)){printf("renderer heap FAIL line%d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(){
 CHECK(ntr::io_init());auto* root=Heap::SetupRootHeap();CHECK(root);ntr::gx_reset();
 printf("renderer root baseline=%u\n",root->VMemoryLeft());
 const auto before=root->VMemoryLeft();
 const auto available=root->VMaxAllocatableSize();CHECK(available>65536);
 void* pressure=root->Allocate(available-65536,4);CHECK(pressure);
 const auto limited=root->VMemoryLeft();CHECK(limited<70000);
 auto vertex=[](int x,int y){ntr::gx_write_port(0x0400048c,uint16_t(x)|(unsigned(uint16_t(y))<<16));ntr::gx_write_port(0x0400048c,0);};
 static ntr::Framebuffer fb;
 for(unsigned frame=0;frame<40;++frame){
  ntr::gx_begin_frame();
  auto* texels=static_cast<uint16_t*>(ntr::host_ptr(0x06800000));
  for(unsigned i=0;i<256*256;++i)texels[i]=0x8000|frame;
  ntr::gx_teximage_param((7u<<26)|(5u<<20)|(5u<<23));
  ntr::gx_write_port(0x040004a4,0x001f00c0);
  ntr::gx_write_port(0x04000500,0);
  vertex(-4096,-4096);vertex(4096,-4096);vertex(0,4096);
  ntr::gx_write_port(0x04000504,0);
  size_t count=0;const auto* triangles=ntr::gx_polygons(count);CHECK(count==1);
  auto rejected=triangles[0];for(auto& v:rejected.v){v.cz=2;v.z=1.5f;}
  // Reserve more than the 224,808-byte allocation which failed after Stage
  // reload, without adding costly visible overdraw to this ownership test.
  for(unsigned i=0;i<1500;++i)ntr::gx_append_polygons(&rejected,1);
  std::memset(&fb,0,sizeof(fb));ntr::gx_render(fb);
  CHECK(fb.px[120][128]>>24);
  CHECK(root->VMemoryLeft()==limited);
  size_t entries=0,bytes=0;ntr::gx_texture_cache_usage(entries,bytes);
  CHECK(bytes<=8*1024*1024);
  if(frame%10==0||frame==39)printf("renderer frame=%u textures=%u decoded=%u game_free=%u\n",frame,unsigned(entries),unsigned(bytes),limited);
 }
 root->Deallocate(pressure);CHECK(root->VMemoryLeft()==before);
 puts("smoke_renderer_heap: PASS 40 texture revisions, bounded cache, clipping scratch and triangles outside exhausted game heap");return 0;
}
