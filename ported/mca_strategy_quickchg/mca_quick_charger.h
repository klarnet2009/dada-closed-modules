/* SPDX-License-Identifier: GPL-2.0 */
/*
 *quick_charger.h
 *
 * mca buck charger strategy driver
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
#ifndef __MCA_QUICK_CHARGE_H__
#define __MCA_QUICK_CHARGE_H__
/* G13g: header rebuilt from the OS4 stock BTF; the template-only QC3_* guesses and the 7 empty inline stubs of G13b are REMOVED (call sites restored from the stock disassembly or deleted) */
#include <mca/smartchg/smart_chg_class.h>
/* G13b: smart battery partition structs, layout from the OS4 stock BTF (smart_batt_spec size 32, smart_batt_spec_curve size 12; member names inferred from the use sites, BTF keeps type/step_size/mv as string offsets) */
struct smart_batt_spec_curve {
	int mv;
	int ma_h;
	int ma_l;
};
struct smart_batt_spec {
	int type;
	int ffc;
	struct smart_batt_temp_range t_range;
	int step_size;
	struct smart_batt_spec_curve *steps;
};
_Static_assert(sizeof(struct smart_batt_spec) == 32, "smart_batt_spec size");
enum fg_ic_site { FG_IC_BASE = 0, FG_IC_FLIP = 1, FG_SITE_MAX = 2 };
enum mca_quick_charge_charge_status {
	MCA_QUICK_CHG_STS_NO_CHARGING = 0,
	MCA_QUICK_CHG_STS_CHARGING = 1,
	MCA_QUICK_CHG_STS_CHARGE_DONE = 2,
	MCA_QUICK_CHG_STS_CHARGE_FAILED = 3,
};

enum platform_class_cp_pmid_error_stat { CP_PMID_ERROR_OK = 0, CP_PMID_ERROR_LOW = 1, CP_PMID_ERROR_HIGH = 2 };	/* stock BTF; names of values 1/2 as in BTF */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
#include <linux/usb/pd.h>
#else
#include <linux/usb/usbpd.h>
#endif
#include <linux/of.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>

#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/common/mca_voter.h>

