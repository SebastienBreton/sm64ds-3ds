#include "resident-relocation-prototype.h"
#include <cstdint>

namespace resident_relocation {
Result plan(int active, const Slot* slots, unsigned count, Resolve resolve,
            Binding* scratch, unsigned capacity, unsigned& prepared_count) {
    prepared_count = 0;
    if (active < 8 || active >= 60) return Result::InvalidProvider;
    if (count > capacity) return Result::Capacity;
    if ((count && (!slots || !scratch)) || !resolve) return Result::InvalidSlot;
    for (unsigned i = 0; i < count; ++i) {
        const auto& slot = slots[i];
        if (!slot.destination || !slot.minimum_bytes ||
            !slot.allowed_providers || !slot.allowed_provider_count ||
            (slot.original_address & 3) ||
            uint64_t(slot.original_address) + slot.minimum_bytes > (uint64_t(1) << 32))
            return Result::InvalidSlot;
        for (unsigned j = 0; j < i; ++j)
            if (slots[j].destination == slot.destination) return Result::DuplicateSlot;
        bool allowed = false;
        for (unsigned j = 0; j < slot.allowed_provider_count; ++j) {
            const auto provider = slot.allowed_providers[j];
            if (provider < 8 || provider >= 60) return Result::InvalidSlot;
            for (unsigned k = 0; k < j; ++k)
                if (slot.allowed_providers[k] == provider) return Result::InvalidSlot;
            if (provider == unsigned(active)) allowed = true;
        }
        // A bank shared by several levels holds per-level tables: entries that
        // address another level's overlay are never used by the resident one
        // (on DS they point at unrelated bytes). Bind them to null instead.
        void* pointer = allowed ? resolve(unsigned(active), slot.original_address, slot.minimum_bytes) : nullptr;
        scratch[i] = {slot.destination, pointer};
    }
    prepared_count = count;
    return Result::Ready;
}

void commit(const Binding* bindings, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        *bindings[i].destination = bindings[i].resolved;
}
}
