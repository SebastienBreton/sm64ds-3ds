#include "resident_groups.h"
extern "C" {
int sm64ds_current_level_overlay();
void* sm64ds_resolve_level_address(unsigned,unsigned,unsigned);
}
namespace {
ResidentGroup* find(unsigned bank) {
    for(unsigned i=0;i<sm64ds_resident_group_count;++i)
        if(sm64ds_resident_groups[i].bank==bank)return &sm64ds_resident_groups[i];
    return nullptr;
}
}
extern "C" bool sm64ds_prepare_actor_relocations(unsigned bank) {
    auto* group=find(bank);
    if(!group)return true;
    return resident_relocation::plan(sm64ds_current_level_overlay(),
        group->slots,group->count,sm64ds_resolve_level_address,group->scratch,
        group->count,group->prepared_count)==resident_relocation::Result::Ready;
}
extern "C" void sm64ds_commit_actor_relocations(unsigned bank) {
    if(auto* group=find(bank))
        resident_relocation::commit(group->scratch,group->prepared_count);
}
extern "C" void sm64ds_clear_actor_relocations(unsigned bank) {
    if(auto* group=find(bank))group->prepared_count=0;
}
