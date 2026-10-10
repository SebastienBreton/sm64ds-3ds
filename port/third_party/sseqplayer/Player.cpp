#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <cmath>
/*
 * SSEQ Player - Player structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-10-23
 *
 * Adapted from source code of FeOS Sound System
 * By fincs
 * https://github.com/fincs/FSS
 */

#include "Player.h"
#include "common.h"
#include <stdexcept>
#include "../../audio/sample_clock.h"
static Sm64dsSampleClock MixerSampleClock(unsigned rate){return IsNativeRate(rate)?Sm64dsSampleClock(1024,2):Sm64dsSampleClock(rate);}

#ifdef SM64DS_AUDIO_PROFILE
// Diagnostic-only phase timers (ARM11 system ticks). Never in production.
extern "C" unsigned long long svcGetSystemTick(void);
namespace audio_profile { unsigned long long phase[6]; }
extern "C" void sm64ds_audio_profile(unsigned long long out[6]){for(int i=0;i<6;++i)out[i]=audio_profile::phase[i];}
#define SM64DS_PROF_BEGIN() unsigned long long prof_t=svcGetSystemTick()
#define SM64DS_PROF_MARK(i) do{const unsigned long long n=svcGetSystemTick();audio_profile::phase[i]+=n-prof_t;prof_t=n;}while(0)
#else
#define SM64DS_PROF_BEGIN() ((void)0)
#define SM64DS_PROF_MARK(i) ((void)0)
#endif
#ifdef SM64DS_AUDIO_TRACE
// Diagnostic-only register trace (DS SOUNDxCNT/TMR/SAD/PNT/LEN equivalents),
// sampled after every sequencer tick. Never compiled into production.
#include <cstdio>
namespace audio_trace {
struct Row{uint32_t sample,cnt,timer,src,loop,len;uint8_t ch,state;};
static Row rows[100000];static size_t count=0;const size_t capacity=100000;static uint32_t previous[16][6];
static void tick(const SoundPool& pool,double clock,unsigned rate){
 const uint32_t sample=IsNativeRate(rate)?uint32_t(clock/1024.0):uint32_t(clock*rate);
 for(unsigned i=0;i<16;++i){
  const auto& c=pool.channels[i];const auto& r=c.reg;
  const uint32_t cnt=r.volumeMul|(r.volumeDiv<<8)|(r.panning<<16)|(r.waveDuty<<24)|(r.repeatMode<<27)|(r.format<<29)|(uint32_t(c.state!=CS_NONE)<<31);
  const uint32_t cur[6]={cnt,r.timer,uint32_t(reinterpret_cast<uintptr_t>(r.source)),r.loopStart,r.length,c.state};
  if(!std::memcmp(cur,previous[i],sizeof cur))continue;
  std::memcpy(previous[i],cur,sizeof cur);
  if(count<capacity)rows[count++]={sample,cur[0],cur[1],cur[2],cur[3],cur[4],uint8_t(i),uint8_t(c.state)};
 }
}
}
extern "C" void sm64ds_audio_trace_write(const char* path){
 FILE* f=std::fopen(path,"w");if(!f)return;
 std::fprintf(f,"sample,ch,cnt,timer,src,loop,len,state\n");
 for(size_t i=0;i<audio_trace::count;++i){const auto& r=audio_trace::rows[i];std::fprintf(f,"%u,%u,%08x,%04x,%08x,%u,%u,%u\n",unsigned(r.sample),unsigned(r.ch),unsigned(r.cnt),unsigned(r.timer),unsigned(r.src),unsigned(r.loop),unsigned(r.len),unsigned(r.state));}
 std::fclose(f);
}
#define SM64DS_TRACE_TICK(pool,clock,rate) audio_trace::tick(pool,clock,rate)
namespace audio_trace { static int16_t mix[2*32768*20];static size_t mixCount=0; }
extern "C" void sm64ds_audio_trace_mix_write(const char* path){FILE* f=std::fopen(path,"wb");if(f){std::fwrite(audio_trace::mix,4,audio_trace::mixCount,f);std::fclose(f);}}
#define SM64DS_TRACE_MIX(l,r) do{if(audio_trace::mixCount<32768*20){audio_trace::mix[2*audio_trace::mixCount]=int16_t(l);audio_trace::mix[2*audio_trace::mixCount+1]=int16_t(r);++audio_trace::mixCount;}}while(0)
#else
#define SM64DS_TRACE_MIX(l,r) ((void)0)
#define SM64DS_TRACE_TICK(pool,clock,rate) ((void)0)
#endif

