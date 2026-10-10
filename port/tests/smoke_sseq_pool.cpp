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
 // Sequence event variables0..15 are local;16..31 share the sound pool.
 {
  SoundPool pool;Player a(&pool),b(&pool);a.sampleRate=b.sampleRate=32768;
  SSEQ writer,reader;writer.bank=reader.bank=&bank;
  writer.data={0xb0,0,7,0,0xb0,16,42,0,0xb0,31,0xfe,0xff,0x80,127,0xff};
  reader.data={0xa1,0xb0,1,16,0xb1,16,1,0,0x80,127,0xff};
  require(a.Variable(0)==-1&&a.Variable(16)==-1&&b.Variable(31)==-1,"variable defaults");
  require(a.Setup(&writer)&&b.Setup(&reader),"variable setup");
  a.tracks[a.trackIds[0]].Run();b.tracks[b.trackIds[0]].Run();
  require(a.Variable(0)==7&&b.Variable(0)==-1&&b.Variable(1)==42,"local scope or FROMVAR");
  require(a.Variable(16)==43&&b.Variable(16)==43&&b.Variable(31)==-2,"global event sharing");
  a.prio=17;a.Stop(false);
  require(a.Variable(0)==7&&a.Variable(16)==43&&a.prio==17,"stop reset retained state");
  require(a.Setup(&left)&&a.Variable(0)==-1&&a.Variable(16)==43&&a.prio==64,"setup scope reset");
  {Player temporary(&pool);temporary.Variable(31)=123;}
  require(b.Variable(31)==123,"player destruction reset globals");
  Player standalone;require(standalone.Variable(16)==-1,"separate pools share globals");
  bool rejected=false;try{a.Variable(32)=0;}catch(const std::out_of_range&){rejected=true;}
  require(rejected,"out-of-range variable accepted");
  std::puts("sequence variables: local isolation, shared globals, FROMVAR, stop/setup lifetime PASS");
 }
 // The status counter counts due ticks before events, not rendered samples.
 {
  Player p;p.sampleRate=32768;require(p.Setup(&reserve),"tick fixture");
  p.Run();require(*p.tickCount==0&&p.tempoCount==120,"first accumulation");
  p.Run();require(*p.tickCount==0&&p.tempoCount==240,"second accumulation");
  p.Run();require(*p.tickCount==1&&p.tempoCount==120,"first event tick");
  p.tempoCount=600;p.SetPaused(true);p.Run();
  require(*p.tickCount==1&&p.tempoCount==600,"paused tick advanced");
  p.SetPaused(false);p.tempo=300;p.tempoRate=384;p.Run();
  require(*p.tickCount==3&&p.tempoCount==570,"multiple ticks or tempo ratio");
  p.Stop(false);p.Run();require(*p.tickCount==3&&p.tempoCount==570,"stopped tick advanced");
  SSEQ ending;ending.bank=&bank;ending.data={0xfe,0xff,0xff,0xff};
  require(p.Setup(&ending)&&*p.tickCount==0,"restart tick reset");
  *p.tickCount=0xfffffffe;p.tempoCount=720;p.Run();
  require(*p.tickCount==1&&!p.IsPlaying()&&p.nTracks==0,"END due count, overflow or reserved tracks");
  p.Run();require(*p.tickCount==1,"finished counter advanced");
  std::puts("sequence ticks: cadence, tempo ratio, pause/stop, END batching, wrap and reset PASS");
 }
 // The 32-track limit is global, while each player keeps 16 logical slots.
 {
  SoundPool pool;Player a(&pool),b(&pool),c(&pool);a.sampleRate=b.sampleRate=c.sampleRate=32768;
  require(a.Setup(&reserve)&&b.Setup(&reserve),"two 16-track players");
  require(a.nTracks==16&&b.nTracks==16&&!c.Setup(&reserve),"global track exhaustion");
  for(auto ai:a.trackIds)for(auto bi:b.trackIds)require(ai!=bi,"overlapping track ownership");
  a.Stop(true);require(c.Setup(&reserve)&&c.nTracks==16&&b.nTracks==16,"released global slots reusable");
 }
 // Player and track priorities are separate bytes; allocation compares their
 // full sum, while the hardware channel stores the low byte. Ties retain it.
 {
  SoundPool pool;Player a(&pool),b(&pool);a.sampleRate=b.sampleRate=32768;
  require(a.Setup(&left)&&b.Setup(&right),"priority setup");
  auto& ta=a.tracks[a.trackIds[0]];auto& tb=b.tracks[b.trackIds[0]];
  require(a.prio==64&&ta.prio==64,"native default priorities");
  a.prio=10;for(unsigned i=0;i<6;++i){int ch=ta.NoteOn(60,100,127);require(ch>=0&&pool.channels[ch].prio==74,"combined note priority");}
  b.prio=9;require(tb.NoteOn(67,100,127)<0,"lower priority stole occupied PSG channel");
  b.prio=20;int ch=tb.NoteOn(67,100,127);require(ch>=0&&pool.channels[ch].prio==84,"higher player priority not used at allocation");
  b.prio=255;require(tb.NoteOnTie(68,100)==ch&&pool.channels[ch].prio==84,"tie changed existing channel priority");
  tb.prio=255;ch=tb.NoteOn(69,100,127);require(ch>=0&&pool.channels[ch].prio==254,"sum truncated before allocation or not narrowed on storage");
 }
 // External controls combine with event-controlled values and reach an
 // already sounding channel, rather than waiting for the next note.
 {
  Player p;p.sampleRate=32768;require(p.Setup(&left),"track control setup");
  std::vector<uint8_t> pcm(1024*4);p.GenerateSamples(pcm,0,1024);
  auto& track=p.tracks[p.trackIds[0]];int channel=-1;
  for(unsigned i=0;i<16;++i)if(p.channels[i].state!=CS_NONE)channel=i;
  require(channel>=0,"track control sounding fixture");auto& ch=p.channels[channel];
  require(ch.reg.panning==0,"left-pan fixture");
  track.SetParameter(9,127);p.GenerateSamples(pcm,0,1024);
  require(ch.reg.panning==127,"external pan did not combine with sequence pan");
  const auto before=ch.reg.sampleIncrease;
  track.SetParameter(0xc,128);p.GenerateSamples(pcm,0,1024);
  require(ch.reg.sampleIncrease>before,"external tune did not affect live pitch");
  track.SetParameter(0x19,30);track.SetParameter(0x1a,7);p.GenerateSamples(pcm,0,1024);
  require(ch.modSpeed==30&&ch.modDepth==7,"live modulation not propagated");
  track.SetParameter(0x1a,0);track.SetParameter(0xa,0x8000);
  for(unsigned i=0;i<4;++i)p.GenerateSamples(pcm,0,1024);
  for(auto byte:pcm)require(!byte,"track attenuation failed to silence live note");
  track.SetParameter(0xa,0);p.GenerateSamples(pcm,0,1024);
  unsigned audible=0;for(auto byte:pcm)audible+=byte!=0;require(audible,"track attenuation restore failed");
  require(!Track::ParameterSupported(9,2)&&!Track::ParameterSupported(0xff,1),"unsupported raw field allowed");
 }
 // Mute advances event timing while suppressing allocation. Mode2 releases
 // existing notes without suppressing later ones (the ROM tests mode&1).
 {
  SSEQ loop;loop.bank=&bank;loop.data={0xb1,0,1,0,60,100,4,67,100,4,0x94,0,0,0};
  Player p;p.sampleRate=32768;require(p.Setup(&loop),"mute setup");
  std::vector<uint8_t> pcm(1024*4);p.GenerateSamples(pcm,0,1024);
  auto& track=p.tracks[p.trackIds[0]];int before=p.variables[0];
  require(active(p.pool,&p)>0,"mute sounding fixture");track.SetMute(1);
  for(unsigned i=0;i<16;++i)p.GenerateSamples(pcm,0,1024);
  require(track.muted&&p.variables[0]>before&&!active(p.pool,&p),"mute stopped event timing or allocated notes");
  for(auto byte:pcm)require(!byte,"muted output not silent");
  track.SetMute(0);for(unsigned i=0;i<4;++i)p.GenerateSamples(pcm,0,1024);
  require(active(p.pool,&p)>0,"unmute did not allow next notes");
  track.SetMute(2);require(!track.muted,"mode2 incorrectly latched mute");
  for(const auto& ch:p.channels)if(ch.trackId==track.trackId)require(ch.state==CS_NONE,"mode2 did not detach active notes");
  for(unsigned i=0;i<4;++i)p.GenerateSamples(pcm,0,1024);
  require(active(p.pool,&p)>0,"mode2 blocked future notes");
 }
 // Channel masks restrict future allocation, intersect instrument capabilities,
 // and do not evict a currently tied note.
 {
  Player p;p.sampleRate=32768;require(p.Setup(&left),"channel mask setup");
  auto& track=p.tracks[p.trackIds[0]];
  require(track.channelMask==0xffff&&!track.channelMaskExplicit,"default channel mask");
  track.SetChannelMask(0x1000400);require(track.channelMask==0x400&&track.channelMaskExplicit,"mask truncation/explicit flag");
  require(track.NoteOn(60,100,127)==10,"PSG did not use selected channel10");
  track.SetChannelMask(0);
  require(track.NoteOnTie(61,100)==10&&track.NoteOn(62,100,127)<0,"mask evicted tie or allowed new allocation");
  track.SetChannelMask(1);require(track.NoteOn(60,100,127)<0,"PSG used PCM-only channel0");
  require(p.ChannelAlloc(TYPE_PCM,200,8)==3,"PCM allocator ignored mask");
  require(p.ChannelAlloc(TYPE_NOISE,200,0x400)<0,"noise used PSG-only channel10");
  require(p.ChannelAlloc(TYPE_NOISE,200,0x4000)==14,"noise rejected channel14");
 }
 // A paused sequence retains its tracks/position/tempo while the hardware
 // clock and other players keep running. Existing notes release immediately.
 {
  SoundPool pool;Player a(&pool),b(&pool);a.sampleRate=b.sampleRate=32768;
  require(a.Setup(&left)&&b.Setup(&right),"pause setup");Player* players[]={&a,&b};
  std::vector<uint8_t> pcm(32768*4);pool.GenerateSamples(pcm,0,32768,32768,players,2);
  auto id=a.trackIds[0];const auto pos=a.tracks[id].pos;const auto wait=a.tracks[id].wait;
  const auto tempo=a.tempoCount;require(active(pool,&a)==1,"pause starts with sounding note");
  a.SetPaused(true);
  for(const auto& ch:pool.channels)if(ch.ply==&a&&ch.state!=CS_NONE)
   require(ch.state==CS_RELEASE&&ch.trackId==-1&&ch.releaseRate==65535,"pause forced fast release");
  for(unsigned i=0;i<5;++i)pool.GenerateSamples(pcm,0,32768,32768,players,2);
  require(a.paused&&a.trackIds[0]==id&&a.tracks[id].pos==pos&&a.tracks[id].wait==wait&&a.tempoCount==tempo,"pause advanced sequence state");
  require(!active(pool,&a)&&b.nTracks==0,"pause froze hardware or another player");
  a.SetPaused(false);pool.GenerateSamples(pcm,0,1024,32768,players,2);
  require(!a.paused&&a.trackIds[0]==id&&a.tracks[id].pos==pos&&a.tracks[id].wait<wait,"resume restarted instead of continuing");
 }
 const unsigned frames=32768,extra=4096;
 std::vector<uint8_t> l((frames+extra)*4),rbuf((frames+extra)*4),mixed(frames*4),continuation(extra*4);
 Player soloL,soloR;soloL.sampleRate=soloR.sampleRate=32768;
 require(soloL.Setup(&left)&&soloR.Setup(&right),"solo setup");
 soloL.GenerateSamples(l,0,frames+extra);soloR.GenerateSamples(rbuf,0,frames+extra);
 SoundPool pool;Player a(&pool),b(&pool);a.sampleRate=b.sampleRate=32768;
 require(a.Setup(&left)&&b.Setup(&right),"shared setup");Player* players[]={&a,&b};
 // Unequal render partitions must not change timing or double-advance channels.
 unsigned done=0;while(done<frames){unsigned n=std::min(317U,frames-done);pool.GenerateSamples(mixed,done*4,n,32768,players,2);done+=n;}
 unsigned nonzero=0;
 for(unsigned i=0;i<mixed.size();i+=2){int expected=sample(l,i)+sample(rbuf,i);expected=std::max(-32768,std::min(32767,expected));require(sample(mixed,i)==expected,"shared PCM differs from independent sum");nonzero+=expected!=0;}
 require(nonzero&&active(pool,&a)==1&&active(pool,&b)==1,"two audible independent owners");
 a.Stop(true);require(!active(pool,&a)&&active(pool,&b)==1,"hard stop crossed owner boundary");
 pool.GenerateSamples(continuation,0,extra,32768,players,2);
 for(unsigned i=0;i<continuation.size();i+=2)require(sample(continuation,i)==sample(rbuf,frames*4+i),"stopping one player reset shared clock");
 b.Stop(false);require(active(pool,&b)==1,"release tail missing");
 for(const auto& ch:pool.channels)if(ch.ply==&b&&ch.state!=CS_NONE)require(ch.trackId==-1&&ch.state==CS_RELEASE,"release tail retained track slot");
 // A new owner reuses freed tracks while old channels retain their old owner.
 {
  Player next(&pool);next.sampleRate=32768;require(next.Setup(&left),"restart during old tail");
  require(active(pool,&b)==1,"setup killed another owner's tail");Player* current[]={&next};
  pool.GenerateSamples(continuation,0,extra,32768,current,1);
  require(active(pool,&next)==1,"new sound after track reuse");
 }
 require(!active(pool,&a),"destruction revived stopped owner");
 b.Stop(true);
 // All 16 PCM channels are shared, and lower priority cannot steal any of them.
 for(unsigned i=0;i<16;++i){int id=a.ChannelAlloc(0,100);require(id>=0,"channel allocation");auto& ch=pool.channels[id];ch.prio=100;ch.state=CS_SUSTAIN;}
 require(b.ChannelAlloc(0,99)<0,"channel limit/priority bypassed");
 int stolen=b.ChannelAlloc(0,101);require(stolen>=0&&pool.channels[stolen].ply==&b,"channel ownership not transferred");
 pool.channels[stolen].prio=101;pool.channels[stolen].state=CS_SUSTAIN;
 a.Stop(true);require(active(pool,&b)==1,"stolen channel killed by former owner");b.Stop(true);
 bool rejected=false;try{a.GenerateSamples(continuation,0,1);}catch(const std::logic_error&){rejected=true;}require(rejected,"per-player renderer accepted shared pool");
 Player* duplicate[]={&a,&a};rejected=false;try{pool.GenerateSamples(continuation,0,1,32768,duplicate,2);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"duplicate clock advancement accepted");
 std::printf("smoke_sseq_pool: PASS shared PCM=%u nonzero, global 32 tracks/16 channels, owner stop/steal/tails, pause/resume, priorities, track controls/mute/channel masks and single clock\n",nonzero);return 0;
 }catch(const std::exception& e){std::printf("smoke_sseq_pool: FAIL %s\n",e.what());return 3;}
}
