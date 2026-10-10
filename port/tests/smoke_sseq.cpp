#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
    auto* seq=sm64ds_sseq_open(path,55,32768);
    if(!seq){std::printf("decoder error: %s\n",sm64ds_sseq_error());return 3;}
    std::int16_t samples[1024*2];unsigned peak=0;uint32_t hash=2166136261u;unsigned nonzero=0;
    FILE* out=std::fopen("sseq-title-arm11.pcm","wb");if(!out)return 4;
    u64 start=svcGetSystemTick();
    for(unsigned chunk=0;chunk<160;++chunk) {
        if(!sm64ds_sseq_render(seq,samples,1024))return 5;
        for(auto sample:samples){unsigned magnitude=sample<0?-int(sample):int(sample);if(magnitude>peak)peak=magnitude;if(sample)++nonzero;}
        for(auto byte:reinterpret_cast<const unsigned char(&)[4096]>(samples)){hash^=byte;hash*=16777619u;}
        if(std::fwrite(samples,1,sizeof(samples),out)!=sizeof(samples))return 6;
    }
    double elapsed=double(svcGetSystemTick()-start)/SYSCLOCK_ARM11;
    std::fclose(out);sm64ds_sseq_close(seq);
    std::printf("SSEQ ARM11: frames=163840 peak=%u nonzero=%u hash=%08lx seconds=%.3f (includes SD writes)\n",peak,nonzero,(unsigned long)hash,elapsed);
    if(!peak || !nonzero)return 7;
    std::puts("smoke_sseq: PASS native ARM11 synthesis; NDSP output/game command integration not exercised");return 0;
}
