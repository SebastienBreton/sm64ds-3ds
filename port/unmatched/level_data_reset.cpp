// Reset numeric level data before overlay initializers, after prior actors die.
// Snapshots contain native pointers into the LIVE tables, never into snapshots.
#include <cstring>
struct LevelResetRange {
    unsigned char *data;
    const unsigned char *initial;
    unsigned size;
    unsigned overlay;
};
extern "C" {
extern const LevelResetRange sm64ds_level_reset_ranges[];
extern const LevelResetRange sm64ds_level_reset_ranges_end[];
// Returns bytes restored; zero means the overlay has no numeric level import.
// This does not initialize actors, reset their BSS, or implement LoadOverlay.
unsigned sm64ds_reset_level_data(unsigned overlay) {
    unsigned bytes=0;
    for (auto *r=sm64ds_level_reset_ranges;r!=sm64ds_level_reset_ranges_end;++r) {
        if(r->overlay!=overlay) continue;
        std::memcpy(r->data,r->initial,r->size);
        bytes+=r->size;
    }
    return bytes;
}
}

// Resolve only already-imported numeric data, with explicit overlay identity.
struct LevelAddressRange {unsigned overlay,original,size;unsigned char* data;};
extern "C" {
extern const LevelAddressRange sm64ds_level_address_ranges[],sm64ds_level_address_ranges_end[];
void* sm64ds_resolve_level_address(unsigned overlay,unsigned address,unsigned size) {
    if(!size)return nullptr;
    for(auto* r=sm64ds_level_address_ranges;r!=sm64ds_level_address_ranges_end;++r)
        if(r->overlay==overlay && address>=r->original && size<=r->size && address-r->original<=r->size-size)
            return r->data+(address-r->original);
    return nullptr;
}
}
