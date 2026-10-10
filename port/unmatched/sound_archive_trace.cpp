#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
extern unsigned char *data_020a5bb8;
int __real_func_02050a84(unsigned);
void* __real__ZN5Sound17InfoSequenceEntry9GetWithIDEj(unsigned);
static void trace(const char* kind,unsigned id,void* caller)
{
    if (!std::getenv("SOUND_ARCHIVE_TRACE")) return;
    void *info=nullptr;
    if (data_020a5bb8) std::memcpy(&info,data_020a5bb8+0x84,sizeof info);
    std::fprintf(stderr,"Sound archive %s: id=%u owner=%p info=%p caller=%p\n",
                 kind,id,data_020a5bb8,info,caller);
    std::fflush(stderr);
}
int __wrap_func_02050a84(unsigned id)
{
    trace("group",id,__builtin_return_address(0));
    return __real_func_02050a84(id);
}
void* __wrap__ZN5Sound17InfoSequenceEntry9GetWithIDEj(unsigned id)
{
    trace("sequence",id,__builtin_return_address(0));
    return __real__ZN5Sound17InfoSequenceEntry9GetWithIDEj(id);
}
}
