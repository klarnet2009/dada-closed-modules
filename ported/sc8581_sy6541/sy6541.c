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
#include "inc/sy6541.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <linux/version.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <linux/pm_wakeup.h>
#include <mca/common/mca_hwid.h>
/* hwid.h */

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_sy6541"
#endif

#define MAX_LENGTH_BYTE 600
#define MAX_REG_COUNT 0x42
#define MCA_SET_OVPGATE_COUNT 10
static int cp_get_adc_data(struct sc8581_device *bq, int channel,  u32 *result);
static int sy6541_init_device(struct sc8581_device *cp);
/********i2c basic read/write interface***********/
static int cp_read_word(struct i2c_client *client, u8 reg, u16 *val)
{
	s32 ret;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
#line 46
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
#line 75
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
#line 136
		mca_log_err("null pointer read\n");
		return -EINVAL;
	}

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, buf);
	if (ret < 0) {
#line 142
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
#line 186
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
#line 221
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

struct mca_debugfs_attr_info cp_sy6541_debugfs_field_tbl[] = {
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_ADDRESS, address),
	mca_debugfs_attr(cp_debugfs, 0664, CP_DEBUG_PROP_COUNT, count),
	mca_debugfs_attr(cp_debugfs, 0600, CP_DEBUG_PROP_DATA, data),
};

#define CP_DEBUGFS_ATTRS_SIZE			ARRAY_SIZE(cp_sy6541_debugfs_field_tbl)

#endif
/*******end debugfs******************************/

static int sy6541_init_protection(struct sc8581_device *cp, int forward_work_mode);
/* SC8581_ADC */
#ifdef DEBUG_CODE
static int sy6541_check_adc_enabled(struct sc8581_device *bq, bool *enabled)
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

static int sy6541_enable_adc(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;
	//bool enabled = false;

	val = enable ? SC8581_ADC_ENABLE : SC8581_ADC_DISABLE;
	val <<= SC8581_ADC_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_15,
		SC8581_ADC_EN_MASK, val);
	//sy6541_check_adc_enabled(bq, &enabled);

	return ret;
}

/* SC8581_CHG_EN */
static noinline int sy6541_enable_charge(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;
	u8 chip_flag = 0;

	ret = cp_read_byte(bq->client, 0xF6, &chip_flag);
	if (ret)
		return ret;
	if (chip_flag != 0x20) {
#line 300
		mca_log_err("sy6541_enable_charge: val[%02x] is not 0x20\n", bq->log_tag, chip_flag);
		sy6541_init_device(bq);
		return 0;
	}

	val = enable ? SC8581_CHG_ENABLE : SC8581_CHG_DISABLE;
	val <<= SC8581_CHG_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
				SC8581_CHG_EN_MASK, val);
#line 308
	mca_log_info("%s enable cp: %d\n", bq->log_tag, enable);

	return 0;
}

static int sy6541_check_charge_enabled(struct sc8581_device *bq, bool *enabled)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_0A, &val);
	if (!ret)
		*enabled = !!(val & 0x02);

	return ret;
}

static void sy6541_enable_qb(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = enable ? SC8581_QB_ENABLE : SC8581_QB_DISABLE;
	val <<= SC8581_QB_EN_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0B,
				SC8581_QB_EN_MASK, val);
}

static int sy6541_disable_rcp(struct sc8581_device *bq, bool enable)
{
	int ret = 0;
	u8 val;

	val = !!enable;
	val <<= SC8581_IBUS_RCP_DIS_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_25,
				SC8581_IBUS_RCP_DIS_MASK, val);
#line 335
	mca_log_info("%s disable rcp: %d\n", bq->log_tag, val);

	return ret;
}



/* SC8581_OVPGATE_ON_DG_SET */
/* SC8581_BUS_OVP */
#define RANGE_MIN_MAX 2
static int sy6541_set_busovp_th(struct sc8581_device *bq, int threshold)
{
	int ret = 0;
	u8 val;
	int ovp_th_range[CP_MODE_DIV_MAX][RANGE_MIN_MAX];
	int base[CP_MODE_DIV_MAX];
	int lsb[CP_MODE_DIV_MAX];

	ovp_th_range[CP_MODE_DIV4][0] = 15000;
	ovp_th_range[CP_MODE_DIV4][1] = 27000;
	ovp_th_range[CP_MODE_DIV2][0] = 7500;
	ovp_th_range[CP_MODE_DIV2][1] = 13500;
	ovp_th_range[CP_MODE_DIV1][0] = 3750;
	ovp_th_range[CP_MODE_DIV1][1] = 6750;
#line 435
	mca_log_info("%s threshold= %d, min = %d, max =%d\n",
		bq->log_tag, threshold, ovp_th_range[bq->work_mode][0],
		ovp_th_range[bq->work_mode][1]);
	if (threshold < ovp_th_range[bq->work_mode][0])
		threshold = ovp_th_range[bq->work_mode][0];
	else if (threshold > ovp_th_range[bq->work_mode][1])
		threshold = ovp_th_range[bq->work_mode][1];

	base[CP_MODE_DIV4] = 15000;
	lsb[CP_MODE_DIV4] = 800;
	base[CP_MODE_DIV2] = 7500;
	lsb[CP_MODE_DIV2] = 400;
	base[CP_MODE_DIV1] = 3750;
	lsb[CP_MODE_DIV1] = 200;

	val = (threshold - base[bq->work_mode]) / lsb[bq->work_mode];

#line 453
	mca_log_info("%s bus_ovpth= %d, val = %d\n", bq->log_tag, threshold, val);

	val <<= 2;
	ret = cp_update_bits(bq->client, 0x05, 0xfc, val);

	return ret;
}

