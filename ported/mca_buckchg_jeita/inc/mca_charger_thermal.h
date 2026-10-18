/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_charger_thermal.h
 *
 * mca strategy interface driver
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
#ifndef __MCA_CHARGER_THERMAL_H__
#define __MCA_CHARGER_THERMAL_H__

enum mca_thermal_sysfs_attr_ele {
	MCA_THERMAL_WIRED_CHG_CURR = 0,
	MCA_THERMAL_WIRED_CHG_CURR2,
	MCA_THERMAL_WIRED_CTRL_LIMIT,
	MCA_THERMAL_WIRED_THERMAL_REMOVE,
	MCA_THERMAL_WIRELESS_CHG_CURR,
	MCA_THERMAL_WIRELESS_CTRL_LIMIT,
	MCA_THERMAL_WIRELESS_QUICK_CHG_CTRL_LIMIT,
	MCA_THERMAL_WIRELESS_THERMAL_REMOVE,
};

#define MAX_THERMAL_LEVEL 16
enum mca_thermal_mode_ele {
	THERMAL_MODE_BUCK_5V_IN = 0,
	THERMAL_MODE_BUCK_9V_IN,
	THERMAL_MODE_INLIMIT = THERMAL_MODE_BUCK_9V_IN,
	THERMAL_MODE_BUCK_5V_ICH,
	THERMAL_MODE_BUCK_9V_ICH,
	THERMAL_MODE_DIV1_SINGLE_CURR,
	THERMAL_MODE_DIV1_MULTI_CURR,
	THERMAL_MODE_DIV2_SINGLE_CURR,
	THERMAL_MODE_DIV2_MULTI_CURR,
	THERMAL_MODE_DIV4_SINGLE_CURR,
	THERMAL_MODE_DIV4_MULTI_CURR,
	THERMAL_MODE_MAX,
};

enum mca_wireless_thermal_mode_ele {
	THERMAL_MODE_WIRELESS_BPP_IN = 0,
	THERMAL_MODE_WIRELESS_BPPQC2_IN,
	THERMAL_MODE_WIRELESS_BPPQC3_IN,
	THERMAL_MODE_WIRELESS_EPP_IN,
	THERMAL_MODE_WIRELESS_INLIMIT = THERMAL_MODE_WIRELESS_EPP_IN,
	THERMAL_MODE_WIRELESS_AUTHEN_20W,
	THERMAL_MODE_WIRELESS_AUTHEN_30W,
	THERMAL_MODE_WIRELESS_AUTHEN_50W,
	THERMAL_MODE_WIRELESS_AUTHEN_80W,
	THERMAL_MODE_WIRELESS_AUTHEN_VOICE_BOX,
	THERMAL_MODE_WIRELESS_AUTHEN_MAGNET_30W,
	THERMAL_MODE_WIRELESS_MAX,
};

struct mca_thermal_data {
	int buck_5v_in;
	int buck_9v_in;
	int buck_5v_ich;
	int buck_9v_ich;
	int div1_single_curr;
	int div1_mullti_curr;
	int div2_single_curr;
	int div2_mullti_curr;
	int div4_single_curr;
	int div4_mullti_curr;
};

struct mca_wireless_thermal_data {
	int wireless_bpp_in;
	int wireless_bppqc2_in;
	int wireless_bppqc3_in;
	int wireless_epp_in;
	int wireless_authen_20w;
	int wireless_authen_30w;
	int wireless_authen_50w;
	int wireless_authen_80w;
	int wireless_authen_voice_box;
	int wireless_authen_magnet_30w;
};

struct mca_thermal_ctrl_info {
	int max_level;
	int voter_ok;
	int sic_chg_curr;
	int sic_init_fcc;
	int chg_curr;
	int limit_level;
	int quickchg_limit_level;
	int wired_thermal_remove;
	int wls_thermal_remove;
};

struct mca_buck_jeita_smartchg_data {
	int wls_super_sts;
};

struct mca_sic_data {
	int power_max;
	int sic_init_fcc;
};

#define POWER_MAX_LIST 4
struct mca_sic_data sic_init_list[POWER_MAX_LIST] = {
	{33, 6600},
	{67, 12400},
	{90, 15600},
	{120, 22000},
};

#define BATT_CELL_SELECT_MAX 3

struct mca_thermal_info {
	struct device *dev;
	struct thermal_cooling_device *tcd;
	struct delayed_work init_voter_work;
	int support_wireless;
	int wireless_phone_level;
	struct mca_thermal_data wired_thermal_data[MAX_THERMAL_LEVEL];
	struct mca_wireless_thermal_data wireless_thermal_data[MAX_THERMAL_LEVEL];
	struct mca_votable *wired_voter[THERMAL_MODE_MAX];
	struct mca_votable *wireless_voter[THERMAL_MODE_WIRELESS_MAX];
	struct mca_thermal_ctrl_info wired_ctrl_info;
	struct mca_thermal_ctrl_info wireless_ctrl_info;
	struct mca_buck_jeita_smartchg_data smartchg_data;

	const char *batt_cell_name;
	int support_multi_wired_sic;
	int wired_sic_count;
	const char *wired_sic_select[BATT_CELL_SELECT_MAX];

	int online;
	int wls_online;
	int real_type;
};

#endif /* __MCA_CHARGER_THERMAL_H__ */
