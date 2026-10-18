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
#define MCA_QUICK_CHG_DIV4_MAX_VOLT 20000
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
#define MCA_QUICK_CHG_OPEN_PATH_CURR 1000
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

/* batt_para */
#define BATT_PARA_MAX_GROUP 16
enum mca_quick_charge_batt_para_ele {
	MCA_QUICK_CHG_BATT_ROLE = 0,
	MCA_QUICK_CHG_BATT_ID,
	MCA_QUICK_CHG_TEMP_PARA_NAME,
	MCA_QUICK_CHG_BATT_PARA_MAX,
};

enum mca_quick_usbpd_dpm_port_pps_ptf_type
{
    USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_SUPPORTED = 0,
    USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_NORMAL,
    USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_WARNING,
    USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_OVERTEMP,
};

/* temp para */
#define TEMP_PARA_MAX_GROUP 10
enum mca_quick_charge_temp_para_ele {
	MCA_QUICK_CHG_TEMP_LOW = 0,
	MCA_QUICK_CHG_TEMP_HIGH,
	MCA_QUICK_CHG_LOW_TEMP_HYS,
	MCA_QUICK_CHG_HIGH_TEMP_HYS,
	MCA_QUICK_CHG_TEMP_MAX_CURRENT,
	MCA_QUICK_CHG_TEMP_NROMAL_FV,
	MCA_QUICK_CHG_TEMP_FFC_FV,
	MCA_QUICK_CHG_VOLT_PARA_NAME,
	MCA_QUICK_CHG_VOLT_FFC_PARA_NAME,
	MCA_QUICK_CHG_STAGE_PARA_NAME,
	MCA_QUICK_CHG_FFC_STAGE_PARA_NAME,
	MCA_QUICK_CHG_TEMP_PARA_MAX,
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
	MCA_QUICK_CHG_CURRENT_MAX,
	MCA_QUICK_CHG_CURRENT_MIN,
	MCA_QUICK_CHG_VOLT_PARA_MAX,
};

struct mca_quick_charge_volt_para {
	int voltage;
	int current_max;
	int current_min;
};

/* volt step para */
#define VSTEP_PARA_MAX_GROUP 16
enum mca_quick_charge_volt_step_para_ele {
	MCA_QUICK_CHG_VOLT_RATIO = 0,
	MCA_QUICK_CHG_CURRENT,
	MCA_QUICK_CHG_VSTEP_RATIO,
	MCA_QUICK_CHG_VSTEP_PARA_MAX,
};

struct mca_quick_charge_volt_step_para {
	int volt_ratio;
	int cur_gap;
	int vstep_ratio;
};

/* single cp limit para */
struct mca_quick_charge_single_cp_limit_para {
	int div1_limit;
	int div2_limit;
	int div4_limit;
};

enum mca_quick_charge_battery_type {
	MCA_BATTERY_TYPE_SINGLE = 0,
	MCA_BATTERY_TYPE_SINGLE_SERIES,	/* single fuelgauge, two cells in series */
	MCA_BATTERY_TYPE_SINGLE_NUM_MAX = MCA_BATTERY_TYPE_SINGLE_SERIES,
	MCA_BATTERY_TYPE_PARALLEL,
	MCA_BATTERY_TYPE_SERIES,
	MCA_BATTERY_TYPE_MAX = MCA_BATTERY_TYPE_SERIES,
};

enum mca_quick_charge_cp_type {
	MCA_CP_TYPE_SINGLE = 0,
	MCA_CP_TYPE_PARALLEL,
	MCA_CP_TYPE_SERIES,
	MCA_CP_TYPE_MAX = MCA_BATTERY_TYPE_SERIES,
};

enum mca_quick_charge_work_cp {
	MCA_QUICK_CHG_CP_MASTER = 0,
	MCA_QUICK_CHG_CP_SLAVE,
	MCA_QUICK_CHG_CP_DUAL,
	MCA_QUICK_CHG_CP_MODE_MAX,
};

