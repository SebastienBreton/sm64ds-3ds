#include <algorithm>
#include <bitset>
#include "sseq_decoder.h"
#include "../third_party/sseqplayer/SDAT.h"
#include "../third_party/sseqplayer/Player.h"
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>
struct Sm64dsSequence {
    std::vector<uint8_t> pcm;
    std::unique_ptr<SDAT> archive;
    Player player;
    std::uintptr_t nativeEvents=0,nativeBank=0,nativeWaves[4]{};
    std::vector<std::pair<const SWAV*,std::uintptr_t>> nativeSamples;
    explicit Sm64dsSequence(SoundPool* pool=nullptr):player(pool){}
};
#ifdef __3DS__
extern "C" {extern void* data_020a0e9c;extern void* data_020a0ea0;}
#endif
extern "C" __attribute__((weak)) bool sm64ds_on_present_worker(){return false;}
namespace {
// Decoder calls run synchronously on the game thread. The game owns this global
// allocation context; the NDSP worker must never call the decoder.
struct AudioHeapScope {
#ifdef __3DS__
    void* previous;
    // The core-1 worker renders without allocating from the game heap (its
    // operator new falls back to libc), so it never redirects the game's
    // unlocked heap selector.
    bool active;
    AudioHeapScope():previous(data_020a0ea0),active(!sm64ds_on_present_worker()) {
        if(!data_020a0e9c)throw std::runtime_error("audio requires the persistent root heap");
        if(active)data_020a0ea0=data_020a0e9c;
    }
    ~AudioHeapScope(){if(active)data_020a0ea0=previous;}
#endif
};
bool audioHeapReady(){
#ifdef __3DS__
    return data_020a0e9c!=nullptr;
#else
    return true;
#endif
}
char error[256]={};
void remember(const char* s){std::snprintf(error,sizeof(error),"%s",s);}}
extern "C" const char* sm64ds_sseq_error(){return error;}
static Sm64dsSequence* open_sequence(const char* path,unsigned sequence,unsigned rate,int archive,int bank=-1,Sm64dsBankState* state=nullptr) {
    error[0]=0;
    if(!audioHeapReady()){remember("audio requires the persistent root heap");return {}; }
    AudioHeapScope heapScope;
    try {
        if(!path || rate<8000 || rate>48000)throw std::invalid_argument("invalid sequence path/sample rate");
        auto result=std::make_unique<Sm64dsSequence>();
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("cannot open local SDAT");
        const auto size=in.tellg();
        if(size<0 || size>32*1024*1024)throw std::runtime_error("invalid SDAT size");
        // This temporary full-archive copy must not fragment the game root heap.
        std::unique_ptr<uint8_t,decltype(&std::free)> source(
            static_cast<uint8_t*>(std::malloc(static_cast<std::size_t>(size))),&std::free);
        if(!source)throw std::bad_alloc();
        in.seekg(0);in.read(reinterpret_cast<char*>(source.get()),size);
        if(!in)throw std::runtime_error("short SDAT read");
        if(size<0x40)throw std::runtime_error("short SDAT");
        PseudoFile file;file.externalData=source.get();file.externalSize=static_cast<std::size_t>(size);
        SDATBankState next;
        if(state){next.fileID=state->fileId;std::copy_n(state->waveArchives,4,next.waveArc);}
        result->archive=std::make_unique<SDAT>(file,sequence,archive,bank,state?&next:nullptr);
        // The parsed SSEQ/bank/waves own their data; release the source archive.
        source.reset();
        result->player.sampleRate=rate;
        if(!result->player.Setup(result->archive->sseq.get(),result->archive->entryOffset))throw std::runtime_error("no sequence track available");
        if(state){state->fileId=next.fileID;std::copy_n(next.waveArc,4,state->waveArchives);}
        return result.release();
    } catch(const std::exception& e){remember(e.what());return nullptr;}
}
extern "C" bool sm64ds_sseq_render(Sm64dsSequence* seq,std::int16_t* stereo,unsigned frames) {
    if(!seq || (!stereo && frames) || frames>48000) {remember("invalid render arguments");return false;}
    if(!audioHeapReady()){remember("audio requires the persistent root heap");return {}; }
    AudioHeapScope heapScope;
    try {
        seq->pcm.resize(frames*4);
        seq->player.GenerateSamples(seq->pcm,0,frames);
        if(frames)std::memcpy(stereo,seq->pcm.data(),frames*4);
        return true;
    } catch(const std::exception& e){remember(e.what());return false;}
}
extern "C" void sm64ds_sseq_close(Sm64dsSequence* seq){
    if(!seq)return;
    AudioHeapScope heapScope;
    delete seq;
}

