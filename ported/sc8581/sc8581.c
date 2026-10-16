// SPDX-License-Identifier: GPL-2.0
/*
 * sc8581.c
 *
 * charge-pump ic driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <mca/platform/platform_cp_class.h>
#include "inc/sc8581_reg.h"
#include "inc/sc8581.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <linux/version.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <linux/pm_wakeup.h>
#include <mca/common/mca_hwid.h>
/* hwid.h */

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_sc8581"
#endif

#define MAX_LENGTH_BYTE 600
#define MAX_REG_COUNT 0x42
#define MCA_SET_OVPGATE_COUNT 10
static int cp_get_adc_data(struct sc8581_device *bq, int channel,  u32 *result);
/********i2c basic read/write interface***********/
static int cp_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
#line 47
		mca_log_info("i2c read word fail: can't read from reg 0x%02X, errcode=%d\n", reg, ret);
		return ret;
	}

	*val = (u16)ret;
	return 0;
}
#ifdef DEBUG_CODE
static int cp_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret;

	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		mca_log_info("i2c write word fail: can't write to reg 0x%02X\n", reg);
		return ret;
	}
	return 0;
}
#endif

static int cp_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
#line 74
		mca_log_info("i2c read byte fail: can't read from reg 0x%02X, errcode=%d\n", reg, ret);
		return ret;
	}

	*val = (u8)ret;
	return 0;

}

static int cp_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
#line 89
		mca_log_info("i2c write byte fail: can't write to reg 0x%02X, errcode=%d\n", reg, ret);
		return ret;
	}

	return 0;
}
#ifdef DEBUG_CODE
static int cp_read_block(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret;
	int i;

	for (i = 0; i < len; i++) {
		ret = i2c_smbus_read_byte_data(client, reg + i);
		if (ret < 0) {
			mca_log_info("i2c read reg 0x%02X faild\n", reg + i);
			return ret;
		}
		buf[i] = ret;
	}
	return 0;
}

static int cp_write_block(struct i2c_client *client, u8 reg, u8 *buf, int len)
{
	int ret;
	int i;

	for (i = 0; i < len; i++) {
		ret = i2c_smbus_write_byte_data(client, reg + i, buf[i]);
		if (ret < 0) {
			mca_log_info("i2c write reg 0x%02X faild\n", reg + i);
			return ret;
		}
	}

	return 0;
}
#endif

static int cp_read_i2c_block_data(struct i2c_client *client, u8 reg, unsigned short len, u8 *buf)
{
	int ret;

	if (!client || !buf) {
#line 134
		mca_log_err("null pointer read\n");
		return -EINVAL;
	}

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);
	if (ret < 0) {
#line 140
		mca_log_err("I2C SMBus read failed, ret=%d\n", ret);
		return ret;
	}

	return ret;
}

static int cp_update_bits(struct i2c_client *client, u8 reg, u8 mask, u8 val)
{
	u8 tmp;

	cp_read_byte(client, reg, &tmp);
	tmp &= ~mask;
	tmp |= val & mask;
	cp_write_byte(client, reg, tmp);
	return 0;
}
/***********************end **********************/

#ifdef CONFIG_DEBUG_FS
#define CP_MAX_REGS_NUMBER	0x2B

enum cp_attr_list {
	CP_DEBUG_PROP_ADDRESS,
	CP_DEBUG_PROP_COUNT,
	CP_DEBUG_PROP_DATA,
};

static struct reg_context {
			int address;
			int count;
			int data;
} reg_info;

static ssize_t cp_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc8581_device *dev_data = (struct sc8581_device *)attr_data->private;
	u8 val = 0;
	ssize_t count = 0;
	int ret = 0;
	char read_buf[MAX_LENGTH_BYTE] = {'\0'};
	int i;

	if (!dev_data || !attr_info) {
#line 185
		mca_log_err("null pointer show\n");
		return count;
	}

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		count = scnprintf(buf, PAGE_SIZE, "%02x\n", reg_info.address);
		break;
	case CP_DEBUG_PROP_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%x\n", reg_info.count);
		break;
	case CP_DEBUG_PROP_DATA:
		for (i = 0; i < reg_info.count; i++) {
			ret = cp_read_byte(dev_data->client, (reg_info.address + i), &val);
			count += scnprintf(read_buf, MAX_LENGTH_BYTE, "%02x: %02x\n",
				reg_info.address + i, val);
			strcat(buf, read_buf);
		}
		break;
	default:
		break;
	}
	return count;
}

static ssize_t cp_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct sc8581_device *dev_data = (struct sc8581_device *)attr_data->private;
	int val = 0;
	int ret = 0;

	if (!dev_data || !attr_info) {
#line 219
		mca_log_err("null pointer store\n");
		return count;
	}

	if (kstrtoint(buf, 16, &val))
		return -EINVAL;

	switch (attr_info->debugfs_attr_name) {
	case CP_DEBUG_PROP_ADDRESS:
		reg_info.address = val;
		break;
	case CP_DEBUG_PROP_COUNT:
		if (val > MAX_REG_COUNT)
			reg_info.count = MAX_REG_COUNT;
		else if (reg_info.count < 1)
			reg_info.count = 1;
		else
			reg_info.count = val;
		break;
	case CP_DEBUG_PROP_DATA:
		ret = cp_write_byte(dev_data->client, reg_info.address, val);
		break;
	default:
		break;
	}

	return count;
}

struct mca_debugfs_attr_info cp_debugfs_field_tbl[] = {
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_ADDRESS, address),
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_COUNT, count),
	mca_debugfs_attr(cp_debugfs, 0600, CP_DEBUG_PROP_DATA, data),
};

#define CP_DEBUGFS_ATTRS_SIZE			ARRAY_SIZE(cp_debugfs_field_tbl)

#endif
/*******end debugfs******************************/

static int sc8581_init_protection(struct sc8581_device *cp, int forward_work_mode);
/* SC8581_ADC */
#ifdef DEBUG_CODE
static int sc8581_check_adc_enabled(struct sc8581_device *bq, bool *enabled)
{
	int ret = 0;
	unsigned int val;

	ret = cp_read_byte(bq->client, SC8581_REG_15, &val);
	if (!ret)
		*enabled = !!(val & SC8581_ADC_EN_MASK);
	mca_log_info("%s enable adc %x, is %d\n", bq->log_tag, val, *enabled);
	return ret;
}
#endif

static int sc8581_enable_adc(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;
	//bool enabled = false;

	val = enable ? SC8581_ADC_ENABLE : SC8581_ADC_DISABLE;
	val <<= SC8581_ADC_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_15,
		SC8581_ADC_EN_MASK, val);
	//sc8581_check_adc_enabled(bq, &enabled);

	return ret;
}

/* SC8581_CHG_EN */
static void sc8581_enable_charge(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_CHG_ENABLE : SC8581_CHG_DISABLE;
	val <<= SC8581_CHG_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
				SC8581_CHG_EN_MASK, val);
#line 300
	mca_log_info("%s enable cp: %d\n", bq->log_tag, enable);

}

static int sc8581_check_charge_enabled(struct sc8581_device *bq, bool *enabled)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_0B, &val);
	if (!ret)
		*enabled = !!(val & SC8581_CHG_EN_MASK);

	return ret;
}

static void sc8581_enable_qb(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_QB_ENABLE : SC8581_QB_DISABLE;
	val <<= SC8581_QB_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
				SC8581_QB_EN_MASK, val);
#line 326
	mca_log_info("%s enable qb: %d\n", bq->log_tag, enable);

}

static int sc8581_disable_rcp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = !!enable;
	val <<= SC8581_IBUS_RCP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_40,
				SC8581_IBUS_RCP_DIS_MASK, val);
#line 340
	mca_log_info("%s disable rcp: %d\n", bq->log_tag, val);

	return ret;
}

/* SC8581_BAT_OVP */
static int sc8581_enable_batovp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_BAT_OVP_ENABLE : SC8581_BAT_OVP_DISABLE;
	val <<= SC8581_BAT_OVP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_01,
		SC8581_BAT_OVP_DIS_MASK, val);

	return ret;
}

static int sc8581_set_batovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (threshold < SC8581_BAT_OVP_BASE)
		threshold = SC8581_BAT_OVP_BASE;

	val = (threshold - SC8581_BAT_OVP_BASE) / SC8581_BAT_OVP_LSB;
	val <<= SC8581_BAT_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_01,
				SC8581_BAT_OVP_MASK, val);

	return ret;
}
/* SC8581_BAT_OCP */
static int sc8581_enable_batocp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	if (bq->chip_vendor == SC8585)
		return ret;

	val = enable ? SC8581_BAT_OCP_ENABLE : SC8581_BAT_OCP_DISABLE;
	val <<= SC8581_BAT_OCP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_02,
				SC8581_BAT_OCP_DIS_MASK, val);

	return ret;
}

