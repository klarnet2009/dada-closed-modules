/*
 * g3k: chip support added from the disassembly of the Xiaomi stock tfa98xx_dlkm (driver v6.13._xiaomi):
 * tfa9878 (register tables identical to the Samsung SM-T545 v6.6.3 tfa_init.c), tfa9875, tfa9865, tfa986x.
 * Included at the end of tfa_init.c (needs its static helpers).
 * UNVERIFIED parts are marked "g3k-guess".
 */

/* numeric bitfield ids taken from the stock disassembly (field-name headers of these chips are not available) */
#define G3K_BF_SWPROFIL		0xe00f
#define G3K_BF_SWVSTEP		0xe10f
#define G3K_BF_OPENMTP		0x01e0
#define G3K_TFA9878_BF_CLKS	0x1060
#define G3K_TFA9878_BF_MTPB	0x1070
#define G3K_TFA9875_BF_CLKS	0x1100
#define G3K_TFA9875_BF_MTPB	0x1110
#define G3K_TFA9875_BF_PHASE	0x58c0

static int g3k_set_swprofile(struct tfa_device *tfa, unsigned short new_value)
{
	int active_value = tfa_dev_get_swprof(tfa);

	tfa->profile = new_value - 1;
	tfa_set_bf_volatile(tfa, G3K_BF_SWPROFIL, new_value);

	return active_value;
}

static int g3k_get_swprofile(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, G3K_BF_SWPROFIL) - 1;
}

static int g3k_set_swvstep(struct tfa_device *tfa, unsigned short new_value)
{
	tfa->vstep = new_value - 1;
	tfa_set_bf_volatile(tfa, G3K_BF_SWVSTEP, new_value);

	return new_value;
}

static int g3k_get_swvstep(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, G3K_BF_SWVSTEP) - 1;
}

/* the stock keeps one copy per chip (identical bodies) */
#define G3K_SWFUNCS(chip) \
static int chip##_set_swprofile(struct tfa_device *tfa, unsigned short v) { return g3k_set_swprofile(tfa, v); } \
static int chip##_get_swprofile(struct tfa_device *tfa) { return g3k_get_swprofile(tfa); } \
static int chip##_set_swvstep(struct tfa_device *tfa, unsigned short v) { return g3k_set_swvstep(tfa, v); } \
static int __used chip##_get_swvstep(struct tfa_device *tfa) { return g3k_get_swvstep(tfa); }

/* ---------------------------------- TFA9878 ---------------------------------- */
G3K_SWFUNCS(tfa9878)

static enum Tfa98xx_Error tfa9878_faim_protect(struct tfa_device *tfa, int status)
{
	/* 0b = FAIM protection enabled 1b = FAIM protection disabled */
	return tfa_set_bf_volatile(tfa, G3K_BF_OPENMTP, (uint16_t)status);
}

static enum Tfa98xx_Error tfa9878_dsp_system_stable(struct tfa_device *tfa, int *ready)
{
	*ready = tfa_get_bf(tfa, G3K_TFA9878_BF_CLKS) == 1;

	return Tfa98xx_Error_Ok;
}

static int tfa9878_get_mtpb(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, G3K_TFA9878_BF_MTPB);
}

static enum Tfa98xx_Error tfa9878_specific(struct tfa_device *tfa)
{
	enum Tfa98xx_Error error = Tfa98xx_Error_Ok;
	unsigned short value, xor;

	if (tfa->in_use == 0)
		return Tfa98xx_Error_NotOpen;

	/* Unlock key 1 and 2 */
	error = tfa_reg_write(tfa, 0x0F, 0x5A6B);
	error = tfa_reg_read(tfa, 0xFB, &value);
	xor = value ^ 0x005A;
	error = tfa_reg_write(tfa, 0xA0, xor);
	tfa98xx_key2(tfa, 0);

