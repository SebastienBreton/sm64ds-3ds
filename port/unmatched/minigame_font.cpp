// Native storage/accessor for the minigame scene's font allocation.
// Original ov004020adc4c returns the pointer at020beb60. BeforeInitResources
// allocates0x4000 bytes and AfterCleanupResources frees/zeros it. Those scene
// methods must own this allocation when minigame lifecycle is integrated.
#include <cstdint>
extern "C" {
void* data_ov004_020beb60=nullptr;
int func_ov004_020adc4c() {
    static_assert(sizeof(void*)==sizeof(int), "3DS pointer ABI");
    return static_cast<int>(reinterpret_cast<uintptr_t>(data_ov004_020beb60));
}
}