/* default value*/
#define MCA_QUICK_CHG_NAME_LEN 16
#define MCA_QUICK_CHG_MAX_BUFF_LEN 256
#define MCA_QUICK_CHG_MIN_VBAT_DEFAULT 3300
#define MCA_QUICK_CHG_MAX_VBAT_DEFAULT 4400
#define MCA_QUICK_CHG_RECHARGE_VBAT_DELTA_DEFAULT 250
#define MCA_QUICK_CHG_MAX_TDIE_DEFAULT 125
#define MCA_QUICK_CHG_MAX_TADP_DEFAULT 125
#define MCA_QUICK_CHG_DIV1_VOLT_DELTA_DEFAULT 300
#define MCA_QUICK_CHG_DIV2_VOLT_DELTA_DEFAULT 300
#define MCA_QUICK_CHG_DIV4_VOLT_DELTA_DEFAULT 500
#define MCA_QUICK_CHG_SINGLE_DIV4_CURR_TH 14000
#define MCA_QUICK_CHG_SINGLE_DIV2_CURR_TH 12000
#define MCA_QUICK_CHG_SINGLE_DIV1_CURR_TH 5000
#define MCA_QUICK_CHG_BUCK_ICL_CURR_TH 0
#define MCA_QUICK_CHG_BUCK_FCC_CURR_TH 0
#define MCA_QUICK_CHG_PPS_BOOST_FCC_CURR_TH 11000
#define MCA_QUICK_CHG_IBUS_TH_DEFAULT 1500
#define MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT 200
#define MCA_QUICK_CHG_IBUS_DEC_HYS_DEFAULT 200
#define MCA_QUICK_CHG_DIV4_VOLT_TH_HIGH 18000
#define MCA_QUICK_CHG_DIV4_VOLT_TH_LOW 12000
#define MCA_QUICK_CHG_DIV2_VOLT_TH_HIGH 9000
#define MCA_QUICK_CHG_DIV2_VOLT_TH_LOW 6000
#define MCA_QUICK_CHG_DIV1_VOLT_TH_HIGH 4500
#define MCA_QUICK_CHG_DIV1_VOLT_TH_LOW 3600
#define MCA_QUICK_CHG_DIV4_MAX_VOLT 21000 /* G20d: stock cap 21000, no ratio */
#define MCA_QUICK_CHG_DIV2_MAX_VOLT 11000
#define MCA_QUICK_CHG_DIV1_MAX_VOLT 5500
#define MCA_QUICK_CHG_VBAT_MAX 5000
#define MCA_QUICK_CHG_VBAT_SERIES_MAX 10000
#define MCA_QUICK_CHG_DEFAULT_IBAT_DELTA 200
#define MCA_QUICK_CHG_ADP_DEFAULT_VOLT 5000
#define MCA_QUICK_CHG_VBUS_OK_HIGH_TH 12000
#define MCA_QUICK_QC_CHG_ADP_DEFAULT_VOLT 9000
#define MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT 9000
#define MCA_QUICK_CHG_ADP_DEFAULT_CURR 2000
#define MCA_QUICK_CHG_ADP_QC_MAX_VOLT 9500
#define MCA_QUICK_CHG_ADP_QC_MAX_CURR 2500
#define MCA_QUICK_CHG_FLASH_CHG_POWER 20000
#define MCA_QUICK_CHG_TURBO_CHG_POWER 30000
#define MCA_QUICK_CHG_SUPER_CHG_POWER 50000
/* for platform qc3 27w and qc3p5 only begin*/
#define MCA_QUICK_CHG_QC3B_VBUS_LIMIT_DEFAULT 9500
#define MCA_QUICK_CHG_QC3B_IBUS_LIMIT_DEFAULT 2000
#define MCA_QUICK_CHG_QC3P5_VBUS_LIMIT_DEFAULT 10000
#define MCA_QUICK_CHG_QC3P5_IBUS_LIMIT_DEFAULT 2000
#define MCA_QUICK_CHG_QC3_IBAT_LIMIT_DEFAULT 4000
#define MCA_QUICK_CHG_QC_FV_LIMIT_DEFAULT 4440
#define MCA_QUICK_CHG_QC_TAPER_HYS_QC3B 30
#define MCA_QUICK_CHG_QC_TAPER_HYS_QC3P5 20
#define MCA_QUICK_CHG_QC_FV_LIMIT_DEFAULT 4440
#define MCA_TAPER_TIMEOUT 3
#define BUS_VOLTAGE_MIN	3000
#define MCA_QUICK_CHG_QC_TAPER_FCC_THR_DEFAULT 2500
/* schedule value */
#define MCA_QUICK_CHG_NORMAL_INTERVAL 20000
#define MCA_QUICK_CHG_FAST_INTERVAL 700
#define MCA_QUICK_CHG_PPS_PTF_INTERVAL 1500
#define MCA_QUICK_CHG_EVENT_WORK_INTERVAL 5000
#define MCA_QUICK_CHG_EVENT_WORK_SLOW_INTERVAL 60000
#define MCA_QUICK_CHG_MAX_ERR_COUNT 10
#define MCA_QUICK_CHG_SINGLE_MAX_ERR_COUNT 5
#define MCA_QUICK_CHG_OPEN_PATH_CURR 2000	/* G13g: stock open_path and qc_open_path load 0x7d0 (template had 1000) */
#define MCA_QUICK_CHG_OPEN_PATH_IBUS_TH 300
#define MCA_QUICK_CHG_DEFAULT_IBUS_COMPENSATION 0
#define MCA_QUICK_CHG_TUNE_VBUS_MAX_COUNT 15
#define MCA_QUICK_CHG_TUNE_VBUS_INTERVAL 300
#define MCA_QUICK_CHG_TUNE_VBUS_WINDOW_MV 1500
#define MCA_QUICK_CHG_OPEN_PATH_COUNT 10
#define MCA_QUICK_CHG_OPEN_PATH_INTERVAL 200
#define MCA_QUICK_CHG_ADP_GAIN_CURR 200
#define MCA_QUICK_CHG_DEFAULT_VSTEP 20
#define MCA_QUICK_CHG_QC3B_DEFAULT_VSTEP 200
#define MCA_QC_QUICK_CHG_OPEN_PATH_COUNT 40
#define CP_ENABLE_IBUS_UCP_RISING_MA 250
#define ALLOW_ENABLE_CP_BATT_SOC_THR 90
#define ALLOW_START_FFC_BATT_SOC_THR 95
#define MCA_QUICK_CHG_PPS_TAPER_HYS 10
#define MCA_QUICK_CHG_FV_HYS 1
#define MCA_QUICK_CHG_VFC_INTERVAL 5000
#define MCA_QUICK_CHG_CP_DEFAULT_FSW 480
#define MCA_QUICK_CHG_IBUS_QUENE_SIZE 20
#define MCA_QUICK_CHG_SWITCH_PMIC_TH 1500
#define OCP_THRESHOLD_MAINT	9280000
#define OCP_THRESHOLD_FLIP	3540000
#define MCA_ZIMI_CYPRESS_HYS_MV 1000
#define MCA_PPS_MAX_VOLT 10000
#define MCA_THIRD_PARTY_PPS_HYS_MV 1000
#define MCA_QUICK_CHG_MAX_CURR_MA 15600

