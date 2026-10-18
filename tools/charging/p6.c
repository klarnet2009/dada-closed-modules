//@@REPL
static int fg_read_rsoc(struct bq_fg_chip *bq, int *rsoc)
{
	int ret, val;
	u16 soc = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
	if (ret < 0) {
		mca_log_info("could not read RSOC, ret = %d\n", ret);
		return ret;
	}

	if (bq->ota_updating) {
		val = bq->batt_rsoc;
	} else {
		bq->batt_rsoc = soc;
		val = soc;
	}

	*rsoc = val;
	return 0;
}
//@@REPL
static void fg_get_ota_update_flag(void *data, unsigned int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	unsigned int flag = bq->unk_1164;

	/* GUESS: the member at +1164 (unk_1164) is the OTA update flag, from the function name and the log text */
	*val = flag;
	mca_log_info("get ota update_flag: %x\n", flag);
}
