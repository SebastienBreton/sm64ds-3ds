// Focused instrumentation around the original billboard renderer.
// Build with build_player_probe.py --wrap func_0204be40.
#include "smoke_player.cpp"
extern "C" void __real_func_0204be40(void*,void*);
extern "C" short data_02082214[];
extern "C" void __wrap_func_0204be40(void* manager,void* particle) {
    static unsigned samples=0;
    if(samples++<9) {
        const auto* node=static_cast<const unsigned char*>(particle);
        const auto* context=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(manager)+0x34);
        const auto* resource=*reinterpret_cast<unsigned char*const*>(context+0x18);
        const auto* definition=*reinterpret_cast<unsigned char*const*>(resource);
        const unsigned index=*reinterpret_cast<const unsigned short*>(node+0x36)>>4;
        printf("Particle size: base=%d multiplier=%d aspect=%d rotation=%u trig=%d,%d\n",
            *reinterpret_cast<const int*>(node+0x30),*reinterpret_cast<const short*>(node+0x34),
            *reinterpret_cast<const short*>(definition+0x20),index,data_02082214[index*2],data_02082214[index*2+1]);
    }
    __real_func_0204be40(manager,particle);
}
