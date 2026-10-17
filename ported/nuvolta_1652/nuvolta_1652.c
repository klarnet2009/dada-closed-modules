// SPDX-License-Identifier: GPL-2.0
/*
 * nuvolta_1652.c
 *
 * wireless RX ic driver
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

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irqreturn.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>

#include "inc/nuvolta_1652.h"
#include "inc/fw_update_nu1652.h"
#include "inc/fw_update_nu1661.h"
#include "inc/fw_update_nu1665.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_class.h>
#include "inc/os4_defs.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "nuvolta_1652"
#endif

/*
 * The stock link (module.lds) merges every zero-size lockdep key into the one .bss address
 * of total_length; the DDK/ThinLTO link keeps one section per object. Put all keys (and
 * total_length) into one explicit section so that the relocation targets resolve as in the stock.
 */
#define NUV_KEY_SECTION	__attribute__((section(".bss.nuvolta_1652_probe.__key")))
#undef mutex_init
#define mutex_init(mutex)						\
do {									\
	static struct lock_class_key __key NUV_KEY_SECTION;		\
	__mutex_init((mutex), #mutex, &__key);				\
} while (0)

#define I2C_RETRY_CNT 3

static const struct regmap_config nuvolta_1652_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xFFFF,
};

static int rx1652_read(struct nuvolta_1652_chg *chip, u8 *val, u16 addr)
{
	int i, ret = 0;
	unsigned int temp;

	mutex_lock(&chip->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_read(chip->regmap, addr, &temp);
		if (IS_ERR_VALUE((unsigned long)ret)) {
#line 68
			mca_log_err("failed-read, reg(0x%02X), ret(%d)\n", addr, ret);
		} else {
			*val = (u8)temp;
			break;
		}
	}
	mutex_unlock(&chip->i2c_lock);

	return ret;
}

static int rx1652_read_buffer(struct nuvolta_1652_chg *chip, u8 *buf, u16 addr, int size)
{
	int ret = 0;

	while (size--) {
		ret = rx1652_read(chip, buf++, addr++);
		if (IS_ERR_VALUE((unsigned long)ret)) {
#line 86
			mca_log_err("i2c read error: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int rx1652_write(struct nuvolta_1652_chg *chip, u8 val, u16 addr)
{
	int i, ret = 0;

	mutex_lock(&chip->i2c_lock);
	for (i = 0; i < I2C_RETRY_CNT; ++i) {
		ret = regmap_write(chip->regmap, addr, val);
		if (IS_ERR_VALUE((unsigned long)ret))
#line 102
			mca_log_err("failed-write, reg(0x%02X), ret(%d)\n", addr, ret);
		else
			break;
	}
	mutex_unlock(&chip->i2c_lock);

	return ret;
}


static int rx1652_write_buffer(struct nuvolta_1652_chg *chip, u8 *buf, u16 addr, int size)
{
	int ret = 0;

	while (size--) {
		ret = rx1652_write(chip, *buf++, addr++);
		if (IS_ERR_VALUE((unsigned long)ret)) {
#line 119
			mca_log_err("i2c write error: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int nuvolta_1652_set_enable_mode(bool enable, void *data)
{
	int ret = 0;
	int en = !!enable;
	int gpio_enable_val = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

#line 134
	mca_log_info("set enable mode:%d\n", enable);

	ret = gpio_direction_output(chip->enable_gpio, !en);
	if (ret)
#line 138
		mca_log_err("set direction for enable gpio [%d] fail\n", chip->enable_gpio);

	gpio_enable_val = gpio_get_value(chip->enable_gpio);
#line 141
	mca_log_info("nuvolta enable gpio val is :%d\n", gpio_enable_val);

	return ret;
}


static int nuvolta_1652_check_i2c(struct nuvolta_1652_chg *chip)
{
	int ret = 0;
	u8 data = 0;

	mutex_lock(&chip->data_transfer_lock);

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_SENT_CMD);
	if (ret < 0)
		goto exit;

	msleep(20);

	ret = rx1652_read(chip, &data, REG_RX_SENT_CMD);
	if (ret < 0)
		goto exit;

	if (data == RX_CMD_I2C_CHECK_MASK)
#line 164
		mca_log_info("i2c check ok!\n");
	else {
#line 166
		mca_log_err("i2c check failed!\n");
		ret = -1;
	}

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_ops_check_i2c(void *data)
{
	int ret = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	ret = nuvolta_1652_check_i2c(chip);
	return ret;
}

static noinline void nuvolta_1652_dump_register(struct nuvolta_1652_chg *chip)
{
	u8 buf0[16] = {0};
	u8 buf1[16] = {0};
	u8 buf2[3] = {0};
	int i;

	rx1652_read_buffer(chip, buf0, 0x00, 16);
	rx1652_read_buffer(chip, buf1, 0x20, 16);
	rx1652_read_buffer(chip, buf2, 0x60, 3);

	for (i = 0; i < 16; i += 8)
#line 218
		mca_log_info("0x0000: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X", i, buf0[i], i + 1, buf0[i + 1], i + 2, buf0[i + 2], i + 3, buf0[i + 3], i + 4, buf0[i + 4], i + 5, buf0[i + 5], i + 6, buf0[i + 6], i + 7, buf0[i + 7]);
	for (i = 0; i < 16; i += 8)
#line 228
		mca_log_info("0x0020: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X", i + 0x20, buf1[i], i + 0x21, buf1[i + 1], i + 0x22, buf1[i + 2], i + 0x23, buf1[i + 3], i + 0x24, buf1[i + 4], i + 0x25, buf1[i + 5], i + 0x26, buf1[i + 6], i + 0x27, buf1[i + 7]);
#line 233
	mca_log_info("0x0060: [0x%02X]=0x%02X, [0x%02X]=0x%02X, [0x%02X]=0x%02X", 0x60, buf2[0], 0x61, buf2[1], 0x62, buf2[2]);
}

static bool nuvolta_1652_check_cmd_free(struct nuvolta_1652_chg *chip, u16 reg)
{
	u8 rx_cmd_busy = 0;
	u8 retry = 0;

	while (retry++ < I2C_CMD_CHECK_RETRY_COUNT) {
		rx1652_read(chip, &rx_cmd_busy, reg);
		if (rx_cmd_busy != I2C_CMD_CHECK_BUSY)
			return true;
	}

#line 195
	mca_log_info("reg: %x always busy\n", reg);
	nuvolta_1652_dump_register(chip);
	return false;
}

static bool nuvolta_1652_check_buffer_ready(struct nuvolta_1652_chg *chip)
{
	return nuvolta_1652_check_cmd_free(chip, REG_RX_REV_DATA4);
}

static bool nuvolta_1652_check_rx_ready(struct nuvolta_1652_chg *chip)
{
	return nuvolta_1652_check_cmd_free(chip, REG_RX_REV_DATA5);
}

static u8 nuvolta_1652_get_boot_fw_version(struct nuvolta_1652_chg *chip)
{
	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665)
		return ((~chip->fw_data_ptr[1527]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1651)
		return ((~chip->fw_data_ptr[1*1024-9]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1661)
		return ((~chip->fw_data_ptr[2*1024-9]) & 0xFF);

#line 263
	mca_log_err("unexpect rx chip vendor\n");
	return 0;
}

static u8 nuvolta_1652_get_rx_fw_version(struct nuvolta_1652_chg *chip)
{
	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665)
		return ((~chip->fw_data_ptr[24567]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1651)
		return ((~chip->fw_data_ptr[23*1024-9]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1661)
		return ((~chip->fw_data_ptr[22*1024-9]) & 0xFF);

#line 276
	mca_log_err("unexpect rx chip vendor\n");
	return 0;
}

static u8 nuvolta_1652_get_tx_fw_version(struct nuvolta_1652_chg *chip)
{
	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665)
		return ((~chip->fw_data_ptr[32759]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1651)
		return ((~chip->fw_data_ptr[32*1024-9]) & 0xFF);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1661)
		return ((~chip->fw_data_ptr[32*1024-9]) & 0xFF);

#line 289
	mca_log_err("unexpect rx chip vendor\n");
	return 0;
}

static int nuvolta_1652_disable_mcu(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 297
	mca_log_info("disable mcu\n");

	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665)
		ret = rx1652_write(chip, RX_CMD_DIS_MCU_1665, REG_MCU_CTRL_REG);
	else if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1651 ||
				chip->project_vendor == WLS_CHIP_VENDOR_FUDA1661)
		ret = rx1652_write(chip, RX_CMD_DIS_MCU_1651, REG_MCU_CTRL_REG);
	else
		ret = rx1652_write(chip, RX_CMD_DIS_MCU_1651, REG_MCU_CTRL_REG);

	if (ret < 0)
		return ret;

	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665)
		return ret;

	ret = rx1652_write(chip, RX_CMD_SET_1P8V, REG_GPIO_RAIL_CTRL);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_mux_burn_free(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 324
	mca_log_info("mux burn free\n");

	ret = rx1652_write(chip, RX_CMD_MTP_STATE_EN, REG_MTP_STATE_PIN);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_select_all_sector(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 337
	mca_log_info("select all sector\n");

	ret = rx1652_write(chip, SECTOR_SELECT_MASK, REG_SECTOR_SELECT_REG);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_get_fw_version(u8 *check_result, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 read_buf[FW_VERSION_BUF_LENGTH] = {0};
	u8 fw_boot_check = 0, fw_rx_check = 0, fw_tx_check = 0;

	ret = rx1652_write(chip, RX_CMD_ENABLE_CRC, REG_RX_INT_3);
	if (ret < 0)
		return ret;

	msleep(20);

	ret = rx1652_read_buffer(chip, read_buf, REG_RX_REV_DATA8, FW_VERSION_BUF_LENGTH);
	if (ret < 0)
		return ret;

	fw_boot_check = read_buf[0];
	fw_rx_check = read_buf[1];
	fw_tx_check = read_buf[2];

#line 367
	mca_log_info("boot_check: 0x%x, rx_check: 0x%x, tx_check: 0x%x\n",
		fw_boot_check, fw_rx_check, fw_tx_check);

	if (fw_boot_check == CRC_CHECK_SUCCESS)
		chip->wls_fw_data->fw_boot_id = read_buf[4];
	else
		chip->wls_fw_data->fw_boot_id = CRC_CHECK_ERR_VER;

	if (fw_rx_check == CRC_CHECK_SUCCESS)
		chip->wls_fw_data->fw_rx_id = read_buf[5];
	else
		chip->wls_fw_data->fw_rx_id = CRC_CHECK_ERR_VER;

	if (fw_tx_check == CRC_CHECK_SUCCESS)
		chip->wls_fw_data->fw_tx_id = read_buf[6];
	else
		chip->wls_fw_data->fw_tx_id = CRC_CHECK_ERR_VER;

	if ((chip->wls_fw_data->fw_boot_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_boot_id >= nuvolta_1652_get_boot_fw_version(chip)))
		*check_result |= BOOT_CHECK_SUCCESS;

	if ((chip->wls_fw_data->fw_rx_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_rx_id >= nuvolta_1652_get_rx_fw_version(chip)))
		*check_result |= RX_CHECK_SUCCESS;

	if ((chip->wls_fw_data->fw_tx_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_tx_id >= nuvolta_1652_get_tx_fw_version(chip)))
		*check_result |= TX_CHECK_SUCCESS;

#line 397
	mca_log_info("fw_data_1652 version: boot = 0x%x, rx = 0x%x, tx = 0x%x\n",
		nuvolta_1652_get_boot_fw_version(chip), nuvolta_1652_get_rx_fw_version(chip),
		nuvolta_1652_get_tx_fw_version(chip));

#line 401
	mca_log_info("ic fw version: boot = 0x%x, rx = 0x%x, tx = 0x%x\n",
		chip->wls_fw_data->fw_boot_id, chip->wls_fw_data->fw_rx_id, chip->wls_fw_data->fw_tx_id);

	return ret;
}


static int nuvolta_1652_get_firmware_version(char *buf, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	scnprintf(buf, 10, "%02x.%02x.%02x",
				chip->wls_fw_data->fw_boot_id, chip->wls_fw_data->fw_rx_id, chip->wls_fw_data->fw_tx_id);

#line 415
	mca_log_info("rx firmware version: boot = 0x%x, rx = 0x%x, tx = 0x%x\n",
		chip->wls_fw_data->fw_boot_id, chip->wls_fw_data->fw_rx_id, chip->wls_fw_data->fw_tx_id);

	return 0;
}

static int nuvolta_1652_enter_write_mode(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 425
	mca_log_info("enter write mode\n");

	if (chip->project_vendor != WLS_CHIP_VENDOR_FUDA1665)
		ret = rx1652_write(chip, RX_CMD_ENTER_WRITE_MODE, REG_MTP_CTRL2);

	return ret;
}

static int nuvolta_1652_exit_write_mode(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 437
	mca_log_info("exit write mode\n");

	ret = rx1652_write(chip, 0x00, REG_MTP_CTRL2);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_exit_dtm_mode(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

#line 450
	mca_log_info("exit dtm mode\n");

	ret = rx1652_write(chip, 0x00, REG_DTM_UNLOCK_REG1);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, 0x00, REG_DTM_UNLOCK_REG0);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_enter_dtm_mode(struct nuvolta_1652_chg *chip)
{
	int ret = 0;
	u8 data;

#line 468
	mca_log_info("enter dtm mode\n");

	ret = rx1652_write(chip, RX_CMD_DISABLE_SLEEP, REG_RX_SLEEP_CTRL_REG);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, 0x00, REG_DTM_UNLOCK_REG0);
	if (ret < 0)
		return ret;

	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665) {
		ret = rx1652_write(chip, FUDA1665_DTM_REG0_DATA0, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG0_DATA1, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG0_DATA2, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG0_DATA3, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
	} else {
		ret = rx1652_write(chip, FUDA1651_DTM_REG0_DATA0, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG0_DATA1, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG0_DATA2, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG0_DATA3, REG_DTM_UNLOCK_REG0);
		if (ret < 0)
			return ret;
	}

	ret = rx1652_read(chip, &data, REG_DTM_UNLOCK_REG0);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, 0x00, REG_DTM_UNLOCK_REG1);
	if (ret < 0)
		return ret;

	if (chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665) {
		ret = rx1652_write(chip, FUDA1665_DTM_REG1_DATA0, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG1_DATA1, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG1_DATA2, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1665_DTM_REG1_DATA3, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
	} else {
		ret = rx1652_write(chip, FUDA1651_DTM_REG1_DATA0, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG1_DATA1, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG1_DATA2, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
		ret = rx1652_write(chip, FUDA1651_DTM_REG1_DATA3, REG_DTM_UNLOCK_REG1);
		if (ret < 0)
			return ret;
	}

	ret = rx1652_write(chip, RX_CMD_DISABLE_SLEEP, REG_RX_SLEEP_CTRL_REG);
	if (ret < 0)
		return ret;

#line 546
	mca_log_info("reg:0x2717 = 0x%x\n", data);

	return ret;
}

static int nuvolta_1652_set_confirm_data(void *data, u8 confirm_data)
{
	int ret = 0;
	u8 read_data = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	ret = nuvolta_1652_check_i2c(chip);
	if (ret < 0)
		return ret;

	ret = nuvolta_1652_enter_dtm_mode(chip);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, RX_CMD_DIS_MCU_EN_TRIM, REG_MCU_CTRL_REG);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, RX_CMD_DISABLE_SLEEP, REG_RX_SLEEP_CTRL_REG);
	if (ret < 0)
		return ret;

	msleep(20);

	ret = rx1652_write(chip, confirm_data, REG_CONFIRM_DATA);
	if (ret < 0)
		return ret;

	msleep(20);

	ret = rx1652_read(chip, &read_data, REG_CONFIRM_DATA);
	if (ret < 0)
		return ret;

	if (read_data != confirm_data) {
#line 586
		mca_log_err("set failed, read data: %d\n", read_data);
		return -1;
	}

#line 590
	mca_log_info("set confirm data success: %d\n", confirm_data);

	ret = rx1652_write(chip, RX_CMD_ENTER_TRIM, REG_MTP_CTRL2);
	if (ret < 0)
		return ret;

	return ret;
}

static int nuvolta_1652_get_confirm_data(struct nuvolta_1652_chg *chip, u8 *confirm_data)
{
	int ret = 0;
	u8 data = 0;

	ret = nuvolta_1652_check_i2c(chip);
	if (ret < 0)
		return ret;

	ret = nuvolta_1652_enter_dtm_mode(chip);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, RX_CMD_DIS_MCU_EN_TRIM, REG_MCU_CTRL_REG);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, RX_CMD_DISABLE_SLEEP, REG_RX_SLEEP_CTRL_REG);
	if (ret < 0)
		return ret;

	msleep(20);

	ret = rx1652_read(chip, &data, REG_CONFIRM_DATA);
	if (ret < 0)
		return ret;

	*confirm_data = data;
#line 627
	mca_log_info("get confirm data: 0x%x\n", data);

	ret = rx1652_write(chip, RX_CMD_ENTER_TRIM, REG_MTP_CTRL2);
	if (ret < 0)
		return ret;

	return ret;
}


static int nuvolta_1652_erase_fw_data(void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 confirm_data = 0;
	u8 read_data = 0, wrfail = 0, busy = 0;
	int i = 0, j = 0;
	int fw_data_length = RX_FW_DATA_DEFAULT_LENGTH;
	bool first_word = true;

#line 647
	mca_log_info("erase firmware\n");
	ret |= nuvolta_1652_get_confirm_data(chip, &confirm_data);
	ret |= nuvolta_1652_set_confirm_data(chip, 0x00);
	if (ret < 0)
		return -1;

	ret = nuvolta_1652_enter_dtm_mode(chip);
	if (ret < 0) {
#line 655
		mca_log_err("failed to enter dtm mode\n");
		return ret;
	}

	ret = nuvolta_1652_disable_mcu(chip);
	if (ret < 0) {
#line 661
		mca_log_err("failed to disable_mcu\n");
		goto exit;
	}

	ret = nuvolta_1652_mux_burn_free(chip);
	if (ret < 0) {
#line 667
		mca_log_err("failed to mux_burn_free\n");
		goto exit;
	}

	ret = nuvolta_1652_select_all_sector(chip);
	if (ret < 0) {
#line 673
		mca_log_err("failed to select_all_sector\n");
		goto exit;
	}

	ret = nuvolta_1652_enter_write_mode(chip);
	if (ret < 0) {
#line 679
		mca_log_err("failed to enter_write_mode\n");
		goto exit;
	}

	msleep(20);

	for (i = 0; i < fw_data_length; i += RX_FW_HANDLE_STEP) {
		if ((i % RX_FW_FORMAT_LENGTH) == 0)
			msleep(20);

		if (first_word && chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665) {
			first_word = false;
			ret = rx1652_write(chip, RX_CMD_WRITE_ENABLE, REG_MTP_CTRL0);
			ret |= rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA0);
			ret |= rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA1);
			ret |= rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA2);
			ret |= rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA3);
			ret |= rx1652_write(chip, RX_CMD_EXECUTE_HIGH, REG_MTP_CTRL1);
			ret |= rx1652_write(chip, RX_CMD_EXECUTE_LOW, REG_MTP_CTRL1);
			ret |= rx1652_write(chip, RX_CMD_ENTER_WRITE_MODE, REG_MTP_CTRL2);

			if (ret)
				goto exit;
		}

		ret = rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA0);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA1);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA2);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, RX_CMD_ERASE_MASK, REG_MTP_CTRL_DATA3);
		if (ret < 0)
			goto exit;

		for (j = 0; j < RX_FW_WRITE_CHECK_COUNT; j++) {
			ret = rx1652_read(chip, &read_data, REG_MTP_STATUS);
			if (ret < 0)
				goto exit;

			busy = (read_data & MTP_STATUS_MASK) >> MTP_STATUS_SHIFT;
			if (busy == 0) {
				wrfail = (read_data & MTP_WRITE_RESULT_MASK) >> MTP_WRITE_RESULT_SHIFT;
				if (wrfail == 1) {
#line 729
					mca_log_err("wrfail, MTP error\n");
					ret = -1;
					goto exit;
				} else {
					break;
				}
			}
		}
	}

	ret = nuvolta_1652_exit_write_mode(chip);
	if (ret < 0)
		goto exit;

	ret = nuvolta_1652_exit_dtm_mode(chip);
	if (ret < 0)
		goto exit;

	return ret;

