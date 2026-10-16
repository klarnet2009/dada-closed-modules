// SPDX-License-Identifier: GPL-2.0
/*
 * mca_wireless_revchg.c
 *
 * wireless reverse charge driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>

#include "mca_wireless_revchg.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/strategy/mca_wireless_revchg.h>
#define platform_class_wireless_get_tx_ss(role, p) ((void)*(p), 0)	/* not in the OS4 stock provider */
#define platform_class_wireless_get_tx_tdie(role, p) ((void)*(p), 0)
#line 47
#include <mca/protocol/protocol_class.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_mievent.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_wireless_revchg"
#endif

static int mca_wireless_rev_charge_config(int enable);
static void mca_wireless_rev_set_reverse_src(int boost_src);

static struct mca_wireless_revchg *g_wls_rev_info;

int mca_wireless_rev_get_rev_boost_default(int *rev_boost_default)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	*rev_boost_default = info->rev_boost_default;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_rev_boost_default);

int mca_wireless_rev_get_user_reverse_chg(bool *user_reverse_chg)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (info->proc_data.user_reverse_chg)
		*user_reverse_chg = true;
	else
		*user_reverse_chg = false;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_user_reverse_chg);

int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (user_reverse_chg)
		info->proc_data.user_reverse_chg = true;
	else
		info->proc_data.user_reverse_chg = false;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_user_reverse_chg);

int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (info->proc_data.reverse_chg_en)
		*reverse_chg_en = true;
	else
		*reverse_chg_en = false;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg);

int mca_wireless_rev_set_reverse_chg(bool reverse_chg_en)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (reverse_chg_en)
		info->proc_data.reverse_chg_en = true;
	else
		info->proc_data.reverse_chg_en = false;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_reverse_chg);

int mca_wireless_rev_set_usb_plugin(bool wls_sleep_usb_insert)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (wls_sleep_usb_insert)
		info->proc_data.wls_sleep_usb_insert = true;
	else
		info->proc_data.wls_sleep_usb_insert = false;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_usb_plugin);

int mca_wireless_rev_get_fw_update(bool *fw_update)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	*fw_update = info->proc_data.wls_sleep_fw_update;

	mca_log_info("wls_get_sleep_fw_update: %d\n", info->proc_data.wls_sleep_fw_update);

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_fw_update);

int mca_wireless_rev_get_reverse_chg_state(int *state)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	*state = info->proc_data.reverse_chg_sts;

	mca_log_info("wls_get_reverse_chg_state: %d\n", info->proc_data.reverse_chg_sts);

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_reverse_chg_state);

int mca_wireless_rev_set_firmware_state(int fw_update_state)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	info->proc_data.firmware_update_state = fw_update_state;

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_firmware_state);

int mca_wireless_rev_get_firmware_state(int *state)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	*state = info->proc_data.firmware_update_state;

	mca_log_info("wls_get_firmware_update_state: %d\n", info->proc_data.firmware_update_state);

	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_get_firmware_state);


static int mca_wireless_rev_download_firmware(void)
{
	int ret = 0;
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	u8 check_result = 0;
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	platform_class_wireless_get_fw_version_check(WIRELESS_ROLE_MASTER, &check_result);
	if ((check_result == (BOOT_CHECK_SUCCESS | RX_CHECK_SUCCESS | TX_CHECK_SUCCESS)) && !info->proc_data.force_download) {
		mca_log_err("wireless_download_firmware no need update");
		info->proc_data.firmware_update_state = FIRMWARE_NO_UPDATE;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_NO_UPDATE);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		return ret;
	}
	info->proc_data.firmware_update_state = FIRMWARE_UPDATING;
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_UPDATING);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
	ret = platform_class_wireless_download_fw(WIRELESS_ROLE_MASTER);
	return ret;
}


static int mca_wireless_rev_onekey_download_firmware(void)
{
	int ret = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	u8 check_result = 0;
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	ret = mca_wireless_rev_charge_config(true);
	if (ret < 0)
		goto onekey_download_exit;

	mca_log_err("enter onekey download firmware");
	if (!info->proc_data.only_check) {
		if (info->proc_data.from_bin)
			ret = platform_class_wireless_download_fw_from_bin(WIRELESS_ROLE_MASTER);
		else if (info->proc_data.fw_erase)
			ret = platform_class_wireless_erase_fw(WIRELESS_ROLE_MASTER);
		else
			ret = mca_wireless_rev_download_firmware();

		if (ret < 0) {
			info->proc_data.firmware_update_state = FIRMWARE_UPDATE_ERROR;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_UPDATE_ERROR);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
			mca_log_err("wireless_download_firmware fail!!!");
			goto onekey_download_exit;
		}
		msleep(100);
	}

	if (!info->proc_data.only_check) {
		ret = mca_wireless_rev_charge_config(false);
		msleep(1000);
		ret = mca_wireless_rev_charge_config(true);
	}

	platform_class_wireless_get_fw_version_check(WIRELESS_ROLE_MASTER, &check_result);
	if (check_result == (BOOT_CHECK_SUCCESS | RX_CHECK_SUCCESS | TX_CHECK_SUCCESS)) {
		ret = platform_class_wireless_set_confirm_data(WIRELESS_ROLE_MASTER, 0x66);
		mca_log_err("wireless_download_firmware success");
		info->proc_data.firmware_update_state = FIRMWARE_UPDATE_FINISH;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_UPDATE_FINISH);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	} else {
		info->proc_data.firmware_update_state = FIRMWARE_UPDATE_ERROR;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_UPDATE_ERROR);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		mca_log_err("wireless_check_firmware fail!!!");
	}

onekey_download_exit:
	ret = mca_wireless_rev_charge_config(false);
	return ret;
}

