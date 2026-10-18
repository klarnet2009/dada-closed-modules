/* SPDX-License-Identifier: GPL-2.0 */
/*
 *mca_quick_wireless.h
 *
 * mca quick wireless charger strategy driver
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

#ifndef __MCA_QUICK_WIRELESS_H__
#define __MCA_QUICK_WIRELESS_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
#include <linux/usb/pd.h>
#else
#include <linux/usb/usbpd.h>
#endif
#include <linux/init.h>
#include <linux/slab.h>
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

#include <mca/strategy/strategy_wireless_class.h>
#include <mca/common/mca_voter.h>

/* default value*/
#define MCA_WLS_QUICK_CHG_MIN_VBAT_DEFAULT 3300
#define MCA_WLS_QUICK_CHG_MAX_VBAT_DEFAULT 4400
#define MCA_WLS_QUICK_CHG_RECHARGE_VBAT_DEFAULT 4300
#define MCA_WLS_QUICK_CHG_MAX_TDIE_DEFAULT 125
#define MCA_WLS_QUICK_CHG_MAX_TADP_DEFAULT 125
#define MCA_WLS_QUICK_CHG_SINGLE_DIV4_CURR_TH 9200
#define MCA_WLS_QUICK_CHG_SINGLE_DIV2_CURR_TH 0
#define MCA_WLS_QUICK_CHG_SINGLE_DIV1_CURR_TH 0
#define MCA_WLS_QUICK_CHG_IBUS_TH_DEFAULT 1500
#define MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT 200
#define MCA_WLS_QUICK_CHG_IBUS_DEC_HYS_DEFAULT 200
#define MCA_WLS_QUICK_CHG_DIV4_VOLT_TH_HIGH 18000
#define MCA_WLS_QUICK_CHG_DIV4_VOLT_TH_LOW 12000
#define MCA_WLS_QUICK_CHG_DIV2_VOLT_TH_HIGH 9000
#define MCA_WLS_QUICK_CHG_DIV2_VOLT_TH_LOW 6000
#define MCA_WLS_QUICK_CHG_DIV1_VOLT_TH_HIGH 4500
#define MCA_WLS_QUICK_CHG_DIV1_VOLT_TH_LOW 3600
#define MCA_WLS_QUICK_CHG_DIV4_MAX_VOLT 20000
#define MCA_WLS_QUICK_CHG_DIV2_MAX_VOLT 11000
#define MCA_WLS_QUICK_CHG_DIV1_MAX_VOLT 5500
#define MCA_WLS_QUICK_CHG_VBAT_MAX 5000
#define MCA_WLS_QUICK_CHG_VBAT_SERIES_MAX 10000
#define MCA_WLS_QUICK_CHG_DEFAULT_IBAT_DELTA 200
#define MCA_WLS_QUICK_CHG_ADP_DEFAULT_VOLT 5000
#define MCA_WLS_QUICK_CHG_ADP_DEFAULT_CURR 2000
#define MCA_WLS_QUICK_CHG_SUPER_CHG_POWER 30000
#define MCA_WLS_QUICK_CHG_FLASH_CHG_POWER 20000
/* schedule value */
#define MCA_WLS_QUICK_CHG_FAST_POOLLING_INTERVAL_MS 200
#define MCA_WLS_QUICK_CHG_FAST_INTERVAL_MS 500
#define MCA_WLS_QUICK_CHG_NORMAL_INTERVAL_MS 1000
#define MCA_WLS_QUICK_CHG_SLOW_INTERVAL_MS 2000
#define MCA_WLS_QUICK_CHG_FAST_INTERVAL 1000
#define MCA_WLS_QUICK_CHG_STEP_THRESHOLD_MV 100