exit:
	msleep(100);
#line 751
	mca_log_err("wrfail, MTP error\n");

	ret = nuvolta_1652_exit_write_mode(chip);
	if (ret < 0)
		return ret;

	ret = nuvolta_1652_exit_dtm_mode(chip);
	if (ret < 0)
		return ret;

	return -1;
}

static int nuvolta_1652_download_fw_data(struct nuvolta_1652_chg *chip,
	unsigned char *fw_data, int fw_data_length)
{
	int ret = 0;
	u8 read_data = 0, wrfail = 0, busy = 0;
	int i = 0, j = 0;
	bool first_word = true;

#line 772
	mca_log_info("start\n");

	ret = nuvolta_1652_enter_dtm_mode(chip);
	if (ret < 0) {
#line 776
		mca_log_err("failed to enter dtm mode\n");
		return ret;
	}

	ret = nuvolta_1652_disable_mcu(chip);
	if (ret < 0) {
#line 782
		mca_log_err("failed to disable_mcu\n");
		goto exit;
	}

	ret = nuvolta_1652_mux_burn_free(chip);
	if (ret < 0) {
#line 788
		mca_log_err("failed to mux_burn_free\n");
		goto exit;
	}

	ret = nuvolta_1652_select_all_sector(chip);
	if (ret < 0) {
#line 794
		mca_log_err("failed to select_all_sector\n");
		goto exit;
	}

	ret = nuvolta_1652_enter_write_mode(chip);
	if (ret < 0) {
#line 800
		mca_log_err("failed to enter_write_mode\n");
		goto exit;
	}

	msleep(20);

	for (i = 0; i < fw_data_length; i += RX_FW_HANDLE_STEP) {
		if ((i % RX_FW_FORMAT_LENGTH) == 0)
			msleep(20);

		if (first_word && chip->project_vendor == WLS_CHIP_VENDOR_FUDA1665) {
			first_word = false;
			ret = rx1652_write(chip, RX_CMD_WRITE_ENABLE, REG_MTP_CTRL0);
			ret |= rx1652_write(chip, fw_data[i+3], REG_MTP_CTRL_DATA0);
			ret |= rx1652_write(chip, fw_data[i+2], REG_MTP_CTRL_DATA1);
			ret |= rx1652_write(chip, fw_data[i+1], REG_MTP_CTRL_DATA2);
			ret |= rx1652_write(chip, fw_data[i], REG_MTP_CTRL_DATA3);
			ret |= rx1652_write(chip, RX_CMD_EXECUTE_HIGH, REG_MTP_CTRL1);
			ret |= rx1652_write(chip, RX_CMD_EXECUTE_LOW, REG_MTP_CTRL1);
			ret |= rx1652_write(chip, RX_CMD_ENTER_WRITE_MODE, REG_MTP_CTRL2);

			if (ret)
				goto exit;
		}

		ret = rx1652_write(chip, fw_data[i+3], REG_MTP_CTRL_DATA0);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, fw_data[i+2], REG_MTP_CTRL_DATA1);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, fw_data[i+1], REG_MTP_CTRL_DATA2);
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, fw_data[i], REG_MTP_CTRL_DATA3);
		if (ret < 0)
			goto exit;

		for (j = 0; j < RX_FW_WRITE_CHECK_COUNT; j++) {
			ret = rx1652_read(chip, &read_data, REG_MTP_STATUS);
			if (ret < 0)
				goto exit;

			busy = (read_data & MTP_STATUS_MASK) >> MTP_STATUS_SHIFT;
			if (busy == 0) {
				wrfail = (read_data & MTP_WRITE_RESULT_MASK) >> MTP_WRITE_RESULT_SHIFT;
				if (wrfail == 1) {
#line 850
					mca_log_err("wrfail, MTP error\n");
					ret = -1;
					goto exit;
				} else {
					break;
				}
			}
		}
	}

	ret = nuvolta_1652_exit_write_mode(chip);
	if (ret < 0)
		goto exit;

	ret = nuvolta_1652_exit_dtm_mode(chip);
	if (ret < 0)
		goto exit;

	return ret;

exit:
	msleep(100);
#line 872
	mca_log_err("wrfail, MTP error\n");

	ret = nuvolta_1652_exit_write_mode(chip);
	if (ret < 0)
		return ret;

	ret = nuvolta_1652_exit_dtm_mode(chip);
	if (ret < 0)
		return ret;

	return -1;
}

static int nuvolta_1652_download_fw_from_bin(void *data)
{
	int ret = 0;
	int i = 0;
	u8 confirm_data = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

#line 892
	mca_log_info("enter download firmware\n");
	for (i = 0; i < chip->fw_bin_length; ++i)
		chip->fw_bin[i] = ~(chip->fw_bin[i]);

#line 896
	mca_log_info("download firmware from bin\n");
	ret |= nuvolta_1652_get_confirm_data(chip, &confirm_data);
	ret |= nuvolta_1652_set_confirm_data(chip, 0x00);
	if (ret < 0)
		return -1;

	ret = nuvolta_1652_download_fw_data(chip, chip->fw_bin, chip->fw_bin_length);
	chip->fw_bin_length = 0;
	return ret;
}