// Modern standard library supplies codecvt facet IDs.


SoundPool::SoundPool() : secondsUntilNextClock(SecondsPerClockCycle)
{
 globalVariables.fill(-1);
 for(unsigned i=0;i<16;++i)channels[i].chnId=i;
}

Player::Player(SoundPool* shared) : ownedPool(shared ? nullptr : std::make_unique<SoundPool>()),
 pool(shared ? *shared : *ownedPool), prio(64), nTracks(0), tempo(0), tempoCount(0), tempoRate(0),
 masterVol(0), sseqVol(0), sseq(nullptr), tracks(pool.tracks), channels(pool.channels),
 sampleRate(0), interpolation(INTERPOLATION_NONE)
{
 memset(this->trackIds, 0xff, sizeof(this->trackIds));
 if(ownedPool)for(auto& channel:channels)channel.ply=this;
 memset(this->variables, -1, sizeof(this->localVariables));
}
Player::~Player(){Stop(true);}

int16_t& Player::Variable(unsigned index)
{
 if(index>=32)throw std::out_of_range("SSEQ variable index");
 return index<16 ? variables[index] : pool.globalVariables[index-16];
}

// Original FSS Function: Player_Setup
bool Player::Setup(const SSEQ *sseqToPlay, unsigned entryOffset)
{
	if (!sseqToPlay || entryOffset >= sseqToPlay->data.size() || sseqToPlay->info.vol>127)return false;
	this->Stop(true);
	this->sseq = sseqToPlay;

	int firstTrack = this->TrackAlloc();
	if (firstTrack == -1)
		return false;
	this->tracks[firstTrack].Init(firstTrack, this, nullptr, 0);

	this->nTracks = 1;
	this->trackIds[0] = firstTrack;

	unsigned mask=1;
	if (this->sseq->data[entryOffset]==0xfe) {
		if(this->sseq->data.size()-entryOffset<=3){this->FreeTracks();return false;}
		mask=this->sseq->data[entryOffset+1]|(unsigned(this->sseq->data[entryOffset+2])<<8);
		entryOffset+=3;
	}
	this->tracks[firstTrack].startPos = this->tracks[firstTrack].pos = this->sseq->data.data()+entryOffset;
	for(unsigned logical=1;logical<16;++logical)if(mask&(1U<<logical)) {
		int id=this->TrackAlloc();if(id<0)break;
		this->tracks[id].Init(id,this,nullptr,logical);
		this->trackIds[logical]=id;++this->nTracks;
	}


	this->ClearState();
    this->sseqVol=Cnv_Sust(sseqToPlay->info.vol);

	return true;
}

// Original FSS Function: Player_ClearState
void Player::ClearState()
{
 this->paused=false;
 *this->tickCount=0;
 this->prio=64;
	this->tempo = 120;
	this->tempoCount = 0;
	this->tempoRate = 0x100;
	this->masterVol = 0; // this is actually the highest level
	memset(this->variables, -1, sizeof(this->localVariables));
 if(ownedPool){pool.secondsIntoPlayback=0;pool.secondsUntilNextClock=ClockPeriod(sampleRate);}
}

// Original FSS Function: Player_FreeTracks
void Player::FreeTracks()
{
	for (uint8_t i = 0; i < FSS_TRACKCOUNT; ++i)
		if(this->trackIds[i]!=0xff){this->tracks[this->trackIds[i]].Free();this->trackIds[i]=0xff;}
	this->nTracks = 0;
}