int mca_wireless_rev_update_fw_version(int cmd)
{
	int ret = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int power_good = 0;
	int usb_online = 0, rsoc = 0, vcell = 0;
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (info->proc_data.fw_updating) {
		mca_log_info("Firmware Update is on going!\n");
		return ret;
	}

	if ((cmd < FW_UPDATE_POWER_ON) || (cmd >= FW_UPDATE_MAX)) {
		mca_log_err("Firmware Update:invalid cmd\n");
		return -1;
	}

	if (info->proc_data.reverse_chg_en) {
		mca_wireless_rev_enable_reverse_charge(false);
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		mca_log_info("Firmware Update is beginning, stop reverse chg\n");
	}

	(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
	if (power_good) {
		mca_log_info("power good on, stop fw update\n");
		return ret;
	}

	info->proc_data.from_bin = false;
	info->proc_data.force_download = false;
	info->proc_data.only_check = false;
	info->proc_data.user_update = false;
	info->proc_data.fw_erase = false;

	if (cmd > FW_UPDATE_FORCE)
		info->proc_data.from_bin = true;
	else if (cmd == FW_UPDATE_FORCE)
		info->proc_data.force_download = true;
	else if (cmd == FW_UPDATE_CHECK)
		info->proc_data.only_check = true;
	else if (cmd == FW_UPDATE_USER) {
		info->proc_data.user_update = true;
		(void)platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, false);
	} else if (cmd == FW_UPDATE_ERASE)
		info->proc_data.fw_erase = true;

	if (cmd == FW_UPDATE_POWER_ON)
		info->proc_data.power_on_update = true;
	else
		info->proc_data.power_on_update = false;

	(void)platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb_online);
	ret |= strategy_class_fg_ops_get_voltage(&vcell);
	ret |= strategy_class_fg_ops_get_rsoc(&rsoc);
	mca_log_info("Fw update, usb_online = %d, rsoc =%d, vcell = %d", usb_online, rsoc, vcell);

	if (ret < 0) {
		mca_log_err("Fail get battery info!\n");
		if (cmd == FW_UPDATE_USER)
			(void)platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, true);
		return ret;
	}

	if ((!usb_online && vcell >= 3800 && rsoc > 5) || (cmd != FW_UPDATE_POWER_ON))
		schedule_delayed_work(&info->fw_update_work, msecs_to_jiffies(0));
	else
		mca_log_info("No update wls firmware: usb_online = %d, rsoc =%d, vcell = %d", usb_online, rsoc, vcell);

	return ret;
}
EXPORT_SYMBOL(mca_wireless_rev_update_fw_version);

int mca_wireless_rev_set_wired_chg_ok(bool ok)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	info->proc_data.wired_chg_ok = ok;
	return 0;
}
EXPORT_SYMBOL(mca_wireless_rev_set_wired_chg_ok);

static int mca_wireless_rev_charger_adapter_config(int enable)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;
	int ret = 0;
	bool usb_present = false;
	int otg_boost_enable = 0, otg_gate_enable = 0;

	if (!info)
		return -1;

	if (info->force_stop) {
		mca_log_err("hw error, return fail\n");
		return -1;
	}

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_WIRELESS_REVCHG, enable);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_WIRELESS_REVCHG, enable);

	(void)platform_class_cp_get_int_stat(CP_ROLE_MASTER, VUSB_PRESENT_STAT, &usb_present);
	platform_class_buckchg_ops_get_otg_boost_enable_status(MAIN_BUCK_CHARGER, &otg_boost_enable);
	platform_class_buckchg_ops_get_otg_gate_enable_status(MAIN_BUCK_CHARGER, &otg_gate_enable);

	if (enable) {
		if (usb_present && (!otg_gate_enable || !otg_boost_enable)) {
			if (!info->proc_data.wired_chg_ok) {
				mca_log_err("wait vbus 9v fail!\n");
				return -1;
			}
			info->proc_data.wired_chg_ok = false; //reset flag
			return ret;
		}
		mca_wireless_rev_set_reverse_src(PMIC_REV_BOOST);
		mca_log_err("should not be here, check code\n");
	} else {
		info->proc_data.wired_chg_ok = false; //reset flag
		mca_log_info("rev chg close\n");
	}
	return ret;
}

static int mca_wireless_rev_charger_boost_config(int enable)
{
	int ret = 0;
	int retry_cnt = 0;
	int src_voltage;
	int voltage = 5000;
	int otg_boost_vol = 5200;
	EN_SRC en_boost = REV_EN_BOOST;
	int src_enable;
	struct mca_wireless_revchg *info = g_wls_rev_info;
	int power_good = 0;

	if (!info)
		return -1;

	src_enable = (en_boost << 16) | (info->rev_boost_src << 8) | enable;

	if (enable) {
		ret |= platform_class_cp_enable_ovpgate_with_check(CP_ROLE_MASTER, REVCHG_TYPE, false);
		mca_log_err("close ovp gate!!!,ret = %d", ret);
		msleep(100);

		src_voltage = (info->rev_boost_src << 16) | voltage;
		mca_log_err("enable boost!!!");
		ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
		mca_log_err("enable boost voltage ret = %d", ret);
		ret |= platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);
		mca_log_err("enable boost!!!ret = %d", ret);

		while (voltage < info->rev_boost_voltage) {
			voltage += 500;
			src_voltage = (info->rev_boost_src << 16) | voltage;
			msleep(20);
			(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
			if (power_good)
				break;
			if (voltage < info->rev_boost_voltage) {
				ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
				mca_log_err("wireless_reverse_charger_boost_config:boost voltage:%d\n", voltage);
			} else {
				src_voltage = (info->rev_boost_src << 16) | info->rev_boost_voltage;
				ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
				mca_log_err("wireless_reverse_charger_boost_config:boost voltage end:%d\n", info->rev_boost_voltage);
			}
		}

		if (power_good)
			return -1;

		msleep(20);
		while (retry_cnt < 3) {
			ret = platform_class_wireless_check_i2c_is_ok(WIRELESS_ROLE_MASTER);
			if (!ret)
				break;
			msleep(20);
			mca_log_err("trx i2c check fail!");
			++retry_cnt;
		}
		if (retry_cnt >= 3)
			mca_charge_mievent_report(CHARGE_DFX_WLS_TRX_IIC_ERR, NULL, 0);
	} else {
		src_voltage = (info->rev_boost_src << 16) | otg_boost_vol;
		mca_log_err("close boost!!!");
		ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
		ret |= platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);
		msleep(100);
		ret |= platform_class_cp_enable_ovpgate_with_check(CP_ROLE_MASTER, REVCHG_TYPE, true);
		mca_log_err("close ovpgate!!!");
	}
	mca_log_err("boost done!!!,ret = %d", ret);
	return ret;
}

static int mca_wireless_rev_external_boost_config(int enable)
{
	int ret = 0;
	int retry_cnt = 0;
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	if (enable) {
		if (!info->support_tx_only) {
			ret |= platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, false);
			mca_log_err("close ovp gate!!!,ret = %d", ret);
			msleep(100);
		}

		ret |= platform_class_wireless_set_external_boost_enable(WIRELESS_ROLE_MASTER, true);
		msleep(20);
		while (retry_cnt < 3) {
			ret = platform_class_wireless_check_i2c_is_ok(WIRELESS_ROLE_MASTER);
			if (!ret)
				break;
			msleep(20);
			mca_log_err("trx i2c check fail!");
			++retry_cnt;
		}
	} else {
		ret |= platform_class_wireless_set_external_boost_enable(WIRELESS_ROLE_MASTER, false);
		msleep(100);
		ret |= platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, true);
		mca_log_err("enable ovpgate!!!");
	}

	mca_log_err("set reverse boost done!!!,ret = %d", ret);
	return ret;
}

