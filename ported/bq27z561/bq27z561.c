// SPDX-License-Identifier: GPL-2.0
/*
 * bq27z561 fuel gauge driver
 *
 * Copyright (C) 2017 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/smartchg/smart_chg_class.h>
#include <linux/unistd.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_event.h>
#include <mca/strategy/strategy_class.h>
#include "hwid.h"
#include "inc/bq27z561.h"
#include <mca/platform/platform_buckchg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "fuelgauge_ic"
#endif

#define BLOCK_LENGTH_MAX	64
#define REG_BLOCK_LENGTH	32
#define FG_IC_SOH_TH_DEFAUL 92
#define FG_IC_CYCLE_TH_DEFAUL 400
#define FG_IC_CYCLE_STEP_DEFAUL 50
#define FG_I2C_RETRY_CNT	5
#define FG_IC_RESISTANCE_ID_DEFAUL 100000

#define FG_TEMP_LOW_TH -1280
#define FG_TEMP_HIGH_TH 1280
#define FG_VOLT_LOW_TH 0
#define FG_VOLT_HIGH_TH 5000	/* stock 0x1388 (template 10000) */
#define FG_CURR_LOW_TH -25000
#define FG_CURR_HIGH_TH 6000
#define FG_RM_FCC_LOW_TH 0
#define FG_RM_FCC_HIGH_TH 15000	/* stock 0x3a98 (template 7500) */
#define FG_RM_FCC_DEFAULT_TH 2500

#define BYTES_TO_U16(bytes) \
	((u16)((bytes)[0] | ((bytes)[1] << 8)))

static int fg_convert_bytes_to_volt(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_curr(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_temp(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_fcc(struct bq_fg_chip *fg, u8 *bytes);
static int fg_convert_bytes_to_cycle_count(struct bq_fg_chip *fg, u8 *bytes);

static int fg_get_raw_soc(struct bq_fg_chip *bq, int *raw_soc);
static int fg_read_current(struct bq_fg_chip *bq, int *curr);
static int fg_read_temperature(struct bq_fg_chip *bq, int *temp);
static int fg_get_chip_ok(struct bq_fg_chip *bq, int *ok);
static int fg_get_seal(struct bq_fg_chip *bq, int *value);
static int fg_update_record_voltage_level(struct bq_fg_chip *bq);
static inline unsigned long fg_convert_u8_to_u16(unsigned char lsb, unsigned char hsb);
static int fg_read_max_cell_volt(struct bq_fg_chip *bq, int *max_volt);
static int fg_get_cutoff_voltage(struct bq_fg_chip *bq, int *volt);

#if 0
/* fg read write block can not too much type,read block may fail,
write block may make system crash*/
static int __fg_read_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	int ret;
	int retry_cnt = 0, count;
	u8 reg_now, len_now, *buf_now;

	reg_now = reg;
	len_now = len;
	buf_now = buf;
	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		while (len_now) {
			if (len_now <= 16)
				count = len_now;
			else
				count = 16;

			ret = i2c_smbus_read_i2c_block_data(client, reg_now, count, buf_now);
			len_now -= count;
			if (len_now != 0) {
				reg_now += count;
				buf_now += count;
			}
		}
		if (ret < 0) {
			usleep_range(4000, 4100);
			retry_cnt++;
			reg_now = reg;
			len_now = len;
			buf_now = buf;
		} else {
			return ret;
		}
	}

	mca_log_err("i2c read block fail: can't write to reg 0x%02X, ret:%d, cnt:%d\n",
			reg, ret, retry_cnt);

	return ret;
}


static int __fg_write_block(struct i2c_client *client, u8 reg, u8 *buf, u8 len)
{
	int ret;
	int retry_cnt = 0, count;
	u8 reg_now, len_now, *buf_now;

	reg_now = reg;
	len_now = len;
	buf_now = buf;
	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		while (len_now) {
			if (len_now <= 16)
				count = len_now;
			else
				count = 16;

			ret = i2c_smbus_write_i2c_block_data(client, reg_now, count, buf_now);
			len_now -= count;
			if (len_now != 0) {
				reg_now += count;
				buf_now += count;
			}
		}

		if (ret < 0) {
			usleep_range(4000, 4100);
			retry_cnt++;
			reg_now = reg;
			len_now = len;
			buf_now = buf;
		} else {
			return ret;
		}
	}

	mca_log_err("i2c write block fail: can't write to reg 0x%02X, ret:%d, cnt:%d\n",
			reg, ret, retry_cnt);

	return ret;
}
#endif

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
#line 110
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
#line 138
		mca_log_err("I2C transfer failed, ret=%d\n", ret);
	kfree(wbuf);

	return ret;
}

static int __fg_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret < 0) {
#line 152
			mca_log_err("i2c write byte fail: can't write 0x%02X to reg 0x%02X, ret:%d, cnt:%d\n",
					val, reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			return ret;
		}
	}
#line 160
	mca_log_err("i2c write byte fail: can't write 0x%02X to reg 0x%02X, ret:%d, cnt max:%d\n",
			val, reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret < 0) {
#line 174
			mca_log_err("i2c read byte fail: can't read reg 0x%02X\n, ret:%d, cnt:%d",
					reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			*val = (u8)ret;
			return ret;
		}
	}

#line 184
	mca_log_err("i2c read byte fail: can't read reg 0x%02X\n, ret:%d, cnt max:%d",
			reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_read_word_data(client, reg);
		if (ret < 0) {
#line 198
			mca_log_err("i2c read word fail: can't read from reg 0x%02X, ret:%d, cnt:%d\n",
					reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		}  else {
			*val = (u16)ret;
			return ret;
		}
	}

#line 208
	mca_log_err("i2c read word fail: can't read from reg 0x%02X, ret:%d, cnt max:%d\n",
			reg, ret, FG_I2C_RETRY_CNT);

	return ret;
}

static int __fg_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	int ret = 0;
	int retry_cnt = 1;

	while(retry_cnt <= FG_I2C_RETRY_CNT) {
		ret = i2c_smbus_write_word_data(client, reg, val);
		if (ret < 0) {
#line 222
			mca_log_err("i2c write word fail: can't write 0x%04X to reg 0x%02X, ret:%d, cnt:%d\n",
					val, reg, ret, retry_cnt);
			usleep_range(4000, 4100);
			retry_cnt++;
		} else {
			return ret;
		}
	}
#line 230
	mca_log_err("i2c write word fail: can't write 0x%04X to reg 0x%02X, ret:%d, cnt max:%d\n",
			val, reg, ret, FG_I2C_RETRY_CNT);
	return ret;
}

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

static int fg_write_block(struct bq_fg_chip *bq, u8 reg, u8 *data, u8 len)
{
	int ret;

	mutex_lock(&bq->i2c_rw_lock);
	ret = __fg_write_block(bq->client, reg, data, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static noinline int fg_read_block(struct bq_fg_chip *bq, u8 reg, u8 *buf, u8 len)
{
	int ret;

	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_read_block(bq->client, reg, buf, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

static u8 checksum(u8 *data, u8 len)
{
	u8 i;
	u16 sum = 0;

	for (i = 0; i < len; i++)
		sum += data[i];

	sum &= 0xFF;

	return 0xFF - sum;
}

__maybe_unused
static char *fg_print_buf(const char *msg, u8 *buf, u8 len)
{
	int i;
	int idx = 0;
	int num;
	static u8 strbuf[128];

	for (i = 0; i < len; i++) {
		num = sprintf(&strbuf[idx], "%02X ", buf[i]);
		idx += num;
	}
#line 330
	mca_log_debug("%s %s\n", msg, strbuf);

	return strbuf;
}

static int fg_write_read_block(struct bq_fg_chip *bq, u8 reg, u8 *data, u8 len, u8 lens)
{
	int ret;

	ret = __fg_write_block(bq->client, reg, data, len);
	if (ret < 0)
		return ret;

	usleep_range(4000, 4100);
	ret = __fg_read_block(bq->client, reg, data, lens);

	return ret;
}

/* G13l: structure from the stock disassembly (no locking here; the caller holds i2c_rw_lock) */
static noinline int __fg_mac_read_block(struct bq_fg_chip *bq, u16 cmd, u8 *buf, u8 len)
{
	int ret = 0;
	u8 cksum_calc, cksum;
	u8 t_buf[40] = { 0 };
	u8 t_len;
	int i;

	t_buf[0] = (u8)cmd;
	t_buf[1] = (u8)(cmd >> 8);

	if (cmd == FG_MAC_CMD_IF_SIG) {
		ret = __fg_write_block(bq->client, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 2);
		if (ret < 0) {
#line 367
			mca_log_err("mac write fail, ret: %d\n", ret);
			return ret;
		}
#line 370
		mca_log_err("IF_SIG cmd=0x%04x, delay 1100ms for app_checksum\n", cmd);
		msleep(1100);
		ret = __fg_read_block(bq->client, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 36);
	} else {
		ret = fg_write_read_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 2, 36);
	}
	if (ret < 0) {
#line 377
		mca_log_err("mac write read fail, ret: %d\n", ret);
		return ret;
	}

	if (t_buf[0] != (u8)cmd || t_buf[1] != (u8)(cmd >> 8)) {
#line 381
		mca_log_err("mac read wrong, read: %02x%02x, out: %02x%02x\n",
			(u8)cmd, (u8)(cmd >> 8), t_buf[0], t_buf[1]);
		return -1;
	}

	if (cmd == FG_MAC_CMD_LIFETIME1)
		fg_print_buf("mac_read_block FG_MAC_CMD_LIFETIME1: ", t_buf, 36);
	else if (cmd == FG_MAC_CMD_LIFETIME3)
		fg_print_buf("mac_read_block FG_MAC_CMD_LIFETIME3: ", t_buf, 36);

	cksum = t_buf[34];
	t_len = t_buf[35];

	if (t_len > 42) {
#line 395
		mca_log_err("cksum:%d, t_len err:%d.\n", cksum, t_len);
		t_len = 42;
	} else if (t_len < 2) {
#line 398
		mca_log_err("cksum:%d, t_len err:%d.\n", cksum, t_len);
		t_len = 2;
	}

	cksum_calc = checksum(t_buf, t_len - 2);
	if (cksum_calc != cksum) {
#line 404
		mca_log_err("cksum:%d, t_len err:%d.\n", cksum, t_len);
		return -1;
	}

	for (i = 0; i < len; i++)
		buf[i] = t_buf[i + 2];

	return ret;
}

/* G13l: stock wrapper: no data_lock, i2c_rw_lock taken with mutex_trylock + usleep_range(40, 50) unless an OTA is running */
static int fg_mac_read_block(struct bq_fg_chip *bq, u16 cmd, u8 *buf, u8 len)
{
	int ret = 0;

	if (bq->ota_updating)
		return 0;
	while (!mutex_trylock(&bq->i2c_rw_lock)) {
		if (bq->ota_updating)
			return 0;
		usleep_range(40, 50);
	}
	ret = __fg_mac_read_block(bq, cmd, buf, len);
	mutex_unlock(&bq->i2c_rw_lock);

	return ret;
}

/* G13l: generated from the stock disassembly (register index, label, line pins, word/byte read); UNWIRED: the stock callers are in fg_sysfs_show; the read helpers __fg_read_byte/__fg_read_word are still the TEMPLATE ones */
static noinline int fg_get_over_peak_flag(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[22];
	u8 val = 0;
	int ret;

	if (reg == 0xff) {
#line 2307
		mca_log_info("OVER_PEAK command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_byte(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 2314
			mca_log_info("could not read OVER_PEAK, ret=%d\n", ret);
		}
	}
done:
	return val;
}

static noinline int fg_get_current_dev(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[23];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1123
		mca_log_info("CURR_DEV command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1130
			mca_log_info("could not read CURR_DEV, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_power_dev(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[24];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1146
		mca_log_info("POW_DEV command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1153
			mca_log_info("could not read POW_DEV, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_average_current(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[25];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1169
		mca_log_info("AVE_CUR command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1176
			mca_log_info("could not read AVE_CUR, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_start_learning(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[30];
	u8 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1280
		mca_log_info("START_LEARNING command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_byte(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1287
			mca_log_info("could not read START_LEARNING, ret=%d\n", ret);
		}
	}
done:
	return val;
}

static noinline int fg_get_stop_learning(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[31];
	u8 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1315
		mca_log_info("STOP_LEARNING command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_byte(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1322
			mca_log_info("could not read STOP_LEARNING, ret=%d\n", ret);
		}
	}
done:
	return val;
}

static noinline int fg_get_actual_power(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[33];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1350
		mca_log_info("ACT_POWER command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1357
			mca_log_info("could not read ACT_POWER, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_learning_power(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[32];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1373
		mca_log_info("EST_POWER command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1380
			mca_log_info("could not read EST_POWER, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_learning_power_dev(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[34];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1421
		mca_log_info("POWER_DEV command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1428
			mca_log_info("could not read POWER_DEV, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_learning_time_dev(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[35];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1444
		mca_log_info("TIME_DEV command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1451
			mca_log_info("could not read TIME_DEV, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_constant_power(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[36];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1467
		mca_log_info("CONST_POWER command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1474
			mca_log_info("could not read CONST_POWER, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_referance_power(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[38];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 2177
		mca_log_info("REF_POWER command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 2184
			mca_log_info("could not read REF_POWER, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_nvt_referance_power(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[40];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 2200
		mca_log_info("NVT_REF_CURR command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 2207
			mca_log_info("could not read NVT_REF_CURR, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_referance_current(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[39];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1533
		mca_log_info("REF_CURR command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1540
			mca_log_info("could not read REF_CURR, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_start_learning_b(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[41];
	u8 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1556
		mca_log_info("START_LEARNING_B command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_byte(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1563
			mca_log_info("could not read START_LEARNING_B, ret=%d\n", ret);
		}
	}
done:
	return val;
}

static noinline int fg_get_stop_learning_b(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[42];
	u8 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1574
		mca_log_info("STOP_LEARNING_B command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_byte(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1581
			mca_log_info("could not read STOP_LEARNING_B, ret=%d\n", ret);
		}
	}
done:
	return val;
}

static noinline int fg_get_learning_power_b(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[43];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1592
		mca_log_info("EST_POWER_B command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1599
			mca_log_info("could not read EST_POWER_B, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_actual_power_b(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[44];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1615
		mca_log_info("ACT_POWER_B command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1622
			mca_log_info("could not read ACT_POWER_B, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

static noinline int fg_get_learning_power_dev_b(struct bq_fg_chip *bq)
{
	u8 reg = bq->regs[45];
	u16 val = 0;
	int ret;

	if (reg == 0xff) {
#line 1638
		mca_log_info("POWER_DEV_B command not supported!\n");
		return 0;
	}
	if (!bq->ota_updating) {
		while (!mutex_trylock(&bq->i2c_rw_lock)) {
			if (bq->ota_updating)
				goto done;
			usleep_range(40, 50);
		}
		ret = __fg_read_word(bq->client, reg, &val);
		mutex_unlock(&bq->i2c_rw_lock);
		if (ret < 0) {
#line 1645
			mca_log_info("could not read POWER_DEV_B, ret=%d\n", ret);
		}
	}
done:
	return (u16)-val;
}

/* G13l: generated from the stock disassembly (command, length, byte offsets, destination); UNWIRED: the stock callers are in fg_sysfs_show */
static noinline __used void fg_read_chem_df_sign(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x8, buf, 32);
	if (ret < 0)
		return;
	*val = (buf[1] << 8) | buf[0];
}

static noinline __used void fg_get_app_checksum(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x4, buf, 32);
	if (ret < 0)
		return;
	*val = (buf[1] << 8) | buf[0];
}

static noinline __used void fg_get_csd_flag(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a2, buf, 8);
	if (ret < 0)
		return;
	*val = buf[1];
}

static noinline __used void fg_get_csd_r1(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a2, buf, 8);
	if (ret < 0)
		return;
	*val = (buf[3] << 8) | buf[2];
}

static noinline __used void fg_get_csd_r2(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a2, buf, 8);
	if (ret < 0)
		return;
	*val = (buf[5] << 8) | buf[4];
}

static noinline __used void fg_get_dcr_slope1(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a0, buf, 12);
	if (ret < 0)
		return;
	*val = (buf[5] << 8) | buf[4];
}

static noinline __used void fg_get_dcr_slope2(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a0, buf, 12);
	if (ret < 0)
		return;
	*val = (buf[7] << 8) | buf[6];
}

static noinline __used void fg_get_dcr_slope3(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x10a0, buf, 12);
	if (ret < 0)
		return;
	*val = (buf[9] << 8) | buf[8];
}

static noinline __used void fg_get_charge_absoc(struct bq_fg_chip *bq, int *val)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x71, buf, 8);
	if (ret < 0)
		return;
	*val = (buf[3] << 8) | buf[2];
}

static noinline __used void fg_get_max_dsg_current(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x60, buf, 8);
	if (ret < 0)
		return;
	bq->max_dsg_current = (buf[5] << 8) | buf[4];
}

static noinline __used void fg_get_min_temp(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x60, buf, 8);
	if (ret < 0)
		return;
	bq->min_temp = buf[7];
}

static noinline __used void fg_get_min_voltage(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x1031, buf, 8);
	if (ret < 0)
		return;
	bq->min_voltage = (buf[7] << 8) | buf[6];
}

static noinline __used void fg_get_cuv_count(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5e, buf, 32);
	if (ret < 0)
		return;
	bq->cuv_count = (buf[17] << 8) | buf[16];
}

static noinline __used void fg_get_cuv_last_cycle(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5e, buf, 32);
	if (ret < 0)
		return;
	bq->cuv_last_cycle = (buf[19] << 8) | buf[18];
}

static noinline __used void fg_get_hcuv_count(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 8);
	if (ret < 0)
		return;
	bq->hcuv_count = (buf[5] << 8) | buf[4];
}

static noinline __used void fg_get_hcuv_last_cycle(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 8);
	if (ret < 0)
		return;
	bq->hcuv_last_cycle = (buf[7] << 8) | buf[6];
}

static noinline __used void fg_get_ocd_count(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5e, buf, 32);
	if (ret < 0)
		return;
	bq->ocd_count = (buf[29] << 8) | buf[28];
}

static noinline __used void fg_get_ocd_last_cycle(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5e, buf, 32);
	if (ret < 0)
		return;
	bq->ocd_last_cycle = (buf[31] << 8) | buf[30];
}

static noinline __used void fg_get_hocd_count(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 32);
	if (ret < 0)
		return;
	bq->hocd_count = (buf[13] << 8) | buf[12];
}

static noinline __used void fg_get_hocd_last_cycle(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 32);
	if (ret < 0)
		return;
	bq->hocd_last_cycle = (buf[15] << 8) | buf[14];
}

static noinline __used void fg_get_hscd_count(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 32);
	if (ret < 0)
		return;
	bq->hscd_count = (buf[17] << 8) | buf[16];
}

static noinline __used void fg_get_hscd_last_cycle(struct bq_fg_chip *bq)
{
	u8 buf[64] = { 0 };
	int ret;

	ret = fg_mac_read_block(bq, 0x5f, buf, 32);
	if (ret < 0)
		return;
	bq->hscd_last_cycle = (buf[19] << 8) | buf[18];
}

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
#line 435
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

static int fg_read_i2c_block_data(struct bq_fg_chip *info, u8 reg, unsigned short len, u8 *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(info->dev);

	if (!buf)
		return -EINVAL;

	if (!client || !client->adapter)
		return -ENODEV;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);
	if (ret < 0) {
		mca_log_err("I2C SMBus read failed\n");
		return ret;
	}

	return ret;
}

#define FG_BATT_INFO_LEN 16
static int fg_get_batt_info(struct bq_fg_chip *fg, void *pdata)
{
	int ret;
	struct fg_batt_info *data = (struct fg_batt_info *)pdata;
	u8 buf[FG_BATT_INFO_LEN];
	u8 reg;
	unsigned short len;
	int offset;
	int i, retry_count;

	/* 5: wait resume */
	for (retry_count = 0; retry_count < 5; retry_count++) {
		if (atomic_read(&fg->pm_suspend)) {
#line 489
			mca_log_info("fg_get_batt_info wait resume\n");
			msleep(30); /* 30: wait i2c client device resume */
		} else {
			break;
		}
	}

	mutex_lock(&fg->i2c_rw_lock);
	reg = bq27z561_regs[BQ_FG_REG_TEMP];
	len = 16;
	memset(buf, 0, FG_BATT_INFO_LEN);
	ret = fg_read_i2c_block_data(fg, reg, len, buf);
	mutex_unlock(&fg->i2c_rw_lock);
	if (ret < 0) {
#line 501
		mca_log_err("could not read batt data, reg=0x%02x, ret=%d\n", reg, ret);
		return ret;
	}

	for (i = 0; i < len; i += 8) {
#line 506
		mca_log_debug("%02x regs[%d] = %02x%02x%02x%02x %02x%02x%02x%02x\n",
			reg, i, buf[i], buf[i + 1], buf[i + 2], buf[i + 3],
			buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}

	offset = bq27z561_regs[BQ_FG_REG_CN] - reg;
	data->curr = fg_convert_bytes_to_curr(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_VOLT] - reg;
	data->volt = fg_convert_bytes_to_volt(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_TEMP] - reg;
	data->temp = fg_convert_bytes_to_temp(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_RM] - reg;
	data->rm = fg_convert_bytes_to_rm(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_FCC] - reg;
	data->fcc = fg_convert_bytes_to_fcc(fg, buf + offset);

	fg_read_max_cell_volt(fg, &data->vcell_max);

	mutex_lock(&fg->i2c_rw_lock);
	reg = bq27z561_regs[BQ_FG_REG_CC];
	len = 8;
	memset(buf, 0, FG_BATT_INFO_LEN);
	ret = fg_read_i2c_block_data(fg, reg, len, buf);
	mutex_unlock(&fg->i2c_rw_lock);
	if (ret < 0) {
#line 534
		mca_log_err("could not read batt data, reg=0x%02x, ret=%d\n", reg, ret);
		return ret;
	}

	for (i = 0; i < len; i += 8) {
#line 539
		mca_log_debug("%02x regs[%d] = %02x%02x%02x%02x %02x%02x%02x%02x\n",
			reg, i, buf[i], buf[i + 1], buf[i + 2], buf[i + 3],
			buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
	}

	offset = bq27z561_regs[BQ_FG_REG_CC] - reg;
	data->cycle_count = fg_convert_bytes_to_cycle_count(fg, buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_SOC] - reg;
	data->rsoc = BYTES_TO_U16(buf + offset);

	offset = bq27z561_regs[BQ_FG_REG_SOH] - reg;
	data->soh = BYTES_TO_U16(buf + offset);

	mca_log_debug("curr: %d, volt: %d, temp: %d, rsoc: %d, rm: %d, fcc: %d, cycle_count: %d, soh: %d\n",
		data->curr, data->volt, data->temp, data->rsoc,
		data->rm, data->fcc, data->cycle_count, data->soh);
	return 0;
}

#define READ_FASTCHARGE_REG 0
static int fg_get_fastcharge_mode(struct bq_fg_chip *bq, int *ffc)
{
#if READ_FASTCHARGE_REG
	u8 data[3];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHARGING_STATUS, data, 3);
	if (ret < 0) {
		mca_log_err("could not write fastcharge = %d\n", ret);
		return ret;
	}

	if(bq->fg_vendor == MPC7021){
		*ffc = (data[0] & BIT(7)) >> 7;
	}else {
		*ffc = (data[2] & FG_FLAGS_FASTCHAGE) >> 5;
	}

	return 0;
#endif

	*ffc = bq->fast_mode;
	return 0;

}

static int fg_get_gague_mode(struct bq_fg_chip *bq)
{
	u8 data[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_GAGUE_STATUS, data, 4);
	if (ret < 0) {
		mca_log_err("could not write gague mode = %d\n", ret);
		return ret;
	}

	bq->batt_fd_1 = !!(data[0] & BIT(0));
	bq->batt_fc_1 = !!(data[0] & BIT(1));
	bq->batt_td_1 = !!(data[0] & BIT(2));
	bq->batt_tc_1 = !!(data[0] & BIT(3));
#line 592
	mca_log_info("gague status FD %d, FC = %d, TD %d TC %d\n",
		bq->batt_fd_1, bq->batt_fc_1, bq->batt_td_1, bq->batt_tc_1);

	return 0;
}

static int fg_set_fastcharge_mode(struct bq_fg_chip *bq, bool enable)
{
	u8 data[2];
	int ret, len = 0;

	if (bq->fast_mode == enable)
		return 0;

	if (atomic_read(&bq->digest_in_process)) {
#line 607
		mca_log_info("wait fg auth to be done\n");
		return 0;
	}

	if(bq->fg_vendor != MPC7021)
		len = 2;

	data[0] = enable;
#line 612
	mca_log_info("set fastcharge mode: enable: %d\n", enable);
	if (enable) {
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FASTCHARGE_EN, data, len);

		if (ret < 0) {
			mca_log_info("could not write fastcharge = %d\n", ret);
			return ret;
		}
	} else {
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FASTCHARGE_DIS, data, len);
		if (ret < 0) {
			mca_log_info("could not write fastcharge = %d\n", ret);
			return ret;
		}
	}
	bq->fast_mode = enable;

	return ret;
}

static int fg_read_status(struct bq_fg_chip *bq)
{
	int ret;
	u16 flags;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_BATT_STATUS], &flags);
	if (ret < 0)
		return ret;

	bq->batt_fc = !!(flags & FG_FLAGS_FC);
	bq->batt_fd = !!(flags & FG_FLAGS_FD);
	bq->batt_rca = !!(flags & FG_FLAGS_RCA);
	bq->batt_dsg = !!(flags & FG_FLAGS_DSG);

	return 0;
}