/*
 * G13g: all enums and structs below are taken from the OS4 stock BTF of mca_strategy_quickchg.ko
 * (work/re/g13g_tools/dump_mca_strategy_quickchg/btf_all.txt, START_ID=141904).  BTF keeps ~30 member names only as
 * string offsets ("?strNNN"); those names are GUESSES (marked GUESS-NAME) taken from the template use sites.
 * Sizes and offsets are pinned by _Static_assert at the end of the file.
 */
/* batt_para */
#define BATT_PARA_MAX_GROUP 16
enum mca_quick_charge_batt_para_ele {
	MCA_QUICK_CHG_BATT_ROLE = 0,
	MCA_QUICK_CHG_BATT_ID = 1,
	MCA_QUICK_CHG_TEMP_PARA_NAME = 2,
	MCA_QUICK_CHG_BATT_PARA_MAX = 3,
};

enum mca_quick_usbpd_dpm_port_pps_ptf_type {
	USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_SUPPORTED = 0,
	USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_NORMAL = 1,	/* stock BTF: ..._PTF_NORMAL (name kept for the template call sites) */
	USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_WARNING = 2,
	USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_OVERTEMP = 3,
};

/* temp para */
#define TEMP_PARA_MAX_GROUP 10
enum mca_quick_charge_temp_para_ele {
	MCA_QUICK_CHG_TEMP_LOW = 0,
	MCA_QUICK_CHG_TEMP_HIGH = 1,
	MCA_QUICK_CHG_LOW_TEMP_HYS = 2,
	MCA_QUICK_CHG_HIGH_TEMP_HYS = 3,
	MCA_QUICK_CHG_TEMP_MAX_CURRENT = 4,
	MCA_QUICK_CHG_TEMP_NROMAL_FV = 5,
	MCA_QUICK_CHG_TEMP_FFC_FV = 6,
	MCA_QUICK_CHG_VOLT_PARA_NAME = 7,
	MCA_QUICK_CHG_VOLT_FFC_PARA_NAME = 8,
	MCA_QUICK_CHG_STAGE_PARA_NAME = 9,
	MCA_QUICK_CHG_FFC_STAGE_PARA_NAME = 10,
	MCA_QUICK_CHG_TEMP_PARA_MAX = 11,
};

struct mca_quick_charge_temp_para {
	int temp_low;
	int temp_high;
	int low_temp_hysteresis;
	int high_temp_hysteresis;
	int max_current;
	int normal_max_fv;
	int ffc_max_fv;
};

/* volt para */
#define VOLT_PARA_MAX_GROUP 10
enum mca_quick_charge_volt_para_ele {
	MCA_QUICK_CHG_VOLTAGE = 0,
	MCA_QUICK_CHG_CURRENT_MAX = 1,
	MCA_QUICK_CHG_CURRENT_MIN = 2,
	MCA_QUICK_CHG_VOLT_PARA_MAX = 3,
};

struct mca_quick_charge_volt_para {
	int voltage;	/* GUESS-NAME (BTF ?str494351) */
	int current_max;
	int current_min;
};

/* volt step para */
#define VSTEP_PARA_MAX_GROUP 16
enum mca_quick_charge_volt_step_para_ele {
	MCA_QUICK_CHG_VOLT_RATIO = 0,
	MCA_QUICK_CHG_CYCLE_COUNT = 1,
	MCA_QUICK_CHG_CURRENT = 2,
	MCA_QUICK_CHG_VBAT_GAP = 3,
	MCA_QUICK_CHG_VSTEP_RATIO = 4,
	MCA_QUICK_CHG_VSTEP_PARA_MAX = 5,
};

struct mca_quick_charge_volt_step_para {
	int volt_ratio;
	int cycle_count;
	int cur_gap;
	int vbat_gap;
	int vstep_ratio;
};

#define VSTEP_PARA_EX_MAX_GROUP 8
enum mca_quick_charge_volt_step_para_ex_ele {
	MCA_QUICK_CHG_DELTA_V_THR = 0,
	MCA_QUICK_CHG_VSTEP_MULTIPLIER = 1,
	MCA_QUICK_CHG_VBAT_CHANGE_THR = 2,
	MCA_QUICK_CHG_BOOST_TIME_THR = 3,
	MCA_QUICK_CHG_VSTEP_PARA_EX_MAX = 4,
};

