//@@REPL
static int fg_read_rsoc(struct bq_fg_chip *bq, int *rsoc)
{
	int ret;
	u16 soc = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
	if (ret < 0) {
		mca_log_info("could not read RSOC, ret = %d\n", ret);
		return ret;
	}

	if (!bq->ota_updating)
		bq->batt_rsoc = soc;

	*rsoc = bq->batt_rsoc;
	return 0;
}
//@@REPL
static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int rm;

	if (fg->fake_rm)
		return fg->fake_rm;

	rm = value * 1000;
	if (value > FG_RM_FCC_HIGH_TH || fg->ota_updating) {
		mca_log_err("rm out of range: %d\n", value);
		rm = fg->batt_rm;
	}

	if (!value && fg->batt_rm / 1000 > FG_RM_FCC_DEFAULT_TH)
		rm = fg->batt_rm;

	fg->batt_rm = rm;
	return fg->batt_rm;
}
//@@REPL
static int fg_convert_bytes_to_fcc(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int fcc;

	if (fg->fake_fcc)
		return fg->fake_fcc;

	fcc = value * 1000;
	fg->enable_fcc_default = false;
	if (value > FG_RM_FCC_HIGH_TH || fg->ota_updating) {
		mca_log_err("fcc out of range: %d\n", value);
		fcc = fg->batt_fcc;
	}

	if (!value && fg->batt_fcc > 0) {
		fcc = fg->batt_fcc;
	} else if (!value && !fg->batt_fcc) {
		fg->enable_fcc_default = true;
		fcc = FG_RM_FCC_DEFAULT_TH * 1000;
	}

	fg->batt_fcc = fcc;
	return fg->batt_fcc;
}
//@@REPL
static int fg_read_fcc(struct bq_fg_chip *bq, int *fcc)
{
	int ret;
	u16 data = 0;

	if (bq->regs[BQ_FG_REG_FCC] == INVALID_REG_ADDR) {
		mca_log_info("FCC command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_FCC], &data);
	if (ret < 0) {
		mca_log_info("could not read FCC ret %d\n", ret);
		return ret;
	}

	*fcc = fg_convert_bytes_to_fcc(bq, (u8 *)&data);
	return 0;
}
//@@REPL
static int fg_read_rm(struct bq_fg_chip *bq, int *rm)
{
	int ret;
	u16 data = 0;

	if (bq->regs[BQ_FG_REG_RM] == INVALID_REG_ADDR) {
		mca_log_info("RemainingCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_RM], &data);
	if (ret < 0) {
		mca_log_info("could not read DC, ret=%d\n", ret);
		return ret;
	}

	*rm = fg_convert_bytes_to_rm(bq, (u8 *)&data);
	return 0;
}
//@@REPL
static int fg_read_soh(struct bq_fg_chip *bq, int *soh)
{
	int cutoff = 0;
	u16 tmp = 0;
	int ret, i, base;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (bq->fake_soh != -1) {
		*soh = bq->fake_soh;
		return 0;
	}

	if (bq->regs[BQ_FG_REG_SOH] == INVALID_REG_ADDR) {
		mca_log_info("SOH command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOH], &tmp);
	if (ret < 0) {
		mca_log_info("could not read soh, ret=%d\n", ret);
		return ret;
	}

	/* stock: country_version (hwid+4) == 0 selects base_termv, else base_termv_gl */
	base = hwid->country_version == 0 ? bq->base_termv : bq->base_termv_gl;
	if (base) {
		cutoff = 0;
		fg_get_cutoff_voltage(bq, &cutoff);
		for (i = bq->soh_comp_cnt - 1; i >= 0; i--) {
			if (cutoff - base > bq->soh_delta_comp[i].soh_delta) {
				tmp = (u16)(tmp + bq->soh_delta_comp[i].soh_comp);
				if (tmp >= 100)
					tmp = 100;
				mca_log_err("dbcgx soh compensation, soh_comp = %d\n", bq->soh_delta_comp[i].soh_comp);
				break;
			}
		}
	}

	*soh = tmp;
	return 0;
}
