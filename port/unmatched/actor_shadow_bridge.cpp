// Native ABI bridge for two original shadow helpers omitted from the host link.
// Bodies mirror the named original src translation units; no geometry/Y bias.
// DropShadowScaleXYZ uses scalar registers just like the adjacent original
// DropShadowRadHeight bridge (see ShadowModel.h Fix12 by-value ABI note).
#include "dActor_c.h"
#include "ShadowModel.h"
extern "C" {
extern BMD_File data_020ad524;
void _ZN11ShadowModel9InitModelEP9Matrix4x35Fix12IiES3_S3_j(
    ShadowModel*, Matrix4x3*, int, int, int, u32);
void _ZN8dActor_c18DropShadowScaleXYZER11ShadowModelR9Matrix4x35Fix12IiES5_S5_j(
    dActor_c* self, ShadowModel* shadow, Matrix4x3* matrix,
    int scaleX, int scaleY, int scaleZ, u8 opacity)
{
    if (self->mFlags & 0x10) return;
    _ZN11ShadowModel9InitModelEP9Matrix4x35Fix12IiES3_S3_j(
        shadow, matrix, scaleX, scaleY, scaleZ, opacity);
}
}
int ShadowModel::InitCuboid()
{
    return SetFile(&data_020ad524, 1, -1);
}
