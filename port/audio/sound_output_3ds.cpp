#include "sound_output_3ds.h"
#include <3ds.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
// Defined by sound_native.cpp, the only caller of setLock.
extern "C" __attribute__((weak)) volatile int sm64ds_sound_game_waiting;
struct Sm64dsSoundOutputState {s16* samples;ndspWaveBuf buffers[3];bool owned[3];};
bool Sm64dsSoundOutput::open(Sm64dsSoundCommands& commands){
    if(initialized){std::snprintf(failure,sizeof(failure),"sound output already open");return false;}
    failure[0]=0;submitted=completed=0;
    Result result=ndspInit();
    if(R_FAILED(result)){std::snprintf(failure,sizeof(failure),"ndspInit failed: %08lx",(unsigned long)result);return false;}
    initialized=true;
    state=static_cast<Sm64dsSoundOutputState*>(std::calloc(1,sizeof(Sm64dsSoundOutputState)));
    if(!state){std::snprintf(failure,sizeof(failure),"sound output allocation failed");close();return false;}
    state->samples=static_cast<s16*>(linearAlloc(Count*Frames*2*sizeof(s16)));
    if(!state->samples){std::snprintf(failure,sizeof(failure),"sound linear allocation failed");close();return false;}
    service=&commands;
    ndspChnReset(0);
    // The mixer renders at the DSP's own rate (DS-native 32728.498 Hz), so
    // the DSP must not resample: LINEAR at 32768 Hz swept its fractional
    // phase every ~25 ms and amplitude-modulated the treble at ~39.5 Hz.
    ndspChnSetInterp(0,NDSP_INTERP_NONE);
    ndspChnSetRate(0,NDSP_SAMPLE_RATE);ndspChnSetFormat(0,NDSP_FORMAT_STEREO_PCM16);
    float mix[12]{};mix[0]=mix[1]=1.0f;ndspChnSetMix(0,mix);
    return true;
}
bool Sm64dsSoundOutput::pump(){
    if(!initialized){std::snprintf(failure,sizeof(failure),"sound output is closed");return false;}
    for(unsigned i=0;i<Count;++i){
        auto& buffer=state->buffers[i];
        if(state->owned[i]&&buffer.status==NDSP_WBUF_DONE){state->owned[i]=false;++completed;}
        if(state->owned[i])continue;
        buffer={};buffer.data_pcm16=state->samples+i*Frames*2;buffer.nsamples=Frames;
        auto* lock=static_cast<RecursiveLock*>(serviceLock);
        constexpr unsigned Chunk=128;
        for(unsigned done=0;done<Frames;done+=Chunk){
            if(lock){
                while(sm64ds_sound_game_waiting)svcSleepThread(50000);
                RecursiveLock_Lock(lock);
            }
            const bool rendered=service->render(buffer.data_pcm16+done*2,Chunk);
            if(lock)RecursiveLock_Unlock(lock);
            if(!rendered){
                std::snprintf(failure,sizeof(failure),"sound render failed: %s",service->error());close();return false;
            }
        }
        DSP_FlushDataCache(buffer.data_pcm16,Frames*2*sizeof(s16));
        ndspChnWaveBufAdd(0,&buffer);state->owned[i]=true;++submitted;
    }
    return true;
}
void Sm64dsSoundOutput::close(){
    if(initialized){
        ndspChnWaveBufClear(0);
        // Join the DSP worker before releasing either queue descriptors or PCM.
        ndspExit();initialized=false;
    }
    if(state){if(state->samples)linearFree(state->samples);std::free(state);state=nullptr;}
    service=nullptr;
}