extern "C" Sm64dsSequence* sm64ds_sseq_open(const char* path,unsigned sequence,unsigned rate) {
    return open_sequence(path,sequence,rate,-1);
}
extern "C" Sm64dsSequence* sm64ds_ssar_open(const char* path,unsigned archive,unsigned sequence,unsigned rate,unsigned bank,Sm64dsBankState* state) {
    if(archive>0x7fffffffU){remember("invalid archive index");return nullptr;}
    if(bank!=0xffffffffU && bank>0xffffU){remember("invalid bank index");return nullptr;}
    return open_sequence(path,sequence,rate,int(archive),bank==0xffffffffU?-1:int(bank),state);
}

extern "C" bool sm64ds_sseq_playing(const Sm64dsSequence* seq) {
    if(!seq)return false;
    return seq->player.IsPlaying();
}
extern "C" unsigned sm64ds_sseq_channels(const Sm64dsSequence* seq) {
    unsigned mask=0;
    if(seq)for(unsigned i=0;i<16;++i)
        if(seq->player.channels[i].state!=CS_NONE && seq->player.channels[i].ply==&seq->player)mask|=1U<<i;
    return mask;
}
extern "C" void sm64ds_sseq_stop(Sm64dsSequence* seq,bool killSound) {
    if(!seq)return;
    AudioHeapScope heapScope;
    seq->player.Stop(killSound);
    // Player::Stop also handles detached tails belonging to this owner.
}

// Native sound players already share SWAR resources. Decoding a whole copy for
// every effect multiplies their PCM footprint until the game heap is exhausted.
// Weak entries never extend sample lifetime beyond active players/release tails.
struct WaveCache {
    struct Entry { std::uintptr_t address;std::size_t size;std::weak_ptr<SWAR> waves; };
    std::vector<Entry> entries;
    void collect() {
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const Entry& e){return e.waves.expired();}),entries.end());
    }
    std::shared_ptr<SWAR> load(Sm64dsSoundBuffer blob,PseudoFile& file) {
        collect();
        const auto address=reinterpret_cast<std::uintptr_t>(blob.data);
        for(auto& e:entries)if(e.address==address && e.size==blob.size)
            if(auto shared=e.waves.lock())return shared;
        auto decoded=std::make_shared<SWAR>();decoded->Read(file);
        entries.push_back({address,blob.size,decoded});
        return decoded;
    }
    void invalidate(std::uintptr_t first,std::uintptr_t last) {
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Entry& e){
            return e.address<=last && (first<=e.address || first-e.address<e.size);
        }),entries.end());
    }
};