static int fg_get_manu_info(unsigned char val, int base, int step)
{
	int index = 0;
	int data = 0;

#line 665
	mca_log_info("val:%d, '0':%d, 'A':%d, 'a':%d\n", val, '0', 'A', 'a');
	if (val > '0' && val < '9')
		index = val - '0';
	if (val > 'A' && val < 'Z')
		index = val - 'A' + 10;
	if (val > 'a' && val < 'z')
		index = val - 'a' + 36;

#line 673
	mca_log_info("base:%d, index:%d, step:%d\n", base, index, step);
	data = base + index * step;

	return data;
}

static int fg_get_manufacture_data(struct bq_fg_chip *bq)
{
	u8 t_buf[40];
	int ret;
	int i;
	int byte, base, step;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf, 32);
	if (ret < 0) {
#line 688
		mca_log_info("failed to get MANE NAME\n");
		return ret;
	}

	if (strncmp(t_buf, "MI", 2) != 0) {
#line 693
		mca_log_info("Can not get MI battery data\n");
		manu_info[TERMINATION].data = BQ27Z561_DEFUALT_TERM;
		manu_info[FFC_TERMINATION].data = BQ27Z561_DEFUALT_FFC_TERM;
		manu_info[RECHARGE_VOL].data = BQ27Z561_DEFUALT_RECHARGE_VOL;
		return 0;
	}

	for (i = 0; i < MANU_DATA_LEN; i++) {
		byte = manu_info[i].byte;
		base = manu_info[i].base;
		step = manu_info[i].step;
		manu_info[i].data = fg_get_manu_info(t_buf[byte], base, step);
#line 705
		mca_log_info("manu_info[%d].data %d\n", i, manu_info[i].data);
	}

	return 0;
}

static int fg_read_cell_vendor(struct bq_fg_chip *bq)
{
	u8 t_buf[40];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
#line 718
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
#line 721
	mca_log_info("%s\n", t_buf);

	switch (t_buf[1]) {
	case FG_CELL_NAME_A:
		bq->cell_name = "samsung";
		break;
	case FG_CELL_NAME_B:
		bq->cell_name = "sony";
		break;
	case FG_CELL_NAME_C:
		bq->cell_name = "sanyo";
		break;
	case FG_CELL_NAME_D:
		bq->cell_name = "lg";
		break;
	case FG_CELL_NAME_E:
		bq->cell_name = "panasonic";
		break;
	case FG_CELL_NAME_F:
		bq->cell_name = "atl";
		break;
	case FG_CELL_NAME_G:
		bq->cell_name = "byd";
		break;
	case FG_CELL_NAME_H:
		bq->cell_name = "tws";
		break;
	case FG_CELL_NAME_I:
		bq->cell_name = "lishen";
		break;
	case FG_CELL_NAME_J:
		bq->cell_name = "coslight";
		break;
	case FG_CELL_NAME_L:
		bq->cell_name = "lwn";
		break;
	default:
		bq->cell_name = "default";
		break;
	}

	return 0;
}

static int fg_read_rsoc(struct bq_fg_chip *bq, int *rsoc)
{
	int ret, val;
	u16 soc = 0;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOC], &soc);
	if (ret < 0) {
#line 772
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
#line 797
		mca_log_info("could not read temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_temp(bq, (u8 *)&temp_now);
	return 0;
}

static int fg_set_temperature(void *data, int value)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (bq) {
		bq->fake_temp = value;
#line 831
		mca_log_err("set fake_temp: %d\n", bq->fake_temp);
	}

	return 0;
}

static int fg_read_volt(struct bq_fg_chip *bq, int *vbat)
{
	int ret;
	u16 volt = 0;

	if (bq->fg_error)
		return 3700;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_VOLT], &volt);
	if (ret < 0) {
#line 847
		mca_log_err("could not read volt ret = %d\n", ret);
		return ret;
	}

	*vbat = fg_convert_bytes_to_volt(bq, (u8 *)&volt);
	return 0;
}

static int fg_convert_bytes_to_volt(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int volt = value;

	if (volt < FG_VOLT_LOW_TH || volt > FG_VOLT_HIGH_TH || fg->ota_updating) {
#line 879
		mca_log_err("volt out of range: %d\n", volt);
		volt = fg->batt_volt;
	}

	if (!volt && fg->batt_volt > FG_VOLT_LOW_TH)
		volt = fg->batt_volt;

	fg->batt_volt = volt;
#line 887
	mca_log_debug("volt: %d\n", fg->batt_volt);
	return fg->batt_volt;
}

static int fg_convert_bytes_to_curr(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 temp_curr = BYTES_TO_U16(bytes);
	int curr;

	if (temp_curr > 32768)
		curr = -1 * (temp_curr - 65536);
	else
		curr = -1 * temp_curr;

	if (curr < FG_CURR_LOW_TH || curr > FG_CURR_HIGH_TH || fg->ota_updating) {
#line 902
		mca_log_err("current out of range: %d\n", curr);
		curr = fg->batt_curr / 1000;
	}

	fg->batt_curr = curr * 1000;
#line 907
	mca_log_debug("temp_curr: %d, curr: %d\n", temp_curr, fg->batt_curr);
	return fg->batt_curr;
}

static int fg_convert_bytes_to_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH || fg->ota_updating) {
#line 917
		mca_log_err("temperature out of range: %d\n", temp);
		temp = fg->batt_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->batt_temp = temp;
	return fg->batt_temp;
}

static int fg_convert_bytes_to_rm(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int rm;

	if (fg->fake_rm)
		return fg->fake_rm;

	rm = value * 1000;
	if (value > FG_RM_FCC_HIGH_TH || fg->ota_updating) {
#line 955
		mca_log_err("rm out of range: %d\n", value);
		rm = fg->batt_rm;
	}

	if (!value && fg->batt_rm / 1000 > FG_RM_FCC_DEFAULT_TH)
		rm = fg->batt_rm;

	fg->batt_rm = rm;
	return fg->batt_rm;
}

static int fg_convert_bytes_to_fcc(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int fcc;

	if (fg->fake_fcc)
		return fg->fake_fcc;

	fcc = value * 1000;
	fg->enable_fcc_default = false;
	if (value > FG_RM_FCC_HIGH_TH || fg->ota_updating) {
#line 975
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
#line 1037
		mca_log_err("could not read current, ret = %d\n", ret);
		*curr = 0;
		return ret;
	}
	*curr = fg_convert_bytes_to_curr(bq, (u8 *)&temp_curr);

	return 0;
}

static int fg_read_max_cell_volt(struct bq_fg_chip *bq, int *max_volt)
{
	int ret;
	int cell_volt_1 = 0, cell_volt_2 = 0;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	/* MPC7021 support */
	if(bq->fg_vendor != MPC7021){
		ret = fg_read_volt(bq, max_volt);
		return ret;
	}

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 36);
	if (ret < 0)
		return ret;

	cell_volt_1 = (t_buf[1] << 8) | t_buf[0];
	cell_volt_2 = (t_buf[3] << 8) | t_buf[2];

	mca_log_debug("cell volt = [%d %d] \n", cell_volt_1, cell_volt_2);

	*max_volt = cell_volt_1 > cell_volt_2 ? cell_volt_1 : cell_volt_2;

	return 0;
}