// Original FSS Function: Player_Stop
void Player::Stop(bool bKillSound)
{
 // DS stop frees tracks but preserves player parameters and shared status.
	for (uint8_t i = 0; i < FSS_TRACKCOUNT; ++i)
	{
		uint8_t trackId = this->trackIds[i];
		if(trackId==0xff)continue;
		this->tracks[trackId].ClearState();
		for (int j = 0; j < 16; ++j)
		{
			Channel &chn = this->channels[j];
			if (chn.state != CS_NONE && chn.ply == this && chn.trackId == trackId)
			{
				if (bKillSound)
					chn.Kill();
				else {chn.Release();chn.trackId=-1;}
			}
		}
	}
	this->FreeTracks();
	if(bKillSound)for(auto& channel:this->channels)if(channel.ply==this)channel.Kill();
}

void Player::SetPaused(bool value)
{
 this->paused=value;
 if(value)for(auto id:trackIds)if(id!=0xff)tracks[id].ReleaseAllNotes(127);
}

// Original FSS Function: Chn_Alloc
int Player::ChannelAlloc(int type, int priority, unsigned channelMask,bool explicitMask)
{
    channelMask &= ~pool.lockedChannels;
    if(!explicitMask)channelMask &= ~pool.weakLockedChannels;
	static const uint8_t pcmChnArray[] = { 4, 5, 6, 7, 2, 0, 3, 1, 8, 9, 10, 11, 14, 12, 15, 13 };
	static const uint8_t psgChnArray[] = { 8, 9, 10, 11, 12, 13 };
	static const uint8_t noiseChnArray[] = { 14, 15 };
	static const uint8_t arraySizes[] = { sizeof(pcmChnArray), sizeof(psgChnArray), sizeof(noiseChnArray) };
	static const uint8_t *const arrayArray[] = { pcmChnArray, psgChnArray, noiseChnArray };

	auto chnArray = arrayArray[type];
	int arraySize = arraySizes[type];

	int curChnNo = -1;
	for (int i = 0; i < arraySize; ++i)
	{
		int thisChnNo = chnArray[i];
        if(!(channelMask&(1U<<thisChnNo)))continue;
		Channel &thisChn = this->channels[thisChnNo];
		if (curChnNo != -1)
		{
			Channel &curChn = this->channels[curChnNo];
			if (thisChn.prio >= curChn.prio) {
				if (thisChn.prio != curChn.prio)
					continue;
				if (curChn.vol <= thisChn.vol)
					continue;
			}
		}
		curChnNo = thisChnNo;
	}

	if (curChnNo == -1 || priority < this->channels[curChnNo].prio)
		return -1;
    pool.rawPcm[curChnNo].stop();
	this->channels[curChnNo].ply = this;
	this->channels[curChnNo].noteLength = -1;
	this->channels[curChnNo].vol = 0x7FF;
	this->channels[curChnNo].clearHistory();
	return curChnNo;
}

// Original FSS Function: Track_Alloc
int Player::TrackAlloc()
{
	for (int i = 0; i < FSS_MAXTRACKS; ++i)
	{
		Track &thisTrk = this->tracks[i];
		if (!thisTrk.state[TS_ALLOCBIT])
		{
			thisTrk.Zero();
			thisTrk.state.set(TS_ALLOCBIT);
			thisTrk.updateFlags.reset();
			return i;
		}
	}
	return -1;
}

// Original FSS Function: Player_Run
bool Player::IsPlaying() const
{
 for(auto id:trackIds)if(id!=0xff&&tracks[id].pos&&!tracks[id].state[TS_END])return true;
 return false;
}

void Player::Run()
{
 if(this->paused||!this->IsPlaying())return;
 // ARM7 publishes all ticks due before running events, even if END occurs
 // in the first tick. New tempo takes effect in the following accumulation.
 unsigned ticks=this->tempoCount/240;
 this->tempoCount%=240;
 *this->tickCount+=ticks;
 while(ticks-- && this->IsPlaying()) {
  for(uint8_t i=0;i<FSS_TRACKCOUNT;++i){
   const auto id=this->trackIds[i];
   if(id==0xff||!this->tracks[id].pos)continue;
   auto& track=this->tracks[id];track.Run();
   if(track.state[TS_END]){track.Free();this->trackIds[i]=0xff;--this->nTracks;}
  }
 }
 if(!this->IsPlaying())this->Stop(false);
 this->tempoCount+=(uint32_t(this->tempo)*uint32_t(this->tempoRate))>>8;
}