struct mca_quick_charge_volt_step_ex_para {
	int delta_v_thr;
	int vstep_multiplier;
	int vbat_change_thr;
	long long boost_time_thr;	/* BTF T27, 8 bytes at +16 (type unresolved: GUESS s64) */
};

enum mca_quick_charge_drop_fcc_para_ele {
	MCA_QUICK_CHG_CYCLE = 0,
	MCA_QUICK_CHG_DROP_FCC_STEP = 1,
	MCA_QUICK_CHG_PARA_MAX = 2,
};

enum mca_quick_charge_enable_cp_para_ele {
	MCA_QUICK_CHG_CYCLECOUNT = 0,
	MCA_QUICK_CHG_VBAT_DIFF = 1,
	MCA_QUICK_CHG_SOC_THR = 2,
	MCA_QUICK_CHG_EN_CP_PARA_MAX = 3,
};

struct mca_quick_charge_drop_fcc_para {
	int cyclecount;
	int drop_fcc_step;
};

struct mca_quick_charge_enable_cp_para {
	int cyclecount;
	int vbat_diff;
	int soc_threshold;
};

enum mca_quick_charge_battery_type {
	MCA_BATTERY_TYPE_SINGLE = 0,
	MCA_BATTERY_TYPE_PARALLEL = 1,
	MCA_BATTERY_TYPE_SERIES = 2,
	MCA_BATTERY_TYPE_MAX = 2,
};

enum mca_quick_charge_cp_type {
	MCA_CP_TYPE_SINGLE = 0,
	MCA_CP_TYPE_PARALLEL = 1,
	MCA_CP_TYPE_SERIES = 2,
	MCA_CP_TYPE_MAX = 2,
};

enum mca_quick_charge_work_cp {
	MCA_QUICK_CHG_CP_MASTER = 0,
	MCA_QUICK_CHG_CP_SLAVE = 1,
	MCA_QUICK_CHG_CP_DUAL = 2,
	MCA_QUICK_CHG_CP_MODE_MAX = 3,
};

enum mac_quick_charge_chn_ele {
	MCA_QUICK_CHG_CH_SINGLE = 0,
	MCA_QUICK_CHG_CH_MULTI = 1,
	MCA_QUICK_CHG_CH_MAX = 2,
};

struct mca_quick_charge_volt_para_info {
	int volt_para_size;
	int *stage_para;
	struct mca_quick_charge_volt_para *volt_para;
};

struct mca_quick_charge_temp_para_info {
	struct mca_quick_charge_temp_para temp_para;
	struct mca_quick_charge_volt_para_info volt_info;
	struct mca_quick_charge_volt_para_info volt_ffc_info;
};

struct mca_quick_charge_batt_para_info {
	int temp_para_size;
	struct mca_quick_charge_temp_para_info *temp_info;
};

#define MCA_VFC_PARA_MAX_GROUP 15
enum mca_quick_charge_vfc_para_ele {
	MCA_QUICK_CHG_VFC_PARA_IOUT = 0,
	MCA_QUICK_CHG_VFC_PARA_FSW = 1,
	MCA_QUICK_CHG_VFC_PARA_SIZE = 2,
};

struct mca_quick_charge_vfc_iout_fsw_map {
	int iout;
	int fsw;
};

struct mca_quick_charge_vfc_para {
	int support_cp_vfc;
	int vfc_para_size;
	struct mca_quick_charge_vfc_iout_fsw_map iout_fsw_map[MCA_VFC_PARA_MAX_GROUP];
};

#define MCA_QUICK_CHG_MAX_MODE_CNT 3
enum mca_quick_charge_support_mode {
	MCA_QUICK_CHG_MODE_DIV_1 = 1,
	MCA_QUICK_CHG_MODE_DIV_2 = 2,
	MCA_QUICK_CHG_MODE_DIV_4 = 4,
};

enum mca_quick_charge_chg_mode {
	CHG_MODE_DIV1 = 0,
	CHG_MODE_DIV2 = 1,
	CHG_MODE_DIV4 = 2,
	CHG_MODE_MAX = 3,
};

struct mca_quick_charge_adp_info {
	int adp_mode;
	struct adapter_power_cap cap_info;
};

struct mca_quick_charge_ibus_queue_info {
	int data[MCA_QUICK_CHG_IBUS_QUENE_SIZE];
	int count;
	int index;
	unsigned int sum;
	int avg;
};