// Game side: give every PCM wave a bank can play its decoded buffer (see
// SWAV::Prepare), so notes on the realtime renderer never allocate. Only the
// waves the bank references are allocated, not the whole archive.
// Conservative static bound on the programs a sequence can select: program 0
// (every track starts there) plus each PATCH reachable from the entry by
// following jumps, calls, opened tracks and both outcomes of IF, decoding
// commands exactly as Track::Run does. False when the set cannot be bounded
// (random/variable program, unknown command, out-of-range read, budget): the
// caller then prepares the whole bank. Uses no heap.
static bool sequence_programs(const std::uint8_t* data,std::size_t size,std::size_t entry,std::size_t instruments,std::bitset<1024>& programs){
    programs.reset();programs.set(0);
    if(instruments>programs.size())return false;
    std::uint32_t pending[64],seen[256];unsigned npending=0,nseen=0;
    auto queue=[&](std::size_t offset){
        if(offset>=size)return false;
        for(unsigned i=0;i<nseen;++i)if(seen[i]==offset)return true;
        if(nseen==256||npending==64)return false;
        seen[nseen++]=std::uint32_t(offset);pending[npending++]=std::uint32_t(offset);return true;
    };
    if(entry<size && data[entry]==0xfe)entry+=3; // Player::Setup's track mask
    if(!queue(entry))return false;
    unsigned budget=1u<<16;
    while(npending){
        std::size_t p=pending[--npending];
        bool conditional=false;
        for(bool linear=true;linear;){
            if(!budget-- || p>=size)return false;
            const unsigned cmd=data[p++];
            auto skip=[&](std::size_t n){if(size-p<n)return false;p+=n;return true;};
            auto u24=[&](std::size_t& v){if(size-p<3)return false;v=data[p]|(data[p+1]<<8)|(std::size_t(data[p+2])<<16);p+=3;return true;};
            auto vl=[&](std::uint32_t& v){v=0;for(unsigned i=0;i<4;++i){if(p>=size)return false;const unsigned b=data[p++];v=(v<<7)|(b&0x7f);if(!(b&0x80))return true;}return false;};
            std::uint32_t value;std::size_t target;
            const bool wasConditional=conditional;conditional=false;
            if(cmd<0x80){if(!skip(1)||!vl(value))return false;continue;}
            switch(cmd){
            case 0x80:if(!vl(value))return false;break;                               // rest
            case 0x81:if(!vl(value))return false;if(value<instruments)programs.set(value);break; // program
            case 0x93:if(!skip(1)||!u24(target)||!queue(target))return false;break;   // open track
            case 0x94:if(!u24(target)||!queue(target))return false;linear=wasConditional;break; // goto
            case 0x95:if(!u24(target)||!queue(target))return false;break;             // call
            case 0xfd:case 0xff:linear=wasConditional;break;                         // return, fin
            case 0xfc:break;                                                         // loop end
            case 0xa2:conditional=true;break;                                        // if
            case 0xa0:case 0xa1:{                                                    // random, from variable
                if(p>=size)return false;
                const unsigned inner=data[p++];
                if(inner<0x80 || (inner>=0xb0&&inner<=0xbd)){if(!skip(1))return false;}
                else switch(inner){
                    // The override replaces the argument these read through Override::val.
                    case 0x80:case 0xc0:case 0xc1:case 0xc2:case 0xc3:case 0xc4:case 0xca:case 0xcb:case 0xcf:
                    case 0xd0:case 0xd1:case 0xd2:case 0xd3:case 0xd4:case 0xd5:case 0xe0:case 0xe3:break;
                    default:return false; // program, flow or a raw-read command
                }
                if(!skip(cmd==0xa0?4:1))return false;
                break;
            }
            case 0xc0:case 0xc1:case 0xc2:case 0xc3:case 0xc4:case 0xc5:case 0xc6:case 0xc7:case 0xc8:case 0xc9:
            case 0xca:case 0xcb:case 0xcc:case 0xcd:case 0xce:case 0xcf:case 0xd0:case 0xd1:case 0xd2:case 0xd3:
            case 0xd4:case 0xd5:case 0xd6:case 0xd7:if(!skip(1))return false;break;
            case 0xe0:case 0xe1:case 0xe3:case 0xfe:if(!skip(2))return false;break;
            default:
                if(cmd>=0xb0&&cmd<=0xbd){if(!skip(3))return false;break;}
                return false;
            }
        }
    }
    return true;
}
static void prepare_bank_waves(const SBNK& bank,const SWAR* const swars[4],const std::bitset<1024>* programs=nullptr){
    auto prepare=[&](const SBNKInstrumentRange& range){
        if(range.swar>=4 || !swars[range.swar])return;
        const auto found=swars[range.swar]->swavs.find(range.swav);
        if(found!=swars[range.swar]->swavs.end())found->second.Prepare();
    };
    for(std::size_t index=0;index<bank.instruments.size();++index){
        const auto& instrument=bank.instruments[index];
        if(instrument.ranges.empty() || (programs && !programs->test(index)))continue;
        if(instrument.record==16 || instrument.record==17){
            // Track::NoteOn plays a range as PCM unless its record is 0 or PSG (2/3).
            for(const auto& range:instrument.ranges)if(range.record==1 || range.record>=4)prepare(range);
        }else if(instrument.record==1)prepare(instrument.ranges[0]);
    }
}
// The waves a start can play: those of its reachable programs when bounded,
// else every wave the bank references.
static void prepare_sequence_waves(Sm64dsSoundBuffer events,unsigned entryOffset,const SBNK& bank,const SWAR* const swars[4]){
    std::bitset<1024> programs;
    const bool bounded=events.data && sequence_programs(static_cast<const std::uint8_t*>(events.data),events.size,entryOffset,bank.instruments.size(),programs);
    prepare_bank_waves(bank,swars,bounded?&programs:nullptr);
}
static Sm64dsSequence* open_data(Sm64dsSoundBuffer events,unsigned entryOffset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves,unsigned rate,unsigned volume,SoundPool* pool,WaveCache* cache=nullptr) {
    error[0]=0;
    if(!audioHeapReady()){remember("audio requires the persistent root heap");return nullptr;}
    AudioHeapScope heapScope;
    try {
        if(!events.data || !events.size || events.size>32*1024*1024 || entryOffset>=events.size || rate<8000 || rate>48000 || volume>127 || !waves)
            throw std::invalid_argument("invalid loaded sequence data");
        auto input=[](Sm64dsSoundBuffer blob) {
            if(!blob.data || blob.size<16 || blob.size>32*1024*1024)throw std::invalid_argument("invalid loaded sound resource");
            PseudoFile file;file.externalData=static_cast<const uint8_t*>(blob.data);file.externalSize=blob.size;
            const unsigned declared=ReadLE<uint32_t>(file.externalData+8);
            if(declared<16 || declared>blob.size)throw std::range_error("truncated loaded sound resource");
            file.externalSize=declared;return file;
        };
        auto result=std::make_unique<Sm64dsSequence>(pool);
        result->archive=std::make_unique<SDAT>();auto& a=*result->archive;
        a.sseq=std::make_unique<SSEQ>();
        auto* eventBytes=static_cast<const uint8_t*>(events.data);
        a.sseq->data.assign(eventBytes,eventBytes+events.size);
        result->nativeEvents=reinterpret_cast<std::uintptr_t>(events.data);
        result->nativeBank=reinterpret_cast<std::uintptr_t>(bank.data);
        a.sseq->info.vol=volume;a.entryOffset=entryOffset;
        a.sbnk=std::make_unique<SBNK>();auto bankFile=input(bank);a.sbnk->Read(bankFile);
        a.sseq->bank=a.sbnk.get();
        for(unsigned i=0;i<4;++i){
            if(!waves[i].data && !waves[i].size)continue;
            auto waveFile=input(waves[i]);
            if(cache)a.swar[i]=cache->load(waves[i],waveFile);
            else {a.swar[i]=std::make_shared<SWAR>();a.swar[i]->Read(waveFile);}
            a.sbnk->waveArc[i]=a.swar[i].get();
            result->nativeWaves[i]=reinterpret_cast<std::uintptr_t>(waves[i].data);
            for(auto& sample:a.swar[i]->swavs){
                const auto* raw=static_cast<const uint8_t*>(waves[i].data);
                const unsigned offset=ReadLE<uint32_t>(raw+0x3c+sample.first*4);
                result->nativeSamples.emplace_back(&sample.second,reinterpret_cast<std::uintptr_t>(raw)+offset+12);
            }
        }
        {const SWAR* swars[4]{};for(unsigned i=0;i<4;++i)swars[i]=a.swar[i].get();prepare_sequence_waves(events,entryOffset,*a.sbnk,swars);}
        result->player.sampleRate=rate;
        if(!pool && !result->player.Setup(a.sseq.get(),entryOffset))throw std::runtime_error("cannot set up loaded sequence");
        return result.release();
    }catch(const std::exception& e){remember(e.what());return nullptr;}
}