static int fg_read_fcc(struct bq_fg_chip *bq, int *fcc)
{
	int ret;
	u16 data = 0;

	if (bq->regs[BQ_FG_REG_FCC] == INVALID_REG_ADDR) {
#line 1052
		mca_log_info("FCC command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_FCC], &data);
	if (ret < 0) {
#line 1058
		mca_log_info("could not read FCC ret %d\n", ret);
		return ret;
	}

	*fcc = fg_convert_bytes_to_fcc(bq, (u8 *)&data);
	return 0;
}

static int fg_get_isc_alert_level(struct bq_fg_chip *bq, int *level)
{
	int ret;
	u8 isc_alert_level;

	if (bq->fake_isc)
		return bq->fake_isc;

	if (bq->regs[NVT_FG_REG_ISC] == INVALID_REG_ADDR) {
#line 1075
		mca_log_info("ISC command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_ISC], &isc_alert_level);

	if (ret < 0)
#line 1082
		mca_log_info("could not read ISC, ret=%d\n", ret);

	*level = isc_alert_level;
	return 0;
}

static int fg_set_isc_alert_level(struct bq_fg_chip *bq, int value)
{
	int ret = 0;

	bq->fake_isc = value;
#line 1093
	mca_log_info("set fake isc:: %d\n", bq->fake_isc);
	return ret;
}

static int fg_get_soa_alert_level(struct bq_fg_chip *bq, int *level)
{
	int ret;
	u8 soa_alert_level;

	if (bq->regs[NVT_FG_REG_SOA_L] == INVALID_REG_ADDR) {
#line 1103
		mca_log_info("SOA command not supported!\n");
		return 0;
	}

	ret = fg_read_byte(bq, bq->regs[NVT_FG_REG_SOA_L], &soa_alert_level);
	if (ret < 0) {
#line 1109
		mca_log_info("could not read SOA, ret=%d\n", ret);
		return ret;
	}

	*level = soa_alert_level;
	return 0;
}




static int fg_get_average_temp(struct bq_fg_chip *bq)
{
	int ret;
	u16 ave_temp;

	if (bq->regs[NVT_FG_AVE_TEMP] == INVALID_REG_ADDR) {
#line 1260
		mca_log_info("AVE_TEMP command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_AVE_TEMP], &ave_temp);

	if (ret < 0)
#line 1267
		mca_log_info("could not read AVE_TEMP, ret=%d\n", ret);

	ave_temp = ave_temp - 2730;

	return ave_temp;
}


static int fg_set_start_learning(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_START_LEARNING] == INVALID_REG_ADDR) {
#line 1297
		mca_log_info("START_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING], 0x01);

	if (ret < 0)
#line 1304
		mca_log_info("could not write START_LEARNING, ret=%d\n", ret);

	return ret;
}


static int fg_set_stop_learning(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING] == INVALID_REG_ADDR) {
#line 1332
		mca_log_info("STOP_LEARNING command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING], 0x01);

	if (ret < 0)
#line 1339
		mca_log_info("could not write STOP_LEARNING, ret=%d\n", ret);

	return ret;
}



static int fg_set_learning_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 est_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	est_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_EST_POWER] == INVALID_REG_ADDR) {
#line 1403
		mca_log_info("EST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_EST_POWER], est_power);

	if (ret < 0)
#line 1410
		mca_log_info("could not write EST_POWER, ret=%d\n", ret);

	return ret;
}




static int fg_set_constant_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 constant_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	constant_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_CONST_POWER] == INVALID_REG_ADDR) {
#line 1497
		mca_log_info("CONST_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_CONST_POWER], constant_power);

	if (ret < 0)
#line 1504
		mca_log_info("could not write CONST_POWER, ret=%d\n", ret);

	return ret;
}

static int fg_get_remaining_time(struct bq_fg_chip *bq)
{
	int ret;
	u16 remaining_time;

	if (bq->regs[NVT_FG_REG_CONST_POWER_TM] == INVALID_REG_ADDR) {
#line 1515
		mca_log_info("POWER_TM command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[NVT_FG_REG_CONST_POWER_TM], &remaining_time);

	if (ret < 0)
#line 1522
		mca_log_info("could not read POWER_TM, ret=%d\n", ret);

	return remaining_time;
}







static int fg_get_rel_soh(struct bq_fg_chip *bq, u32 *rel_soh)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_REL_SOH, t_buf, 2);
	if (ret < 0)
		return ret;

	*rel_soh =  (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_get_rel_soh_cyclecount(struct bq_fg_chip *bq, u32 *rel_soh_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_REL_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*rel_soh_cyclecount =  (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}


static int fg_get_eis_soh(struct bq_fg_chip *bq, u32 *eis_soh)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_EIS_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*eis_soh =  (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_get_eis_soh_cyclecount(struct bq_fg_chip *bq, u32 *eis_soh_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_EIS_SOH, t_buf, 4);
	if (ret < 0)
		return ret;

	*eis_soh_cyclecount =  (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}

static int fg_get_qmax_cyclecount(struct bq_fg_chip *bq, u32 *qmax_cyclecount)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 12);
	if (ret < 0)
		return ret;

	*qmax_cyclecount =  (t_buf[11] << 8) | (t_buf[10]);
	return ret;
}

static int fg_get_calibration_ffc_iterm(struct bq_fg_chip *bq, int *iterm)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_1, t_buf, 36);
	if (ret < 0)
		return ret;

	if (bq->batt_temp < 200)
		*iterm = (t_buf[1] << 8) | (t_buf[0]);
	else if (bq->batt_temp < 250)
		*iterm = (t_buf[5] << 8) | (t_buf[4]);
	else if (bq->batt_temp < 300)
		*iterm = (t_buf[9] << 8) | (t_buf[8]);
	else if (bq->batt_temp < 350)
		*iterm = (t_buf[13] << 8) | (t_buf[12]);
	else if (bq->batt_temp < 400)
		*iterm = (t_buf[17] << 8) | (t_buf[16]);
	else if (bq->batt_temp < 450)
		*iterm = (t_buf[21] << 8) | (t_buf[20]);
	else if (bq->batt_temp < 480)
		*iterm = (t_buf[25] << 8) | (t_buf[24]);
	else
		*iterm = (t_buf[29] << 8) | (t_buf[28]);

	return ret;
}

static int fg_get_calibration_charge_energy(struct bq_fg_chip *bq, int *charge_energy)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_2, t_buf, 36);
	if (ret < 0)
		return ret;

	if (bq->batt_temp < 200)
		*charge_energy = (t_buf[3] << 8) | (t_buf[2]);
	else if (bq->batt_temp < 250)
		*charge_energy = (t_buf[7] << 8) | (t_buf[6]);
	else if (bq->batt_temp < 300)
		*charge_energy = (t_buf[11] << 8) | (t_buf[10]);
	else if (bq->batt_temp < 350)
		*charge_energy = (t_buf[15] << 8) | (t_buf[14]);
	else if (bq->batt_temp < 400)
		*charge_energy = (t_buf[19] << 8) | (t_buf[18]);
	else if (bq->batt_temp < 450)
		*charge_energy = (t_buf[23] << 8) | (t_buf[22]);
	else if (bq->batt_temp < 480)
		*charge_energy = (t_buf[27] << 8) | (t_buf[26]);
	else
		*charge_energy = (t_buf[31] << 8) | (t_buf[30]);

	return ret;
}

static int fg_get_real_supplement_energy(struct bq_fg_chip *bq, int *supplement_energy)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALIBRATION_INFO_4, t_buf, 36);
	if (ret < 0)
		return ret;

	*supplement_energy = (t_buf[2] << 8) | (t_buf[1]);

	return ret;
}

static int fg_get_batt_use_environment(struct bq_fg_chip *bq, u8 *batt_use_environment)
{
	int ret = 0, i;
	unsigned char t_buf[64] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CALC_RVALUE, t_buf, 32);
	if (ret < 0) {
		return ret;
	}

	for (i = 0; i < 32; i++)
		batt_use_environment[i] = t_buf[i];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_OVER_VOL_DURATION, t_buf, 6);
	if (ret < 0) {
		return ret;
	}

	for (i = 32; i < 38; i++)
		batt_use_environment[i] = t_buf[i - 32];

	return 0;
}

static int fg_get_max_life_vol(struct bq_fg_chip *bq, u32 *max_life_vol)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 4);
	if (ret < 0)
		return ret;

	switch(bq->fg_vendor) {
	case MPC7021:
		*max_life_vol = (t_buf[1] << 8 | t_buf[0]) + (t_buf[3] << 8 | t_buf[2]);
		break;
	default:
		*max_life_vol = (t_buf[1] << 8) | (t_buf[0]);
		break;
	}

	return ret;
}

static int fg_get_min_life_vol(struct bq_fg_chip *bq, u32 *min_life_vol)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};


	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 20);
		if (ret < 0)
			return ret;
		*min_life_vol = (t_buf[5] << 8 | t_buf[4]) + (t_buf[7] << 8 | t_buf[6]);
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, 20);
		if (ret < 0)
			return ret;

		*min_life_vol = (t_buf[19] << 8) | (t_buf[18]);
		break;
	}

	return ret;
}

static int fg_get_max_life_temp(struct bq_fg_chip *bq, u32 *max_life_temp)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*max_life_temp = t_buf[16];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8);
		if (ret < 0)
			return ret;

		*max_life_temp = t_buf[6];
		break;
	}

	return ret;
}

static int fg_get_min_life_temp(struct bq_fg_chip *bq, u32 *min_life_temp)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*min_life_temp = t_buf[17];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 8);
		if (ret < 0)
			return ret;

		*min_life_temp = t_buf[7];
		break;
	}

	return ret;
}

static int fg_get_over_vol_duration(struct bq_fg_chip *bq, u32 *over_vol_duration)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_OVER_VOL_DURATION, t_buf, 12);
	if (ret < 0)
		return ret;

	*over_vol_duration = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | (t_buf[8]);
	return ret;
}



static int fg_set_referance_power(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 referance_power;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	referance_power =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_REF_POWER] == INVALID_REG_ADDR) {
#line 2230
		mca_log_info("REF_POWER command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_REF_POWER], referance_power);

	if (ret < 0)
#line 2237
		mca_log_info("could not write REF_POWER, ret=%d\n", ret);

	return ret;
}

static int fg_set_start_learning_b(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_START_LEARNING_B] == INVALID_REG_ADDR) {
#line 2247
		mca_log_info("START_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_START_LEARNING_B], 0x01);

	if (ret < 0)
#line 2254
		mca_log_info("could not write START_LEARNING_B, ret=%d\n", ret);

	return ret;
}

static int fg_set_stop_learning_b(struct bq_fg_chip *bq)
{
	int ret;

	if (bq->regs[NVT_FG_REG_STOP_LEARNING_B] == INVALID_REG_ADDR) {
#line 2264
		mca_log_info("STOP_LEARNING_B command not supported!\n");
		return 0;
	}

	ret = fg_write_byte(bq, bq->regs[NVT_FG_REG_STOP_LEARNING_B], 0x01);

	if (ret < 0)
#line 2271
		mca_log_info("could not write STOP_LEARNING_B, ret=%d\n", ret);

	return ret;
}

static int fg_set_learning_power_b(struct bq_fg_chip *bq, int val)
{
	int ret;
	u16 est_power_b;
	u32 result;

	if (val > 32767)
		val =  32767;

	result = (u32)(-1 *  val);
	est_power_b =  (u16)(result & 0xFFFF);

	if (bq->regs[NVT_FG_REG_EST_POWER_B] == INVALID_REG_ADDR) {
#line 2289
		mca_log_info("EST_POWER_B command not supported!\n");
		return 0;
	}

	ret = fg_write_word(bq, bq->regs[NVT_FG_REG_EST_POWER_B], est_power_b);

	if (ret < 0)
#line 2296
		mca_log_info("could not write EST_POWER_B, ret=%d\n", ret);

	return ret;
}


static int fg_read_dc(struct bq_fg_chip *bq, int *dc)
{
	int ret;
	u16 tmp = 0;

	if (bq->regs[BQ_FG_REG_DC] == INVALID_REG_ADDR) {
#line 2325
		mca_log_info("DesignCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_DC], &tmp);
	if (ret < 0) {
#line 2331
		mca_log_info("could not read DC, ret=%d\n", ret);
		return -1;
	}

	*dc = tmp * 1000;
	return 0;
}

static int fg_read_qmax(struct bq_fg_chip *bq, u32 *qmax)
{
	int ret = 0;
	u8 t_buf[BATTERY_RANDOM_LEN] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX_CYCLECOUNT, t_buf, 10);
	if (ret < 0)
		return ret;

	*qmax =  (t_buf[9] << 8) | (t_buf[8]);

	if (*qmax == 0) {
		ret = fg_mac_read_block(bq, FG_MAC_CMD_QMAX, t_buf, 2);
		if (ret < 0)
			return ret;

		*qmax =  (t_buf[1] << 8) | (t_buf[0]);
	}

	return ret;
}

static int fg_read_rm(struct bq_fg_chip *bq, int *rm)
{
	int ret;
	u16 data = 0;

	if (bq->regs[BQ_FG_REG_RM] == INVALID_REG_ADDR) {
#line 2367
		mca_log_info("RemainingCapacity command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_RM], &data);
	if (ret < 0) {
#line 2373
		mca_log_info("could not read DC, ret=%d\n", ret);
		return ret;
	}

	*rm = fg_convert_bytes_to_rm(bq, (u8 *)&data);
	return 0;
}

static int fg_read_soh(struct bq_fg_chip *bq, int *soh)
{
	u16 tmp = 0;
	int cutoff = 0;
	int ret, i, base;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (bq->fake_soh != -1) {
		*soh = bq->fake_soh;
		return 0;
	}

	if (bq->regs[BQ_FG_REG_SOH] == INVALID_REG_ADDR) {
#line 2395
		mca_log_info("SOH command not supported!\n");
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_SOH], &tmp);
	if (ret < 0) {
#line 2401
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
#line 2412
				mca_log_err("dbcgx soh compensation, soh_comp = %d\n", bq->soh_delta_comp[i].soh_comp);
				break;
			}
		}
	}

	*soh = tmp;
	return 0;
}

static int fg_read_fcc_soh(struct bq_fg_chip *bq, u32 *fcc_soh)
{
	int ret;
	u8 t_buf[64];

	memset(t_buf, 0, 64);
	if (bq->fg_vendor != BQ27Z561)
		return 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FCC_SOH, t_buf, 2);
	if (ret < 0) {
#line 2433
		mca_log_info("could not read FCC_SOH, ret=%d\n", ret);
		return ret;
	}

	*fcc_soh = (t_buf[0] << 8) | t_buf[1];
	return ret;
}

static int fg_read_cyclecount(struct bq_fg_chip *bq, int *cycle_count)
{
	int ret;
	u16 cc = 0;

	if (bq->fake_cycle) {
		*cycle_count = bq->fake_cycle;
		return 0;
	}

	if (bq->regs[BQ_FG_REG_CC] == INVALID_REG_ADDR) {
#line 2452
		mca_log_info("Cycle Count not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CC], &cc);
	if (ret < 0) {
#line 2458
		mca_log_info("could not read Cycle Count, ret=%d\n", ret);
		return ret;
	}

	*cycle_count = fg_convert_bytes_to_cycle_count(bq, (u8 *)&cc);
	return 0;
}

static int fg_set_cyclecount(struct bq_fg_chip *bq, int value)
{
	int ret = 0;

	bq->fake_cycle = value;
#line 2471
	mca_log_info("set fake cyclecount:: %d\n", bq->fake_cycle);
	return ret;
}

static int fg_read_manufacturing_date(struct bq_fg_chip *bq, u8 *buf)
{
	unsigned char t_buf[64] = { 0 };
	int ret, i, len;
	char manufacturing_date[8] = {'2', '0', '2', '0', '0', '0', '0', '0'};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_BATT_SN, t_buf, 32);
	if (ret < 0) {
#line 2499
		mca_log_info("failed to get BATT_SN\n");
		return ret;
	}

	/*'0'-'9'0x30-0x39*/
	manufacturing_date[3] = '0' + (t_buf[6] & 0xF);
	manufacturing_date[6] = '0' + (t_buf[8] & 0xF);
	manufacturing_date[7] = '0' + (t_buf[9] & 0xF);
	/*'A'0x41 10yue, 'A'0x42 'A'0x43*/
	switch (t_buf[7]) {
	case 0x41:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '0';
		break;
	case 0x42:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '1';
		break;
	case 0x43:
		manufacturing_date[4] = '1';
		manufacturing_date[5] = '2';
		break;
	default:
		manufacturing_date[4] = '0';
		manufacturing_date[5] = '0' + (t_buf[7] & 0xF);
		break;
	}
	len = ARRAY_SIZE(manufacturing_date);

	mca_log_err("read manufacturing_date=%c,%c,%c,%c,%c,len=%d\n",
	manufacturing_date[3], manufacturing_date[4], manufacturing_date[5],
	manufacturing_date[6], manufacturing_date[7], len);

	for (i = 0; i < len; i++)
		buf[i] = manufacturing_date[i];

	return 0;
}

static int fg_read_first_usage_date(struct bq_fg_chip *bq, u8 *buf)
{
	unsigned char t_buf[64] = { 0 };
	int ret, i, len;
	unsigned char buf1, buf2;
	char first_usage_date[9] = {'0', '0', '0', '0', '0', '0', '0', '0', '\0'};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, t_buf, 32);
	if (ret < 0) {// read error, show 9999999
		memcpy(buf,"99999999",8);
		mca_log_info("failed to get first_usage_date\n");
		return ret;
	}
	/*t_buf[11] year0x00-0x09,t_buf[12] month,0x01-0x0C,t_buf[13] day0x26,day26*/
	first_usage_date[3] = '0' + (t_buf[11] & 0xF);

	buf1 = t_buf[13] / 10;
	buf2 = t_buf[13] % 10;
	first_usage_date[6] = '0' + (buf1 & 0xF);
	first_usage_date[7] = '0' + (buf2 & 0xF);

	switch (t_buf[12]){
	case 10:
		first_usage_date[4] = '1';
		first_usage_date[5] = '0';
		break;
	case 11:
		first_usage_date[4] = '1';
		first_usage_date[5] = '1';
		break;
	case 12:
		first_usage_date[4] = '1';
		first_usage_date[5] = '2';
		break;
	default:
		first_usage_date[4] = '0';
		first_usage_date[5] = '0' + (t_buf[12] & 0xF);
		break;
	}

	if (strncmp(&first_usage_date[3], "00000", 5)) {
		first_usage_date[0] = '2';
		first_usage_date[2] = '2';
	} //read date != 00000000, show date
	mca_log_err("read first_usage_date=%s\n", first_usage_date);
	len = strlen(first_usage_date);
	for (i = 0; i < len; i++)
		buf[i] = first_usage_date[i];

	return 0;
}