#define MCA_QUICK_CHG_IBAT_QUENE_SIZE 5
struct mca_quick_charge_ibat_queue_info {
	int data[MCA_QUICK_CHG_IBAT_QUENE_SIZE];
	int count;
	int index;
	unsigned int sum;
	int avg;
};

enum mca_quick_charge_temp_hys_ele {
	MCA_QUICK_TEMP_HYS_DIS = 0,
	MCA_QUICK_TEMP_HYS_HIGH = 1,
	MCA_QUICK_TEMP_HYS_LOW = 2,
};

enum mca_quick_chg_attr_list {
	MCA_QUICK_CHG_MODE_ENABLE = 0,
	MCA_QUICK_CHG_CP_PATH_ENABLE = 1,
	MCA_QUICK_CHG_POWER_MAX = 2,
	MCA_QUICK_CHG_FAKE_PPS_PTF = 3,
	MCA_QUICK_CHG_TYPE = 4,
	MCA_QUICK_CHG_CURR_LIMIT = 5,
	MCA_QUICK_CHG_CURR_RATIO = 6,
	MCA_QUICK_CHG_VOLT_DEC = 7,
	MCA_QUICK_CHG_VFC_IOUT = 8,
	MCA_QUICK_CHG_CHARING_STAGE = 9,
};

enum qc_vbus_voltage_tune {
	DIR_HOLD = 0,
	DIR_UP = 1,
	DIR_DOWN = 2,
};

enum VBUS_TUNE_STAT {
	XM_VBUS_TUNE_INIT = 0,
	XM_VBUS_TUNE_VBUS_LOW = 1,
	XM_VBUS_TUNE_WAIT = 2,
	XM_VBUS_TUNE_READY_UP = 3,
	XM_VBUS_TUNE_READY_DOWN = 4,
	XM_VBUS_TUNE_OK = 5,
	XM_VBUS_TUNE_FAIL = 6,
};

struct secure_quick_charge_data {
	int max_vbatt;
	int secure_cur;
};

struct cp_work_mode_unit {
	int l_thres;
	int h_thres;
};

struct mca_cp_work_mode {
	bool support_mode_switch;
	int cur_mode;
	int default_mode;
	bool div1_vbat_low;
	bool last_stage;
	struct cp_work_mode_unit mode_table[3];
};

struct mca_quick_charge_process_data {
	int adp_type;
	int charge_flag;
	int type_chg;
	int temp_hys_en;
	int parallel_temp_hys_en[FG_SITE_MAX];
	int total_err;
	int error_num[MCA_QUICK_CHG_CP_MODE_MAX];
	int cur_protocol;
	int work_mode;
	int cur_cp_mode;
	int cur_work_cp;
	int dual_cp_count;
	int adp_mode;
	int adp_mode_power[CHG_MODE_MAX];
	int ui_power;
	int max_power;			/* GUESS-NAME (BTF ?str622979, same string as adapter_power_cap +16) */
	int quick_charge_type;
	int cur_adp_volt;
	int cur_adp_cur;
	int min_adp_volt;
	int max_adp_volt;
	int max_adp_curr;
	int max_ibat_final;
	int max_adap_ibat;
	int ratio;			/* GUESS-NAME (BTF ?str2279597) */
	int delta_volt;
	int delta_ibat;
	int single_curr;
	int max_curr;			/* GUESS-NAME (BTF ?str746853) */
	int non_mi_pps_curr;
	int open_path;
	int multi_ibus_th;
	int again_multi_ibus_th;
	int ibus_inc;
	int ibus_dec;
	int temp_max_cur[FG_IC_MAX];
	int temp_max_fv[FG_IC_MAX];
	int vbat[FG_IC_MAX];
	int parall_vbat[FG_SITE_MAX];
	int ibat[FG_IC_MAX];
	int parall_ibat[FG_SITE_MAX];
	int soc;			/* GUESS-NAME (BTF ?str500662) */
	int ibat_total;
	int vbus;			/* GUESS-NAME (BTF ?str687674) */
	int ibus;
	int count;			/* GUESS-NAME (BTF ?str7244, same string as ibus_queue.count) */
	int adp_info_index[CHG_MODE_MAX];
	int *thermal_cur;
	int *cp_path_enable;
	int temp_para_index[FG_IC_MAX];
	int parall_temp_para_index[FG_SITE_MAX];
	int cur_stage[FG_IC_MAX];
	int parall_cur_stage[FG_SITE_MAX];
	int ffc_flag;
	int parall_ffc_flag[FG_SITE_MAX];
	int parall_zone_changed[FG_SITE_MAX];
	int zone_changed;
	int vfc_iout;
	int ibus_compensation;
	struct mca_quick_charge_adp_info adp_info[ADAPTER_CAP_MAX_NR];
	struct mca_quick_charge_volt_para_info *cur_volt_para[FG_IC_MAX];
	struct mca_quick_charge_volt_para_info *cur_volt_paraller[FG_SITE_MAX];
	struct secure_quick_charge_data secure_info;
	struct mca_quick_charge_ibus_queue_info ibus_queue;
	struct mca_quick_charge_ibat_queue_info ibat_queue;
	int sw_ocp_curr;
	int ocp_thr[FG_SITE_MAX];
	bool stop_charging;
	bool base_batt_close;
	bool to_pd_verify;
	bool parallel_to_pd_verify;
	int base_close_curr;
	bool ban_cp_charging;
	int try_en_cp_count;
	bool exit_cp_charging;
	int decrease_sum;
	int taper_fcc_ma;
	bool drop_float_volt_done;
	int last_vfloat;
	bool ignore_ov_flag;
};

