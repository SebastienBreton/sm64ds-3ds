// Native overlay ownership replaces the DS address-range test at0205dc7c.
// Preserve the original three-pointer registration node and LIFO list ABI.
#include "ntr/rt.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstdint>
struct DestructorNode { DestructorNode* next; void (*callback)(void*); void* object; };
extern "C" DestructorNode* data_020aa3f0;
// Some originals register lazy static objects from actor InitResources,
// after the overlay initializer's explicit scope has ended. On DS0205dc7c
// also selects destructor records by the object's address in overlay BSS.
#if defined(__GNUC__)
extern "C" {
int sm64ds_minigame_storage_owner(const void*) __attribute__((weak));
extern unsigned char sm64ds_overlay14_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay14_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay78_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay78_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay84_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay84_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay91_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay91_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay95_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay95_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_menu_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_menu_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay80_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay80_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay63_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay63_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay10_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay10_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_castle_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_castle_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay62_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay62_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay85_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay85_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay89_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay89_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay94_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay94_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay98_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay98_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay100_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay100_bss_end[] __attribute__((weak));
extern unsigned char sm64ds_overlay102_bss_begin[] __attribute__((weak));
extern unsigned char sm64ds_overlay102_bss_end[] __attribute__((weak));
}
#endif
namespace {
struct Owner { DestructorNode* node; int overlay; };
// Ownership bookkeeping must outlive scene heaps. The game replaces C++ new
// with its current-heap allocator, so use the native C heap explicitly here.
template<class T> struct NativeAllocator {
    using value_type=T;
    NativeAllocator()=default;
    template<class U> NativeAllocator(const NativeAllocator<U>&) {}
    T* allocate(std::size_t n) {
        if(n>std::numeric_limits<std::size_t>::max()/sizeof(T))std::abort();
        auto* p=static_cast<T*>(std::malloc(n*sizeof(T)));
        if(!p)std::abort();
        return p;
    }
    void deallocate(T* p,std::size_t) {std::free(p);}
    template<class U> bool operator==(const NativeAllocator<U>&) const {return true;}
    template<class U> bool operator!=(const NativeAllocator<U>&) const {return false;}
};
std::vector<Owner,NativeAllocator<Owner>> owners;
int current_overlay=-1; // Main executable resources survive overlay unloading.
int registration_owner(const void* object) {
    if(current_overlay>=0)return current_overlay;
#if defined(__GNUC__)
    if(sm64ds_minigame_storage_owner){
        const int owner=sm64ds_minigame_storage_owner(object);
        if(owner>=0)return owner;
    }
    struct Range {const unsigned char* begin;const unsigned char* end;int overlay;};
    const Range ranges[]={
        {sm64ds_menu_bss_begin,sm64ds_menu_bss_end,7},
        {sm64ds_overlay14_bss_begin,sm64ds_overlay14_bss_end,14},
        {sm64ds_overlay78_bss_begin,sm64ds_overlay78_bss_end,78},
        {sm64ds_overlay84_bss_begin,sm64ds_overlay84_bss_end,84},
        {sm64ds_overlay91_bss_begin,sm64ds_overlay91_bss_end,91},
        {sm64ds_overlay95_bss_begin,sm64ds_overlay95_bss_end,95},
        {sm64ds_castle_bss_begin,sm64ds_castle_bss_end,9},
        {sm64ds_overlay10_bss_begin,sm64ds_overlay10_bss_end,10},
        {sm64ds_overlay63_bss_begin,sm64ds_overlay63_bss_end,63},
        {sm64ds_overlay80_bss_begin,sm64ds_overlay80_bss_end,80},
        {sm64ds_overlay62_bss_begin,sm64ds_overlay62_bss_end,62},
        {sm64ds_overlay85_bss_begin,sm64ds_overlay85_bss_end,85},
        {sm64ds_overlay89_bss_begin,sm64ds_overlay89_bss_end,89},
        {sm64ds_overlay94_bss_begin,sm64ds_overlay94_bss_end,94},
        {sm64ds_overlay98_bss_begin,sm64ds_overlay98_bss_end,98},
        {sm64ds_overlay100_bss_begin,sm64ds_overlay100_bss_end,100},
        {sm64ds_overlay102_bss_begin,sm64ds_overlay102_bss_end,102},
    };
    const auto address=reinterpret_cast<std::uintptr_t>(object);
    for(const auto& range:ranges){
        const auto begin=reinterpret_cast<std::uintptr_t>(range.begin);
        const auto end=reinterpret_cast<std::uintptr_t>(range.end);
        if(begin && address>=begin && address<end)return range.overlay;
    }
#endif
    return current_overlay;
}
[[noreturn]] void invalid(const char* reason) {
    std::fprintf(stderr,"FATAL: native overlay destructors: %s\n",reason);
    std::fflush(stderr);std::abort();
}
}
extern "C" int sm64ds_set_overlay_scope(int overlay) {
    if(overlay < -1) invalid("invalid scope");
    const int previous=current_overlay;current_overlay=overlay;return previous;
}
extern "C" void func_020731dc(void* object, void* destructor, void** storage) {
    if(!storage) invalid("null registration node");
    auto* node=reinterpret_cast<DestructorNode*>(storage);
    const unsigned saved=ntr::rt_irq_disable();
    for(const auto& owner:owners) if(owner.node==node) {
        // sm64ds_overlay2_reload keeps registered objects and their nodes but
        // restores the guard bits of guarded function-local statics (e.g.
        // Player::ShowMessage2's offset Vector3, guard data_ov002_0210e15c),
        // so the next call constructs and registers the same static again.
        // On DS the unload ran its destructor and the reload cleared the node:
        // the identical registration is the equivalent state. Keep it.
        // (Without this, the second message after a star-select course entry
        // aborted the game.)
        if(node->object==object && reinterpret_cast<void*>(node->callback)==destructor) {
            ntr::rt_irq_restore(saved);
            return;
        }
        std::fprintf(stderr,"overlay duplicate node=%p oldScope=%d newScope=%d oldObject=%p newObject=%p oldCallback=%p newCallback=%p caller=%p\n",node,owner.overlay,current_overlay,node->object,object,reinterpret_cast<void*>(node->callback),destructor,__builtin_return_address(0));
        invalid("node registered twice without teardown");
    }
    owners.push_back({node,registration_owner(object)});
    node->next=data_020aa3f0;
    node->callback=reinterpret_cast<void(*)(void*)>(destructor);
    node->object=object;
    data_020aa3f0=node;
    ntr::rt_irq_restore(saved);
}
extern "C" unsigned sm64ds_destroy_overlay_resources(int overlay) {
    if(overlay<0) invalid("cannot unload main executable resources");
    const int previous=sm64ds_set_overlay_scope(overlay);
    unsigned count=0;
    // As on DS: detach the complete matching batch under IRQ masking, then
    // invoke callbacks with the caller's IRQ state. Repeat for new registrations.
    for(;;) {
        DestructorNode *collected=nullptr,**tail=&collected;
        const unsigned saved=ntr::rt_irq_disable();
        for(auto** link=&data_020aa3f0;*link;) {
            auto* node=*link;
            auto found=owners.begin();
            for(;found!=owners.end() && found->node!=node;++found) {}
            if(found==owners.end()) invalid("untracked node in destructor list");
            if(found->overlay!=overlay) {link=&node->next;continue;}
            *link=node->next;node->next=nullptr;*tail=node;tail=&node->next;
        }
        ntr::rt_irq_restore(saved);
        if(!collected)break;
        while(collected) {
            auto* node=collected;collected=node->next;
            // Keep detached pending nodes tracked until their own callback.
            // They must not be registered again while awaiting destruction.
            const unsigned irq=ntr::rt_irq_disable();
            for(auto it=owners.begin();it!=owners.end();++it)
                if(it->node==node) {owners.erase(it);break;}
            ntr::rt_irq_restore(irq);
            if(node->callback)node->callback(node->object);
            ++count;
        }
    }
    sm64ds_set_overlay_scope(previous);
    return count;
}

