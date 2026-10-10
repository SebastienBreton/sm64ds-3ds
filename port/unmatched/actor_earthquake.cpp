// Correct native ABI for the recovered veneer at 0x02010168.
// ROM: load camera into r0, then bx func_0200d8c8 without changing r1/r2.
// The matched source's two-argument prototype drops the position/strength
// registers; an ordinary C++ call needs all three arguments stated.
#include "types.h"
extern "C" {
extern void *data_0209f318;
void func_0200d8c8(void *camera, const Vector3 *position, int strength);
void _ZN8dActor_c10EarthquakeERK7Vector35Fix12IiE(
    void *actor, const Vector3 *position, int strength)
{
    (void)actor;
    func_0200d8c8(data_0209f318, position, strength);
}
}