	switch (tfa->rev) {
	case 0x0a78:
		tfa_reg_write(tfa, 0x01, 0x2e18);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x04, 0x0240);
		tfa_reg_write(tfa, 0x52, 0x587c);
		tfa_reg_write(tfa, 0x61, 0x0183);
		tfa_reg_write(tfa, 0x63, 0x055a);
		tfa_reg_write(tfa, 0x65, 0x0542);
		tfa_reg_write(tfa, 0x71, 0x303e);
		tfa_reg_write(tfa, 0x83, 0x009a);
		break;
	case 0x1a78:
		tfa_reg_write(tfa, 0x01, 0x2e18);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x04, 0x0241);
		tfa_reg_write(tfa, 0x52, 0x587c);
		tfa_reg_write(tfa, 0x61, 0x0183);
		tfa_reg_write(tfa, 0x63, 0x055a);
		tfa_reg_write(tfa, 0x65, 0x0542);
		tfa_reg_write(tfa, 0x70, 0xb7ff);
		tfa_reg_write(tfa, 0x71, 0x303e);
		tfa_reg_write(tfa, 0x83, 0x009a);
		tfa_reg_write(tfa, 0x84, 0x0211);
		tfa_reg_write(tfa, 0x8c, 0x0210);
		tfa_reg_write(tfa, 0xce, 0x2202);
		tfa_reg_write(tfa, 0xd5, 0x0000);
		tfa_irq_init(tfa);
		break;
	default:
		pr_info("\nWarning: Optimal settings not found for device with revid = 0x%x \n", tfa->rev);
		break;
	}

	return error;
}

void tfa9878_ops(struct tfa_device_ops *ops)
{
	tfa_set_ops_defaults(ops);

	ops->tfa_init = tfa9878_specific;
	ops->dsp_system_stable = tfa9878_dsp_system_stable;
	ops->faim_protect = tfa9878_faim_protect;
	ops->get_mtpb = tfa9878_get_mtpb;
	ops->set_swprof = tfa9878_set_swprofile;
	ops->get_swprof = tfa9878_get_swprofile;
	ops->set_swvstep = tfa9878_set_swvstep;
	ops->get_swvstep = tfa9878_get_swvstep;
}

/* ---------------------------------- TFA9875 ---------------------------------- */
G3K_SWFUNCS(tfa9875)

static enum Tfa98xx_Error tfa9875_faim_protect(struct tfa_device *tfa, int status)
{
	return tfa_set_bf_volatile(tfa, G3K_BF_OPENMTP, (uint16_t)status);
}

static enum Tfa98xx_Error tfa9875_dsp_system_stable(struct tfa_device *tfa, int *ready)
{
	*ready = tfa_get_bf(tfa, G3K_TFA9875_BF_CLKS) == 1;

	return Tfa98xx_Error_Ok;
}

static int tfa9875_get_mtpb(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, G3K_TFA9875_BF_MTPB);
}

static enum Tfa98xx_Error tfa9875_phase_shift(struct tfa_device *tfa)
{
	return tfa_set_bf_volatile(tfa, G3K_TFA9875_BF_PHASE, 1);
}

/* bitfields in registers 0x10 and 0x13 are written volatile */
static int g3k_set_bitfield_vol(struct tfa_device *tfa, uint16_t bf, uint16_t value)
{
	if (((bf >> 8) & 0xff) == 0x13 || ((bf >> 8) & 0xff) == 0x10)
		return tfa_set_bf_volatile(tfa, bf, value);

	return tfa_set_bf(tfa, bf, value);
}

static int tfa9875_set_bitfield(struct tfa_device *tfa, uint16_t bf, uint16_t value)
{
	return g3k_set_bitfield_vol(tfa, bf, value);
}

static enum Tfa98xx_Error tfa9875_specific(struct tfa_device *tfa)
{
	enum Tfa98xx_Error error = Tfa98xx_Error_Ok;
	unsigned short value = 0;

	if (tfa->in_use == 0)
		return Tfa98xx_Error_NotOpen;

	tfa_reg_write(tfa, 0x0F, 0x5A6B);
	tfa_reg_read(tfa, 0xFB, &value);
	error = tfa_reg_write(tfa, 0xA0, value ^ 0x005A);
	tfa98xx_key2(tfa, 0);

