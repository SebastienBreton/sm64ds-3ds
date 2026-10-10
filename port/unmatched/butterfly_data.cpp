// Native counterparts of ov100 butterfly resource storage and state constants.
#include "daBtfly_c.h"
using ButterflyState=void (daBtfly_c::*)();
static_assert(sizeof(ButterflyState)==8,"butterfly ARM member-pointer ABI");
extern "C" {
ButterflyState data_ov100_02147e20=&daBtfly_c::State5;
ButterflyState data_ov100_02147e28=&daBtfly_c::State3;
ButterflyState data_ov100_02147e30=&daBtfly_c::State4;
ButterflyState data_ov100_02147e38=&daBtfly_c::State7;
ButterflyState data_ov100_02147e40=&daBtfly_c::State1;
ButterflyState data_ov100_02147e48=&daBtfly_c::State0;
ButterflyState data_ov100_02147e50=&daBtfly_c::State2;
ButterflyState data_ov100_02147e58=&daBtfly_c::State6;

// Shared still model owned by the IBL initializer in the original overlay.
// Initialize its resource registration here until that actor's states are linked.
extern unsigned char data_ov100_02148668[];
extern unsigned data_ov100_02148670[];
void func_02017acc(void*,int);
void func_02017ab4(void*);
void func_020731dc(void*,void(*)(void*),void*);
void sm64ds_init_butterfly_shared_model() {
    func_02017acc(data_ov100_02148668,0x9c00);
    func_020731dc(data_ov100_02148668,func_02017ab4,data_ov100_02148670);
}
}