#define MCA_WLS_QUICK_CHG_MAX_ERR_COUNT 10
#define MCA_WLS_QUICK_CHG_SINGLE_MAX_ERR_COUNT 5
#define MCA_WLS_QUICK_CHG_OPEN_PATH_IBUS_TH 250
#define MCA_WLS_QUICK_CHG_OPEN_PATH_COUNT 35	/* G13k: stock start_charging +0xc74..+0xc8c compares 0x22/0x23 (was 20 in the template) */
#define MCA_WLS_QUICK_CHG_DEFAULT_VSTEP 8
#define MCA_WLS_QUICK_CHG_VADPT_STEP_MV 100
#define MCA_WLS_QUICK_CHG_PRE_VOLTAGE_LOW_TEMP_TH 15

#define MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_A	8000
#define MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_B	6000
#define MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_A_HY	500
#define MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_B_HY	1000

#define MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_BIG	500
#define MCA_WLS_QUICK_CHG_STARTUP_BIG_ICL_MA	800
#define MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_NORMAL	400
#define MCA_WLS_QUICK_CHG_STARTUP_NORMAL_ICL_MA	700
#define MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_SMALL	200
#define MCA_WLS_QUICK_CHG_STARTUP_SMALL_ICL_MA	500	/* stock start_charging +0x288: 500 (was 700) */
#define MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_MID	400	/* stock: ibat_max >= 5000 in the small branch */
#define MCA_WLS_QUICK_CHG_ICL_AFTER_BQ_SMALL_MA	100

#define MCA_WLS_QUICK_CHG_EXIT_FCC 2200
#define MCA_WLS_QUICK_CHG_EXIT_VOUT 11000
#define MCA_WLS_QUICK_CHG_EXIT_ICL 400
#define MCA_WLS_QUICK_CHG_ENABLE_CP_SOC_TH 90

#define MCA_WLS_QUICK_CHG_CP_VBUS_READY_COUNT 100
#define MCA_WLS_QUICK_CHG_VOUT_ERROR_EXIT_CV_MV 6000

#define MCA_WLS_QUICK_CHG_MAX_MODE_CNT 3
enum mca_wireless_quick_charge_support_mode {
	MCA_WLS_QUICK_CHG_MODE_DIV_1 = 1,
	MCA_WLS_QUICK_CHG_MODE_DIV_2 = 2,
	MCA_WLS_QUICK_CHG_MODE_DIV_4 = 4,
};

/* batt_para */
#define BATT_PARA_MAX_GROUP 16
enum mca_wireless_quick_charge_batt_para_ele {
	MCA_WLS_QUICK_CHG_BATT_ROLE = 0,
	MCA_WLS_QUICK_CHG_BATT_ID,
	MCA_WLS_QUICK_CHG_TEMP_PARA_NAME,
	MCA_WLS_QUICK_CHG_BATT_PARA_MAX,
};

/* temp para */
#define TEMP_PARA_MAX_GROUP 10
enum mca_wireless_quick_charge_temp_para_ele {
	MCA_WLS_QUICK_CHG_TEMP_LOW = 0,
	MCA_WLS_QUICK_CHG_TEMP_HIGH,
	MCA_WLS_QUICK_CHG_LOW_TEMP_HYS,
	MCA_WLS_QUICK_CHG_HIGH_TEMP_HYS,
	MCA_WLS_QUICK_CHG_TEMP_MAX_CURRENT,
	MCA_WLS_QUICK_CHG_TEMP_NROMAL_FV,
	MCA_WLS_QUICK_CHG_TEMP_FFC_FV,
	MCA_WLS_QUICK_CHG_VOLT_PARA_NAME,
	MCA_WLS_QUICK_CHG_VOLT_FFC_PARA_NAME,
	MCA_WLS_QUICK_CHG_STAGE_PARA_NAME,
	MCA_WLS_QUICK_CHG_FFC_STAGE_PARA_NAME,
	MCA_WLS_QUICK_CHG_TEMP_PARA_MAX,
};

/* volt para */
#define VOLT_PARA_MAX_GROUP 10
enum mca_wireless_quick_charge_volt_para_ele {
	MCA_WLS_QUICK_CHG_VOLTAGE = 0,
	MCA_WLS_QUICK_CHG_CURRENT_MAX,
	MCA_WLS_QUICK_CHG_CURRENT_MIN,
	MCA_WLS_QUICK_CHG_VOLT_PARA_MAX,
};