extern "C" Sm64dsSequence* sm64ds_sseq_open_data(Sm64dsSoundBuffer events,unsigned offset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves,unsigned rate,unsigned volume) {
    return open_data(events,offset,bank,waves,rate,volume,nullptr);
}

struct Sm64dsMixer {
    struct Alarm { std::uint64_t first=0,period=0;std::int64_t remaining=0;unsigned tag=0;bool enabled=false; } alarms[8];
    Sm64dsAlarmCallback alarmCallback=nullptr;
    void* alarmContext=nullptr;
    bool rendering=false;
    SoundPool pool; // Destroyed after every owner/resource below.
    WaveCache waves;
    std::array<std::array<int16_t,16>,16> localVariables;
    std::array<uint32_t,16> ticks{};
    std::unique_ptr<Sm64dsSequence> players[16],retired[16];
    std::vector<uint8_t> pcm;
    // Decoded ahead of a locked start (game thread only, like WaveCache).
    std::vector<std::shared_ptr<SWAR>> prepared;
    unsigned rate;
    explicit Sm64dsMixer(unsigned sampleRate):rate(sampleRate){for(auto& vars:localVariables)vars.fill(-1);pool.secondsUntilNextClock=ClockPeriod(rate);}
    void collect(){
        for(auto& p:retired)if(p&&!sm64ds_sseq_channels(p.get()))p.reset();
        for(auto& p:players)if(p&&!sm64ds_sseq_playing(p.get())&&!sm64ds_sseq_channels(p.get()))p.reset();
        waves.collect();
    }
    void release(unsigned id){
        if(!players[id])return;
        players[id]->player.Stop(false);
        collect();
        if(!players[id])return;
        // Every retained owner holds at least one distinct hardware channel,
        // so sixteen entries suffice for the entire hardware pool.
        for(auto& p:retired)if(!p){p=std::move(players[id]);return;}
        throw std::logic_error("sound tail ownership invariant");
    }
};
extern "C" Sm64dsMixer* sm64ds_mixer_open(unsigned rate){
    if(!audioHeapReady()){remember("audio requires the persistent root heap");return nullptr;}
    AudioHeapScope scope;
    try{if(rate<8000||rate>48000)throw std::invalid_argument("invalid mixer rate");
        auto* mixer=new Sm64dsMixer(rate);
        mixer->pcm.reserve(48000*4); // render never grows it on another thread
        return mixer;}
    catch(const std::exception& e){remember(e.what());return nullptr;}
}
extern "C" void sm64ds_mixer_close(Sm64dsMixer* mixer){
    if(!mixer)return;AudioHeapScope scope;delete mixer;
}
extern "C" bool sm64ds_mixer_start(Sm64dsMixer* mixer,unsigned id,Sm64dsSoundBuffer events,unsigned offset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves,unsigned volume){
    if(!mixer||id>=16){remember("invalid mixer player");return false;}
    AudioHeapScope scope;
    try{
        // Parse/own resources before stopping an existing player. Failed resource
        // validation must not destroy the sound already in progress.
        std::unique_ptr<Sm64dsSequence> next(open_data(events,offset,bank,waves,mixer->rate,volume,&mixer->pool,&mixer->waves));
        if(!next)return false;
        mixer->release(id);
        next->player.variables=mixer->localVariables[id].data();
        next->player.tickCount=&mixer->ticks[id];
        if(!next->player.Setup(next->archive->sseq.get(),offset))throw std::runtime_error("shared sequence tracks exhausted");
        mixer->players[id]=std::move(next);return true;
    }catch(const std::exception& e){remember(e.what());return false;}
}
extern "C" void sm64ds_mixer_prepare(Sm64dsMixer* mixer,Sm64dsSoundBuffer events,unsigned entryOffset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves){
    if(!mixer||!waves||!audioHeapReady())return;
    AudioHeapScope scope;
    try{
        auto view=[](Sm64dsSoundBuffer blob,PseudoFile& file){
            if(!blob.data || blob.size<16 || blob.size>32*1024*1024)return false;
            file.externalData=static_cast<const uint8_t*>(blob.data);file.externalSize=blob.size;
            const unsigned declared=ReadLE<uint32_t>(file.externalData+8);
            if(declared<16 || declared>blob.size)return false;
            file.externalSize=declared;return true;
        };
        const SWAR* swars[4]{};
        for(unsigned i=0;i<4;++i){
            PseudoFile file;if(!view(waves[i],file))continue;
            mixer->prepared.push_back(mixer->waves.load(waves[i],file));
            swars[i]=mixer->prepared.back().get();
        }
        // Waves are allocated here, unlocked, for the programs this start reaches; the locked
        // start finds them prepared. Waves already playing are never touched.
        PseudoFile bankFile;
        if(view(bank,bankFile)){SBNK parsed;parsed.Read(bankFile);prepare_sequence_waves(events,entryOffset,parsed,swars);}
    }catch(const std::exception&){}
}
extern "C" void sm64ds_mixer_release_prepared(Sm64dsMixer* mixer){
    if(!mixer||mixer->prepared.empty())return;
    AudioHeapScope scope;mixer->prepared.clear();
}
extern "C" bool sm64ds_mixer_stop(Sm64dsMixer* mixer,unsigned id){
    if(!mixer||id>=16){remember("invalid mixer player");return false;}
    AudioHeapScope scope;
    try{mixer->release(id);return true;}catch(const std::exception& e){remember(e.what());return false;}
}
// ARM9 wave teardown also unlinks SBNK slots (func_0205b6b0). Reflect that
// ownership change in parsed copies, preventing future notes on a freed SWAR.
extern "C" void sm64ds_mixer_forget_resource(Sm64dsMixer* mixer,const void* resource){
    if(!mixer)return;
    AudioHeapScope scope;
    const auto address=reinterpret_cast<std::uintptr_t>(resource);
    mixer->waves.invalidate(address,address);
    auto unlink=[&](auto& owners){for(auto& owner:owners)if(owner)
        for(unsigned i=0;i<4;++i)if(owner->nativeWaves[i]==reinterpret_cast<std::uintptr_t>(resource))
            {owner->archive->sbnk->waveArc[i]=nullptr;owner->archive->sbnk->waveArcInvalidated[i]=true;}
    };
    unlink(mixer->players);unlink(mixer->retired);
}