static int nuvolta_1652_download_fw(void *data)
{
	int ret = 0;
	u8 confirm_data = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

#line 913
	mca_log_info("download firmware normal\n");

	ret |= nuvolta_1652_get_confirm_data(chip, &confirm_data);
	ret |= nuvolta_1652_set_confirm_data(chip, 0x00);
	if (ret < 0)
		return -1;

	ret = nuvolta_1652_download_fw_data(chip, chip->fw_data_ptr, chip->fw_data_size);

	return ret;
}

//#ifdef CONFIG_SYSFS
static int nuvolta_1652_set_fw_bin(const char *buf, int count, void *data)
{
	static u16 total_length __attribute__((section(".bss.nuvolta_1652_probe.__key")));
	static u8 serial_number;
	static u8 fw_area;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

#line 934
	mca_log_info("buf:%s, count:%d\n", buf, count);
	if (strncmp("length:", buf, 7) == 0) {
		if (kstrtou16(buf+7, 10, &total_length))
			return -EINVAL;
		chip->fw_bin_length = total_length;
		serial_number = 0;
#line 940
		mca_log_info("total_length:%d, serial_number:%d\n", total_length, serial_number);
	} else if (strncmp("area:", buf, 5) == 0) {
		if (kstrtou8(buf+5, 10, &fw_area))
			return -EINVAL;
#line 944
		mca_log_info("area:%d\n", fw_area);
	} else {
		memcpy((chip->fw_bin + serial_number * count), buf, count);
		serial_number++;
#line 948
		mca_log_info("serial_number:%d, count:%d\n", serial_number, count);
	}
	return count;
}

static void nuvolta_1652_clear_int(struct nuvolta_1652_chg *chip, u8 int_flag_lsb, u8 int_flag_msb)
{
	bool status = true;
	int ret = 0;

	mutex_lock(&chip->data_transfer_lock);

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_CLEAR_INT, REG_RX_SENT_CMD);
	ret = rx1652_write(chip, RX_CLEAR_INT_LENGTH, REG_RX_SENT_DATA1);
	ret = rx1652_write(chip, int_flag_lsb, REG_RX_SENT_DATA2);
	ret = rx1652_write(chip, int_flag_msb, REG_RX_SENT_DATA3);
	ret = rx1652_write(chip, RX_CLEAR_INT_TRIGGER_RX, REG_RX_INT_0);

#line 970
	mca_log_info("clear int ret: %d, int_flag_lsb = %d, int_flag_msb = %d\n", ret, int_flag_lsb, int_flag_msb);

exit:
	mutex_unlock(&chip->data_transfer_lock);
}

static int nuvolta_1652_get_rx_rtx_mode(int *mode, void *data)
{
	u8 temp = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	rx1652_read(chip, &temp, RX_RTX_MODE);
#line 982
	mca_log_info("data = %d\n", temp);
	if (temp == TRX_MODE_STATUS)
		*mode = RTX_MODE;
	else
		*mode = RX_MODE;
	return 0;
}

static int nuvolta_1652_enable_reverse_chg(bool enable, void *data)
{
	int ret = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (enable)
		ret = rx1652_write(chip, RX_CMD_ENABLE_TX, REG_RX_INT_3);
	else
		ret = rx1652_write(chip, RX_CMD_DISABLE_TX, REG_RX_INT_3);

#line 1000
	mca_log_err("enable_reverse_chg: %d\n", enable);
	return ret;
}

static int nuvolta_1652_is_present(int *present, void *data)
{
	int ret = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->proc_data.power_good_flag)
		*present = 1;
	else
		*present = 0;

	return ret;
}

static int nuvolta_1652_get_cep_value(u8 *cep, void *data)
{
	int ret = 0;
	bool status = true;
	u8 read_buf[128];
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		return ret;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, 30);
	if (ret < 0)
		return ret;

	*cep = read_buf[10];
#line 1048
	mca_log_info("cep: %d\n", *cep);

	return ret;
}

static int nuvolta_1652_enable_reverse_fod(bool enable, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 gain = 94;
	u8 offset = 0;

	mutex_lock(&chip->data_transfer_lock);

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		goto exit;

	if (enable) {
		ret = rx1652_write(chip, RX_CMD_ENABLE_REVERSE_FOD, REG_RX_SENT_CMD);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, REVERSE_FOD_EN, REG_RX_SENT_DATA1);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, gain, REG_RX_SENT_DATA2);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, offset, REG_RX_SENT_DATA3);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, REVERSE_FOD_TRIGGER_RX, REG_RX_INT_0);
		if (ret < 0)
			goto exit;
#line 1082
		mca_log_info("gain: %d, offset:%d\n", gain, offset);
	} else {
		ret = rx1652_write(chip, RX_CMD_ENABLE_REVERSE_FOD, REG_RX_SENT_CMD);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, REVERSE_FOD_DIS, REG_RX_SENT_DATA1);
		if (ret < 0)
			goto exit;
		ret = rx1652_write(chip, DISABLE_REVERSE_FOD_TRIGGER_RX, REG_RX_INT_0);
		if (ret < 0)
			goto exit;
#line 1093
		mca_log_info("disable reverse fod\n");
	}

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_set_vout(int vout, void *data)
{
	int ret = 0;
	u8 vout_h, vout_l;
	u8 cep;
	int max_vol = VOUT_SET_MAX_MV;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag)
		return -1;

	mutex_lock(&chip->data_transfer_lock);
	msleep(20);

	if (chip->proc_data.parallel_charge == true) {
		ret = nuvolta_1652_get_cep_value(&cep, chip);
		if (ABS(cep) > ABS_CEP_VALUE) {
#line 1118
			mca_log_info("vol:%d ,cep %d, not set\n", vout, cep);
			goto exit;
		}
	}

	if (vout < VOUT_SET_MIN_MV)
		vout = VOUT_SET_DEFAULT_MV;
	else if (vout > max_vol)
		vout = max_vol;

	vout_h = (u8)(vout >> 8);
	vout_l = (u8)(vout & 0xFF);

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_SET_RX_VOUT, REG_RX_SENT_CMD);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, VOUT_SET_PACKET_LENGTH, REG_RX_SENT_DATA1);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, vout_l, REG_RX_SENT_DATA2);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, vout_h, REG_RX_SENT_DATA3);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, VOUT_SET_TRIGGER_RX, REG_RX_INT_0);
	if (ret < 0)
		goto exit;

	chip->proc_data.vout_setted = vout;
#line 1156
	mca_log_info("wls_set_vout: %d\n", vout);

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_get_vout(int *vout, void *data)
{
	int ret = 0;
	bool status = true;
	u8 cep = 0;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag) {
		*vout = 0;
		return ret;
	}

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return ret;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, I2C_READ_RX_BUF_LENGTH);
	if (ret < 0)
		return ret;

	*vout = read_buf[GET_VOUT_SHIFT1] * 256 + read_buf[GET_VOUT_SHIFT0];
	cep = read_buf[GET_CEP_SHIFT];

#line 1202
	mca_log_info("wls_get_vout: %d\n", *vout);

	return ret;
}

static int nuvolta_1652_get_iout(int *iout, void *data)
{
	int ret = 0;
	bool status = true;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag) {
		*iout = 0;
		return ret;
	}

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return 0;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, I2C_READ_RX_BUF_LENGTH);
	if (ret < 0)
		return ret;

	*iout = read_buf[GET_IOUT_SHIFT1] * 256 + read_buf[GET_IOUT_SHIFT0];

#line 1245
	mca_log_info("wls_get_iout: %d\n", *iout);

	return ret;
}

static int nuvolta_1652_get_vrect(int *vrect, void *data)
{
	int ret = 0;
	bool status = true;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag) {
		*vrect = 0;
		return ret;
	}

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return 0;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, I2C_READ_RX_BUF_LENGTH);
	if (ret < 0)
		return ret;

	*vrect = read_buf[VRECT_SHIFT1] * 256 + read_buf[VRECT_SHIFT0];
#line 1287
	mca_log_info("wls_get_vrect: %d\n", *vrect);

	return ret;
}

static int nuvolta_1652_get_temp(int *temp, void *data)
{
	int ret = 0;
	bool status = true;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag) {
		*temp = 0;
		return ret;
	}

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return 0;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, I2C_READ_RX_BUF_LENGTH);
	if (ret < 0)
		return ret;

	*temp = read_buf[GET_RX_TEMP_SHIFT1] * 256 + read_buf[GET_RX_TEMP_SHIFT0];
#line 1329
	mca_log_info("wls_get_rx_temp: %d\n", *temp);

	return ret;
}

static int nuvolta_1652_get_tx_adapter(int *adapter, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->proc_data.power_good_flag)
		*adapter = chip->proc_data.adapter_type;
	else
		*adapter = ADAPTER_NONE;

#line 1343
	mca_log_info("wls_get_adapter: %d\n", chip->proc_data.adapter_type);

	return 0;
}

static int nuvolta_1652_get_tx_adapter_by_i2c(int *adapter, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	bool status = true;
	u8 read_buf[40];

	if (!chip->proc_data.power_good_flag) {
		*adapter = 0;
		return ret;
	}

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return -1;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return -1;
	}

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, RX_AUTH_DATA_LENGTH);
	if (ret < 0)
		return ret;
	/*  adapter type */
	*adapter = read_buf[7];

#line 1385
	mca_log_info("wls_get_adapter_by_i2c: %d\n", *adapter);

	return ret;
}


static int nuvolta_1652_is_car_adapter(bool *enable, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->proc_data.is_car_tx)
		*enable = true;
	else
		*enable = false;

	return 0;
}

static int nuvolta_1652_get_hall_gpio_status(bool *status, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->hall_gpio_status)
		*status = true;
	else
		*status = false;

	return 0;
}

static int nuvolta_1652_get_magnetic_case_flag(bool *status, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->magnetic_case_flag)
		*status = true;
	else
		*status = false;

	return 0;
}

static int nuvolta_1652_set_input_current_limit(int value, void *data)
{
#line 1429
	mca_log_info("set input current limit:%d\n", value);

	return mca_adsp_glink_write_prop(ADSP_PROP_ID_DC_INPUT_CURR_LIMIT, &value, sizeof(value));
}

static int nuvolta_1652_get_rx_int_flag(int *int_flag, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	*int_flag = chip->proc_data.int_flag;

	return 0;
}

static int nuvolta_1652_get_rx_power_mode(u8 *power_mode, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	u8 read_buf[5] = {0};
	bool status = true;
	int ret = 0;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return -1;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return -1;
	}

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, 5);
	if (ret < 0)
		return ret;

	*power_mode = read_buf[POWER_MODE_SHIFT];
#line 1475
	mca_log_info("rx power mode: %d\n", *power_mode);
#line 1476
	mca_log_info("boot: 0x%x, rx: 0x%x, tx: 0x%x\n",
				read_buf[0], read_buf[1], read_buf[2]);

	return 0;
}

static int nuvolta_1652_get_tx_max_power(u8 *max_power, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	u8 read_buf[6] = {0};
	bool status = true;
	int ret = 0;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return -1;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return -1;
	}

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, 6);
	if (ret < 0)
		return ret;

	*max_power = read_buf[MAX_POWER_SHIFT];
#line 1514
	mca_log_info("tx max power: %d\n", *max_power);

	return 0;
}

static int nuvolta_1652_get_auth_value(int *value, void *data)
{
	u8 auth_data = 0;
	bool status = true;
	int ret = 0;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return auth_data;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return auth_data;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return auth_data;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return auth_data;
	}

	u8 read_buf[RX_AUTH_DATA_LENGTH];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, RX_AUTH_DATA_LENGTH);
	if (ret < 0)
		return auth_data;

	auth_data = read_buf[8];
#line 1552
	mca_log_info("auth_data = 0x%x\n", auth_data);

	if (auth_data > AUTH_STATUS_FAILED) {
		chip->proc_data.epp_tx_id_l = read_buf[TX_ID_LOW_SHIFT];
		chip->proc_data.epp_tx_id_h = read_buf[TX_ID_HIGH_SHIFT];
	}

	chip->proc_data.adapter_type = read_buf[ADAPTER_TYPE_SHIFT];
	if (chip->proc_data.adapter_type == ADAPTER_NONE)
		chip->proc_data.adapter_type = ADAPTER_AUTH_FAILED;

	if (auth_data > AUTH_STATUS_UUID_OK) {
		chip->proc_data.uuid[0] = read_buf[UUID0_SHIFT];
		chip->proc_data.uuid[1] = read_buf[UUID1_SHIFT];
		chip->proc_data.uuid[2] = read_buf[UUID2_SHIFT];
		chip->proc_data.uuid[3] = read_buf[UUID3_SHIFT];
	}

	*value = auth_data;

