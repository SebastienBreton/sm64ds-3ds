#define Player Sm64dsNitroPlayer
#define Track Sm64dsNitroTrack
#define Channel Sm64dsNitroChannel
#include "third_party/sseqplayer/Player.h"
#include <cstdio>
#include <stdexcept>
#ifdef __3DS__
#include "ntr/mmio.h"
extern "C" bool _ZN4Heap13SetupRootHeapEv();
#endif
static void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static unsigned active(const SoundPool& pool,const Player* owner){
 unsigned n=0;for(const auto& c:pool.channels)if(c.state!=CS_NONE&&c.ply==owner)++n;return n;
}
static int sample(const std::vector<uint8_t>& b,unsigned i){return int16_t(unsigned(b[i])|(unsigned(b[i+1])<<8));}
int main(){
#ifdef __3DS__
 if(!ntr::io_init()||!_ZN4Heap13SetupRootHeapEv())return 2;
#endif
 try{
 SBNK bank;bank.instruments.emplace_back();auto& inst=bank.instruments.back();inst.record=2;
 inst.ranges.emplace_back(0,127,2);auto& r=inst.ranges.back();
 r.swav=3;r.swar=0;r.noteNumber=60;r.attackRate=127;r.decayRate=127;r.sustainLevel=80;r.releaseRate=60;r.pan=64;
 SSEQ left,right,reserve;left.bank=right.bank=reserve.bank=&bank;
 left.data={0xc0,0,60,100,127,0xff};right.data={0xc0,127,67,100,127,0xff};
 reserve.data={0xfe,0xff,0xff,0x80,127,0xff};
 unsigned previousPeak=0;
 for(unsigned volume:{0U,32U,64U,110U,127U}){
  left.info.vol=volume;Player p;p.sampleRate=32768;require(p.Setup(&left),"volume setup");
  std::vector<uint8_t> pcm(32768*4);p.GenerateSamples(pcm,0,32768);
  unsigned peak=0,nonzero=0;for(unsigned i=0;i<pcm.size();i+=2){int v=sample(pcm,i);unsigned mag=v<0?-v:v;peak=std::max(peak,mag);nonzero+=v!=0;}
  require(volume?peak>previousPeak:peak==0,"metadata volume not applied monotonically");
  std::printf("sequence volume=%u peak=%u nonzero=%u\n",volume,peak,nonzero);previousPeak=peak;
 }
 // Explicit zero in an INFO entry must remain zero rather than becoming127.
 std::vector<uint8_t> metadata(12);PseudoFile input;input.data=&metadata;INFOEntrySEQ info;info.Read(input);require(info.vol==0,"INFO zero volume overridden");
 left.info.vol=127;Player p;p.sampleRate=32768;require(p.Setup(&left),"external volume setup");
 std::vector<uint8_t> pcm(1024*4);p.GenerateSamples(pcm,0,1024);
 p.SetExternalVolume(-32768);
 for(unsigned i=0;i<8;++i)p.GenerateSamples(pcm,0,1024);
 for(auto b:pcm)require(!b,"external attenuation not propagated to sounding notes");
 require(active(p.pool,&p)==1,"attenuation killed note");
 p.SetExternalVolume(0);p.GenerateSamples(pcm,0,1024);unsigned audible=0;for(auto b:pcm)audible+=b!=0;require(audible,"attenuation restore failed");
 std::puts("smoke_sseq_volume: PASS metadata levels, explicit zero and live attenuation restore");return 0;
 }catch(const std::exception& e){std::printf("smoke_sseq_volume: FAIL %s\n",e.what());return 3;}
}
