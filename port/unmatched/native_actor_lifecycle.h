#pragma once

// Reviewed actor banks use generated descriptors. The caller must destroy
// actors before unloading; this service owns scoped resources and registry
// restoration, not actor destruction or scene selection.
struct NativeActorLifecycle {
    unsigned id, kind, reset_bytes, bss_bytes;
    unsigned char* bss_begin;
    unsigned char* bss_end;
    void (*const* constructors)();
    unsigned constructor_count;
    const unsigned* ids;
    void* const* profiles;
    void** previous_profiles;
    unsigned profile_count;
    bool loaded;
};

extern "C" {
extern NativeActorLifecycle sm64ds_actor_lifecycles[];
extern const unsigned sm64ds_actor_lifecycle_count;
bool sm64ds_has_actor_lifecycle(unsigned id, unsigned kind);
bool sm64ds_load_actor_lifecycle(unsigned id, unsigned kind);
bool sm64ds_unload_actor_lifecycle(unsigned id, unsigned kind);
}
