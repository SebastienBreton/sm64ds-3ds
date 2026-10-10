// Host runtime wrappers for the interactive game link (build_game.py --wrap).
// Diagnostics read environment switches all over the port, some per packet
// or per draw; newlib's getenv rescans the whole environment every time.
// Lookups are cached until the environment changes (setenv/unsetenv/putenv).
#define u32 CtrU32
#define s32 CtrS32
#define vu32 CtrVU32
#define vs32 CtrVS32
#include <3ds.h>
#undef vs32
#undef vu32
#undef s32
#undef u32
#include <cstdlib>
#include <cstring>
#include <cstdint>

extern "C" {
char* __real_getenv(const char*);
int __real_setenv(const char*,const char*,int);
int __real_unsetenv(const char*);
int __real_putenv(char*);
}
namespace {
struct Entry {char name[48];char* value;bool used;};
constexpr unsigned Capacity=128;
Entry cache[Capacity];
RecursiveLock lock;
volatile int state=0;
void ensure_lock(){
    if(state==2)return;
    if(__sync_bool_compare_and_swap(&state,0,1)){RecursiveLock_Init(&lock);__sync_synchronize();state=2;}
    else while(state!=2)svcSleepThread(10000);
}
unsigned hash(const char* s){unsigned h=2166136261u;while(*s)h=(h^static_cast<unsigned char>(*s++))*16777619u;return h;}
void clear(){for(auto& e:cache)e.used=false;}
}
extern "C" char* __wrap_getenv(const char* name){
    if(!name || std::strlen(name)>=sizeof(Entry::name))return __real_getenv(name);
    ensure_lock();RecursiveLock_Lock(&lock);
    unsigned slot=hash(name)%Capacity;char* value=nullptr;bool found=false;
    for(unsigned probe=0;probe<Capacity;++probe,slot=(slot+1)%Capacity){
        auto& e=cache[slot];
        if(!e.used){
            value=__real_getenv(name);
            std::strcpy(e.name,name);e.value=value;e.used=true;found=true;break;
        }
        if(!std::strcmp(e.name,name)){value=e.value;found=true;break;}
    }
    if(!found)value=__real_getenv(name); // cache full: uncached lookup
    RecursiveLock_Unlock(&lock);
    return value;
}
extern "C" int __wrap_setenv(const char* name,const char* value,int overwrite){
    ensure_lock();RecursiveLock_Lock(&lock);clear();const int r=__real_setenv(name,value,overwrite);RecursiveLock_Unlock(&lock);return r;
}
extern "C" int __wrap_unsetenv(const char* name){
    ensure_lock();RecursiveLock_Lock(&lock);clear();const int r=__real_unsetenv(name);RecursiveLock_Unlock(&lock);return r;
}
extern "C" int __wrap_putenv(char* string){
    ensure_lock();RecursiveLock_Lock(&lock);clear();const int r=__real_putenv(string);RecursiveLock_Unlock(&lock);return r;
}

// castle-min30: memcmp (build_game.py --wrap memcmp). newlib's ARM memcmp
// compares one aligned word per 11 instructions and falls back to a byte loop
// for any misalignment; Azahar's guest time follows the instruction count, so
// the PPU/composite snapshot checks (tens of KB per changed frame: character
// blocks, maps, palettes, mask rows) cost several ms on pause/dialog frames.
// Same result as newlib: 0, or the difference of the first differing bytes
// (as unsigned char). sm64ds_reference_memcmp=1 (set by diagnostic mains)
// forwards to newlib for A/B checks.
extern "C" {
int __real_memcmp(const void*,const void*,size_t);
volatile int sm64ds_reference_memcmp=0;
int __wrap_memcmp(const void* a,const void* b,size_t n){
    if(sm64ds_reference_memcmp)return __real_memcmp(a,b,n);
    const unsigned char* p=static_cast<const unsigned char*>(a);
    const unsigned char* q=static_cast<const unsigned char*>(b);
    if(n>=16 && ((reinterpret_cast<uintptr_t>(p)^reinterpret_cast<uintptr_t>(q))&3)==0){
        while(reinterpret_cast<uintptr_t>(p)&3){if(*p!=*q)return int(*p)-int(*q);++p;++q;--n;}
        const uint32_t* x=reinterpret_cast<const uint32_t*>(p);
        const uint32_t* y=reinterpret_cast<const uint32_t*>(q);
#if defined(__arm__)
        // 16 bytes per iteration with two 4-register loads (Azahar charges per
        // instruction, so the multi-register loads halve the loop cost). Stops
        // before the first block that differs; the word loop below locates it.
        if(n>=16){
            uint32_t blocks=n>>4;
            asm volatile(
                "1:\n\t"
                "ldmia %[x], {r4,r5,r6,r7}\n\t"
                "ldmia %[y], {r8,r9,r10,r12}\n\t"
                "eor r4, r4, r8\n\t"
                "eor r5, r5, r9\n\t"
                "eor r6, r6, r10\n\t"
                "eor r7, r7, r12\n\t"
                "orr r4, r4, r5\n\t"
                "orr r6, r6, r7\n\t"
                "orrs r4, r4, r6\n\t"
                "bne 2f\n\t"
                "add %[x], %[x], #16\n\t"
                "add %[y], %[y], #16\n\t"
                "subs %[b], %[b], #1\n\t"
                "bne 1b\n"
                "2:\n"
                :[x]"+r"(x),[y]"+r"(y),[b]"+r"(blocks)
                :
                :"r4","r5","r6","r7","r8","r9","r10","r12","cc","memory");
            n=(n&15)+(size_t(blocks)<<4);
        }
#else
        while(n>=32){
            const uint32_t d=(x[0]^y[0])|(x[1]^y[1])|(x[2]^y[2])|(x[3]^y[3]);
            const uint32_t e=(x[4]^y[4])|(x[5]^y[5])|(x[6]^y[6])|(x[7]^y[7]);
            if(d|e)break; // the word loop below finds the first difference
            x+=8;y+=8;n-=32;
        }
#endif
        while(n>=4 && *x==*y){++x;++y;n-=4;}
        p=reinterpret_cast<const unsigned char*>(x);q=reinterpret_cast<const unsigned char*>(y);
    }
    for(;n;--n,++p,++q)if(*p!=*q)return int(*p)-int(*q);
    return 0;
}
}