struct mca_quick_charge_sysfs_data {
	int chg_enable;
	int chg_limit[MCA_QUICK_CHG_CH_MAX];
	int mode_enable[CHG_MODE_MAX];
	int cp_path_enable[CHG_MODE_MAX][CP_ROLE_MAX];
	int curr_limit[CHG_MODE_MAX][MCA_QUICK_CHG_CH_MAX];
	int cur_ratio;
	int volt_dec;
};

struct mca_quick_charge_smartchg_data {
	int delta_fv;
	int delta_ichg;
	int pwr_boost_state;
};

struct mca_quick_charge_info {
	struct device *dev;				/* +0 */
	struct mca_votable *input_suspend_voter;
	struct mca_votable *buck_input_voter;
	struct mca_votable *buck_charge_enable_voter;
	struct mca_votable *buck_charge_curr_voter;
	struct mca_votable *buck_vterm_voter;
	struct mca_votable *curr_max_voter;	/* +0x30: NEVER assigned in the GLOBAL 0.7 stock (nor here), so the 'batt_safe' vote on it is a no-op (G17b, D3) */
	struct delayed_work monitor_work;		/* +56, GUESS-NAME */
	struct delayed_work pps_ptf_work;
	struct delayed_work vfc_work;
	struct delayed_work delay_enable_pmic_work;
	struct delayed_work ovp_monitor_work;
	struct notifier_block shutdown_notifier;
	struct mca_votable *voter[MCA_QUICK_CHG_CH_MAX * CHG_MODE_MAX];
	struct mca_votable *chg_disable_voter;
	struct mca_votable *chg_en_voter;
	struct mca_votable *single_chg_cur_voter;
	struct mca_votable *multi_chg_cur_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *thermal_flip_voter;
	struct mca_votable *flip_fcc_voter;
	struct mca_quick_charge_smartchg_data smartchg_data;
	/* dts config */
	int batt_type;
	int cp_type;
	int min_vbat;
	int max_vbat;
	int recharge_vbat;
	int die_temp_max;
	int adp_temp_max;
	int en_buck_parallel_chg;
	int fv_hys_delta_mv;
	int curr_terminate_ratio;
	int curr_terminate_section;
	int buck_icl_fcc_curr[2];
	int div_delta_ibat[CHG_MODE_MAX];
	int div_delta_volt[CHG_MODE_MAX];
	int div_single_curr[CHG_MODE_MAX];
	int div_max_curr[CHG_MODE_MAX];
	int support_pps_100w;
	int non_mi_pps_max_curr[CHG_MODE_MAX];
	int open_path_th[CHG_MODE_MAX];
	int multi_ibus_th[CHG_MODE_MAX];
	int again_multi_ibus_th[CHG_MODE_MAX];
	int ibus_inc_hysteresis[CHG_MODE_MAX];
	int ibus_dec_hysteresis[CHG_MODE_MAX];
	int ibus_compensation[CHG_MODE_MAX];
	int support_mode;				/* GUESS-NAME (BTF ?str507574) */
	int support_ichg_cutoff_priority;
	int max_power;					/* GUESS-NAME (BTF ?str622979) */
	int pmic_fv_compensation;
	int ui_max_power_limit;
	bool is_qcom_qc;
	int qc3_max_vbus_limit_mv;
	int qc3p5_max_vbus_limit_mv;
	int qc3_ibat_max_limit_ma;
	int qc3_max_ibus_limit_ma;
	int qc3p5_max_ibus_limit_ma;
	int qc3p5_max_ibat_limit_ma;
	int qc_taper_fcc_ma;
	int qc_taper_soc_thr;
	int pps_taper_fcc_ma;
	int pps_middle_taper_fcc_ma;
	int pps_high_taper_fcc_ma;
	int qc_normal_charge_fv_max_mv;
	int qc3_taper_vol_hys;
	int qc3p5_taper_vol_hys;
	int pps_taper_vol_hys;
	int fc2_taper_timer;
	int hardware_cv;
	int has_gbl_batt_para;
	int support_pmic_vterm_dynamics_adjust;
	int support_taper_fv_from_volt_para;
	int support_dtpt_ichg_dec;
	int enable_vstep_ex;
	int bypass_curr;
	bool taper_done_no_retry;
	bool pd_switch_to_pmic;
	struct mca_quick_charge_volt_step_para vstep_para[VSTEP_PARA_MAX_GROUP];
	struct mca_quick_charge_volt_step_ex_para vstep_para_ex[VSTEP_PARA_EX_MAX_GROUP];
	struct mca_quick_charge_batt_para_info batt_para[FG_IC_MAX];
	struct mca_quick_charge_batt_para_info base_flip_para[FG_SITE_MAX];
	struct mca_quick_charge_vfc_para vfc_para;
	/* process data */
	int online;					/* GUESS-NAME (BTF ?str11194) */
	int cur_support_mode;
	int thermal_cur[CHG_MODE_MAX][MCA_QUICK_CHG_CH_MAX];
	int thermal_flip_cur;
	struct mca_quick_charge_process_data proc_data;
	struct mca_quick_charge_sysfs_data sysfs_data;
	int batt_auth;
	int batt_missing;
	int force_stop;
	int tune_vbus_retry;
	int master_cp_enable_count;
	int dtpt_status;
	int trigger_antr_burn;
	int shutdown_mode;
	int is_eu_model;
	int pps_ptf;
	int fake_pps_ptf;
	int support_curr_monitor;
	int curr_monitor_time_s;
	int cp_switch_pmic_th;
	int third_pps_switch_pmic;
	int quick_chg_fv_hys;
	int mult_to_single_count;
	int use_buck_fcc_threshold;
	bool use_buck_chg_once_flag;
	bool use_buck_chg_once;
	bool fastchg_temp_flag;
	ktime_t time_start;				/* GUESS-NAME (BTF ?str222839), 8 bytes at +2808 */
	bool boost_done;
	bool check_vbat_ov;
	int support_base_flip;
	int base_flip_same;
	int pmih_fcc;
	bool stop_pmic;
	int current_fcc;
	struct mutex data_lock;				/* BTF T189: 48 bytes at +2840 (GUESS: struct mutex of a DEBUG_SPINLOCK kernel) */
	bool support_cancel_compensation;
	int max_vbat_threshold;
	int cp_vendor;
	struct mca_cp_work_mode cp_work_mode;
	bool pmic_chg_need_fixed_volt;
	bool fcc_too_low;
	int curr_batt_cycle[FG_SITE_MAX];
	bool parallel_fcc_too_low[FG_SITE_MAX];
	int pmic_single_cp_chg;
	int cur_max_threshold[CHG_MODE_MAX];
	int ibus_threshold[CHG_MODE_MAX];
	int pmih_fcc_value[4];				/* BTF T8596, 16 bytes at +2984, unused by direct offset (type GUESS) */
	int vbat_threshold;
	bool use_buck_flag;
	bool support_third_pps_power;
	int third_pps_vbus_max;
	int third_pps_ibus_max;
	int third_pps_ibat_limit;
	int drop_fv_cycle;
	int drop_fv_value;
	int cyclecount;
	int enable_cp_para_size;
	int drop_fcc_para_size;
	struct mca_quick_charge_drop_fcc_para drop_fcc_para[4];
	struct mca_quick_charge_enable_cp_para enable_cp_para[4];
	bool avoid_overvolt_at_high_temp;
	int effective_on_p_generation;
	int anti_overvoltage_strategy;
	bool clear_vstep_open_buckchg;
	int monitor_interval;
	int reduce_max_cur;
	int support_dtpt_reduce_max_cur;
	bool support_eu_power;
	int eu_vbus_max;
	int eu_ibus_max;
	bool switch_normal;
	int curr_max;
	/* GLOBAL OS4.0.0.7 tail (+3168..3207, names/offsets from the global BTF; types of the 1-byte fields GUESSED as bool).
	 * Only batt_safe_active (+3192, byte), batt_emerg (+3196, int) and support_batt_safe_lvl (+3200, byte) are used by the
	 * stock code; the other fields have no reader or writer in the stock module (not set from any DT key here). */
	bool support_100w_enable_loadsw;	/* +3168 */
	bool loadsw_enable;			/* +3169 */
	bool pmic_cp_overtemp;			/* +3170 */
	bool adp_cc_max;			/* +3171 */
	bool adp_cc_max_90;			/* +3172 */
	bool adp_cc_max_95;			/* +3173 */
	bool adapt_cc_mode;			/* +3174 */
	bool first_five_data;			/* +3175 */
	int support_charge_after_cap_change;	/* +3176 */
	int cp_overtemp_threshold;		/* +3180 */
	int cp_overtemp_fcc_limit;		/* +3184 */
	int first_rbus;				/* +3188 */
	bool batt_safe_active;			/* +3192 */
	int batt_emerg;				/* +3196 */
	bool support_batt_safe_lvl;		/* +3200 */
};