/* SC8581_BUS_UCP */
static void sy6541_enable_busucp(struct sc8581_device *bq, bool enable)
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


static void sy6541_set_key(struct sc8581_device *bq, bool enable)
{
	cp_update_bits(bq->client, 0x28, 0x03, enable ? 0x03 : 0x01);
}



#ifdef DEBUG_CODE
static int sy6541_set_busocp_alarm_th(struct sc8581_device *bq, int threshold)
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
#define SC8581_ADC_REG_BASE SC8581_REG_17
static int sy6541_get_adc_data(struct sc8581_device *bq, int channel, u32 *result)
{
	int ret = 0;
	u16 val;
	u16 tmp;
	u8 temp_adc;

	cp_read_byte(bq->client, SC8581_REG_15, &temp_adc);
#line 553
	mca_log_info("SY6541_REG_15 = %d", temp_adc);
	cp_read_byte(bq->client, SC8581_REG_16, &temp_adc);
#line 555
	mca_log_info("SY6541_REG_16 = %d", temp_adc);

	ret = cp_read_word(bq->client, SC8581_ADC_REG_BASE + (channel << 1), &tmp);
	if (ret < 0) {
#line 559
		mca_log_info("adc read error");
		return ret;
	}
#line 562
	mca_log_info("adc channel = %d, val = 0x%04x", channel, tmp);
	val = swab16(tmp);

	/* the stock sy6541 switch has 9 slots (the sc8581 channel numbering), 6 and 7 are not scaled */
	switch (channel) {
	case 0:
		val = val * 732 / 1000;
		break;
	case 1:
	case 2:
	case 3:
		val = val * 11718 / 10000;
		break;
	case 4:
	case 5:
		val = val * 22 / 100;
		break;
	case 8:
		val = val * 495 / 1000 - 40;
		break;
	default:
		break;
	}

	*result = val;
#line 591
	mca_log_debug("%s channel %d adc %x, result: %d\n", bq->log_tag, channel, tmp, *result);
	return ret;
}

/* SC8581_SYNC_FUNCTION_EN */
#ifdef DEBUG_CODE
/* SC8581_SYNC_MASTER_EN */
static int sy6541_enable_config_func(struct sc8581_device *bq, bool enable)
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
/* SC8581_MODE */
static int sy6541_set_operation_mode(struct sc8581_device *bq, int operation_mode)
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
#line 665
		mca_log_info("%s operation mode error%d\n", bq->log_tag, operation_mode);
		return -1;
	break;
	}
	bq->operation_mode = val;

	ret = sy6541_init_protection(bq, bq->work_mode);
#line 673
	mca_log_info("%s set operation mode %d reg %d work_mode %d\n", bq->log_tag, operation_mode, val, bq->work_mode);
	val <<= SC8581_MODE_SHIFT;
	cp_update_bits(bq->client, SC8581_REG_0E,
				SC8581_MODE_MASK, val);

	return 0;
}

static int sy6541_get_operation_mode(struct sc8581_device *bq, int *operation_mode)
{
	int ret = 0;
	u8 val;

	ret = cp_read_byte(bq->client, SC8581_REG_0E, &val);
	if (ret) {
#line 687
		mca_log_info("%s get operation mode fail\n", bq->log_tag);
		return ret;
	}

	*operation_mode = (val & SC8581_MODE_MASK);

	return ret;
}

static int sy6541_get_int_stat(struct sc8581_device *bq, int channel, bool *enable)
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
static int sy6541_enable_acdrv_manual(struct sc8581_device *bq, bool enable)
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
static int sy6541_enable_wpcgate(struct sc8581_device *bq, bool enable)
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

static int sy6541_enable_ovpgate(struct sc8581_device *bq, bool enable)
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
#line 777
		mca_log_info("%s count %d, ovpgate_en %d, penable %d\n",
			bq->log_tag, i, bq->ovpgate_en, penable);

		ret = cp_update_bits(bq->client, SC8581_REG_0B,
			SC8581_OVPGATE_EN_MASK, val);
		msleep(10);
	}
#line 783
	mca_log_err("%s enable %d\n", bq->log_tag, enable);
	return ret;
}

