// Emit PCM and portable state for baseline/candidate comparison.
#include "SDAT.h"
#include "Player.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
static std::ofstream state;
template<class T> static void record(T value) { state.write(reinterpret_cast<const char*>(&value),sizeof value); }
static void snapshot(const SoundPool& pool,const Player& player) {
    record(pool.rawClockRemainder);record(pool.secondsIntoPlayback);record(pool.secondsUntilNextClock);
    record(player.localTickCount);record(player.tempoCount);record(player.nTracks);
    for(auto v:player.localVariables)record(v);
    for(auto v:pool.globalVariables)record(v);
    for(const auto& c:pool.channels) {
        record(c.state);record(c.trackId);record(c.prio);record(c.ampl);record(c.noteLength);
        record(c.flags.to_ulong());record(c.reg.samplePosition);record(c.reg.timerCounter);
        record(c.reg.volumeMul);record(c.reg.volumeDiv);record(c.reg.panning);record(c.reg.format);
        record(c.reg.psgX);record(c.reg.psgLastCount);record(c.reg.psgLast);
        record(c.reg.length);record(c.reg.totalLength);record(c.sampleHistoryPtr);
        for(auto v:c.sampleHistory)record(v);
    }
    for(const auto& raw:pool.rawPcm) {
        record(raw.enabled);record(raw.keyOn);record(raw.position);record(raw.sample);
        record(raw.read);record(raw.write);record(raw.available);record(raw.sourceOffset);record(raw.elapsed);
        for(auto v:raw.fifo)record(v);
    }
    for(const auto& capture:pool.captures) {
        record(capture.enabled);record(capture.period);record(capture.elapsed);record(capture.position);
        record(capture.offset);record(capture.read);record(capture.write);record(capture.available);
        for(auto v:capture.fifo)record(v);
    }
}
int main(int argc,char** argv) {
    if(argc!=6)return 2;
    try {
        std::ifstream input(argv[1],std::ios::binary);
        if(!input)return 3;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        PseudoFile file;file.data=&bytes;
        SDAT archive(file,std::stoul(argv[2]));
        Player player;player.sampleRate=32768;
        player.interpolation=Interpolation(std::stoul(argv[3]));
        if(!player.Setup(archive.sseq.get()))return 4;
        std::ofstream output(std::string(argv[4])+".pcm",std::ios::binary);
        state.open(std::string(argv[4])+".state",std::ios::binary);
        const unsigned total=std::stoul(argv[5]);
        std::vector<uint8_t> pcm(1092*4);
        const unsigned sizes[]={1,317,1092,512,64};
        unsigned done=0,block=0;
        // Live DMA capture rings coexist with the sequence and raw channels.
        alignas(4) std::int16_t rawSamples[64],rings[2][128]{};
        for(unsigned i=0;i<64;++i)rawSamples[i]=int(i)*997-31000;
        while(done<total) {
            const unsigned phase=block%128;
            if(phase==17) {
                auto& raw=player.pool.rawPcm[1];raw={};
                raw.source=reinterpret_cast<const uint8_t*>(rawSamples);
                raw.format=1;raw.repeat=1;raw.lengthBytes=sizeof rawSamples;raw.period=512;
                raw.volume=127;raw.pan=93;raw.start();
                player.pool.captures[0]={};auto& capture=player.pool.captures[0];
                capture.destination=reinterpret_cast<uint8_t*>(rings[0]);capture.bytes=sizeof rings[0];capture.start();
            }
            if(phase==32)player.pool.surroundAmount=0x3000;
            if(phase==41)player.SetPaused(true);
            if(phase==49)player.SetPaused(false);
            if(phase==61){player.pool.rawPcm[1].stop();player.pool.captures[0].stop();}
            if(phase==71)player.pool.surroundAmount=0;
            if(phase==81)player.SetExternalVolume(-1024);
            if(phase==87)player.SetExternalVolume(0);
            const unsigned count=std::min(sizes[block%5],total-done);
            player.mutes=phase>=96?0xaaaa:0;
            player.GenerateSamples(pcm,0,count);
            output.write(reinterpret_cast<const char*>(pcm.data()),count*4);
            snapshot(player.pool,player);
            state.write(reinterpret_cast<const char*>(rings),sizeof rings);
            done+=count;++block;
        }
        player.Stop(false);
        snapshot(player.pool,player);
        std::printf("PASS sequence%u interpolation%u frames%u blocks%u\n",unsigned(std::stoul(argv[2])),unsigned(player.interpolation),done,block);
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 5; }
}