static int mca_wireless_rev_firmware_update_boost_config(int enable)
{
	int ret = 0;
	int retry_cnt = 0;
	int src_voltage;
	int voltage = 5000;
	int otg_boost_vol = 5200;
	EN_SRC en_boost = REV_EN_BOOST;
	int src_enable;
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return -1;

	src_enable = (en_boost << 16) | (info->rev_boost_default << 8) | enable;

	if (enable) {
		ret |= platform_class_cp_enable_ovpgate_with_check(CP_ROLE_MASTER, REVCHG_TYPE, false);
		msleep(100);
		src_voltage = (info->rev_boost_default << 16) | voltage;
		ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
		ret |= platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);

		while (voltage < info->rev_boost_voltage) {
			voltage += 500;
			src_voltage = (info->rev_boost_default << 16) | voltage;
			msleep(20);

			if (voltage < info->rev_boost_voltage) {
				ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
				mca_log_err("wireless_reverse_charger_boost_config:boost voltage:%d\n", voltage);
			} else {
				src_voltage = (info->rev_boost_default << 16) | info->rev_boost_voltage;
				ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
				mca_log_err("wireless_reverse_charger_boost_config:boost voltage end:%d\n", info->rev_boost_voltage);
			}
		}

		msleep(20);
		while (retry_cnt < 3) {
			ret = platform_class_wireless_check_i2c_is_ok(WIRELESS_ROLE_MASTER);
			if (!ret)
				break;
			msleep(20);
			mca_log_err("trx i2c check fail!");
			++retry_cnt;
		}
	} else {
		src_voltage = (info->rev_boost_src << 16) | otg_boost_vol;
		ret |= platform_class_buckchg_ops_set_boost_voltage(MAIN_BUCK_CHARGER, src_voltage);
		ret |= platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);
		msleep(100);
		ret |= platform_class_cp_enable_ovpgate_with_check(CP_ROLE_MASTER, REVCHG_TYPE, true);
		mca_log_err("close ovpgate!!!");
	}
	mca_log_err("boost done!!!,ret = %d", ret);
	return ret;

}

static int mca_wireless_rev_charge_config(int enable)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;
	int ret = 0;
	int power_good = 0;

	if (!info)
		return -1;

	mca_log_err("wireless_reverse_charge_config:rev boost src:%d\n", info->rev_boost_src);

	(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
	if (power_good && enable)
		return -1;

	if (info->proc_data.wls_sleep_fw_update) {
		if (info->rev_boost_default == PMIC_REV_BOOST) {
			ret = mca_wireless_rev_firmware_update_boost_config(enable);
			mca_log_err("fw update start boost config!!!");
			return ret;
		} else if (info->rev_boost_default == EXTERNAL_BOOST) {
			ret = mca_wireless_rev_external_boost_config(enable);
			mca_log_info("fw update start external boost config!!!");
			return ret;
		}
	}

	switch (info->rev_boost_src) {
	case PMIC_REV_BOOST:
	case PMIC_HBOOST:
		mca_log_err("start boost config!!!");
		ret = mca_wireless_rev_charger_boost_config(enable);
		mca_log_err("end boost config!!!");
		break;
	case EXTERNAL_BOOST:
		ret = mca_wireless_rev_external_boost_config(enable);
		break;
	case CHARGER_ADAPTER:
		ret = mca_wireless_rev_charger_adapter_config(enable);
		break;
	default:
		break;
	}

	return ret;
}

static int mca_wireless_rev_switch_reverse_boost_src(int boost_src)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;
	int usb_real_type = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	if (!info)
		return -1;

	if (info->is_switching) {
		info->rev_boost_src = boost_src;
		mca_log_err("mca_wireless_rev_switch_boost_src: is in switching, skip!!!,boost_src_now = %d\n", info->rev_boost_src);
		return 0;
	}
	info->is_switching = true;

	mca_log_err("mca_wireless_rev_switch_boost_src: %d to %d  boost_en:%d\n", info->rev_boost_src, boost_src, info->proc_data.reverse_chg_en);
	mca_wireless_rev_enable_reverse_charge(false);
	info->rev_boost_src = boost_src;
	msleep(2000);

	protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &usb_real_type);
	if (usb_real_type == XM_CHARGER_TYPE_CDP || usb_real_type == XM_CHARGER_TYPE_SDP)
		info->proc_data.bc12_reverse_chg = true;
	else
		info->proc_data.bc12_reverse_chg = false;

	mca_log_err("xm_wireless_switch_reverse_boost_src: bc12_reverse_chg is %d", info->proc_data.bc12_reverse_chg);
	if (info->proc_data.bc12_reverse_chg) {
		info->proc_data.reverse_chg_en = false;
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_MODE=%d", REVERSE_CHARGE_CLOSE);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		goto exit;
	}
	//when sleep 2s user close reverse chg should not open tx work
	if (!info->proc_data.user_reverse_chg) {
		info->proc_data.reverse_chg_en = false;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_MODE=%d", REVERSE_CHARGE_CLOSE);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		info->proc_data.user_reverse_chg = true;
		goto exit;
	}

	/*use delay work to enable tx to avoid enable tx in irq process, otherwise it will cause irq process slowly*/
	schedule_delayed_work(&info->enable_tx_work, msecs_to_jiffies(100));
exit:
	mca_log_info("relax dev\n");
	pm_relax(info->dev);
	info->is_switching = false;		//reset flag
	return 0;
}

static void mca_wireless_rev_set_reverse_src(int boost_src)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;

	if (!info)
		return;

	if (boost_src >= BOOST_SRC_MAX) {
		mca_log_err("boost source index is out of range!!!\n");
		return;
	}
	cancel_delayed_work_sync(&info->disable_tx_work);
	mca_log_err("mca_wireless_rev_set_boost_src:%d pre:%d boost_en:%d\n", boost_src, info->rev_boost_src, info->proc_data.reverse_chg_en);

	if (info->rev_boost_src != boost_src) {
		if (info->proc_data.reverse_chg_en == true)
			mca_wireless_rev_switch_reverse_boost_src(boost_src);
		else
			info->rev_boost_src = boost_src;
	}
}

