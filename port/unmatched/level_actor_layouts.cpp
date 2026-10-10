#include "daObjSwitch_c.h"
#include "daObjHeart_c.h"
static_assert(sizeof(daObjSwitch_c::State)==8,"Switch PMF original8");
static_assert(sizeof(daObjSwitch_c::StateEntry)==16,"Switch table16");
static_assert(sizeof(daObjSwitch_c::Resources)==12,"Switch descriptor12");
static_assert(sizeof(daObjSwitch_c)==0x354,"Switch original size");
#include "daWarpkun_c.h"
static_assert(sizeof(daWarpkun_c)==0x108,"ROM factory allocates 0x108");
static_assert(__builtin_offsetof(daWarpkun_c,mdCcAc_c)==0xd4,"ROM collider offset 0xd4");
static_assert(sizeof(dCcAc_c)==0x34,"collider closes at 0x108");
