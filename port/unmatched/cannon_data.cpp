// Native resource handles and callbacks corresponding to ov098 cannon data.
struct CannonCallbacks {
    void aim() asm("func_ov098_0213b0a4");
    void fire() asm("func_ov098_0213ade8");
    void emerge() asm("func_ov098_0213ad08");
    void player() asm("func_ov098_0213aa28");
};
using CannonState=void (CannonCallbacks::*)();
static_assert(sizeof(CannonState)==8,"cannon ARM member-pointer ABI");
extern "C" {
CannonState data_ov098_0213c644=&CannonCallbacks::player;
CannonState data_ov098_0213c64c=&CannonCallbacks::aim;
CannonState data_ov098_0213c654=&CannonCallbacks::emerge;
CannonState data_ov098_0213c65c=&CannonCallbacks::fire;
unsigned short data_ov098_0213c63c[3]={0,4096,0};
}