int mca_wireless_rev_enable_reverse_charge(bool enable)
{
	struct mca_wireless_revchg *info = g_wls_rev_info;
	int power_good = 0;
	int pen_hall3;
	int pen_hall4;
	int ret = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int len;

	if (!info)
		return -1;

	if (info->proc_data.fw_updating) {
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		mca_log_info("fw updating, don't enable reverse charge\n");
		return -1;
	}

	if (enable) {
		if (info->support_tx_only) {
			platform_class_wireless_get_pen_hall3(WIRELESS_ROLE_MASTER, &pen_hall3);
			platform_class_wireless_get_pen_hall4(WIRELESS_ROLE_MASTER, &pen_hall4);
			if (pen_hall3 && pen_hall4) {
				mca_log_info("pen is not attached, don't enable reverse charge\n");
				return -1;
			}
		}

		(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
		if (power_good) {
			info->proc_data.reverse_chg_sts = REVERSE_STATE_FORWARD;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_FORWARD);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
			mca_log_info("wireless charging, don't enable reverse charge\n");
			return -1;
		}
		pm_stay_awake(info->dev);
		info->proc_data.reverse_chg_en = true;
		info->proc_data.reverse_chg_sts = REVERSE_STATE_OPEN;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_MODE=%d", REVERSE_CHARGE_OPEN);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_OPEN);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_OPEN);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		mca_log_err("start reverse_charge_config_work\n");
		if (info->support_tx_only)
			schedule_delayed_work(&info->reverse_charge_config_work, msecs_to_jiffies(0));
		else
			schedule_delayed_work(&info->reverse_charge_config_work, msecs_to_jiffies(200));
		schedule_delayed_work(&info->tx_ping_timeout_work, msecs_to_jiffies(REVERSE_PING_TIMEOUT_TIMER));
		schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(300));
		if (info->support_tx_only)
			schedule_delayed_work(&info->pen_place_err_check_work, msecs_to_jiffies(REVERSE_PPE_TIMEOUT_TIMER));
	} else {
		mca_log_err("close");
		if (!info->proc_data.wireless_reverse_closing) {
			info->proc_data.wireless_reverse_closing = true;
			mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
			cancel_delayed_work_sync(&info->monitor_work);
			ret |= platform_class_wireless_enable_reverse_chg(WIRELESS_ROLE_MASTER, false);
			mca_log_err("start reverse_charge_config\n");
			ret = mca_wireless_rev_charge_config(false);
			mca_log_err("stop reverse_charge_config\n");
			if (!info->is_switching) {
				info->proc_data.reverse_chg_en = false;
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_REVERSE_CHG_MODE=%d", REVERSE_CHARGE_CLOSE);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);
			}
			if (!info->tx_timeout_flag) {
				cancel_delayed_work_sync(&info->tx_ping_timeout_work);
				cancel_delayed_work_sync(&info->tx_transfer_timeout_work);
			}
			if (!info->proc_data.reverse_chg_en)
				pm_relax(info->dev);
			info->proc_data.wireless_reverse_closing = false;
		}
		mca_log_err("close end");
	}

	return ret;
}
EXPORT_SYMBOL(mca_wireless_rev_enable_reverse_charge);


static void mca_wireless_rev_charge_config_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, reverse_charge_config_work.work);
	int ret = 0;
	int usb_real_type = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	if (!info->proc_data.reverse_chg_en) {
		mca_log_info("reverse_chg_en has been close, return\n");
		return;
	}

	if (!info->support_tx_only) {
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &usb_real_type);
		if (usb_real_type == XM_CHARGER_TYPE_CDP || usb_real_type == XM_CHARGER_TYPE_SDP)
			info->proc_data.bc12_reverse_chg = true;
		else
			info->proc_data.bc12_reverse_chg = false;

		if (info->proc_data.bc12_reverse_chg) {
			mca_log_err("bc12 cannot reverse charge!!!\n");
			mca_wireless_rev_enable_reverse_charge(false);
			info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
			return;
		}
	}

	mca_log_err("start reverse_chg_config!!!\n");
	ret = mca_wireless_rev_charge_config(true);
	mca_log_err("stop reverse_chg_config!!!ret = %d", ret);

	if (!info->proc_data.user_reverse_chg) {
		mca_log_err("user close reverse charge!!!");
		mca_wireless_rev_enable_reverse_charge(false);
		return;
	}

	if (ret) {
		mca_log_err("reverse charge fail!!!");
		mca_wireless_rev_enable_reverse_charge(false);
		mca_log_err("reverse charge fail!!!end");
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		return;
	}

	ret = platform_class_wireless_enable_reverse_chg(WIRELESS_ROLE_MASTER, true);
	mca_log_err("reverse charge success = %d!!!", ret);
}

static void mca_wireless_rev_tx_ping_timeout_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, tx_ping_timeout_work.work);
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	info->tx_timeout_flag = true;
	mca_wireless_rev_enable_reverse_charge(false);
	info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	if (info->support_tx_only) {
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}

	mca_log_info("reverse chg ping timeout");
	info->tx_timeout_flag = false;
}

static void mca_wireless_rev_tx_transfer_timeout_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, tx_transfer_timeout_work.work);
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	info->tx_timeout_flag = true;
	mca_wireless_rev_enable_reverse_charge(false);
	info->proc_data.reverse_chg_sts = REVERSE_STATE_TIMEOUT;
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_TIMEOUT);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	if (info->support_tx_only) {
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_TIMEOUT);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}

	mca_log_info("reverse chg transfer timeout");
	info->tx_timeout_flag = false;
}

static void mca_wireless_disable_tx_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, disable_tx_work.work);
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	mca_wireless_rev_enable_reverse_charge(false);
	info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	if (info->support_tx_only) {
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}

	mca_log_err("reverse chg disable tx");
}

static void mca_wireless_enable_tx_work(struct work_struct *work)
{
	mca_wireless_rev_enable_reverse_charge(true);
	mca_log_err("reverse chg enbale tx");
}

static void mca_wireless_rev_update_to_wire_work(struct work_struct *work)
{
	mca_wireless_rev_set_reverse_src(CHARGER_ADAPTER);
	mca_log_err("rev_update_to_wire_work");
}

static void mca_wireless_rev_update_to_boost_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, rev_update_to_boost_work.work);

	mca_wireless_rev_set_reverse_src(info->rev_boost_default);
	mca_log_err("rev_update_to_boost_work");
}

static void mca_wireless_update_fw_mainthread_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, fw_update_work.work);

	if (info->proc_data.fw_updating) {
		mca_log_info("Firmware Update is on going!\n");
		return;
	}
	info->proc_data.fw_updating = true;
	info->proc_data.wls_sleep_fw_update = true;

	//pmic_glink_send_power_supply_notification(XM_POWER_SUPPLY_GET_REQ);
	(void)platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, false);

	pm_stay_awake(info->dev);
	mca_wireless_rev_onekey_download_firmware();
	pm_relax(info->dev);

	info->proc_data.wls_sleep_fw_update = false;
	info->proc_data.fw_updating = false;
	//pmic_glink_send_power_supply_notification(XM_POWER_SUPPLY_GET_REQ);

	if (!info->proc_data.wls_sleep_usb_insert)
		(void)platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, true);
}

static void mca_wireless_poweron_update_work(struct work_struct *work)
{
	mca_wireless_rev_update_fw_version(FW_UPDATE_POWER_ON);
}

static void mca_wireless_rev_test_start_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, reverse_test_start_work.work);

	info->proc_data.user_reverse_chg = true;
	mca_wireless_rev_enable_reverse_charge(true);
	info->wait_for_reverse_test = false;
	mca_log_info("[factory reverse test] start reverse charging test\n");
	schedule_delayed_work(&info->reverse_test_stop_work, msecs_to_jiffies(12000));
}

static void mca_wireless_rev_test_stop_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, reverse_test_stop_work.work);
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	info->tx_timeout_flag = true;
	mca_wireless_rev_enable_reverse_charge(false);
	info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
	mca_log_info("[factory reverse test], ready timer trigger, stop reverse charging test\n");
	info->tx_timeout_flag = false;
}