	switch (tfa->rev) {
	case 0x1a75:
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x51, 0x0020);
		tfa_reg_write(tfa, 0x53, 0x0336);
		tfa_reg_write(tfa, 0x58, 0x0210);
		tfa_reg_write(tfa, 0x5f, 0x0080);
		tfa_reg_write(tfa, 0x61, 0x0183);
		tfa_reg_write(tfa, 0x63, 0x056a);
		tfa_reg_write(tfa, 0x6f, 0x0385);
		tfa_reg_write(tfa, 0x70, 0xde5f);
		tfa_reg_write(tfa, 0x73, 0x0183);
		tfa_reg_write(tfa, 0x74, 0xd118);
		tfa_reg_write(tfa, 0x75, 0xd77a);
		tfa_reg_write(tfa, 0x83, 0x06de);
		tfa_reg_write(tfa, 0x85, 0x0380);
		tfa_reg_write(tfa, 0x87, 0x040a);
		tfa_reg_write(tfa, 0xd5, 0x004d);
		break;
	case 0x0a75:
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x53, 0x0237);
		tfa_reg_write(tfa, 0x58, 0x0210);
		tfa_reg_write(tfa, 0x5f, 0x0080);
		tfa_reg_write(tfa, 0x61, 0x0183);
		tfa_reg_write(tfa, 0x64, 0x4040);
		tfa_reg_write(tfa, 0x6f, 0x0083);
		tfa_reg_write(tfa, 0x70, 0xdedf);
		tfa_reg_write(tfa, 0x73, 0x0182);
		tfa_reg_write(tfa, 0x74, 0xd0f8);
		tfa_reg_write(tfa, 0x75, 0xd57a);
		tfa_reg_write(tfa, 0x83, 0x009a);
		tfa_reg_write(tfa, 0x85, 0x0380);
		tfa_reg_write(tfa, 0xd5, 0x004d);
		break;
	default:
		pr_info("\nWarning: Optimal settings not found for device with revid = 0x%x \n", tfa->rev);
		break;
	}

	return error;
}

/* status check of the tfa9875: STATUS_FLAG0 (0x1000) based, same flow as the other probus chips */
static enum Tfa98xx_Error tfa9875_tfa_status(struct tfa_device *tfa)
{
	int val;

	val = tfa_read_reg(tfa, 0x1000);
	if (val < 0)
		return -val;

	if (!tfa_get_bf_value(0x1020, val) || !tfa_get_bf_value(0x1030, val) ||
	    !tfa_get_bf_value(0x1010, val) || tfa_get_bf_value(0x1040, val) ||
	    tfa_get_bf_value(0x1050, val))
		pr_err("Misc errors detected: STATUS_FLAG0 = 0x%x\n", val & 0xffff);

	if (!tfa_get_bf_value(0x1020, val))
		tfa_set_bf_volatile(tfa, 0x1020, 1);
	if (!tfa_get_bf_value(0x1030, val))
		tfa_set_bf_volatile(tfa, 0x1030, 1);
	if (!tfa_get_bf_value(0x1010, val))
		tfa_set_bf_volatile(tfa, 0x1010, 1);
	if (tfa_get_bf_value(0x1040, val))
		tfa_set_bf_volatile(tfa, 0x1040, 1);
	if (tfa_get_bf_value(0x1050, val))
		tfa_set_bf_volatile(tfa, 0x1050, 1);

	if (!tfa_get_bf(tfa, 0x1100))
		pr_err("ERROR: CLKS is unstable\n");
	if (!tfa_get_bf(tfa, 0x1170))
		pr_err("ERROR: PLL not locked\n");
	if (tfa_get_bf(tfa, 0x1120) || tfa_get_bf(tfa, 0x1180))
		pr_err("TDM related errors: STATUS_FLAG1 = 0x%x\n", tfa_read_reg(tfa, 0x1120) & 0xffff);

	if (tfa_get_bf(tfa, 0x1300)) {
		pr_err("BODNOK error detected : STATUS_FLAG3 = 0x%x\n", tfa_read_reg(tfa, 0x1300) & 0xffff);
		tfa_set_bf_volatile(tfa, 0x1300, 1);
		return Tfa98xx_Error_Ok;
	}