#line 1572
	mca_log_info("tx_id_l: 0x%x, tx_id_h: 0x%x\n",
		chip->proc_data.epp_tx_id_l, chip->proc_data.epp_tx_id_h);
#line 1574
	mca_log_info("adapter type: %d\n", chip->proc_data.adapter_type);
#line 1575
	mca_log_info("uuid: 0x%x, 0x%x, 0x%x, 0x%x\n",
		chip->proc_data.uuid[0], chip->proc_data.uuid[1], chip->proc_data.uuid[2], chip->proc_data.uuid[3]);

	return 0;
}

static int nuvolta_1652_set_adapter_voltage(int voltage, void *data)
{
	int ret = 0;
	bool status = true;
	u8 vol_h = 0, vol_l = 0;

	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	mutex_lock(&chip->data_transfer_lock);

	if ((voltage < ADAPTER_VOL_MIN_MV) || (voltage > ADAPTER_VOL_MAX_MV))
		voltage = ADAPTER_VOL_DEFAULT_MV;

	vol_h = (u8)(voltage >> 8);
	vol_l = (u8)(voltage & 0xFF);

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_TRANSMIT_PACKET, REG_RX_SENT_CMD);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, ADAPTER_VOL_PACKET_LENGTH, REG_RX_SENT_DATA1);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, ADAPTER_VOL_SET_TYPE, REG_RX_SENT_DATA2);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, TRANS_DATA_LENGTH_3BYTE, REG_RX_SENT_DATA3);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_FASTCHG_SET_TX_VOLTAGE, REG_RX_SENT_DATA4);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, vol_l, REG_RX_SENT_DATA5);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, vol_h, REG_RX_SENT_DATA6);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, ADAPTER_VOL_TRIGGER_RX, REG_RX_INT_0);
	if (ret < 0)
		goto exit;

#line 1633
	mca_log_info("adapter voltage setted: %d\n", voltage);

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_get_tx_uuid(u8 *uuid, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (chip->proc_data.power_good_flag)
		memcpy(uuid, chip->proc_data.uuid, sizeof(chip->proc_data.uuid));
	else
		return -1;

	return 0;
}

static void nuvolta_1652_set_bpp_plus_fod(struct nuvolta_1652_chg *chip, struct fod_params_t *params_base)
{
	u8 params_offset;
	int buffer_length;
	struct params_t params_buffer[FOD_PARAMS_MAX_LENGTH];
	bool status = true;
	int ret = 0;
	struct params_t *params_ptr = chip->magnetic_case_flag ? params_base->params_mag : params_base->params;

	mutex_lock(&chip->data_transfer_lock);
	params_offset = 0;
	buffer_length = sizeof(struct params_t) * FOD_PARAMS_MAX_LENGTH;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_FOD_SET, REG_RX_SENT_CMD); //cmd 0x98
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, BPP_PLUS_FOD_SET_CMD_LENGTH, REG_RX_SENT_DATA1); //cmd length:13
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, params_base->type, REG_RX_SENT_DATA2); //params type
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, params_offset, REG_RX_SENT_DATA3); //params offset:0
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, buffer_length, REG_RX_SENT_DATA4); //params length:max 10 one time
	if (ret < 0)
		goto exit;

	memcpy((void *)params_buffer, (const void *)&params_ptr[params_offset], buffer_length);

	ret = rx1652_write_buffer(chip, (u8 *)params_buffer, REG_RX_SENT_DATA5, buffer_length); //params length:max 10 one time
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, FOD_SET_TRIGGER_RX, REG_RX_INT_0); //triger int to rx
	if (ret < 0)
		goto exit;

#line 1699
	mca_log_info( "set bpp plus fod type: %d\n", params_base->type);
	msleep(20); //wait rx save for params

exit:
	mutex_unlock(&chip->data_transfer_lock);
}

static void nuvolta_1652_set_fod(struct nuvolta_1652_chg *chip, struct fod_params_t *params_base)
{
	u8 params_offset;
	u8 count = 0;
	int buffer_length;
	struct params_t params_buffer[FOD_PARAMS_MAX_LENGTH];
	int i = 0;
	bool status = true;
	int ret = 0;
	struct params_t *params_ptr = chip->magnetic_case_flag ? params_base->params_mag : params_base->params;

	mutex_lock(&chip->data_transfer_lock);

	for (i = 0; i < params_base->length; ++i)
#line 1720
		mca_log_info("params[%d]: %d, %d\n", i, params_ptr[i].gain, params_ptr[i].offset);

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_FOD_SET, REG_RX_SENT_CMD); //cmd 0x98
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, params_base->type, REG_RX_SENT_DATA2); //params type
	if (ret < 0)
		goto exit;

	params_offset = 0;
	while (params_offset < params_base->length) {
		count = params_offset + 5 <= params_base->length ? 5 : params_base->length - params_offset;
		buffer_length = sizeof(struct params_t) * count;

		ret = rx1652_write(chip, buffer_length + 3, REG_RX_SENT_DATA1); // cmd length
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, params_offset, REG_RX_SENT_DATA3); // params offset
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, buffer_length, REG_RX_SENT_DATA4); // buffer_length
		if (ret < 0)
			goto exit;

		memcpy((void *)params_buffer, (const void *)&params_ptr[params_offset], buffer_length);

		ret = rx1652_write_buffer(chip, (u8 *)params_buffer, REG_RX_SENT_DATA5, buffer_length); //params length:max 10 one time
		if (ret < 0)
			goto exit;

		ret = rx1652_write(chip, buffer_length + 5, REG_RX_INT_0); // triger int to rx
		if (ret < 0)
			goto exit;

		msleep(20); // wait rx save fod params
		params_offset += count;
	}

#line 1765
	mca_log_info("set fod type: %d\n", params_base->type);
exit:
	mutex_unlock(&chip->data_transfer_lock);
}

static int nuvolta_1652_set_fod_params(int value, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int i = 0;
	bool found = true;
	int uuid = 0;

	uuid |= chip->proc_data.uuid[0] << 24;
	uuid |= chip->proc_data.uuid[1] << 16;
	uuid |= chip->proc_data.uuid[2] << 8;
	uuid |= chip->proc_data.uuid[3];

	for (i = 0; i < chip->fod_params_size; i++) {
		found = true;
		if (uuid != chip->fod_params[i].uuid) {
			found = false;
			continue;
		}
		/* found fod by uuid */
		if (found) {
#line 1790
			mca_log_info("uuid: 0x%x,0x%x,0x%x,0x%x\n", chip->proc_data.uuid[0],
				chip->proc_data.uuid[1], chip->proc_data.uuid[2], chip->proc_data.uuid[3]);
			if (uuid == 0x09010305)
				i += (chip->proc_data.adapter_type < ADAPTER_VOICE_BOX);
			nuvolta_1652_set_fod(chip, &chip->fod_params[i]);
			return 0;
		}
	}

	if (((chip->proc_data.adapter_type == ADAPTER_QC3) || (chip->proc_data.adapter_type == ADAPTER_PD)) &&
		(!chip->proc_data.epp)) {
		found = true;
#line 1802
		mca_log_info("fod bpp plus\n");
		nuvolta_1652_set_bpp_plus_fod(chip, &chip->fod_params_bpp_plus);
	} else if (chip->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3) {
		found = true;
#line 1806
		mca_log_info("fod epp+ default\n");
		nuvolta_1652_set_fod(chip, &chip->fod_params_default);
	}

	if (!found)
#line 1811
		mca_log_info("can not found fod params, uuid: 0x%x,0x%x,0x%x,0x%x\n",
			chip->proc_data.uuid[0], chip->proc_data.uuid[1], chip->proc_data.uuid[2], chip->proc_data.uuid[3]);

	return 0;
}

static int nuvolta_1652_get_rx_fastcharge_status(u8 *fc_flag, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	u8 fastchg_result = 0;
	bool status = true;
	int ret = 0;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return fastchg_result;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return fastchg_result;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return fastchg_result;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return fastchg_result;
	}

	ret = rx1652_read(chip, &fastchg_result, RX_FASTCHG_RESULT);
	if (ret < 0)
		return fastchg_result;

	*fc_flag = fastchg_result;

	chip->proc_data.fc_flag = fastchg_result;

#line 1851
	mca_log_info("fastch result: %d\n", *fc_flag);

	return ret;
}

static int nuvolta_1652_receive_transparent_data(u8 *rcv_value, int buff_len,
		int *rcv_len, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	int i;
	u8 rsp_len = 0;

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		return -1;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return -1;

	ret = nuvolta_1652_check_buffer_ready(chip);
	if (!ret) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return -1;
		ret = nuvolta_1652_check_buffer_ready(chip);
		if (!ret)
			return -1;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, RECEIVE_DATA_MAX_COUNT);
	if (ret < 0)
		return ret;

	*rcv_len = read_buf[RECEIVE_DATA_LENGTH_SHIFT];
#line 1889
	mca_log_info("receive_transparent_data data_length=%d\n", *rcv_len);

	rsp_len = read_buf[RECEIVE_DATA_LENGTH_SHIFT];
	rsp_len = (rsp_len >> 4) + (rsp_len & 0x0F);
	if (rsp_len > buff_len) {
#line 1894
		mca_log_info("receive_transparent_data buffer overflow\n");
		*rcv_len = 0;
		return -1;
	}

	for (i = 0; i < rsp_len; i++) {
		rcv_value[i] = read_buf[RECEIVE_DATA_SHIFT + i];
#line 1901
		mca_log_info("receive_transparent_data i=%d, data[i]=0x%x\n", i, rcv_value[i]);
	}

	return ret;
}

static int nuvolta_1652_send_transparent_data(u8 *send_data, u8 length, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	u8 i;
	int ret = 0;

	mutex_lock(&chip->data_transfer_lock);

#line 1915
	mca_log_info("data[0] = 0x%x data[1] = 0x%x data[2] = 0x%x, length=%d\n",
		send_data[0], send_data[1], send_data[2], length);

	msleep(20);

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_TRANSMIT_PACKET, REG_RX_SENT_CMD);
	if (ret < 0) {
#line 1926
		mca_log_err("FAILED!\n");
		goto exit;
	}

	ret = rx1652_write(chip, length, REG_RX_SENT_DATA1);
	if (ret < 0) {
#line 1932
		mca_log_err("write lenth FAILED!\n");
		goto exit;
	}

	for (i = 0; i < length; i++) {
		ret = rx1652_write(chip, send_data[i], (REG_RX_SENT_DATA2 + i));
		if (ret < 0) {
#line 1939
			mca_log_err("write data FAILED!\n");
			goto exit;
		}

#line 1943
		mca_log_info("i=%d, send_data[0+i]=0x%x, (0x0002+i)=0x%x\n",
						i, send_data[i], (REG_RX_SENT_DATA2 + i));
	}

	ret = rx1652_write(chip, (length + 2), REG_RX_INT_0);
	if (ret < 0) {
#line 1949
		mca_log_err("write trigger FAILED!\n");
		goto exit;
	}

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_rcv_product_test_cmd(u8 *rev_data, int *length, void *data)
{
	int ret = 0;
	u8 data_length;
	u8 i;
	u8 max_data_length = 15;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	ret = nuvolta_1652_check_rx_ready(chip);
	if (ret < 0)
		return ret;

	ret = rx1652_write(chip, 0x88, 0x0062);
	if (ret < 0)
		return ret;

	ret = nuvolta_1652_check_buffer_ready(chip);
	if (!ret) {
		ret = rx1652_write(chip, 0x88, 0x0062);
		if (ret < 0)
			return ret;
		ret = nuvolta_1652_check_buffer_ready(chip);
		if (!ret)
			return ret;
	}

	u8 read_buf[16];

	ret = rx1652_read_buffer(chip, read_buf, (0x1200 + 0x38), 16);
	if (ret < 0)
		return ret;

	data_length = read_buf[0];
	data_length = data_length > max_data_length ? max_data_length : data_length;
	*length = data_length;

#line 1994
	mca_log_info("data_length=%d\n", data_length);

	for (i = 0; i < data_length; i++) {
		rev_data[i] = read_buf[i+1];
#line 1998
		mca_log_info("i=%d, data[i]=0x%x\n", i, rev_data[i]);
	}

	return ret;
}