static void mca_wireless_rev_pen_place_err_check_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info =  container_of(work,
		struct mca_wireless_revchg, pen_place_err_check_work.work);
	int ss = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	(void)platform_class_wireless_get_tx_ss(WIRELESS_ROLE_MASTER, &ss);
	if (!ss) {
		mca_log_err("pen place err check timeout, pen place err: hall\n");
		platform_class_wireless_set_pen_place_err(WIRELESS_ROLE_MASTER, 2);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_PEN_PLACE_ERR=%d", 2);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}

	mca_log_info("reverse charge status: %d\n", info->proc_data.reverse_chg_sts);

	return;
}

static void mca_wireless_rev_pen_data_handle_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info =  container_of(work,
		struct mca_wireless_revchg, pen_data_handle_work.work);
	int pen_soc = 255, tx_vout = 0, tx_iout = 0, tx_tdie = 0;
	int pen_full_flag = 0;
	static int full_soc_count = 0;
	u64 pen_mac = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	// get pen data
	(void)platform_class_wireless_get_pen_soc(WIRELESS_ROLE_MASTER, &pen_soc);
	(void)platform_class_wireless_get_tx_vout(WIRELESS_ROLE_MASTER, &tx_vout);
	(void)platform_class_wireless_get_tx_iout(WIRELESS_ROLE_MASTER, &tx_iout);
	(void)platform_class_wireless_get_tx_tdie(WIRELESS_ROLE_MASTER, &tx_tdie);
	(void)platform_class_wireless_get_pen_full_flag(WIRELESS_ROLE_MASTER, &pen_full_flag);
	(void)platform_class_wireless_get_pen_mac(WIRELESS_ROLE_MASTER, &pen_mac);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_PEN_PLACE_ERR=%d", 0);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_PEN_SOC=%d", pen_soc);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_PEN_MAC=%llx", pen_mac);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	if (pen_full_flag) {
		mca_log_info("pens battery is full, disable reverse chg\n");
		mca_wireless_rev_enable_reverse_charge(false);
	} else if ((pen_soc == 100) && (full_soc_count < PEN_SOC_FULL_COUNT)) {
		full_soc_count++;
		mca_log_info("pens soc is 100 count: %d\n", full_soc_count);
	} else {
		full_soc_count = 0;
	}

	if (full_soc_count == PEN_SOC_FULL_COUNT) {
		mca_log_info("pens soc is 100 exceed 18 ,disable reverse chg\n");
		full_soc_count = 0;
		pen_full_flag = 1;
		mca_wireless_rev_enable_reverse_charge(false);
	}

	if (pen_full_flag) {
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}

	if (info->proc_data.reverse_chg_sts == REVERSE_STATE_TRANSFER) {
		schedule_delayed_work(&info->pen_data_handle_work, msecs_to_jiffies(REVERSE_PEN_DELAY_TIMER));
	}

	mca_log_info("loop pen data handle work\n");
	return;
}

static void mca_wireless_user_enable_revchg_work(struct work_struct *work)
{
	mca_wireless_rev_enable_reverse_charge(true);
}

static void mca_wireless_user_disable_revchg_work(struct work_struct *work)
{
	mca_wireless_rev_enable_reverse_charge(false);
}

static void mca_wireless_rev_monitor_work(struct work_struct *work)
{
	struct mca_wireless_revchg *info = container_of(work,
		struct mca_wireless_revchg, monitor_work.work);
	int isense = 0, vrect = 0, power_good = 0;

	platform_class_wireless_get_trx_isense(WIRELESS_ROLE_MASTER, &isense);
	platform_class_wireless_get_trx_vrect(WIRELESS_ROLE_MASTER, &vrect);

	mca_log_info("wireless revchg: [isense:%d], [vrect:%d]\n", isense, vrect);
	(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
	if (!power_good)
		schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(1000));
}

static void mca_wireless_rev_init_work(struct mca_wireless_revchg *info)
{
	INIT_DELAYED_WORK(&info->monitor_work, mca_wireless_rev_monitor_work);
	INIT_DELAYED_WORK(&info->reverse_charge_config_work, mca_wireless_rev_charge_config_work);
	INIT_DELAYED_WORK(&info->tx_ping_timeout_work, mca_wireless_rev_tx_ping_timeout_work);
	INIT_DELAYED_WORK(&info->tx_transfer_timeout_work, mca_wireless_rev_tx_transfer_timeout_work);
	INIT_DELAYED_WORK(&info->disable_tx_work, mca_wireless_disable_tx_work);
	INIT_DELAYED_WORK(&info->enable_tx_work, mca_wireless_enable_tx_work);
	INIT_DELAYED_WORK(&info->rev_update_to_wire_work, mca_wireless_rev_update_to_wire_work);
	INIT_DELAYED_WORK(&info->rev_update_to_boost_work, mca_wireless_rev_update_to_boost_work);
	INIT_DELAYED_WORK(&info->fw_update_work, mca_wireless_update_fw_mainthread_work);
	INIT_DELAYED_WORK(&info->poweron_update_work, mca_wireless_poweron_update_work);
	INIT_DELAYED_WORK(&info->reverse_test_start_work, mca_wireless_rev_test_start_work);
	INIT_DELAYED_WORK(&info->reverse_test_stop_work, mca_wireless_rev_test_stop_work);
	INIT_DELAYED_WORK(&info->pen_place_err_check_work, mca_wireless_rev_pen_place_err_check_work);
	INIT_DELAYED_WORK(&info->pen_data_handle_work, mca_wireless_rev_pen_data_handle_work);
	INIT_DELAYED_WORK(&info->user_enable_revchg_work, mca_wireless_user_enable_revchg_work);
	INIT_DELAYED_WORK(&info->user_disable_revchg_work, mca_wireless_user_disable_revchg_work);
}

static int mca_wireless_rev_get_status(int status, void *value, void *data)
{
	struct mca_wireless_revchg *info = (struct mca_wireless_revchg *)data;
	//int *cur_val = (int *)value;

	if (!info || !value)
		return -1;

	switch (status) {
	default:
		return -1;
	}

	return 0;
}

