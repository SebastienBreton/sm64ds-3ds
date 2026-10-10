// Native copy of func_ov009_0211145c: replace its Metrowerks register-scheduling
// asm hatch with the identical stores and sound call. Original source preserved.
#include "common.h"
extern char* _ZN8dActor_c13ClosestPlayerEv(void* self);
extern int Vec3_Sub(struct Vector3* dst, void* a, void* b);
extern int Vec3_HorzLen(struct Vector3* v);
extern char* _ZN8dActor_c5SpawnEjjRK7Vector3PK10Vector3_16as(unsigned int id, unsigned int p1, void* pos, void* rot, int a, int b);
extern void func_ov009_02111224(char* c, int r1);
extern int func_0201267c(unsigned int a, void* b);
extern int RandomIntInternal(void* rng);
extern int data_0209e650;

void func_ov009_0211145c(char* c) {
    if (*(unsigned char*)(c + 0x180) != 0) {
        char* p;
        struct Vector3 copy;
        struct Vector3 diff;
        int n;
        p = _ZN8dActor_c13ClosestPlayerEv(c);
        if (p == 0) {
            return;
        }
        Vec3_Sub(&diff, c + 0x5c, p + 0x5c);
        copy.x = diff.x;
        copy.y = diff.y;
        copy.z = diff.z;
        if (Vec3_HorzLen(&copy) > 0x7d0000) {
            return;
        }
        n = *(int*)(c + 8) & 0xf;
        if (n > 1) {
            int i = 0;
            int lim = n - 1;
            if (lim > 0) {
                do {
                    char* a = _ZN8dActor_c5SpawnEjjRK7Vector3PK10Vector3_16as(0x157, 0x10, c + 0x5c, 0, *(signed char*)(c + 0xcc), -1);
                    if (a != 0) {
                        func_ov009_02111224(a, *(int*)(c + 4));
                    }
                    i++;
                } while (i < lim);
            }
        }
        *(int *)(c + 0x160) = -0x14000;
        *(int *)(c + 0x168) = (int)0xff06a000u;
        func_0201267c(0x6a, c + 0x74);
    }
    *(short*)(c + 0x92) = 5000 - (unsigned int)RandomIntInternal(&data_0209e650) % 4000;
    *(unsigned short*)(c + 0x94) = (unsigned int)RandomIntInternal(&data_0209e650) >> 16;
    *(int*)(c + 0x174) = 0x28000;
    *(int*)(c + 0x17c) = 3;
    *(int*)(c + 0xb0) &= ~0x10000;
}
