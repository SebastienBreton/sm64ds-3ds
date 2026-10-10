#pragma once
// Test-only APIs, supplied by frozen independent GX translation units.
#include "platform/3ds/gx_geometry_arena.h"
namespace ntr {
void gx_reset_baseline();void gx_reset_candidate();
void gx_write_fifo_baseline(uint32_t);void gx_write_fifo_candidate(uint32_t);
void gx_write_port_baseline(uint32_t,uint32_t);void gx_write_port_candidate(uint32_t,uint32_t);
void gx_begin_frame_baseline();void gx_begin_frame_candidate();
void gx_bind_texture_baseline(const uint32_t*,int,int);void gx_bind_texture_candidate(const uint32_t*,int,int);
void gx_append_polygons_baseline(const GxTriangle*,size_t);void gx_append_polygons_candidate(const GxTriangle*,size_t);
void gx_set_matrix_slot_baseline(int,const float*);void gx_set_matrix_slot_candidate(int,const float*);
const GxTriangle* gx_polygons_baseline(size_t&);
const GxTriangle* gx_polygons_candidate(size_t&);
void gx_render_baseline(Framebuffer&);void gx_render_candidate(Framebuffer&);
void gx_test_state_baseline(std::vector<unsigned char>&);
void gx_test_state_candidate(std::vector<unsigned char>&);
const gx_geometry::Packet& gx_test_packet();
}
