#include "native_actor_lifecycle.h"
#include "actor_registry.h"
#include "resident_groups.h"
#include <cstdint>
#include <cstring>

extern "C" {
unsigned sm64ds_reset_level_data(unsigned);
unsigned sm64ds_destroy_overlay_resources(int);
int sm64ds_set_overlay_scope(int);
}

namespace {
NativeActorLifecycle* find(unsigned id, unsigned kind) {
    for (unsigned i = 0; i < sm64ds_actor_lifecycle_count; ++i) {
        auto& entry = sm64ds_actor_lifecycles[i];
        if (entry.id == id && entry.kind == kind) return &entry;
    }
    return nullptr;
}
}

extern "C" bool sm64ds_has_actor_lifecycle(unsigned id, unsigned kind) {
    return find(id, kind) != nullptr;
}

extern "C" bool sm64ds_load_actor_lifecycle(unsigned id, unsigned kind) {
    auto* entry = find(id, kind);
    if (!entry || entry->loaded) return false;
    const auto bytes = reinterpret_cast<uintptr_t>(entry->bss_end)
                     - reinterpret_cast<uintptr_t>(entry->bss_begin);
    if (bytes != entry->bss_bytes) return false;
    if (!sm64ds_prepare_actor_relocations(id)) return false;
    if (sm64ds_reset_level_data(id) != entry->reset_bytes) return false;
    if (bytes) std::memset(entry->bss_begin, 0, bytes);
    sm64ds_commit_actor_relocations(id);
    const int previous = sm64ds_set_overlay_scope(id);
    for (unsigned i = 0; i < entry->constructor_count; ++i)
        entry->constructors[i]();
    sm64ds_set_overlay_scope(previous);
    for (unsigned i = 0; i < entry->profile_count; ++i) {
        entry->previous_profiles[i] = data_020a4bb8[entry->ids[i]];
        data_020a4bb8[entry->ids[i]] = entry->profiles[i];
    }
    entry->loaded = true;
    return true;
}

extern "C" bool sm64ds_unload_actor_lifecycle(unsigned id, unsigned kind) {
    auto* entry = find(id, kind);
    if (!entry || !entry->loaded) return false;
    sm64ds_destroy_overlay_resources(id);
    for (unsigned i = 0; i < entry->profile_count; ++i)
        data_020a4bb8[entry->ids[i]] = entry->previous_profiles[i];
    entry->loaded = false;
    sm64ds_clear_actor_relocations(id);
    return true;
}
