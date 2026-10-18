//@@NEW fg_read_one_probe_ok
static int fg_read_packvolt(struct bq_fg_chip *bq, int *vbat)
{
	int ret;
	u16 volt = 0;

	if (bq->fg_error)
		return 3700;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_PACK_BATT], &volt);
	if (ret < 0) {
		mca_log_err("could not read volt ret = %d\n", ret);
		return ret;
	}

	*vbat = fg_convert_bytes_to_volt(bq, (u8 *)&volt);
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_convert_bytes_to_original_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH || fg->ota_updating) {
		mca_log_err("temperature out of range: %d\n", temp);
		temp = fg->batt_ori_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->batt_ori_temp = temp;
	mca_log_info("temperature: %d\n", temp);
	return fg->batt_ori_temp;
}
//@@NEW fg_read_one_probe_ok
static int fg_read_original_temperature(struct bq_fg_chip *bq, int *temp)
{
	int ret;
	u16 temp_now = 0;

	if (bq->fg_error) {
		*temp = FG_ERROR_FAKE_TEMP;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_ORIGINAL_TEMP], &temp_now);
	if (ret < 0) {
		mca_log_info("could not read temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_original_temp(bq, (u8 *)&temp_now);
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_vsiloss(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (bq->fake_v_siloss) {
		*val = bq->fake_v_siloss;
		return 0;
	}

	ret = fg_mac_read_block(bq, 0xf0, buf, 32);
	if (ret < 0) {
		mca_log_err("could not read vsiloss = %d\n", ret);
		return ret;
	}

	*val = (buf[1] << 8) | buf[0];
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_iaver(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (bq->fake_i_aver) {
		*val = bq->fake_i_aver;
		return 0;
	}

	ret = fg_mac_read_block(bq, 0xd8, buf, 32);
	if (ret < 0) {
		mca_log_err("could not read i_aver = %d\n", ret);
		return ret;
	}

	*val = (s16)((buf[3] << 8) | buf[2]);
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_fast_mode_times(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (!val) {
		mca_log_err("fast_mode_times pointer is NULL\n");
		return -EINVAL;
	}

	ret = fg_mac_read_block(bq, 0xe5, buf, 32);
	if (ret < 0) {
		mca_log_err("could not read fast_mode_times = %d\n", ret);
		return ret;
	}

	*val = (buf[1] << 8) | buf[0];
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_fcc_default(void *data, bool *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	*val = bq->enable_fcc_default;
	return 0;
}
//@@NEW fg_read_one_probe_ok
static void fg_get_pack_vendor(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	*val = bq->pack_vendor;
}
//@@NEW fg_read_one_probe_ok
static void fg_get_ota_update_flag(void *data, unsigned int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	/* GUESS: the member at +1164 (unk_1164) is the OTA update flag, from the function name and the log text */
	*val = bq->unk_1164;
	mca_log_info("get ota update_flag: %x\n", bq->unk_1164);
}
//@@NEW fg_read_one_probe_ok
static void fg_get_one_average_current(void *data, int *val)
{
	*val = fg_get_average_current((struct bq_fg_chip *)data);
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_iaver(void *data, int *val)
{
	if (!data || !val)
		return -EINVAL;
	return fg_get_iaver((struct bq_fg_chip *)data, val);
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_vsiloss(void *data, int *val)
{
	if (!data || !val)
		return -EINVAL;
	return fg_get_vsiloss((struct bq_fg_chip *)data, val);
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_fg_vendor(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (!bq || !val)
		return -EINVAL;
	*val = bq->fg_vendor;
	return 0;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_batt_qmax(void *data, int *val)
{
	if (data && val)
		return fg_read_qmax((struct bq_fg_chip *)data, (u32 *)val);

	mca_log_err("fg_get_one_batt_qmax err\n");
	return -EINVAL;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_csd_flag(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	u8 buf[64] = { 0 };
	int ret;

	if (!bq || !val) {
		mca_log_err("fg_get_one_csd_flag err\n");
		return -EINVAL;
	}

	ret = fg_mac_read_block(bq, 0x10a2, buf, 8);
	if (ret < 0)
		return ret;
	*val = buf[1];
	return ret;
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_manufacturing_date(void *data, unsigned char *buf)
{
	return fg_read_manufacturing_date((struct bq_fg_chip *)data, buf);
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_first_usage_date(void *data, unsigned char *buf)
{
	return fg_read_first_usage_date((struct bq_fg_chip *)data, buf);
}
//@@NEW fg_read_one_probe_ok
static int fg_get_one_fast_mode_times(void *data, int *val)
{
	return fg_get_fast_mode_times((struct bq_fg_chip *)data, val);
}
//@@NEW fg_read_one_probe_ok
static int fg_read_one_original_temperature(void *data, int *val)
{
	return fg_read_original_temperature((struct bq_fg_chip *)data, val);
}