static void fg_write_fake_first_usage_date(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	int ret, len;
	unsigned char data[32] = { 0 };
	char *tmp_buf = NULL;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	len = strlen(tmp_buf);

	strscpy(tmp_buf, buf, size + 1);
	mca_log_info("write first_usage_date=%s,len=%d\n", tmp_buf,len);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
		mca_log_info("failed to get first_usage\n");
		return;
	}

	/*example,20241220,2024(0x34)1(0x31)2(0x32)2(0x32)0(0x30)*/
	data[11] = (tmp_buf[3] & 0xF);
	data[13] = (tmp_buf[6] & 0xF) * 10 + (tmp_buf[7] & 0xF);

	if (tmp_buf[4] == 0x31)
		data[12] = 0x0A;
	else
		data[12] = 0x00;

	data[12] = data[12] + (tmp_buf[5] & 0xF);

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
		mca_log_err("write first_usage_date failed\n");
}

static void fg_write_first_usage_date(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	int ret, len;
	unsigned char data[32] = { 0 };
	unsigned char data_temp[32] = { 0 };
	char *tmp_buf = NULL;
	int retry = 0;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	len = strlen(tmp_buf);

	strscpy(tmp_buf, buf, size + 1);
	mca_log_info("write first_usage_date=%s,len=%d\n", tmp_buf,len);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
#line 2693
		mca_log_info("failed to get first_usage\n");
		return;
	}

	if ((data[11] == 0x00 && data[12] == 0x00 && data[13] == 0x00) ||
	(data[11] == 0xFF && data[12] == 0xFF && data[13] == 0xFF)) {
		mca_log_err("first_usage_date invalid, write\n");
	} else {
		mca_log_err("first_usage_date valid,do not write\n");
		return;
	}

	/*example,20241220,2024(0x34)1(0x31)2(0x32)2(0x32)0(0x30)*/
	data[11] = (tmp_buf[3] & 0xF);
	data[13] = (tmp_buf[6] & 0xF) * 10 + (tmp_buf[7] & 0xF);

	if (tmp_buf[4] == 0x31)
		data[12] = 0x0A;
	else
		data[12] = 0x00;

	data[12] = data[12] + (tmp_buf[5] & 0xF);

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
		mca_log_err("write first_usage_date failed\n");

	while (retry <= 2) {
		msleep(100);
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data_temp, 32);
		retry++;
		if (ret < 0) {
#line 2715
			mca_log_info("failed to read first_usage_date\n");
			continue;
		}
		if (data[11] == data_temp[11] &&
			data[12] == data_temp[12] &&
			data[13] == data_temp[13]) {
			mca_log_err("write first_usage_date success, retry: %d\n", retry);
			break;
		} else {
			mca_log_err("write again first_usage_date, retry: %d\n", retry);
			fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
		}
	}
}

static int fg_read_tte(struct bq_fg_chip *bq, int *tte)
{
	int ret;
	u16 temp;

	if (bq->regs[BQ_FG_REG_TTE] == INVALID_REG_ADDR) {
#line 2769
		mca_log_info("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TTE], &temp);
	if (ret < 0) {
#line 2775
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*tte = temp;
	return 0;
}

static int fg_read_charging_voltage(struct bq_fg_chip *bq, int *volt)
{
	int ret;
	u16 cv;

	if (bq->regs[BQ_FG_REG_CHG_VOL] == INVALID_REG_ADDR) {
#line 2792
		mca_log_info(" not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_CHG_VOL], &cv);
	if (ret < 0) {
#line 2798
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*volt = cv;
	return 0;
}

static int fg_get_temp_max(struct bq_fg_chip *bq, int *fc)
{
	char data_lifetime[32];
	int ret = 0;
	int val = 0;
	int reg_addr = 0, byte_pos = 0;

	if(bq->fg_vendor == MPC7021) {
		reg_addr = FG_MAC_CMD_LIFETIME2;
		byte_pos = 16;
	} else {
		reg_addr = FG_MAC_CMD_LIFETIME1;
		byte_pos = 6;
	}

	memset(data_lifetime, 0, sizeof(data_lifetime));
	ret = fg_mac_read_block(bq, reg_addr, data_lifetime, sizeof(data_lifetime));

	if (ret < 0)
#line 2829
		mca_log_info("failed to get FG_MAC_CMD_LIFETIME1\n");
	val = data_lifetime[byte_pos];

	if (val < 20 || val > 60) {
		ret = fg_mac_read_block(bq, reg_addr, data_lifetime, sizeof(data_lifetime));
		if (ret < 0)
			mca_log_info("failed to get FG_MAC_CMD_LIFETIME\n");
		val = data_lifetime[byte_pos];
	}
#line 2835
	mca_log_info("fg get temperature max is: %d\n", val);

	*fc = val;
	return 0;
}

static int fg_read_ttf(struct bq_fg_chip *bq, int *ttf)
{
	int ret;
	u16 temp;

	if (bq->regs[BQ_FG_REG_TTE] == INVALID_REG_ADDR) {
#line 2845
		mca_log_info("Time To Empty not supported!\n");
		return -1;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_TTF], &temp);
	if (ret < 0) {
#line 2851
		mca_log_info("could not read Time To Empty, ret=%d\n", ret);
		return ret;
	}

	if (ret == 0xFFFF)
		return -ENODATA;

	*ttf = temp;
	return 0;
}

static int fg_set_co(struct bq_fg_chip *bq, bool value)
{
	int ret = 0;
	bool chg;
	u8 flag;
	u8 data[4] = {0xa1, 0xb2, 0xc3, 0xd4};

	if (value) {
		//1.enable
		ret =  fg_mac_write_block(bq, FG_MAC_CMD_ENABLE_CO, data, 4);
		if (ret < 0) {
			mca_log_err("enable co fail\n");
		}
		//2.close co
		ret =  fg_mac_write_block(bq, FG_MAC_CMD_CLOSE_CO, data, 4);
		if (ret < 0) {
			mca_log_err("close co fail\n");
		}
		//3.check status
		ret = fg_mac_read_block(bq, FG_MCA_CMD_SEAL_STATE, &flag, 1);
		if (ret < 0) {
			mca_log_info("failed to get co state\n");
		}
		chg = !!(flag & FG_FLAGS_CO);
		if (chg) {
			ret =  fg_mac_write_block(bq, FG_MAC_CMD_CLOSE_CO, data, 4);
			if (ret < 0) {
				mca_log_err("enable co fail\n");
			}
		}
	} else {
		//1.enable
		ret =  fg_mac_write_block(bq, FG_MAC_CMD_ENABLE_CO, data, 4);
		if (ret < 0) {
			mca_log_err("enable co fail\n");
		}
		//2.open co
		ret =  fg_mac_write_block(bq, FG_MAC_CMD_OPEN_CO, data, 4);
		if (ret < 0) {
			mca_log_err("open co fail\n");
		}
		//3.check status
		ret = fg_mac_read_block(bq, FG_MCA_CMD_SEAL_STATE, &flag, 1);
		if (ret < 0) {
			mca_log_info("failed to get co state\n");
		}
		chg = !!(flag & FG_FLAGS_CO);
		if (!chg) {
			ret =  fg_mac_write_block(bq, FG_MAC_CMD_OPEN_CO, data, 4);
			if (ret < 0) {
				mca_log_err("enable co fail\n");
			}
		}
	}

	return ret;
}

static int StringToHex(char *str, unsigned char *out, unsigned int *outlen)
{
	char *p = str;
	char high = 0, low = 0;
	int tmplen = strlen(p), cnt = 0;

	if (!p)
		return 0;

	tmplen = strlen(p);
	while (cnt < (tmplen / 2)) {
		high = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;
		low = (*(++ p) > '9' && ((*p <= 'F') || (*p <= 'f'))) ? *(p) - 48 - 7 : *(p) - 48;
		out[cnt] = ((high & 0x0f) << 4 | (low & 0x0f));
		p++;
		cnt++;
	}
	if (tmplen % 2 != 0)
		out[cnt] = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;

	if (!outlen)
		*outlen = tmplen / 2 + tmplen % 2;

	return tmplen / 2 + tmplen % 2;
}

static int fg_sha256_auth(struct bq_fg_chip *bq, u8 *rand_num, int length)
{
	int ret;
	u8 cksum_calc;
	u8 t_buf[36];
	/*
	 * 1. The host writes 0x00 to 0x3E.
	 * 2. The host writes 0x00 to 0x3F.
	 * 3. Write the random challenge should be written in a 32-byte block to address 0x40-0x5F.
	 * 4. Write the checksum (2’s complement sum of (1), (2), and (3)) to address 0x60.
	 * 5. Write the length 0x24 to address 0x61.
	 * 6. Write all 36byte data to 0X3E.
	 * 7. Wait for at least 400ms to get digest.
	*/
	t_buf[0] = 0x00;
	t_buf[1] = 0x00;

	if (length > 32)
		return  0;

	memcpy(t_buf + 2, rand_num, length);
	cksum_calc = checksum(rand_num, length);
	t_buf[2 + length] = cksum_calc;
	t_buf[2 + length + 1] = 0x24;
	atomic_set(&bq->digest_in_process, 1); /* 1: set flag */
	ret = fg_write_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, length + 4);
	if (ret < 0) {
#line 2988
		mca_log_err("batt_auth write BQ_FG_REG_ALT_MAC fail\n");
		atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */
		return ret;
	}
	msleep(400);

	ret = fg_read_block(bq, bq->regs[BQ_FG_REG_MAC_DATA], bq->digest, length);
	if (ret < 0) {
#line 2996
		mca_log_err("batt_auth read verify_digest fail\n");
		atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */
		return ret;
	}

	atomic_set(&bq->digest_in_process, 0); /* 0: clear flag */

	return 0;
}

static int get_verify_digest(struct bq_fg_chip *bq, char *buf)
{
	u8 digest_buf[4];
	int i;
	int len;

	if (!bq)
		return 0;

	for (i = 0; i < BATTERY_DIGEST_LEN; i++) {
		memset(digest_buf, 0, sizeof(digest_buf));
		snprintf(digest_buf, sizeof(digest_buf) - 1, "%02x", bq->digest[i]);
		strlcat(buf, digest_buf, BATTERY_DIGEST_LEN * 2 + 1);
	}
	len = strlen(buf);
	buf[len] = '\0';

#line 3023
	mca_log_info("buf = %s\n", buf);
	return strlen(buf) + 1;
}

static int set_verify_digest(struct bq_fg_chip *bq, u8 *rand_num)
{
	int i;
	u8 random[BATTERY_DIGEST_LEN] = {0};
	char kbuf[BATTERY_RANDOM_LEN + 1] = {0};

	if (!bq)
		return 0;

	strscpy(kbuf, rand_num, BATTERY_RANDOM_LEN + 1);
	StringToHex(kbuf, random, &i);
	fg_sha256_auth(bq, random, BATTERY_DIGEST_LEN);

	return 0;
}

static void fg_update_lifetime_data(struct bq_fg_chip *bq)
{
	int ret;
	u8 t_buf[40] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 32);
	if (ret < 0)
		return;

	switch(bq->fg_vendor) {
	case MPC7021:
		bq->cell1_max = (t_buf[1] << 8 | t_buf[0]) + (t_buf[3] << 8 | t_buf[2]);
		bq->max_charge_current = (t_buf[9] << 8) | t_buf[8];
		bq->max_discharge_current = (signed short int)((t_buf[11] << 8) | t_buf[10]);
		bq->total_fw_runtime = (t_buf[15] << 24) | (t_buf[14] << 16) | (t_buf[13] << 8) | t_buf[12];
		bq->time_spent_in_lt = (t_buf[23] << 24) | (t_buf[22] << 16) | (t_buf[21] << 8) | t_buf[20];
		break;
	default:
		bq->cell1_max = (t_buf[1] << 8) | t_buf[0];
		bq->max_charge_current = (t_buf[3] << 8) | t_buf[2];
		bq->max_discharge_current = (signed short int)((t_buf[5] << 8) | t_buf[4]);

		bq->max_temp_cell = t_buf[6];
		bq->min_temp_cell = t_buf[7];
		break;
	}

	memset(t_buf, 0, sizeof(t_buf));

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 32);
		if (ret < 0)
			return;
		bq->max_temp_cell = t_buf[16];
		bq->min_temp_cell = t_buf[17];
		bq->time_spent_in_ht = (t_buf[7] << 24) | (t_buf[6] << 16) | (t_buf[5] << 8) | t_buf[4];
		bq->time_spent_in_ot = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | t_buf[8];
		break;
	default:
		break;
	}

	memset(t_buf, 0, sizeof(t_buf));

	switch(bq->fg_vendor) {
	case MPC7021:
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, 32);
		if (ret < 0)
			return;

		bq->total_fw_runtime = (t_buf[1] << 8) | t_buf[0];
		bq->time_spent_in_lt = (t_buf[5] << 8) | t_buf[4];
		bq->time_spent_in_ht = (t_buf[13] << 8) | t_buf[12];
		bq->time_spent_in_ot = (t_buf[15] << 8) | t_buf[14];
		break;
	}
}

static void fg_update_aged_flag(struct bq_fg_chip *bq)
{
	int cc = 0;

	fg_read_cyclecount(bq, &cc);

	if (cc > bq->cycle_th) {
		bq->soh_th -= 1;
		bq->cycle_th += bq->cycle_step;
		bq->aged_flag = 0;
	}

#line 4646
	mca_log_info("ui_soh %d soh_th %d\n", bq->ui_soh, bq->soh_th);
	if (bq->ui_soh < bq->soh_th)
		bq->aged_flag = 1;
}

static void fg_update_data_work(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int chip_ok = 0;

	fg_get_chip_ok(bq, &chip_ok);
	if (!chip_ok)
		return;

	fg_update_aged_flag(bq);
	fg_update_lifetime_data(bq);
	fg_get_gague_mode(bq);
	if (bq->update_fw_status == FG_FW_INIT)
		fg_update_record_voltage_level(bq);

}

static void fg_set_force_report_full(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	u8 t_buf[2] = {0};
	int ret = 0;

	t_buf[0] = 0x31;
	t_buf[1] = 0x00;
	ret = fg_write_block(bq, bq->regs[BQ_FG_REG_ALT_MAC], t_buf, 2);
	if (ret < 0) {
#line 4678
		mca_log_err("set force_report_full fail\n");
		return;
	}
#line 4681
	mca_log_info("set force_report_full success\n");
}

static int fg_get_chip_ok(struct bq_fg_chip *bq, int *ok)
{
	u16 flags;
	int ret = 0, times = 0;

	while (times < FG_ERROR_CHECK_MAX_TIMES) {
		ret = fg_read_word(bq, bq->regs[BQ_FG_REG_BATT_STATUS], &flags);
		if (ret < 0) {
#line 4692
			mca_log_info("i2c read fail, retry %d\n", times);
			bq->fg_error = true;
			times++;
		} else {
			bq->fg_error = false;
			break;
		}
	}

	*ok =  !bq->fg_error;
	return 0;
}

static int fg_get_raw_soc(struct bq_fg_chip *bq, int *raw_soc)
{
	int rm, fcc;
	int ret;

	ret = fg_read_rm(bq, &rm);
	ret |= fg_read_fcc(bq, &fcc);
	if (ret)
		return -1;

	rm = rm / 1000;
	fcc = fcc / 1000;

	*raw_soc = (rm * 10000) / fcc;
	return 0;
}

static int fg_read_tsim(struct bq_fg_chip *bq, u32 *tsim)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 36);
	if (ret < 0)
		return ret;

	if (bq->fg_vendor == BQ27Z561)
		*tsim = (t_buf[13] << 8) | t_buf[12];
	else
		*tsim = (t_buf[17] << 8) | t_buf[16];

	return ret;
}