static int nuvolta_1652_process_factory_cmd(u8 cmd, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 send_data[8];
	u8 data_h;
	u8 data_l;
	int rx_iout;
	int rx_vout;
	u8 index = 0;

	switch (cmd) {
	case FACTORY_TEST_CMD_RX_IOUT:
		ret = nuvolta_1652_get_iout(&rx_iout, chip);
		data_h = ((uint32_t)rx_iout & 0x00ff);
		data_l = ((uint32_t)rx_iout & 0xff00) >> 8;
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_IOUT;
		send_data[index++] = data_h;
		send_data[index++] = data_l;
#line 2026
		mca_log_err("nuvolta_1652_factory_test --rx_iout--0x%x,0x%x iout=%d\n", data_h, data_l, rx_iout);
		break;
	case FACTORY_TEST_CMD_RX_VOUT:
		ret = nuvolta_1652_get_vout(&rx_vout, chip);
		data_h = ((uint32_t)rx_vout & 0x00ff);
		data_l = ((uint32_t)rx_vout & 0xff00) >> 8;
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_VOUT;
		send_data[index++] = data_h;
		send_data[index++] = data_l;
#line 2038
		mca_log_err("nuvolta_1652_factory_test --rx_vout--0x%x,0x%x vout=%d\n", data_h, data_l, rx_vout);
		break;
	case FACTORY_TEST_CMD_RX_FW_ID:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_5BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_FW_ID;
		send_data[index++] = 0x0;
		send_data[index++] = 0x0;
		send_data[index++] = chip->wls_fw_data->fw_rx_id;
		send_data[index++] = chip->wls_fw_data->fw_tx_id;
#line 2049
		mca_log_err("nuvolta_1652_factory_test --fw_version--0x%x 0x%x\n", chip->wls_fw_data->fw_rx_id, chip->wls_fw_data->fw_tx_id);
		break;
	case FACTORY_TEST_CMD_RX_CHIP_ID:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_3BYTE;
		send_data[index++] = FACTORY_TEST_CMD_RX_CHIP_ID;
		send_data[index++] = 16;
		send_data[index++] = 51;
#line 2058
		mca_log_err("nuvolta_1652_factory_test --rx_chip_id--0x%x0x%x\n", chip->wls_fw_data->hw_id_h, chip->wls_fw_data->hw_id_l);
		break;
	case FACTORY_TEST_CMD_ADAPTER_TYPE:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_2BYTE;
		send_data[index++] = FACTORY_TEST_CMD_ADAPTER_TYPE;
		send_data[index++] = chip->proc_data.adapter_type;
#line 2066
		mca_log_err("nuvolta_1652_factory_test --usb_type--%d\n", chip->proc_data.adapter_type);
		break;
	case FACTORY_TEST_CMD_REVERSE_REQ:
		index = 0;
		send_data[index++] = TX_ACTION_NO_REPLY;
		send_data[index++] = TRANS_DATA_LENGTH_1BYTE;
		send_data[index++] = FACTORY_TEST_CMD_REVERSE_REQ;
#line 2073
		mca_log_err("[ factory reverse test] receive request\n");
		break;
	default:
		return -1;
	}
	ret = nuvolta_1652_send_transparent_data(send_data, index, chip);
	return ret;
}

static int nuvolta_1652_get_ss_voltage(int *ss_voltage, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	u8 read_buf[4];
	int ret = 0;

	if (!chip->proc_data.power_good_flag) {
		*ss_voltage = 0;
		return ret;
	}

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		return -1;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0) {
		*ss_voltage = 0;
		return -1;
	}

	ret = nuvolta_1652_check_buffer_ready(chip);
	if (!ret) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0) {
			*ss_voltage = 0;
			return -1;
		}
		ret = nuvolta_1652_check_buffer_ready(chip);
		if (!ret)
			return -1;
	}

	ret = rx1652_read_buffer(chip, read_buf, RX_SS_VOLTAGE, 2);
	if (ret < 0)
		return ret;

	*ss_voltage = ((int)read_buf[1] << 8) + (int)read_buf[0];
	chip->proc_data.ss_voltage = *ss_voltage;
#line 2122
	mca_log_info("ss_voltage = %d\n", *ss_voltage);

	return ret;
}

static int nuvolta_1652_set_rx_offset(int rx_offset, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;

	chip->fake_rx_offset = rx_offset;
	return ret;
}

static int nuvolta_1652_get_rx_offset(int *rx_offset, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;

	*rx_offset = RX_OFFSET_GOOD;
	if (!chip->proc_data.power_good_flag)
		return ret;

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		return -1;

	if (chip->fake_rx_offset != RX_OFFSET_GOOD) {
		*rx_offset = RX_OFFSET_BAD;
#line 2151
		mca_log_info("rx_offset = %d\n", *rx_offset);
	}

	if (chip->proc_data.ss_voltage > 0 && chip->proc_data.ss_voltage < RX_OFFSET_THRESHOLD)
		*rx_offset = RX_OFFSET_BAD;

#line 2157
	mca_log_info("rx_offset = %d, ss_voltage = %d", *rx_offset, chip->proc_data.ss_voltage);
	return ret;
}

static int nuvolta_1652_set_rx_sleep_mode(int sleep_for_dam, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;

	if (sleep_for_dam) {
		ret = rx1652_write(chip, 0x03, 0x0063);
		if (ret < 0)
			return -1;
	}
	return ret;
}

static int nuvolta_1652_do_renego(u8 max_power, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;

	mutex_lock(&chip->data_transfer_lock);

#line 2181
	mca_log_info("max_power = %d\n", max_power);

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		goto exit;

	ret = rx1652_write(chip, RX_CMD_RENEGO_SET, REG_RX_SENT_CMD);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, RENEGO_LENGTH, REG_RX_SENT_DATA1);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, max_power, REG_RX_SENT_DATA2);
	if (ret < 0)
		goto exit;

	ret = rx1652_write(chip, RENEGO_TRIGGER_RX, REG_RX_INT_0);
	if (ret < 0)
		goto exit;

exit:
	mutex_unlock(&chip->data_transfer_lock);
	return ret;
}

static int nuvolta_1652_set_parallel_charge(bool parallel, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	chip->proc_data.parallel_charge = parallel;
#line 2213
	mca_log_info("set_parallel_charge: %d\n", parallel);

	return 0;
}

static int nuvolta_1652_get_vout_setted(int *vout_setted, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	*vout_setted = chip->proc_data.vout_setted;
#line 2223
	mca_log_info("vout_setted is %d\n", *vout_setted);

	return 0;
}

static int nuvolta_1652_get_poweroff_err_code(u8 *err_code, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	rx1652_read(chip, err_code, RX_POWER_OFF_ERR);

	return 0;
}

static int nuvolta_1652_get_project_vendor(int *project_vendor, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	*project_vendor = chip->project_vendor;
#line 2242
	mca_log_info("project_vendor is %d\n", *project_vendor);

	return 0;
}

static int nuvolta_1652_send_tx_q_value(u8 value, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 send_value[4] = {0x01, 0x28, 0x62, 0};

	if (!chip->proc_data.power_good_flag)
		return -1;

	send_value[3] = value;

	ret = nuvolta_1652_send_transparent_data(send_value, ARRAY_SIZE(send_value), chip);
#line 2259
	mca_log_info("{0x%02x, 0x%02x, 0x%02x, 0x%02x}\n",
					send_value[0], send_value[1], send_value[2], send_value[3]);
	return ret;
}

#define SUPER_TX_FAN_SPEED_MIN_PERCENT 0
#define SUPER_TX_FAN_SPEED_MAX_PERCENT 10
static int nuvolta_1652_send_tx_fan_speed(int value, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	u8 send_value[5] = {0x00, 0x38, 0x63, 0, 0};

	if (!chip->proc_data.power_good_flag)
		return -1;

	if (value < SUPER_TX_FAN_SPEED_MIN_PERCENT || value > SUPER_TX_FAN_SPEED_MAX_PERCENT)
		return -1;

	if (chip->proc_data.tx_speed == value)
		return ret;

	send_value[3] = (u8)value;
	chip->proc_data.tx_speed = value;

	ret = nuvolta_1652_send_transparent_data(send_value, ARRAY_SIZE(send_value), chip);
#line 2285
	mca_log_info("send_tx_fan_speed_request = %d", chip->proc_data.tx_speed);
	return ret;
}

static int nuvolta_1652_get_tx_fan_speed(int *value, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	*value = (int)chip->proc_data.tx_speed;
#line 2294
	mca_log_info("tx_fan_speed is %d\n", *value);

	return 0;
}

static int nuvolta_1652_check_firmware_state(bool *update, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	int ret = 0;
	bool status = true;

	if (!chip->proc_data.power_good_flag)
		return -1;

	ret = nuvolta_1652_check_rx_ready(chip);
	if (!ret)
		goto err;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		goto err;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			goto err;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	{
		u8 read_buf[128];

		ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, 30);
		if (ret < 0)
			goto err;

		chip->wls_fw_data->fw_boot_id = read_buf[0];
		chip->wls_fw_data->fw_rx_id = read_buf[1];
		chip->wls_fw_data->fw_tx_id = read_buf[2];
	}

#line 2336
	mca_log_info("ic fw version: %02x.%02x.%02x\n",
		chip->wls_fw_data->fw_boot_id,
		chip->wls_fw_data->fw_rx_id,
		chip->wls_fw_data->fw_tx_id);

	if ((chip->wls_fw_data->fw_boot_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_boot_id >= nuvolta_1652_get_boot_fw_version(chip)) &&
		(chip->wls_fw_data->fw_rx_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_rx_id >= nuvolta_1652_get_rx_fw_version(chip)) &&
		(chip->wls_fw_data->fw_tx_id != CRC_CHECK_ERR_VER) &&
		(chip->wls_fw_data->fw_tx_id >= nuvolta_1652_get_tx_fw_version(chip))) {
#line 2347
		mca_log_info("no need update\n");
		*update = false;
	} else {
#line 2350
		mca_log_info("need update\n");
		*update = true;
	}

	return ret;

err:
#line 2357
	mca_log_err("i2c error!\n");
	*update = false;
	return ret;
}

static int nuvolta_1652_set_debug_fod_params(void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	struct fod_params_t fod_params;
	int index = chip->wls_debug_one_fod_index;
	int i = 0, k = 0;
	int uuid = 0;
	bool found = false;

	if (!chip->proc_data.power_good_flag) {
#line 2372
		mca_log_err("power good flag is false\n");
		return -1;
	}

	if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_ALL_DIRECTLY) {
		fod_params.type = chip->wls_debug_all_fod_params->type;
		fod_params.length = chip->wls_debug_all_fod_params->length;
		for (i = 0; i < fod_params.length; i++) {
			fod_params.params[i].gain = chip->wls_debug_all_fod_params->params[i].gain;
			fod_params.params_mag[i].gain = chip->wls_debug_all_fod_params->params[i].gain;
			fod_params.params[i].offset = chip->wls_debug_all_fod_params->params[i].offset;
			fod_params.params_mag[i].offset = chip->wls_debug_all_fod_params->params[i].offset;
		}
		nuvolta_1652_set_fod(chip, &fod_params);
		return 0;
	}

	uuid |= chip->proc_data.uuid[0] << 24;
	uuid |= chip->proc_data.uuid[1] << 16;
	uuid |= chip->proc_data.uuid[2] << 8;
	uuid |= chip->proc_data.uuid[3];
	//check all params
	for (i = 0; i < chip->fod_params_size; i++) {
		found = true;
		// uuid checking
		if (uuid != chip->fod_params[i].uuid) {
			found = false;
			continue;
		}
		// found
		if (found) {
#line 2403
			mca_log_info("uuid: 0x%x,0x%x,0x%x,0x%x\n", chip->proc_data.uuid[0],
				chip->proc_data.uuid[1], chip->proc_data.uuid[2], chip->proc_data.uuid[3]);
			if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_EPP_ALL &&
				chip->fod_params[i].length == chip->wls_debug_all_fod_params->length) {
				for (k = 0; k < chip->fod_params[i].length; k++) {
					chip->fod_params[i].params[k].gain = chip->wls_debug_all_fod_params->params[k].gain;
					chip->fod_params[i].params_mag[k].gain = chip->wls_debug_all_fod_params->params[k].gain;
					chip->fod_params[i].params[k].offset = chip->wls_debug_all_fod_params->params[k].offset;
					chip->fod_params[i].params_mag[k].offset = chip->wls_debug_all_fod_params->params[k].offset;
				}
			} else if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_EPP_ONE &&
						index < chip->fod_params[i].length) {
				chip->fod_params[i].params[index].gain = chip->wls_debug_one_fod_param.gain;
				chip->fod_params[i].params_mag[index].gain = chip->wls_debug_one_fod_param.gain;
				chip->fod_params[i].params[index].offset = chip->wls_debug_one_fod_param.offset;
				chip->fod_params[i].params_mag[index].offset = chip->wls_debug_one_fod_param.offset;
			}
			nuvolta_1652_set_fod(chip, &chip->fod_params[i]);
			return 0;
		}
	}

	if (((chip->proc_data.adapter_type == ADAPTER_QC3) || (chip->proc_data.adapter_type == ADAPTER_PD)) &&
		(!chip->proc_data.epp)) {
		found = true;
#line 2428
		mca_log_info("fod bpp plus\n");
		nuvolta_1652_set_bpp_plus_fod(chip, &chip->fod_params_bpp_plus);
	} else if (chip->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3) {
		found = true;
#line 2432
		mca_log_info("fod epp+ default\n");
		if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_EPP_ALL &&
			chip->fod_params_default.length == chip->wls_debug_all_fod_params->length) {
			for (k = 0; k < chip->fod_params_default.length; k++) {
				chip->fod_params_default.params[k].gain = chip->wls_debug_all_fod_params->params[k].gain;
				chip->fod_params_default.params_mag[k].gain = chip->wls_debug_all_fod_params->params[k].gain;
				chip->fod_params_default.params[k].offset = chip->wls_debug_all_fod_params->params[k].offset;
				chip->fod_params_default.params_mag[k].offset = chip->wls_debug_all_fod_params->params[k].offset;
			}
		} else if (chip->wls_debug_set_fod_type == WLS_DEBUG_SET_FOD_EPP_ONE &&
					index < chip->fod_params_default.length) {
			chip->fod_params_default.params[index].gain = chip->wls_debug_one_fod_param.gain;
			chip->fod_params_default.params_mag[index].gain = chip->wls_debug_one_fod_param.gain;
			chip->fod_params_default.params[index].offset = chip->wls_debug_one_fod_param.offset;
			chip->fod_params_default.params_mag[index].offset = chip->wls_debug_one_fod_param.offset;
		}
		nuvolta_1652_set_fod(chip, &chip->fod_params_default);
	}

	if (!found)
