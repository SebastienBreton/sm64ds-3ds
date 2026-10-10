// Isolated actual-PICA probe. Golden physical bytes are generated from the
// original DS geometry engine, software3D renderer and display-capture renderer.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "ntr/vram_bus.h"
#include "ntr/display_capture.h"
#include "viewer_3ds.h"
#include "cpu_mode.h"
#include "fixtures.h"
#include "capture_golden.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
bool player_present(bool,bool);
namespace ntr {void rt_dispatch_pending_irqs(){}}
static unsigned differing_bmp_bytes(const char* left,const char* right){
 FILE* a=std::fopen(left,"rb");FILE* b=std::fopen(right,"rb");if(!a||!b){std::puts("FAIL missing native diagnostic image");return 0;}
 std::fseek(a,54,SEEK_SET);std::fseek(b,54,SEEK_SET);unsigned errors=0;unsigned char aa[768],bb[768];
 for(unsigned row=0;row<192;++row){if(std::fread(aa,1,sizeof aa,a)!=sizeof aa||std::fread(bb,1,sizeof bb,b)!=sizeof bb){errors=0;break;}for(unsigned i=0;i<sizeof aa;++i)errors+=aa[i]!=bb[i];}
 std::fclose(a);std::fclose(b);return errors;
}
static bool check_banks(unsigned index,ntr::VramBanks& banks){
 const auto record=capture_golden_cases[index];unsigned position=0,errors=0;
 for(unsigned run=record.first;run<record.first+record.runs;++run)for(unsigned i=0;i<capture_golden_runs[run].count;++i){
  const unsigned bank=position/65536,word=position%65536;const auto* ptr=banks.physical(bank)+word*2;
  const unsigned actual=ptr[0]|(unsigned(ptr[1])<<8),expected=capture_golden_runs[run].value;
  if(actual!=expected){if(errors<4)std::printf("Capture mismatch fixture%u bank%u byte%x got%04x expected%04x\n",index,bank,word*2,actual,expected);++errors;}++position;
 }
 std::printf("Native capture fixture%u words=%u bank-byte-mismatches=%u\n",index,position,errors);
 return position==4*65536 && !errors;
}
int main(){
 gfxInitDefault();mkdir("sdmc:/sm64ds",0777);chdir("sdmc:/sm64ds");std::freopen("log.txt","w",stdout);std::setvbuf(stdout,nullptr,_IONBF,0);
 v3ds_auto();sm64ds_configure_cpu_mode();setenv("PLAYER_PHYSICAL_VRAM","1",1);setenv("PLAYER_NATIVE_COMPOSITE","1",1);setenv("SM64DS_FORCE_3D","1",1);
 if(!ntr::io_init())return 3;v3ds_renderer_init();auto* banks=ntr::io_vram_banks();bool pass=true;unsigned cases=0;
 for(unsigned prepared=0;prepared<2;++prepared)for(unsigned index=0;index<sizeof(capture_fixtures)/sizeof(capture_fixtures[0]);++index){
  if(prepared)setenv("PLAYER_PREPARED_PPU","1",1);else unsetenv("PLAYER_PREPARED_PPU");
  ntr::io_display_capture()->reset();ntr::gx_reset();capture_geometry_commands([](unsigned a,unsigned v){ntr::gx_write_port(a,v);});
  for(unsigned bank=0;bank<9;++bank){std::memset(banks->physical(bank),0,banks->bank_size(bank));banks->changed(bank,0,banks->bank_size(bank));}
  auto f=capture_fixtures[index];for(unsigned bank=0;bank<4;++bank)ntr::io_write(0x04000240+bank,f.unmapped&&bank==3?0x81:0x80,1);
  for(unsigned sprite=0;sprite<128;++sprite){ntr::io_write(0x07000000+sprite*8,0x200,2);ntr::io_write(0x07000400+sprite*8,0x200,2);}
  ntr::io_write(0x04000304,0x820f,2);ntr::io_write(0x04000000,f.display|0x108,4);ntr::io_write(0x04001000,0x10000,4);
  ntr::io_write(0x05000000,0x4210,2);ntr::io_write(0x04000050,0,2);ntr::io_write(0x0400006c,f.master,2);ntr::io_write(0x0400106c,0,2);ntr::io_write(0x04000064,f.control,4);
  size_t polys=0;ntr::gx_polygons(polys);if(polys!=6){std::printf("FAIL native geometry polygons=%u\n",unsigned(polys));return 4;}
  const auto need=ntr::io_display_capture()->sources(0,false,banks->lcdc_mask());
  const unsigned before=v3ds_gpu_geometry_calls();if(!player_present(true,true))return 5;const unsigned requests=v3ds_gpu_geometry_calls()-before;
  const unsigned expectedRequests=1+unsigned(need.geometry||need.composite);
  pass&=requests==expectedRequests;pass&=check_banks(index,*banks);
  pass&=ntr::io_read(0x04000064,4)==((f.control&0xef3f1f1f)&~0x80000000u);pass&=!ntr::io_display_capture()->latched();
  std::printf("Native centre policy fixture%u prepared%u GPUgeometry calls=%u expected%u stereo=%u\n",index,prepared,requests,expectedRequests,unsigned(v3ds_native_stereo()));
  if(need.geometry||need.composite){
   const unsigned centreLeft=differing_bmp_bytes("player-native-capture-centre.bmp","player-native-geometry.bmp");
   const unsigned centreRight=differing_bmp_bytes("player-native-capture-centre.bmp","player-native-geometry-right.bmp");
   const unsigned eyes=differing_bmp_bytes("player-native-geometry.bmp","player-native-geometry-right.bmp");
   std::printf("Native centre/eyes distinct bytes C-L=%u C-R=%u L-R=%u\n",centreLeft,centreRight,eyes);pass&=centreLeft&&centreRight&&eyes;
  }
  pass&=v3ds_native_stereo();++cases;
 }
 v3ds_exit();std::printf("%s native DS-centre display capture %u cases: four complete banks + centre/L/R + request policy\n",pass?"PASS":"FAIL",cases);std::puts(pass?"PASS":"FAIL");return pass?0:1;
}