void Player::UpdateTracks()
{
 if(!ownedPool)throw std::logic_error("shared channels require the pool clock");
	for (int i = 0; i < 16; ++i)
		this->channels[i].UpdateTrack();
	for (int i = 0; i < FSS_MAXTRACKS; ++i)
		this->tracks[i].updateFlags.reset();
}

// Original FSS Function: Snd_Timer
void Player::Timer()
{
	this->UpdateTracks();

	for (int i = 0; i < 16; ++i)
		this->channels[i].Update();

	this->Run();
}

static inline int32_t muldiv7(int32_t val, uint8_t mul)
{
	return mul == 127 ? val : ((val * mul) >> 7);
}

// DS mixer arithmetic (melonDS SPUChannel::Run/PanOutput, SPU::Mix, GBATEK
// SOUNDxCNT): volume shift {4,3,2,0} by divider, 7-bit volume and pan where
// 127 counts as 128, pan as 128-pan/pan, summed at 2^-10 into 2^8 headroom.
// Capture units and speakers both take sum>>8 clamped to 16 bits (the speaker
// master volume is the SDK's fixed 127, i.e. 128).
static inline void DsPan(int32_t sample, unsigned volume, unsigned divider, unsigned pan, int32_t& left, int32_t& right)
{
	static const uint8_t shifts[4] = { 4, 3, 2, 0 };
	// (sample << shift) * volume, written without shifting a negative value.
	const int32_t value = sample * (static_cast<int32_t>(volume >= 127 ? 128 : volume) << shifts[divider & 3]);
	const int32_t p = pan >= 127 ? 128 : static_cast<int32_t>(pan);
	left = static_cast<int32_t>((static_cast<int64_t>(value) * (128 - p)) >> 10);
	right = static_cast<int32_t>((static_cast<int64_t>(value) * p) >> 10);
}
static inline int32_t DsCapture(int32_t sum) { sum >>= 8; return sum < -0x8000 ? -0x8000 : sum > 0x7FFF ? 0x7FFF : sum; }
static inline int32_t DsSpeaker(int32_t sum) { return DsCapture(sum); }

void Player::GenerateSamples(std::vector<uint8_t> &buf, unsigned offset, unsigned samples)
{
 if(!ownedPool)throw std::logic_error("shared channels require the pool renderer");
 Player* self=this;
 pool.GenerateSamples(buf,offset,samples,sampleRate,&self,1,mutes.to_ulong());
}

