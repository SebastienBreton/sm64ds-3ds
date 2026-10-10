#pragma once
#include "resident-relocation-prototype.h"
struct ResidentGroup {
    unsigned bank;
    const resident_relocation::Slot* slots;
    unsigned count;
    resident_relocation::Binding* scratch;
    unsigned prepared_count;
};
extern "C" {
extern ResidentGroup sm64ds_resident_groups[];
extern const unsigned sm64ds_resident_group_count;
bool sm64ds_prepare_actor_relocations(unsigned);
void sm64ds_commit_actor_relocations(unsigned);
void sm64ds_clear_actor_relocations(unsigned);
}
