/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_strategy_buckchg.h
 *
 * mca buck charger strategy driver
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

#ifndef __MCA_STRATEGY_BUCKCHG_H__
#define __MCA_STRATEGY_BUCKCHG_H__

#define STATEGY_INPUT_DEFAULT_VALUE 100
#define STATEGY_INPUT_5V_DEFAULT_VALUE 2000
#define STATEGY_INPUT_9V_DEFAULT_VALUE 1600
#define STATEGY_CHARGE_CURRENT_DEFAULT_VALUE 4000
#define STATEGY_ITERM_DEFAULT_VALUE 500
#define STATEGY_VTERM_DEFAULT_VALUE 4400
#define STATEGY_SUPPORT_MULTI_BUCK 0
#define STATEGY_CHARGE_ENABLE 1
#define STATEGY_CHARGE_DISENABLE 0
#define STATEGY_CHARGE_INPUT_SUSPEND 0
#define STATEGY_CHARGE_INPUT_VOLT_DEFAULT 4600
#define STATEGY_CHARGE_FWS_DEFAULT 5000
#define STATEGY_CHARGE_AICL_VBAT_TH 4100
#define STATEGY_CHARGE_AICL_VBAT_TH_4P2V 4200
#define STATEGY_CHARGE_AICL_TH_4P1V 4100
#define STATEGY_CHARGE_AICL_TH_4P5V 4500
#define STATEGY_CHARGE_AICL_TH_4P4V 4400
#define STATEGY_CHARGE_VBUS_5V 5000
#define STATEGY_CHARGE_VBUS_9V 9000
#define STATEGY_CHARGE_VBUS_12V 12000
#define STAEGY_BATT_MISS_ICL 500
#define STAEGY_BATT_MISS_FV 4200
#define STAEGY_CHARGE_PLATE_SHOCK 600
#define STATEGY_CHARGE_VTERM_LOW_TH 4300

#define STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT 9000
#define STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H 9200
#define STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L 8400

#define CHARGE_PPS_INPUT_DEFAULT 3000
#define CHARGE_DCP_INPUT_DEFAULT 1500
#define CHARGE_DCP_INPUT_BOOST 2000
#define CHARGE_CDP_INPUT_DEFAULT 1500
#define CHARGE_SDP_INPUT_DEFAULT 500
#define CHARGE_FLOAT_INPUT_DEFAULT 1000
#define CHARGE_PPS_PTF_INPUT_DEFAULT 1000
#define CHARGE_PPS_PTF_CHARGE_DEFAULT 1100
#define CHARGE_DCP_CHARGE_DEFAULT 2000
#define CHARGE_DCP_CHARGE_BOOST 2000
#define CHARGE_CDP_CHARGE_DEFAULT 1500
#define CHARGE_SDP_CHARGE_DEFAULT 500
#define CHARGE_FLOAT_CHARGE_DEFAULT 500
#define CHARGE_BATT_AUTH_FAIL_DEFAULT 2000
#define CHARGE_WLS_REVCHG_INPUT_DEFAULT 1000
#define CHARGE_WLS_REVCHG_INPUT_QC2 500
#define CHARGE_MONITOR_WORK_NORMAL_INTERVAL 10000
#define CHARGE_MONITOR_WORK_FAST_INTERVAL 5000
#define WLS_REVCHG_FAST_INTERVAL 3000
#define WLS_REVCHG_NORMAL_INTERVAL 4000
#define WLS_REVCHG_SLOW_INTERVAL 6000
#define SOURCE_STATUS_MONITOR_INTERVAL 5000
#define CHARGE_SW_CV_WORK_NORMAL_INTERVAL 5000
#define CHARGE_SW_CV_WORK_FAST_INTERVAL 2000
#define CHARGE_SW_CV_VBAT_ALARM_DELTA 10

