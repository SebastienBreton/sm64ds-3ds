// Pair two native synth owners against the two iteration policies.
#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <algorithm>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
bool sm64ds_audio_test_selective=false;
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    char path[512];std::snprintf(path,sizeof path,"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
    unsigned fixtures=0,frames=0;
    for(unsigned id:{0u,14u,28u,41u,55u,69u,82u}) {
        auto* baseline=sm64ds_sseq_open(path,id,32768);
        auto* candidate=sm64ds_sseq_open(path,id,32768);
        if(!baseline || !candidate){std::printf("FATAL: sound open sequence%u: %s\n",id,sm64ds_sseq_error());return 3;}
        std::int16_t pcm[2][1092*2];unsigned hash=2166136261u;
        const unsigned sizes[]={1,317,1092,512,64};
        unsigned done=0,block=0;
        while(done<163840) {
            const unsigned count=std::min(sizes[block%5],163840-done);
            sm64ds_audio_test_selective=false;
            if(!sm64ds_sseq_render(baseline,pcm[0],count))return 4;
            sm64ds_audio_test_selective=true;
            if(!sm64ds_sseq_render(candidate,pcm[1],count))return 5;
            if(std::memcmp(pcm[0],pcm[1],count*4) || sm64ds_sseq_channels(baseline)!=sm64ds_sseq_channels(candidate) ||
                sm64ds_sseq_playing(baseline)!=sm64ds_sseq_playing(candidate)) {
                std::printf("FATAL: audio policy mismatch sequence%u frame%u count%u\n",id,done,count);return 6;
            }
            const auto* bytes=reinterpret_cast<const unsigned char*>(pcm[0]);
            for(unsigned i=0;i<count*4;++i)hash=(hash^bytes[i])*16777619u;
            done+=count;++block;
        }
        sm64ds_sseq_close(candidate);sm64ds_sseq_close(baseline);
        ++fixtures;frames+=done;
        std::printf("Audio scan exact sequence%u frames%u PCMhash%08x\n",id,done,hash);
    }
    std::printf("PASS native audio iteration policies: %u sequences, %u frames, identical PCM/channel masks/playing status, uneven render partitions\n",fixtures,frames);
    return 0;
}
