//@@REPL
static const char *fg_get_device_name(struct bq_fg_chip *bq)
{
	int ret;
	char data[8] = { 0 };
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (bq->fake_device_name[0])
		return bq->fake_device_name;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_DEVICE_NAME, data, 8);
	if (ret < 0) {
		mca_log_err("could not read device_name, ret=%d\n", ret);
		/* stock: "NULL" */
		memset(bq->device_name, 0, sizeof(bq->device_name));
		memcpy(bq->device_name, "NULL", 4);
		return bq->device_name;
	}

	data[7] = 0;
	memset(bq->device_name, 0, sizeof(bq->device_name));
	/* stock: platform_version == 3 keeps the first 5 bytes, every other platform bytes 3..6 of block 0x004A */
	if (hwid && hwid->platform_version == 3)
		memcpy(bq->device_name, data, 5);
	else
		memcpy(bq->device_name, data + 3, 4);
	bq->device_name[7] = 0;

	mca_log_info("0x004A: %.*s, model_name: %.*s\n", (int)strnlen(data, 7), data,
		     (int)strnlen(bq->device_name, 7), bq->device_name);

	return bq->device_name;
}
