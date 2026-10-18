//@@REPL
static int fg_write_byte(struct bq_fg_chip *bq, u8 reg, u8 val)
{
	int ret;

	/* G13q, from the stock: trylock + usleep_range(40, 50), nothing is written (ret 0) while the gauge OTA flag is set */
	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_write_byte(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}
//@@REPL
static int fg_write_word(struct bq_fg_chip *bq, u8 reg, u16 val)
{
	int ret = 0;

	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_write_word(bq->client, reg, val);
	mutex_unlock(&bq->i2c_rw_lock);
	return ret;
}
//@@NEW fg_mac_write_block
static __always_inline int __fg_mac_write_block(struct bq_fg_chip *bq, u16 cmd, u8 *data, u8 len)
{
	int ret;
	u8 cksum;
	u8 t_buf[40];
	int i;
	bool secure_delay = false;

	if (cmd > 0x4000 || cmd == FG_MAC_CMD_UI_SOH || cmd == FG_MAC_CMD_CLEAR_COUNT_DATA)
		secure_delay = true;

	t_buf[0] = (u8)cmd;
	t_buf[1] = (u8)(cmd >> 8);
	for (i = 0; i < len; i++)
		t_buf[i+2] = data[i];

	/*write command/addr, data*/
	ret = __fg_write_block(bq->client, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, len + 2);
	if (ret < 0) {
		mca_log_err("wirte block failed %d %d\n", t_buf[0], t_buf[1]);
		return ret;
	}

	cksum = checksum(t_buf, len + 2);
	t_buf[0] = cksum;
	t_buf[1] = len + 4; /*buf length, cmd, CRC and len byte itself*/
	/*write checksum and length*/
	ret = __fg_write_block(bq->client, bq->regs[BQ_FG_REG_MAC_CHKSUM], t_buf, 2);
	if (secure_delay)
		msleep(100);

	return ret;
}
//@@REPL
static int fg_mac_write_block(struct bq_fg_chip *bq, u16 cmd, u8 *data, u8 len)
{
	int ret;

	/* DEVIATION from the stock (safe side): the stock has no length check (it relies on the array-bounds trap of t_buf[40]) */
	if (len > 32)
		return -1;

	/* G13q, from the stock: no data_lock, i2c_rw_lock by trylock + usleep_range(40, 50), nothing is written (ret 0) while ota_updating */
	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_mac_write_block(bq, cmd, data, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}
