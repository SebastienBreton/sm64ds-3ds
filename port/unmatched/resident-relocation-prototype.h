#pragma once

// Typed resident bindings used by the native actor lifecycle.
// The caller samples the unique loaded level once. The resolver is the existing
// pure bounds-checked numeric-data resolver, with provider identity explicit.
namespace resident_relocation {
struct Slot {
    void** destination;
    unsigned original_address, minimum_bytes;
    const unsigned* allowed_providers;
    unsigned allowed_provider_count;
};
struct Binding { void** destination; void* resolved; };
using Resolve = void* (*)(unsigned, unsigned, unsigned);
enum class Result { Ready, InvalidProvider, InvalidSlot, DuplicateSlot,
                    Capacity, Unresolved };

// Failure publishes count=0 and never writes a destination. Partial scratch
// bindings have no authority. The loader commits only a complete Ready plan,
// while provider residency remains unchanged, before invoking constructors.
Result plan(int active_provider, const Slot* slots, unsigned count,
            Resolve resolve, Binding* scratch, unsigned capacity,
            unsigned& prepared_count);
void commit(const Binding* bindings, unsigned count);
}
