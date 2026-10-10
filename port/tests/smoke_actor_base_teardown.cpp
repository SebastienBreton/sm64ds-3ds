#include "ntr/mmio.h"
#include "Heap.h"
#include "dBgActor_c.h"
#include "unmatched/actor_registry.h"
#include <cstdio>
#include <cstring>

// Call the linked ABI entry points explicitly: an inline destructor compiled
// in this probe would not exercise the HAL TU's late-weakened fallback bug.
extern "C" void _ZN10dBgActor_cC2Ev(void*);
extern "C" void _ZN10dBgActor_cD1Ev(void*);
extern "C" unsigned short data_020a4b54;

int main(){
    if(!ntr::io_init())return 1;
    auto* heap=Heap::SetupRootHeap();if(!heap)return 2;
    // Constructors read the current spawn profile even before resources are
    // initialized. Provide the same priorities/ranges as the arrow fixture;
    // a null profile makes a teardown-only test read address zero at creation.
    struct SpawnInfo {
        void* factory;unsigned short behavior,render;
        unsigned flags;int offsetY,radius,distance,farDistance;
    };
    static_assert(sizeof(SpawnInfo)==0x1c,"native spawn profile");
    static SpawnInfo profile{nullptr,0x12b,0xbc,2,0xc0000,0xe0000,0x1000000,0x1000000};
    data_020a4b54=0x12b;data_020a4bb8[0x12b]=&profile;
    const auto before=heap->VMemoryLeft();
    alignas(dBgActor_c) static unsigned char storage[sizeof(dBgActor_c)];
    for(unsigned cycle=0;cycle<3;++cycle){
        std::memset(storage,0,sizeof storage);
        _ZN10dBgActor_cC2Ev(storage);
        _ZN10dBgActor_cD1Ev(storage);
        if(heap->VMemoryLeft()!=before){
            std::printf("FAIL actor base teardown heap cycle%u\n",cycle);return 3;
        }
    }
    std::puts("smoke_actor_base_teardown: PASS three native constructions/destructions, exact heap recovery");
    return 0;
}
