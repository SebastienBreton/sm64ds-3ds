#define Player Sm64dsNitroPlayer
#define Track Sm64dsNitroTrack
#define Channel Sm64dsNitroChannel
#include "third_party/sseqplayer/Player.h"
#include "ntr/mmio.h"
#include <cstdio>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
static void tick(Player& p){p.tempoCount=240;p.Run();}
int main(){
 if(!ntr::io_init()||!_ZN4Heap13SetupRootHeapEv())return 2;
 Player p;p.sampleRate=32768;SSEQ seq;SBNK bank;seq.bank=&bank;
 // Main restarts track1 every tick. Child holds a long rest.
 seq.data={0xfe,3,0,0x93,1,14,0,0,0x80,1,0x94,3,0,0,0x80,127,0x94,14,0,0};
 if(!p.Setup(&seq)||p.nTracks!=2)return 3;
 const auto child=p.trackIds[1];
 for(unsigned i=0;i<1024;++i){tick(p);if(p.nTracks!=2||p.trackIds[1]!=child)return 4;}
 // Ended slots are removed and cannot be implicitly reallocated by OPEN_TRACK.
 seq.data[14]=0xff;if(!p.Setup(&seq))return 5;tick(p);
 if(p.nTracks!=1||p.trackIds[1]!=0xff)return 6;
 for(unsigned i=0;i<64;++i){tick(p);if(p.nTracks!=1||p.trackIds[1]!=0xff)return 7;}
 // No allocation header: opening an unreserved track does nothing.
 seq.data={0x93,15,8,0,0,0x80,1,0xff,0x80,1,0xff};
 if(!p.Setup(&seq)||p.nTracks!=1)return 8;tick(p);if(p.nTracks!=1)return 9;
 // Sparse/high-bit masks stay inside the sixteen logical track slots.
 for(unsigned mask:{0U,2U,0x8000U,0xffffU}){
  seq.data={0xfe,(uint8_t)mask,(uint8_t)(mask>>8),0x80,1,0xff};
  if(!p.Setup(&seq))return 10;
  unsigned count=1;
  for(unsigned i=1;i<16;++i){if(mask&(1U<<i)){++count;if(p.trackIds[i]==0xff||p.tracks[p.trackIds[i]].num!=i||p.tracks[p.trackIds[i]].pos)return 11;}else if(p.trackIds[i]!=0xff)return 12;}
  if(p.nTracks!=count)return 13;
 }
 // A release tail is detached before its track storage can be reused.
 auto& t=p.tracks[p.trackIds[0]];p.channels[0].trackId=t.trackId;p.channels[0].state=CS_SUSTAIN;t.ReleaseAllNotes();
 if(p.channels[0].trackId!=-1||p.channels[0].state!=CS_RELEASE)return 14;
 p.Stop(true);if(p.channels[0].state!=CS_NONE||p.nTracks)return 15;
 std::puts("smoke_sseq_tracks: PASS 1024 restarts, ended/unreserved slots, sparse masks and detached tails");return 0;
}