static int sc8581_set_batocp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (bq->chip_vendor == SC8585)
		return ret;

	if (threshold < SC8581_BAT_OCP_BASE)
		threshold = SC8581_BAT_OCP_BASE;

	val = (threshold - SC8581_BAT_OCP_BASE) / SC8581_BAT_OCP_LSB;
	val <<= SC8581_BAT_OCP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_02,
				SC8581_BAT_OCP_MASK, val);

	return ret;
}
/* SC8581_USB_OVP */
static int sc8581_set_usbovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (bq->chip_vendor != SC8585) {
		if (threshold == 6500)
			val = SC8581_USB_OVP_6PV5;
		else
			val = (threshold - SC8581_USB_OVP_BASE) / SC8581_USB_OVP_LSB;
	} else {
		if (threshold == 7500)
			val = SC8585_USB_OVP_7PV5;
		else
			val = (threshold - SC8581_USB_OVP_BASE) / SC8581_USB_OVP_LSB;
	}

	val <<= SC8581_USB_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_03,
				SC8581_USB_OVP_MASK, val);

	return ret;
}
/* SC8581_OVPGATE_ON_DG_SET */
static int sc8581_set_ovpgate_on_dg_set(struct sc8581_device *bq, int data)
{
	int ret = 0;
	u8 val;

	val = data ? SC8581_OVPGATE_ON_DG_128MS : SC8581_OVPGATE_ON_DG_20MS;
	val <<= SC8581_OVPGATE_ON_DG_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_03,
				SC8581_OVPGATE_ON_DG_MASK, val);

	return ret;
}
/* SC8581_WPC_OVP */
static int sc8581_set_wpcovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (bq->chip_vendor != SC8585) {
		if (threshold == 6500)
			val = SC8581_WPC_OVP_6PV5;
		else
			val = (threshold - SC8581_WPC_OVP_BASE) / SC8581_WPC_OVP_LSB;
	} else {
		if (threshold == 7500)
			val = SC8585_WPC_OVP_7PV5;
		else
			val = (threshold - SC8581_WPC_OVP_BASE) / SC8581_WPC_OVP_LSB;
	}


	val <<= SC8581_WPC_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_04,
				SC8581_WPC_OVP_MASK, val);

	return ret;
}
/* SC8581_BUS_OVP */
#define RANGE_MIN_MAX 2
static int sc8581_set_busovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;
	int ovp_th_range[CP_MODE_DIV_MAX][RANGE_MIN_MAX];
	int base[CP_MODE_DIV_MAX];
	int lsb[CP_MODE_DIV_MAX];
	int ovp_mask;

	if (bq->chip_vendor == SC8585) {
		ovp_th_range[CP_MODE_DIV4][0] = 15000;
		ovp_th_range[CP_MODE_DIV4][1] = 22000;
		ovp_th_range[CP_MODE_DIV2][0] = 7500;
		ovp_th_range[CP_MODE_DIV2][1] = 13300;
		ovp_th_range[CP_MODE_DIV1][0] = 3750;
		ovp_th_range[CP_MODE_DIV1][1] = 5500;
	} else {
		ovp_th_range[CP_MODE_DIV4][0] = 14000;
		ovp_th_range[CP_MODE_DIV4][1] = 22000;
		ovp_th_range[CP_MODE_DIV2][0] = 7000;
		ovp_th_range[CP_MODE_DIV2][1] = 13300;
		ovp_th_range[CP_MODE_DIV1][0] = 3500;
		ovp_th_range[CP_MODE_DIV1][1] = 5500;
	}
#line 497
	mca_log_info("%s threshold= %d, min = %d, max =%d\n",
		bq->log_tag, threshold, ovp_th_range[bq->work_mode][0],
		ovp_th_range[bq->work_mode][1]);
	if (threshold < ovp_th_range[bq->work_mode][0])
		threshold = ovp_th_range[bq->work_mode][0];
	else if (threshold > ovp_th_range[bq->work_mode][1])
		threshold = ovp_th_range[bq->work_mode][1];

	switch (bq->chip_vendor) {
	case SC8585:
		base[CP_MODE_DIV4] = SC8585_BUS_OVP_41MODE_BASE;
		lsb[CP_MODE_DIV4] = SC8585_BUS_OVP_41MODE_LSB;
		base[CP_MODE_DIV2] = SC8585_BUS_OVP_21MODE_BASE;
		lsb[CP_MODE_DIV2] = SC8585_BUS_OVP_21MODE_LSB;
		base[CP_MODE_DIV1] = SC8585_BUS_OVP_11MODE_BASE;
		lsb[CP_MODE_DIV1] = SC8585_BUS_OVP_11MODE_LSB;
		ovp_mask = SC8585_BUS_OVP_MASK;
		break;
	case SC8581:
	case SC8561:
		base[CP_MODE_DIV4] = SC8581_BUS_OVP_41MODE_BASE;
		lsb[CP_MODE_DIV4] = SC8581_BUS_OVP_41MODE_LSB;
		base[CP_MODE_DIV2] = SC8581_BUS_OVP_21MODE_BASE;
		lsb[CP_MODE_DIV2] = SC8581_BUS_OVP_21MODE_LSB;
		base[CP_MODE_DIV1] = SC8581_BUS_OVP_11MODE_BASE;
		lsb[CP_MODE_DIV1] = SC8581_BUS_OVP_11MODE_LSB;
		ovp_mask = SC8581_BUS_OVP_MASK;
		break;
	default:
#line 526
		mca_log_info("%s chip_vendor =%x no valid\n", bq->log_tag, bq->chip_vendor);
		return -1;
	}

	val = (threshold - base[bq->work_mode]) / lsb[bq->work_mode];

#line 532
	mca_log_info("%s bus_ovpth= %d, val = %d\n", bq->log_tag, threshold, val);

	val <<= SC8581_BUS_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_05,
		ovp_mask, val);

	return ret;
}
/* SC8581_OUT_OVP */
static int sc8581_set_outovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (threshold < 4800)
		threshold = 4800;

	val = (threshold - SC8581_OUT_OVP_BASE) / SC8581_OUT_OVP_LSB;
	val <<= SC8581_OUT_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_05,
		SC8581_OUT_OVP_MASK, val);

	return ret;
}
/* SC8581_BUS_OCP */
static int sc8581_enable_busocp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_BUS_OCP_ENABLE : SC8581_BUS_OCP_DISABLE;
	val <<= SC8581_BUS_OCP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_06,
		SC8581_BUS_OCP_DIS_MASK, val);

	return ret;
}

static int sc8581_set_busocp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (bq->chip_vendor == SC8585) {
		if (threshold < SC8585_BUS_OCP_BASE)
			threshold = SC8585_BUS_OCP_BASE;
		val = (threshold - SC8585_BUS_OCP_BASE) / SC8585_BUS_OCP_LSB;
	} else {
		if (threshold < SC8581_BUS_OCP_BASE)
			threshold = SC8581_BUS_OCP_BASE;
		val = (threshold - SC8581_BUS_OCP_BASE) / SC8581_BUS_OCP_LSB;
	}

	val <<= SC8581_BUS_OCP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_06,
		SC8581_BUS_OCP_MASK, val);

	return ret;
}
/* SC8581_BUS_UCP */
static void sc8581_enable_busucp(struct sc8581_device *bq, bool enable)
{
	u8 val;

	val = enable ? SC8581_BUS_UCP_ENABLE : SC8581_BUS_UCP_DISABLE;
	val <<= SC8581_BUS_UCP_DIS_SHIFT;
	cp_update_bits(bq->client, SC8581_REG_07,
		SC8581_BUS_UCP_DIS_MASK, val);
	val = enable ? 0 : 1;
	val <<= 1;
	cp_update_bits(bq->client, SC8581_REG_07, 0x02, val);
	val = enable ? 0 : 1;
	val <<= 3;
	cp_update_bits(bq->client, SC8581_REG_07, 0x08, val);
}
/* SC8581_PMID2OUT_OVP */
static int sc8581_enable_pmid2outovp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_PMID2OUT_OVP_ENABLE : SC8581_PMID2OUT_OVP_DISABLE;
	val <<= SC8581_PMID2OUT_OVP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_08,
		SC8581_PMID2OUT_OVP_DIS_MASK, val);

	return ret;
}

static int sc8581_set_pmid2outovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (threshold < SC8581_PMID2OUT_OVP_BASE)
		threshold = SC8581_PMID2OUT_OVP_BASE;


	val = (threshold - SC8581_PMID2OUT_OVP_BASE) / SC8581_PMID2OUT_OVP_LSB;
	val <<= SC8581_PMID2OUT_OVP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_08,
				SC8581_PMID2OUT_OVP_MASK, val);

	return ret;
}
/* SC8581_PMID2OUT_UVP */
static int sc8581_enable_pmid2outuvp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_PMID2OUT_UVP_ENABLE : SC8581_PMID2OUT_UVP_DISABLE;
	val <<= SC8581_PMID2OUT_UVP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_09,
		SC8581_PMID2OUT_UVP_DIS_MASK, val);

	return ret;
}

static void sc8581_set_key(struct sc8581_device *bq, bool enable)
{
	u8 val = 0;
	int i;

	for (i = 0; i < 10; i++) {
		cp_read_byte(bq->client, SC8581_REG_76, &val);
#line 665
		mca_log_info("%s register 0x76 = 0x%x\n", val);
		if ((val & 0x01) && enable) {
#line 667
			mca_log_info("enter key mode\n");
			break;
		}
		if (!(val & 0x01) && !enable) {
#line 672
			mca_log_info("exit key mode\n");
			break;
		}
		cp_write_byte(bq->client, SC8581_REG_76, 0x44);
		cp_write_byte(bq->client, SC8581_REG_76, 0x41);
		cp_write_byte(bq->client, SC8581_REG_76, 0x47);
		cp_write_byte(bq->client, SC8581_REG_76, 0x41);
		cp_write_byte(bq->client, SC8581_REG_76, 0x4d);
		cp_write_byte(bq->client, SC8581_REG_76, 0x41);
	}
}

static void sc8581_set_vout2out_uvp(struct sc8581_device *bq, bool high)
{
	sc8581_set_key(bq, true);
	cp_write_byte(bq->client, SC8581_REG_84, 0x03);
	sc8581_set_key(bq, false);
#line 698
	mca_log_info("cp set vout2out uvp high: %d\n", high);
}

static void sc8581_set_pmid2outuvp_th(struct sc8581_device *bq, int threshold)
{
	u8 val;

	if (bq->chip_vendor == SC8585) {
		val = threshold > 7 ? 5 : 0;
	} else {
		if (threshold < SC8581_PMID2OUT_UVP_BASE)
			threshold = SC8581_PMID2OUT_UVP_BASE;
		val = (threshold - SC8581_PMID2OUT_UVP_BASE) / SC8581_PMID2OUT_UVP_LSB;
	}
	val <<= SC8581_PMID2OUT_UVP_SHIFT;
	cp_update_bits(bq->client, SC8581_REG_09,
		SC8581_PMID2OUT_UVP_MASK, val);
	if (bq->chip_vendor == SC8585 && bq->vout2out_uvp_fix)
		sc8581_set_vout2out_uvp(bq, true);
}
/* SC8581_BAT_OVP_ALM */
static int sc8581_enable_batovp_alarm(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_BAT_OVP_ALM_ENABLE : SC8581_BAT_OVP_ALM_DISABLE;
	val <<= SC8581_BAT_OVP_ALM_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_6C,
		SC8581_BAT_OVP_ALM_DIS_MASK, val);

	return ret;
}