/* volt step para */
#define VSTEP_PARA_MAX_GROUP 16
enum mca_wireless_quick_charge_volt_step_para_ele {
	MCA_WLS_QUICK_CHG_VOLT_RATIO = 0,
	MCA_WLS_QUICK_CHG_CURRENT,
	MCA_WLS_QUICK_CHG_VSTEP_RATIO,
	MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX,
};

enum mca_wireless_quick_charge_charge_work_cp {
	MCA_WLS_QUICK_CHG_CP_MASTER = 0,
	MCA_WLS_QUICK_CHG_CP_SLAVE,
	MCA_WLS_QUICK_CHG_CP_DUAL,
	MCA_WLS_QUICK_CHG_CP_MODE_MAX,
};

enum mca_wireless_quick_charge_battery_type {
	MCA_BATTERY_TYPE_SINGLE = 0,
	MCA_BATTERY_TYPE_PARALLEL,
	MCA_BATTERY_TYPE_SERIES,
	MCA_BATTERY_TYPE_MAX = MCA_BATTERY_TYPE_SERIES,
};

enum mca_wireless_quick_charge_cp_type {
	MCA_CP_TYPE_SINGLE = 0,
	MCA_CP_TYPE_PARALLEL,
	MCA_CP_TYPE_SERIES,
	MCA_CP_TYPE_MAX = MCA_BATTERY_TYPE_SERIES,
};

enum mca_wireless_quick_charge_chn_ele {
	MCA_WLS_QUICK_CHG_CH_SINGLE = 0,
	MCA_WLS_QUICK_CHG_CH_MULTI,
	MCA_WLS_QUICK_CHG_CH_MAX,
};

enum MCA_VBUS_TUNE_STAT {
	MCA_VBUS_TUNE_INIT,
	MCA_VBUS_TUNE_VBUS_LOW,
	MCA_VBUS_TUNE_WAIT,
	MCA_VBUS_TUNE_READY_UP,
	MCA_VBUS_TUNE_READY_DOWN,
	MCA_VBUS_TUNE_OK,
	MCA_VBUS_TUNE_FAIL,
};

struct mca_wireless_quick_charge_volt_para {
	int voltage;
	int current_max;
	int current_min;
};

struct mca_wireless_quick_charge_volt_para_info {
	int volt_para_size;
	int *stage_para;
	struct mca_wireless_quick_charge_volt_para *volt_para;
};

struct mca_wireless_quick_charge_temp_para {
	int temp_low;
	int temp_high;
	int low_temp_hysteresis;
	int high_temp_hysteresis;
	int max_current;
	int normal_max_fv;
	int ffc_max_fv;
};

struct mca_wireless_quick_charge_temp_para_info {
	struct mca_wireless_quick_charge_temp_para temp_para;
	struct mca_wireless_quick_charge_volt_para_info volt_info;
	struct mca_wireless_quick_charge_volt_para_info volt_ffc_info;
};

struct mca_wireless_quick_charge_batt_para_info {
	int temp_para_size;
	struct mca_wireless_quick_charge_temp_para_info *temp_info;
};

struct mca_wireless_quick_charge_volt_step_para {
	int volt_ratio;
	int cur_gap;
	int vstep_ratio;
};

enum mca_wireless_quick_charge_chg_mode {
	CHG_MODE_DIV1 = 0,
	CHG_MODE_DIV2,
	CHG_MODE_DIV4,
	CHG_MODE_MAX,
};

enum mca_wireless_quick_charge_temp_hys_ele {
	MCA_WLS_QUICK_TEMP_HYS_DIS = 0,
	MCA_WLS_QUICK_TEMP_HYS_HIGH,
	MCA_WLS_QUICK_TEMP_HYS_LOW,
};

