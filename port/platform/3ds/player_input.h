#pragma once
#include <cstdint>
namespace player_input {
// Submit one 3DS HID held-key sample through the original DS input processor.
// Circle Pad digital direction bits share the D-pad path; analog input is pending.
void submit(uint32_t held);
void reset();
void poll();
void wait_frame();
}
