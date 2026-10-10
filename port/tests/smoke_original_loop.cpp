// Bootstrap a real Stage, then exercise the untouched main loop on the
// cooperative VBlank runtime. Build with --wrap func_02019390,
// --wrap func_02030e4c and --wrap func_0203df40. This single-player probe
// deliberately fails if an unlinked dispatcher is reached. func_0203df40
// includes LOCAL input sampling/transfer as well as wireless modes; this idle
// probe disables the complete dispatcher and is not an input-progression test.
#undef main
#define main bootstrap_stage_main
#include "smoke_player.cpp"
#undef main
#include "ntr/rt.h"
// Keep libctru's fixed-width typedef spellings separate from the DS headers.
#define u32 CtrU32
#define s32 CtrS32
#define vu32 CtrVU32
#define vs32 CtrVS32
#define Profile CtrProfile
#include <3ds.h>
#undef Profile
#undef vs32
#undef vu32
#undef s32
#undef u32
extern "C" void func_020197b8();
extern "C" void __real_func_02019390();
extern "C" {
unsigned char data_0209e64c=0; // Original BSS: complete input dispatcher disabled in this idle fixture.
unsigned char data_0208f274=1; // Original ARM9 ROM byte.
void __wrap_func_0203df40() {
    printf("FAIL: unlinked native local/wireless input dispatcher reached in idle fixture\n");
    std::abort();
}
int __wrap_func_02030e4c() {
    printf("FAIL: unexpected wireless dispatcher in single-player loop\n");
    std::abort();
}
}
static unsigned original_updates;
static bool original_failed;
static bool original_gpu;
static bool original_ndsp;
static bool original_capture_only;
static double original_update_start;
static int16_t original_capture[160*256*2];
static ndspWaveBuf original_capture_buffer;
extern "C" void __wrap_func_02019390() {
    ntr::gx_begin_frame();
    original_update_start=v3ds_time_ms();
    ++original_updates;
    __real_func_02019390();
}
static bool original_frame(uint64_t frame) {
    static unsigned last_update;
    static double game_total,audio_total,present_total;
    const double entry=v3ds_time_ms();
    if(original_updates!=last_update)game_total+=entry-original_update_start;
    last_update=original_updates;
    static int16_t pcm[2*547];
    const unsigned samples=(frame*32768/60)-((frame-1)*32768/60);
    const bool sound=original_ndsp?sm64ds_native_sound_output_pump():sm64ds_native_sound_render(pcm,samples);
    if(!sound){printf("Original loop audio failure: %s\n",sm64ds_native_sound_output_error());original_failed=true;return false;}
    const double after_audio=v3ds_time_ms();
    audio_total+=after_audio-entry;
    const bool capture=data_0209b274==120;
    if(original_gpu && (capture || !original_capture_only) && !player_present(capture,false)){original_failed=true;return false;}
    present_total+=v3ds_time_ms()-after_audio;
    if(frame%60==0){
        printf("Original loop average ms: game=%.3f audio=%.3f presentation=%.3f\n",game_total/60,audio_total/60,present_total/60);
        game_total=audio_total=present_total=0;
    }
    if(frame<3 || frame%60==0)printf("Original loop: vblank=%llu updates=%u\n",(unsigned long long)frame,original_updates);
    return true;
}
int smoke_main() {
    setenv("PLAYER_STAGE_CONSTRUCT","1",1);
    setenv("PLAYER_STAGE_FRAMES","1",1);
    if(bootstrap_stage_main())return 1;
    original_updates=0;
    original_gpu=std::getenv("ORIGINAL_LOOP_GPU")!=nullptr;
    original_ndsp=std::getenv("ORIGINAL_LOOP_NDSP")!=nullptr;
    original_capture_only=std::getenv("ORIGINAL_LOOP_CAPTURE_ONLY")!=nullptr;
    if(original_capture_only)puts("Capture-only diagnostic: native simulation/audio still advance each tick; presentation is not real-time.");
    if(original_ndsp&&(!original_gpu||original_capture_only))return 2; // GPU VBlank supplies wall-clock pacing.
    if(original_gpu)v3ds_renderer_init();
    if(original_ndsp&&!sm64ds_native_sound_output_open())return 3;
    if(original_ndsp){
        original_capture_buffer.data_pcm16=original_capture;
        original_capture_buffer.nsamples=160*256;
        ndspSetCapture(&original_capture_buffer);
    }
    const char* setting=std::getenv("ORIGINAL_LOOP_FRAMES");
    const unsigned limit=setting?std::atoi(setting):16;
    if(limit<4 || limit>1200)return 2;
    const auto frames=ntr::rt_run(func_020197b8,original_frame,limit);
    if(original_ndsp){
        unsigned submitted=0,completed=0;
        sm64ds_native_sound_output_counts(submitted,completed);
        sm64ds_native_sound_output_close();
        printf("Original loop NDSP: submitted=%u completed=%u\n",submitted,completed);
        unsigned nonzero=0,peak=0;
        for(auto value:original_capture){nonzero+=value!=0;const unsigned magnitude=value<0?-int(value):int(value);if(magnitude>peak)peak=magnitude;}
        printf("Original loop NDSP capture: nonzero=%u peak=%u\n",nonzero,peak);
        if(limit>=120 && (nonzero<1000 || peak<100))original_failed=true;
        if(limit>=120 && (completed<60 || submitted-completed>3))original_failed=true;
    }
    printf("Original loop complete: vblanks=%llu updates=%u failed=%d\n",(unsigned long long)frames,original_updates,original_failed);
    return original_failed || frames!=limit || original_updates<2;
}