struct mca_wireless_quick_charge_process_data {
	int charge_flag;
	int charge_flag_pre;
	int temp_hys_en;
	int total_err;
	int error_num[MCA_WLS_QUICK_CHG_CP_MODE_MAX];
	int work_mode;
	int cur_cp_mode;
	int cur_work_cp;
	int adp_mode;
	int ratio;
	int delta_ibat;
	int single_curr;
	int max_curr;
	int cur_adp_volt;
	int cur_adp_curr;
	int multi_ibus_th;
	int ibus_inc;
	int ibus_dec;
	int temp_max_cur[FG_IC_MAX];
	int temp_max_fv[FG_IC_MAX];
	int secure_cur;
	int vbat[FG_IC_MAX];
	int parall_vbat[FG_SITE_MAX];
	int ibat[FG_IC_MAX];
	int parall_ibat[FG_SITE_MAX];
	int ibat_total;
	int vbus;
	int ibus;
	int temp_para_index[FG_IC_MAX];
	int cur_stage[FG_IC_MAX];
	int parall_cur_stage[FG_SITE_MAX];
	int ffc_flag;
	int qc_enable;
	int soc;
	int sw_qc_ichg;
	int sw_thermal_ichg;
	int enable_quickchg;
	int debug_qc_ichg;
	int sw_cv_cur;

	int startup_icl_target_ma;
	int startup_init_up_mv;
	int icl_target_ma;
	int icl_setting_ma;
	int rx_iout_limit;
	int magnet_limit;
	int tune_polling_interval;
	int cp_vendor;		/* OS4 BTF +228 (new) */
	bool zone_changed;	/* OS4 BTF +232 (new) */

	struct wls_adapter_power_cap wls_power;
	struct mca_wireless_quick_charge_volt_para_info *cur_volt_para[FG_IC_MAX];
	struct mca_wireless_quick_charge_volt_para_info *cur_volt_paraller[FG_SITE_MAX];
};

struct mca_wireless_quick_charge_smartchg_data {
	int delta_fv;
	int delta_ichg;
};

/* G13c: laid out member by member from the module BTF of the OS4 stock mca_quick_wireless.ko (size 1008, _Static_assert below).
 * Names that the BTF keeps only as offsets into the base string table are "name guess" (dev, monitor_work, support_mode, online, curr_batt_cycle is BTF). */
struct mca_wireless_quick_charge_info {
	struct device *dev;				/* +0 (name guess) */
	struct mca_votable *input_limit_voter;
	struct mca_votable *chg_enable_voter;		/* OS4 new */
	struct mca_votable *charge_limit_voter;
	struct mca_votable *chg_disable_voter;
	struct mca_votable *single_chg_cur_voter;
	struct mca_votable *multi_chg_cur_voter;
	struct delayed_work monitor_work;		/* +56 (name guess) */
	struct mca_votable *voter[MCA_WLS_QUICK_CHG_CH_MAX * CHG_MODE_MAX];	/* +192 */
	struct mca_votable *wls_thermal_flip_voter;	/* +240 new */
	struct mca_votable *flip_fcc_voter;		/* +248 new */

	//dt_config
	int batt_type;
	int cp_type;
	int min_vbat;
	int max_vbat;
	int recharge_vbat;
	int die_temp_max;
	int adp_temp_max;
	int div_delta_ibat[CHG_MODE_MAX];
	int div_single_curr[CHG_MODE_MAX];
	int div_max_curr[CHG_MODE_MAX];
	int multi_ibus_th[CHG_MODE_MAX];
	int ibus_inc_hysteresis[CHG_MODE_MAX];
	int ibus_dec_hysteresis[CHG_MODE_MAX];
	int support_cp_num_switch;			/* +356 new */
	int wls_switch_st;				/* +360 new */
	int sw_single_cp_fcc;				/* +364 new */
	int support_mode;				/* +368 (name guess) */
	int has_gbl_batt_para;
	int support_hall;
	int vout_set_max;				/* +380 new */
	int sw_cv_taper_reduce_cv;			/* +384 new */
	bool support_base_flip;				/* +388 */
	int pps_high_taper_fcc_ma;			/* +392 new */
	int hardware_cv;				/* +396 new */
	int fc2_taper_timer;				/* +400 new */
	bool taper_done_no_retry;			/* +404 new */
	int wls_thermal_flip_cur;			/* +408 new */
	struct mca_wireless_quick_charge_volt_step_para vstep_para[VSTEP_PARA_MAX_GROUP];
	struct mca_wireless_quick_charge_batt_para_info batt_para[FG_IC_MAX];
	struct mca_wireless_quick_charge_batt_para_info base_flip_para[FG_SITE_MAX];
	struct mca_wireless_quick_charge_smartchg_data smartchg_data;