static int fg_read_tambient(struct bq_fg_chip *bq, u32 *tambient)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 36);
	if (ret < 0)
		return ret;

	if (bq->fg_vendor == BQ27Z561)
		*tambient = (t_buf[15] << 8) | (t_buf[14]);
	else
		*tambient = (t_buf[9] << 8) | (t_buf[8]);

	return ret;
}

static int fg_read_tremq(struct bq_fg_chip *bq, u32 *tremq)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 36);
	if (ret < 0)
		return ret;

	*tremq = (t_buf[1] << 8) | (t_buf[0]);
	return ret;
}

static int fg_read_tfullchgq(struct bq_fg_chip *bq, u32 *tfullchgq)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = { 0 };

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, 36);
	if (ret < 0)
		return ret;

	*tfullchgq = (t_buf[3] << 8) | (t_buf[2]);
	return ret;
}

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

/**ti fg unseal procee*/
static int fg_unseal_send_key_for_ti(struct bq_fg_chip *bq)
{
	int value = 0;
	int ret = 0;
	u8 cmd[2] = {0x2e, 0x00};

	value = (bq->key[2] << 8) | bq->key[3];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4805
		mca_log_err(" step 1 first word write fail");
		return ret;
	}

	msleep(4000);

	ret |= fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4813
		mca_log_err("step 2 first word write fail");
		return ret;
	}

	usleep_range(5000, 5100);

	value = (bq->key[0] << 8) | bq->key[1];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4822
		mca_log_err(" step 3 second word write fail");
		return ret;
	}

	ret |= fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4828
		mca_log_err("step 4 second word write fail");
		return ret;
	}

	msleep(200);

#line 4834
	mca_log_err("fg sunseal success");

	ret |= fg_write_block(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], cmd, 2);
	if (ret < 0) {
#line 4838
		mca_log_err("step 5 data flush failed,");
		return ret;
	}
	return ret;
}

static int fg_unseal_send_key_for_nvt(struct bq_fg_chip *bq)
{
	int value = 0;
	int ret = 0;

	value = (bq->key[1] << 8) | bq->key[0];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4852
		mca_log_err(" step 1 first word write fail");
		return ret;
	}

	msleep(300);

	value = (bq->key[3] << 8) | bq->key[2];
	ret = fg_write_word(bq, bq27z561_regs[BQ_FG_REG_ALT_MAC], value & 0xFFFF);
	if (ret < 0) {
#line 4861
		mca_log_err(" step 2 second word write fail");
		return ret;
	}

	msleep(300);
#line 4866
	mca_log_err("fg unseal success");
	return ret;
}


#define UNSEAL_COUNT_MAX 3
static int fg_set_seal(struct bq_fg_chip *bq, int value)
{
	int ret;
	int seal_status = 0;
	int count = 0;

	if (value) {
		ret = fg_get_seal(bq, &seal_status);
		if (seal_status == SEAL_STATE_FA || seal_status == SEAL_STATE_UNSEALED) {
#line 4881
			mca_log_info("FG is unsealed");
			return 0;
		}
		while (count++ < UNSEAL_COUNT_MAX) {
			if (bq->fg_vendor == NFG1000A || bq->fg_vendor == NFG1000B ||
				bq->fg_vendor ==  MPC8011B || bq->fg_vendor == MPC7012) {
				ret = fg_unseal_send_key_for_nvt(bq);
			} else if (bq->fg_vendor == BQ27Z561) {
				ret = fg_unseal_send_key_for_ti(bq);
			} else {
#line 4891
				mca_log_info("unseal don't support for venddor %d\n", bq->fg_vendor);
				break;
			}
			ret |= fg_get_seal(bq, &seal_status);
			if (seal_status == SEAL_STATE_FA || seal_status == SEAL_STATE_UNSEALED) {
#line 4896
				mca_log_info("FG is unsealed");
				break;
			}
			msleep(100);
		}
	} else {
		if (seal_status == SEAL_STATE_SEALED) {
#line 4913
			mca_log_info("FG is sealed");
			return 0;
		}
		while (count++ < UNSEAL_COUNT_MAX) {
			ret = fg_write_word(bq, bq->regs[BQ_FG_REG_ALT_MAC], FG_MAC_CMD_SEAL);
			if (ret < 0)
#line 4909
				mca_log_err("Failed to send seal command");
			ret = fg_get_seal(bq, &seal_status);
			if (seal_status == SEAL_STATE_SEALED) {
#line 4913
				mca_log_info("FG is sealed");
				break;
			}
			msleep(100);
		}
	}


	return ret;
}

#define SEAL_STATUS_MASK 0x03
static int fg_get_seal(struct bq_fg_chip *bq, int *value)
{
	int ret;
	u8 t_buf[BLOCK_LENGTH_MAX] = {0};

	ret = fg_mac_read_block(bq, FG_MCA_CMD_SEAL_STATE, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;

	*value = t_buf[1] & SEAL_STATUS_MASK;

	return ret;

}

static int fg_read_df_check(struct bq_fg_chip *bq, int *value)
{
	int ret;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MCA_CMD_DF, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[1] << 8 | t_buf[0];

	return ret;
}

/* get co auto control state */
#define FC_AUTO_CTRL_MASK 0x02
/* get charge FET mos real state, 1 means charging enable */
#define CHG_FET_STATE_MASK 0x02
static void fg_get_ui_soh(void *data, int *ui_soh)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*ui_soh = info->ui_soh;
}

static int fg_read_pack_vendor(struct bq_fg_chip *bq)
{
	u8 t_buf[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
#line 4986
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
#line 4989
	mca_log_info("%s\n", t_buf);

	switch (t_buf[2]) {
	case 'B':
		bq->pack_vendor = PACK_SUPPLIER_BYD;
		break;
	case 'C':
		bq->pack_vendor = PACK_SUPPLIER_COSLIGHT;
		break;
	case 'S':
		bq->pack_vendor = PACK_SUPPLIER_SUNWODA;
		break;
	case 'N':
		bq->pack_vendor = PACK_SUPPLIER_NVT;
		break;
	case 'U':
		bq->pack_vendor = PACK_SUPPLIER_SCUD;
		break;
	case 'T':
		bq->pack_vendor = PACK_SUPPLIER_TWS;
		break;
	case 'I':
		bq->pack_vendor = PACK_SUPPLIER_LISHEN;
		break;
	case 'K':
		bq->pack_vendor = PACK_SUPPLIER_DESAY;
		break;
	default:
		bq->pack_vendor = PACK_SUPPLIER_NVT;
		break;
	}
	return 0;
}

static int fg_read_vendor(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;

#line 5032
	mca_log_info("fg_vendor = %d", t_buf[2]);

	switch (t_buf[2]) {
	case '4':
		bq->fg_vendor = NFG1000B;
		break;
	case '5':
		bq->fg_vendor = NFG1000A;
		break;
	case '6':
		bq->fg_vendor = MPC8011B;
		break;
	case '7':
		bq->fg_vendor = MPC7012;
		break;
	case 'C':
		bq->fg_vendor = BQ27Z561;
		break;
	case '8':
		bq->fg_vendor = MPC7021;
		break;
	default:
		bq->fg_vendor = MPC8011B;
		break;
	}
	return ret;
}

#define FG_FW_R0	0x0001
#define FG_FW_R1	0x0102
static int fg_read_fw_version(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[4] = {0};
	int value = 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FW_VER, t_buf, 4);
	if (ret < 0)
		return ret;

	value = (t_buf[2] << 8) | (t_buf[3]);
	if (value == FG_FW_R0)
		bq->fw_ver = FW_R0;
	else if (value == FG_FW_R1)
		bq->fw_ver = FW_R1;
	else
		bq->fw_ver = FW_DEFAULT;

	return ret;
}

static int fg_read_eeprom_version(struct bq_fg_chip *bq)
{
	u8 t_buf[4];
	int ret;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 4);
	if (ret < 0) {
#line 5099
		mca_log_info("failed to get MANE NAME\n");
		return 0;
	}
#line 5102
	mca_log_info("%c", t_buf[3]);

	bq->eeprom_version = t_buf[3];

	return 0;
}

static int fg_read_max_temp_occur_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*value = (t_buf[15] << 24) | (t_buf[14] << 16) | (t_buf[13] << 8) | t_buf[12];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = t_buf[20] | (t_buf[21] << 8) | (t_buf[22] << 16) | (t_buf[23] << 24);
		break;
	}

#line 5118
	mca_log_info("%d", *value);
	return ret;
}

static int fg_read_run_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_ITSTATUS1, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[20] | (t_buf[21] << 8) | (t_buf[22] << 16) | (t_buf[23] << 24);
#line 5131
	mca_log_info("%d", *value);
	return ret;
}

static int fg_read_max_temp_time(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	int max_temp_time = 0;
	int run_time = 0;

	ret = fg_read_max_temp_occur_time(bq, &max_temp_time);
	ret |= fg_read_run_time(bq, &run_time);

	*value = run_time > max_temp_time ? ((run_time - max_temp_time) / 60) : 0;
	return ret;
}

static int fg_read_total_fw_runtime(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	ret = fg_mac_read_block(bq, FG_MAC_CMD_FW_RUNTIME, t_buf, REG_BLOCK_LENGTH);
	if (ret < 0)
		return ret;
	*value = t_buf[8] | (t_buf[9] << 8);
#line 5157
	mca_log_info("%d", *value);
	return ret;
}

static int fg_get_temp_min(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[BLOCK_LENGTH_MAX] = {0};

	switch(bq->fg_vendor) {
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, 20);
		if (ret < 0)
			return ret;
		*value = t_buf[17];
		break;
	default:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME1, t_buf, 36);
		if (ret < 0)
			return ret;

		*value = t_buf[7];
		break;
	}

	return ret;
}

static int fg_read_time_ht(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch (bq->fg_vendor) {
	case BQ27Z561:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[19] << 8) | (t_buf[18])) * 2;
		break;
	case NFG1000A:
	case NFG1000B:
	case MPC8011B:
	case MPC7012:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[13] << 8) | (t_buf[12]));
		break;
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = (t_buf[7] << 24) | (t_buf[6] << 16) | (t_buf[5] << 8) | t_buf[4];
		break;
	default:
		break;
	}
	return ret;
}

static int fg_read_time_ot(struct bq_fg_chip *bq, int *value)
{
	int ret = 0;
	u8 t_buf[REG_BLOCK_LENGTH] = {0};

	switch (bq->fg_vendor) {
	case BQ27Z561:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[21] << 8) | (t_buf[20])) * 2;
		break;
	case NFG1000A:
	case NFG1000B:
	case MPC8011B:
	case MPC7012:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME3, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;
		*value = ((t_buf[15] << 8) | (t_buf[14]));
		break;
	case MPC7021:
		ret = fg_mac_read_block(bq, FG_MAC_CMD_LIFETIME2, t_buf, REG_BLOCK_LENGTH);
		if (ret < 0)
			return ret;

		*value = (t_buf[11] << 24) | (t_buf[10] << 16) | (t_buf[9] << 8) | t_buf[8];
		break;
	default:
		break;
	}
	return ret;
}

static int fg_get_soc_decimal_rate(struct bq_fg_chip *bq, int *rate)
{
	int soc, i;
	int ret;

	if (bq->dec_rate_len <= 0)
		return 0;

	ret = fg_read_rsoc(bq, &soc);
	if (ret)
		return -1;

	for (i = 0; i < bq->dec_rate_len; i += 2) {
		if (soc < bq->dec_rate_seq[i]) {
			*rate = bq->dec_rate_seq[i - 1];
			return 0;
		}
	}

	*rate = bq->dec_rate_seq[bq->dec_rate_len - 1];
	return 0;
}

static int fg_get_soc_decimal(struct bq_fg_chip *bq, int *soc_decimal)
{
	int rsoc, raw_soc;

	if (!bq)
		return 0;

	fg_read_rsoc(bq, &rsoc);
	fg_get_raw_soc(bq, &raw_soc);

	*soc_decimal = raw_soc % 100;
	return 0;
}

static noinline __used int fg_read_packvolt(struct bq_fg_chip *bq, int *vbat)
{
	int ret;
	u16 volt = 0;

	if (bq->fg_error)
		return 3700;

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_PACK_BATT], &volt);
	if (ret < 0) {
#line 865
		mca_log_err("could not read volt ret = %d\n", ret);
		return ret;
	}

	*vbat = fg_convert_bytes_to_volt(bq, (u8 *)&volt);
	return 0;
}

static int fg_convert_bytes_to_original_temp(struct bq_fg_chip *fg, u8 *bytes)
{
	u16 value = BYTES_TO_U16(bytes);
	int temp = value - 2730;

	if (temp < FG_TEMP_LOW_TH || temp > FG_TEMP_HIGH_TH || fg->ota_updating) {
#line 934
		mca_log_err("temperature out of range: %d\n", temp);
		temp = fg->batt_ori_temp;
	}

	if (fg->fake_temp != FG_FAKE_TEMP_NONE)
		temp = fg->fake_temp;

	fg->batt_ori_temp = temp;
#line 942
	mca_log_info("temperature: %d\n", temp);
	return fg->batt_ori_temp;
}

static noinline int fg_read_original_temperature(struct bq_fg_chip *bq, int *temp)
{
	int ret;
	u16 temp_now = 0;

	if (bq->fg_error) {
		*temp = FG_ERROR_FAKE_TEMP;
		return 0;
	}

	ret = fg_read_word(bq, bq->regs[BQ_FG_REG_ORIGINAL_TEMP], &temp_now);
	if (ret < 0) {
#line 817
		mca_log_info("could not read temperature, ret = %d\n", ret);
		return ret;
	}

	*temp = fg_convert_bytes_to_original_temp(bq, (u8 *)&temp_now);
	return 0;
}

static noinline int fg_get_vsiloss(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (bq->fake_v_siloss) {
		*val = bq->fake_v_siloss;
		return 0;
	}

	ret = fg_mac_read_block(bq, 0xf0, buf, 32);
	if (ret < 0) {
#line 5659
		mca_log_err("could not read vsiloss = %d\n", ret);
		return ret;
	}

	*val = (buf[1] << 8) | buf[0];
	return 0;
}

static noinline int fg_get_iaver(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (bq->fake_i_aver) {
		*val = bq->fake_i_aver;
		return 0;
	}

	ret = fg_mac_read_block(bq, 0xd8, buf, 32);
	if (ret < 0) {
#line 5680
		mca_log_err("could not read i_aver = %d\n", ret);
		return ret;
	}

	*val = (s16)((buf[3] << 8) | buf[2]);
	return 0;
}

static noinline int fg_get_fast_mode_times(struct bq_fg_chip *bq, int *val)
{
	u8 buf[32] = { 0 };
	int ret;

	if (!val) {
#line 5766
		mca_log_err("fast_mode_times pointer is NULL\n");
		return -EINVAL;
	}

	ret = fg_mac_read_block(bq, 0xe5, buf, 32);
	if (ret < 0) {
#line 5772
		mca_log_err("could not read fast_mode_times = %d\n", ret);
		return ret;
	}

	*val = (buf[1] << 8) | buf[0];
	return 0;
}

static int fg_get_one_fcc_default(void *data, bool *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	*val = bq->enable_fcc_default;
	return 0;
}

static void fg_get_pack_vendor(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	*val = bq->pack_vendor;
}

static void fg_get_ota_update_flag(void *data, unsigned int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	unsigned int flag = bq->unk_1164;

	/* GUESS: the member at +1164 (unk_1164) is the OTA update flag, from the function name and the log text */
	*val = flag;
#line 7231
	mca_log_info("get ota update_flag: %x\n", flag);
}

static void fg_get_one_average_current(void *data, int *val)
{
	*val = fg_get_average_current((struct bq_fg_chip *)data);
}

static int fg_get_one_iaver(void *data, int *val)
{
	if (!data || !val)
		return -EINVAL;
	return fg_get_iaver((struct bq_fg_chip *)data, val);
}

static int fg_get_one_vsiloss(void *data, int *val)
{
	if (!data || !val)
		return -EINVAL;
	return fg_get_vsiloss((struct bq_fg_chip *)data, val);
}

static int fg_get_one_fg_vendor(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (!bq || !val)
		return -EINVAL;
	*val = bq->fg_vendor;
	return 0;
}

static int fg_get_one_batt_qmax(void *data, int *val)
{
	if (data && val)
		return fg_read_qmax((struct bq_fg_chip *)data, (u32 *)val);

#line 7323
	mca_log_err("fg_get_one_batt_qmax err\n");
	return -EINVAL;
}

static int fg_get_one_csd_flag(void *data, int *val)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	u8 buf[64] = { 0 };
	int ret;

	if (!bq || !val) {
#line 7311
		mca_log_err("fg_get_one_csd_flag err\n");
		return -EINVAL;
	}

	ret = fg_mac_read_block(bq, 0x10a2, buf, 8);
	if (ret < 0)
		return ret;
	*val = buf[1];
	return ret;
}

static int fg_get_one_manufacturing_date(void *data, unsigned char *buf)
{
	return fg_read_manufacturing_date((struct bq_fg_chip *)data, buf);
}

