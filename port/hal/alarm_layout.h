#pragma once
#include <cstddef>
#include <cstdint>
// DS compiler aligns these64-bit fields to4 bytes. GCC ARM otherwise uses8.
struct __attribute__((packed, aligned(4))) NativeAlarm {
    void (*callback)(int);
    int argument;
    unsigned tag;
    uint64_t key;
    NativeAlarm *prev, *next;
    uint64_t step, base;
};
static_assert(sizeof(void*)==4, "alarm ABI needs32-bit pointers");
static_assert(offsetof(NativeAlarm,key)==0x0c && offsetof(NativeAlarm,prev)==0x14);
static_assert(offsetof(NativeAlarm,next)==0x18 && offsetof(NativeAlarm,step)==0x1c);
static_assert(offsetof(NativeAlarm,base)==0x24 && sizeof(NativeAlarm)==0x2c);
extern "C" {
extern NativeAlarm *data_020a6444[2];
uint64_t func_02059650();
void func_02059a60(NativeAlarm*, uint64_t);
void func_02059c18(NativeAlarm*);
void func_02059950(NativeAlarm*);
}
