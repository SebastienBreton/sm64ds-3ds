// Native member pointers for recovered func_0204335c (initialization dispatch).
// DS ROM words: 02099ebc={0,1}, 02099ec4={4,1}, 02099e94={8,1}:
// virtual slots 0,1,2. Let the native compiler encode these member pointers.
#include "fBase_c.h"
extern "C" {
fBase_c::ProcessFunction data_02099ebc = &fBase_c::InitResources;
fBase_c::BeforeProcessFunction data_02099ec4 = &fBase_c::BeforeInitResources;
fBase_c::AfterProcessFunction data_02099e94 = &fBase_c::AfterInitResources;
}
#ifdef __arm__
static_assert(sizeof(fBase_c::ProcessFunction) == 8, "factory PMF ABI");
#endif

// ROM virtual slots6/7/8: behavior;9/10/11: rendering.
extern "C" {
fBase_c::ProcessFunction data_02099ea4 = &fBase_c::Behavior;
fBase_c::ProcessFunction data_02099eac = &fBase_c::BeforeBehavior;
fBase_c::AfterProcessFunction data_02099eb4 = &fBase_c::AfterBehavior;
fBase_c::ProcessFunction data_02099ecc = &fBase_c::Render;
fBase_c::ProcessFunction data_02099e74 = &fBase_c::BeforeRender;
fBase_c::AfterProcessFunction data_02099e9c = &fBase_c::AfterRender;
}

extern "C" {
fBase_c::ProcessFunction data_02099e8c = &fBase_c::CleanupResources;
fBase_c::ProcessFunction data_02099e7c = &fBase_c::BeforeCleanupResources;
fBase_c::AfterProcessFunction data_02099e84 = &fBase_c::AfterCleanupResources;
int func_0204335c(void *);
int func_02043288(void *);
int func_0204322c(void *);
int func_020432e4(void *);
void func_02043880(void *);
}
// The scheduler's recovered callbacks are nonvirtual member pointers. Let
// the native compiler encode adapters instead of importing ROM code addresses.
struct NativeActorCallbacks {
    void init() { func_0204335c(this); }
    void behavior() { func_02043288(this); }
    void render() { func_0204322c(this); }
    void cleanup() { func_020432e4(this); }
    void reconcile() { func_02043880(this); }
};
using ActorCallback = void (NativeActorCallbacks::*)();
extern "C" {
ActorCallback data_02099f70 = &NativeActorCallbacks::init;
ActorCallback data_02099f48 = &NativeActorCallbacks::behavior;
ActorCallback data_02099f68 = &NativeActorCallbacks::render;
ActorCallback data_02099f50 = &NativeActorCallbacks::cleanup;
ActorCallback data_02099f60 = &NativeActorCallbacks::reconcile;
}
#ifdef __arm__
static_assert(sizeof(ActorCallback) == 8, "scheduler callback ABI");
#endif
