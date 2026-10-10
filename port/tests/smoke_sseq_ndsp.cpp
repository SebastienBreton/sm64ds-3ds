#include "audio/sseq_decoder.h"
#include "ntr/mmio.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" bool _ZN4Heap13SetupRootHeapEv();
// Capture length must be a multiple of the DSP's 160-frame update quantum.
static s16 captured[160*256*2];

// Exercise real DSP queue ownership, including draining and restarting it.
int main() {
    if(!ntr::io_init() || !_ZN4Heap13SetupRootHeapEv()) return 2;
    Result result=ndspInit();
    std::printf("NDSP init: %08lx\n",(unsigned long)result);
    if(R_FAILED(result)) return 3;
    constexpr unsigned frames=1024, count=3, chunks=64;
    auto* pcm=static_cast<s16*>(linearAlloc(count*frames*2*sizeof(s16)));
    if(!pcm){ndspExit();return 4;}
    ndspWaveBuf buffers[count]{};
    ndspWaveBuf capture{};capture.data_pcm16=captured;capture.nsamples=160*256;
    ndspSetCapture(&capture);
    char path[512];
    std::snprintf(path,sizeof(path),"%s/extracted/dsd/files/data/sound_data.sdat",std::getenv("SM64DS_ASSET_ROOT"));
    bool ok=true;
    for(unsigned pass=0;pass<2 && ok;++pass) {
        auto* seq=sm64ds_sseq_open(path,55,32768);
        if(!seq){std::printf("decoder: %s\n",sm64ds_sseq_error());ok=false;break;}
        ndspChnReset(0);
        ndspChnSetInterp(0,NDSP_INTERP_LINEAR);
        ndspChnSetRate(0,32768);
        ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);
        float mix[12]{};mix[0]=mix[1]=1.f;
        ndspChnSetMix(0,mix);
        std::memset(buffers,0,sizeof(buffers));
        unsigned submitted=0,completed=0,playing=0;
        bool owned[count]{};
        u64 start=svcGetSystemTick();
        while(completed<chunks && ok) {
            for(unsigned i=0;i<count;++i) {
                auto& buf=buffers[i];
                if(owned[i] && buf.status==NDSP_WBUF_PLAYING) ++playing;
                if(owned[i] && buf.status==NDSP_WBUF_DONE){++completed;owned[i]=false;}
                if(!owned[i] && submitted<chunks) {
                    buf={};buf.data_pcm16=pcm+i*frames*2;buf.nsamples=frames;
                    if(!sm64ds_sseq_render(seq,buf.data_pcm16,frames)){std::printf("render: %s\n",sm64ds_sseq_error());ok=false;break;}
                    DSP_FlushDataCache(buf.data_pcm16,frames*2*sizeof(s16));
                    ndspChnWaveBufAdd(0,&buf);owned[i]=true;++submitted;
                }
            }
            const u64 now=svcGetSystemTick();
            if(now-start>10ULL*SYSCLOCK_ARM11){std::printf("timeout start=%llu now=%llu delta=%llu\n",start,now,now-start);ok=false;break;}
            svcSleepThread(1000000);
        }
        double seconds=double(svcGetSystemTick()-start)/SYSCLOCK_ARM11;
        std::printf("NDSP pass=%u submitted=%u completed=%u playing_observations=%u seconds=%.3f\n",pass,submitted,completed,playing,seconds);
        ok=ok && playing && completed==chunks && seconds>1.5 && seconds<4.0;
        // Stop while another buffer is still queued, then reopen on the next pass.
        if(ok) {
            auto& buf=buffers[0];buf={};buf.data_pcm16=pcm;buf.nsamples=frames;
            ok=sm64ds_sseq_render(seq,pcm,frames);
            DSP_FlushDataCache(pcm,frames*2*sizeof(s16));
            if(ok)ndspChnWaveBufAdd(0,&buf);
        }
        ndspChnWaveBufClear(0);
        svcSleepThread(20000000);
        if(ndspChnIsPlaying(0)){std::puts("NDSP clear still playing");ok=false;}
        sm64ds_sseq_close(seq);
    }
    ndspChnWaveBufClear(0);
    ndspExit();
    // The worker has joined: the capture storage can now be read without racing.
    unsigned nonzero=0,peak=0;
    for(s16 value:captured){if(value)++nonzero;unsigned a=value<0?-int(value):int(value);if(a>peak)peak=a;}
    FILE* out=std::fopen("sseq-ndsp-capture.pcm","wb");
    if(!out)ok=false;
    else {if(std::fwrite(captured,1,sizeof(captured),out)!=sizeof(captured))ok=false;std::fclose(out);}
    std::printf("NDSP mixed capture: nonzero=%u peak=%u\n",nonzero,peak);
    ok=ok && nonzero>1000 && peak>100;
    linearFree(pcm);
    std::puts(ok?"smoke_sseq_ndsp: PASS real DSP consumption, drain and restart":"smoke_sseq_ndsp: FAIL");
    return ok?0:5;
}
