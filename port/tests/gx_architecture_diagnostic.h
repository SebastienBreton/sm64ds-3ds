#pragma once
// Diagnostic only: the production GX/runtime never include this header.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <climits>
#if defined(__3DS__) && defined(__arm__)
// Avoid libctru's u32/s32 aliases conflicting with the original game headers.
extern "C" std::uint64_t svcGetSystemTick(void);
#else
#include <chrono>
#endif

namespace ntr { bool gx_diagnostic_decoder_empty(); }
namespace ntr::gx_diag {
enum Category : unsigned { Fifo, Port, MatrixCommand, NormalCommand, VertexCommand,
    TextureCommand, OtherCommand, Multiply, Projection, Direction, TextureBind,
    Assembly, TriangleEmission, Bulk, CategoryCount };
inline const char* names[CategoryCount] = {"FIFO inclusive", "port inclusive",
    "matrix command inclusive", "NORMAL command inclusive", "vertex command inclusive",
    "texture command inclusive", "other command inclusive", "matrix multiply",
    "projection", "direction", "texture bind", "assembly inclusive", "triangle emission", "bulk inclusive"};
struct Statistics { std::uint64_t calls=0, samples=0, ticks=0, minimum=UINT64_MAX, maximum=0; };
inline Statistics stats[CategoryCount];
inline std::uint64_t commands[256]{}, frames=0, blocks[3]{}, words[3]{},
    aligned_empty_blocks=0, pending_blocks=0, clock_pair_min=0;
inline unsigned mode=0, mask=63, offset=0;
inline bool enabled=false, count_enabled=true;

inline std::uint64_t clock() {
#if defined(__3DS__) && defined(__arm__)
    return svcGetSystemTick();
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}
inline double ticks_per_ms() {
#if defined(__3DS__) && defined(__arm__)
    return (16756991.0 * 16.0) / 1000.0; // libctru SYSCLOCK_SOC * 16
#else
    using P=std::chrono::steady_clock::period;
    return double(P::den)/double(P::num)/1000.0;
#endif
}
inline bool selected(unsigned phase) {
    if(mode==1)return phase==Fifo || phase==Port;
    if(mode==2)return phase>=MatrixCommand && phase<=OtherCommand;
    if(mode==3)return phase>=Multiply && phase<=TriangleEmission;
    if(mode==4)return phase==Bulk;
    return false;
}
struct Scope {
    unsigned phase;
    bool timed=false;
    std::uint64_t start=0;
    explicit Scope(unsigned category,bool eligible=true):phase(category) {
        if(!enabled || !count_enabled || !eligible)return;
        const auto ordinal=stats[phase].calls++;
        timed=selected(phase) && ((ordinal+offset)&mask)==0;
        if(timed)start=clock();
    }
    ~Scope() {
        if(!timed)return;
        const auto elapsed=clock()-start;
        auto& s=stats[phase];++s.samples;s.ticks+=elapsed;
        if(elapsed<s.minimum)s.minimum=elapsed;
        if(elapsed>s.maximum)s.maximum=elapsed;
    }
};
inline unsigned category(std::uint8_t cmd) {
    if(enabled && count_enabled)++commands[cmd];
    if(cmd>=0x10 && cmd<=0x1c)return MatrixCommand;
    if(cmd==0x21)return NormalCommand;
    if(cmd>=0x23 && cmd<=0x28)return VertexCommand;
    if(cmd==0x2a || cmd==0x2b)return TextureCommand;
    return OtherCommand;
}
inline void bulk(const void* source,const void* destination,int count) {
    if(!enabled || !count_enabled || count<=0 || reinterpret_cast<std::uintptr_t>(destination)!=0x04000400u)return;
    const auto a=reinterpret_cast<std::uintptr_t>(source);
    const unsigned kind=(a>=0x06000000u && a<0x07000000u)?1:
        (a>=0x04000000u && a<0x04010000u)?2:0;
    ++blocks[kind];words[kind]+=count;
    if(gx_diagnostic_decoder_empty())++aligned_empty_blocks;else ++pending_blocks;
}
inline void configure() {
    if(const char* v=std::getenv("PLAYER_GX_DIAGNOSTIC_MODE")){
        const int value=std::atoi(v);count_enabled=value>=0;mode=value>=0?unsigned(value):0;
    }
    if(const char* v=std::getenv("PLAYER_GX_SAMPLE_RATE")){
        const unsigned rate=unsigned(std::atoi(v));
        if(rate && !(rate&(rate-1)) && rate<=4096)mask=rate-1;
    }
    if(const char* v=std::getenv("PLAYER_GX_SAMPLE_OFFSET"))offset=unsigned(std::atoi(v))&mask;
    clock_pair_min=UINT64_MAX;
    for(unsigned i=0;i<256;++i){const auto a=clock();const auto elapsed=clock()-a;if(elapsed<clock_pair_min)clock_pair_min=elapsed;}
    std::printf("GX diagnostic startup mode=%u counters=%u rate=%u offset=%u clock_pair_min_ticks=%llu\n",
        mode,unsigned(count_enabled),mask+1,offset,(unsigned long long)clock_pair_min);
}
inline void begin(bool active) {enabled=active;if(active)++frames;}
inline void end() {enabled=false;}
inline void dump() {
    const double scale=ticks_per_ms();
    std::puts("GX sampled estimates include timer/counter overhead; phases are inclusive and hierarchy levels must not be added");
    std::printf("GX diagnostic frames=%llu pair_min_ms=%.9f\n",(unsigned long long)frames,clock_pair_min/scale);
    for(unsigned i=0;i<CategoryCount;++i){const auto& s=stats[i];
        const double estimated=s.samples?double(s.ticks)*double(s.calls)/double(s.samples)/scale:0;
        std::printf("GX diagnostic phase=%u name=%s calls=%llu samples=%llu ticks=%llu min_ticks=%llu max_ticks=%llu estimated_ms/update=%.9f\n",
            i,names[i],(unsigned long long)s.calls,(unsigned long long)s.samples,(unsigned long long)s.ticks,
            (unsigned long long)(s.samples?s.minimum:0),(unsigned long long)s.maximum,frames?estimated/frames:0);
    }
    for(unsigned i=0;i<256;++i)if(commands[i])std::printf("GX diagnostic command=%02x calls=%llu\n",i,(unsigned long long)commands[i]);
    std::printf("GX diagnostic FIFO blocks RAM_or_other=%llu VRAM=%llu IO=%llu words=%llu/%llu/%llu entry_empty=%llu entry_pending=%llu\n",
        (unsigned long long)blocks[0],(unsigned long long)blocks[1],(unsigned long long)blocks[2],
        (unsigned long long)words[0],(unsigned long long)words[1],(unsigned long long)words[2],
        (unsigned long long)aligned_empty_blocks,(unsigned long long)pending_blocks);
}
}