static int fg_get_one_first_usage_date(void *data, unsigned char *buf)
{
	return fg_read_first_usage_date((struct bq_fg_chip *)data, buf);
}

static int fg_get_one_fast_mode_times(void *data, int *val)
{
	return fg_get_fast_mode_times((struct bq_fg_chip *)data, val);
}

static int fg_read_one_original_temperature(void *data, int *val)
{
	return fg_read_original_temperature((struct bq_fg_chip *)data, val);
}

static int fg_read_one_probe_ok(void *data, bool *ok)
{
	*ok = 1;
	return 0;
}

#define TERM_VOLTAGE_MIN 3000
#define TERM_VOLTAGE_MAX 3400
static int fg_set_cutoff_voltage(struct bq_fg_chip *bq, int value)
{
	int ret = 0;
	u8 data[2] = {0xb8, 0x0b};

	if (value < TERM_VOLTAGE_MIN && value > TERM_VOLTAGE_MAX) {
		mca_log_info("term_voltage over range\n");
		return ret;
	}

	data[0] = (value & 0xFF);
	data[1] = (value >> 8) & 0xFF;
#line 5281
	mca_log_info("data[0] = %d, data[1] = %d, value = %d", data[0], data[1], value);
	ret = fg_mac_write_block(bq, FG_MAC_CMD_CUTOFF_VOLTAGE, data, 2);
	if (ret < 0) {
#line 5287
		mca_log_err("could not write term_voltage = %d\n", ret);
		return ret;
	}
	return ret;
}

static int fg_get_cutoff_voltage(struct bq_fg_chip *bq, int *volt)
{
	u8 data[2] = {0};
	int ret;
	int value = 3000;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_CUTOFF_VOLTAGE, data, 2);
	if (ret < 0) {
#line 5304
		mca_log_err("could not read cutoff_voltage = %d\n", ret);
		return ret;
	}
	value = (data[1] << 8) | data[0];

	*volt = value;
	return 0;
}

#define THE_10_BYTE 0xB2
#define THE_11_BYTE 0x0C
#define THE_12_BYTE 0x66
#define THE_13_BYTE 0x0D
#define VERSION_NUMBER 0x42
#define DF_CHECK_FIRST 0X5f
#define DF_CHECK_SECOND 0X80
#define MAX_RETRY_FOR_UPDATE_FW 0x03
static int fg_update_record_voltage_level(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[36] = {0};
	int seal_status = 0;
	int count = 0;
	int address = bq->start_byte_address;
	bool check_status = 0;

	if (!bq->support_voltage_record_level || bq->fg_vendor != MPC8011B) {
		mca_log_err("can't support ota \n");
		bq->update_fw_status = FG_FW_INVALID;
		return -1;
	}

	/*First if updated*/
	ret = fg_mac_read_block(bq, FG_MAC_CMD_CHEM_NAME, t_buf, 6);
	if (t_buf[3] >= bq->version_number) {
#line 5344
		mca_log_info("it has been upteded version_number[0x%x][0x%x]\n", t_buf[3], bq->version_number);
		bq->update_fw_status = FG_FW_SUCCESS;
		return 0;
	}
	/*Second Update Fw*/
	ret = fg_set_seal(bq, true);
	ret |= fg_get_seal(bq, &seal_status);
	if (seal_status != SEAL_STATE_UNSEALED && seal_status != SEAL_STATE_FA) {
#line 5352
		mca_log_info("Fg Unseal Failed\n");
		bq->update_fw_status = FG_FW_ERROR;
		return ret;
	}

	/*Modify Vcutoff*/
	while (count++ < MAX_RETRY_FOR_UPDATE_FW) {
		memset(t_buf, 0, sizeof(t_buf));
		check_status = false;
		ret = fg_mac_read_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
#line 5363
			mca_log_err("could not read record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		for (int i = 0; i < bq->byte_length; i++) {
			t_buf[address + i] = bq->record_voltage[i];
#line 5369
			mca_log_info("Reg:t_buf[%d]=[0x%x]\n", address + i, t_buf[address + i]);
		}

		ret = fg_mac_write_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
#line 5374
			mca_log_err("could not write record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		msleep(100);
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_RECORD_VOLTAGE_LEVEL, t_buf, 32);
		if (ret < 0) {
#line 5382
			mca_log_err("could not read record_voltage_level, ret=%d\n", ret);
			msleep(100);
			continue;
		}

		for (int i = 0; i < bq->byte_length; i++) {
			if (t_buf[address + i] != bq->record_voltage[i]) {
				check_status = true;
				break;
			}
#line 5392
			mca_log_info("Update:t_buf[%d]=[0x%x]\n", address + i, t_buf[address + i]);
		}

		if (check_status) {
#line 5396
			mca_log_err("Modify record_voltage_level Error\n");
			msleep(100);
			continue;
		} else {
#line 5400
			mca_log_info("Modify record_voltage_level Suceess\n");
			break;
		}
	}

	if (count >= MAX_RETRY_FOR_UPDATE_FW) {
		bq->update_fw_status = FG_FW_ERROR;
		goto error;
	}

	count = 0;
	/*Modify Version Number*/
	while (count++ < MAX_RETRY_FOR_UPDATE_FW) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		t_buf[3] = bq->version_number;
		ret = fg_mac_write_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		if (ret < 0) {
#line 5418
			mca_log_err("could not write update_version, ret=%d\n", ret);
			msleep(100);
			continue;
		}
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_UPDATE_VERSION, t_buf, 32);
		if (ret < 0) {
#line 5425
			mca_log_err("could not read update_version, ret=%d\n", ret);
			msleep(100);
			continue;
		}

		if (t_buf[3] != bq->version_number) {
#line 5431
			mca_log_err("Modify update_version Error\n");
			msleep(100);
			continue;
		} else {
#line 5435
			mca_log_info("Modify update_version Sucess\n");
			break;
		}
	}

	if (count >= MAX_RETRY_FOR_UPDATE_FW) {
		bq->update_fw_status = FG_FW_ERROR;
		goto error;
	}

	bq->update_fw_status = FG_FW_SUCCESS;
error:
	fg_set_seal(bq, false);
	return ret;
}

static int fg_clear_count_data(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 t_buf[4] = {0xD3, 0xC2, 0xB1, 0xA0};

	ret = fg_mac_write_block(bq, FG_MAC_CMD_CLEAR_COUNT_DATA, t_buf, 4);
	if (ret < 0) {
#line 5458
		mca_log_err("could not write clear_cout_data = %d\n", ret);
		return ret;
	}

	return 0;
}


static const char *fg_get_device_name(struct bq_fg_chip *bq)
{
	int ret;
	char data[8] = { 0 };
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (bq->fake_device_name[0])
		return bq->fake_device_name;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_DEVICE_NAME, data, 8);
	if (ret < 0) {
#line 5477
		mca_log_err("could not read device_name, ret=%d\n", ret);
		/* stock: "NULL" */
		strncpy(bq->device_name, "NULL", 7);
		bq->device_name[7] = 0;
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

#line 5497
	mca_log_info("0x004A: %.*s, model_name: %.*s\n", (int)strnlen(data, 7), data,
		     (int)strnlen(bq->device_name, 7), bq->device_name);

	return bq->device_name;
}

static int fg_get_adapt_power(struct bq_fg_chip *bq, int *adapt_power)
{
	int ret = 0;
	u8 t_buf[5];
	static char ch = '\0';

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, t_buf, 5);
	if (ret < 0) {
#line 5513
		mca_log_err("failed to get adapt power = %d\n", ret);
		return ret;
	}
	ch = t_buf[4];

	switch (ch) {
	case '0':
		bq->adapt_power = 10;
		break;
	case '1':
		bq->adapt_power = 15;
		break;
	case '2':
		bq->adapt_power = 18;
		break;
	case '3':
		bq->adapt_power = 25;
		break;
	case '4':
		bq->adapt_power = 33;
		break;
	case '5':
		bq->adapt_power = 35;
		break;
	case '6':
		bq->adapt_power = 40;
		break;
	case '7':
		bq->adapt_power = 55;
		break;
	case '8':
		bq->adapt_power = 60;
		break;
	case '9':
		bq->adapt_power = 67;
		break;
	case 'A':
		bq->adapt_power = 80;
		break;
	case 'B':
		bq->adapt_power = 90;
		break;
	case 'C':
		bq->adapt_power = 100;
		break;
	case 'D':
		bq->adapt_power = 120;
		break;
	case 'E':
		bq->adapt_power = 140;
		break;
	case 'F':
		bq->adapt_power = 160;
		break;
	case 'G':
		bq->adapt_power = 180;
		break;
	case 'H':
		bq->adapt_power = 200;
		break;
	case 'I':
		bq->adapt_power = 220;
		break;
	case 'J':
		bq->adapt_power = 240;
		break;
	default:
		bq->adapt_power = 0;
		break;
	}
#line 5583
	mca_log_info("ch: %c, adapt_power: %d\n", ch, bq->adapt_power);

	*adapt_power = bq->adapt_power;

	return ret;
}

static int fg_get_dod_count(struct bq_fg_chip *bq)
{
	u8 data[REG_BLOCK_LENGTH] = {0};
	int ret = 0;

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MIXDATARECORD1, data, REG_BLOCK_LENGTH);
	if (ret < 0) {
#line 5605
		mca_log_err("could not read dod_count = %d\n", ret);
		return ret;
	}

	bq->count_level1 = fg_convert_u8_to_u16(data[8], data[9]);
	bq->count_level2 = fg_convert_u8_to_u16(data[10], data[11]);
	bq->count_level3 = fg_convert_u8_to_u16(data[12], data[13]);
	bq->count_lowtemp = fg_convert_u8_to_u16(data[20], data[21]);

#line 5614
	mca_log_info("dod_count: %d, %d, %d, %d\n",
		bq->count_level1, bq->count_level2, bq->count_level3, bq->count_lowtemp);

	return 0;
}

static int fg_get_count_level1(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level1)
		*count = bq->fake_count_level1;
	else
		*count = bq->count_level1;
	return 0;
}

static int fg_get_count_level2(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level2)
		*count = bq->fake_count_level2;
	else
		*count = bq->count_level2;
	return 0;
}

static int fg_get_count_level3(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_level3)
		*count = bq->fake_count_level3;
	else
		*count = bq->count_level3;
	return 0;
}

static int fg_get_count_lowtemp(struct bq_fg_chip *bq, int *count)
{
	if (bq->fake_count_lowtemp)
		*count = bq->fake_count_lowtemp;
	else
		*count = bq->count_lowtemp;
	return 0;
}

static int fg_get_one_dod_count(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_dod_count(info);
}

static int fg_get_one_count_level1(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level1(info, count);
}

static int fg_get_one_count_level2(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level2(info, count);
}

static int fg_get_one_count_level3(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_level3(info, count);
}


static int fg_get_one_count_lowtemp(void *data, int *count)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_count_lowtemp(info, count);
}

static int fg_get_one_cutoff_voltage(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_cutoff_voltage(info, volt);
}

static int fg_set_one_cutoff_voltage(void *data, int value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_set_cutoff_voltage(info, value);
}

static int fg_set_one_clear_count_data(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_clear_count_data(info);
}

static int fg_read_one_fc(void *data, bool *fc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int rc;

	rc = fg_read_status(info);
	*fc = info->batt_fc;

	return rc;
}

static int fg_set_one_co(void *data, bool value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_set_co(info, value);
}

static int fg_read_one_rsoc(void *data, int *rsoc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_rsoc(info, rsoc);
}

static int fg_read_one_current(void *data, int *curr)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_current(info, curr);
}
static int fg_set_verify_digest(void *data, char *buf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return set_verify_digest(info, buf);
}

static int fg_get_verify_digest(void *data, char *buf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return get_verify_digest(info, buf);
}

static int fg_set_authentic(void *data, int value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	info->batt_auth = value;
	return 0;
}

static int fg_get_authentic(void *data, int *value)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	*value = info->batt_auth;
	return 0;
}

static int fg_get_error_state(void *data, bool *error)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int ok;

	(void)fg_get_chip_ok(info, &ok);

	*error = info->fg_error;
	return 0;
}

static int fg_read_one_volt(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_volt(info, volt);
}


static int fg_read_one_temperature(void *data, int *temp)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;
	int rc;

	rc = fg_read_temperature(info, temp);

	return rc;
}

static int fg_read_one_status(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_status(info);
}

static int fg_read_one_rm(void *data, int *rm)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_rm(info, rm);
}

static int fg_get_one_fastcharge_mode(void *data, int *ffc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_fastcharge_mode(info, ffc);
}

static int fg_set_one_fastcharge_mode(void *data, bool en)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_set_fastcharge_mode(info, en);
}

static int fg_get_one_soc_decimal(void *data, int *soc_decimal)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soc_decimal(info, soc_decimal);
}

static int fg_read_one_charging_voltage(void *data, int *volt)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_charging_voltage(info, volt);
}

static int fg_get_one_chip_ok(void *data, int *ok)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_chip_ok(info, ok);
}

static int fg_read_one_cyclecount(void *data, int *cc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_cyclecount(info, cc);
}

static int fg_read_one_tte(void *data, int *tte)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_tte(info, tte);
}

static int fg_read_one_ttf(void *data, int *ttf)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_ttf(info, ttf);
}

static int fg_read_one_fcc(void *data, int *fcc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_fcc(info, fcc);
}

static int fg_read_one_dc(void *data, int *dc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_dc(info, dc);
}

static int fg_get_one_soc_decimal_rate(void *data, int *rate)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soc_decimal_rate(info, rate);
}

static int fg_read_one_soh(void *data, int *soh)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_soh(info, soh);
}

static int fg_get_one_temp_max(void *data, int *temp_max)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_temp_max(info, temp_max);
}

static int fg_get_one_temp_min(void *data, int *temp_min)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_temp_min(info, temp_min);
}

static int fg_get_one_time_ot(void *data, int *time_ot)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_read_time_ot(info, time_ot);
}

static int fg_get_one_batt_cell_info(void *data, const char **name)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (info->cell_name) {
		*name = info->cell_name;
		return 0;
	}

	return -1;
}

static int fg_get_one_adapt_power(void *data, int *adapt_power)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_adapt_power(info, adapt_power);
}

static int fg_get_aged_flag(void *data, int *aged_flag)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (!data)
		return -1;

	*aged_flag = info->aged_flag;
	return 0;
}

static int fg_get_one_isc_alert_level(void *data, int *level)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_isc_alert_level(info, level);
}

static int fg_get_one_soa_alert_level(void *data, int *level)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_soa_alert_level(info, level);
}

static  int fg_get_one_raw_soc(void *data, int *raw_soc)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_raw_soc(info, raw_soc);
}

static int fg_read_batt_info(void *data, void *info)
{
	struct bq_fg_chip *fg = (struct bq_fg_chip *)data;

	return fg_get_batt_info(fg, info);
}

static int fg_get_one_calibration_ffc_iterm(void *data, int *iterm)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calibration_ffc_iterm(info, iterm);
}

static int fg_get_one_real_supplement_energy(void *data, int *supplement_energy)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_real_supplement_energy(info, supplement_energy);
}

static int fg_get_one_calibration_charge_energy(void *data, int *charge_energy)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calibration_charge_energy(info, charge_energy);
}

static void fg_fl4p0_enable_check(void *data)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int ret, count = 0;
	int seal_status = 0;
	u8 t_buf[36] = {0};

	ret = fg_set_seal(bq, true);
	ret |= fg_get_seal(bq, &seal_status);
	if (seal_status != SEAL_STATE_UNSEALED && seal_status != SEAL_STATE_FA) {
#line 6179
		mca_log_err("Fg Unseal Failed");
		return;
	}

	while (count++ < UNSEAL_COUNT_MAX) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
#line 6187
			mca_log_err("could not read feature_info, ret=%d", ret);
			msleep(100);
			continue;
		}
		t_buf[0] = 0x78;
		t_buf[1] = 0x00;
		ret = fg_mac_write_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
#line 6195
			mca_log_err("could not write feature_info, ret=%d", ret);
			msleep(100);
		} else {
#line 6198
			mca_log_err("write feature_info success");
			break;
		}
	}

	count = 0;
	while (count++ < UNSEAL_COUNT_MAX) {
		memset(t_buf, 0, sizeof(t_buf));
		ret = fg_mac_read_block(bq, FG_MAC_CMD_FEATURE_INFO, t_buf, 32);
		if (ret < 0) {
#line 6208
			mca_log_err("could not read feature_info, ret=%d", ret);
			msleep(100);
			continue;
		}

		if (t_buf[0] != 0x78 || t_buf[1] != 0x00) {
#line 6214
			mca_log_err("Enable FL4P0 failed");
			msleep(100);
		} else {
#line 6217
			mca_log_err("Enable FL4P0 Sucess");
			break;
		}
	}

	fg_set_seal(bq, false);
	return;
}

static int fg_get_one_device_name(void *data, const char **device_name)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	if (!info || !device_name)
		return -1;


	*device_name = fg_get_device_name(info);
	if (*device_name)
		return 0;

	return -1;
}