#define ALLOW_QUICK_CHG_SOC_THR    90
#define ALLOW_START_FFC_BATT_SOC_THR	95
#define ALLOW_FFC_TEMP_LOW_THR    20
#define ALLOW_FFC_TEMP_HIGH_THR    48
#define CHARGE_BATT_USE_SC6601A_BUCK 0
#define VBAT_FG_TO_PMIC_RATIO_DEFAULT 1
#define VOTE_BUCK_VTERM_BUF_DEFAULT 0
#define VOTE_BUCK_ITERM_BUF_DEFAULT 0
#define DEFAULT_SUPPORT_HW_BC12 0
#define MCA_WIRE_CHARGE_DEFAULT_IBUS_CURRENT    500
#define MCA_WIRE_CHARGE_DEFAULT_IBAT_CURRENT    500
#define FULL_REPLUG_LIMIT_RAWSOC_TH 9900
#define BUCKCHG_OK_TO_HIGH_IBAT_RAWSOC_TH 9600

enum usbin_wlsrevchg_type {
	REV_USBIN_TYPE_PPS = 0,
	REV_USBIN_TYPE_OTHER,
	REV_USBIN_TYPE_MAX,
};

enum mca_buck_usbpd_dpm_port_pps_ptf_type
{
    USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_SUPPORTED = 0,
    USBPD_BUCK_DPM_PORT_PPS_PTF_NORMAL,
    USBPD_BUCK_DPM_PORT_PPS_PTF_WARNING,
    USBPD_BUCK_DPM_PORT_PPS_PTF_OVERTEMP,
};

/*
 * G13b: layouts below are from the OS4 stock BTF of mca_strategy_buckchg.ko (struct strategy_buckchg_dev, size 2336;
 * strategy_buckchg_proc_data, size 80).  Member NAMES that the stock BTF keeps as unresolved string offsets (?strNNN) are guesses (marked).
 */
/* enums below: OS4 stock BTF of mca_strategy_buckchg.ko */
enum mca_buck_charge_charge_status {
	MCA_BUCK_CHG_NO_CHARGING = 0,
	MCA_BUCK_CHG_STS_CHARGING = 1,
	MCA_BUCK_CHG_STS_CHARGE_DONE = 2,
	MCA_BUCK_CHG_STS_NA = 3,
};

enum MCA_BATT_CHGR_STATUS_TYPE {
	MCA_BATT_CHGR_STATUS_INHIBIT = 0,
	MCA_BATT_CHGR_STATUS_TRICKLE = 1,
	MCA_BATT_CHGR_STATUS_PRECHARGE = 2,
	MCA_BATT_CHGR_STATUS_FULLON = 3,
	MCA_BATT_CHGR_STATUS_TAPER = 4,
	MCA_BATT_CHGR_STATUS_TERMINATION = 5,
	MCA_BATT_CHGR_STATUS_PAUSE = 6,
	MCA_BATT_CHGR_STATUS_CHARGING_DISABLED = 7,
	MCA_BATT_CHGR_STATUS_FAST_LINEAR = 8,
};

enum mca_quick_charge_charge_status {
	MCA_QUICK_CHG_STS_NO_CHARGING = 0,
	MCA_QUICK_CHG_STS_CHARGING = 1,
	MCA_QUICK_CHG_STS_CHARGE_DONE = 2,
	MCA_QUICK_CHG_STS_CHARGE_FAILED = 3,
};

enum vusb_ovp_location {
	VUSB_OVP_NONE = 0,
	VUSB_OVP_BEFORE_CP = 1,
	VUSB_OVP_BEFORE_PMIC = 2,
};

enum reverse_quick_charge_state {
	REVCHG_NORMAL = 0,
	REVCHG_QUICK = 1,
	REVCHG_SCREENON = 2,
	REVCHG_TRY_QUICK = 3,
};

enum cp_vendor {
	SC8541_VENDOR = 0,
	SC8561_VENDOR = 1,
	SC8581_VENDOR = 2,
	SC8585_VENDOR = 3,
	BQ25960_VENDOR = 4,
	SY6541_VENDOR = 5,
	cp_vendor_max = 6,
};

enum otg_src_cfg {
	BOOST_SRC_CHARGER = 0,
	BOOST_SRC_HBOOST = 1,
	BOOST_SRC_EXTERNAL = 2,
	BOOST_SRC_INVALID = 3,
};

enum mca_strategy_support_pps_type {
	MCA_STRATEGY_PPS_DEFAULT = 0,
	MCA_STRATEGY_PPS_100W_CN = 1,
	MCA_STRATEGY_PPS_100W_NON_CN = 2,
	MCA_STRATEGY_PPS_100W_ALL = 3,
};

