/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_wireless_revchg.h
 *
 * wireless reverse charge
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

#include <linux/notifier.h>
#include <linux/reboot.h>

#ifndef __WIRELESS_REVCHG_H__
#define __WIRELESS_REVCHG_H__

#define MCA_WLS_REV_CHG_VOLTAGE_DEFAULT 9000

#define REVERSE_PING_TIMEOUT_TIMER		(20 * 1000)
#define REVERSE_TRANSFER_TIMEOUT_TIMER	(100 * 1000)
#define REVERSE_TEST_DELAY_MS			(2 * 1000)
#define REVERSE_FW_UPDATE_CNT_NUM 		(15 * 1000)
#define REVERSE_PPE_TIMEOUT_TIMER		(3 * 1000)
#define REVERSE_PEN_DELAY_TIMER			(10 * 1000)

#define RX_CHECK_SUCCESS				(1 << 0)
#define TX_CHECK_SUCCESS				(1 << 1)
#define BOOT_CHECK_SUCCESS				(1 << 2)
#define POWER_ON_UPDATE_TIMER			(10 * 1000)

#define PEN_SOC_FULL_COUNT 18

// interrupts reverse definition
enum mca_rev_chg_int_flag {
	RTX_INT_UNKNOWN = 0,
	RTX_INT_PING,
	RTX_INT_GET_RX,
	RTX_INT_CEP_TIMEOUT,
	RTX_INT_EPT,
	RTX_INT_PROTECTION,
	RTX_INT_GET_TX,
	RTX_INT_REVERSE_TEST_READY,
	RTX_INT_REVERSE_TEST_DONE,
	RTX_INT_FOD,
	RTX_INT_EPT_PKT,
	RTX_INT_ERR_CODE,
	RTX_INT_TX_DET_RX,
	RTX_INT_TX_CONFIG,
	RTX_INT_TX_CHS_UPDATE,
	RTX_INT_TX_BLE_CONNECT,
};

enum reverse_charge_mode {
	REVERSE_CHARGE_CLOSE = 0,
	REVERSE_CHARGE_OPEN,
};

enum reverse_charge_state {
	REVERSE_STATE_OPEN,
	REVERSE_STATE_TIMEOUT,
	REVERSE_STATE_ENDTRANS,
	REVERSE_STATE_FORWARD,
	REVERSE_STATE_TRANSFER,
	REVERSE_STATE_WAITPING,
};

enum FW_UPDATE_CMD {
	FW_UPDATE_POWER_ON,
	FW_UPDATE_ERASE = 97,
	FW_UPDATE_USER,
	FW_UPDATE_CHECK,
	FW_UPDATE_FORCE,
	FW_UPDATE_FROM_BIN,
	FW_UPDATE_MAX,
};

enum mca_rev_chg_attr_list {
	MCA_REV_CHG_WIRELESS_CHIP_FW = 0,
	MCA_REV_CHG_WLS_FW_STATE,
	MCA_REV_CHG_REVERSE_CHG_MODE,
	MCA_REV_CHG_REVERSE_CHG_STATE
};

struct mca_wireless_rev_proc_data {
	bool wireless_reverse_closing;
	bool reverse_chg_en;
	bool user_reverse_chg;
	bool bc12_reverse_chg;
	bool batt_missing;
	bool wired_chg_ok;
	int reverse_chg_sts;
	int int_flag;
	//firmware update
	bool fw_updating;
	bool only_check;
	bool from_bin;
	bool force_download;
	bool user_update;
	bool fw_erase;
	bool power_on_update;
	bool wls_sleep_fw_update;
	bool wls_sleep_usb_insert;
	int firmware_update_state;
};

struct mca_wireless_revchg {
	struct device *dev;
	struct mca_votable *usbin_rev_disable_voter;

	//notifier
	struct notifier_block shutdown_notifier;
	//delayed_work
	struct delayed_work monitor_work;
	struct delayed_work reverse_charge_config_work;
	struct delayed_work tx_ping_timeout_work;
	struct delayed_work tx_transfer_timeout_work;
	struct delayed_work disable_tx_work;
	struct delayed_work enable_tx_work;
	struct delayed_work rev_update_to_wire_work;
	struct delayed_work rev_update_to_boost_work;
	struct delayed_work fw_update_work;
	struct delayed_work poweron_update_work;
	struct delayed_work reverse_test_start_work;
	struct delayed_work reverse_test_stop_work;
	struct delayed_work pen_place_err_check_work;
	struct delayed_work pen_data_handle_work;

	//dt config
	int rev_boost_src;
	int rev_boost_default;
	int rev_boost_voltage;
	int support_tx_only;

	struct mca_wireless_rev_proc_data proc_data;

	bool is_switching;
	bool wait_for_reverse_test;
	int force_stop;
	bool tx_timeout_flag;
};

#endif /* __WIRELESS_REVCHG_H__ */