// ARM7 image 0x4200/0x41c4: a weak reservation may be overridden by an
// explicit track mask; a strong reservation always excludes the channel.
extern "C" bool sm64ds_mixer_lock_channels(Sm64dsMixer* mixer,unsigned mask,unsigned flags,bool lock){
    if(!mixer){remember("missing mixer for channel reservation");return false;}
    AudioHeapScope scope;
    if(lock){
        for(unsigned i=0;i<16;++i)if((mask&(1u<<i)) && !(mixer->pool.lockedChannels&(1u<<i))){mixer->pool.channels[i].Kill();mixer->pool.rawPcm[i].stop();}
    }
    auto& reserved=(flags&1)?mixer->pool.weakLockedChannels:mixer->pool.lockedChannels;
    if(lock)reserved|=mask;else reserved&=~mask;
    mixer->collect();return true;
}

extern "C" bool sm64ds_mixer_pcm_command(Sm64dsMixer* mixer,unsigned command,const std::uintptr_t a[4]){
    if(!mixer||!a){remember("missing PCM command state");return false;}
    if(command==0x13){
        if(a[0]>0x8000){remember("unsupported surround attenuation amount");return false;}
        mixer->pool.surroundAmount=a[0];return true;
    }
    if(command==0xf){
        if(a[0]>=8){remember("invalid sound alarm index");return false;}
        auto& alarm=mixer->alarms[a[0]];alarm={};
        alarm.first=std::uint64_t(std::uint32_t(a[1]))*32;
        alarm.period=std::uint64_t(std::uint32_t(a[2]))*32;
        alarm.tag=a[3]&255;return true;
    }
    if(command==0x15){
        if(a[0]>3||a[1]>3||a[2]>1||a[3]>1){remember("invalid sound output routing");return false;}
        mixer->pool.leftOutput=a[0];mixer->pool.rightOutput=a[1];
        mixer->pool.excludeChannel1=a[2];mixer->pool.excludeChannel3=a[3];return true;
    }
    if(command==0xe){
        const unsigned id=(a[2]>>31)&1;
        if(!a[0] || (a[2]&0x18000000)){remember("unsupported capture source/add mode or missing destination");return false;}
        auto& capture=mixer->pool.captures[id];capture={};
        capture.destination=reinterpret_cast<std::uint8_t*>(a[0]&~std::uintptr_t(3));
        capture.bytes=(a[1]&0xffff)*4;if(!capture.bytes)capture.bytes=4;
        capture.pcm8=(a[2]>>30)&1;capture.oneShot=!(a[2]&0x20000000);
        return true;
    }
    if(command==0xb){
        const unsigned id=a[0]&0xffff,period=(a[0]>>16)&0xffff;
        const unsigned format=(a[3]>>24)&3,repeat=(a[3]>>26)&3;
        const unsigned loop=(a[3]&0xffff)*4,length=(a[2]&0x1fffff)*4;
        if(id>=16||!a[1]||format>1||!period||(repeat!=1&&repeat!=2)||!length||loop+length<16){remember("unsupported or invalid raw PCM setup");return false;}
        auto& raw=mixer->pool.rawPcm[id];raw={};
        raw.source=reinterpret_cast<const std::uint8_t*>(a[1]&~std::uintptr_t(3));raw.period=period;
        raw.format=format;raw.repeat=repeat;raw.loopBytes=loop;raw.lengthBytes=length;
        raw.volume=(a[2]>>24)&127;raw.divider=(a[2]>>22)&3;raw.pan=(a[3]>>16)&127;
        mixer->pool.channels[id].Kill();return true;
    }
    if(command!=9&&command!=10){remember("invalid hardware start/stop command");return false;}
    if(command==9 && (a[2]&255) && !mixer->alarmCallback){remember("sound alarm callback is not installed");return false;}
    if(command==10 && (a[3]&1)){remember("raw PCM hold-after-stop not implemented");return false;}
    for(unsigned i=0;i<16;++i)if(a[0]&(1u<<i)){
        if(command==9)mixer->pool.rawPcm[i].start();
        else {mixer->pool.rawPcm[i].stop();mixer->pool.channels[i].Kill();}
    }
    for(unsigned i=0;i<2;++i)if(a[1]&(1u<<i)){
        if(command==9)mixer->pool.captures[i].start();else mixer->pool.captures[i].stop();
    }
    for(unsigned i=0;i<8;++i)if(a[2]&(1u<<i)){
        auto& alarm=mixer->alarms[i];
        if(command==9){alarm.remaining=alarm.first;alarm.enabled=true;}
        else {if(alarm.enabled)alarm.tag=(alarm.tag+1)&255;alarm.enabled=false;}
    }
    return true;
}
extern "C" void sm64ds_mixer_alarm_callback(Sm64dsMixer* mixer,Sm64dsAlarmCallback callback,void* context){
    if(mixer){mixer->alarmCallback=callback;mixer->alarmContext=context;}
}
extern "C" unsigned sm64ds_mixer_captures(const Sm64dsMixer* mixer){
    if(!mixer)return 0;
    return unsigned(mixer->pool.captures[0].enabled)|(unsigned(mixer->pool.captures[1].enabled)<<1);
}
// Selection mirrors the supplied ARM7 image: 6068 (track cursor), 600c
// (player bank), 4148 (PCM source). Both range boundaries are inclusive.
extern "C" bool sm64ds_mixer_invalidate(Sm64dsMixer* mixer,unsigned command,std::uintptr_t first,std::uintptr_t last){
    if(command<0x1b || command>0x1d || first>last){remember("invalid sound invalidation range");return false;}
    if(!mixer)return true;
    AudioHeapScope scope;
    try{
        auto contains=[&](std::uintptr_t address){return address>=first && address<=last;};
        if(command==0x1d){
            mixer->waves.invalidate(first,last);
            auto invalidateSamples=[&](const auto& owners){
                for(const auto& owner:owners)if(owner)
                    for(auto& channel:mixer->pool.channels)if(channel.ply==&owner->player && channel.state!=CS_NONE){
                        const unsigned format=channel.state==CS_START?(channel.tempReg.CR>>29)&3:channel.reg.format;
                        if(format==3)continue;
                        const auto* source=channel.state==CS_START?channel.tempReg.SOURCE:channel.reg.source;
                        for(const auto& sample:owner->nativeSamples)
                            if(source==sample.first && contains(sample.second)){channel.Kill();break;}
                    }
            };
            invalidateSamples(mixer->players);invalidateSamples(mixer->retired);
        }else{
            for(unsigned id=0;id<16;++id){
                auto* owner=mixer->players[id].get();
                if(!owner || !owner->player.IsPlaying())continue;
                bool affected=command==0x1c && contains(owner->nativeBank);
                if(command==0x1b){
                    const auto begin=reinterpret_cast<std::uintptr_t>(owner->archive->sseq->data.data());
                    for(auto trackId:owner->player.trackIds)if(trackId<FSS_MAXTRACKS){
                        const auto& track=mixer->pool.tracks[trackId];
                        const auto cursor=reinterpret_cast<std::uintptr_t>(track.pos);
                        if(track.ply==&owner->player && cursor>=begin && cursor-begin<=owner->archive->sseq->data.size() &&
                           contains(owner->nativeEvents+cursor-begin)){affected=true;break;}
                    }
                }
                if(affected)mixer->release(id);
            }
        }
        mixer->collect();return true;
    }catch(const std::exception& e){remember(e.what());return false;}
}
extern "C" bool sm64ds_mixer_render(Sm64dsMixer* mixer,int16_t* stereo,unsigned frames){
    if(!mixer||(!stereo&&frames)||frames>48000){remember("invalid mixer render");return false;}
    if(mixer->rendering){remember("recursive sound render");return false;}
    struct Guard {bool& value;Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(mixer->rendering);
    try{
        {AudioHeapScope scope;mixer->pcm.resize(frames*4);}
        unsigned done=0,callbacks=0;
        while(done<frames){
            unsigned chunk=frames-done;
            // Same accumulator as SoundPool: numerator ARM7 cycles per sample,
            // divided by denominator into 16.76 MHz timer ticks.
            const bool native=IsNativeRate(mixer->rate);
            const std::uint64_t numerator=native?1024:ARM7_CLOCK;
            const std::uint64_t denominator=native?2:std::uint64_t(mixer->rate)*2;
            const auto remainder=mixer->pool.rawClockRemainder;
            for(auto& alarm:mixer->alarms)if(alarm.enabled){
                if(alarm.remaining<=0){chunk=0;break;}
                const auto needed=std::uint64_t(alarm.remaining)*denominator;
                const auto until=needed<=remainder?0:(needed-remainder+numerator-1)/numerator;
                chunk=std::min<std::uint64_t>(chunk,until);
            }
            if(chunk){
                AudioHeapScope scope;
                Player* active[16];unsigned count=0;
                for(auto& p:mixer->players)if(p)active[count++]=&p->player;
                mixer->pool.GenerateSamples(mixer->pcm,done*4,chunk,mixer->rate,active,count);
                const auto cycles=(remainder+std::uint64_t(chunk)*numerator)/denominator;
                for(auto& alarm:mixer->alarms)if(alarm.enabled)alarm.remaining-=cycles;
                done+=chunk;
            }
            for(;;){
                int next=-1;
                for(unsigned i=0;i<8;++i)if(mixer->alarms[i].enabled && mixer->alarms[i].remaining<=0 &&
                    (next<0 || mixer->alarms[i].remaining<mixer->alarms[next].remaining))next=i;
                if(next<0)break;
                if(++callbacks>65536 || !mixer->alarmCallback)throw std::runtime_error("sound alarm dispatch failed");
                auto& alarm=mixer->alarms[next];const auto tag=alarm.tag;
                if(alarm.period)alarm.remaining+=alarm.period;else alarm.enabled=false;
                // Outside the decoder heap scope. Rebuild player pointers on
                // the next chunk, since a callback may replace a sequence.
                mixer->alarmCallback(next,tag,mixer->alarmContext);
            }
        }
        if(frames)std::memcpy(stereo,mixer->pcm.data(),frames*4);
        // Retired players are released on the game side (sm64ds_mixer_collect).
        if(!sm64ds_on_present_worker()){AudioHeapScope scope;mixer->collect();}
        return true;
    }catch(const std::exception& e){remember(e.what());return false;}
}
extern "C" void sm64ds_mixer_collect(Sm64dsMixer* mixer){
    if(!mixer)return;
    try{AudioHeapScope scope;mixer->collect();}catch(const std::exception& e){remember(e.what());}
}
extern "C" unsigned sm64ds_mixer_active(const Sm64dsMixer* mixer){
    unsigned mask=0;if(mixer)for(unsigned i=0;i<16;++i)if(sm64ds_sseq_playing(mixer->players[i].get()))mask|=1U<<i;return mask;
}
extern "C" unsigned sm64ds_mixer_channels(const Sm64dsMixer* mixer){
    unsigned mask=0;if(mixer)for(unsigned i=0;i<16;++i)if(mixer->pool.channels[i].state!=CS_NONE)mask|=1U<<i;return mask;
}
extern "C" unsigned sm64ds_mixer_retired(const Sm64dsMixer* mixer){
    unsigned count=0;if(mixer)for(auto& p:mixer->retired)count+=bool(p);return count;
}

extern "C" bool sm64ds_mixer_pause(Sm64dsMixer* mixer,unsigned id,bool paused){
    if(!mixer||id>=16){remember("invalid mixer player");return false;}
    AudioHeapScope scope;
    if(mixer->players[id])mixer->players[id]->player.SetPaused(paused);
    return true;
}

extern "C" bool sm64ds_mixer_parameter(Sm64dsMixer* mixer,unsigned id,unsigned offset,unsigned value,unsigned width){
    if(id>=16||!((width==2&&(offset==6||offset==0x1a))||(width==1&&offset==4))){remember("unsupported player parameter");return false;}
    // Writes to inactive slots have no observable effect: Setup initializes these
    // fields on the next start. Keep their valid command semantics before init.
    if(!mixer||!mixer->players[id])return true;
    auto& player=mixer->players[id]->player;
    if(offset==6)player.SetExternalVolume(static_cast<int16_t>(value));
    else if(offset==0x1a)player.tempoRate=static_cast<uint16_t>(value);
    else player.prio=static_cast<uint8_t>(value);
    return true;
}

extern "C" bool sm64ds_mixer_track_parameter(Sm64dsMixer* mixer,unsigned id,unsigned mask,unsigned offset,unsigned value,unsigned width){
    if(id>=16||!Track::ParameterSupported(offset,width)){remember("unsupported track parameter");return false;}
    if(!mixer||!mixer->players[id])return true;
    auto& p=mixer->players[id]->player;
    for(unsigned i=0;i<16;++i)if((mask&(1U<<i))&&p.trackIds[i]!=0xff)
        p.tracks[p.trackIds[i]].SetParameter(offset,value);
    return true;
}

extern "C" bool sm64ds_mixer_track_mute(Sm64dsMixer* mixer,unsigned id,unsigned mask,unsigned mode){
    if(id>=16){remember("invalid mute player");return false;}
    if(!mixer||!mixer->players[id])return true;
    auto& p=mixer->players[id]->player;
    for(unsigned i=0;i<16;++i)if((mask&(1U<<i))&&p.trackIds[i]!=0xff)
        p.tracks[p.trackIds[i]].SetMute(mode);
    return true;
}

extern "C" bool sm64ds_mixer_track_channels(Sm64dsMixer* mixer,unsigned id,unsigned mask,unsigned channels){
    if(id>=16){remember("invalid channel-mask player");return false;}
    if(!mixer||!mixer->players[id])return true;
    auto& p=mixer->players[id]->player;
    for(unsigned i=0;i<16;++i)if((mask&(1U<<i))&&p.trackIds[i]!=0xff)
        p.tracks[p.trackIds[i]].SetChannelMask(channels);
    return true;
}

extern "C" bool sm64ds_mixer_set_variable(Sm64dsMixer* mixer,unsigned id,unsigned index,int16_t value){
    if(!mixer||id>=16||index>=32){remember("invalid sequence variable");return false;}
    if(index<16)mixer->localVariables[id][index]=value;
    else mixer->pool.globalVariables[index-16]=value;
    return true;
}
extern "C" bool sm64ds_mixer_get_variable(const Sm64dsMixer* mixer,unsigned id,unsigned index,int16_t* value){
    if(!value||id>=16||index>=32){remember("invalid sequence variable");return false;}
    *value=!mixer?-1:index<16?mixer->localVariables[id][index]:mixer->pool.globalVariables[index-16];
    return true;
}

extern "C" void sm64ds_mixer_copy_status(const Sm64dsMixer* mixer,std::int16_t locals[16][16],std::uint32_t ticks[16],std::int16_t globals[16]){
    for(unsigned id=0;id<16;++id){
        for(unsigned i=0;i<16;++i)locals[id][i]=mixer?mixer->localVariables[id][i]:-1;
        ticks[id]=mixer?mixer->ticks[id]:0;
    }
    for(unsigned i=0;i<16;++i)globals[i]=mixer?mixer->pool.globalVariables[i]:-1;
}
extern "C" bool sm64ds_mixer_get_ticks(const Sm64dsMixer* mixer,unsigned id,uint32_t* value){
    if(!value||id>=16){remember("invalid sequence tick counter");return false;}
    *value=mixer?mixer->ticks[id]:0;return true;
}

extern "C" unsigned sm64ds_mixer_enabled_channels(const Sm64dsMixer* mixer){
    unsigned mask=0;if(mixer)for(unsigned i=0;i<16;++i)if(mixer->pool.channels[i].reg.enable||mixer->pool.rawPcm[i].enabled)mask|=1U<<i;
    return mask;
}
