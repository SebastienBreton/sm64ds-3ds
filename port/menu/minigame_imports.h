#pragma once
inline bool sm64ds_minigame_imported(unsigned id) {
    switch(id) {
#define SM64DS_MINIGAME(actor,factory,profile,rom_factory,vtable,name) case actor:return true;
#define SM64DS_MINIGAME_INIT(function,address)
#include "minigame_imports.inc"
#undef SM64DS_MINIGAME_INIT
#undef SM64DS_MINIGAME
    default:return false;
    }
}