#line 2452
		mca_log_info("can not found fod params, uuid: 0x%x,0x%x,0x%x,0x%x\n",
			chip->proc_data.uuid[0], chip->proc_data.uuid[1], chip->proc_data.uuid[2], chip->proc_data.uuid[3]);

	return 0;
}

static int nuvolta_1652_set_debug_fod(int *args, int count, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;
	u8 index = 0;

	switch (args[0]) {
	case DEBUG_SET_ONE_EPP_FOD:
#line 2465
		mca_log_info("set one epp fod\n");
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_EPP_ONE;
		index = args[1];
		chip->wls_debug_one_fod_index = index;
		chip->wls_debug_one_fod_param.gain = args[2];
		chip->wls_debug_one_fod_param.offset = args[3];
		nuvolta_1652_set_debug_fod_params(chip);
		break;
	case DEBUG_SET_ALL_EPP_FOD:
#line 2474
		mca_log_info("set all epp fod\n");
		if (chip->wls_debug_all_fod_params == NULL)
			chip->wls_debug_all_fod_params = kmalloc(sizeof(struct fod_params_t), GFP_KERNEL);
		chip->wls_debug_all_fod_params->length = count / 2;
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_EPP_ALL;
		for (index = 0; index < chip->wls_debug_all_fod_params->length; ++index) {
			chip->wls_debug_all_fod_params->params[index].gain = args[index * 2 + 1];
			chip->wls_debug_all_fod_params->params[index].offset = args[index * 2 + 2];
		}
			nuvolta_1652_set_debug_fod_params(chip);
		break;
	case DEBUG_SET_ALL_FOD:
#line 2486
		mca_log_info("wls debug set all fod\n");
		if (chip->wls_debug_all_fod_params == NULL)
			chip->wls_debug_all_fod_params = kmalloc(sizeof(struct fod_params_t), GFP_KERNEL);
		chip->wls_debug_all_fod_params->type = args[1];
		chip->wls_debug_all_fod_params->length = count / 2 - 1;
		chip->wls_debug_set_fod_type = WLS_DEBUG_SET_FOD_ALL_DIRECTLY;
		for (index = 0; index < chip->wls_debug_all_fod_params->length; ++index) {
			chip->wls_debug_all_fod_params->params[index].gain = args[index * 2 + 2];
			chip->wls_debug_all_fod_params->params[index].offset = args[index * 2 + 3];
		}
		nuvolta_1652_set_debug_fod_params(chip);
		break;
	default:
#line 2499
		mca_log_err("not support debug_fod_type, return\n");
		break;
	}

	return 0;
}

static int nuvolta_1652_get_debug_fod_type(WLS_DEBUG_SET_FOD_TYPE *type, void *data)
{
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	*type = chip->wls_debug_set_fod_type;

	return 0;
}

static int nuvolta_1652_get_trx_isense(int *isense, void *data)
{
	int ret = 0;
	bool status = true;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return ret;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, RECEIVE_DATA_MAX_COUNT);
	if (ret < 0)
		return ret;

	*isense = read_buf[TRX_ISENSE_HIGH] * 256 + read_buf[TRX_ISENSE_LOW];

#line 2547
	mca_log_info("trx isense: %d\n", *isense);

	return ret;
}

static int nuvolta_1652_get_trx_vrect(int *vrect, void *data)
{
	int ret = 0;
	bool status = true;
	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	status = nuvolta_1652_check_rx_ready(chip);
	if (!status)
		return ret;

	ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
	if (ret < 0)
		return ret;

	status = nuvolta_1652_check_buffer_ready(chip);
	if (!status) {
		ret = rx1652_write(chip, RX_CMD_START_READ, REG_RX_INT_2);
		if (ret < 0)
			return ret;
		status = nuvolta_1652_check_buffer_ready(chip);
		if (!status)
			return 0;
	}

	u8 read_buf[128];

	ret = rx1652_read_buffer(chip, read_buf, RX_DATA_INFO, RECEIVE_DATA_MAX_COUNT);
	if (ret < 0)
		return ret;

	*vrect = read_buf[TRX_VRECT_HIGH] * 256 + read_buf[TRX_VRECT_LOW];

#line 2584
	mca_log_info("trx vrect: %d\n", *vrect);

	return ret;
}

static __maybe_unused int nuvolta_1652_set_cp_status(int status, void *data)
{
  	int ret = 0;
  	bool rdy = true;
  	u8 cp_status = 0;
  	struct nuvolta_1652_chg *chip = (struct nuvolta_1652_chg *)data;

	if (!chip->proc_data.power_good_flag) {
  		mca_log_info("power good disonline, don't set cp_status\n");
  		return 0;
  	}
  	cp_status = (u8)(status & 0xFF);
  	rdy = nuvolta_1652_check_rx_ready(chip);
  	if (!rdy)
  		return 0;
  	ret = rx1652_write(chip, 0xB8, 0x0000);
  	if (ret < 0)
  		return ret;
  	ret = rx1652_write(chip, 0x01, 0x0001);
  	if (ret < 0)
  		return ret;
  	ret = rx1652_write(chip, cp_status, 0x0002);
  	if (ret < 0)
  		return ret;
  	ret = rx1652_write(chip, 0x03, 0x0060);
  	if (ret < 0)
  		return ret;
  	mca_log_info("set cp status: %d\n", status);
  	return ret;
}

static struct platform_class_wireless_ops nuvolta_1652_wls_ops = {
	.wls_enable_reverse_chg = nuvolta_1652_enable_reverse_chg,
	.wls_is_present = nuvolta_1652_is_present,
	.wls_set_vout = nuvolta_1652_set_vout,
	.wls_get_vout = nuvolta_1652_get_vout,
	.wls_get_iout = nuvolta_1652_get_iout,
	.wls_get_vrect = nuvolta_1652_get_vrect,
	.wls_get_temp = nuvolta_1652_get_temp,
	.wls_get_tx_adapter = nuvolta_1652_get_tx_adapter,
	.wls_get_tx_adapter_by_i2c = nuvolta_1652_get_tx_adapter_by_i2c,
	.wls_set_enable_mode = nuvolta_1652_set_enable_mode,
	.wls_is_car_adapter = nuvolta_1652_is_car_adapter,
	.wls_set_fw_bin = nuvolta_1652_set_fw_bin,
	.wls_get_rx_rtx_mode = nuvolta_1652_get_rx_rtx_mode,
	.wls_set_input_current_limit = nuvolta_1652_set_input_current_limit,
	.wls_get_rx_int_flag = nuvolta_1652_get_rx_int_flag,
	.wls_get_rx_power_mode = nuvolta_1652_get_rx_power_mode,
	.wls_get_tx_max_power = nuvolta_1652_get_tx_max_power,
	.wls_get_auth_value = nuvolta_1652_get_auth_value,
	.wls_set_adapter_voltage = nuvolta_1652_set_adapter_voltage,
	.wls_get_tx_uuid = nuvolta_1652_get_tx_uuid,
	.wls_set_fod_params = nuvolta_1652_set_fod_params,
	.wls_get_rx_fastcharge_status = nuvolta_1652_get_rx_fastcharge_status,
	.wls_receive_transparent_data = nuvolta_1652_receive_transparent_data,
	.wls_send_transparent_data = nuvolta_1652_send_transparent_data,
	.wls_get_ss_voltage = nuvolta_1652_get_ss_voltage,
	.wls_do_renego = nuvolta_1652_do_renego,
	.wls_set_parallel_charge = nuvolta_1652_set_parallel_charge,
	.wls_get_vout_setted = nuvolta_1652_get_vout_setted,
	.wls_get_poweroff_err_code = nuvolta_1652_get_poweroff_err_code,
	.wls_get_project_vendor = nuvolta_1652_get_project_vendor,
	.wls_get_fw_version = nuvolta_1652_get_firmware_version,
	.wls_check_i2c_is_ok = nuvolta_1652_ops_check_i2c,
	.wls_enable_rev_fod = nuvolta_1652_enable_reverse_fod,
	.wls_send_tx_q_value = nuvolta_1652_send_tx_q_value,
	.wls_download_fw_from_bin = nuvolta_1652_download_fw_from_bin,
	.wls_erase_fw = nuvolta_1652_erase_fw_data,
	.wls_get_fw_version_check = nuvolta_1652_get_fw_version,
	.wls_download_fw = nuvolta_1652_download_fw,
	.wls_set_confirm_data = nuvolta_1652_set_confirm_data,
	.wls_receive_test_cmd = nuvolta_1652_rcv_product_test_cmd,
	.wls_process_factory_cmd = nuvolta_1652_process_factory_cmd,
	.wls_get_hall_gpio_status = nuvolta_1652_get_hall_gpio_status,
	.wls_check_firmware_state = nuvolta_1652_check_firmware_state,
	.wls_set_debug_fod = nuvolta_1652_set_debug_fod,
	.wls_get_debug_fod_type = nuvolta_1652_get_debug_fod_type,
	.wls_set_debug_fod_params = nuvolta_1652_set_debug_fod_params,
	.wls_set_tx_fan_speed = nuvolta_1652_send_tx_fan_speed,
	.wls_get_tx_fan_speed = nuvolta_1652_get_tx_fan_speed,
	.wls_get_rx_offset = nuvolta_1652_get_rx_offset,
	.wls_set_rx_offset = nuvolta_1652_set_rx_offset,
	.wls_set_rx_sleep_mode = nuvolta_1652_set_rx_sleep_mode,
	.wls_get_trx_isense = nuvolta_1652_get_trx_isense,
	.wls_get_trx_vrect = nuvolta_1652_get_trx_vrect,
	.wls_get_magnetic_case_flag = nuvolta_1652_get_magnetic_case_flag,
};

//-------------------------irq & work---------------------------
static int_map_t rx_irq_map[] = {
	DECL_INTERRUPT_MAP(RX_IRQ_LDO_ON, RX_INT_LDO_ON),
	DECL_INTERRUPT_MAP(RX_IRQ_FAST_CHARGE, RX_INT_FAST_CHARGE),
	DECL_INTERRUPT_MAP(RX_IRQ_AUTHEN_FINISH, RX_INT_AUTHEN_FINISH),
	DECL_INTERRUPT_MAP(RX_IRQ_RENEGO_DONE, RX_INT_RENEGO_DONE),
	DECL_INTERRUPT_MAP(RX_IRQ_ALARM_SUCCESS, RX_INT_ALARM_SUCCESS),
	DECL_INTERRUPT_MAP(RX_IRQ_ALARM_FAIL, RX_INT_ALARM_FAIL),
	DECL_INTERRUPT_MAP(RX_IRQ_OOB_GOOD, RX_INT_OOB_GOOD),
	DECL_INTERRUPT_MAP(RX_IRQ_RPP, RX_INT_RPP),
	DECL_INTERRUPT_MAP(RX_IRQ_TRANSPARENT_SUCCESS, RX_INT_TRANSPARENT_SUCCESS),
	DECL_INTERRUPT_MAP(RX_IRQ_TRANSPARENT_FAIL, RX_INT_TRANSPARENT_FAIL),
	DECL_INTERRUPT_MAP(RX_IRQ_FACTORY_TEST, RX_INT_FACTORY_TEST),
	DECL_INTERRUPT_MAP(RX_IRQ_OCP_OTP_ALARM, RX_INT_OCP_OTP_ALARM),
	DECL_INTERRUPT_MAP(RX_IRQ_FIRST_AUTHEN, RX_INT_FIRST_AUTHEN),
	DECL_INTERRUPT_MAP(RX_IRQ_POWER_OFF, RX_INT_POWER_OFF),
	DECL_INTERRUPT_MAP(RX_IRQ_POWER_OFF, RX_INT_POWER_ON),
};

