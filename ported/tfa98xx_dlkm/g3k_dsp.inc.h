/*
 * g3k: functions of the Xiaomi stock tfa98xx_dlkm (driver v6.13._xiaomi) that the v6.5.3 template lacks,
 * written from the disassembly. Included at the end of tfa_dsp.c.
 * "g3k-guess" marks parts that are not verified against the stock bytes.
 */


/* wait until the bitfield bf reaches (>=) value, polling at most `tries` times; -ETIME on timeout */
int tfa_wait4manstate(struct tfa_device *tfa, uint16_t bf, uint16_t value, int tries)
{
	int len = (bf & 0x0f) + 1;
	int pos = (bf >> 4) & 0x0f;
	uint16_t mask = (uint16_t)(~(0xffffffffu << len)) << pos;
	int left = tries, bfval;
	unsigned short regval;
	enum Tfa98xx_Error err;

	do {
		regval = 0;
		if (tfa->dev_ops.tfa_reg_read) {
			err = tfa->dev_ops.tfa_reg_read(tfa, (bf >> 8) & 0xff, &regval);
			if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail)
				pr_err("Error getting bf :%d \n", -100 - (int)err);
		}
		bfval = (regval & mask) >> pos;
		if (bfval >= (int)value)
			break;
	} while (--left);

	if (!left) {
		pr_err("timeout waiting for bitfield:0x%04x, value:%d, %d times\n", bf, value, tries);
		return -62; /* -ETIME */
	}

	return 0;
}

/* the four TDM bitfield ids (enable, slots, bit width, ...) depend on the device revision */
void tfa_dev_get_tdm_add(struct tfa_device *tfa, uint16_t *a, uint16_t *b, uint16_t *c, uint16_t *d)
{
	switch (tfa->rev & 0xff) {
	case 0x73:
	case 0x78:
		*a = 0x2000; *b = 0x20c3; *c = 0x2144; *d = 0x2224;
		break;
	case 0x72:
	case 0x74:
		*a = 0x2000; *b = 0x2103; *c = 0x2204; *d = 0x22a4;
		break;
	case 0x13:
		*a = 0x2040; *b = 0x20c3; *c = 0x2144; *d = 0x2224;
		break;
	default:
		pr_err("unsuported device rev:%x\n", (unsigned int)(tfa->rev & 0xffff));
		return;
	}
}

static void g3k_rmw_reg(struct tfa_device *tfa, unsigned char reg, unsigned short keep, unsigned short set)
{
	unsigned short v = 0;
	enum Tfa98xx_Error err;

	if (tfa->dev_ops.tfa_reg_read) {
		err = tfa->dev_ops.tfa_reg_read(tfa, reg, &v);
		if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail)
			pr_err("Error getting bf :%d \n", -100 - (int)err);
	}
	if ((v & keep) | set) {
		unsigned short nv = (v & keep) | set;

		if (nv != v && tfa->dev_ops.tfa_reg_write) {
			err = tfa->dev_ops.tfa_reg_write(tfa, reg, nv);
			if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail)
				pr_err("Error setting bf :%d \n", -100 - (int)err);
		}
	}
}

int tfa_dev_set_tdm_bitwidth(struct tfa_device *tfa, int bitwidth)
{
	int slot, shift_a, shift_b;

	if (bitwidth == 32 || bitwidth == 24) {
		slot = 2;
		shift_a = 0x1f;
		shift_b = 0x17;
		if (tfa->tfa_family != 1 && tfa->is_probus_device == 1)
			shift_b = 0x1f;
	} else if (bitwidth == 16) {
		slot = 0;
		shift_a = 0xf;
		shift_b = 0xf;
	} else {
		pr_err("unsupported tdm bitwidth:%d\n", bitwidth);
		return -EINVAL;
	}

	if (tfa->tfa_family == 2) {
		uint16_t a = 0, b = 0, c = 0, d = 0;

		tfa_dev_get_tdm_add(tfa, &a, &b, &c, &d);
		tfa_set_bf(tfa, a, 0);
		tfa_set_bf(tfa, b, slot);
		tfa_set_bf(tfa, c, shift_a);
		tfa_set_bf(tfa, d, shift_b);
		tfa_set_bf(tfa, a, 1);
		return 0;
	}

	/* tfa1 family: only when the device is a TDM one (g3k-guess: daimap == 2) */
	if (tfa->daimap != Tfa98xx_DAI_TDM)
		return 0;
	g3k_rmw_reg(tfa, 0x10, 0xfff7, 0);
	g3k_rmw_reg(tfa, 0x14, 0x0fff, slot << 12);
	g3k_rmw_reg(tfa, 0x11, 0xfe0f, shift_a << 4);
	g3k_rmw_reg(tfa, 0x10, 0x83ff, shift_b << 10);
	g3k_rmw_reg(tfa, 0x10, 0xffff, 0x8);

	return 0;
}