static int sc8581_set_batovp_alarm_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (threshold < SC8581_BAT_OVP_ALM_BASE)
		threshold = SC8581_BAT_OVP_ALM_BASE;

	val = (threshold - SC8581_BAT_OVP_ALM_BASE) / SC8581_BAT_OVP_ALM_LSB;
	val <<= SC8581_BAT_OVP_ALM_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_6C,
		SC8581_BAT_OVP_ALM_MASK, val);

	return ret;
}
/* SC8581_BUS_OCP_ALM */
static int sc8581_enable_busocp_alarm(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_BUS_OCP_ALM_ENABLE : SC8581_BUS_OCP_ALM_DISABLE;
	val <<= SC8581_BUS_OCP_ALM_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_6D,
		SC8581_BUS_OCP_ALM_DIS_MASK, val);

	return ret;
}
#ifdef DEBUG_CODE
static int sc8581_set_busocp_alarm_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;

	if (threshold < SC8581_BUS_OCP_ALM_BASE)
		threshold = SC8581_BUS_OCP_ALM_BASE;

	val = (threshold - SC8581_BUS_OCP_ALM_BASE) / SC8581_BUS_OCP_ALM_LSB;
	val <<= SC8581_BUS_OCP_ALM_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_6D,
		SC8581_BUS_OCP_ALM_MASK, val);
	return ret;
}
#endif
static int sc8581_set_adc_scanrate(struct sc8581_device *bq, bool oneshot)
{
	int ret = 0;
	u8 val;

	val = oneshot ? SC8581_ADC_RATE_ONESHOT : SC8581_ADC_RATE_CONTINOUS;
	val <<= SC8581_ADC_RATE_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_15,
		SC8581_ADC_RATE_MASK, val);

	return ret;
}

#define SC8581_ADC_REG_BASE SC8581_REG_17
static int sc8581_get_adc_data(struct sc8581_device *bq, int channel, u32 *result)
{
	int ret = 0;
	unsigned int val_h;
	u16 val;
	u16 tmp;
	u8 temp_adc;

	if (channel >= ADC_MAX_NUM)
		return -1;
#line 809
	mca_log_info("adc channel = %d", channel);
	cp_read_byte(bq->client, SC8581_REG_15, &temp_adc);
#line 811
	mca_log_info("SC8581_REG_15 = %d", temp_adc);
	cp_read_byte(bq->client, SC8581_REG_16, &temp_adc);
#line 813
	mca_log_info("SC8581_REG_16 = %d", temp_adc);

	ret = cp_read_word(bq->client, SC8581_ADC_REG_BASE + (channel << 1), &tmp);
	if (ret < 0) {
#line 817
		mca_log_info("adc read error");
		return ret;
	}
	val_h = ((tmp & 0xFF) << 8);
	val = val_h | (tmp >> 8);

	switch (channel) {
	case ADC_IBUS:
		if (bq->chip_vendor == SC8585)
			val = val * SC8585_IBUS_ADC_LSB / SC8585_CURRENT_SCALE;
		else
			val = val * SC8581_IBUS_ADC_LSB / SC8581_CURRENT_SCALE;
		break;
	case ADC_VBUS:
		val = val * SC8581_VBUS_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VUSB:
		val = val * SC8581_VUSB_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VWPC:
		val = val * SC8581_VWPC_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VOUT:
		val = val * SC8581_VOUT_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_VBAT:
		val = val * SC8581_VBAT_ADC_LSB / SC8581_VOLTAGE_SCALE;
		break;
	case ADC_IBAT:
		val = val * SC8581_IBAT_ADC_LSB / SC8581_IBAT_SCALE;
		break;
	case ADC_TBAT:
		val = val * SC8581_TSBAT_ADC_LSB / SC8581_TEMP_SCALE;
		break;
	case ADC_TDIE:
		val = val * SC8581_TDIE_ADC_LSB / SC8581_TDIE_SCALE;
		break;
	default:
		break;
	}

	*result = val;
#line 859
	mca_log_debug("%s channel %d adc %x, result: %d\n", bq->log_tag, channel, tmp, *result);
	return ret;
}

static int sc8581_set_adc_scan(struct sc8581_device *bq, int channel, bool enable)
{
	int ret = 0;
	u8 reg;
	u8 mask;
	u8 shift;
	u8 val;

	if (channel > ADC_MAX_NUM)
		return -1;


	if (channel == ADC_IBUS) {
		reg = SC8581_REG_15;
		shift = SC8581_IBUS_ADC_DIS_SHIFT;
		mask = SC8581_IBUS_ADC_DIS_MASK;
	} else {
		reg = SC8581_REG_16;
		shift = 8 - channel;
		mask = 1 << shift;
	}

	val = enable ?  (0 << shift) : (1 << shift);

	ret = cp_update_bits(bq->client, reg, mask, val);

	return ret;
}
/* SC8581_SYNC_FUNCTION_EN */
static int sc8581_enable_parallel_func(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_SYNC_FUNCTION_ENABLE : SC8581_SYNC_FUNCTION_DISABLE;
	val <<= SC8581_SYNC_FUNCTION_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0E,
		SC8581_SYNC_FUNCTION_EN_MASK, val);

	return ret;
}
#ifdef DEBUG_CODE
/* SC8581_SYNC_MASTER_EN */
static int sc8581_enable_config_func(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_SYNC_CONFIG_MASTER : SC8581_SYNC_CONFIG_SLAVE;
	val <<= SC8581_SYNC_MASTER_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0E,
		SC8581_SYNC_MASTER_EN_MASK, val);

	return ret;
}
#endif
/* SC8581_REG_RST */
static int sc8581_set_reg_reset(struct sc8581_device *bq)
{
	int ret = 0;
	u8 val;

	val = SC8581_REG_RESET;
	val <<= SC8581_REG_RST_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0E,
		SC8581_REG_RST_MASK, val);

	return ret;
}

/* SC8581_MODE */
static int sc8581_set_operation_mode(struct sc8581_device *bq, int operation_mode)
{
	int ret = 0;
	u8 val;

	switch (operation_mode) {
	case SC8581_FORWARD_4_1_CHARGER_MODE:
	case SC8581_REVERSE_1_4_CONVERTER_MODE:
		val = operation_mode;
		bq->work_mode = CP_MODE_DIV4;
		break;
	case SC8581_FORWARD_2_1_CHARGER_MODE:
	case SC8581_REVERSE_1_2_CONVERTER_MODE:
		val = operation_mode;
		bq->work_mode = CP_MODE_DIV2;
		break;
	case SC8581_FORWARD_1_1_CHARGER_MODE:
	case SC8581_REVERSE_1_1_CONVERTER_MODE:
		val = operation_mode;
		bq->work_mode = CP_MODE_DIV1;
		break;
	case SC8581_FORWARD_1_1_CHARGER_MODE_REVERSEED:
		val = SC8581_FORWARD_1_1_CHARGER_MODE;
		bq->work_mode = CP_MODE_DIV1;
		break;
	case SC8581_REVERSE_1_1_CONVERTER_MODE_REVERSED:
				val = SC8581_REVERSE_1_1_CONVERTER_MODE;
		bq->work_mode = CP_MODE_DIV1;
		break;
	default:
#line 963
		mca_log_info("%s operation mode error%d\n", bq->log_tag, operation_mode);
		return -1;
	break;
	}
	bq->operation_mode = val;

	ret = sc8581_init_protection(bq, bq->work_mode);
#line 970
	mca_log_info("%s set operation mode %d reg %d work_mode %d\n", bq->log_tag, operation_mode, val, bq->work_mode);
	val <<= SC8581_MODE_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0E,
				SC8581_MODE_MASK, val);

	return ret;
}

static int sc8581_get_operation_mode(struct sc8581_device *bq, int *operation_mode)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_0E, &val);
	if (ret) {
#line 985
		mca_log_info("%s get operation mode fail\n", bq->log_tag);
		return ret;
	}

	*operation_mode = (val & SC8581_MODE_MASK);

	return ret;
}

static int sc8581_get_int_stat(struct sc8581_device *bq, int channel, bool *enable)
{
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_10, &val);
	switch (channel) {
	case VOUT_OK_REV_STAT:
		*enable = !!(val & SC8581_VOUT_OK_REV_STAT_MASK);
		break;
	case VOUT_OK_CHG_STAT:
		*enable = !!(val & SC8581_VOUT_OK_CHG_STAT_MASK);
		break;
	case VOUT_INSERT_STAT:
		*enable = !!(val & SC8581_VOUT_INSERT_STAT_MASK);
		break;
	case VBUS_PRESENT_STAT:
		*enable = !!(val & SC8581_VBUS_PRESENT_STAT_MASK);
		break;
	case VWPC_PRESENT_STAT:
		*enable = !!(val & SC8581_VWPC_INSERT_STAT_MASK);
		break;
	case VUSB_PRESENT_STAT:
		*enable = !!(val & SC8581_VUSB_INSERT_STAT_MASK);
		break;
	default:
		*enable = 0;
		break;
	}
	return ret;
}
/* SC8581_ACDRV_MANUAL_EN */
static int sc8581_enable_acdrv_manual(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val = 0;

	val = enable ? SC8581_ACDRV_MANUAL_MODE : SC8581_ACDRV_AUTO_MODE;
	val <<= SC8581_ACDRV_MANUAL_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
		SC8581_ACDRV_MANUAL_EN_MASK, val);

	return ret;
}