static void mca_wireless_reverse_chg_handler(struct mca_wireless_revchg *info)
{
	int ret = 0;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	u8 err_code, dfx_code;
	int ss = 0;
	int pen_soc = 255;
	u64 pen_mac = 0;

	mca_log_info("reverse chg handler, int_index = %d\n", info->proc_data.int_flag);

	switch (info->proc_data.int_flag) {
	case RTX_INT_PING:
		if (!info->tx_timeout_flag)
			cancel_delayed_work_sync(&info->tx_ping_timeout_work);
		schedule_delayed_work(&info->tx_transfer_timeout_work, msecs_to_jiffies(REVERSE_TRANSFER_TIMEOUT_TIMER));
		mca_log_info("RTX_INT_PING");
		break;
	case RTX_INT_GET_RX:
		ret = platform_class_wireless_enable_rev_fod(WIRELESS_ROLE_MASTER, true);
		if (ret)
			mca_log_info("RTX_INT_GET_RX: set rev fod failed in GET_RX!\n");
		info->proc_data.reverse_chg_sts = REVERSE_STATE_TRANSFER;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_TRANSFER);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_TRANSFER);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}
		if (!info->tx_timeout_flag)
			cancel_delayed_work_sync(&info->tx_transfer_timeout_work);
		mca_log_info("RTX_INT_GET_RX: get rx!\n");
		break;
	case RTX_INT_CEP_TIMEOUT:
		info->proc_data.reverse_chg_sts = REVERSE_STATE_WAITPING;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_WAITPING);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_WAITPING);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		schedule_delayed_work(&info->tx_ping_timeout_work, msecs_to_jiffies(REVERSE_PING_TIMEOUT_TIMER));
		mca_log_info("RTX_INT_CEP_TIMEOUT: tx ping timeout!\n");
		break;
	case RTX_INT_EPT:
		schedule_delayed_work(&info->tx_ping_timeout_work, msecs_to_jiffies(REVERSE_PING_TIMEOUT_TIMER));
		mca_log_info("RTX_INT_EPT: end power transfer!\n");
		break;
	case RTX_INT_PROTECTION:
		/*01-tsd  02-tx_oc 03-PK_OC 04-UVLO 05-other*/
		if (info->proc_data.wireless_reverse_closing == false) {
			(void)platform_class_wireless_get_poweroff_err_code(WIRELESS_ROLE_MASTER, &err_code);
			if (err_code == 4) {
				schedule_delayed_work(&info->disable_tx_work, msecs_to_jiffies(700));
				mca_charge_mievent_report(CHARGE_DFX_WLS_TRX_UVLO, NULL, 0);
			} else if (err_code != 0) {
				mca_wireless_rev_enable_reverse_charge(false);
				info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);

				if (info->support_tx_only) {
					len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
						"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
					event_data.event = event;
					event_data.event_len = len;
					mca_event_report_uevent(&event_data);
				}

				if (err_code == 2)
					mca_charge_mievent_report(CHARGE_DFX_WLS_TRX_OCP, NULL, 0);
			}
			mca_log_info("RTX_INT_PROTECTION: protection, %d", err_code);
		} else
			mca_log_info("RTX_INT_PROTECTION: invalid protection");
		break;
	case RTX_INT_GET_TX:
		if (!info->proc_data.wireless_reverse_closing) {
			mca_wireless_rev_enable_reverse_charge(false);
			info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			if (info->support_tx_only) {
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);
			}

			mca_log_info("RTX_INT_GET_TX: get tx!!");
		}
		break;
	case RTX_INT_REVERSE_TEST_READY:
		mca_log_info("RTX_INT_REVERSE_TEST_READY: receiver reverse test ready, cancel timer");
		if (!info->tx_timeout_flag)
			cancel_delayed_work_sync(&info->reverse_test_stop_work);
		break;
	case RTX_INT_REVERSE_TEST_DONE:
		mca_wireless_rev_enable_reverse_charge(false);
		mca_log_info("RTX_INT_REVERSE_TEST_DONE: reverse test done");
		break;
	case RTX_INT_FOD:
		mca_wireless_rev_enable_reverse_charge(false);
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}

		mca_charge_mievent_report(CHARGE_DFX_WLS_TRX_FOD, NULL, 0);
		mca_log_info("RTX_INT_FOD: fod!!");
		break;
	case RTX_INT_EPT_PKT:
		schedule_delayed_work(&info->tx_transfer_timeout_work, msecs_to_jiffies(REVERSE_TRANSFER_TIMEOUT_TIMER));
		mca_log_info("RTX_INT_EPT_PKT: ept receive!!");
		break;
	case RTX_INT_ERR_CODE:
		(void)platform_class_wireless_get_tx_err_code(WIRELESS_ROLE_MASTER, &err_code, &dfx_code);
		mca_log_err("RTX_INT_ERR_CODE, err_code:0x%02x\n", err_code);
		break;
	case RTX_INT_TX_DET_RX:
		mca_log_info("RTX_INT_TX_DET_RX trigger!");
		cancel_delayed_work_sync(&info->pen_place_err_check_work);
		(void)platform_class_wireless_get_tx_ss(WIRELESS_ROLE_MASTER, &ss);
		if (ss < 100) {
			mca_log_info("tx get ss_reg value: %d, pen place err: ss\n", ss);
			mca_wireless_rev_enable_reverse_charge(false);
			cancel_delayed_work_sync(&info->pen_data_handle_work);
			platform_class_wireless_set_pen_place_err(WIRELESS_ROLE_MASTER, 1);
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_PEN_PLACE_ERR=%d", 1);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			if (info->support_tx_only) {
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);
			}
		}
		break;
	case RTX_INT_TX_CONFIG:
		mca_log_info("RTX_INT_TX_CONFIG trigger!");
		schedule_delayed_work(&info->pen_data_handle_work, msecs_to_jiffies(REVERSE_PEN_DELAY_TIMER));
		break;
	case RTX_INT_TX_CHS_UPDATE:
		mca_log_info("RTX_INT_TX_CHS_UPDATE trigger!");
		(void)platform_class_wireless_get_pen_soc(WIRELESS_ROLE_MASTER, &pen_soc);
		(void)platform_class_wireless_get_pen_mac(WIRELESS_ROLE_MASTER, &pen_mac);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_SOC=%d", pen_soc);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_PEN_MAC=%llx", pen_mac);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		break;
	case RTX_INT_TX_BLE_CONNECT:
		mca_log_info("RTX_INT_TX_BLE_CONNECT trigger!");
		(void)platform_class_wireless_get_pen_soc(WIRELESS_ROLE_MASTER, &pen_soc);
		(void)platform_class_wireless_get_pen_mac(WIRELESS_ROLE_MASTER, &pen_mac);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_SOC=%d", pen_soc);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_PEN_MAC=%llx", pen_mac);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
		break;
	default:
		break;
	}
	info->proc_data.int_flag = 0;
}

static void mca_wireless_rev_process_int_change(int value, struct mca_wireless_revchg *info)
{
	info->proc_data.int_flag = value;
	mca_wireless_reverse_chg_handler(info);
}

static ATOMIC_NOTIFIER_HEAD(pen_charge_state_notifier);

