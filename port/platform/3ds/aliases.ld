/* GNU ld counterparts of the MSVC /alternatename pragmas in port/hal that are
   still real renames under Itanium mangling (the rest -- class vs struct,
   static member vs namespace function, return type -- mangle identically under
   GCC and need nothing). PROVIDE only binds when the alias is referenced and
   not otherwise defined. Passed to the linker as an implicit script. */
PROVIDE(data_020a4d38 = _ZN6Memory16rootHeapIteratorE);
PROVIDE(data_020a4d34 = _ZN6Memory25isRootHeapIterInitializedE);
PROVIDE(_ZN6Memory14defaultHeapPtrE = data_020a0ea0);
PROVIDE(data_020a0eac = data_020a0eac_c);
PROVIDE(data_0209b44c = data_0209b44c_c);
PROVIDE(data_0209f2d8 = data_0209f2d8_c);
PROVIDE(_Z5Crashv = Crash);

/* dBgPi ROM table contains D1/D0; use GCC address point after RTTI header. */
PROVIDE(data_02099368 = _ZTV5dBgPi + 8);