/* SC8581_WPCGATE_EN */
static int sc8581_enable_wpcgate(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_WPCGATE_ENABLE : SC8581_WPCGATE_DISABLE;
	val <<= SC8581_WPCGATE_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
				SC8581_WPCGATE_EN_MASK, val);

	return ret;
}

/* SC8581_OVPGATE_EN */

static int sc8581_enable_ovpgate(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;
	int i;
	bool penable = true;
	u8 value;

	bq->ovpgate_en = enable;
	if (!bq->i2c_is_working)
		msleep(30);

	val = enable ? SC8581_OVPGATE_ENABLE : SC8581_OVPGATE_DISABLE;
	val <<= SC8581_OVPGATE_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
		SC8581_OVPGATE_EN_MASK, val);

	for (i = 0; i < MCA_SET_OVPGATE_COUNT; i++) {
		ret = cp_read_byte(bq->client, SC8581_REG_0B, &value);
		if (!ret) {
			penable = !!(value & SC8581_OVPGATE_EN_MASK);
			if (bq->ovpgate_en == penable)
				break;
		}
#line 1079
		mca_log_info("%s count %d, ovpgate_en %d, penable %d\n",
			bq->log_tag, i, bq->ovpgate_en, penable);

		ret = cp_update_bits(bq->client, SC8581_REG_0B,
			SC8581_OVPGATE_EN_MASK, val);
		msleep(10);
	}
#line 1086
	mca_log_err("%s enable %d\n", bq->log_tag, enable);
	return ret;
}

static int sc8581_get_ovpgate_status(struct sc8581_device *bq, bool *enable)
{
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0F, &val);
#line 1148
	mca_log_info("%s ovpgate_status SC8581_REG_0F=0x%x\n", bq->log_tag, val);
	if (ret) {
#line 1150
		mca_log_info("%s get ovpgate status fail\n", bq->log_tag);
		return ret;
	}

	*enable = !!(val & SC8581_OVPGATE_STAT_MASK);
	return ret;
}

#define MANUAL_GATE_MASK 0x38
static int sc8581_enable_acdrv_manual_ovpgate_wpcgate(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val = 0;

	val = (enable << SC8581_OVPGATE_EN_SHIFT);
	val |= (enable << SC8581_WPCGATE_EN_SHIFT);
	val |= (enable << SC8581_ACDRV_MANUAL_EN_SHIFT);

	ret = cp_update_bits(bq->client, SC8581_REG_0B, MANUAL_GATE_MASK, val);
	return ret;
}

/* SC8581_IBAT_SNS */
static int sc8581_set_sense_resistor(struct sc8581_device *bq, int r_mohm)
{
	int ret = 0;
	u8 val;

	if (r_mohm == 1)
		val = SC8581_IBAT_SNS_RES_1MHM;
	else if (r_mohm == 2)
		val = SC8581_IBAT_SNS_RES_2MHM;
	else
		return -1;


	val <<= SC8581_IBAT_SNS_RES_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0E,
		SC8581_IBAT_SNS_RES_MASK, val);

	return ret;
}
/* SC8581_SS_TIMEOUT */
static int sc8581_set_ss_timeout(struct sc8581_device *bq, u8 val)
{

	val = val > SC8581_SS_TIMEOUT_81920MS ? SC8581_SS_TIMEOUT_DISABLE : val;
	val <<= SC8581_SS_TIMEOUT_SET_SHIFT;

	return cp_update_bits(bq->client, SC8581_REG_0D,
		SC8581_SS_TIMEOUT_SET_MASK,		val);
}
/* SC8581_WD_TIMEOUT_SET */
#ifdef DEBUG_CODE
static int sc8581_set_wdt(struct sc8581_device *bq, int ms)
{
	int ret = 0;
	u8 val;

	switch (ms) {
	case 0:
		val = SC8581_WD_TIMEOUT_DISABLE;
	case 200:
		val = SC8581_WD_TIMEOUT_0P2S;
		break;
	case 500:
		val = SC8581_WD_TIMEOUT_0P5S;
		break;
	case 1000:
		val = SC8581_WD_TIMEOUT_1S;
		break;
	case 5000:
		val = SC8581_WD_TIMEOUTADC_DATA5S;
		break;
	case 30000:
		val = SC8581_WD_TIMEOUT_30S;
		break;
	case 100000:
		val = SC8581_WD_TIMEOUT_100S;
		break;
	case 255000:
		val = SC8581_WD_TIMEOUT_255S;
		break;
	default:
		val = SC8581_WD_TIMEOUT_DISABLE;
		break;
	}

	val <<= SC8581_WD_TIMEOUT_SET_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0D,
		SC8581_WD_TIMEOUT_SET_MASK, val);
	return ret;
}
#endif
/* OTHER */
static int sc8581_set_batovp_alarm_int_mask(struct sc8581_device *bq, u8 mask)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_6C, &val);
	if (ret)
		return ret;


	val |= (mask << SC8581_BAT_OVP_ALM_MASK_SHIFT);
	ret = cp_write_byte(bq->client, SC8581_REG_6C, val);

	return ret;
}

static int sc8581_set_busocp_alarm_int_mask(struct sc8581_device *bq, u8 mask)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_6D, &val);
	if (ret)
		return ret;


	val |= (mask << SC8581_BUS_OCP_ALM_MASK_SHIFT);
	ret = cp_write_byte(bq->client, SC8581_REG_6D, val);

	return ret;
}

static int sc8581_set_ucp_fall_dg(struct sc8581_device *bq, u8 date)
{
	int ret = 0;
	u8 val;

	val = date;
	val <<= SC8581_BUS_UCP_FALL_DG_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_07,
		SC8581_BUS_UCP_FALL_DG_MASK, val);

	return ret;
}

static int sc8581_set_enable_tsbat(struct sc8581_device *bq, u8 data)
{
	int ret = 0;
	int val = 0;

	val = data ? SC8581_TSBAT_ENABLE : SC8581_TSBAT_DISABLE;
	val <<= SC8581_TSBAT_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_70,
		SC8581_TSBAT_EN_MASK, val);

	return ret;
}

static int sc8581_tsbat_flt_dis(struct sc8581_device *bq, bool disable)
{
	int ret = 0;
	u8 val;

	val = disable ? SC8581_TSBAT_FLT_DISABLE : SC8581_TSBAT_FLT_ENABLE;
	val <<= SC8581_TSBAT_FLT_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0F,
		SC8581_TSBAT_FLT_DIS_MASK, val);

	return ret;
}

static int sc8581_pin_diag_dis(struct sc8581_device *bq, bool disable)
{
	int ret = 0;
	u8 val;

	val = disable ? SC8581_PIN_DIAG_DIS_DISABLE : SC8581_PIN_DIAG_DIS_ENABLE;
	val <<= SC8581_PIN_DIAG_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_7C,
		SC8581_PIN_DIAG_DIS_MASK, val);

	return ret;
}

static int sc8581_set_sync(struct sc8581_device *bq, u8 date)
{
	int ret = 0;
	u8 val;

	val = date;
	val <<= SC8581_SYNC_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0C,
		SC8581_SYNC_MASK, val);

	return ret;
}


static int sc8581_set_switch_freq(struct sc8581_device *bq, u8 data)
{
	int ret = 0;
	u8 val;

	val = data;
	val <<= SC8581_FSW_SET_SHIFT;

	if (bq->chip_vendor == SC8585) {
		ret = cp_update_bits(bq->client, SC8581_REG_0C,
			SC8585_FSW_SET_MASK, val);
	} else {
		ret = cp_update_bits(bq->client, SC8581_REG_0C,
			SC8581_FSW_SET_MASK, val);
	}

	return ret;
}

static int sc8581_set_fsw(struct sc8581_device *cp, int fsw)
{
	int ret;
	int fsw_min = cp->fsw_cfg.min;
	int fsw_max = cp->fsw_cfg.max;
	int step_val = cp->fsw_cfg.step;
	int val;

	fsw = fsw > fsw_max ? fsw_max : (fsw < fsw_min ? fsw_min : fsw);
	val = (fsw - fsw_min) / step_val;

#line 1373
	mca_log_info("%s fsw: %d, val: %d\n", cp->log_tag, fsw, val);
	ret = sc8581_set_switch_freq(cp, val);

	return ret;
}

static int sc8581_get_fsw(struct sc8581_device *cp, int *fsw)
{
	int ret;
	u8 byte;
	int val;

	if (cp->chip_vendor == SC8585) {
		ret = cp_read_byte(cp->client, SC8581_REG_0C, &byte);
		val = (byte & SC8585_FSW_SET_MASK) >> SC8585_FSW_SET_SHIFT;
	} else {
		ret = cp_read_byte(cp->client, SC8581_REG_0C, &byte);
		val = (byte & SC8581_FSW_SET_MASK) >> SC8581_FSW_SET_SHIFT;
	}

	*fsw = cp->fsw_cfg.min + val * cp->fsw_cfg.step;
#line 1394
	mca_log_info("%s byte: %02x, val: %d, fsw: %d\n", cp->log_tag, byte, val, *fsw);

	return ret;
}

static int sc8581_get_tdie(struct sc8581_device *cp, int *tdie)
{
	int ret = 0;
	int val = 0;
	u8 byte_h = 0;
	u8 byte_l = 0;

	*tdie = 0;
	if (cp->chip_vendor == SC8585) {
		ret = cp_read_byte(cp->client, SC8581_REG_27, &byte_h);
		ret |= cp_read_byte(cp->client, SC8581_REG_28, &byte_l);
		if (ret) {
#line 1411
			mca_log_err("read tdie adc failed, ret=%d\n", ret);
			return 0;
		}
		val = (byte_h & SC8581_TDIE_POL_H_MASK << 8) | (byte_l & SC8581_TDIE_POL_L_MASK);
	}

	*tdie = val * SC8581_TDIE_ADC_LSB;
#line 1418
	mca_log_info("%s byte_h: %02x, byte_l: %02x, val: %d, tdie: %d\n",
		cp->log_tag, byte_h, byte_l, val, *tdie);

	return ret;
}