static int sy6541_get_ovpgate_status(struct sc8581_device *bq, bool *enable)
{
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0F, &val);
#line 859
	mca_log_info("%s ovpgate_status SY6541_REG_0F=0x%x\n", bq->log_tag, val);
	if (ret) {
#line 861
		mca_log_info("%s get ovpgate status fail\n", bq->log_tag);
		return ret;
	}

	*enable = !!(val & SC8581_OVPGATE_STAT_MASK);
	return ret;
}

#define MANUAL_GATE_MASK 0x38
/* SC8581_IBAT_SNS */
/* SC8581_SS_TIMEOUT */
/* SC8581_WD_TIMEOUT_SET */
#ifdef DEBUG_CODE
static int sy6541_set_wdt(struct sc8581_device *bq, int ms)
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
static int sy6541_set_switch_freq(struct sc8581_device *bq, u8 data)
{
	int ret = 0;
	u8 val;

	val = data;
	val <<= SC8581_FSW_SET_SHIFT;
	ret = cp_update_bits(bq->client, SC8581_REG_0C, 0x78, val);

	return ret;
}

static int sy6541_set_fsw(struct sc8581_device *cp, int fsw)
{
	int ret;
	int fsw_min = cp->fsw_cfg.min;
	int fsw_max = cp->fsw_cfg.max;
	int step_val = cp->fsw_cfg.step;
	int val;

	fsw = fsw > fsw_max ? fsw_max : (fsw < fsw_min ? fsw_min : fsw);
	val = (fsw - fsw_min) / step_val;

#line 954
	mca_log_info("%s fsw: %d, val: %d\n", cp->log_tag, fsw, val);
	sy6541_set_key(cp, true);
	ret = sy6541_set_switch_freq(cp, val);
	sy6541_set_key(cp, false);

	return ret;
}

static int sy6541_get_fsw(struct sc8581_device *cp, int *fsw)
{
	int ret;
	u8 byte;
	int val;

	ret = cp_read_byte(cp->client, SC8581_REG_0C, &byte);
	val = (byte & SC8585_FSW_SET_MASK) >> SC8585_FSW_SET_SHIFT;

	*fsw = cp->fsw_cfg.min + val * cp->fsw_cfg.step;
#line 969
	mca_log_info("%s byte: %02x, val: %d, fsw: %d\n", cp->log_tag, byte, val, *fsw);

	return ret;
}