struct strategy_buckchg_proc_data {
	int online;			/* +0 (name guess) */
	int qc_type;
	int real_type;
	int chg_en;
	int chg_status;
	int ibus_limit;
	int ibat_limit;
	int voltage;			/* +28 (name guess) */
	int vbus;			/* +32 (name guess) */
	int ibus;
	int vbat;
	int ibat;
	int curr_fv;
	int curr_pd_pos;
	bool wls_revchg_init_done;	/* +56 */
	bool pdsuspendsupported;
	bool charge_done_force_5v;
	bool is_pd_9v;
	ktime_t eu_start_time;		/* +64 */
	int pre_volt;			/* +72 */
};

struct mca_smartchg_data {
	int pwr_boost_state;
};

struct otg_incold_threshold {
	int temp_threshold;
	int soc_threshold;
};

#define OTG_INCOLD_THRESHOLD_MAX 5

struct strategy_buckchg_dev {
	struct device *dev;			/* +0 */
	int batt_type;
	int support_multi_buck;
	unsigned int in_dcp;			/* +16 */
	unsigned int in_pd;
	unsigned int in_pps;
	unsigned int in_hvdcp;
	unsigned int in_hvdcp3;
	unsigned int in_hvdcp3p5;
	unsigned int in_cdp;
	unsigned int in_sdp;
	unsigned int in_float;
	unsigned int chg_dcp;			/* +52 */
	unsigned int chg_pd;
	unsigned int chg_pps;
	unsigned int chg_hvdcp;
	unsigned int chg_hvdcp3;
	unsigned int chg_hvdcp3p5;
	unsigned int chg_cdp;
	unsigned int chg_sdp;
	unsigned int chg_float;
	unsigned int chg_batt_auth_failed;	/* +88 */
	unsigned int ffc_temp_low;
	unsigned int ffc_temp_high;
	int pmic_fv_compensation;		/* +100 */
	int bat_temp_fv_comp_cold_th;
	int pmic_fv_compensation_cold;
	int bat_temp_fv_comp_hot_th;
	int pmic_fv_compensation_hot;
	bool support_diff_temp_comp;		/* +120 */
	int pmic_middle_fv_compensation;	/* +124 */
	int pmic_high_fv_compensation;
	int support_pmic_vterm_dynamics_adjust;
	int pmic_wls_fv_compensation;
	int pmic_iterm_compensation;
	int support_reverse_quick_charge;	/* +144 */
	int support_revchg_screenon;
	int support_base_flip;
	int base_flip_same;
	int need_cp_to_pmic;
	int support_charge_more;
	int curr_term_compensation;		/* +168 */
	int rev_req_vadp[REV_USBIN_TYPE_MAX];	/* +172 */
	int rev_vadp_valid_h[REV_USBIN_TYPE_MAX];	/* +180 */
	int rev_vadp_valid_l[REV_USBIN_TYPE_MAX];	/* +188 */
	int vusb_ovp_location;			/* +196 */
	int cp_absent_retry_count;
	bool cp_i2c_error_reported;		/* +204 */
	bool pmic_temp_term_flag;
	int quick_chg_status;			/* +208 */
	struct mca_votable *chg_enable_voter;	/* +216 */
	struct mca_votable *input_suppend_voter;
	struct mca_votable *input_voltage_voter;
	struct mca_votable *input_limit_voter;
	struct mca_votable *charge_limit_voter;
	struct mca_votable *iterm_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *buck_5v_in_voter;
	struct mca_votable *buck_5v_ich_voter;
	struct mca_votable *buck_9v_in_voter;
	struct mca_votable *buck_9v_ich_voter;	/* +296 */
	int buck_5v_in;				/* +304 */
	int buck_5v_ich;
	int buck_9v_in;
	int buck_9v_ich;
	int hvdcp_allow_flag;
	int vbat_ov_count;
	int buck_abnormal_cnt;
	struct mca_smartchg_data smartchg_data;	/* +332 */
	struct strategy_buckchg_proc_data proc_data;	/* +336 */
	struct adapter_power_cap_info pwr_cap;	/* +416 */
	struct delayed_work monitor_work;	/* +560 (name guess) */
	struct delayed_work sw_cv_work;
	struct delayed_work base_flip_sw_cv_work;
	struct delayed_work wls_revchg_monitor_work;
	struct delayed_work csd_pulse_process_work;
	struct delayed_work source_status_monitor_work;
	struct delayed_work check_pd_secret_work;
	struct delayed_work rerun_handle_pd_auth_work;
	struct delayed_work cp_i2c_err_monitor_work;
	struct delayed_work soc_limit_stepper_work;	/* +1784 */
	struct notifier_block thermal_board_nb;	/* +1920 */
	struct notifier_block panel_nb;		/* +1944 */
	int init_ok;				/* +1968 */
	int thermal_board_temp;
	int source_boost_status;
	int force_stop;
	int quick_charge_status;
	int aicl_thd;
	int wls_revchg_en;
	int verify_process_end;
	int batt_auth;
	int pdo_nums;
	bool rev_icl_for_qc2;			/* +2008 */
	bool csd_flag;
	int pps_ptf;				/* +2012 */
	bool is_eu_model;			/* +2016 */
	bool reverse_auth_sts;
	int otg_boost_src;			/* +2020 */
	bool sw_cv_running;			/* +2024 */
	bool is_non_compliant_qc;
	bool non_compliant_run_once;
	int batt_missing;			/* +2028 */
	bool start_quick_revchg;		/* +2032 */
	bool revchg_bcl;
	int allow_start_ffc_batt_soc_thr;	/* +2036 */
	int parallel_iterm;
	bool screen_status;			/* +2044 */
	int ibat_check_cnt;			/* +2048 */
	int last_gear_shift;
	bool wls_online;			/* +2056 */
	bool wls_revchg_online;
	int soc_limit_low;			/* +2060 */
	int soc_limit_high;
	int terminated_by_cp;
	int ffc_terminated_by_cp;
	int soc_limit;
	int sw_cv_fcc_step;			/* +2080 */
	int sw_cv_fv_step;
	int sw_cv_fcc_limit_buffer;
	int cp_vendor;
	int support_pps_100w;			/* +2096 */
	bool support_disable_otg_incold;	/* +2100 */
	bool disable_otg_incold;
	struct mutex otg_incold_mutex;		/* +2104 */
	struct delayed_work otg_monitor_work;	/* +2152 */
	int otg_threshold_size;			/* +2288 */
	int otg_temp_threshold[OTG_INCOLD_THRESHOLD_MAX];	/* +2292 */
	int otg_soc_threshold[OTG_INCOLD_THRESHOLD_MAX];	/* +2312 */
	int drop_fv_cycle;			/* +2332 */
	/* GLOBAL OS4.0.0.7 (stock BTF, G14e): battery-emergency state */
	int batt_emerg;				/* +2336: set to 1 by event BATT_EMERG (85); read in select_charg_para (G23c) */
	bool support_batt_safe_lvl;		/* +2340: BTF name; nothing in the buckchg stock code reads or writes it (G14e: no use found) */
};

_Static_assert(sizeof(struct strategy_buckchg_dev) == 2344, "dev size");  /* GLOBAL 0.7 (CN 0.6: 2336) */
_Static_assert(offsetof(struct strategy_buckchg_dev, batt_emerg) == 2336, "batt_emerg");
_Static_assert(offsetof(struct strategy_buckchg_dev, support_batt_safe_lvl) == 2340, "support_batt_safe_lvl");
_Static_assert(sizeof(struct strategy_buckchg_proc_data) == 80, "proc size");
_Static_assert(offsetof(struct strategy_buckchg_dev, proc_data) == 336, "proc_data");
_Static_assert(offsetof(struct strategy_buckchg_dev, monitor_work) == 560, "monitor_work");
_Static_assert(offsetof(struct strategy_buckchg_dev, otg_monitor_work) == 2152, "otg_monitor_work");
_Static_assert(offsetof(struct strategy_buckchg_dev, drop_fv_cycle) == 2332, "drop_fv_cycle");
#endif /*__MCA_STRATEGY_BUCKCHG_H__ */