static int bq_parse_dt(struct bq_fg_chip *bq)
{
	struct device_node *node = bq->dev->of_node;
	int size = 0;
	int ret = 0;

	of_get_property(node, "bq,soc_decimal_rate", &size);
	if (size) {
		bq->dec_rate_seq = devm_kzalloc(bq->dev, size, GFP_KERNEL);
		if (bq->dec_rate_seq) {
			bq->dec_rate_len = (size / sizeof(*bq->dec_rate_seq));
			if (bq->dec_rate_len % 2) {
#line 7387
				mca_log_info("invalid soc decimal rate seq\n");
				return -EINVAL;
			}
			ret = of_property_read_u32_array(node,
					"bq,soc_decimal_rate",
					bq->dec_rate_seq,
					bq->dec_rate_len);
			if (ret) {
#line 7395
				mca_log_info("Failed to get bq,soc_decimal_rate!\n");
				return -EINVAL;
			}
		} else
#line 7399
			mca_log_info("error allocating memory for dec_rate_seq\n");
	}
	mca_parse_dts_u32(node, "bq,charge-full-design", &bq->batt_dc, DEFUALT_FULL_DESIGN);
	mca_parse_dts_u32(node, "ic_role", &bq->dev_role, 0);
	mca_parse_dts_u32(node, "soh_th", &bq->soh_th, FG_IC_SOH_TH_DEFAUL);
	mca_parse_dts_u32(node, "cycle_th", &bq->cycle_th, FG_IC_CYCLE_TH_DEFAUL);
	mca_parse_dts_u32(node, "cycle_step", &bq->cycle_step, FG_IC_CYCLE_STEP_DEFAUL);
	mca_parse_dts_u32(node, "resistance-id", &bq->resistance_id, FG_IC_RESISTANCE_ID_DEFAUL);
	mca_parse_dts_u8_array(node, "unseal_key", bq->key, BATTERY_UNSEAL_KEY_LEN);
#line 7409
	mca_log_err("0x%x,0x%x,0x%x,0x%x\n",bq->key[0], bq->key[1], bq->key[2], bq->key[3]);
	bq->support_voltage_record_level = of_property_read_bool(node, "support-voltage-record-level");
	if (bq->support_voltage_record_level) {
		mca_parse_dts_u8(node, "version_number", &bq->version_number, 0);
		mca_parse_dts_u8(node, "start_byte_address", &bq->start_byte_address, 0);
		mca_parse_dts_u8(node, "byte_length", &bq->byte_length, 0);
		size = of_property_count_u8_elems(node, "record_voltage");
		if (size < 0 ) {
#line 7422
			mca_log_err("record_voltage is not exist\n");
			bq->update_fw_status = FG_FW_INVALID;
		} else {
#line 7425
			mca_log_info("record_voltage size %d, reg_length %d\n", size, bq->byte_length);
			bq->record_voltage = devm_kzalloc(bq->dev, size, GFP_KERNEL);
			if (bq->record_voltage)
				mca_parse_dts_u8_array(node, "record_voltage", bq->record_voltage, size);
			else
				bq->update_fw_status = FG_FW_INVALID;

		}
		if (!bq->version_number || !bq->start_byte_address || !bq->byte_length) {
			bq->update_fw_status = FG_FW_INVALID;
#line 7435
			mca_log_err("OTA INFO ERROR [0x%x][0x%x][0x%x]\n", bq->version_number, bq->start_byte_address, bq->byte_length);
		}
	}

	return 0;
}

static int fg_set_term_config(struct bq_fg_chip *bq)
{
	int ret = 0;
	u8 data_term[32] = {0};

	if (bq->fg_vendor != BQ27Z561)
		return 0;

	data_term[0] = 220;
	data_term[1] = 5;

	ret = fg_mac_write_block(bq, FG_MAC_CMD_TERM_CURRENTS, data_term, 2);
	if (ret < 0) {
		mca_log_info("could not write term current = %d\n", ret);
		return ret;
	}
	return ret;
}

static inline unsigned long fg_convert_u8_to_u16(unsigned char lsb, unsigned char hsb)
{
	return ((hsb << 8) | lsb);
}

static unsigned long fg_get_calc_rvalue(struct bq_fg_chip *bq)
{
	unsigned long k1 = 45;
	unsigned long k2 = 45;
	unsigned long k3 = 70;
	unsigned long k4 = 45;
	unsigned long k5 = 100;
	unsigned long k6 = 70;
	unsigned long k7 = 45;
	unsigned long CCcc, FFff, IIii, JJjj, MMmm, NNnn, OOoo;
	unsigned char data[32] = { 0 };
	unsigned long rvalue;

	if (fg_mac_read_block(bq, FG_MAC_CMD_CALC_RVALUE, data, 32) < 0)
		return 0;

	CCcc = fg_convert_u8_to_u16(data[4], data[5]);
	FFff = fg_convert_u8_to_u16(data[10], data[11]);
	IIii = fg_convert_u8_to_u16(data[16], data[17]);
	JJjj = fg_convert_u8_to_u16(data[18], data[19]);
	MMmm = fg_convert_u8_to_u16(data[24], data[25]);
	NNnn = fg_convert_u8_to_u16(data[26], data[27]);
	OOoo = fg_convert_u8_to_u16(data[28], data[29]);

	rvalue = (CCcc * k1 + FFff * k2 + IIii * k3 + JJjj * k4 +
		MMmm * k5 + NNnn * k6 + OOoo * k7) / 10;
#line 7569
	mca_log_info("[%u]calr_rvalue=%lu\n", bq->dev_role, rvalue);

	return rvalue;
}

static unsigned long fg_get_one_calc_rvalue(void *data)
{
	struct bq_fg_chip *info = (struct bq_fg_chip *)data;

	return fg_get_calc_rvalue(info);
}

static void fg_store_ui_soh(struct bq_fg_chip *bq, const char *buf, size_t size)
{
	char *pchar = NULL;
	int count = 0, i = 0, ret;
	unsigned char data_uisoh[32] = { 0 };
	unsigned char data[32] = { 0 };
	unsigned int temp = 0;
	char *tmp_buf = NULL;

	tmp_buf = kzalloc(size + 1, GFP_KERNEL);
	if (!tmp_buf)
		return;

	strscpy(tmp_buf, buf, size + 1);
#line 7634
	mca_log_info("%s\n", tmp_buf);
	while ((pchar = strsep(&tmp_buf, " ")) != NULL) {
		if (kstrtouint(pchar, 0, &temp)) {
#line 7637
			mca_log_err("pchar cover to int fail\n");
			goto _write_reg;
		}
		data_uisoh[count] = (unsigned char)temp;
#line 7641
		mca_log_info("data_uisoh[%d]=%d\n", count, data_uisoh[count]);
		++count;
		if (count >= 11)
			break;
	}

_write_reg:
	bq->ui_soh = data_uisoh[0];

	ret = fg_mac_read_block(bq, FG_MAC_CMD_UI_SOH, data, 32);
	if (ret < 0) {
#line 7652
		mca_log_info("failed to get first_usage\n");
		return;
	}
	for (i = 0; i < 11; i++)
		data[i] = data_uisoh[i];

	if (fg_mac_write_block(bq, FG_MAC_CMD_UI_SOH, data, 32))
#line 7660
		mca_log_err("write ui_soh failed\n");
}

static ssize_t fg_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t fg_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info fg_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CHIP_OK, chip_ok),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_VOL, vbatt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CURRENT, ibatt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RSOC, rsoc),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TEMP, temp),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_CYCLE, cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_QMAX, qmax),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RM, rm),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FCC, charger_full),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_SOH, soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FCC_SOH, fcc_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FAST_CHARGE, fast_charge),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RESISTANCE_ID, resistance_id),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TSIM, tsim),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TAMBIENT, tambinet),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TREMQ, tremq),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TFULLCHGQ, tfullchgq),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_CURRENT, avercurrent),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_SEAL, seal),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DF_CHECK, df_check),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_VENDOR_ID, vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_PACK_VENDOR_ID, pack_vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CELL_VENDOR_ID, cell_vendor),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EEPROM_VERSION, eeprom_version),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_DESIGN_CAPACITY, design_capacity),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CALC_RVALUE, calc_rvalue),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AGED_FLAG, aged_flag),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_UI_SOH, ui_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_BATT_SN, batt_sn),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MANUFACTURING_DATE, manufacturing_date),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_FIRST_USAGE_DATE, first_usage_date),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_FAKE_FIRST_USAGE_DATE, fake_first_usage_date),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_SOH_NEW, soh_new),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_DOD_COUNT, dod_count),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL1, count_level1),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL2, count_level2),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LEVEL3, count_level3),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_COUNT_LOWTEMP, count_lt),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CUTOFF_VOL, cutoff_vol),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_DEVICE_NAME, device_name),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_TEMP_OCCUR_TIME, max_temp_occur_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_RUN_TIME, run_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_TEMP_TIME, max_temp_time),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TOTAL_FW_RUN_TIME, total_fw_runtime),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TEMP_MAX, temp_max),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TIME_HT, time_ht),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_TIME_OT, time_ot),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OVER_PEAK_FLAG, peak_flag),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_MONITOR_ISC, isc),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MONITOR_SOA, soa),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_CURRENT_DEVIATION, current_deviation),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_POWER_DEVIATION, power_deviation),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_CURRENT, average_current),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_AVERAGE_TEMPERATURE, average_temperature),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_START_LEARNING, start_learning),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_STOP_LEARNING, stop_learning),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_ACTUAL_POWER, action_power),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_LEARNING_POWER, learning_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_POWER_DEV, learning_power_dev),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_TIME_DEV, learning_time_dev),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_CONSTANT_POWER, constant_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REMAINING_TIME, remaining_time),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_REFERANCE_POWER, referance_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_NVT_REFERANCE_POWER, nvt_referance_power),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REFERANCE_CURRENT, referance_current),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_START_LEARNING_B, start_learning_b),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_STOP_LEARNING_B, stop_learning_b),
	mca_sysfs_attr_rw(fg_sysfs, 0664, FG_IC_PROP_LEARNING_POWER_B, learning_power_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_ACTUAL_POWER_B, action_power_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_LEARNING_POWER_DEV_B, learning_power_dev_b),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REL_SOH, rel_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_REL_SOH_CYCLECOUNT, rel_soh_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EIS_SOH, eis_soh),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_EIS_SOH_CYCLECOUNT, eis_soh_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_QMAX_CYCLECOUNT, qmax_cyclecount),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_BATT_USE_ENVIRRONMENT, batt_use_environment),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_LIFE_VOL, max_life_vol),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_LIFE_VOL, min_life_vol),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MAX_LIFE_TEMP, max_life_temp),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_MIN_LIFE_TEMP, min_life_temp),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_OVER_VOL_DURATION, over_vol_duration),
	mca_sysfs_attr_ro(fg_sysfs, 0440, FG_IC_PROP_FC, fc),
};

#define FG_SYSFS_ATTRS_SIZE   ARRAY_SIZE(fg_sysfs_field_tbl)