// ---------------------------------------------------------------------------
// Overlay 2 (Stage scene code) reload semantics (physics-levels, 2026-10-08).
// On DS, GetSceneOverlayID maps scenes 3/6/7 to overlay 2 and star select /
// menu / Rec Room / mini-games to other overlays: every entry into a stage
// scene from a non-stage scene unloads and reloads overlay 2, so its BSS is
// cleared and its static initializers run again (e.g. __sinit_ov002_021019d0
// re-seeds the player RNG data_ov002_0210e160 = 1234). The port keeps overlay
// 2 resident and ran those initializers once, so mutable overlay-2 statics
// leaked from one level into the next (A/B trace: seed 0x4d2 on DS vs the
// castle-grounds value on 3DS at course arrival).
// Restore the overlay-2 BSS image taken right after its first static
// initialization, except the destructor-registered resource objects and their
// registration nodes, which stay owned by the existing teardown machinery.
#include <cstring>
#if defined(__GNUC__)
extern "C" {
extern unsigned char data_ov002_0210d9a0[] __attribute__((weak)); // first ov2 BSS label (DS 0x0210d9a0)
extern unsigned char data_ov002_02111190[] __attribute__((weak)); // last label, 16 bytes (DS end 0x021111a0)
void func_0201973c(void*) __attribute__((weak));        // RNG seed record callback: plain state
void _ZN7Vector3D1Ev(void*) __attribute__((weak));
}
namespace {
constexpr unsigned kOv2BssSize=0x021111a0-0x0210d9a0;
unsigned char* ov2_snapshot=nullptr;
bool ov2_bss_ok() {
    return data_ov002_0210d9a0 && data_ov002_02111190 &&
           data_ov002_02111190+16-data_ov002_0210d9a0==static_cast<long>(kOv2BssSize);
}
void ov2_keep(unsigned char* keep,const void* p,unsigned size) {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(data_ov002_0210d9a0);
    for(unsigned i=0;i<size;++i)if(a+i>=b && a+i<b+kOv2BssSize)keep[a+i-b]=1;
}
}
extern "C" void sm64ds_overlay2_snapshot() {
    if(!ov2_bss_ok()){std::fprintf(stderr,"overlay2 reload: BSS layout unavailable\n");return;}
    if(!ov2_snapshot)ov2_snapshot=static_cast<unsigned char*>(std::malloc(kOv2BssSize));
    if(!ov2_snapshot)invalid("overlay2 snapshot allocation");
    std::memcpy(ov2_snapshot,data_ov002_0210d9a0,kOv2BssSize);
}
extern "C" unsigned sm64ds_overlay2_reload() {
    if(!ov2_snapshot || !ov2_bss_ok())return 0;
    static unsigned char keep[kOv2BssSize];
    std::memset(keep,0,sizeof keep);
    const unsigned saved=ntr::rt_irq_disable();
    for(auto* node=data_020aa3f0;node;node=node->next) {
        ov2_keep(keep,node,sizeof(DestructorNode));
        if(func_0201973c && node->callback==reinterpret_cast<void(*)(void*)>(func_0201973c))continue;
        // SharedFilePtr records are 8 bytes, Vector3 12; keep a margin for others.
        const bool vec=_ZN7Vector3D1Ev && node->callback==reinterpret_cast<void(*)(void*)>(_ZN7Vector3D1Ev);
        ov2_keep(keep,node->object,vec?12:8);
    }
    unsigned restored=0;
    for(unsigned i=0;i<kOv2BssSize;++i)
        if(!keep[i] && data_ov002_0210d9a0[i]!=ov2_snapshot[i]){data_ov002_0210d9a0[i]=ov2_snapshot[i];++restored;}
    ntr::rt_irq_restore(saved);
    if(std::getenv("SM64DS_OV2_RELOAD_TRACE"))std::printf("overlay2 reload: %u bytes restored\n",restored);
    return restored;
}
#endif