static int_map_t rtx_irq_map[] = {
	DECL_INTERRUPT_MAP(RTX_IRQ_PING, RTX_INT_PING),
	DECL_INTERRUPT_MAP(RTX_IRQ_GET_RX, RTX_INT_GET_RX),
	DECL_INTERRUPT_MAP(RTX_IRQ_CEP_TIMEOUT, RTX_INT_CEP_TIMEOUT),
	DECL_INTERRUPT_MAP(RTX_IRQ_EPT, RTX_INT_EPT),
	DECL_INTERRUPT_MAP(RTX_IRQ_PROTECTION, RTX_INT_PROTECTION),
	DECL_INTERRUPT_MAP(RTX_IRQ_GET_TX, RTX_INT_GET_TX),
	DECL_INTERRUPT_MAP(RTX_IRQ_REVERSE_TEST_READY, RTX_INT_REVERSE_TEST_READY),
	DECL_INTERRUPT_MAP(RTX_IRQ_REVERSE_TEST_DONE, RTX_INT_REVERSE_TEST_DONE),
	DECL_INTERRUPT_MAP(RTX_IRQ_FOD, RTX_INT_FOD),
};

static int nuvolta_1652_config_rx_int_flag(u16 int_flag)
{
	int i = 0;
	int flag = 0;

	for (i = 0; i < ARRAY_SIZE(rx_irq_map); i++) {
		if (rx_irq_map[i].irq_regval & int_flag)
			flag |= rx_irq_map[i].irq_flag;
	}

	return flag;
}

static int nuvolta_1652_config_rtx_int_flag(u16 int_flag)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(rtx_irq_map); i++) {
		if (rtx_irq_map[i].irq_regval & int_flag)
			return rtx_irq_map[i].irq_flag;
	}

	return RTX_INT_UNKNOWN;
}

static void nuvolta_1652_init_detect_work(struct work_struct *work)
{
	struct nuvolta_1652_chg *chip = container_of(work, struct nuvolta_1652_chg,	init_detect_work.work);
	int ret = 0;

	if (gpio_is_valid(chip->power_good_gpio)) {
		ret = gpio_get_value(chip->power_good_gpio);
#line 2708
		mca_log_info("init power good: %d\n", ret);
		if (ret) {
			nuvolta_1652_set_enable_mode(false, chip);
			usleep_range(125000, 150000);
			nuvolta_1652_set_enable_mode(true, chip);
		}
	}
}

static void nuvolta_1652_pg_det_work(struct work_struct *work)
{
	struct nuvolta_1652_chg *chip = container_of(work,
		struct nuvolta_1652_chg, pg_detect_work.work);
	int ret = 0;

	if (gpio_is_valid(chip->power_good_gpio)) {
		ret = gpio_get_value(chip->power_good_gpio);
		if (ret) {
			if (chip->proc_data.power_good_flag) {
#line 2727
				mca_log_info("wireless is already attached\n");
				return;
			}
#line 2730
			mca_log_info("power_good high, wireless attached\n");
			chip->proc_data.power_good_flag = true;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_WIRELESS_CONNECT, NULL);
		} else {
#line 2734
			mca_log_info("power_good low, wireless detached\n");
			chip->proc_data.power_good_flag = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_WIRELESS_DISCONNECT, NULL);
			memset(&chip->proc_data, 0, sizeof(chip->proc_data));
		}
	}
}

static irqreturn_t nuvolta_1652_power_good_handler(int irq, void *dev_id)
{
	struct nuvolta_1652_chg *chip = dev_id;

#line 2746
	mca_log_info("power_good detected\n");

	schedule_delayed_work(&chip->pg_detect_work, msecs_to_jiffies(0));

	return IRQ_HANDLED;
}

static void nuvolta_1652_interrupt_work(struct work_struct *work)
{
	struct nuvolta_1652_chg *chip = container_of(work,
		struct nuvolta_1652_chg, interrupt_work.work);

	u8 int_l = 0, int_h = 0;
	u16 int_val = 0;
	int ret = 0;

	mutex_lock(&chip->wireless_chg_int_lock);

	ret = rx1652_read(chip, &int_l, REG_RX_REV_CMD); //0x0020
	if (ret < 0)
#line 2766
		mca_log_err("read int 0x20 error\n");

	ret = rx1652_read(chip, &int_h, REG_RX_REV_DATA1); //0x0021
	if (ret < 0)
#line 2770
		mca_log_err("read int 0x21 error\n");

	int_val = (int_h << 8) | int_l;
	mca_log_info("int_flag: 0x%x\n", int_val);

	nuvolta_1652_get_rx_rtx_mode(&chip->proc_data.int_trx_mode, chip);

	if (chip->proc_data.int_trx_mode) {
		chip->proc_data.int_flag = nuvolta_1652_config_rtx_int_flag(int_val);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_REV_WIRELESS,
			MCA_EVENT_WIRELESS_INT_CHANGE, chip->proc_data.int_flag);
	} else {
		chip->proc_data.int_flag = nuvolta_1652_config_rx_int_flag(int_val);
#line 2784
		mca_log_info("int_flag: 0x%x\n", chip->proc_data.int_flag);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			MCA_EVENT_WIRELESS_INT_CHANGE, chip->proc_data.int_flag);
	}

	chip->proc_data.int_flag = 0;
	nuvolta_1652_clear_int(chip, int_l, int_h);

	mutex_unlock(&chip->wireless_chg_int_lock);
#line 2792
	mca_log_info("already clear int and unlock mutex\n");
}

static irqreturn_t nuvolta_1652_interrupt_handler(int irq, void *dev_id)
{
	struct nuvolta_1652_chg *chip = dev_id;

	schedule_delayed_work(&chip->interrupt_work, msecs_to_jiffies(0));

	return IRQ_HANDLED;
}

static void nuvolta_1652_hall_interrupt_work(struct work_struct *work)
{
	struct nuvolta_1652_chg *chip = container_of(work,
		struct nuvolta_1652_chg, hall_interrupt_work.work);
	int ret = 0;

	if (gpio_is_valid(chip->hall_int_gpio)) {
		ret = gpio_get_value(chip->hall_int_gpio);
		if (!ret) {
			if (chip->magnetic_case_flag) {
#line 2814
				mca_log_info("magnetic_case_flag is already attached\n");
				return;
			}
#line 2817
			mca_log_info("hall_interrupt low, magnetic_case attached\n");
			chip->hall_gpio_status = false;
			chip->magnetic_case_flag = true;
		} else {
#line 2821
			mca_log_info("hall_interrupt high, magnetic_case detached\n");
			chip->magnetic_case_flag = false;
			chip->hall_gpio_status = true;
		}
	}
}

static irqreturn_t nuvolta_1652_hall_interrupt_handler(int irq, void *dev_id)
{
	struct nuvolta_1652_chg *chip = dev_id;

#line 2832
	mca_log_info("hall_int detected\n");

	schedule_delayed_work(&chip->hall_interrupt_work, msecs_to_jiffies(0));

	return IRQ_HANDLED;
}

static int nuvolta_1652_parse_params(struct device_node *node,
	const char *name, struct params_t *params)
{
	int i, j;
	int len;
	u8 *idata = NULL;

	if (strcmp(name, "null") == 0) {
#line 2847
		mca_log_info("no need parse params\n");
		return -1;
	}

	len = mca_parse_dts_u8_count(node, name,
		DEFAULT_FOD_PARAM_LEN,
		PARAMS_T_MAX);	//temp
	if (len < 0) {
#line 2855
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	idata = kcalloc(len, sizeof(u8), GFP_KERNEL);

	mca_parse_dts_u8_array(node, name, idata, len);
	for (i = 0; i < len / 2; i++) {
		j = 2 * i;
		params[i].gain = idata[j];
		params[i].offset = idata[j + 1];
#line 2866
		mca_log_debug("[%d]params: gain:%d, offset:%d\n", i, params[i].gain, params[i].offset);
	}

	kfree(idata);
	idata = NULL;
	return 0;
}

static int nuvolta_1652_parse_fod_params(struct device_node *node,
	struct nuvolta_1652_chg *info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "fod_params",
		FOD_PARA_MAX_GROUP,
		FOD_PARA_MAX);
	if (array_len < 0) {
#line 2884
		mca_log_err("parse fod_params failed\n");
		return -1;
	}

	info->fod_params_size = array_len / FOD_PARA_MAX;

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "fod_params", i, &tmp_string))
			return -1;

		row = i / FOD_PARA_MAX;
		col = i % FOD_PARA_MAX;
		switch (col) {
		case FOD_PARA_TYPE:
			if (kstrtou8(tmp_string, 10, &info->fod_params[row].type))
				return -1;
			break;
		case FOD_PARA_LENGTH:
			if (kstrtou8(tmp_string, 10, &info->fod_params[row].length))
				return -1;
			break;
		case FOD_PARA_UUID:
			if (kstrtoint(tmp_string, 16, &info->fod_params[row].uuid))
				return -1;
			break;
		case FOD_PARA_PARAMS:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params[row].params))
				return -1;
			break;
		case FOD_PARA_PARAMS_MAG:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params[row].params_mag))
				return -1;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int nuvolta_1652_parse_fod_params_default(struct device_node *node,
	struct nuvolta_1652_chg *info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "fod_params_default",
		FOD_PARA_MAX_GROUP,
		FOD_PARA_MAX);
	if (array_len < 0) {
#line 2935
		mca_log_err("parse fod_params_default failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "fod_params_default", i, &tmp_string))
			return -1;

		row = i / FOD_PARA_MAX;
		col = i % FOD_PARA_MAX;
#line 2945
		mca_log_debug("[%d]fod params default %s\n", i, tmp_string);
		switch (col) {
		case FOD_PARA_TYPE:
			if (kstrtou8(tmp_string, 10, &info->fod_params_default.type))
				return -1;
			break;
		case FOD_PARA_LENGTH:
			if (kstrtou8(tmp_string, 10, &info->fod_params_default.length))
				return -1;
			break;
		case FOD_PARA_UUID:
			if (kstrtoint(tmp_string, 16, &info->fod_params_default.uuid))
				return -1;
			break;
		case FOD_PARA_PARAMS:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params_default.params))
				return -1;
			break;
		case FOD_PARA_PARAMS_MAG:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params_default.params_mag))
				return -1;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int nuvolta_1652_parse_fod_params_bpp_plus(struct device_node *node,
	struct nuvolta_1652_chg *info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "fod_params_bpp_plus",
		FOD_PARA_MAX_GROUP,
		FOD_PARA_MAX);
	if (array_len < 0) {
#line 2985
		mca_log_err("parse fod_params_bpp_plus failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "fod_params_bpp_plus", i, &tmp_string))
			return -1;

		row = i / FOD_PARA_MAX;
		col = i % FOD_PARA_MAX;
#line 2995
		mca_log_debug("[%d]fod params bpp plus %s\n", i, tmp_string);
		switch (col) {
		case FOD_PARA_TYPE:
			if (kstrtou8(tmp_string, 10, &info->fod_params_bpp_plus.type))
				return -1;
			break;
		case FOD_PARA_LENGTH:
			if (kstrtou8(tmp_string, 10, &info->fod_params_bpp_plus.length))
				return -1;
			break;
		case FOD_PARA_UUID:
			if (kstrtoint(tmp_string, 16, &info->fod_params_bpp_plus.uuid))
				return -1;
			break;
		case FOD_PARA_PARAMS:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params_bpp_plus.params))
				return -1;
			break;
		case FOD_PARA_PARAMS_MAG:
			if (nuvolta_1652_parse_params(node, tmp_string, info->fod_params_bpp_plus.params_mag))
				return -1;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int nuvolta_1652_parse_fw_fod_data(struct nuvolta_1652_chg *chip)
{
	int ret = 0;
	struct device_node *node = of_find_node_by_name(NULL, "mca_nu1652_fod_data");

	switch (chip->project_vendor) {
	case WLS_CHIP_VENDOR_FUDA1651:
		chip->fw_data_ptr = fw_data_1652[chip->fw_version_index];
		chip->fw_data_size = sizeof(fw_data_1652[chip->fw_version_index]);
		break;
	case WLS_CHIP_VENDOR_FUDA1665:
		chip->fw_data_ptr = fw_data_1665[chip->fw_version_index];
		chip->fw_data_size = sizeof(fw_data_1665[chip->fw_version_index]);
		break;
	case WLS_CHIP_VENDOR_FUDA1661:
		chip->fw_data_ptr = fw_data_1661[chip->fw_version_index];
		chip->fw_data_size = sizeof(fw_data_1661[chip->fw_version_index]);
		break;
	default:
		chip->fw_data_ptr = fw_data_1652[chip->fw_version_index];
		chip->fw_data_size = sizeof(fw_data_1652[chip->fw_version_index]);
		break;
	}

	ret = nuvolta_1652_parse_fod_params(node, chip);
	ret = nuvolta_1652_parse_fod_params_default(node, chip);
	ret = nuvolta_1652_parse_fod_params_bpp_plus(node, chip);

#line 3053
	mca_log_info("cur fw_data is:[0]:%02x; [1]:%02x; [2]:%02x\n",
				chip->fw_data_ptr[0], chip->fw_data_ptr[1], chip->fw_data_ptr[2]);
	return ret;
}

static int nuvolta_1652_parse_dt(struct nuvolta_1652_chg *chip)
{
	struct device_node *node = chip->dev->of_node;
	int ret = 0;

	if (!node) {
#line 3064
		mca_log_err("No DT data Failing Probe\n");
		return -EINVAL;
	}

	(void)mca_parse_dts_u32(node, "project_vendor", &chip->project_vendor, 0);
	(void)mca_parse_dts_u32(node, "support-hall", &chip->support_hall, 0);
	(void)mca_parse_dts_u32(node, "rx_role", &chip->rx_role, 0);
	(void)mca_parse_dts_u32(node, "fw_version_index", &chip->fw_version_index, 0);

#line 3073
	mca_log_err("project_vendor = %d, support-hall = %d, fw_version_index = %d", chip->project_vendor, chip->support_hall, chip->fw_version_index);

	nuvolta_1652_parse_fw_fod_data(chip);

	chip->nu1652_pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->nu1652_pinctrl)) {
#line 3079
		mca_log_err("failed to get nu1652_pinctrl\n");
		return -1;
	}

	chip->pinctrl_stat = pinctrl_lookup_state(chip->nu1652_pinctrl, "rx_int_pull_up");
	if (IS_ERR_OR_NULL(chip->pinctrl_stat)) {
#line 3085
		mca_log_err("failed to parse pinctrl_stat\n");
		return -1;
	}

	ret = pinctrl_select_state(chip->nu1652_pinctrl, chip->pinctrl_stat);
	if (ret) {
#line 3091
		mca_log_err("failed to select pinctrl_stat\n");
		return -1;
	}

	if (chip->support_hall) {
		chip->pinctrl_stat_hall = pinctrl_lookup_state(chip->nu1652_pinctrl, "hall_int_pull_up");
		if (IS_ERR_OR_NULL(chip->pinctrl_stat_hall)) {
#line 3098
			mca_log_err("failed to parse pinctrl_stat_hall\n");
			return -1;
		}

		ret = pinctrl_select_state(chip->nu1652_pinctrl, chip->pinctrl_stat_hall);
		if (ret) {
#line 3104
			mca_log_err("failed to select pinctrl_stat_hall\n");
			return -1;
		}
	}

	chip->enable_gpio = of_get_named_gpio(node, "sleep-rx-gpio", 0);
	if ((!gpio_is_valid(chip->enable_gpio))) {
#line 3111
		mca_log_err("fail_enable_gpio %d\n", chip->enable_gpio);
		return -EINVAL;
	}
	ret = gpio_request(chip->enable_gpio, "rx-enable-gpio");
	if (ret)
