/*
 * g3n: aw_dev_init_cmm of the stock (185 insns), GENERATED from the disassembly (work/re/src/g3n_tools/cmmgen.py): a pure
 * sequence of stores into the NEW struct aw_device of the stock firmware driver: descriptors {reg, ~mask, value ...} of the
 * bitfields "btn pwd", "btn amppd", "btn hmute", "btn uls humte", "btn bop", "btn noise gate", "btn none psm/mpd/dsmzth/dither"
 * (name pointers at +0x110/+0x128/+0x140/+0x158/+0x3b0/+0x3e0/+0x3f8/+0x410/+0x428/+0x3c8), the ef_lock-like items, 0xff markers and
 * the three ops at +0x480/+0x488/+0x490 (reg_val_to_db, db_val_to_reg, get_irq_type).
 * OUR struct aw_device does NOT have the stock layout, so the stores go to the overlay buffer aw_dev->g3n_cmm[] (same offsets,
 * same values); nothing reads it yet. Meaning of the individual words is NOT recovered (the word layout of a descriptor is a guess).
 */
#define CMM_SZ 0x498
#define CMM64(o, v) put_unaligned((u64)(v), (u64 *)(p + (o)))
#define CMM32(o, v) put_unaligned((u32)(v), (u32 *)(p + (o)))
#define CMM16(o, v) put_unaligned((u16)(v), (u16 *)(p + (o)))
#define CMM_PTR(o, s) put_unaligned((unsigned long)(s), (unsigned long *)(p + (o)))
#define CMM_FN(o, f) put_unaligned((unsigned long)(f), (unsigned long *)(p + (o)))

void aw_dev_init_cmm(struct aw_device *aw_dev)
{
	u8 *p = aw_dev->g3n_cmm;

	CMM_FN(0x480, aw_direct_reg_val_to_db); /* .text+0x10af8 */
	CMM_FN(0x488, aw_direct_db_val_to_reg); /* .text+0x10b00 */
	CMM_FN(0x490, aw_get_irq_type); /* .text+0x10b08 */
	CMM64(0xf0, 0x200000003);
	CMM64(0xf8, 0xffff0000ffff);
	CMM16(0x90, 0x0);
	CMM64(0x80, 0x0);
	CMM64(0x88, 0x0);
	CMM64(0x100, 0xffffff7f00000004);
	CMM64(0x108, 0x8000000000);
	CMM64(0x70, 0x0);
	CMM64(0x78, 0x0);
	CMM_PTR(0x110, "btn pwd"); /* .rodata+0x6456 */
	CMM64(0x118, 0xfffffffe00000004);
	CMM64(0x3f0, 0x1);
	CMM64(0x60, 0x0);
	CMM64(0x68, 0x0);
	CMM64(0x130, 0xfffffffd00000004);
	CMM64(0x138, 0x2);
	CMM64(0x120, 0x1);
	CMM_PTR(0x128, "btn amppd"); /* .rodata+0x1345 */
	CMM64(0x248, 0x0);
	CMM_PTR(0x140, "btn hmute"); /* .rodata+0x18ff */
	CMM64(0x148, 0xfffffeff00000004);
	CMM_PTR(0x3e0, "btn noise gate"); /* .rodata+0x58d */
	CMM32(0x224, 0x0);
	CMM64(0x160, 0xffff7fff00000004);
	CMM64(0x168, 0x8000);
	CMM64(0x150, 0x100);
	CMM_PTR(0x158, "btn uls humte"); /* .rodata+0x2f34 */
	CMM32(0x400, 0xff);
	CMM32(0x418, 0xff);
	CMM64(0x170, 0xffffdfff00000004);
	CMM64(0x178, 0x2000);
	CMM32(0x430, 0xff);
	CMM64(0x1d8, 0xffffb4c400000001);
	CMM64(0x1e0, 0x31100000211);
	CMM32(0x1e8, 0x11);
	CMM32(0x3d0, 0xff);
	CMM64(0x39c, 0xfffff3ff00000006);
	CMM32(0x1cc, 0xff);
	CMM64(0x3a4, 0x80000000400);
	CMM32(0x184, 0xff);
	CMM64(0x1ec, 0xffffdfff0000001f);
	CMM64(0x1f4, 0x200000000000);
	CMM32(0x200, 0x1789);
	CMM32(0x1fc, 0x21);
	CMM64(0x208, 0xfffffdff00000022);
	CMM32(0x210, 0xfc00);
	CMM64(0x214, 0xffff0fff00000061);
	CMM32(0x204, 0x3ff);
	CMM64(0x22c, 0x3ff);
	CMM32(0x38, 0x40);
	CMM64(0x21c, 0xfffffc0000000005);
	CMM32(0x1a8, 0xff);
	CMM64(0x3b8, 0xffffefff00000004);
	CMM64(0x3c0, 0x1000);
	CMM_PTR(0x3b0, "btn bop"); /* .rodata+0x377b */
	CMM64(0x388, 0x55aa00000000);
	CMM32(0x190, 0xff);
	CMM64(0x3e8, 0xfffffffe0000005e);
	CMM_PTR(0x3f8, "btn none psm"); /* .rodata+0x5e87 */
	CMM_PTR(0x410, "btn none mpd"); /* .rodata+0x34f1 */
	CMM_PTR(0x428, "btn none dsmzth"); /* .rodata+0x59c */
	CMM_PTR(0x3c8, "btn none dither"); /* .rodata+0x2548 */
	CMM32(0x1b4, 0xff);
	CMM64(0xa0, 0x7800000002);
	CMM64(0xa8, 0x8000ffff7fff);
	CMM32(0xcc, 0xff);
	CMM32(0xdc, 0xff);
	CMM64(0xb0, 0xffff7fff0000007c);
	CMM32(0xb8, 0x8000);
	CMM32(0xbc, 0xff);
}