	//process data
	int online;					/* +680 (name guess) */
	struct mca_wireless_quick_charge_process_data proc_data;
	int force_stop;
	int batt_auth;
	int dtpt_status;
	int tune_vbus_retry;
	bool charge_abnormal;
	bool soc_limit_enable;
	int audio_phone_sts;
	int thermal_phone_flag;
	int curr_batt_cycle[FG_IC_MAX];			/* +996 new */
};

#define QWD_OFF(m, o) _Static_assert(offsetof(struct mca_wireless_quick_charge_info, m) == (o), "quick_charge_info." #m)
QWD_OFF(input_limit_voter, 8); QWD_OFF(chg_enable_voter, 16); QWD_OFF(charge_limit_voter, 24); QWD_OFF(chg_disable_voter, 32);
QWD_OFF(single_chg_cur_voter, 40); QWD_OFF(multi_chg_cur_voter, 48); QWD_OFF(monitor_work, 56); QWD_OFF(voter, 192);
QWD_OFF(wls_thermal_flip_voter, 240); QWD_OFF(flip_fcc_voter, 248); QWD_OFF(batt_type, 256); QWD_OFF(cp_type, 260);
QWD_OFF(min_vbat, 264); QWD_OFF(max_vbat, 268); QWD_OFF(recharge_vbat, 272); QWD_OFF(die_temp_max, 276); QWD_OFF(adp_temp_max, 280);
QWD_OFF(div_delta_ibat, 284); QWD_OFF(div_single_curr, 296); QWD_OFF(div_max_curr, 308); QWD_OFF(multi_ibus_th, 320);
QWD_OFF(ibus_inc_hysteresis, 332); QWD_OFF(ibus_dec_hysteresis, 344); QWD_OFF(support_cp_num_switch, 356); QWD_OFF(wls_switch_st, 360);
QWD_OFF(sw_single_cp_fcc, 364); QWD_OFF(support_mode, 368); QWD_OFF(has_gbl_batt_para, 372); QWD_OFF(support_hall, 376);
QWD_OFF(vout_set_max, 380); QWD_OFF(sw_cv_taper_reduce_cv, 384); QWD_OFF(support_base_flip, 388); QWD_OFF(pps_high_taper_fcc_ma, 392);
QWD_OFF(hardware_cv, 396); QWD_OFF(fc2_taper_timer, 400); QWD_OFF(taper_done_no_retry, 404); QWD_OFF(wls_thermal_flip_cur, 408);
QWD_OFF(vstep_para, 412); QWD_OFF(batt_para, 608); QWD_OFF(base_flip_para, 640); QWD_OFF(smartchg_data, 672); QWD_OFF(online, 680);
QWD_OFF(proc_data, 688); QWD_OFF(force_stop, 968); QWD_OFF(batt_auth, 972); QWD_OFF(dtpt_status, 976); QWD_OFF(tune_vbus_retry, 980);
QWD_OFF(charge_abnormal, 984); QWD_OFF(soc_limit_enable, 985); QWD_OFF(audio_phone_sts, 988); QWD_OFF(thermal_phone_flag, 992);
QWD_OFF(curr_batt_cycle, 996);
_Static_assert(sizeof(struct mca_wireless_quick_charge_info) == 1008, "quick_charge_info size");
_Static_assert(sizeof(struct mca_wireless_quick_charge_process_data) == 280, "quick_charge_process_data size");

#endif /* __MCA_QUICK_WIRELESS_H__ */