#line 3116
		mca_log_err("request enable gpio [%d] fail\n", chip->enable_gpio);
	else {
		ret = gpio_direction_output(chip->enable_gpio, 0);
		if (ret)
#line 3120
			mca_log_err("set direction for enable gpio [%d] fail\n", chip->enable_gpio);
	}

	chip->irq_gpio = of_get_named_gpio(node, "rx-int", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
#line 3125
		mca_log_err("fail_irq_gpio %d\n", chip->irq_gpio);
		return -EINVAL;
	}

	if (chip->support_hall) {
		chip->hall_int_gpio = of_get_named_gpio(node, "hall-int2", 0);
		if (!gpio_is_valid(chip->hall_int_gpio)) {
#line 3132
			mca_log_err("fail_hall_int_gpio %d\n", chip->hall_int_gpio);
			return -EINVAL;
		}
	}

	chip->power_good_gpio = of_get_named_gpio(node, "pwr-det-int", 0);
	if (!gpio_is_valid(chip->power_good_gpio)) {
#line 3139
		mca_log_err("fail_power_good_gpio %d\n", chip->power_good_gpio);
		return -EINVAL;
	}



	return 0;
}

static int nuvolta_rx1652_gpio_init(struct nuvolta_1652_chg *chip)
{
	int ret = 0;

	if (gpio_is_valid(chip->irq_gpio)) {
		chip->irq = gpio_to_irq(chip->irq_gpio);
		if (chip->irq < 0) {
#line 3153
			mca_log_err("gpio_to_irq Fail!\n");
			goto fail_irq_gpio;
		}
	} else {
#line 3157
		mca_log_err("irq gpio not provided\n");
		goto fail_irq_gpio;
	}

	if (gpio_is_valid(chip->power_good_gpio)) {
		chip->power_good_irq = gpio_to_irq(chip->power_good_gpio);
		if (chip->power_good_irq < 0) {
#line 3164
			mca_log_err("gpio_to_power_good Fail!\n");
			goto fail_power_good_gpio;
		}
	} else {
#line 3168
		mca_log_err("power good gpio not provided\n");
		goto fail_power_good_gpio;
	}

	if (chip->support_hall) {
		if (gpio_is_valid(chip->hall_int_gpio)) {
			chip->hall_int_irq = gpio_to_irq(chip->hall_int_gpio);
			if (chip->hall_int_irq < 0) {
#line 3176
				mca_log_err("gpio_to_hall_int Fail!\n");
				goto fail_hall_int_gpio;
			}
		} else {
#line 3180
			mca_log_err("hall int gpio not provided\n");
			goto fail_hall_int_gpio;
		}
	}

	return ret;

fail_hall_int_gpio:
	gpio_free(chip->hall_int_gpio);
fail_power_good_gpio:
	gpio_free(chip->power_good_gpio);
fail_irq_gpio:
	gpio_free(chip->irq_gpio);

	return ret;
}


static int nuvolta_1652_shutdown_cb(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct nuvolta_1652_chg *bcdev = container_of(nb, struct nuvolta_1652_chg,
							shutdown_notifier);

	if (code == SYS_POWER_OFF || code == SYS_RESTART) {
#line 3205
		mca_log_info("start adsp shutdown\n");
		if (bcdev->proc_data.power_good_flag) {
			nuvolta_1652_set_enable_mode(false, bcdev);
			msleep(150);
			//TODO:set wls_iin = 0
			nuvolta_1652_set_enable_mode(true, bcdev);
		}
	}

	return NOTIFY_DONE;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
static int nuvolta_1652_probe(struct i2c_client *client)
#else
static int nuvolta_1652_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	int ret = 0;
	struct nuvolta_1652_chg *chip;

#line 3225
	mca_log_info("nuvolta 1652 probe start!\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->wls_fw_data = devm_kzalloc(&client->dev, sizeof(*chip->wls_fw_data), GFP_KERNEL);
	if (!chip->wls_fw_data)
		return -ENOMEM;

	chip->regmap = devm_regmap_init_i2c(client, &nuvolta_1652_regmap_config);
	if (IS_ERR(chip->regmap)) {
#line 3237
		mca_log_err("failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	chip->client = client;
	chip->dev = &client->dev;

	device_init_wakeup(&client->dev, TRUE);
	i2c_set_clientdata(client, chip);

	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->wireless_chg_int_lock);
	mutex_init(&chip->data_transfer_lock);

	nuvolta_1652_parse_dt(chip);
	nuvolta_rx1652_gpio_init(chip);

	INIT_DELAYED_WORK(&chip->interrupt_work, nuvolta_1652_interrupt_work);
	INIT_DELAYED_WORK(&chip->pg_detect_work, nuvolta_1652_pg_det_work);
	INIT_DELAYED_WORK(&chip->init_detect_work, nuvolta_1652_init_detect_work);
	INIT_DELAYED_WORK(&chip->hall_interrupt_work, nuvolta_1652_hall_interrupt_work);

	chip->shutdown_notifier.notifier_call = nuvolta_1652_shutdown_cb;
	chip->shutdown_notifier.priority = 255;
	register_reboot_notifier(&chip->shutdown_notifier);

	if (chip->irq) {
		ret = devm_request_threaded_irq(&chip->client->dev, chip->irq, NULL,
				nuvolta_1652_interrupt_handler,
				(IRQF_TRIGGER_FALLING | IRQF_ONESHOT),
				"nuvolta_1652_chg_stat_irq", chip);
		if (ret)
#line 3269
			mca_log_err("Failed irq = %d ret = %d\n", chip->irq, ret);
	}
	enable_irq_wake(chip->irq);

	if (chip->power_good_irq) {
		ret = devm_request_threaded_irq(&chip->client->dev, chip->power_good_irq, NULL,
				nuvolta_1652_power_good_handler,
				(IRQF_TRIGGER_FALLING |  IRQF_TRIGGER_RISING | IRQF_ONESHOT),
				"nuvolta_1652_power_good_irq", chip);
		if (ret)
#line 3279
			mca_log_err("Failed irq = %d ret = %d\n", chip->power_good_irq, ret);
	}
	enable_irq_wake(chip->power_good_irq);

	if (chip->support_hall) {
		if (chip->hall_int_irq) {
			ret = devm_request_threaded_irq(&chip->client->dev, chip->hall_int_irq, NULL,
					nuvolta_1652_hall_interrupt_handler,
					(IRQF_TRIGGER_FALLING |  IRQF_TRIGGER_RISING | IRQF_ONESHOT),
					"nuvolta_1652_hall_int_irq", chip);
			if (ret)
#line 3290
				mca_log_err("Failed hall irq = %d ret = %d\n", chip->hall_int_irq, ret);
		}
		enable_irq_wake(chip->hall_int_irq);
	}

	ret = platform_class_wireless_register_ops(chip->rx_role, chip, &nuvolta_1652_wls_ops);
	if (ret) {
#line 3297
		mca_log_err("register ops fail\n");
		goto err_dev;
	}

	schedule_delayed_work(&chip->init_detect_work, msecs_to_jiffies(1000));
	if (chip->support_hall)
		schedule_delayed_work(&chip->hall_interrupt_work, msecs_to_jiffies(1200));

#line 3305
	mca_log_err("nuvolta 1652 probe success!\n");

	return ret;

err_dev:
#line 3310
	mca_log_err("nuvolta 1652 probe failed\n");
	return ret;
}

static int nuvolta_1652_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nuvolta_1652_chg *chip = i2c_get_clientdata(client);

	return enable_irq_wake(chip->irq);
}

static int nuvolta_1652_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct nuvolta_1652_chg *chip = i2c_get_clientdata(client);

	return disable_irq_wake(chip->irq);
}

static void nuvolta_1652_remove(struct i2c_client *client)
{
	struct nuvolta_1652_chg *chip = i2c_get_clientdata(client);

	mutex_destroy(&chip->i2c_lock);
	mutex_destroy(&chip->wireless_chg_int_lock);
	mutex_destroy(&chip->data_transfer_lock);


	if (chip->irq_gpio > 0)
		gpio_free(chip->irq_gpio);
	if (chip->power_good_gpio > 0)
		gpio_free(chip->power_good_gpio);
	if (chip->support_hall) {
		if (chip->hall_int_gpio > 0)
			gpio_free(chip->hall_int_gpio);
	}
}

static void nuvolta_1652_shutdown(struct i2c_client *client)
{
}

static const struct dev_pm_ops nuvolta_1652_pm_ops = {
	.suspend = nuvolta_1652_suspend,
	.resume = nuvolta_1652_resume,
};

static const struct of_device_id nuvolta_1652_of_match[] = {
	{.compatible = "fuda,nu1652",},
	{},
};

static const struct i2c_device_id nuvolta_1652_id[] = {
	{"nuvolta_1652", 0},
	{},
};

static struct i2c_driver nuvolta_1652_i2c_driver = {
	.driver		= {
		.name = "nuvolta_1652",
		.owner = THIS_MODULE,
		.of_match_table = nuvolta_1652_of_match,
		.pm = &nuvolta_1652_pm_ops,
	},
	.probe		= nuvolta_1652_probe,
	.remove		= nuvolta_1652_remove,
	.shutdown	= nuvolta_1652_shutdown,
	.id_table	= nuvolta_1652_id,
};

module_i2c_driver(nuvolta_1652_i2c_driver);

MODULE_DESCRIPTION("nuvolta wireless charge driver");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