static int sc8581_set_acdrv_up(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_ACDRV_UP_ENABLE : SC8581_ACDRV_UP_DISABLE;
	val <<= SC8581_ACDRV_UP_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_7C,
		SC8581_ACDRV_UP_MASK, val);

	return ret;
}

enum cp_reg_idx {
	VBAT_OVP_REG = 0,
	IBAT_OCP_REG,
	VUSB_OVP_REG,
	VWPC_OVP_REG,
	VOUT_VBUS_OVP_REG,
	IBUS_OCP_REG,
	IBUS_UCP_REG,
	PMID2OUT_OVP_REG,
	PMID2OUT_UVP_REG,
	CONVERTER_STATE_REG,
	CTRL1_REG,
	CTRL2_REG,
	CTRL3_REG,
	CTRL4_REG,
	CTRL5_REG,
	INT_STAT_REG,
	INT_FLAG_REG,
	FLT_FLAG_REG,
	DEVICE_ID_REG,
	FAULT_STATUS_REG,
	CP_REG_MAX,
};

/*
static unsigned int sc8581_reg_list[] = {
	0x01, // VBAT_OVP_REG
	0x02, // IBAT_OCP_REG
	0x03, // VUSB_OVP_REG
	0x04, // VWPC_OVP_REG
	0x05, // VOUT_VBUS_OVP_REG
	0x06, // IBUS_OCP_REG
	0x07, // IBUS_UCP_REG
	0x08, // PMID2OUT_OVP_REG
	0x09, // PMID2OUT_UVP_REG
	0x0A, // CONVERTER_STATE_REG
	0x0B, // CTRL1_REG
	0x0C, // CTRL2_REG
	0x0D, // CTRL3_REG
	0x0E, // CTRL4_REG
	0x0F, // CTRL5_REG
	0x10, // INT_STAT_REG
	0x11, // INT_FLAG_REG
	0x13, // FLT_FLAG_REG
	0x6E, // DEVICE_ID_REG
	0x70, // FAULT_STATUS_REG
};
#define CP_MAX_REG_NUM			ARRAY_SIZE(sc8581_reg_list)
*/

