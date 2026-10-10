/* Native form of ROM 0x02111314 (__sinit_d_a_obj_kb1_billboard.cpp, ov044).
 * The promoted TU spells it as a C++ global with a constructor, which GCC
 * would run once at program start; the overlay lifecycle must instead run it
 * at every load, after BSS clearing, like retail. Same calls as
 * src/__sinit_ov023_02111aa8.c (ROM-checked: file 0x622, dtor 0x02017ab4). */
extern void func_02017acc();
extern void func_020731dc();
extern int data_ov044_02111680[];
extern int func_02017ab4[];
extern int data_ov044_02111688[];
void __sinit_ov044_02111314(void)
{
    func_02017acc(data_ov044_02111680, 1570);
    func_020731dc(data_ov044_02111680, func_02017ab4, data_ov044_02111688);
}
