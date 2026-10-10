// Level collision for sm64ds_viewer (game world units, Fix12).
#ifndef VIEWER_CLSN_H
#define VIEWER_CLSN_H
struct ClsnBox { int x0, y0, z0, ex, ey, ez; };   // world Fix12 octree box: origin + extents (KCL storage x64)
bool clsn_load(unsigned handle, ClsnBox *box);
// Downward ray at (x, z) from ytop to ybot; true + floor height on a hit.
bool clsn_floor(int x, int z, int ytop, int ybot, int *y);
// Segment in Fix12 world coordinates; endpoint is shortened to first hit.
bool clsn_segment(const int start[3], int end[3]);
#endif
