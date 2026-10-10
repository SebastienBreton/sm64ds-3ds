#pragma once
#include <cstdint>
namespace observed_cadence {
// Configure before entering the original game loop. Other smoke entry points
// leave this module dormant, even when their object graph contains its hook.
void configure();
void after_frame(uint64_t frame);
void report();
// Real-time catch-up (hardware bring-up): presentation lateness becomes debt
// that later frames repay by not waiting. While behind(), the caller may skip
// presenting a frame so the game logic and music keep real-time speed.
bool behind();
void note_skipped();
}
