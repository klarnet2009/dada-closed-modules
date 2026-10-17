/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_buckchg_jeita.h
 *
 * mca buck charger jeita control driver
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
#ifndef __MCA_BUCKCHG_JEITA_H__
#define __MCA_BUCKCHG_JEITA_H__

#define MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL 1000
#define MCA_BUCKCHG_JEITA_UPDATE_FAST_INTERVAL 5000
#define MCA_BUCKCHG_JEITA_UPDATE_NORMAL_INTERVAL 3000
#define MCA_BUCKCHG_JEITA_FLIP_CURRENT_DEFAULT_VALUE 2950

#define MCA_BUCKCHG_JEITA_VBAT_HIGH_HYST 50
#define MCA_BUCKCHG_JEITA_VBAT_LOW_HYST 50

#define JEITA_VOLT_DATA_MAX_GROUP 4
enum jeita_volt_para_ele {
	BUCKCHG_VOLTAGE_TH = 0,
	BUCKCHG_CURRENT_MAX,
	BUCKCHG_VOLTAGE_PARA_MAX,
};

enum vi_term_decrease {
	VTERM_DECREASE = 0,
	ITERM_DECREASE,
	DECREASE_VOLTAGE_PARA_MAX,
};

struct mca_buckchg_jeita_volt_data {
	int voltage;
	int max_current;
};

struct mca_buckchg_jeita_volt_para {
	int size;
	struct mca_buckchg_jeita_volt_data *volt_data;
};

#define JEITA_DATA_MAX_GROUP 15
enum jeita_temp_para_ele {
	JEITA_TEMP_PARA_TEMP_LOW = 0,
	JEITA_TEMP_PARA_TEMP_HIGH,
	JEITA_TEMP_PARA_LOW_TEMP_HYS,
	JEITA_TEMP_PARA_HIGH_TEMP_HYS,
	JEITA_TEMP_PARA_MAX_CURRENT,
	JEITA_TEMP_PARA_VTERM,
	JEITA_TEMP_PARA_ITERM,
	JEITA_TEMP_PARA_VOLT_PARA_NAME,
	JEITA_TEMP_PARA_MAX,
};

struct mca_buckchg_jeita_data {
	int temp_low;
	int temp_high;
	int low_temp_hys;
	int high_temp_hys;
	int max_current;
	int vterm;
	int iterm;
	struct mca_buckchg_jeita_volt_para volt_para;
};

struct mca_buckchg_jeita_para {
	int size;
	int fcc_size;
	struct mca_buckchg_jeita_data *jeita_data;
	struct mca_buckchg_jeita_data *jeita_ffc_data;
};

struct mca_buckchg_jeita_proc_data {
	int cur_jeita_index;
	int temp_hys_en;
	int max_chg_curr;
};

struct mca_buck_jeita_smartchg_data {
	int delta_fv;
	int delta_ichg;
};

struct mca_buckchg_jeita_dev {
	struct device *dev;
	int online;
	int voter_ok;
	int dtpt_status;
	int has_gbl_batt_para;
	int vbat_high_hyst;
	int vbat_low_hyst;
	int support_base_flip;
	int high_tbat_stop_chg_fv;
	int real_type;

	struct delayed_work monitor_work;
	struct mca_votable *flip_fcc_voter;
	struct mca_votable *en_voter;
	struct mca_votable *fcc_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *iterm_voter;
	struct mca_buckchg_jeita_proc_data proc_data;
	struct mca_buckchg_jeita_proc_data base_proc_data;
	struct mca_buckchg_jeita_proc_data flip_proc_data;
	/* dts config */
	struct mca_buckchg_jeita_para jeita_para;
	struct mca_buckchg_jeita_para base_jeita_para;
	struct mca_buckchg_jeita_para flip_jeita_para;
	struct mca_buck_jeita_smartchg_data smartchg_data;
	bool baacfg_update;
	int vterm;
	int vi_term_decrease[DECREASE_VOLTAGE_PARA_MAX];
};

#endif /* __MCA_BUCKCHG_JEITA_H__ */