	return Tfa98xx_Error_Ok;
}

void tfa9875_ops(struct tfa_device_ops *ops)
{
	tfa_set_ops_defaults(ops);

	ops->tfa_init = tfa9875_specific;
	ops->dsp_system_stable = tfa9875_dsp_system_stable;
	ops->phase_shift = tfa9875_phase_shift;
	ops->faim_protect = tfa9875_faim_protect;
	ops->get_mtpb = tfa9875_get_mtpb;
	ops->set_swprof = tfa9875_set_swprofile;
	ops->get_swprof = tfa9875_get_swprofile;
	ops->set_swvstep = tfa9875_set_swvstep;
	ops->get_swvstep = tfa9875_get_swvstep;
	ops->set_bitfield = tfa9875_set_bitfield;
	ops->tfa_status = tfa9875_tfa_status;
}

/* ---------------------- TFA9865 / TFA986x (no get_swvstep/get_mtpb) ---------------------- */
G3K_SWFUNCS(tfa9865)
G3K_SWFUNCS(tfa986x)

static enum Tfa98xx_Error tfa9865_dsp_system_stable(struct tfa_device *tfa, int *ready)
{
	*ready = tfa_get_bf(tfa, G3K_TFA9875_BF_CLKS) == 1;

	return Tfa98xx_Error_Ok;
}

static enum Tfa98xx_Error tfa986x_dsp_system_stable(struct tfa_device *tfa, int *ready)
{
	*ready = tfa_get_bf(tfa, G3K_TFA9875_BF_CLKS) == 1;

	return Tfa98xx_Error_Ok;
}

static int tfa9865_set_bitfield(struct tfa_device *tfa, uint16_t bf, uint16_t value)
{
	return g3k_set_bitfield_vol(tfa, bf, value);
}

static int tfa986x_set_bitfield(struct tfa_device *tfa, uint16_t bf, uint16_t value)
{
	return g3k_set_bitfield_vol(tfa, bf, value);
}

/* common preamble of the 986x/9865 init: reset-ish bitfields, wait for manager state, unlock keys */
static enum Tfa98xx_Error g3k_986x_unlock(struct tfa_device *tfa)
{
	unsigned short value = 0;

	tfa_set_bf(tfa, 0x0000, 0);
	tfa_set_bf(tfa, 0x0460, 0);
	tfa_wait4manstate(tfa, 0x1203, 1, 50);
	tfa_reg_write(tfa, 0x0F, 0x5A6B);
	tfa_reg_read(tfa, 0xFB, &value);
	tfa_reg_write(tfa, 0xA0, value ^ 0x005A);
	tfa98xx_key2(tfa, 0);

	return Tfa98xx_Error_Ok;
}

/* g3k-guess: revision dispatch and the 0x4c65 / 0x3c65 / 0x2a65 / 0xc65 register tables are read from the
 * stock disassembly but the fall-through structure between the revisions is not fully verified */
static enum Tfa98xx_Error tfa9865_specific(struct tfa_device *tfa)
{
	enum Tfa98xx_Error error = Tfa98xx_Error_Ok;

	if (tfa->in_use == 0)
		return Tfa98xx_Error_NotOpen;

	g3k_986x_unlock(tfa);

