#include "hal/alarm_layout.h"
extern "C" { NativeAlarm *data_020a6444[2] = {}; }
void func_02059a60(NativeAlarm *p, uint64_t key)
{
    NativeAlarm *n;

    if (p->step != 0) {
        uint64_t t = func_02059650();
        key = p->base;
        if (key < t) {
            key = key + ((t - key) / p->step + 1) * p->step;
        }
    }
    p->key = key;

    n = data_020a6444[0];
    if (n != 0) {
        do {
            if ((long long)(key - n->key) < 0) {
                p->prev = n->prev;
                n->prev = p;
                p->next = n;
                if (p->prev) {
                    p->prev->next = p;
                    return;
                }
                data_020a6444[0] = p;
                func_02059c18(p);
                return;
            }
            n = n->next;
        } while (n);
    }

    p->next = 0;
    {
        NativeAlarm *tail = data_020a6444[1];
        data_020a6444[1] = p;
        p->prev = tail;
        if (tail) {
            tail->next = p;
            return;
        }
    }
    data_020a6444[1] = p;
    data_020a6444[0] = p;
    func_02059c18(p);
}
