/*
 * g3k: generic helpers of the newer AWINIC SDK used by the stock aw882xx_dlkm (they replace the per-pid
 * aw_pid_<n>_* copies of the OnePlus snapshot). Included at the end of aw882xx_init.c.
 * Not wired into the pid tables (the per-pid functions of the template are still used).
 */

/* register value == dB value */
static unsigned int aw_direct_reg_val_to_db(unsigned int value)
{
	return value;
}

static unsigned int aw_direct_db_val_to_reg(unsigned int value)
{
	return value;
}

/* [9:6]: -6 dB steps, [5:0]: 0.125 dB steps (value * 8 per dB) */
static unsigned int aw_6_0P125_reg_val_to_db(unsigned int value)
{
	return (value >> 6) * 0x30 + (value & 0x3f);
}

static unsigned int aw_6_0P125_db_val_to_reg(unsigned int value)
{
	return ((value / 0x30) << 6) + (value % 0x30);
}

/* [x:4]: -6 dB steps, [3:0]: 0.5 dB steps */
static unsigned int aw_6_0P5_reg_val_to_db(unsigned int value)
{
	return (value >> 4) * 0xc + (value & 0xf);
}

static unsigned int aw_6_0P5_db_val_to_reg(unsigned int value)
{
	return ((value / 0xc) << 4) + (value % 0xc);
}

/* generic irq decoding: bit 14 UVLI, bit 11 BSTOCI, bit 3 OCDI, bit 1 OTHI of the status register */
static unsigned int aw_get_irq_type(struct aw_device *aw_dev, unsigned int value)
{
	unsigned int ret = INT_TYPE_NONE;

	if (value & BIT(14)) {
		aw_dev_info(aw_dev->dev, "UVLO: occur");
		ret |= INT_TYPE_UVLO;
	}
	if (value & BIT(11)) {
		aw_dev_info(aw_dev->dev, "BSTOCI: occur");
		ret |= INT_TYPE_BSTOC;
	}
	if (value & BIT(3)) {
		aw_dev_info(aw_dev->dev, "OCDI: occur");
		ret |= INT_TYPE_OCDI;
	}
	if (value & BIT(1)) {
		aw_dev_info(aw_dev->dev, "OTHI: occur");
		ret |= INT_TYPE_OTHI;
	}

	return ret;
}

/* g3k-guess: the stock variant for devices without boost has the same visible prefix; the tail was not compared */
static unsigned int aw_get_irq_nobst_type(struct aw_device *aw_dev, unsigned int value)
{
	unsigned int ret = INT_TYPE_NONE;

	if (value & BIT(14)) {
		aw_dev_info(aw_dev->dev, "UVLO: occur");
		ret |= INT_TYPE_UVLO;
	}
	if (value & BIT(11)) {
		aw_dev_info(aw_dev->dev, "BSTOCI: occur");
		ret |= INT_TYPE_BSTOC;
	}
	if (value & BIT(3)) {
		aw_dev_info(aw_dev->dev, "OCDI: occur");
		ret |= INT_TYPE_OCDI;
	}
	if (value & BIT(1)) {
		aw_dev_info(aw_dev->dev, "OTHI: occur");
		ret |= INT_TYPE_OTHI;
	}

	return ret;
}