	switch (tfa->rev) {
	case 0x4c65:
		tfa_reg_write(tfa, 0x00, 0x8041);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x08, 0x01c2);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f5e);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x62, 0x05c6);
		break;
	case 0x3c65:
		tfa_reg_write(tfa, 0x00, 0xc041);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x08, 0x01c2);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f5e);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x62, 0x0580);
		tfa_reg_write(tfa, 0x63, 0x0194);
		tfa_reg_write(tfa, 0x68, 0x0820);
		tfa_reg_write(tfa, 0x74, 0x61a0);
		tfa_reg_write(tfa, 0x75, 0x9a00);
		tfa_reg_write(tfa, 0x78, 0x0001);
		tfa_reg_write(tfa, 0xd7, 0x0001);
		tfa_reg_write(tfa, 0xdd, 0x0006);
		break;
	case 0x0c65:
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x08, 0x01c2);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x62, 0x0580);
		tfa_reg_write(tfa, 0x63, 0x0194);
		fallthrough;
	case 0x0a65:
	case 0x2a65:
		tfa_reg_write(tfa, 0x00, 0xca41);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x08, 0x0002);
		tfa_reg_write(tfa, 0x52, 0x1400);
		tfa_reg_write(tfa, 0x59, 0x0001);
		tfa_reg_write(tfa, 0x5a, 0x36bc);
		tfa_reg_write(tfa, 0x5f, 0x0080);
		tfa_reg_write(tfa, 0x75, 0x9900);
		tfa_reg_write(tfa, 0xda, 0x0002);
		break;
	default:
		pr_info("\nWarning: Optimal settings not found for device with revid = 0x%x \n", tfa->rev);
		break;
	}

	tfa_set_bf(tfa, 0x0000, 1);
	tfa_wait4manstate(tfa, 0x1203, 0, 50);
	tfa_set_bf(tfa, 0x0460, 1);
	tfa_irq_init(tfa);

	return error;
}

/* g3o: the four revisions 0x100a64 0x110a64 0x110a65 0x200a65 are separate case blocks in the stock (separate compares),
 * identical in effect: the body is expanded four times so that the switch is not lowered to a range test */
#define G3O_986X_T3(tfa) do { \
tfa_reg_write(tfa, 0x00, 0xf241); \
tfa_reg_write(tfa, 0x02, 0x0628); \
tfa_reg_write(tfa, 0x08, 0x009a); \
tfa_reg_write(tfa, 0x50, 0xc000); \
tfa_reg_write(tfa, 0x54, 0xe0e0); \
tfa_reg_write(tfa, 0x5a, 0x5f5e); \
tfa_reg_write(tfa, 0x5b, 0x74e2); \
tfa_reg_write(tfa, 0x5c, 0xb02b); \
tfa_reg_write(tfa, 0x5f, 0x00a0); \
tfa_reg_write(tfa, 0x62, 0x0684); \
tfa_reg_write(tfa, 0x63, 0x80d4); \
tfa_reg_write(tfa, 0x65, 0x0c58); \
tfa_reg_write(tfa, 0x67, 0x006e); \
tfa_reg_write(tfa, 0x68, 0x0820); \
tfa_reg_write(tfa, 0x69, 0x0119); \
tfa_reg_write(tfa, 0x74, 0x6028); \
tfa_reg_write(tfa, 0x75, 0x1daa); \
tfa_reg_write(tfa, 0x78, 0x0001); \
tfa_reg_write(tfa, 0x7c, 0x1072); \
tfa_reg_write(tfa, 0xdd, 0x01b6); \
} while (0)

/* g3o: rebuilt from the stock disassembly (497 insns). The full revid is computed here from tfa->rev and the bitfields
 * 0xf3b1/0xf090/0xf4f0/0x0687 and kept in tfa->rev_ext (+0x20, NAME GUESSED); the register tables follow the stock, the
 * revision numbers include the 0x10000/0x100000/0x200000 bits of that computation. The meaning of the revisions is not
 * known (no public source); the stock returns the result of the 0xa0 register write. */
static enum Tfa98xx_Error tfa986x_specific(struct tfa_device *tfa)
{
	enum Tfa98xx_Error error;
	unsigned int revid;
	unsigned short value;

	if (tfa->in_use == 0)
		return Tfa98xx_Error_NotOpen;

	revid = tfa->rev;
	value = 0;
	tfa_set_bf(tfa, 0x0000, 0);
	tfa_set_bf(tfa, 0x0460, 0);
	tfa_wait4manstate(tfa, 0x1203, 1, 50);