void SoundPool::GenerateSamplesLegacy(std::vector<uint8_t>& buf, unsigned offset, unsigned samples,
 unsigned rate, Player* const* players, unsigned count, unsigned mute)
{
 if(rate<8000 || rate>48000 || count>16 || (count && !players) || offset>buf.size() || samples>(buf.size()-offset)/4)
  throw std::invalid_argument("invalid shared sound render");
 for(unsigned i=0;i<count;++i){
  if(!players[i] || &players[i]->pool!=this || players[i]->sampleRate!=rate)
   throw std::invalid_argument("incompatible shared sound player");
  for(unsigned j=0;j<i;++j)if(players[j]==players[i])throw std::invalid_argument("duplicate shared sound player");
 }
 const double secondsPerSample=ClockStep(rate),clockPeriod=ClockPeriod(rate);
 const Sm64dsSampleClock sampleClock=MixerSampleClock(rate);

	for (unsigned smpl = 0; smpl < samples; ++smpl)
	{
		this->secondsIntoPlayback += secondsPerSample;

		int32_t leftChannel = 0, rightChannel = 0;
        int directLeft[2]{},directRight[2]{};
        auto contribute=[&](unsigned channel,int left,int right){
            if(channel==1||channel==3){unsigned index=channel/2;directLeft[index]=left;directRight[index]=right;}
            if((channel==1&&excludeChannel1)||(channel==3&&excludeChannel3))return;
            leftChannel+=left;rightChannel+=right;
        };
        rawClockRemainder+=sampleClock.numerator;
        const unsigned rawCycles=sampleClock.reduce(rawClockRemainder);

		// I need to advance the sound channels here
		for (int i = 0; i < 16; ++i)
		{
			Channel &chn = this->channels[i];
            auto& raw=rawPcm[i];
            if(raw.enabled){
                const unsigned volume=(surroundAmount && i!=1 && i!=3)?sm64ds_surround_volume(raw.volume,raw.pan,surroundAmount):raw.volume;
                raw.run(rawCycles,volume);
                if(!(mute&BIT(i))){
                    int32_t l,r;DsPan(raw.sample,volume,raw.divider,raw.pan,l,r);contribute(i,l,r);
                }
                continue;
            }

			if (chn.state > CS_NONE)
			{
				// The hardware advances the timer, then outputs the current
				// sample (a one-shot that just ended outputs nothing).
				chn.IncrementSample(rawCycles);
				if (chn.state == CS_NONE || (mute & BIT(i)))
					continue;
				const int32_t sample = chn.GenerateSample();
                const unsigned volume=(surroundAmount && i!=1 && i!=3)?sm64ds_surround_volume(chn.reg.volumeMul,chn.reg.panning,surroundAmount):chn.reg.volumeMul;
                int32_t l,r;DsPan(sample,volume,chn.reg.volumeDiv,chn.reg.panning,l,r);contribute(i,l,r);
			}
		}

        const int32_t captureLeft=DsCapture(leftChannel),captureRight=DsCapture(rightChannel);
        SM64DS_TRACE_MIX(captureLeft,captureRight);
        for(unsigned i=0;i<2;++i){
            captures[i].period=rawPcm[1+i*2].period?rawPcm[1+i*2].period:65536;
            captures[i].run(rawCycles,i?captureRight:captureLeft);
        }
        if(leftOutput)leftChannel=((leftOutput&1)?directLeft[0]:0)+((leftOutput&2)?directLeft[1]:0);
        if(rightOutput)rightChannel=((rightOutput&1)?directRight[0]:0)+((rightOutput&2)?directRight[1]:0);
        leftChannel=DsSpeaker(leftChannel);rightChannel=DsSpeaker(rightChannel);

		buf[offset++] = leftChannel & 0xFF;
		buf[offset++] = (leftChannel >> 8) & 0xFF;
		buf[offset++] = rightChannel & 0xFF;
		buf[offset++] = (rightChannel >> 8) & 0xFF;

		if (this->secondsIntoPlayback > this->secondsUntilNextClock)
		{
			// Hardware advances once, irrespective of the number of active sequences.
   for(auto& channel:channels)channel.UpdateTrack();
   for(auto& track:tracks)track.updateFlags.reset();
   for(auto& channel:channels)channel.Update();
   for(unsigned i=0;i<count;++i)players[i]->Run();
   SM64DS_TRACE_TICK(*this,this->secondsIntoPlayback,rate);
			this->secondsUntilNextClock += clockPeriod;
		}
	}
}


void Player::SetExternalVolume(int16_t attenuation)
{
 sseqVol=attenuation;
 for(auto id:trackIds)if(id!=0xff)tracks[id].updateFlags.set(TUF_VOL);
}

