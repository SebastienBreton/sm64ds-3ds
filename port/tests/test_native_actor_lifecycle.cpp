// Behavioral differential against the five previously validated bank wrappers.
// Constructors/resources are observers here; this is not actor runtime proof.
#include "unmatched/native_actor_lifecycle.h"
#include "unmatched/actor_registry.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
void* data_020a4bb8_storage[512];
void** data_020a4bb8 = data_020a4bb8_storage;
}
namespace {
struct Event {
    unsigned operation, bank, value;
    bool operator==(const Event& b) const {
        return operation == b.operation && bank == b.bank && value == b.value;
    }
};
std::vector<Event> events;
int scope = 77;
bool reset_success = true;
unsigned expected_bank;
unsigned expected_kind;
NativeActorLifecycle& descriptor(unsigned bank) {
    for (unsigned i = 0; i < sm64ds_actor_lifecycle_count; ++i)
        if (sm64ds_actor_lifecycles[i].id == bank) return sm64ds_actor_lifecycles[i];
    std::abort();
}
void constructor(unsigned bank, unsigned ordinal) {
    const auto& entry = descriptor(bank);
    assert(scope == int(bank));
    assert(std::all_of(entry.bss_begin, entry.bss_end,
                       [](unsigned char value) { return value == 0; }));
    events.push_back({2, bank, ordinal});
}
void prepare(unsigned bank) {
    expected_bank = bank;
    expected_kind = descriptor(bank).kind;
    scope = 77;
    reset_success = true;
    events.clear();
    for (unsigned i = 0; i < 512; ++i)
        data_020a4bb8[i] = reinterpret_cast<void*>(uintptr_t(0x10000+4*i));
    std::memset(descriptor(bank).bss_begin, 0xa5, descriptor(bank).bss_bytes);
}
}

extern "C" unsigned sm64ds_reset_level_data(unsigned bank) {
    assert(bank == expected_bank);
    events.push_back({0, bank, reset_success ? descriptor(bank).reset_bytes : 0xffffffffu});
    return reset_success ? descriptor(bank).reset_bytes : 0xffffffffu;
}
extern "C" int sm64ds_set_overlay_scope(int next) {
    const int previous = scope;
    scope = next;
    events.push_back({1, unsigned(next), unsigned(previous)});
    return previous;
}
extern "C" unsigned sm64ds_destroy_overlay_resources(int bank) {
    const auto& entry = descriptor(bank);
    for (unsigned i = 0; i < entry.profile_count; ++i)
        assert(data_020a4bb8[entry.ids[i]] == entry.profiles[i]);
    events.push_back({3, unsigned(bank), 0});
    return 0;
}

// The generated fixture defines original profile identities, BSS extents,
// constructor observers and prototypes for legacy/current callback symbols.
#include "native_actor_lifecycle_fixture.h"

namespace {
std::vector<Event> cycle(unsigned bank, bool (*load)(), bool (*unload)()) {
    prepare(bank);
    assert(!unload());
    assert(load());
    assert(!load());
    assert(scope == 77);
    const auto& entry = descriptor(bank);
    for (unsigned i = 0; i < entry.profile_count; ++i)
        assert(data_020a4bb8[entry.ids[i]] == entry.profiles[i]);
    assert(unload());
    assert(!unload());
    for (unsigned i = 0; i < 512; ++i)
        assert(data_020a4bb8[i] == reinterpret_cast<void*>(uintptr_t(0x10000+4*i)));
    return events;
}
void failure(unsigned bank, bool (*load)(), bool (*unload)()) {
    prepare(bank);
    reset_success = false;
    assert(!load());
    assert(!unload());
    assert(events.size() == 1 && scope == 77);
    assert(std::all_of(descriptor(bank).bss_begin, descriptor(bank).bss_end,
                       [](unsigned char value) { return value == 0xa5; }));
    for (unsigned i = 0; i < 512; ++i)
        assert(data_020a4bb8[i] == reinterpret_cast<void*>(uintptr_t(0x10000+4*i)));
}
}

int main() {
    unsigned cycles = 0;
    for (const auto& callbacks : test_callbacks) {
        for (unsigned i = 0; i < 3; ++i) {
            const auto previous = cycle(callbacks.bank, callbacks.legacy_load,
                                        callbacks.legacy_unload);
            const auto current = cycle(callbacks.bank, callbacks.load, callbacks.unload);
            assert(previous == current);
            ++cycles;
        }
        failure(callbacks.bank, callbacks.legacy_load, callbacks.legacy_unload);
        failure(callbacks.bank, callbacks.load, callbacks.unload);
        const auto& entry = descriptor(callbacks.bank);
        const auto before = events;
        assert(!sm64ds_has_actor_lifecycle(callbacks.bank, entry.kind+100));
        assert(!sm64ds_load_actor_lifecycle(callbacks.bank, entry.kind+100));
        assert(!sm64ds_unload_actor_lifecycle(callbacks.bank, entry.kind+100));
        assert(events == before);
        auto& mutable_entry = descriptor(callbacks.bank);
        prepare(callbacks.bank);
        ++mutable_entry.bss_bytes;
        assert(!callbacks.load());
        --mutable_entry.bss_bytes;
        assert(events.empty() && scope == 77);
        assert(std::all_of(mutable_entry.bss_begin, mutable_entry.bss_end,
                           [](unsigned char value) { return value == 0xa5; }));
    }
    assert(cycles == 15);
    assert(!sm64ds_has_actor_lifecycle(104, 19));
    std::puts("PASS 15 legacy/descriptor cycles; reset failure and kind guards exact");
}