static void mca_wireless_rev_process_hall_change(int value, struct mca_wireless_revchg *info)
{
	bool is_pen_attached = !!value;
	bool pen_hall3_gpio_value = !(value & (1 << 3));
	bool pen_hall4_gpio_value = !(value & (1 << 4));
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int len;

	mca_log_info("pen hall change, set reverse charge: %d\n", is_pen_attached);
	if (!info->proc_data.fw_updating)
		mca_wireless_rev_enable_reverse_charge(is_pen_attached);

	if (!is_pen_attached) {
		cancel_delayed_work_sync(&info->pen_data_handle_work);
		cancel_delayed_work_sync(&info->pen_place_err_check_work);
		info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->support_tx_only) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_PEN_PLACE_ERR=%d", 0);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			platform_class_wireless_set_pen_place_err(WIRELESS_ROLE_MASTER, 0);
		}
	}

	atomic_notifier_call_chain(&pen_charge_state_notifier, is_pen_attached, NULL);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_PEN_HALL3=%d", pen_hall3_gpio_value);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_PEN_HALL4=%d", pen_hall4_gpio_value);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void mca_wireless_rev_process_ppe_hall_change(int value, struct mca_wireless_revchg *info)
{
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int len;

	mca_log_info("pen ppe hall change, place err: %d\n", value);

	if (value) {
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_PEN_PLACE_ERR=%d", value);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);

		if (info->proc_data.reverse_chg_en && info->proc_data.reverse_chg_sts == REVERSE_STATE_TRANSFER) {
			mca_wireless_rev_enable_reverse_charge(false);
			cancel_delayed_work_sync(&info->pen_data_handle_work);
			cancel_delayed_work_sync(&info->pen_place_err_check_work);
			info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			if (info->support_tx_only) {
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_REVERSE_PEN_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);
			}
		}
	} else {
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_PEN_PLACE_ERR=%d", 0);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}
}

static void mca_wireless_rev_process_usb_plug_change(int value, struct mca_wireless_revchg *info)
{
	bool usb_present = false;

	usb_present = value;
	if (!info->proc_data.wls_sleep_fw_update && usb_present) {
		if (info->is_switching)
			info->rev_boost_src = CHARGER_ADAPTER;
		else
			schedule_delayed_work(&info->rev_update_to_wire_work, msecs_to_jiffies(0));
	} else if (!info->proc_data.wls_sleep_fw_update && !usb_present) {
		if (info->is_switching)
			info->rev_boost_src = info->rev_boost_default;
		else
			schedule_delayed_work(&info->rev_update_to_boost_work, msecs_to_jiffies(0));
	}
}

static void mca_wireless_rev_process_online_change(int value, struct mca_wireless_revchg *info)
{
	int len;
	int power_good = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	if (!value) {
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_REV_TEST, &info->wait_for_reverse_test);
		if (info->wait_for_reverse_test)
			schedule_delayed_work(&info->reverse_test_start_work, msecs_to_jiffies(REVERSE_TEST_DELAY_MS));
	} else {
		(void)platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good);
		if (power_good && !info->proc_data.wireless_reverse_closing && info->proc_data.reverse_chg_en) {
			info->proc_data.wireless_reverse_closing = true;
			mca_charge_mievent_set_state(MIEVENT_STATE_PLUG, 0);
			cancel_delayed_work_sync(&info->monitor_work);
			(void)platform_class_wireless_enable_reverse_chg(WIRELESS_ROLE_MASTER, false);
			mca_wireless_rev_charge_config(false);

			info->proc_data.reverse_chg_en = false;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_MODE=%d", REVERSE_CHARGE_CLOSE);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_REVERSE_CHG_STATE=%d", REVERSE_STATE_ENDTRANS);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);

			if (!info->tx_timeout_flag) {
				cancel_delayed_work_sync(&info->tx_ping_timeout_work);
				cancel_delayed_work_sync(&info->tx_transfer_timeout_work);
			}
			pm_relax(info->dev);
			info->proc_data.wireless_reverse_closing = false;
			mca_log_info("power_good on: get tx!!");
		}
	}
}

static int mca_wireless_rev_process_event(int event, int value, void *data)
{
	struct mca_wireless_revchg *info = data;

	if (!data)
		return -1;

	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		mca_wireless_rev_process_online_change(value, info);
		break;
	case MCA_EVENT_WIRELESS_INT_CHANGE:
		mca_wireless_rev_process_int_change(value, info);
		break;
	case MCA_EVENT_WIRELESS_PEN_HALL_CHANGE:
		mca_wireless_rev_process_hall_change(value, info);
		break;
	case MCA_EVENT_WIRELESS_PEN_PPE_HALL_CHANGE:
		mca_wireless_rev_process_ppe_hall_change(value, info);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		mca_log_info("batt-btb change event %d\n", value);
		mca_vote(info->usbin_rev_disable_voter, "batt_miss", true, value);
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_USB_CONNECT:
		if (!info->support_tx_only)
		mca_wireless_rev_process_usb_plug_change(value, info);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
		mca_log_info("lpd status event %d\n", value);
		mca_vote(info->usbin_rev_disable_voter, "lpd", true, value);
		break;
	case MCA_EVENT_CONN_ANTIBURN_CHANGE:
		mca_log_info("anti-burn change event %d\n", value);
		mca_vote(info->usbin_rev_disable_voter, "antiburn", true, value);
		break;
	case MCA_EVENT_CC_SHORT_VBUS:
		mca_log_info("cc short vbus event %d\n", value);
		mca_vote(info->usbin_rev_disable_voter, "antiburn", true, value);
		break;
	case MCA_EVENT_CP_CBOOT_FAIL:
		mca_log_info("cp cboot short event %d\n", value);
		mca_vote(info->usbin_rev_disable_voter, "cp_cboot_short", true, value);
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		mca_log_info("adsp crash,stop wireless revchg");
		mca_vote(info->usbin_rev_disable_voter, "abnormal", true, 1);
		break;
	case MCA_EVENT_CHARGE_RESTORE:
		mca_log_info("adsp restore, disable vote\n");
		mca_vote(info->usbin_rev_disable_voter, "abnormal", true, 0);
		break;
	default:
		break;
	}

	return 0;
}

#ifdef CONFIG_SYSFS
static ssize_t strategy_wireless_rev_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t strategy_wireless_rev_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info strategy_wireless_rev_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(strategy_wireless_rev_sysfs, 0664, MCA_REV_CHG_WIRELESS_CHIP_FW, wireless_chip_fw),
	mca_sysfs_attr_rw(strategy_wireless_rev_sysfs, 0664, MCA_REV_CHG_WLS_FW_STATE, wls_fw_state),
	mca_sysfs_attr_rw(strategy_wireless_rev_sysfs, 0664, MCA_REV_CHG_REVERSE_CHG_MODE, reverse_chg_mode),
	mca_sysfs_attr_rw(strategy_wireless_rev_sysfs, 0664, MCA_REV_CHG_REVERSE_CHG_STATE, reverse_chg_state),
};

#define MCA_REV_CHG_ATTRS_SIZE ARRAY_SIZE(strategy_wireless_rev_sysfs_field_tbl)

static struct attribute *strategy_wireless_rev_sysfs_attrs[MCA_REV_CHG_ATTRS_SIZE + 1];

static const struct attribute_group strategy_wireless_rev_sysfs_attr_group = {
	.attrs = strategy_wireless_rev_sysfs_attrs,
};

