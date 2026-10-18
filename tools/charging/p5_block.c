/* G13q: structure from the stock: no retry loop (a single i2c_transfer), NULL / adapter checks, message "I2C transfer failed" (the stock inlines
 * __fg_read_block into its callers, __fg_write_block stays a function) */
static __always_inline int __fg_read_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msgs[2];
	int ret;

	if (!buf)
		return -EINVAL;
	if (!client || !client->adapter)
		return -ENODEV;

	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = buf;

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		mca_log_err("I2C transfer failed, ret=%d\n", ret);

	return ret;
}

static noinline int __fg_write_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	struct i2c_msg msg = { 0 };
	u8 *wbuf;
	int ret;

	if (!buf)
		return -EINVAL;
	if (!client || !client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.len = len + 1;
	wbuf = kmalloc(len + 1, GFP_KERNEL);
	msg.buf = wbuf;
	if (!wbuf)
		return -ENOMEM;

	wbuf[0] = reg;
	memcpy(wbuf + 1, buf, len);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		mca_log_err("I2C transfer failed, ret=%d\n", ret);
	kfree(wbuf);

	return ret;
}