_Static_assert(sizeof(struct mca_quick_charge_info) == 3208, "info size");
_Static_assert(offsetof(struct mca_quick_charge_info, batt_safe_active) == 3192, "batt_safe_active");
_Static_assert(offsetof(struct mca_quick_charge_info, batt_emerg) == 3196, "batt_emerg");
_Static_assert(offsetof(struct mca_quick_charge_info, support_batt_safe_lvl) == 3200, "support_batt_safe_lvl");
_Static_assert(sizeof(struct mca_quick_charge_process_data) == 712, "proc size");
_Static_assert(sizeof(struct mca_quick_charge_sysfs_data) == 92, "sysfs size");
_Static_assert(sizeof(struct mca_quick_charge_volt_step_ex_para) == 24, "vstep_ex size");
_Static_assert(sizeof(struct mca_quick_charge_temp_para_info) == 80, "temp_info size");
_Static_assert(sizeof(struct mca_quick_charge_vfc_para) == 128, "vfc size");
_Static_assert(sizeof(struct mca_cp_work_mode) == 40, "cp_work_mode size");
_Static_assert(sizeof(struct mca_quick_charge_ibus_queue_info) == 96, "ibus_queue size");
_Static_assert(sizeof(struct mca_quick_charge_ibat_queue_info) == 36, "ibat_queue size");
_Static_assert(offsetof(struct mca_quick_charge_info, monitor_work) == 56, "monitor_work");
_Static_assert(offsetof(struct mca_quick_charge_info, shutdown_notifier) == 736, "shutdown_notifier");
_Static_assert(offsetof(struct mca_quick_charge_info, voter) == 760, "voter");
_Static_assert(offsetof(struct mca_quick_charge_info, smartchg_data) == 864, "smartchg_data");
_Static_assert(offsetof(struct mca_quick_charge_info, bypass_curr) == 1176, "bypass_curr");
_Static_assert(offsetof(struct mca_quick_charge_info, vstep_para) == 1184, "vstep_para");
_Static_assert(offsetof(struct mca_quick_charge_info, vfc_para) == 1760, "vfc_para");
_Static_assert(offsetof(struct mca_quick_charge_info, proc_data) == 1928, "proc_data");
_Static_assert(offsetof(struct mca_quick_charge_info, sysfs_data) == 2640, "sysfs_data");
_Static_assert(offsetof(struct mca_quick_charge_info, time_start) == 2808, "time_start");
_Static_assert(offsetof(struct mca_quick_charge_info, data_lock) == 2840, "data_lock");
_Static_assert(offsetof(struct mca_quick_charge_info, support_cancel_compensation) == 2888, "support_cancel_compensation");
_Static_assert(offsetof(struct mca_quick_charge_info, cp_work_mode) == 2900, "cp_work_mode");
_Static_assert(offsetof(struct mca_quick_charge_info, pmih_fcc_value) == 2984, "pmih_fcc_value");
_Static_assert(offsetof(struct mca_quick_charge_info, drop_fcc_para) == 3040, "drop_fcc_para");
_Static_assert(offsetof(struct mca_quick_charge_info, enable_cp_para) == 3072, "enable_cp_para");
_Static_assert(offsetof(struct mca_quick_charge_info, curr_max) == 3164, "curr_max");
_Static_assert(offsetof(struct mca_quick_charge_process_data, ibat_queue) == 624, "ibat_queue");
_Static_assert(offsetof(struct mca_quick_charge_process_data, ocp_thr) == 664, "ocp_thr");
_Static_assert(offsetof(struct mca_quick_charge_process_data, ignore_ov_flag) == 708, "ignore_ov_flag");

#endif /* __MCA_QUICK_CHARGE_H__ */