static ssize_t strategy_wireless_rev_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mca_wireless_revchg *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int len = 0;
	int rc;
	char wls_fw_data[10] = { 0 };

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_wireless_rev_sysfs_field_tbl, MCA_REV_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_REV_CHG_WIRELESS_CHIP_FW:
		rc = platform_class_wireless_get_fw_version(WIRELESS_ROLE_MASTER, wls_fw_data);
		if (rc < 0)
			return rc;
		len = snprintf(buf, PAGE_SIZE, "%s\n", wls_fw_data);
		break;
	case MCA_REV_CHG_WLS_FW_STATE:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.firmware_update_state);
		break;
	case MCA_REV_CHG_REVERSE_CHG_MODE:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.reverse_chg_en);
		break;
	case MCA_REV_CHG_REVERSE_CHG_STATE:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.reverse_chg_sts);
		break;
	default:
		len = 0;
		break;
	}

	return len;
}

static ssize_t strategy_wireless_rev_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_wireless_revchg *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int val;
	int rc;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_wireless_rev_sysfs_field_tbl, MCA_REV_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_REV_CHG_WIRELESS_CHIP_FW:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		rc = mca_wireless_rev_update_fw_version(val);
		if (rc < 0)
			return rc;
		break;
	case MCA_REV_CHG_REVERSE_CHG_MODE:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		mca_log_info("store reverse_chg_mode = %d\n", val);

		if (!val || info->proc_data.reverse_chg_sts != REVERSE_STATE_TRANSFER) {
			// enabling reverse charge in REVERSE_STATE_TRANSFER status will cause the transfer to
			// terminate, this is usually not expected behavior, so only enable it when we are not
			// in that status
			schedule_delayed_work(val ? &info->user_enable_revchg_work : &info->user_disable_revchg_work, 0);
			if (!val)
				info->proc_data.reverse_chg_sts = REVERSE_STATE_ENDTRANS;
		} else {
			mca_log_info("store reverse_chg_mode no operation\n");
		}

		if (!info->support_tx_only) {
			rc = mca_wireless_rev_set_user_reverse_chg(val);
			if (rc < 0)
				return rc;
		}

		break;
	default:
		break;
	}

	return count;
}

static int strategy_wireless_rev_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(strategy_wireless_rev_sysfs_attrs, strategy_wireless_rev_sysfs_field_tbl,
		MCA_REV_CHG_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "wls_rev_charge",
		dev, &strategy_wireless_rev_sysfs_attr_group);
}

static void strategy_wireless_rev_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "wls_rev_charge",
		dev, &strategy_wireless_rev_sysfs_attr_group);
}

#else

static inline int strategy_wireless_rev_create_group(struct device *dev)
{
}

static void strategy_wireless_rev_remove_group(struct device *dev)
{
}

#endif /* CONFIG_SYSFS */






static int mca_wireless_rev_parse_dt(struct mca_wireless_revchg *info)
{
	struct device_node *node = info->dev->of_node;
	int ret = 0;

	if (!node) {
		mca_log_err("device tree node missing\n");
		return -EINVAL;
	}

	(void)mca_parse_dts_u32(node, "rev_boost_src", &info->rev_boost_default,
		PMIC_REV_BOOST);
	(void)mca_parse_dts_u32(node, "rev_boost_voltage", &info->rev_boost_voltage,
		MCA_WLS_REV_CHG_VOLTAGE_DEFAULT);
	(void)mca_parse_dts_u32(node, "support-tx-only", &info->support_tx_only, 0);

	return ret;
}

static int mca_wireless_usbin_rev_disable_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_wireless_revchg *info = data;

	if (!data)
		return -1;

	info->force_stop = effective_result;
	mca_log_info("force_stop: %d\n", info->force_stop);

	if (info->rev_boost_src == CHARGER_ADAPTER && info->force_stop)
		mca_wireless_rev_enable_reverse_charge(false);

	return 0;
}

int pen_charge_state_notifier_register_client(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&pen_charge_state_notifier, nb);
}
EXPORT_SYMBOL(pen_charge_state_notifier_register_client);

int pen_charge_state_notifier_unregister_client(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&pen_charge_state_notifier, nb);
}
EXPORT_SYMBOL(pen_charge_state_notifier_unregister_client);

static int mca_wireless_rev_shutdown_cb(struct notifier_block *nb, unsigned long code,
		void *unused)
{
	struct mca_wireless_revchg *info = container_of(nb, struct mca_wireless_revchg,
							shutdown_notifier);
	int fw_update_cnt = 0;

	if (code == SYS_POWER_OFF || code == SYS_RESTART) {
		mca_log_err("start shutdown\n");
		while (info->proc_data.fw_updating && fw_update_cnt++ < REVERSE_FW_UPDATE_CNT_NUM) {
			msleep(1000);
			mca_log_err("fw is updating: %d\n", info->proc_data.fw_updating);
		}
	}

	return NOTIFY_DONE;
}

static int mca_wireless_revchg_probe(struct platform_device *pdev)
{
	struct mca_wireless_revchg *info;
	static int probe_cnt;
	int ret = 0;

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	device_init_wakeup(info->dev, true);

	ret = mca_wireless_rev_parse_dt(info);
	info->rev_boost_src = info->rev_boost_default;

	mca_wireless_rev_init_work(info);

	info->usbin_rev_disable_voter = mca_create_votable("usbin_rev_disable", MCA_VOTE_OR,
		mca_wireless_usbin_rev_disable_voter_cb, 0, info);
	if (IS_ERR(info->usbin_rev_disable_voter))
		return -1;

	info->shutdown_notifier.notifier_call = mca_wireless_rev_shutdown_cb;
	info->shutdown_notifier.priority = 255;
	register_reboot_notifier(&info->shutdown_notifier);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_REV_WIRELESS,
		mca_wireless_rev_process_event, mca_wireless_rev_get_status, NULL, info);

	info->proc_data.user_reverse_chg = true;

	g_wls_rev_info = info;

#ifndef CONFIG_FACTORY_BUILD
	schedule_delayed_work(&info->poweron_update_work, msecs_to_jiffies(POWER_ON_UPDATE_TIMER));
#endif
	strategy_wireless_rev_create_group(info->dev);
	mca_log_info("probe %s\n", ret == -EPROBE_DEFER ? "Over probe cnt max" : "OK");
	return ret;
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,wireless_revchg"},
	{},
};

static int mca_wireless_revchg_remove(struct platform_device *pdev)
{
	strategy_wireless_rev_remove_group(&pdev->dev);
	return 0;
}

static void mca_wireless_revchg_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver mca_wireless_revchg_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_wireless_revchg",
		.of_match_table = match_table,
	},
	.probe = mca_wireless_revchg_probe,
	.remove = mca_wireless_revchg_remove,
	.shutdown = mca_wireless_revchg_shutdown,
};

static int __init mca_wireless_revchg_init(void)
{
	return platform_driver_register(&mca_wireless_revchg_driver);
}
module_init(mca_wireless_revchg_init);

static void __exit mca_wireless_revchg_exit(void)
{
	platform_driver_unregister(&mca_wireless_revchg_driver);
}
module_exit(mca_wireless_revchg_exit);

MODULE_DESCRIPTION("MCA Wireless Reverse Charge");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