static int sy6541_get_tdie(struct sc8581_device *cp, int *tdie)
{
	int ret = 0;
	int val = 0;
	u8 byte_h = 0;
	u8 byte_l = 0;

	ret = cp_read_byte(cp->client, 0x23, &byte_h);
	ret |= cp_read_byte(cp->client, 0x24, &byte_l);
	val = (byte_h & SC8581_TDIE_POL_H_MASK << 8) | (byte_l & SC8581_TDIE_POL_L_MASK);

	*tdie = val * 495 / 1000;
#line 986
	mca_log_info("%s byte_h: %02x, byte_l: %02x, val: %d, tdie: %d\n",
		cp->log_tag, byte_h, byte_l, val, *tdie);

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
static unsigned int sy6541_reg_list[] = {
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
#define CP_MAX_REG_NUM			ARRAY_SIZE(sy6541_reg_list)
*/

static void sy6541_abnormal_charging_judge(struct sc8581_device *bq, u8 *data)
{
	int val[2] = {0};
	static int cp_cboot_short = 0;

	if ((data[INT_STAT_REG] & 0x08) == 0) {
#line 1089
		mca_log_info("VOUT UVLO\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VOUT_UVLO, NULL);
	}

	if ((data[INT_STAT_REG] & 0x3F) != 0x3F)
#line 1093
		mca_log_info("%s VIN have problem\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x08)
#line 1095
		mca_log_info("%s VBUS_ERRORHI_STAT\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x10)
#line 1097
		mca_log_info("%sVBUS_ERRORLO_STAT\n", bq->log_tag);

	if (data[CONVERTER_STATE_REG] & 0x01) {
#line 1099
		mca_log_info("%s CBOOT SHORT/OPEN 111\n", bq->log_tag);
		if (!cp_cboot_short) {
                	cp_cboot_short = 1;
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_CBOOT_FAIL, &cp_cboot_short);
                }
	} else if (cp_cboot_short) {
#line 1105
		mca_log_info("%s CBOOT SHORT/OPEN 000\n", bq->log_tag);
		cp_cboot_short = 0;
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_CBOOT_FAIL, &cp_cboot_short);
	}

	if (data[VBAT_OVP_REG] & 0x20) {
#line 1110
		mca_log_info("%s VBAT_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VBAT_OVP, NULL);
	}

	if (data[VUSB_OVP_REG] & 0x20) {
#line 1114
		mca_log_info("%s VUSB_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_OVP, NULL);
	}

	if (data[VWPC_OVP_REG] & 0x20) {
#line 1118
		mca_log_info("%s VWPC_OVP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VWPC_OVP, NULL);
	}

	if (data[IBUS_OCP_REG] & 0x20) {
#line 1122
		mca_log_info("%s IBUS_OCP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IBUS_OCP, NULL);
	}

	if (data[IBUS_UCP_REG] & 0x01) {
#line 1126
		mca_log_info("%s IBUS_UCP\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_IBUS_UCP, NULL);
	}

	if (data[CONVERTER_STATE_REG] & 0x80) {
#line 1130
		mca_log_info("%s POR_FLAG\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_POR_FLAG, NULL);
	}

	if (data[FLT_FLAG_REG] & SC8581_TSHUT_FLAG_MASK) {
#line 1134
		mca_log_info("%s TSHUT\n", bq->log_tag);
		cp_get_adc_data(bq, ADC_TDIE, &val[0]);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_TSHUT_FLAG, val);
	}

	if (data[FLT_FLAG_REG] & SC8581_VBUS_OVP_FLAG_MASK) {
#line 1139
		mca_log_info("%s vbus ovp\n", bq->log_tag);
		mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VBUS_OVP, NULL);
	}
}

#define CP_DUMP_REG_LEN			16
static int sy6541_dump_important_regs(struct sc8581_device *bq, union cp_propval *val)
{
	int ret = 0;
	u8 data[CP_DUMP_REG_LEN] = { 0 };
	u8 reg[CP_REG_MAX] = { 0 };
	u8 base_addr;
	int i = 0;

	base_addr = SC8581_REG_00;
	ret = cp_read_i2c_block_data(bq->client, base_addr, CP_DUMP_REG_LEN, data);
	if (ret < 0) {
#line 1156
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		memcpy(reg, data + 1, 15);
		for (i = 0; i < CP_DUMP_REG_LEN; i += 8) {
#line 1160
			mca_log_info("%s: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, "
				"[0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X",
				bq->log_tag, base_addr + i, data[i], base_addr + i + 1, data[i + 1],
				base_addr + i + 2, data[i + 2], base_addr + i + 3, data[i + 3],
				base_addr + i + 4, data[i + 4], base_addr + i + 5, data[i + 5],
				base_addr + i + 6, data[i + 6], base_addr + i + 7, data[i + 7]);
		}
	}

	if (!!(data[11] & SC8581_OVPGATE_EN_MASK) != bq->ovpgate_en) {
#line 1170
		mca_log_info("set cp ovpgate not effective, repeat set ovpgate\n");
		sy6541_enable_ovpgate(bq, bq->ovpgate_en);
	}

	base_addr = SC8581_REG_10;
	ret = cp_read_i2c_block_data(bq->client, base_addr, CP_DUMP_REG_LEN, data);
	if (ret < 0) {
#line 1177
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		reg[INT_STAT_REG] = data[SC8581_REG_10 - base_addr];
		reg[INT_FLAG_REG] = data[SC8581_REG_11 - base_addr];
		reg[FLT_FLAG_REG] = data[SC8581_REG_13 - base_addr];
		for (i = 0; i < CP_DUMP_REG_LEN; i += 8) {
#line 1183
			mca_log_info("%s: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, "
				"[0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X",
				bq->log_tag, base_addr + i, data[i], base_addr + i + 1, data[i + 1],
				base_addr + i + 2, data[i + 2], base_addr + i + 3, data[i + 3],
				base_addr + i + 4, data[i + 4], base_addr + i + 5, data[i + 5],
				base_addr + i + 6, data[i + 6], base_addr + i + 7, data[i + 7]);
		}
	}

	base_addr = SC8581_REG_6E;
	ret = cp_read_i2c_block_data(bq->client, base_addr, 1, data);
	if (ret < 0) {
#line 1195
		mca_log_err("dump registers failed, base_addr: 0x%02X\n", base_addr);
	} else {
		reg[DEVICE_ID_REG] = data[0];
#line 1198
		mca_log_info("%s: [0x%02X]=0x%02X", bq->log_tag, base_addr, data[0]);
	}

	// CONVERTER STATE Register (Address=0Ah)
	if ((reg[CONVERTER_STATE_REG] & SC8581_CP_SWITCHING_STAT_MASK) == 0) {
#line 1203
		mca_log_info("%s cp switching stop, enter abnormal charging judge\n", bq->log_tag);
		sy6541_abnormal_charging_judge(bq, reg);
	}

	return 0;
}

static int sy6541_enable_batovp(struct sc8581_device *cp, bool enable)
{
	u8 val = enable ? 0 : 1;

	val <<= 7;
	return cp_update_bits(cp->client, 0x01, 0x80, val);
}

static int sy6541_enable_busocp(struct sc8581_device *cp, bool enable)
{
	u8 val = enable ? 0 : 1;

	val <<= 7;
	return cp_update_bits(cp->client, 0x06, 0x80, val);
}

static int sy6541_enable_pmid2out_a(struct sc8581_device *cp, bool enable)
{
	u8 val = enable ? 0 : 3;

	return cp_update_bits(cp->client, 0x02, 0x03, val);
}

static int sy6541_enable_pmid2out_b(struct sc8581_device *cp, bool enable)
{
	u8 val = enable ? 4 : 0;

	return cp_update_bits(cp->client, 0x02, 0x0c, val);
}

static int sy6541_set_batovp_th(struct sc8581_device *cp, int threshold)
{
	u8 val;

	if (threshold < 4450)
		threshold = 4450;
	val = (threshold - 4450) / 25;
	return cp_update_bits(cp->client, 0x01, 0x1f, val);
}

static int sy6541_set_usbovp_th(struct sc8581_device *cp, int threshold)
{
	u8 val;

	if (threshold == 7500)
		val = 0xf;
	else
		val = (threshold - 11000) / 1000;
	return cp_update_bits(cp->client, 0x03, 0x0f, val);
}

static int sy6541_set_busocp_th(struct sc8581_device *cp, int threshold)
{
	u8 val;

	if (threshold < 2100)
		threshold = 2100;
	val = (threshold - 2100) / 150;
	return cp_update_bits(cp->client, 0x06, 0x1f, val);
}

static int sy6541_set_wpcovp_th(struct sc8581_device *cp, int threshold)
{
	u8 val;

	if (threshold == 7500)
		val = 0xf;
	else
		val = (threshold - 11000) / 1000;
	return cp_update_bits(cp->client, 0x04, 0x0f, val);
}

static int sy6541_set_outovp_th(struct sc8581_device *cp, int threshold)
{
	u8 val;

	if (threshold < 4800)
		threshold = 4800;
	val = (threshold - 4800) / 200;
	return cp_update_bits(cp->client, 0x05, 0x03, val);
}

static int sy6541_set_f3_bit(struct sc8581_device *cp, int mode)
{
	switch (mode) {
	case 1:
	case 2:
		return cp_update_bits(cp->client, 0xf3, 0x08, 0x08);
	case 0:
		return cp_update_bits(cp->client, 0xf3, 0x08, 0x00);
	default:
		return 0;
	}
}

static int sy6541_init_protection(struct sc8581_device *cp, int work_mode)
{
	int ret = 0;

	ret = sy6541_enable_batovp(cp, true);
	ret = sy6541_enable_busocp(cp, true);
	sy6541_enable_busucp(cp, true);
	ret = sy6541_enable_pmid2out_a(cp, true);
	ret = sy6541_enable_pmid2out_b(cp, true);
	ret = sy6541_set_batovp_th(cp, cp->cfg.bat_ovp_th);
	ret = sy6541_set_busovp_th(cp, cp->cfg.bus_ovp_th[work_mode]);
	ret = sy6541_set_usbovp_th(cp, cp->cfg.usb_ovp_th[work_mode]);
	ret = sy6541_set_busocp_th(cp, cp->cfg.bus_ocp_th[work_mode]);
	ret = sy6541_set_wpcovp_th(cp, cp->cfg.wpc_ovp_th);
	ret = sy6541_set_outovp_th(cp, cp->cfg.out_ovp_th);
	ret = sy6541_set_f3_bit(cp, cp->operation_mode);

	return ret;
}

#define SY_FLAG_FN(name, reg, mask) static __maybe_unused int name(struct sc8581_device *cp, bool en) { 	u8 val = en ? (mask) : 0; 	return cp_update_bits(cp->client, reg, mask, val); }

SY_FLAG_FN(sy_f_0e08, 0x0e, 0x08)
SY_FLAG_FN(sy_f_0380, 0x03, 0x80)
SY_FLAG_FN(sy_f_1540, 0x15, 0x40)
SY_FLAG_FN(sy_f_1501, 0x15, 0x01)
SY_FLAG_FN(sy_f_1680, 0x16, 0x80)
SY_FLAG_FN(sy_f_1640, 0x16, 0x40)
SY_FLAG_FN(sy_f_1620, 0x16, 0x20)
SY_FLAG_FN(sy_f_1610, 0x16, 0x10)
SY_FLAG_FN(sy_f_1608, 0x16, 0x08)
SY_FLAG_FN(sy_f_1601, 0x16, 0x01)
SY_FLAG_FN(sy_f_1580, 0x15, 0x80)
SY_FLAG_FN(sy_f_f620, 0xf6, 0x20)

static int sy_f_0d38(struct sc8581_device *cp, int v)
{
	return cp_update_bits(cp->client, 0x0d, 0x38, v << 3);
}

static int sy_f_0730(struct sc8581_device *cp, int v)
{
	return cp_update_bits(cp->client, 0x07, 0x30, v << 4);
}


static int sy6541_init_device(struct sc8581_device *cp)
{
	int ret = 0;

	ret |= sy_f_0e08(cp, true);
	ret |= sy_f_0d38(cp, 5);
	ret |= sy_f_0730(cp, 1);
	ret |= sy_f_0380(cp, true);
	ret |= sy_f_1540(cp, false);
	ret |= cp_update_bits(cp->client, 0x15, 0x01, 0x00);
	ret |= cp_update_bits(cp->client, 0x16, 0x80, 0x00);
	ret |= sy_f_1640(cp, false);
	ret |= sy_f_1620(cp, false);
	ret |= sy_f_1610(cp, false);
	ret |= sy_f_1608(cp, false);
	ret |= sy_f_1601(cp, false);
	ret |= cp_update_bits(cp->client, 0x15, 0x80, 0x00);
	sy6541_set_operation_mode(cp, 1);
	cp->fsw_cfg.max = 1050;
	cp->fsw_cfg.min = 300;
	cp->fsw_cfg.step = 50;
	sy6541_set_fsw(cp, CP_DEFAULT_FSW);
	ret |= sy_f_f620(cp, true);
#line 1270
	mca_log_err("%s success to init CP device\n", cp->log_tag);

	return ret;
}



static int cp_get_adc_data(struct sc8581_device *bq, int channel,  u32 *result)
{
	int ret = 0;

	ret = sy6541_get_adc_data(bq, channel, result);

	if (ret)
#line 1284
		mca_log_info(" %s failed get ADC value\n", bq->log_tag);


	return ret;
}

static int ops_cp_get_int_stat(int channel, bool *result, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_get_int_stat(bq, channel, result);

	if (ret)
#line 1295
		mca_log_info("failed get int stat %d\n", channel);

	return ret;
}

#ifdef __GENKSYMS__
__attribute__((fcrc8ArrESl402))
#endif
noinline int sy_cp_enable_adc(struct sc8581_device *bq, bool enable)
{
	sy6541_enable_adc(bq, enable);

	return 0;
}
EXPORT_SYMBOL(sy_cp_enable_adc);

static int ops_cp_dump_register(void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_dump_important_regs(bq, NULL);

	if (ret)
		mca_log_info("%s failed dump registers ret=%d\n", bq->log_tag, ret);

	return ret;
}

static int ops_cp_get_chip_vendor(int *chip_id, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	*chip_id = bq->chip_vendor;
#line 1336
	mca_log_info("%s %d\n", bq->log_tag, *chip_id);

	return 0;
}

static int ops_cp_enable_charge(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret;

	ret = sy6541_enable_charge(bq, enable);
	if (ret)
#line 1347
		mca_log_err("%s failed enable cp charge\n", bq->log_tag);
	return ret;
}

static int ops_cp_get_charge_enable(bool *enabled, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_check_charge_enabled(bq, enabled);

	if (ret)
#line 1358
		mca_log_err("%s failed get enable cp charge status ret =%d\n", bq->log_tag, ret);


	return ret;
}



static int ops_cp_set_rcp(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_disable_rcp(bq, enable);

	if (ret)
		mca_log_err("%s failed enable cp rcp\n", bq->log_tag);

	return ret;
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

static int ops_cp_set_mode(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_set_operation_mode(bq, value);

	if (ret)
#line 1425
		mca_log_err("%s failed set cp charge mode\n", bq->log_tag);

	return ret;
}

static int ops_cp_get_mode(int *mode, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_get_operation_mode(bq, mode);
	if (ret)
#line 1436
		mca_log_err("%s failed to get div_mode\n", bq->log_tag);

	return ret;
}

static int ops_cp_device_init(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_init_device(bq);

	if (ret)
		mca_log_err("%s failed init cp init device\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_adc(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sy6541_enable_adc(bq, enable);

	return 0;
}

static int ops_cp_get_bypass_support(bool *enabled, void *chg_dev)
{
	*enabled = true;
#line 1463
	mca_log_info("%s %d\n", __func__, *enabled);

	return 0;
}

static int ops_enable_acdrv_manual(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_enable_acdrv_manual(bq, enable);

	if (ret)
		mca_log_info("%s failed enable cp acdrv manual\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_wpcgate(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_enable_wpcgate(bq, enable);
	if (ret)
		mca_log_info("%s failed enable cp wpcgate\n", bq->log_tag);

	return ret;
}

static int ops_cp_enable_ovpgate(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_enable_ovpgate(bq, enable);

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

		ret = sy6541_enable_ovpgate(bq, en);
		if (ret)
			mca_log_info("%s failed enable cp ovpgate with check\n", bq->log_tag);
	}
	return ret;
}

static int ops_cp_get_ovpgate_status(bool *enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_get_ovpgate_status(bq, enable);

	if (ret)
#line 1529
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

static int ops_cp_get_battery_present(bool *present, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;

	ret = sy6541_get_int_stat(bq, VOUT_INSERT_STAT, present);

	return ret;
}

static int ops_cp_get_errorhl_stat(int *stat, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0A, &val);
	if (ret) {
#line 1589
		mca_log_err("%s get pmid error stat fail\n", bq->log_tag);
		return ret;
	}

	*stat = CP_PMID_ERROR_OK;
	if (val & SC8581_VBUS_ERRORLO_STAT_MASK)
		*stat = CP_PMID_ERROR_LOW;
	else if (val & SC8581_VBUS_ERRORHI_STAT_MASK)
		*stat = CP_PMID_ERROR_HIGH;

#line 1598
	mca_log_info("%s val: 0x%02x, stat: %d", bq->log_tag, val, *stat);
	return ret;
}

static int ops_cp_enable_busucp(bool enable, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	sy6541_enable_busucp(bq, enable);
	return 0;
}

static int ops_cp_set_fsw(int fsw, void *chg_dev)
{
	return sy6541_set_fsw((struct sc8581_device *)chg_dev, fsw);
}

static int ops_cp_set_default_fsw(void *chg_dev)
{
	return sy6541_set_fsw((struct sc8581_device *)chg_dev, CP_DEFAULT_FSW);
}

static int ops_cp_get_fsw(int *fsw, void *chg_dev)
{
	return sy6541_get_fsw((struct sc8581_device *)chg_dev, fsw);
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
	return sy6541_get_tdie((struct sc8581_device *)chg_dev, tdie);
}

static int sy6541_get_ovpgate_enable(struct sc8581_device *bq, bool *enable)
{
	int ret = 0;
	u8 val = 0;

	ret = cp_read_byte(bq->client, SC8581_REG_0B, &val);
#line 794
	mca_log_info("%s ovpgate_status SY6541_REG_0B=0x%x\n", bq->log_tag, val);
	if (ret) {
#line 796
		mca_log_info("%s get ovpgate enable fail\n", bq->log_tag);
		return ret;
	}

	*enable = !!(val & SC8581_OVPGATE_EN_MASK);
	return ret;
}

static int sy6541_set_revchg(struct sc8581_device *bq, bool en)
{
	int mode = 0;
	bool ovpgate = false;
	bool charging = false;

	if (en) {
		sy6541_enable_qb(bq, true);
		sy6541_set_operation_mode(bq, SC8581_REVERSE_1_2_CONVERTER_MODE);
		sy6541_enable_ovpgate(bq, true);
		sy6541_enable_acdrv_manual(bq, true);
		sy6541_get_ovpgate_enable(bq, &ovpgate);
		sy6541_get_operation_mode(bq, &mode);
#line 835
		mca_log_info("cp mode: %d, ovpgate_enable status: %d", mode, ovpgate);
		if (ovpgate && mode == SC8581_REVERSE_1_2_CONVERTER_MODE)
			return 0;
	} else {
		sy6541_enable_qb(bq, false);
		sy6541_enable_charge(bq, false);
		sy6541_enable_acdrv_manual(bq, false);
		sy6541_set_operation_mode(bq, SC8581_FORWARD_2_1_CHARGER_MODE);
		sy6541_get_operation_mode(bq, &mode);
		sy6541_check_charge_enabled(bq, &charging);
#line 845
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
		if (!sy6541_set_revchg(bq, en))
			return 0;
#line 1544
		mca_log_info("%s failed set revchg, retry: %d\n", bq->log_tag, retry);
		mdelay(20);
	}

	return -1;
}

static int ops_cp_set_adjustadble_timeout(int value, void *chg_dev)
{
	struct sc8581_device *bq = (struct sc8581_device *)chg_dev;

	cp_update_bits(bq->client, SC8581_REG_0D, 0x3F, value);
	return 0;
}

static struct platform_class_cp_ops sy6541_chg_ops = {
	.cp_set_enable = ops_cp_enable_charge,
	.cp_get_enabled = ops_cp_get_charge_enable,
	.cp_get_bus_voltage = ops_cp_get_vbus,
	.cp_get_bus_current = ops_cp_get_ibus,
	.cp_get_battery_voltage = ops_cp_get_vbatt,
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
	.cp_set_rcp = ops_cp_set_rcp,
	.cp_set_revchg = ops_cp_set_revchg,
	.cp_set_adjustadble_timeout = ops_cp_set_adjustadble_timeout,
};

static void sy6541_irq_handler(struct work_struct *work)
{
	struct sc8581_device *bq = container_of(work, struct sc8581_device, irq_handle_work.work);
	bool usb_present;
	static bool last_usb_present;

	sy6541_get_int_stat(bq, VUSB_PRESENT_STAT, &usb_present);
	if (usb_present != last_usb_present) {
		if (usb_present)
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_INSERT, NULL);
		else
			mca_event_block_notify(MCA_EVENT_TYPE_CP_INFO, MCA_EVENT_CP_VUSB_OUT, NULL);
		last_usb_present = usb_present;
	}

#line 1697
	mca_log_info("%s handler\n", bq->log_tag);
	if (bq->i2c_is_working)
		sy6541_dump_important_regs(bq, NULL);
}

static irqreturn_t sy6541_int_isr(int irq, void *private)
{
	struct sc8581_device *bq = private;

#line 1706
	mca_log_info("%s %s\n", bq->log_tag, __func__);

	pm_wakeup_dev_event(bq->dev, 500, true);
	schedule_delayed_work(&bq->irq_handle_work, 0);

	return IRQ_HANDLED;
}



static int sy6541_register_irq(struct sc8581_device *bq)
{
	int ret = 0;

	ret = devm_gpio_request(bq->dev, bq->irq_gpio, dev_name(bq->dev));
	if (ret < 0) {
#line 1759
		mca_log_info(" %s failed to master request gpio\n", bq->log_tag);
		return -1;
	}

	bq->irq = gpio_to_irq(bq->irq_gpio);
	if (bq->irq_gpio < 0) {
#line 1765
		mca_log_info("%s failed to master get gpio_irq\n", bq->log_tag);
		return -1;
	}

	ret = request_irq(bq->irq, sy6541_int_isr,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		dev_name(bq->dev), bq);
	if (ret < 0) {
#line 1773
		mca_log_info("%s failed to master request irq\n", bq->log_tag);
		return -1;
	}

	enable_irq_wake(bq->irq);

	return 0;
}



static int sy6541_dump_log_head(void *data, char *buf, int size)
{
	struct sc8581_device *bq = (struct sc8581_device *)data;

	if (!data)
		return 0;

	if (bq->cp_role == 0)
		return snprintf(buf, size, "cp_vusb cp_vbus cp_ibus cp_ibat cp_vbat cp_vout ");
	else
		return snprintf(buf, size, "cp_vusb1 cp_vbus1 cp_ibus1 cp_ibat1 cp_vbat1 cp_vout1 ");
}

static int sy6541_dump_log_context(void *data, char *buf, int size)
{
	struct sc8581_device *bq = (struct sc8581_device *)data;
	unsigned int vbus, vusb, ibus, ibat, vbat;

	if (!data)
		return snprintf(buf, size, "%-8d%-8d%-8d%-8d%-8d", -1, -1, -1, -1, -1);

	(void)ops_cp_get_vbus(&vbus, data);
	(void)ops_cp_get_vusb(&vusb, data);
	(void)ops_cp_get_ibus(&ibus, data);
	(void)cp_get_adc_data(bq, ADC_VBAT, &ibat);
	(void)cp_get_adc_data(bq, ADC_VOUT, &vbat);

	if (bq->cp_role == 0)
		return snprintf(buf, size, "%-8u%-8u%-8u%-8u%-8u",
			vusb, vbus, ibus, ibat, vbat);
	else
		return snprintf(buf, size, "%-9u%-9u%-9u%-9u%-9u",
			vusb, vbus, ibus, ibat, vbat);
}

static struct mca_log_charge_log_ops g_sy6541_log_ops = {
	.dump_log_head = sy6541_dump_log_head,
	.dump_log_context = sy6541_dump_log_context,
};




int is_sy6541_cp(struct sc8581_device *bq)
{
	int ret = 0;
	u8 id = 0;
	int count;

	for (count = 1; count <= 5; count++) {
		ret = cp_read_byte(bq->client, 0x6e, &id);
		if (ret >= 0)
			return id;
		msleep(100);
#line 1851
		mca_log_info(" %s failed to read sy6541 device id, retry count = %d\n", bq->log_tag, count);
	}
#line 1858
	mca_log_info("%s failed to detect sy6541 device. retry %d times\n", bq->log_tag, 5);
	return ret;
}
EXPORT_SYMBOL(is_sy6541_cp);

int sy6541_probe(struct sc8581_device *bq)
{
	int ret = 0;

	ret = sy6541_register_irq(bq);
	if (ret) {
#line 1884
		mca_log_err("%s failed to int irq\n", bq->log_tag);
		return ret;
	}

	sy6541_init_device(bq);
	INIT_DELAYED_WORK(&bq->irq_handle_work, sy6541_irq_handler);
	schedule_delayed_work(&bq->irq_handle_work, 0);
	bq->chip_ok = true;
	bq->ovpgate_en = true;
	bq->i2c_is_working = true;
#ifdef CONFIG_DEBUG_FS
	reg_info.address = SC8581_REG_00;
	reg_info.count = 1;

	if (bq->cp_role == SC8581_SLAVE)
		mca_debugfs_create_group("sy6541_01", cp_sy6541_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE, bq);
	else
		mca_debugfs_create_group("sy6541_00", cp_sy6541_debugfs_field_tbl, CP_DEBUGFS_ATTRS_SIZE, bq);
#endif
	platform_class_cp_register_ops(bq->cp_role, &sy6541_chg_ops, bq);
	mca_log_charge_log_register(MCA_CHARGE_LOG_ID_CP_MASTER_IC,
		&g_sy6541_log_ops, bq);
	device_init_wakeup(bq->dev, true);
#line 1914
	mca_log_err("%s probe success %d\n", bq->log_tag, ret);

	return 0;
}
EXPORT_SYMBOL(sy6541_probe);