static void sc8581_abnormal_charging_judge(struct sc8581_device *bq, u8 *data)
{
	int val[2] = {0};
	static int cp_cboot_short = 0;

	if (!data)
		return;

	if ((data[INT_STAT_REG] & 0x08) == 0) {
#line 1496
		mca_log_info("VOUT UVLO\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VOUT_UVLO, NULL);
	}

	if ((data[INT_STAT_REG] & 0x3F) != 0x3F)
#line 1501
		mca_log_info("%s VIN have problem\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x08)
#line 1504
		mca_log_info("%s VBUS_ERRORHI_STAT\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x10)
#line 1507
		mca_log_info("%sVBUS_ERRORLO_STAT\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x01) {
#line 1510
		mca_log_info("%s CBOOT SHORT/OPEN 111\n", bq->log_tag);
		if (!cp_cboot_short) {
                	cp_cboot_short = 1;
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_CBOOT_FAIL, &cp_cboot_short);
                }
	} else if (cp_cboot_short) {
#line 1516
		mca_log_info("%s CBOOT SHORT/OPEN 000\n", bq->log_tag);
		cp_cboot_short = 0;
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_CBOOT_FAIL, &cp_cboot_short);
	}

	if (data[VBAT_OVP_REG] & 0x20) {
#line 1522
		mca_log_info("%s VBAT_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VBAT_OVP, NULL);
	}

	if (bq->chip_vendor != SC8585) {
		if (data[IBAT_OCP_REG] & 0x10) {
#line 1528
			mca_log_info("%s IBAT_OCP\n", bq->log_tag);
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IBAT_OCP, NULL);
		}
	}

	if (data[VUSB_OVP_REG] & 0x20) {
#line 1534
		mca_log_info("%s VUSB_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_OVP, NULL);
	}

	if (data[VWPC_OVP_REG] & 0x20) {
#line 1539
		mca_log_info("%s VWPC_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VWPC_OVP, NULL);
	}

	if (data[IBUS_OCP_REG] & 0x20) {
#line 1544
		mca_log_info("%s IBUS_OCP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IBUS_OCP, NULL);
	}

	if (data[IBUS_UCP_REG] & 0x01) {
#line 1549
		mca_log_info("%s IBUS_UCP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IBUS_UCP, NULL);
	}

	if (data[PMID2OUT_OVP_REG] & 0x08) {
#line 1554
		mca_log_info("%s PMID2OUT_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_PMID2OUT_OVP, NULL);
	}
	if (data[PMID2OUT_UVP_REG] & 0x08) {
#line 1558
		mca_log_info("%s PMID2OUT_UVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_PMID2OUT_UVP, NULL);
	}

	if (data[CONVERTER_STATE_REG] & 0x80) {
#line 1563
		mca_log_info("%s POR_FLAG\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_POR_FLAG, NULL);
	}

	if (data[FLT_FLAG_REG] & SC8581_TSHUT_FLAG_MASK) {
#line 1568
		mca_log_info("%s TSHUT\n", bq->log_tag);
		cp_get_adc_data(bq, ADC_TDIE, &val[0]);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_TSHUT_FLAG, val);
	}

	if (data[FLT_FLAG_REG] & SC8581_VBUS_OVP_FLAG_MASK) {
#line 1574
		mca_log_info("%s vbus ovp\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VBUS_OVP, NULL);
	}
}

#define CP_DUMP_REG_LEN			16
static int sc8581_dump_important_regs(struct sc8581_device *bq, union cp_propval *val)
{
	int ret = 0;
	u8 data[CP_DUMP_REG_LEN] = { 0 };
	u8 reg[CP_REG_MAX] = { 0 };
	u8 base_addr;
	int i = 0;

	base_addr = SC8581_REG_00;
	ret = cp_read_i2c_block_data(bq->client, base_addr, CP_DUMP_REG_LEN, data);
	if (ret < 0) {
#line 1591
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		memcpy(reg, data + 1, 15);
		for (i = 0; i < CP_DUMP_REG_LEN; i += 8) {
#line 1595
			mca_log_info("%s: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, "
				"[0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X",
				bq->log_tag, base_addr + i, data[i], base_addr + i + 1, data[i + 1],
				base_addr + i + 2, data[i + 2], base_addr + i + 3, data[i + 3],
				base_addr + i + 4, data[i + 4], base_addr + i + 5, data[i + 5],
				base_addr + i + 6, data[i + 6], base_addr + i + 7, data[i + 7]);
		}
	}

	if (!!(data[11] & SC8581_OVPGATE_EN_MASK) != bq->ovpgate_en) {
#line 1605
		mca_log_info("set cp ovpgate not effective, repeat set ovpgate\n");
		sc8581_enable_ovpgate(bq, bq->ovpgate_en);
	}

	base_addr = SC8581_REG_10;
	ret = cp_read_i2c_block_data(bq->client, base_addr, CP_DUMP_REG_LEN, data);
	if (ret < 0) {
#line 1612
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		reg[INT_STAT_REG] = data[SC8581_REG_10 - base_addr];
		reg[INT_FLAG_REG] = data[SC8581_REG_11 - base_addr];
		reg[FLT_FLAG_REG] = data[SC8581_REG_13 - base_addr];
		for (i = 0; i < CP_DUMP_REG_LEN; i += 8) {
#line 1618
			mca_log_info("%s: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, "
				"[0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X",
				bq->log_tag, base_addr + i, data[i], base_addr + i + 1, data[i + 1],
				base_addr + i + 2, data[i + 2], base_addr + i + 3, data[i + 3],
				base_addr + i + 4, data[i + 4], base_addr + i + 5, data[i + 5],
				base_addr + i + 6, data[i + 6], base_addr + i + 7, data[i + 7]);
		}
	}

	base_addr = SC8581_REG_6E;
	ret = cp_read_i2c_block_data(bq->client, base_addr, 4, data);
	if (ret < 0) {
#line 1630
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		reg[DEVICE_ID_REG] = data[SC8581_REG_6E - base_addr];
		reg[FAULT_STATUS_REG] = data[SC8581_REG_70 - base_addr];
#line 1634
		mca_log_info("%s: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X",
			bq->log_tag, base_addr, data[0], base_addr + 1, data[1], base_addr + 2, data[2], base_addr + 3, data[3]);
	}

	// CONVERTER STATE Register (Address=0Ah)
	if ((reg[CONVERTER_STATE_REG] & SC8581_CP_SWITCHING_STAT_MASK) == 0) {
#line 1640
		mca_log_info("%s cp switching stop, enter abnormal charging judge\n", bq->log_tag);
		sc8581_abnormal_charging_judge(bq, reg);
	}

	return 0;
}

static int sc8581_init_int_src(struct sc8581_device *bq)
{
	int ret = 0;

	ret = sc8581_set_batovp_alarm_int_mask(bq, SC8581_BAT_OVP_ALM_NOT_MASK);
	if (ret) {
#line 1653
		mca_log_info(" %s failed to set alarm mask:%d\n", bq->log_tag, ret);
		return ret;
	}
	ret = sc8581_set_busocp_alarm_int_mask(bq, SC8581_BUS_OCP_ALM_NOT_MASK);
	if (ret) {
#line 1658
		mca_log_info("%s failed to set alarm mask:%d\n", bq->log_tag, ret);
		return ret;
	}

	return ret;
}

static int sc8581_init_protection(struct sc8581_device *cp, int work_mode)
{
	int ret = 0;

	ret = sc8581_enable_batovp(cp, true);
	ret = sc8581_enable_batocp(cp, false);
	ret = sc8581_enable_busocp(cp, true);
	sc8581_enable_busucp(cp, true);
	ret = sc8581_enable_pmid2outovp(cp, true);
	ret = sc8581_enable_pmid2outuvp(cp, true);
	ret = sc8581_enable_batovp_alarm(cp, true);
	ret = sc8581_enable_busocp_alarm(cp, true);
	ret = sc8581_set_batovp_th(cp, cp->cfg.bat_ovp_th);
	ret = sc8581_set_batocp_th(cp, BAT_OCP_TH);
	ret = sc8581_set_batovp_alarm_th(cp, cp->cfg.bat_ovp_alarm_th);
	ret = sc8581_set_busovp_th(cp, cp->cfg.bus_ovp_th[work_mode]);
	ret = sc8581_set_usbovp_th(cp, cp->cfg.usb_ovp_th[work_mode]);
	ret = sc8581_set_busocp_th(cp, cp->cfg.bus_ocp_th[work_mode]);

	ret = sc8581_set_wpcovp_th(cp, cp->cfg.wpc_ovp_th);
	ret = sc8581_set_outovp_th(cp, cp->cfg.out_ovp_th);
	sc8581_set_pmid2outuvp_th(cp, cp->cfg.pmid2out_uvp_th);
	ret = sc8581_set_pmid2outovp_th(cp, cp->cfg.pmid2out_ovp_th);

	return ret;
}

static int sc8581_init_adc(struct sc8581_device *cp)
{
	sc8581_set_adc_scanrate(cp, false);
	sc8581_set_adc_scan(cp, ADC_IBUS, true);
	sc8581_set_adc_scan(cp, ADC_VBUS, true);
	sc8581_set_adc_scan(cp, ADC_VUSB, true);
	sc8581_set_adc_scan(cp, ADC_VWPC, true);
	sc8581_set_adc_scan(cp, ADC_VOUT, true);
	sc8581_set_adc_scan(cp, ADC_VBAT, true);
	if (cp->chip_vendor != SC8585) {
		sc8581_set_adc_scan(cp, ADC_IBAT, true);
		sc8581_set_adc_scan(cp, ADC_TBAT, true);
	}
	sc8581_set_adc_scan(cp, ADC_TDIE, true);
	sc8581_enable_adc(cp, false);

	return 0;
}

static int sc8581_init_device(struct sc8581_device *cp)
{
	int ret = 0;
	int retry_cnt = 0;

	sc8581_set_reg_reset(cp);
	sc8581_enable_parallel_func(cp, false);

	while (retry_cnt < ERROR_RECOVERY_COUNT)	{
		sc8581_enable_acdrv_manual_ovpgate_wpcgate(cp, 1);
		ret |= sc8581_set_ss_timeout(cp, SC8581_SS_TIMEOUT_5120MS);
		ret |= sc8581_set_ucp_fall_dg(cp, SC8581_BUS_UCP_FALL_DG_5MS);
		if (cp->chip_vendor != SC8585) {
			ret |= sc8581_set_acdrv_up(cp, true);
			ret |= sc8581_set_sense_resistor(cp, 1);
			ret |= sc8581_set_enable_tsbat(cp, SC8581_TSBAT_DISABLE);
		}
		ret |= sc8581_set_sync(cp, SC8581_SYNC_NO_SHIFT);
		ret |= sc8581_set_ovpgate_on_dg_set(cp, SC8581_OVPGATE_ON_DG_20MS);
		ret |= sc8581_init_adc(cp);
		ret |= sc8581_init_int_src(cp);
		ret |= sc8581_set_operation_mode(cp, SC8581_FORWARD_2_1_CHARGER_MODE);
		if (cp->chip_vendor == SC8581) {
			ret |= sc8581_tsbat_flt_dis(cp, SC8581_TSBAT_FLT_DISABLE);
			ret |= sc8581_pin_diag_dis(cp, SC8581_PIN_DIAG_DIS_DISABLE);
		}

		if (cp->chip_vendor == SC8585) {
			cp->fsw_cfg.min = SC8585_FSW_MIN;
			cp->fsw_cfg.max = SC8585_FSW_MAX;
			cp->fsw_cfg.step = SC8585_FSW_STEP;
			cp_write_byte(cp->client, SC8581_REG_42, 0x82); // pengwanglin@xiaomi.com: enable resonance
		} else {
			cp->fsw_cfg.min = SC8581_FSW_MIN;
			cp->fsw_cfg.max = SC8581_FSW_MAX;
			cp->fsw_cfg.step = SC8581_FSW_STEP;
		}
		sc8581_set_fsw(cp, CP_DEFAULT_FSW);

		if (ret < 0) {
			retry_cnt++;
		} else {
#line 1754
			mca_log_err("%s success to init CP device\n", cp->log_tag);
			break;
		}
	}

	return ret;
}

static int cp_charge_detect_device(struct sc8581_device *bq)
{
	int ret = 0;
	u8 data;
	int retry_cnt = 0;

	while (retry_cnt < ERROR_RECOVERY_COUNT) {
		ret = cp_read_byte(bq->client, SC8581_REG_6E, &data);
		if (ret < 0) {
			retry_cnt++;
			msleep(100);
#line 2382
			mca_log_info(" %s failed to read device id, retry count = %d\n", bq->log_tag, retry_cnt);
		} else
			break;
	}

	if (retry_cnt == ERROR_RECOVERY_COUNT && ret < 0) {
#line 2388
		mca_log_info("%s failed to detect CP device. retry %d times\n", bq->log_tag, retry_cnt);
		return ret;
	}

#line 2392
	mca_log_info("%s sucess read device id = %x\n", bq->log_tag, data);
	if (data == SC8585_DEVICE_ID)
		bq->chip_vendor = SC8585;
	else if (data == SC8581_DEVICE_ID)
		bq->chip_vendor = SC8581;
	else if (data == SC8561_DEVICE_ID)
		bq->chip_vendor = SC8561;
	else {
#line 2400
		mca_log_info("%s device_id is invalid\n",  bq->log_tag);
		return -1;
	}

	return ret;
}

static int cp_get_adc_data(struct sc8581_device *bq, int channel,  u32 *result)
{
	int ret = 0;

	ret = sc8581_get_adc_data(bq, channel, result);

	if (ret)
#line 1769
		mca_log_info(" %s failed get ADC value\n", bq->log_tag);


	return ret;
}

static int ops_cp_get_int_stat(int channel, bool *result, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_get_int_stat(bq, channel, result);

	if (ret)
#line 1783
		mca_log_info("failed get int stat %d\n", channel);

	return ret;
}

static int cp_enable_adc(struct sc8581_device *bq, bool enable)
{
	int ret = 0;

	ret = sc8581_enable_adc(bq, enable);

	if (ret)
		mca_log_info("%s failed to enable/disable ADC\n", bq->log_tag);

	return ret;
}

static int ops_cp_dump_register(void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_dump_important_regs(bq, NULL);

	if (ret)
		mca_log_info("%s failed dump registers ret=%d\n", bq->log_tag, ret);

	return ret;
}

static int ops_cp_get_chip_vendor(int *chip_id, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	*chip_id = bq->chip_vendor;
#line 1818
	mca_log_info("%s %d\n", bq->log_tag, *chip_id);

	return 0;
}

static int ops_cp_enable_charge(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sc8581_enable_charge(bq, enable);
	return 0;
}

static int ops_cp_get_charge_enable(bool *enabled, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_check_charge_enabled(bq, enabled);

	if (ret)
#line 1844
		mca_log_err("%s failed get enable cp charge status ret =%d\n", bq->log_tag, ret);


	return ret;
}

static int ops_cp_enable_qb(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sc8581_enable_qb(bq, enable);
	return 0;
}

static int ops_cp_set_rcp(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_disable_rcp(bq, enable);

	if (ret)
		mca_log_err("%s failed enable cp rcp\n", bq->log_tag);

	return ret;
}

static int ops_cp_set_pmid2outuvp_th(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sc8581_set_pmid2outuvp_th(bq, value);
	return 0;
}

static int ops_cp_get_vbus(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_VBUS, val);

	return ret;
}

static int ops_cp_get_vusb(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_VUSB, val);

	return ret;

}

static int ops_cp_get_ibus(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_IBUS, val);

	return ret;
}

static int ops_cp_get_vbatt(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_VBAT, val);

	return ret;
}

static int ops_cp_get_ibatt(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_IBAT, val);

	return ret;
}

static int ops_cp_get_vout(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_VOUT, val);

	return ret;
}

static int ops_cp_set_mode(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_set_operation_mode(bq, value);

	if (ret)
#line 1958
		mca_log_err("%s failed set cp charge mode\n", bq->log_tag);

	return ret;
}

static int ops_cp_get_mode(int *mode, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_get_operation_mode(bq, mode);
	if (ret)
#line 1970
		mca_log_err("%s failed to get div_mode\n", bq->log_tag);

	return ret;
}

static int ops_cp_device_init(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_init_device(bq);

	if (ret)
#line 1983
		mca_log_err("%s failed init cp init device\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_adc(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_enable_adc(bq, enable);

	return ret;
}

static int ops_cp_get_bypass_support(bool *enabled, void *chg_dev)
{
	*enabled = true;
#line 2001
	mca_log_info("%s %d\n", __func__, *enabled);

	return 0;
}

static int ops_enable_acdrv_manual(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_enable_acdrv_manual(bq, enable);

	if (ret)
		mca_log_info("%s failed enable cp acdrv manual\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_wpcgate(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_enable_wpcgate(bq, enable);
	if (ret)
		mca_log_info("%s failed enable cp wpcgate\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_ovpgate(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_enable_ovpgate(bq, enable);

	if (ret)
		mca_log_info("%s failed enable cp ovpgate\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_ovpgate_with_check(int type_temp, bool en, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;
	static int ovp_type = 0;
	static bool ovp_enable_flag = true;

	if (en)
		ovp_type &= ~(1 << type_temp);
	else
		ovp_type |= (1 << type_temp);

	if ((!!ovp_type && ovp_enable_flag) || (!ovp_type && !ovp_enable_flag)) {
		ovp_enable_flag = en;

		ret = sc8581_enable_ovpgate(bq, en);
		if (ret)
			mca_log_info("%s failed enable cp ovpgate with check\n", bq->log_tag);
	}
	return ret;
}

static int ops_cp_get_ovpgate_status(bool *enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_get_ovpgate_status(bq, enable);

	if (ret)
#line 2075
		mca_log_info("%s failed get cp ovpgate status\n", bq->log_tag);

	return ret;

}

static int ops_cp_get_present(bool *present, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	*present  = bq->chip_ok;
	return ret;
}

static int ops_cp_get_battery_temmperature(u32 *val, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = cp_get_adc_data(bq, ADC_TBAT, val);
	return ret;

}

static int ops_cp_get_battery_present(bool *present, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sc8581_get_int_stat(bq, VOUT_INSERT_STAT, present);

	return ret;
}

static int ops_cp_get_errorhl_stat(int *stat, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0A, &val);
	if (ret) {
#line 2149
		mca_log_err("%s get pmid error stat fail\n", bq->log_tag);
		return ret;
	}

	*stat = CP_PMID_ERROR_OK;
	if (val & SC8581_VBUS_ERRORLO_STAT_MASK)
		*stat = CP_PMID_ERROR_LOW;
	else if (val & SC8581_VBUS_ERRORHI_STAT_MASK)
		*stat = CP_PMID_ERROR_HIGH;

#line 2159
	mca_log_info("%s val: 0x%02x, stat: %d", bq->log_tag, val, *stat);
	return ret;
}

static int ops_cp_enable_busucp(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sc8581_enable_busucp(bq, enable);
	return 0;
}

static int ops_cp_set_fsw(int fsw, void *chg_dev)
{
	return sc8581_set_fsw((struct sc8581_device *)chg_dev, fsw);
}

static int ops_cp_set_default_fsw(void *chg_dev)
{
	return sc8581_set_fsw((struct sc8581_device *)chg_dev, CP_DEFAULT_FSW);
}

static int ops_cp_get_fsw(int *fsw, void *chg_dev)
{
	return sc8581_get_fsw((struct sc8581_device *)chg_dev, fsw);
}

static int ops_cp_get_fsw_step(int *fsw_step, void *chg_dev)
{
	struct sc8581_device *cp = (struct sc8581_device *)chg_dev;

	*fsw_step = CP_DEFAULT_FSW;
	if (cp)
		*fsw_step = cp->fsw_cfg.step;

	return 0;
}

static int ops_cp_get_tdie(int *tdie, void *chg_dev)
{
	return sc8581_get_tdie((struct sc8581_device *)chg_dev, tdie);
}

static int sc8581_get_ovpgate_enable(struct sc8581_device *bq, bool *enable)
{
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0B, &val);
#line 1096
	mca_log_info("%s ovpgate_status SC8581_REG_0B=0x%x\n", bq->log_tag, val);
	if (ret) {
#line 1098
		mca_log_info("%s get ovpgate enable fail\n", bq->log_tag);
		return ret;
	}

	*enable = !!(val & SC8581_OVPGATE_EN_MASK);
	return ret;
}

static int sc8581_set_revchg(struct sc8581_device *bq, bool en)
{
	int mode = 0;
	bool ovpgate = false;
	bool charging = false;

	if (en) {
		sc8581_set_operation_mode(bq, SC8581_REVERSE_1_2_CONVERTER_MODE);
		sc8581_enable_ovpgate(bq, true);
		sc8581_get_ovpgate_enable(bq, &ovpgate);
		sc8581_get_operation_mode(bq, &mode);
#line 1122
		mca_log_info("cp mode: %d, ovpgate_enable status: %d", mode, ovpgate);
		if (ovpgate && mode == SC8581_REVERSE_1_2_CONVERTER_MODE)
			return 0;
	} else {
		sc8581_enable_charge(bq, false);
		sc8581_enable_qb(bq, false);
		sc8581_set_operation_mode(bq, SC8581_FORWARD_2_1_CHARGER_MODE);
		sc8581_get_operation_mode(bq, &mode);
		sc8581_check_charge_enabled(bq, &charging);
#line 1133
		mca_log_info("cp mode: %d, charging_enable: %d", mode, charging);
		if (mode == SC8581_FORWARD_2_1_CHARGER_MODE && !charging)
			return 0;
	}

	return -1;
}

static int ops_cp_set_revchg(bool en, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int retry = 0;

	for (retry = 0; retry < 10; retry++) {
		if (!sc8581_set_revchg(bq, en))
			return 0;
#line 2092
		mca_log_info("%s failed set revchg, retry: %d\n", bq->log_tag, retry);
		mdelay(20);
	}

	return -1;
}

static int ops_cp_set_adjustadble_timeout(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	cp_update_bits(bq->client, SC8581_REG_0D, 0xFF, value);
	return 0;
}

static struct platform_class_cp_ops sc8581_chg_ops = {
	.cp_set_enable = ops_cp_enable_charge,
	.cp_get_enabled = ops_cp_get_charge_enable,
	.cp_get_bus_voltage = ops_cp_get_vbus,
	.cp_get_bus_current = ops_cp_get_ibus,
	.cp_get_battery_voltage = ops_cp_get_vbatt,
	.cp_get_battery_current = ops_cp_get_ibatt,
	.cp_get_battery_temperature = ops_cp_get_battery_temmperature,
	.cp_get_battery_present = ops_cp_get_battery_present,
	.cp_set_mode = ops_cp_set_mode,
	.cp_get_mode = ops_cp_get_mode,
	.cp_device_init = ops_cp_device_init,
	.cp_enable_adc = ops_cp_enable_adc,
	.cp_get_bypass_support = ops_cp_get_bypass_support,
	.cp_dump_register = ops_cp_dump_register,
	.cp_get_chip_vendor = ops_cp_get_chip_vendor,
	.cp_enable_acdrv_manual = ops_enable_acdrv_manual,
	.cp_enable_ovpgate = ops_cp_enable_ovpgate,
	.cp_enable_ovpgate_with_check = ops_cp_enable_ovpgate_with_check,
	.cp_enable_wpcgate = ops_cp_enable_wpcgate,
	.cp_get_ovpgate_status = ops_cp_get_ovpgate_status,
	.cp_get_present = ops_cp_get_present,
	.cp_get_usb_voltage = ops_cp_get_vusb,
	.cp_get_int_stat = ops_cp_get_int_stat,
	.cp_get_errorhl_stat = ops_cp_get_errorhl_stat,
	.cp_enable_busucp = ops_cp_enable_busucp,
	.cp_set_fsw = ops_cp_set_fsw,
	.cp_set_default_fsw = ops_cp_set_default_fsw,
	.cp_get_fsw = ops_cp_get_fsw,
	.cp_get_fsw_step = ops_cp_get_fsw_step,
	.cp_get_tdie = ops_cp_get_tdie,
	.cp_set_qb = ops_cp_enable_qb,
	.cp_set_pmid2outuvp_th = ops_cp_set_pmid2outuvp_th,
	.cp_set_rcp = ops_cp_set_rcp,
	.cp_set_revchg = ops_cp_set_revchg,
	.cp_set_adjustadble_timeout = ops_cp_set_adjustadble_timeout,
};

static void sc8581_irq_handler(struct work_struct *work)
{
	struct sc8581_device *bq = container_of(work, struct sc8581_device, irq_handle_work.work);
	bool usb_present;
	static bool last_usb_present;

	sc8581_get_int_stat(bq, VUSB_PRESENT_STAT, &usb_present);
	if (usb_present != last_usb_present) {
		if (usb_present)
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_INSERT, NULL);
		else
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_OUT, NULL);
		last_usb_present = usb_present;
	}

#line 2259
	mca_log_info("%s handler\n", bq->log_tag);
	if (bq->i2c_is_working)
		sc8581_dump_important_regs(bq, NULL);
}

static irqreturn_t sc8581_int_isr(int irq, void *private)
{
	struct sc8581_device *bq = private;

#line 2268
	mca_log_info("%s %s\n", bq->log_tag, __func__);

	pm_wakeup_dev_event(bq->dev, 500, true);
	schedule_delayed_work(&bq->irq_handle_work, 0);

	return IRQ_HANDLED;
}

static int sc8581_parse_dt(struct sc8581_device *bq)
{
	struct device_node *np = bq->dev->of_node;
	int ret = 0;

	if (!np) {
#line 2282
		mca_log_err("device tree info missing\n");
		return -1;
	}

	mca_parse_dts_u32(np, "ic_role", &bq->cp_role, 0);

	if (bq->cp_role == SC8581_SLAVE)
		strscpy(bq->log_tag, "[1]", sizeof("[1]"));
	else
		strscpy(bq->log_tag, "[0]", sizeof("[0]"));


	bq->irq_gpio = of_get_named_gpio(np, "cp-int", 0);
	if (!gpio_is_valid(bq->irq_gpio)) {
#line 2296
		mca_log_err("%s failed to parse irq_gpio\n", bq->log_tag);
		return -1;
	}

	bq->nlpm_gpio = of_get_named_gpio(np, "cp-nlpm-gpio", 0);
	if (!gpio_is_valid(bq->nlpm_gpio)) {
#line 2302
		mca_log_err("%s failed to parse  sc858_nlpm_gpio\n", bq->log_tag);
		return -1;
	}

	mca_parse_dts_u32(np, "bat-ovp-threshold", &bq->cfg.bat_ovp_th, BAT_OVP_TH);
	mca_parse_dts_u32(np, "bat-ovp-alarm-threshold", &bq->cfg.bat_ovp_alarm_th, BAT_OVP_ALARM_TH);
	mca_parse_dts_u32_array(np, "bus-ovp-threshold", bq->cfg.bus_ovp_th, CP_MODE_DIV_MAX);
#line 2309
	mca_log_info("%s bus_ovp [%d,%d,%d]\n", bq->log_tag, bq->cfg.bus_ovp_th[0], bq->cfg.bus_ovp_th[1], bq->cfg.bus_ovp_th[2]);
	mca_parse_dts_u32_array(np, "usb-ovp-threshold", bq->cfg.usb_ovp_th, CP_MODE_DIV_MAX);
#line 2311
	mca_log_info("%s usb_ovp [%d,%d,%d]\n", bq->log_tag, bq->cfg.usb_ovp_th[0], bq->cfg.usb_ovp_th[1], bq->cfg.usb_ovp_th[2]);
	mca_parse_dts_u32_array(np, "bus-ocp-threshold", bq->cfg.bus_ocp_th, CP_MODE_DIV_MAX);
#line 2313
	mca_log_info("%s bus_ocp [%d,%d,%d]\n", bq->log_tag, bq->cfg.bus_ocp_th[0], bq->cfg.bus_ocp_th[1], bq->cfg.bus_ocp_th[2]);
	mca_parse_dts_u32(np, "wpc-ovp-threshold", &bq->cfg.wpc_ovp_th, WPC_OVP_TH);
	mca_parse_dts_u32(np, "out-ovp-threshold", &bq->cfg.out_ovp_th, OUT_OVP_TH);
	mca_parse_dts_u32(np, "pmid2-uvp-threshold", &bq->cfg.pmid2out_uvp_th, PMID2OUT_UVP_TH);
	mca_parse_dts_u32(np, "pmid2-ovp-threshold", &bq->cfg.pmid2out_ovp_th, PMID2OUT_OVP_TH);
	bq->vout2out_uvp_fix = of_property_read_bool(np, "support_reverse_quick_charge");

	return ret;
}

static int sc8581_register_irq(struct sc8581_device *bq)
{
	int ret = 0;

	ret = devm_gpio_request(bq->dev, bq->irq_gpio, dev_name(bq->dev));
	if (ret < 0) {
#line 2328
		mca_log_info(" %s failed to master request gpio\n", bq->log_tag);
		return -1;
	}

	bq->irq = gpio_to_irq(bq->irq_gpio);
	if (bq->irq_gpio < 0) {
#line 2334
		mca_log_info("%s failed to master get gpio_irq\n", bq->log_tag);
		return -1;
	}

	ret = request_irq(bq->irq, sc8581_int_isr,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		dev_name(bq->dev), bq);
	if (ret < 0) {
#line 2342
		mca_log_info("%s failed to master request irq\n", bq->log_tag);
		return -1;
	}

	enable_irq_wake(bq->irq);

	return 0;
}

static int sc8581_init_gpio(struct sc8581_device *bq)
{
	int ret = 0;

	if (bq->cp_role != SC8581_MASTER)
		return ret;

	ret = devm_gpio_request(bq->dev, bq->nlpm_gpio, dev_name(bq->dev));
	if (ret)
#line 2360
		mca_log_info("%s unable to request nlpm gpio [%d]\n", bq->log_tag, bq->nlpm_gpio);
	else {
		ret = gpio_direction_output(bq->nlpm_gpio, 1);
		if (ret)
#line 2364
			mca_log_info("%s unable to set direction for nlpm gpio[%d]\n", bq->log_tag, bq->nlpm_gpio);
		msleep(400);
	}

	return ret;
}

static int sc8581_dump_log_head(void *data, char *buf, int size)
{
	struct sc8581_device *bq = (struct sc8581_device *)data;

	if (!data)
		return 0;

	if (bq->cp_role == 0)
		return snprintf(buf, size, "cp_vusb cp_vbus cp_ibus cp_ibat cp_vbat cp_vout ");
	else
		return snprintf(buf, size, "cp_vusb1 cp_vbus1 cp_ibus1 cp_ibat1 cp_vbat1 cp_vout1 ");
}

static int sc8581_dump_log_context(void *data, char *buf, int size)
{
	struct sc8581_device *bq = (struct sc8581_device *)data;
	unsigned int vbus, vusb, ibus, ibat, vbat, vout;

	if (!data)
		return snprintf(buf, size, "%-8d%-8d%-8d%-8d%-8d%-8d", -1, -1, -1, -1, -1, -1);

	(void)ops_cp_get_vbus(&vbus, data);
	(void)ops_cp_get_vusb(&vusb, data);
	(void)ops_cp_get_ibus(&ibus, data);
	(void)ops_cp_get_ibatt(&ibat, data);
	(void)ops_cp_get_vbatt(&vbat, data);
	(void)ops_cp_get_vout(&vout, data);

	if (bq->cp_role == 0)
		return snprintf(buf, size, "%-8u%-8u%-8u%-8u%-8u%-8u",
			vusb, vbus, ibus, ibat, vbat, vout);
	else
		return snprintf(buf, size, "%-9u%-9u%-9u%-9u%-9u%-9u",
			vusb, vbus, ibus, ibat, vbat, vout);
}

static struct mca_log_charge_log_ops g_sc8581_log_ops = {
	.dump_log_head = sc8581_dump_log_head,
	.dump_log_context = sc8581_dump_log_context,
};

static bool mca_cp_is_en_buck_project_hwid(void)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!hwid) {
#line 2453
		mca_log_err("get hwid info failed\n");
		return false;
	}

	return hwid->platform_version == 3 && hwid->hwid_value == 0x10004;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
static int sc8581_probe(struct i2c_client *client)
#else
static int sc8581_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct device *dev = &client->dev;
	struct sc8581_device *bq;
	int ret = 0;

	bq = devm_kzalloc(dev, sizeof(*bq), GFP_KERNEL);
	if (!bq)
		return -ENOMEM;

	if (mca_cp_is_en_buck_project_hwid()) {
#line 2478
		mca_log_err("adjust en buck CP addr");
		client->addr = 0x6f;
	} else {
#line 2481
		mca_log_err("normal CP addr");
	}

	bq->client = client;
	bq->dev = dev;
	i2c_set_clientdata(client, bq);

	ret = sc8581_parse_dt(bq);
	if (ret) {
#line 2489
		mca_log_err(" %s failed to parse DTS\n", bq->log_tag);
		return ret;
	}

	ret = sc8581_init_gpio(bq);
	ret = cp_charge_detect_device(bq);
	if (ret) {
#line 2496
		mca_log_err("%s failed to detect device\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IIC_ERROR, NULL);
		return ret;
	}

	INIT_DELAYED_WORK(&bq->irq_handle_work, sc8581_irq_handler);
	ret = sc8581_register_irq(bq);
	if (ret) {
#line 2504
		mca_log_err("%s failed to int irq\n", bq->log_tag);
		return ret;
	}

	ret = sc8581_init_device(bq);
	if (ret) {
#line 2510
		mca_log_err("%s failed to init sc8581\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IIC_ERROR, NULL);
		return ret;
	}

	schedule_delayed_work(&bq->irq_handle_work, 0);
	bq->chip_ok = true;
	bq->ovpgate_en = true;
	bq->i2c_is_working = true;
#ifdef CONFIG_DEBUG_FS
	reg_info.address = SC8581_REG_00;
	reg_info.count = 1;

	if (bq->cp_role == SC8581_SLAVE)
		mca_debugfs_create_group("sc85xx_01", cp_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE, bq);
	else
		mca_debugfs_create_group("sc85xx_00", cp_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE, bq);
#endif
	platform_class_cp_register_ops(bq->cp_role, &sc8581_chg_ops, bq);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_CP_MASTER_IC,
		&g_sc8581_log_ops, bq);
	device_init_wakeup(bq->dev, true);
#line 2532
	mca_log_err("%s probe success %d\n", bq->log_tag, ret);

	return 0;
}

static int sc8581_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8581_device *bq = i2c_get_clientdata(client);
	int ret = 0;

#line 2543
	mca_log_info("%s sc8581 suspend!\n", bq->log_tag);
	ret = cp_enable_adc(bq, false);
	if (ret)
		mca_log_err("%s failed to disable ADC\n", bq->log_tag);
	bq->i2c_is_working = false;

	return ret;
}