/* ---- interrupt report (tfa986x / tfa987x) ---- */
struct g3k_irq_name { int id; const char *name; };

static const struct g3k_irq_name Tfa9878IrqNames[] = {
	{ 0, "STVDDS" }, { 1, "STBSTOC" }, { 2, "STOTDS" }, { 3, "STOCPR" }, { 4, "STUVDS" },
	{ 5, "STMANALARM" }, { 6, "STTDMER" }, { 7, "STNOCLK" }, { 8, "STBODNOK" }, { 9, "9" },
};

static const char * const tfa987x_irq_info[] = {
	"Power on reset", "Overcurrent booster", "Overtemperature", "Overcurrent amp", "Undervoltage",
	"Hw mgr alarm", "TDM error", "Lost clock", "Brown out VDDD",
};

/* the stock names this table in .data (exported symbol layout) */
struct g3k_irq_name tfa986x_irq_names[] = {
	{ 0, "STVDDS" }, { 1, "STBSTOC" }, { 2, "STOTDS" }, { 3, "STOCPR" }, { 4, "STUVDS" },
	{ 5, "STTDMER" }, { 6, "STNOCLK" }, { 7, "STDCTH" }, { 8, "STBODNOK" }, { 9, "STCOOR" },
	{ 10, "STOVDS" }, { 11, "STQPFAIL" }, { 12, "12" },
};

static const char * const tfa986x_irq_info[] = {
	"Power on reset", "Overcurrent booster", "Overtemperature", "Overcurrent amp", "Undervoltage",
	"TDM error", "Lost clock", "DC too high amp", "Brown out VDDD", "Clock out of range",
	"Overvoltage protection", "Qpump fail",
};

void tfa_irq_init(struct tfa_device *tfa)
{
	uint16_t saved;

	if (!tfa->dev_ops.tfa_reg_write)
		return;

	saved = tfa->interrupt_enable[0];
	tfa->dev_ops.tfa_reg_write(tfa, 0x48, 0);	/* disable all interrupts */
	if (tfa->dev_ops.tfa_reg_write)
		tfa->dev_ops.tfa_reg_write(tfa, 0x44, 0xffff);	/* clear all */
	if (tfa->dev_ops.tfa_reg_write)
		tfa->dev_ops.tfa_reg_write(tfa, 0x48, saved);	/* restore enables */

}

int tfa_irq_report(struct tfa_device *tfa)
{
	unsigned short status = 0;
	unsigned int pending;
	enum Tfa98xx_Error err;
	int i, nirq;
	const struct g3k_irq_name *names;
	const char * const *info;

	/* the 987x chips have get_mtpb, the stock 986x/9865 ops leave it NULL */
	if (tfa->dev_ops.get_mtpb) {
		nirq = 9; names = Tfa9878IrqNames; info = tfa987x_irq_info;
	} else {
		nirq = 12; names = tfa986x_irq_names; info = tfa986x_irq_info;
	}

	if (tfa->dev_ops.tfa_reg_read) {
		err = tfa->dev_ops.tfa_reg_read(tfa, 0x40, &status);
		if (err != Tfa98xx_Error_Ok && err != Tfa98xx_Error_Fail)
			return -100 - (int)err;
	}

	pending = status & tfa->interrupt_enable[0];
	for (i = 0; i < nirq; i++)
		if (pending & (1u << i))
			pr_err("device[%d] interrupt: %s %s\n", tfa->dev_idx, names[i].name, info[i]);

	if (tfa->dev_ops.tfa_reg_write) {
		tfa->dev_ops.tfa_reg_write(tfa, 0x48, 0);
		if (tfa->dev_ops.tfa_reg_write)
			tfa->dev_ops.tfa_reg_write(tfa, 0x44, pending);
	}

	return 0;
}