// Isolated segment renderer. Production sources are never changed.
// Every public/tick boundary has the same complete history as the legacy mixer.
namespace audio_blocks {
struct Counters { std::uint64_t blocks=0, samples=0, historyTraversals=0, historyReads=0, fallbackSamples=0; };
static Counters counters;
#ifdef AUDIO_BLOCK_DIAGNOSTICS
#define AUDIO_BLOCK_COUNT(expression) (expression)
#else
#define AUDIO_BLOCK_COUNT(expression) ((void)0)
#endif
constexpr unsigned MaxBlock=128;
static bool overlaps(std::uintptr_t a,std::size_t an,std::uintptr_t b,std::size_t bn) {
    if(!an||!bn)return false;
    if(an>~std::uintptr_t(0)-a||bn>~std::uintptr_t(0)-b)return true;
    return a<b+bn&&b<a+an;
}

}
extern "C" void audio_block_counters(std::uint64_t values[5],bool reset) {
#ifdef AUDIO_BLOCK_DIAGNOSTICS
    const auto& c=audio_blocks::counters;
    values[0]=c.blocks;values[1]=c.samples;values[2]=c.historyTraversals;values[3]=c.historyReads;values[4]=c.fallbackSamples;
    if(reset)audio_blocks::counters={};
#else
    for(unsigned i=0;i<5;++i)values[i]=0;
    (void)reset;
#endif
}

namespace audio_blocks {
static bool range_hits(const void* a,size_t an,const void* b,size_t bn) {
 return overlaps(reinterpret_cast<std::uintptr_t>(a),an,reinterpret_cast<std::uintptr_t>(b),bn);
}
// Captures and output can write live sample buffers, but cannot be allowed to
// mutate engine/control metadata while independent voices are rendered ahead.
static bool stable_metadata(const SoundPool& pool,const std::vector<uint8_t>& out) {
 for(const auto& raw:pool.rawPcm)if(raw.enabled) {
  if(range_hits(raw.source,size_t(raw.loopBytes)+raw.lengthBytes,&pool,sizeof pool))return false;
 }
 for(const auto& capture:pool.captures)if(capture.enabled) {
  if(range_hits(capture.destination,capture.bytes,&pool,sizeof pool)||
     range_hits(capture.destination,capture.bytes,&out,sizeof out)||
     range_hits(capture.destination,capture.bytes,out.data(),out.size()))return false;
  for(const auto& c:pool.channels)if(c.state>CS_NONE) {
   if(c.ply&&range_hits(capture.destination,capture.bytes,c.ply,sizeof *c.ply))return false;
   if(c.reg.source&&range_hits(capture.destination,capture.bytes,c.reg.source,sizeof *c.reg.source))return false;
  }
 }
 return !range_hits(out.data(),out.size(),&pool,sizeof pool);
}
static bool independent(const SoundPool& pool,unsigned id,const std::vector<uint8_t>& out) {
 const auto& c=pool.channels[id];
 if(pool.rawPcm[id].enabled||c.state<=CS_NONE||c.state==CS_START||!c.ply||
    c.ply->interpolation!=INTERPOLATION_NONE||c.reg.format==3||!c.reg.source||
    c.reg.source->dataptr!=c.reg.source->data.data())return false;
 if(!c.reg.length||c.reg.totalLength<c.reg.length||c.reg.samplePosition>=static_cast<int32_t>(c.reg.totalLength)||
    c.reg.totalLength>c.reg.source->data.size())return false;
 const size_t bytes=c.reg.source->data.size()*sizeof(int16_t);
 if(range_hits(out.data(),out.size(),c.reg.source->dataptr,bytes))return false;
 for(const auto& capture:pool.captures)if(capture.enabled&&
    range_hits(capture.destination,capture.bytes,c.reg.source->dataptr,bytes))return false;
 return true;
}
}

