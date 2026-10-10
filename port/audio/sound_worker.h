#pragma once
// Drain the original stream message queue on the serialized game task. The
// capture clock enqueues work; no native thread may touch game heap state.
bool sm64ds_sound_worker_poll();
