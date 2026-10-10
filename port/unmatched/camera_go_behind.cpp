// Native 3DS adapter: retail GoBehindPlayer leaves an optional view pointer
// uninitialized. Define it as null, as the ordinary camera update does.
// Keep the matched retail translation unit unchanged.
/* recovered: named members + shared header, real C++ method, declarations from a shared header */
#include "decl_common.h"
/* recovered: named members + shared header, real C++ method */
#include "Camera.h"
extern "C" {
extern unsigned char data_0209f250;
extern signed char data_02092110;
extern void _ZN6Camera11ChangeStateEPNS_5StateE(void *self, void *st);
}

void Camera::GoBehindPlayer(unsigned int j)
{
    // The retail frame leaves this local unspecified; it may become a pointer.
    // The regular camera update starts the same optional view output at null.
    int slot4, slot8 = 0, slotc;

    if (j != data_0209f250)
        return;
    if (data_02092110 >= 0)
        return;

    /* different launder spellings to defeat CSE across the call */
    *(unsigned int *)(((int)&mFlags)) &= 0xfffffaf7u;
    func_0200cb58((void *)((int)this), 0xa);
    *(unsigned int *)(((long long)(int)((int)&mFlags))) |= 4u;

    slot4 = mState_13c;
    slotc = 0;
    func_0200c66c((void *)((int)this), (void *)(mTargetPlayer + 0x5c), &slot8, &slot4, &slotc);
    if (slot4 == (int)&data_020873dc)
        return;
    if (slot4 == (int)&data_0208742c)
        return;
    _ZN6Camera11ChangeStateEPNS_5StateE((void *)((int)this), &data_0209b0e8);
}