static int sc8581_resume(struct device *dev)
{
	return 0;
}

static void sc8581_i2c_complete(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8581_device *bq = NULL;

	if (!client)
		return;

	bq = i2c_get_clientdata(client);
	if (!bq)
		return;

	bq->i2c_is_working = true;
#line 2570
	mca_log_info("%s sc8581 i2c complete!\n", bq->log_tag);
}

static const struct dev_pm_ops sc8581_pm_ops = {
	.suspend	= sc8581_suspend,
	.resume		= sc8581_resume,
	.complete 	= sc8581_i2c_complete,
};

static void sc8581_remove(struct i2c_client *client)
{
	struct sc8581_device *bq = i2c_get_clientdata(client);
	int ret = 0;

	ret = cp_enable_adc(bq, false);
	if (ret)
		mca_log_err("%s failed to disable ADC\n", bq->log_tag);
}

static void sc8581_shutdown(struct i2c_client *client)
{
	struct sc8581_device *bq = i2c_get_clientdata(client);
	int ret = 0;

	ret = cp_enable_adc(bq, false);
	ret |= gpio_direction_output(bq->nlpm_gpio, 0);
	if (ret)
#line 2598
		mca_log_err("%s unable to reset adc and nlpm fail\n", bq->log_tag);

#line 2600
	mca_log_info("%s sc8581 shutdown!\n", bq->log_tag);
}

static const struct of_device_id sc8581_of_match[] = {
	{ .compatible = "sc8581"},
	{ .compatible = "sc8585_master"},
	{ .compatible = "sc8585_slave"},
	{},
};
MODULE_DEVICE_TABLE(of, sc8581_of_match);

static struct i2c_driver sc8581_driver = {
	.driver = {
		.name = "sc8581_charger_pump",
		.of_match_table = sc8581_of_match,
		.pm = &sc8581_pm_ops,
	},
	.probe = sc8581_probe,
	.remove = sc8581_remove,
	.shutdown = sc8581_shutdown,
};
module_i2c_driver(sc8581_driver);

MODULE_AUTHOR("lvxiaofeng <lvxiaofeng@xiaomi.com>");
MODULE_DESCRIPTION("southchip SC8581 driver");
MODULE_LICENSE("GPL v2");