static struct attribute *fg_sysfs_attrs[FG_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group fg_sysfs_attr_group = {
	.attrs = fg_sysfs_attrs,
};

static ssize_t fg_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;
	unsigned long rvalue;
	unsigned char data[64] = { 0 };
	ssize_t count = 0;
	struct bq_fg_chip *info = (struct bq_fg_chip *)dev_get_drvdata(dev);
	const char *strval;
	bool fc_val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		fg_sysfs_field_tbl, FG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_CHIP_OK:
		fg_get_chip_ok(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_VOL:
		fg_read_volt(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CURRENT:
		fg_read_current(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_RSOC:
		fg_read_rsoc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TEMP:
		fg_read_temperature(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CYCLE:
		fg_read_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_QMAX:
		fg_read_qmax(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_RM:
		fg_read_rm(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FCC:
		fg_read_fcc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_SOH:
		fg_read_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FCC_SOH:
		fg_read_fcc_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FAST_CHARGE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fast_mode);
		break;
	case FG_IC_PROP_RESISTANCE_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->resistance_id);
		break;
	case FG_IC_PROP_TSIM:
		fg_read_tsim(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TAMBIENT:
		fg_read_tambient(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TREMQ:
		fg_read_tremq(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TFULLCHGQ:
		fg_read_tfullchgq(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERCURRENT:
		fg_read_avercurrent(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_SEAL:
		fg_get_seal(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DF_CHECK:
		fg_read_df_check(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fg_vendor);
		break;
	case FG_IC_PROP_PACK_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->pack_vendor);
		break;
	case FG_IC_PROP_CELL_VENDOR_ID:
		count = scnprintf(buf, PAGE_SIZE, "%s\n", info->cell_name);
		break;
	case FG_IC_PROP_EEPROM_VERSION:
		count = scnprintf(buf, PAGE_SIZE, "%c\n", info->eeprom_version);
		break;
	case FG_IC_PROP_MAX_TEMP_OCCUR_TIME:
		fg_read_max_temp_occur_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DESIGN_CAPACITY:
		fg_read_dc(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CALC_RVALUE:
		rvalue = fg_get_calc_rvalue(info);
		count = scnprintf(buf, PAGE_SIZE, "%lu\n", rvalue);
		break;
	case FG_IC_PROP_AGED_FLAG:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->aged_flag);
		break;
	case FG_IC_PROP_UI_SOH:
		if (fg_mac_read_block(info, FG_MAC_CMD_UI_SOH, data, 11) < 0)
			return -1;
		count = scnprintf(buf, PAGE_SIZE,
			"%u %u %u %u %u %u %u %u %u %u %u\n",
			data[0], data[1], data[2], data[3], data[4], data[5],
			data[6], data[7], data[8], data[9], data[10]);
		break;
	case FG_IC_PROP_BATT_SN:
		if (fg_mac_read_block(info, FG_MAC_CMD_BATT_SN, data, 32) < 0)
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_MANUFACTURING_DATE:
		if (fg_read_manufacturing_date(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_FIRST_USAGE_DATE:
		fg_read_first_usage_date(info, data);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_FAKE_FIRST_USAGE_DATE:
		if (fg_read_first_usage_date(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE, "%s\n", data);
		break;
	case FG_IC_PROP_SOH_NEW:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->ui_soh);
		break;
	case FG_IC_PROP_DOD_COUNT:
		val = fg_get_dod_count(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL1:
		fg_get_count_level1(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL2:
		fg_get_count_level2(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LEVEL3:
		fg_get_count_level3(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_COUNT_LOWTEMP:
		fg_get_count_lowtemp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CUTOFF_VOL:
		fg_get_cutoff_voltage(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_DEVICE_NAME:
		strval = fg_get_device_name(info);
		count = scnprintf(buf, PAGE_SIZE, "%s", strval);
		break;
	case FG_IC_PROP_RUN_TIME:
		fg_read_run_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MAX_TEMP_TIME:
		fg_read_max_temp_time(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TOTAL_FW_RUN_TIME:
		fg_read_total_fw_runtime(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TEMP_MAX:
		fg_get_one_temp_max(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TIME_HT:
		fg_read_time_ht(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_TIME_OT:
		fg_read_time_ot(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_OVER_PEAK_FLAG:
		val = fg_get_over_peak_flag(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MONITOR_ISC:
		fg_get_isc_alert_level(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MONITOR_SOA:
		fg_get_soa_alert_level(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CURRENT_DEVIATION:
		val = fg_get_current_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_POWER_DEVIATION:
		val = fg_get_power_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERAGE_CURRENT:
		val = fg_get_average_current(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_AVERAGE_TEMPERATURE:
		val = fg_get_average_temp(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_START_LEARNING:
		val = fg_get_start_learning(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_STOP_LEARNING:
		val = fg_get_stop_learning(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_ACTUAL_POWER:
		val = fg_get_actual_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER:
		val = fg_get_learning_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_DEV:
		val = fg_get_learning_power_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_TIME_DEV:
		val = fg_get_learning_time_dev(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_CONSTANT_POWER:
		val = fg_get_constant_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REMAINING_TIME:
		val = fg_get_remaining_time(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REFERANCE_POWER:
		val = fg_get_referance_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_NVT_REFERANCE_POWER:
		val = fg_get_nvt_referance_power(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REFERANCE_CURRENT:
		val = fg_get_referance_current(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_START_LEARNING_B:
		val = fg_get_start_learning_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_STOP_LEARNING_B:
		val = fg_get_stop_learning_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_B:
		val = fg_get_learning_power_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_ACTUAL_POWER_B:
		val = fg_get_actual_power_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_LEARNING_POWER_DEV_B:
		val = fg_get_learning_power_dev_b(info);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REL_SOH:
		fg_get_rel_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_REL_SOH_CYCLECOUNT:
		fg_get_rel_soh_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_EIS_SOH:
		fg_get_eis_soh(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_EIS_SOH_CYCLECOUNT:
		fg_get_eis_soh_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_QMAX_CYCLECOUNT:
		fg_get_qmax_cyclecount(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_BATT_USE_ENVIRRONMENT:
		if (fg_get_batt_use_environment(info, data))
			return -1;
		count = scnprintf(buf, PAGE_SIZE,
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u %u %u "
			"%u %u %u %u %u %u %u %u\n",
			data[0], data[1], data[2], data[3], data[4], data[5],
			data[6], data[7], data[8], data[9], data[10], data[11],
			data[12], data[13], data[14], data[15], data[16], data[17],
			data[18], data[19], data[20], data[21], data[22], data[23],
			data[24], data[25], data[26], data[27], data[28], data[29],
			data[30], data[31], data[32], data[33], data[34], data[35],
			data[36], data[37]);
		break;
	case FG_IC_PROP_MAX_LIFE_VOL:
		fg_get_max_life_vol(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MIN_LIFE_VOL:
		fg_get_min_life_vol(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MAX_LIFE_TEMP:
		fg_get_max_life_temp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_MIN_LIFE_TEMP:
		fg_get_min_life_temp(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_OVER_VOL_DURATION:
		fg_get_over_vol_duration(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_IC_PROP_FC:
		fg_read_one_fc(info, &fc_val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", fc_val);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t fg_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct bq_fg_chip *info = (struct bq_fg_chip *)dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		fg_sysfs_field_tbl, FG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	(void)kstrtoint(buf, 10, &val);

	switch (attr_info->sysfs_attr_name) {
	case FG_IC_PROP_SEAL:
		fg_set_seal(info, val);
		break;
	case FG_IC_PROP_RSOC:
		break;
	case FG_IC_PROP_CYCLE:
		fg_set_cyclecount(info, val);
		break;
	case FG_IC_PROP_COUNT_LEVEL1:
		info->fake_count_level1 = val;
		break;
	case FG_IC_PROP_COUNT_LEVEL2:
		info->fake_count_level2 = val;
		break;
	case FG_IC_PROP_COUNT_LEVEL3:
		info->fake_count_level3 = val;
		break;
	case FG_IC_PROP_COUNT_LOWTEMP:
		info->count_lowtemp = val;
		break;
	case FG_IC_PROP_DEVICE_NAME:
		strscpy(info->fake_device_name, buf, count + 1);
		break;
	case FG_IC_PROP_MONITOR_ISC:
		fg_set_isc_alert_level(info, val);
		break;
	case FG_IC_PROP_UI_SOH:
		fg_store_ui_soh(info, buf, count);
		break;
	case FG_IC_PROP_FIRST_USAGE_DATE:
		fg_write_first_usage_date(info, buf, count);
		break;
	case FG_IC_PROP_FAKE_FIRST_USAGE_DATE:
		fg_write_fake_first_usage_date(info, buf, count);
		break;
	case FG_IC_PROP_START_LEARNING:
		fg_set_start_learning(info);
		break;
	case FG_IC_PROP_STOP_LEARNING:
		fg_set_stop_learning(info);
		break;
	case FG_IC_PROP_LEARNING_POWER:
		fg_set_learning_power(info, val);
		break;
	case FG_IC_PROP_CONSTANT_POWER:
		fg_set_constant_power(info, val);
		break;
	case FG_IC_PROP_REFERANCE_POWER:
		fg_set_referance_power(info, val);
		break;
	case FG_IC_PROP_START_LEARNING_B:
		fg_set_start_learning_b(info);
		break;
	case FG_IC_PROP_STOP_LEARNING_B:
		fg_set_stop_learning_b(info);
		break;
	case FG_IC_PROP_LEARNING_POWER_B:
		fg_set_learning_power_b(info, val);
		break;
	default:
		break;
	}

	return count;
}

static void fg_sysfs_create_group(struct bq_fg_chip *info)
{
	mca_sysfs_init_attrs(fg_sysfs_attrs, fg_sysfs_field_tbl,
		FG_SYSFS_ATTRS_SIZE);

	if (info->dev_role == FG_MASTER)
		info->sysfs_dev = mca_sysfs_create_group("xm_power", "fg_master", &fg_sysfs_attr_group);
	else if (info->dev_role == FG_SLAVE)
		info->sysfs_dev = mca_sysfs_create_group("xm_power", "fg_slave", &fg_sysfs_attr_group);
	else
#line 8412
		mca_log_info("invalid device");

	if (info->sysfs_dev) {
		dev_set_drvdata(info->sysfs_dev, info);
#line 8416
		mca_log_err("create fg sysfs success\n");
	}
}

static void fg_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_group("xm_power", dev, &fg_sysfs_attr_group);
}

static const struct of_device_id bq_fg_match_table[] = {
	{
		.compatible = "ti,bq27z561_master",
		.data = &bq_mode_data[BQ27Z561_MASTER],
	},
	{
		.compatible = "ti,bq27z561_slave",
		.data = &bq_mode_data[BQ27Z561_SLAVE],
	},
	{
		.compatible = "ti,bq28z610",
		.data = &bq_mode_data[BQ28Z610],
	},
	{},
};
MODULE_DEVICE_TABLE(of, bq_fg_match_table);

static struct i2c_device_id bq_fg_id[] = {
	{ "bq27z561_master", BQ27Z561_MASTER },
	{ "bq27z561_slave", BQ27Z561_SLAVE },
	{ "bq28z610", BQ28Z610 },
	{},
};

static struct fuelguage_ic_ops g_bq_fg_ops = {
	.fg_ic_probe_ok = fg_read_one_probe_ok,
	.fg_ic_get_batt_info = fg_read_batt_info,
	.fg_ic_get_rsoc = fg_read_one_rsoc,
	.fg_ic_get_curr = fg_read_one_current,
	.fg_ic_get_volt = fg_read_one_volt,
	.fg_ic_set_temp = fg_set_temperature,
	.fg_ic_get_temp = fg_read_one_temperature,
	.fg_ic_get_charge_status = fg_read_one_status,
	.fg_ic_get_rm = fg_read_one_rm,
	.fg_ic_get_fastcharge = fg_get_one_fastcharge_mode,
	.fg_ic_set_fastcharge = fg_set_one_fastcharge_mode,
	.fg_ic_get_chg_vol = fg_read_one_charging_voltage,
	.fg_ic_get_chip_ok = fg_get_one_chip_ok,
	.fg_ic_get_cyclecount = fg_read_one_cyclecount,
	.fg_ic_get_tte = fg_read_one_tte,
	.fg_ic_get_ttf = fg_read_one_ttf,
	.fg_ic_get_fcc = fg_read_one_fcc,
	.fg_ic_get_full_design = fg_read_one_dc,
	.fg_ic_get_decimal_rate = fg_get_one_soc_decimal_rate,
	.fg_ic_get_decimal = fg_get_one_soc_decimal,
	.fg_ic_get_soh = fg_read_one_soh,
	.fg_ic_get_temp_max = fg_get_one_temp_max,
	.fg_ic_get_time_ot = fg_get_one_time_ot,
	.fg_ic_get_batt_cell_info = fg_get_one_batt_cell_info,
	.fg_ic_set_verify_digest = fg_set_verify_digest,
	.fg_ic_get_verify_digest = fg_get_verify_digest,
	.fg_ic_set_authentic = fg_set_authentic,
	.fg_ic_get_authentic = fg_get_authentic,
	.fg_ic_get_error_state = fg_get_error_state,
	.fg_ic_get_cutoff_voltage = fg_get_one_cutoff_voltage,
	.fg_ic_set_cutoff_voltage = fg_set_one_cutoff_voltage,
	.fg_ic_get_dod_count = fg_get_one_dod_count,
	.fg_ic_get_count_level1 = fg_get_one_count_level1,
	.fg_ic_get_count_level2 = fg_get_one_count_level2,
	.fg_ic_get_count_level3 = fg_get_one_count_level3,
	.fg_ic_get_count_lowtemp = fg_get_one_count_lowtemp,
	.fg_ic_get_adapt_power = fg_get_one_adapt_power,
	.fg_ic_get_aged_flag = fg_get_aged_flag,
	.fg_ic_get_isc_alert_level = fg_get_one_isc_alert_level,
	.fg_ic_get_soa_alert_level = fg_get_one_soa_alert_level,
	.fg_ic_update_fw = fg_update_data_work,
	.fg_ic_get_raw_soc = fg_get_one_raw_soc,
	.fg_ic_get_device_name = fg_get_one_device_name,
	.fg_ic_get_temp_min = fg_get_one_temp_min,
	.fg_ic_set_force_report_full = fg_set_force_report_full,
	.fg_ic_set_clear_count_data = fg_set_one_clear_count_data,
	.fg_ic_get_fc = fg_read_one_fc,
	.fg_ic_set_co = fg_set_one_co,
	.fg_ic_get_calibration_ffc_iterm = fg_get_one_calibration_ffc_iterm,
	.fg_ic_get_real_supplement_energy = fg_get_one_real_supplement_energy,
	.fg_ic_get_calibration_charge_energy = fg_get_one_calibration_charge_energy,
	.fg_ic_fl4p0_enable_check = fg_fl4p0_enable_check,
	.fg_ic_get_ui_soh = fg_get_ui_soh,
	.fg_ic_get_calc_rvalue = fg_get_one_calc_rvalue,
	/* G13q: the stock entries that were missing (indexes 8, 21, 40, 41, 61, 63, 65, 67, 69, 71, 73, 74, 75).
	 * NOT wired (not ported, see STATUS.md): 64 nfg1000_ota_update_check, 66 get_batt_abnormal_info, 68 set_first_usage_date
	 * (data flash write), 70 fg_read_one_co, 72 set_fast_mode_times (MAC write), 76 get_cc_coulomb_integral. */
	.fg_ic_get_original_temp = fg_read_one_original_temperature,
	.fg_ic_get_fcc_default = fg_get_one_fcc_default,
	.fg_ic_get_vsiloss = fg_get_one_vsiloss,
	.fg_ic_get_iaver = fg_get_one_iaver,
	.fg_ic_get_pack_vendor = fg_get_pack_vendor,
	.fg_ic_get_ota_update_flag = fg_get_ota_update_flag,
	.fg_ic_get_average_current = fg_get_one_average_current,
	.fg_ic_get_manufacturing_date = fg_get_one_manufacturing_date,
	.fg_ic_get_first_usage_date = fg_get_one_first_usage_date,
	.fg_ic_get_fast_mode_times = fg_get_one_fast_mode_times,
	.fg_ic_get_fg_vendor = fg_get_one_fg_vendor,
	.fg_ic_get_csd_flag = fg_get_one_csd_flag,
	.fg_ic_get_batt_qmax = fg_get_one_batt_qmax,
};

static int bq27z561_dump_log_head(void *data, char *buf, int size)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;

	if (!data)
		return 0;

	if (bq->dev_role == 0)
		return snprintf(buf, size, "fg_type Cell      RSoc Soh Volt Curr    RM    FCC   Qmax  Temp SOA ");
	else
		return snprintf(buf, size, "fg_type1 Cell1     RSoc1 Soh1 Volt1 Curr1   RM1   FCC1  Qmax1 Temp1 SOA1 ");
}

static int bq27z561_dump_log_context(void *data, char *buf, int size)
{
	struct bq_fg_chip *bq = (struct bq_fg_chip *)data;
	int rsoc, soh, volt, curr, rm, fcc, qmax, temp, soa;

	if (!data)
		return 0;

	fg_read_rsoc(bq, &rsoc);
	fg_read_soh(bq, &soh);
	fg_read_volt(bq, &volt);
	fg_read_current(bq, &curr);
	fg_read_rm(bq, &rm);
	fg_read_fcc(bq, &fcc);
	fg_read_qmax(bq, &qmax);
	fg_read_temperature(bq, &temp);
	fg_get_soa_alert_level(bq, &soa);

	if (bq->dev_role == 0)
		return snprintf(buf, size, "%-8d%-10s%-5d%-4d%-5d%-8d%-6d%-6d%-6d%-5d%-4d",
			bq->fg_vendor, bq->cell_name, rsoc, soh, volt, curr / 1000,
			rm / 1000, fcc / 1000, qmax / 1000, temp, soa);
	else
		return snprintf(buf, size, "%-9d%-10s%-6d%-5d%-6d%-8d%-6d%-6d%-6d%-6d%-5d",
			bq->fg_vendor, bq->cell_name, rsoc, soh, volt, curr / 1000,
			rm / 1000, fcc / 1000, qmax / 1000, temp, soa);
}

static struct mca_log_charge_log_ops g_bq27z516_log_ops = {
	.dump_log_head = bq27z561_dump_log_head,
	.dump_log_context = bq27z561_dump_log_context,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
static int bq_fg_probe(struct i2c_client *client)
#else
static int bq_fg_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	int ret;
	struct bq_fg_chip *bq;
	char data[32];
	static int probe_cnt;

#line 9830
	mca_log_info("probe_cnt = %d\n", ++probe_cnt);

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	bq->dev = &client->dev;
	bq->client = client;
	bq->ui_soh = 100;
	bq->fake_temp = FG_FAKE_TEMP_NONE;
	bq->batt_fcc = FG_RM_FCC_DEFAULT_TH * 1000;
	i2c_set_clientdata(client, bq);
	mutex_init(&bq->i2c_rw_lock);
	mutex_init(&bq->data_lock);
	atomic_set(&bq->pm_suspend, 0);
	atomic_set(&bq->digest_in_process, 0);
	bq->update_fw_status = FG_FW_INIT;
	strcpy(bq->fake_device_name, "");

	bq_parse_dt(bq);
	memcpy(bq->regs, bq27z561_regs, NUM_REGS);

	ret = fg_mac_read_block(bq, FG_MAC_CMD_MANU_NAME, data, sizeof(data));
	if (ret < 0) {
#line 9861
		mca_log_err("fg device found: %d\n", ret);
		ret = -EPROBE_DEFER;
		if (probe_cnt >= PROBE_CNT_MAX)
			goto out;
		else
			return ret;
	} else if (ret)
#line 9868
		mca_log_err("fg cksum_calc failed\n");

	device_init_wakeup(bq->dev, 1);
	fg_set_term_config(bq);
	fg_get_manufacture_data(bq);
	fg_set_fastcharge_mode(bq, false);
	fg_read_cell_vendor(bq);
	fg_read_pack_vendor(bq);
	fg_read_vendor(bq);
	fg_read_fw_version(bq);
	fg_read_eeprom_version(bq);

	ret = platform_fg_ic_ops_register(bq->dev_role, bq, &g_bq_fg_ops);
	if (ret) {
#line 9889
		mca_log_err("reg ops fail, role = %u\n", bq->dev_role);
		goto out;
	}

#line 9911
	mca_log_err("FuelGaugw probe successfully, role = %u\n", bq->dev_role);
out:
	fg_sysfs_create_group(bq);
	i2c_set_clientdata(client, bq);
	if (bq->dev_role)
		mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_MASTER_IC,
			&g_bq27z516_log_ops, bq);
	else
		mca_log_charge_log_register(MCA_CHARGE_LOG_ID_FG_SLAVE_IC,
			&g_bq27z516_log_ops, bq);
	if (ret == -EPROBE_DEFER) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, MCA_EVENT_BQ_FG_ERROR, 0);
	}
#line 9925
	mca_log_err("%s !!\n", ret == -EPROBE_DEFER ? "Over probe cnt max" : "OK");
	return 0;
}

static int bq_fg_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	if (!bq)
		return 0;

#line 9937
	mca_log_info("suspend\n");
	atomic_set(&bq->pm_suspend, 1); /* 1: set atomic pm suspend flag */

	return 0;
}

static int bq_fg_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	if (!bq)
		return 0;

#line 9951
	mca_log_info("resume\n");
	atomic_set(&bq->pm_suspend, 0); /* 0: clear atomic pm suspend flag */

	return 0;
}

static void bq_fg_remove(struct i2c_client *client)
{
	struct bq_fg_chip *bq = i2c_get_clientdata(client);

	mutex_destroy(&bq->data_lock);
	mutex_destroy(&bq->i2c_rw_lock);
	fg_sysfs_remove_group(bq->sysfs_dev);
}

static void bq_fg_shutdown(struct i2c_client *client)
{
#line 9968
	mca_log_info("bq fuel gauge driver shutdown!\n");
}

static const struct dev_pm_ops bq_fg_pm_ops = {
	.resume		= bq_fg_resume,
	.suspend	= bq_fg_suspend,
};

MODULE_DEVICE_TABLE(i2c, bq_fg_id);

static struct i2c_driver bq_fg_driver = {
	.driver	= {
		.name   = "bq27z561",
		.owner  = THIS_MODULE,
		.of_match_table = bq_fg_match_table,
		.pm	 = &bq_fg_pm_ops,
	},
	.id_table	   = bq_fg_id,

	.probe		  = bq_fg_probe,
	.remove		= bq_fg_remove,
	.shutdown	= bq_fg_shutdown,

};

module_i2c_driver(bq_fg_driver);

MODULE_DESCRIPTION("TI BQ27Z561 Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Texas Instruments");