	if ((revid & 0xff) == 0x66 && tfa_get_bf(tfa, 0xf3b1)) {
		revid -= 1;
		if (tfa_get_bf(tfa, 0xf090) == 0)
			revid |= 0x10000;
		else if (tfa_get_bf(tfa, 0xf4f0) == 0)
			revid |= 0x100000;
		revid -= (tfa_get_bf(tfa, 0xf4f0) != 0);
	}
	if ((revid & 0xff) == 0x66) {
		unsigned int m = tfa_get_bf(tfa, 0x0687) & 3;

		if (m == 1)
			revid += 0x100000;
		else if (m == 2)
			revid += 0x200000;
		else if (m == 3)
			revid += 0x300000;
	}
	tfa->rev_ext = revid;

	tfa_reg_write(tfa, 0x0F, 0x5A6B);
	tfa_reg_read(tfa, 0xFB, &value);
	error = tfa_reg_write(tfa, 0xA0, value ^ 0x005A);
	tfa98xx_key2(tfa, 0);

	if ((tfa->rev_ext & 0xfffe) == 0x3a64)
		tfa->rev_ext = 0x10000 | (tfa->rev_ext & 0xffff);

	switch (tfa->rev_ext) {
	case 0x1a64:
	case 0x11a64:
		tfa_reg_write(tfa, 0x00, 0xf241);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f5e);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x5f, 0x00a0);
		tfa_reg_write(tfa, 0x62, 0x05c4);
		tfa_reg_write(tfa, 0x63, 0x80d4);
		tfa_reg_write(tfa, 0x67, 0x0626);
		tfa_reg_write(tfa, 0x68, 0x0820);
		tfa_reg_write(tfa, 0x74, 0x60f0);
		tfa_reg_write(tfa, 0x75, 0x0e00);
		tfa_reg_write(tfa, 0x76, 0xbf00);
		tfa_reg_write(tfa, 0x78, 0x0001);
		tfa_reg_write(tfa, 0x7c, 0x10f2);
		tfa_reg_write(tfa, 0xcf, 0x2cd7);
		tfa_reg_write(tfa, 0xd7, 0x1000);
		tfa_reg_write(tfa, 0xdd, 0x0036);
		break;
	case 0x3a64:
	case 0x13a64:
		tfa_reg_write(tfa, 0x00, 0xf241);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f4c);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x5f, 0x00a0);
		tfa_reg_write(tfa, 0x62, 0x05c4);
		tfa_reg_write(tfa, 0x63, 0x80d4);
		tfa_reg_write(tfa, 0x67, 0x0066);
		tfa_reg_write(tfa, 0x68, 0x0820);
		tfa_reg_write(tfa, 0x74, 0x60f0);
		tfa_reg_write(tfa, 0x75, 0x0e00);
		tfa_reg_write(tfa, 0x76, 0xbf00);
		tfa_reg_write(tfa, 0x78, 0x0001);
		tfa_reg_write(tfa, 0x7c, 0x10f2);
		tfa_reg_write(tfa, 0xcf, 0x2cd7);
		tfa_reg_write(tfa, 0xd7, 0x1000);
		tfa_reg_write(tfa, 0xdd, 0x0036);
		break;
	case 0x11a65:
	case 0x101a65:
		tfa_reg_write(tfa, 0x00, 0xf241);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f5e);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x5f, 0x00a0);
		tfa_reg_write(tfa, 0x62, 0x05c4);
		tfa_reg_write(tfa, 0x63, 0x80d4);
		tfa_reg_write(tfa, 0x67, 0x0626);
		tfa_reg_write(tfa, 0x68, 0x0820);
		tfa_reg_write(tfa, 0x74, 0x60f0);
		tfa_reg_write(tfa, 0x75, 0x0e00);
		tfa_reg_write(tfa, 0x78, 0x0001);
		tfa_reg_write(tfa, 0x7c, 0x10f2);
		tfa_reg_write(tfa, 0xd7, 0x1000);
		tfa_reg_write(tfa, 0xdd, 0x0036);
		break;
	case 0x13a65:
	case 0x103a65:
		tfa_reg_write(tfa, 0x00, 0xf241);
		tfa_reg_write(tfa, 0x02, 0x0628);
		tfa_reg_write(tfa, 0x50, 0xc000);
		tfa_reg_write(tfa, 0x5a, 0x5f4c);
		tfa_reg_write(tfa, 0x5b, 0x74e2);
		tfa_reg_write(tfa, 0x5c, 0x302b);
		tfa_reg_write(tfa, 0x5f, 0x00a0);
		tfa_reg_write(tfa, 0x62, 0x05c4);
		tfa_reg_write(tfa, 0x63, 0x80d4);
		tfa_reg_write(tfa, 0x67, 0x0066);
		tfa_reg_write(tfa, 0x68, 0x0820);
		tfa_reg_write(tfa, 0x74, 0x60f0);
		tfa_reg_write(tfa, 0x75, 0x0e00);
		tfa_reg_write(tfa, 0x78, 0x0001);
		tfa_reg_write(tfa, 0x7c, 0x10f2);
		tfa_reg_write(tfa, 0xcf, 0x2cd7);
		tfa_reg_write(tfa, 0xd7, 0x1000);
		tfa_reg_write(tfa, 0xdd, 0x0036);
		break;
	case 0x100a64:
		G3O_986X_T3(tfa);
		break;
	case 0x110a64:
		G3O_986X_T3(tfa);
		break;
	case 0x110a65:
		G3O_986X_T3(tfa);
		break;
	case 0x200a65:
		G3O_986X_T3(tfa);
		break;
	default:
		pr_info("\nWarning: Optimal settings not found for device with revid = 0x%x \n", tfa->rev_ext);
		break;
	}
	pr_info("\nInfo: found  device with revid = 0x%x \n", tfa->rev_ext);

	tfa_set_bf(tfa, 0x0000, 1);
	tfa_wait4manstate(tfa, 0x1203, 0, 50);
	tfa_set_bf(tfa, 0x0460, 1);
	tfa->interrupt_enable[0] = 0x071e;
	tfa_irq_init(tfa);

	return error;
}