void SoundPool::GenerateSamples(std::vector<uint8_t>& buf,unsigned offset,unsigned samples,
 unsigned rate,Player* const* players,unsigned count,unsigned mute) {
 static const bool enabled=[](){const char* value=std::getenv("PLAYER_AUDIO_SEGMENT_VOICES");return value&&std::strcmp(value,"1")==0;}();
 if(!enabled){GenerateSamplesLegacy(buf,offset,samples,rate,players,count,mute);return;}

 if(rate<8000||rate>48000||count>16||(count&&!players)||offset>buf.size()||samples>(buf.size()-offset)/4)
  throw std::invalid_argument("invalid shared sound render");
 for(unsigned i=0;i<count;++i) {
  if(!players[i]||&players[i]->pool!=this||players[i]->sampleRate!=rate)
   throw std::invalid_argument("incompatible shared sound player");
  for(unsigned j=0;j<i;++j)if(players[j]==players[i])throw std::invalid_argument("duplicate shared sound player");
 }
 const double step=ClockStep(rate),clockPeriod=ClockPeriod(rate);
 const Sm64dsSampleClock sampleClock=MixerSampleClock(rate);
 while(samples) {
  // A sequence tick can publish a different owner/source. Recheck before
  // advancing the next segment, not only at the public render boundary.
  SM64DS_PROF_BEGIN();
  if(!audio_blocks::stable_metadata(*this,buf)) {
   GenerateSamplesLegacy(buf,offset,samples,rate,players,count,mute);return;
  }
  unsigned span=0;double clock=secondsIntoPlayback;bool tick=false;
  if(IsNativeRate(rate)) {
   // Clock values are integer ARM7 cycles here, exact in a double: same span.
   const double remaining=secondsUntilNextClock-clock;
   const unsigned need=remaining<0?1u:unsigned(remaining/step)+1u;
   span=std::min({need,samples,audio_blocks::MaxBlock});
   clock+=span*step;tick=clock>secondsUntilNextClock;
  } else {
   do{clock+=step;++span;tick=clock>secondsUntilNextClock;}
   while(span<samples&&span<audio_blocks::MaxBlock&&!tick);
  }
  // Bounded integer addition is associative: every ordinary contribution is
  // within signed16 amplitude, with at most16 channels. Clamps stay per sample.
  int32_t left[audio_blocks::MaxBlock]{},right[audio_blocks::MaxBlock]{};
  int32_t directLeft[2][audio_blocks::MaxBlock]{},directRight[2][audio_blocks::MaxBlock]{};
  // Sound-timer ticks of each output sample (the same accumulator as raw PCM
  // and captures, committed after the block).
  unsigned cycles[audio_blocks::MaxBlock];
  std::uint64_t blockRemainder=rawClockRemainder;
  for(unsigned n=0;n<span;++n){blockRemainder+=sampleClock.numerator;cycles[n]=sampleClock.reduce(blockRemainder);}
  unsigned live[16],liveCount=0;
  SM64DS_PROF_MARK(0);
  for(unsigned id=0;id<16;++id) {
   auto& c=channels[id];
   if(!audio_blocks::independent(*this,id,buf)) {
    if(rawPcm[id].enabled||c.state>CS_NONE)live[liveCount++]=id;
    continue;
   }
   // Volume/pan are constant inside a segment (ticks end segments), so the
   // per-sample work is the timer step and two multiply-adds.
   const unsigned pan=c.reg.panning;
   const unsigned volume=(surroundAmount&&id!=1&&id!=3)?sm64ds_surround_volume(c.reg.volumeMul,pan,surroundAmount):c.reg.volumeMul;
   static const uint8_t shifts[4]={4,3,2,0};
   const int32_t gain=int32_t(volume>=127?128:volume)<<shifts[c.reg.volumeDiv&3];
   const int32_t p=pan>=127?128:int32_t(pan),gl=gain*(128-p),gr=gain*p;
   const bool audible=!(mute&BIT(id));
   const bool excluded=(id==1&&excludeChannel1)||(id==3&&excludeChannel3);
   const int16_t* data=c.reg.source->dataptr;
   // minigames-30fps-min: never read a wave without samples (address 0).
   if(!data){sm64ds_sseq_null_source_seen();c.Kill();continue;}
   const uint32_t reload=c.reg.timer,total=c.reg.totalLength,loop=c.reg.loopStart;
   const bool repeat=c.reg.repeatMode==1;
   uint32_t timer=c.reg.timerCounter;int32_t pos=c.reg.samplePosition;bool ended=false;
   for(unsigned n=0;n<span;++n) {
    timer+=cycles[n];
    while(timer>=0x10000){
     timer=timer-0x10000+reload;
     if(++pos>=int32_t(total)){if(repeat)pos=int32_t(loop);else{ended=true;break;}}
    }
    if(ended)break;
    AUDIO_BLOCK_COUNT(++audio_blocks::counters.samples);
    if(!audible)continue;
    // Same as DsPan: (sample<<shift)*volume*(128-p or p) >> 10.
    const int32_t sample=pos<0?0:data[pos];
    const int32_t l=int32_t((int64_t(sample)*gl)>>10),r=int32_t((int64_t(sample)*gr)>>10);
    if(id==1||id==3){directLeft[id/2][n]=l;directRight[id/2][n]=r;}
    if(!excluded){left[n]+=l;right[n]+=r;}
   }
   c.reg.timerCounter=timer;c.reg.samplePosition=pos;
   if(ended)c.Kill();
  }
  SM64DS_PROF_MARK(1);
  // Capture periods follow channels 1/3 and cannot change inside a render.
  for(unsigned id=0;id<2;++id)captures[id].period=rawPcm[1+id*2].period?rawPcm[1+id*2].period:65536;
  for(unsigned n=0;n<span;++n) {
   int32_t l=left[n],r=right[n];
   const unsigned tick=cycles[n];
   // Only dependent/raw/unadmitted channels retain the per-sample loop.
   for(unsigned j=0;j<liveCount;++j) {
    const unsigned id=live[j];auto& raw=rawPcm[id];auto& c=channels[id];
    int32_t a,b;
    if(raw.enabled) {
     const unsigned volume=(surroundAmount&&id!=1&&id!=3)?sm64ds_surround_volume(raw.volume,raw.pan,surroundAmount):raw.volume;
     raw.run(tick,volume);
     if(mute&BIT(id))continue;
     DsPan(raw.sample,volume,raw.divider,raw.pan,a,b);
    } else if(c.state>CS_NONE) {
     c.IncrementSample(tick);AUDIO_BLOCK_COUNT(++audio_blocks::counters.fallbackSamples);
     if(c.state==CS_NONE||(mute&BIT(id)))continue;
     const int32_t value=c.GenerateSample();
     const unsigned volume=(surroundAmount&&id!=1&&id!=3)?sm64ds_surround_volume(c.reg.volumeMul,c.reg.panning,surroundAmount):c.reg.volumeMul;
     DsPan(value,volume,c.reg.volumeDiv,c.reg.panning,a,b);
    } else continue;
    if(id==1||id==3){directLeft[id/2][n]=a;directRight[id/2][n]=b;}
    if((id==1&&excludeChannel1)||(id==3&&excludeChannel3))continue;
    l+=a;r+=b;
   }
   const int32_t cl=DsCapture(l),cr=DsCapture(r);
   SM64DS_TRACE_MIX(cl,cr);
   captures[0].run(tick,cl);captures[1].run(tick,cr);
   if(leftOutput)l=((leftOutput&1)?directLeft[0][n]:0)+((leftOutput&2)?directLeft[1][n]:0);
   if(rightOutput)r=((rightOutput&1)?directRight[0][n]:0)+((rightOutput&2)?directRight[1][n]:0);
   const int16_t out[2]={int16_t(DsSpeaker(l)),int16_t(DsSpeaker(r))};
   std::memcpy(&buf[offset],out,4);offset+=4;
  }
  rawClockRemainder=blockRemainder;
  SM64DS_PROF_MARK(2);
  secondsIntoPlayback=clock;samples-=span;AUDIO_BLOCK_COUNT(++audio_blocks::counters.blocks);
  if(tick) {
   for(auto& c:channels)c.UpdateTrack();
   for(auto& t:tracks)t.updateFlags.reset();
   for(auto& c:channels)c.Update();
   for(unsigned i=0;i<count;++i)players[i]->Run();
   SM64DS_TRACE_TICK(*this,secondsIntoPlayback,rate);
   secondsUntilNextClock+=clockPeriod;
   SM64DS_PROF_MARK(3);
  }
 }
}
