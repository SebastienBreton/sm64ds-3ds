#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv())return 2;
    char path[512];std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
    for(unsigned test=0;test<7;++test) {
        const unsigned archive=test<5?test:2;
        const unsigned entry=test==5?424:test==6?440:archive==0?3:0;
        auto* seq=sm64ds_ssar_open(path,archive,entry,32768,archive==3?32:0xffffffffU);
        if(!seq){std::printf("SSAR error: %s\n",sm64ds_sseq_error());return 3;}
        std::int16_t pcm[1024*2];uint32_t hash=2166136261u;unsigned nonzero=0,peak=0;
        for(unsigned chunk=0;chunk<32;++chunk) {
            if(!sm64ds_sseq_render(seq,pcm,1024))return 4;
            for(auto value:pcm){if(value)++nonzero;unsigned m=value<0?-int(value):value;if(m>peak)peak=m;}
            for(auto byte:reinterpret_cast<const unsigned char(&)[4096]>(pcm)){hash^=byte;hash*=16777619u;}
        }
        sm64ds_sseq_close(seq);
        std::printf("SSAR archive=%u entry=%u hash=%08lx nonzero=%u peak=%u\n",archive,entry,(unsigned long)hash,nonzero,peak);
        if(!nonzero)return 5;
    }
    auto* invalid=sm64ds_ssar_open(path,5,0,32768);
    if(invalid){sm64ds_sseq_close(invalid);return 6;}
    invalid=sm64ds_ssar_open(path,4,1,32768);
    if(invalid){sm64ds_sseq_close(invalid);return 7;}
    std::puts("smoke_ssar: PASS five archive classes, zero-note loops and rejected invalid indices");
    return 0;
}