/* the stock 9865/986x ops do not use tfa_set_ops_defaults(): get_swvstep, get_mtpb and tfa_status stay NULL
 * (tfa_irq_report relies on get_mtpb == NULL to select the 986x irq table) */
static void g3k_986x_ops_common(struct tfa_device_ops *ops)
{
	ops->dsp_msg = tfa_dsp_msg_rpc;
	ops->dsp_msg_read = tfa_dsp_msg_read_rpc;
	ops->tfa_reg_read = tfa98xx_read_register16;
	ops->tfa_reg_write = tfa98xx_write_register16;
	ops->tfa_mem_read = tfa98xx_dsp_read_mem;
	ops->tfa_mem_write = tfa98xx_dsp_write_mem_word;
	ops->dsp_reset = tfa_dsp_reset;
	ops->dsp_write_tables = no_overload_function_available;
	ops->auto_copy_mtp_to_iic = no_overload_function_available2;
	ops->factory_trimmer = no_overload_function_available2;
	ops->phase_shift = no_overload_function_available2;
	ops->set_mute = tfa_set_mute_nodsp;
	ops->faim_protect = tfa_faim_protect;
	ops->set_osc_powerdown = tfa_set_osc_powerdown;
	ops->update_lpm = tfa_update_lpm;
}

void tfa9865_ops(struct tfa_device_ops *ops)
{
	g3k_986x_ops_common(ops);
	ops->tfa_init = tfa9865_specific;
	ops->dsp_system_stable = tfa9865_dsp_system_stable;
	ops->set_swprof = tfa9865_set_swprofile;
	ops->get_swprof = tfa9865_get_swprofile;
	ops->set_swvstep = tfa9865_set_swvstep;
	ops->set_bitfield = tfa9865_set_bitfield;
}

void tfa986x_ops(struct tfa_device_ops *ops)
{
	g3k_986x_ops_common(ops);
	ops->tfa_init = tfa986x_specific;
	ops->dsp_system_stable = tfa986x_dsp_system_stable;
	ops->set_swprof = tfa986x_set_swprofile;
	ops->get_swprof = tfa986x_get_swprofile;
	ops->set_swvstep = tfa986x_set_swvstep;
	ops->set_bitfield = tfa986x_set_bitfield;
}
