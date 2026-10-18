//@@REPL
static int fg_read_byte(struct bq_fg_chip *bq, u8 reg, u8 *val)
{
	int ret;

	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_read_byte(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}
//@@REPL
static int fg_read_word(struct bq_fg_chip *bq, u8 reg, u16 *val)
{
	int ret;

	/* G13q, from the stock: no mutex_lock; give up (ret 0, *val untouched) as soon as the gauge OTA flag (+1156) is set */
	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_read_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}
//@@REPL
static int fg_read_rsoc(struct bq_fg_chip *bq, int *rsoc)
{
	int ret;
	u16 soc = 0;

	if (!bq->ota_updating) {
		ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
		if (ret < 0) {
			mca_log_info("could not read RSOC, ret = %d\n", ret);
			return ret;
		}
		if (!bq->ota_updating)
			bq->batt_rsoc = soc;
	}

	*rsoc = bq->batt_rsoc;
	return 0;
}
//@@REPL
static int fg_read_temperature(struct bq_fg_chip *bq, int *temp)
{
	int ret;
	u16 temp_now = 0;

	if (bq->fake_temp != FG_FAKE_TEMP_NONE) {
		*temp = bq->fake_temp;
		return 0;
	}

	if (bq->fg_error) {
		*temp = FG_ERROR_FAKE_TEMP;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TEMP], &temp_now);
	if (ret < 0) {
		mca_log_info("could not read temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_temp(bq, (u8 *)&temp_now);
	return 0;
}
//@@REPL
static int fg_read_volt(struct bq_fg_chip *bq, int *vbat)
{
	int ret;
	u16 volt = 0;

	if (bq->fg_error)
		return 3700;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_VOLT], &volt);
	if (ret < 0) {
		mca_log_err("could not read volt ret = %d\n", ret);
		return ret;
	}

	*vbat = fg_convert_bytes_to_volt(bq, (u8 *)&volt);
	return 0;
}
//@@REPL
static int fg_convert_bytes_to_volt(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int volt = value;

	if (volt < FG_VOLT_LOW_TH || volt > FG_VOLT_HIGH_TH || fg->ota_updating) {
		mca_log_err("volt out of range: %d\n", volt);
		volt = fg->batt_volt;
	}

	if (!volt && fg->batt_volt > FG_VOLT_LOW_TH)
		volt = fg->batt_volt;

	fg->batt_volt = volt;
	mca_log_debug("volt: %d\n", fg->batt_volt);
	return fg->batt_volt;
}
//@@REPL
static int fg_convert_bytes_to_curr(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 temp_curr = BYTES_TO_U16(bytes);
	int curr;

	if (temp_curr > 32768)
		curr = -1 * (temp_curr - 65536);
	else
		curr = -1 * temp_curr;

	if (curr < FG_CURR_LOW_TH || curr > FG_CURR_HIGH_TH || fg->ota_updating) {
		mca_log_err("current out of range: %d\n", curr);
		curr = fg->batt_curr / 1000;
	}

	fg->batt_curr = curr * 1000;
	mca_log_debug("temp_curr: %d, curr: %d\n", temp_curr, fg->batt_curr);
	return fg->batt_curr;
}
//@@REPL
static int fg_convert_bytes_to_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH || fg->ota_updating) {
		mca_log_err("temperature out of range: %d\n", temp);
		temp = fg->batt_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->batt_temp = temp;
	return fg->batt_temp;
}
//@@REPL
static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int rm = value * 1000;

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
	int fcc = value * 1000;

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
static int fg_convert_bytes_to_cycle_count(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int cnt = value;

	if (fg->fake_cycle)
		cnt = fg->fake_cycle;
	else if (fg->ota_updating)
		cnt = fg->batt_cycle_count;

	fg->batt_cycle_count = cnt;
	return fg->batt_cycle_count;
}
//@@REPL
static int fg_read_current(struct bq_fg_chip *bq, int *curr)
{
	int ret;
	u16 temp_curr = 0;

	if (bq->fg_error) {
		*curr = -500 * 1000;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CN], &temp_curr);
	if (ret < 0) {
		mca_log_err("could not read current, ret = %d\n", ret);
		*curr = 0;
		return ret;
	}
	*curr = fg_convert_bytes_to_curr(bq, (u8 *)&temp_curr);

	return 0;
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

	if (bq->fake_fcc) {
		*fcc = bq->fake_fcc;
		return 0;
	}

	*fcc = fg_convert_bytes_to_fcc(bq, (u8 *)&data);
	return 0;
}
//@@REPL
static int fg_read_dc(struct bq_fg_chip *bq, int *dc)
{
	int ret;
	u16 tmp = 0;

	if (bq->regs[BQ_FG_REG_DC] == INVALID_REG_ADDR) {
		mca_log_info("DesignCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_DC], &tmp);
	if (ret < 0) {
		mca_log_info("could not read DC, ret=%d\n", ret);
		return -1;
	}

	*dc = tmp * 1000;
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

	if (bq->fake_rm) {
		*rm = bq->fake_rm;
		return 0;
	}

	*rm = fg_convert_bytes_to_rm(bq, (u8 *)&data);
	return 0;
}
//@@REPL
static int fg_read_cyclecount(struct bq_fg_chip *bq, int *cycle_count)
{
	int ret;
	u16 cc = 0;

	if (bq->fake_cycle) {
		*cycle_count = bq->fake_cycle;
		return 0;
	}

	if (bq->regs[BQ_FG_REG_CC] == INVALID_REG_ADDR) {
		mca_log_info("Cycle Count not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CC], &cc);
	if (ret < 0) {
		mca_log_info("could not read Cycle Count, ret=%d\n", ret);
		return ret;
	}

	*cycle_count = fg_convert_bytes_to_cycle_count(bq, (u8 *)&cc);
	return 0;
}
//@@REPL
static int fg_read_avercurrent(struct bq_fg_chip *bq, int *avercurrent)
{
	int ret = 0;
	u16 curr = 0;
	int val = 0;

	if (!bq->ota_updating) {
		ret = fg_read_word(bq, bq->regs[BQ_FG_REG_AVER_CURRENT], &curr);
		if (ret < 0)
			return ret;
		val = curr > 32768 ? (-1 * (curr - 65536) * 1000) : (-1 * curr * 1000);
	}

	*avercurrent = val;
	return ret;
}
//@@REPL
static int fg_read_soh(struct bq_fg_chip *bq, int *soh)
{
	int ret, i, cutoff, base;
	u16 tmp = 0;
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