enum mac_quick_charge_chn_ele {
	MCA_QUICK_CHG_CH_SINGLE = 0,
	MCA_QUICK_CHG_CH_MULTI,
	MCA_QUICK_CHG_CH_MAX,
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
	MCA_QUICK_CHG_VFC_PARA_FSW,
	MCA_QUICK_CHG_VFC_PARA_SIZE,
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
	CHG_MODE_DIV2,
	CHG_MODE_DIV4,
	CHG_MODE_MAX,
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

enum mca_quick_charge_temp_hys_ele {
	MCA_QUICK_TEMP_HYS_DIS = 0,
	MCA_QUICK_TEMP_HYS_HIGH,
	MCA_QUICK_TEMP_HYS_LOW,
};

enum mca_quick_chg_attr_list {
	MCA_QUICK_CHG_MODE_ENABLE = 0,
	MCA_QUICK_CHG_CP_PATH_ENABLE,
	MCA_QUICK_CHG_POWER_MAX,
	MCA_QUICK_CHG_FAKE_PPS_PTF,
	MCA_QUICK_CHG_TYPE,
	MCA_QUICK_CHG_CURR_LIMIT,
	MCA_QUICK_CHG_CURR_RATIO,
	MCA_QUICK_CHG_VOLT_DEC,
	MCA_QUICK_CHG_VFC_IOUT,
};

enum qc_vbus_voltage_tune {
	DIR_HOLD,
	DIR_UP,
	DIR_DOWN,
};

enum VBUS_TUNE_STAT {
	XM_VBUS_TUNE_INIT,
	XM_VBUS_TUNE_VBUS_LOW,
	XM_VBUS_TUNE_WAIT,
	XM_VBUS_TUNE_READY_UP,
	XM_VBUS_TUNE_READY_DOWN,
	XM_VBUS_TUNE_OK,
	XM_VBUS_TUNE_FAIL,
};

struct secure_quick_charge_data {
	int max_vbatt;
	int secure_cur;
};

struct mca_quick_charge_process_data {
	int adp_type;
	int charge_flag;
	int type_chg;
	int temp_hys_en;
	int total_err;
	int error_num[MCA_QUICK_CHG_CP_MODE_MAX];
	int cur_protocol;
	int work_mode;
	int cur_cp_mode;
	int cur_work_cp;
	int adp_mode;
	int adp_mode_power[CHG_MODE_MAX];
	int ui_power;
	int max_power;
	int quick_charge_type;
	int cur_adp_volt;
	int cur_adp_cur;
	int min_adp_volt;
	int max_adp_volt;
	int max_adp_curr;
	int max_ibat_final;
	int ratio;
	int delta_volt;
	int delta_ibat;
	int single_curr;
	int max_curr;
	int open_path;
	int multi_ibus_th;
	int ibus_inc;
	int ibus_dec;
	int temp_max_cur[FG_IC_MAX];
	int temp_max_fv[FG_IC_MAX];
	int vbat[FG_IC_MAX];
	int parall_vbat[FG_SITE_MAX];
	int max_vcell[FG_IC_MAX];
	int ibat[FG_IC_MAX];
	int parall_ibat[FG_SITE_MAX];
	int soc;
	int ibat_total;
	int vbus;
	int ibus;
	int adp_info_index[CHG_MODE_MAX];
	int *thermal_cur;
	int *cp_path_enable;
	int temp_para_index[FG_IC_MAX];
	int cur_stage[FG_IC_MAX];
	int parall_cur_stage[FG_SITE_MAX];
	int ffc_flag;
	int zone_changed;
	int vfc_iout;
	int ibus_compensation;
	struct mca_quick_charge_adp_info adp_info[ADAPTER_CAP_MAX_NR];
	struct mca_quick_charge_volt_para_info *cur_volt_para[FG_IC_MAX];
	struct mca_quick_charge_volt_para_info *cur_volt_paraller[FG_SITE_MAX];
	struct secure_quick_charge_data secure_info;
	struct mca_quick_charge_ibus_queue_info ibus_queue;
	int sw_ocp_curr;
	bool cp_iic_ok;
	bool stop_charging;
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
	struct device *dev;
	struct mca_votable *input_suspend_voter;
	struct mca_votable *buck_input_voter;
	struct mca_votable *buck_charge_curr_voter;
	struct delayed_work monitor_work;
	struct delayed_work pps_ptf_work;
	struct delayed_work vfc_work;
	struct delayed_work float_vbat_drop_work;
	struct notifier_block shutdown_notifier;
	struct mca_votable *voter[MCA_QUICK_CHG_CH_MAX * CHG_MODE_MAX];
	struct mca_votable *chg_disable_voter;
	struct mca_votable *chg_en_voter;
	struct mca_votable *single_chg_cur_voter;
	struct mca_votable *multi_chg_cur_voter;
	struct mca_quick_charge_smartchg_data smartchg_data;
	/* dts config */
	int batt_type;
	int cp_type;
	int min_vbat;
	int max_vbat;
	int recharge_vbat_delta;
	int die_temp_max;
	int adp_temp_max;
	int en_buck_parallel_chg;
	int fv_hys_delta_mv;
	int curr_terminate_ratio;
	int buck_icl_fcc_curr[2];
	int div_delta_ibat[CHG_MODE_MAX];
	int div_delta_volt[CHG_MODE_MAX];
	int div_single_curr[CHG_MODE_MAX];
	int div_max_curr[CHG_MODE_MAX];
	int open_path_th[CHG_MODE_MAX];
	int multi_ibus_th[CHG_MODE_MAX];
	int ibus_inc_hysteresis[CHG_MODE_MAX];
	int ibus_dec_hysteresis[CHG_MODE_MAX];
	int ibus_compensation[CHG_MODE_MAX];
	int support_mode;
	int max_power;
	int ui_max_power_limit;
	bool is_platform_qc;
	int qc3_max_vbus_limit_mv;
	int qc3p5_max_vbus_limit_mv;
	int qc3_ibat_max_limit_ma;
	int qc3_max_ibus_limit_ma;
	int qc3p5_max_ibus_limit_ma;
	int qc3p5_max_ibat_limit_ma;
	int qc_taper_fcc_ma;
	int pps_taper_fcc_ma;
	int pps_high_taper_fcc_ma;
	int qc_normal_charge_fv_max_mv;
	int qc3_taper_vol_hys;
	int qc3p5_taper_vol_hys;
	int pps_taper_vol_hys;
	int fc2_taper_timer;
	int hardware_cv;
	int rawsoc_swith_pmic_th;
	int has_gbl_batt_para;
	bool taper_done_no_retry;
	bool pd_switch_to_pmic;
	struct mca_quick_charge_volt_step_para vstep_para[VSTEP_PARA_MAX_GROUP];
	struct mca_quick_charge_batt_para_info batt_para[FG_IC_MAX];
	struct mca_quick_charge_batt_para_info base_flip_para[FG_SITE_MAX];
	struct mca_quick_charge_vfc_para vfc_para;
	/* process data */
	int online;
	int cur_support_mode;
	int thermal_cur[CHG_MODE_MAX][MCA_QUICK_CHG_CH_MAX];
	struct mca_quick_charge_process_data proc_data;
	struct mca_quick_charge_sysfs_data sysfs_data;
	int batt_auth;
	int force_stop;
	int tune_vbus_retry;
	int master_cp_enable_count;
	int dtpt_status;
	int trigger_antr_burn;
	int is_eu_model;
	int pps_ptf;
	int fake_pps_ptf;
	int support_curr_monitor;
	int curr_monitor_time_s;
	int cp_switch_pmic_th;
	bool fastchg_temp_flag;
	ktime_t time_start;
	bool boost_done;
	bool check_vbat_ov;
	int support_base_flip;
};

#endif /* __MCA_QUICK_CHARGE_H__ */

