// The SDK wrapper is a separate archive member. Only targets that request
// --wrap=func_01ffde98 extract it; generic GX users need no game SDK or arena.
extern "C" void __real_func_01ffde98(int,int,int);
extern "C" int hal_arena_contains(const void*);
extern "C" void bmd_compiled_submit(int,int,int,void (*)(int,int,int),int (*)(const void*));
extern "C" void __wrap_func_01ffde98(int channel,int source,int bytes){
 bmd_compiled_submit(channel,source,bytes,__real_func_01ffde98,hal_arena_contains);
}
