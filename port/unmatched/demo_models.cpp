// Preserve the cartridge's 0x60/0x84 model allocations and raw-field ABI.
// GCC's virtual-base layout for daDemo_c.h does not preserve those offsets.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "unmatched/native_overlay.h"
extern "C" {
void _ZN13SharedFilePtr7ReleaseEv(void*);
void _ZN5ModelD1Ev(void*);
void _ZN9ModelAnimD1Ev(void*);
void _ZN6Memory16operator_delete2EPv(void*);
void _ZN5Model9DoSetFileEPcii();
void _ZN5Model11UpdateVertsEv();
void _ZN5Model9Virtual10ER9Matrix4x3();
void _ZN5Model6RenderEPK7Vector3();
void _ZN9ModelAnim11UpdateVertsEv();
void _ZN9ModelAnim9Virtual10ER9Matrix4x3();
void _ZN9ModelAnim6RenderEPK7Vector3();
void _ZN9ModelAnim9Virtual18EjPK7Vector3();
int sm64ds_current_level_overlay();
void* sm64ds_resolve_level_address(unsigned,unsigned,unsigned);
}
namespace {
void release(void* p) { if(p)_ZN13SharedFilePtr7ReleaseEv(p); }
void simple_destroy(void* p) {
    release(*reinterpret_cast<void**>(static_cast<char*>(p)+0x5c));
    _ZN5ModelD1Ev(p);
}
void simple_delete(void* p) { simple_destroy(p);_ZN6Memory16operator_delete2EPv(p); }
void anim_destroy(void* p) {
    auto* c=static_cast<unsigned char*>(p);
    release(*reinterpret_cast<void**>(c+0x70));
    for(unsigned i=0;i<c[0x80];++i)release((*reinterpret_cast<void***>(c+0x74))[i]);
    void* sequence=*reinterpret_cast<void**>(c+0x7c);
    if(sequence) {
        for(unsigned i=0;i<c[0x81];++i)release((*reinterpret_cast<void***>(c+0x78))[i]);
        using Delete=void(*)(void*);
        (*reinterpret_cast<Delete**>(sequence))[1](sequence);
    }
    _ZN9ModelAnimD1Ev(p);
}
void anim_delete(void* p) { anim_destroy(p);_ZN6Memory16operator_delete2EPv(p); }
void anim_secondary_destroy(void* p) { anim_destroy(static_cast<char*>(p)-0x50); }
void anim_secondary_delete(void* p) { anim_delete(static_cast<char*>(p)-0x50); }
}
extern "C" {
// These are address points (slot zero), not compiler vtable symbols.
void* data_ov002_0210bae4[]={
    (void*)simple_destroy,(void*)simple_delete,(void*)_ZN5Model9DoSetFileEPcii,
    (void*)_ZN5Model11UpdateVertsEv,(void*)_ZN5Model9Virtual10ER9Matrix4x3,
    (void*)_ZN5Model6RenderEPK7Vector3};
void* data_ov002_0210bcc4[]={
    (void*)anim_destroy,(void*)anim_delete,(void*)_ZN5Model9DoSetFileEPcii,
    (void*)_ZN9ModelAnim11UpdateVertsEv,(void*)_ZN9ModelAnim9Virtual10ER9Matrix4x3,
    (void*)_ZN9ModelAnim6RenderEPK7Vector3,(void*)_ZN9ModelAnim9Virtual18EjPK7Vector3};
void* data_ov002_0210bce8[]={(void*)anim_secondary_destroy,(void*)anim_secondary_delete};

int demo_level() {
    const int level=sm64ds_current_level_overlay();
    switch(level) {
    case 9:case 14:case 15:case 16:case 18:case 20:case 21:case 22:
    case 26:case 27:case 29:case 36:return level;
    default:std::fputs("cutscene resource unavailable in resident overlay\n",stderr);std::abort();
    }
}
void* sm64ds_demo_level_model() {
    auto* result=sm64ds_resolve_level_address(demo_level(),0x02113c20,8);
    if(!result){std::fputs("missing resident cutscene model handle\n",stderr);std::abort();}
    return result;
}
void** sm64ds_demo_level_animation() {
    static void* result;
    result=sm64ds_resolve_level_address(demo_level(),0x02113c28,8);
    if(!result){std::fputs("missing resident cutscene animation handle\n",stderr);std::abort();}
    return &result;
}
}
