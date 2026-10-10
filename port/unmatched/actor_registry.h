#pragma once
// The original global stores a pointer to the registry, not an inline array.
// Keep every native loader and test on the same declaration.
extern "C" {
extern void** data_020a4bb8;
extern void* data_020a4bb8_storage[512];
}
