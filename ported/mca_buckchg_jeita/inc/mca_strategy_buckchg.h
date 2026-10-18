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
    USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_NORMAL,
    USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_WARNING,
    USBPD_BUCK_DPM_PORT_PPS_PTF_NOT_OVERTEMP,
};

struct strategy_buckchg_proc_data {
	int online;
	int qc_type;
	int real_type;
	int chg_en;
	int chg_status;
	int ibus_limit;
	int ibat_limit;
	int voltage;
	int vbus;
	int ibus;
	int vbat;
	int ibat;
	int curr_fv;
	int curr_pd_pos;
	bool wls_revchg_init_done;
	bool pdsuspendsupported;
	bool charge_done_force_5v;
	bool is_pd_9v;
	ktime_t eu_start_time;
	int num_pwr_caps;
};

struct mca_smartchg_data {
       int pwr_boost_state;
};

struct strategy_buckchg_dev {
	struct device *dev;
	/* dt config */
	int support_multi_buck;
	int ship_mode_chip;
	unsigned int in_dcp;
	unsigned int in_pd;
	unsigned int in_hvdcp;
	unsigned int in_hvdcp3;
	unsigned int in_hvdcp3p5;
	unsigned int in_cdp;
	unsigned int in_sdp;
	unsigned int in_float;
	unsigned int chg_dcp;
	unsigned int chg_pd;
	unsigned int chg_hvdcp;
	unsigned int chg_hvdcp3;
	unsigned int chg_hvdcp3p5;
	unsigned int chg_cdp;
	unsigned int chg_sdp;
	unsigned int chg_float;
	unsigned int chg_batt_auth_failed;
	unsigned int ffc_temp_low;
	unsigned int ffc_temp_high;
	int pmic_fv_compensation;
	int support_reverse_quick_charge;
	int rev_req_vadp[REV_USBIN_TYPE_MAX];
	int rev_vadp_valid_h[REV_USBIN_TYPE_MAX];
	int rev_vadp_valid_l[REV_USBIN_TYPE_MAX];
	/* voter */
	struct mca_votable *chg_enable_voter;
	struct mca_votable *input_suppend_voter;
	struct mca_votable *input_voltage_voter;
	struct mca_votable *input_limit_voter;
	struct mca_votable *charge_limit_voter;
	struct mca_votable *iterm_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *buck_5v_in_voter;
	struct mca_votable *buck_5v_ich_voter;
	struct mca_votable *buck_9v_in_voter;
	struct mca_votable *buck_9v_ich_voter;
	int buck_5v_in;
	int buck_5v_ich;
	int buck_9v_in;
	int buck_9v_ich;
	int hvdcp_allow_flag;
	int vbat_ov_count;
	/* proc */
	struct mca_smartchg_data smartchg_data;
	struct strategy_buckchg_proc_data proc_data;
	struct adapter_power_cap_info pwr_cap;
	struct delayed_work monitor_work;
	struct delayed_work sw_cv_work;
	struct delayed_work wls_revchg_monitor_work;
	struct delayed_work csd_pulse_process_work;
	struct delayed_work source_status_monitor_work;
	struct delayed_work check_pd_secret_work;
	struct notifier_block thermal_board_nb;

	int thermal_board_temp;
	int source_boost_status;
	int force_stop;
	int quick_charge_status;
	int aicl_thd;
	int wls_revchg_en;
	int verify_process_end;
	int batt_auth;
	int pdo_nums;
	bool rev_icl_for_qc2;
	bool csd_flag;
	int pps_ptf;
	bool is_eu_model;
	bool cp_revert_auth;
	bool sw_cv_running;
	bool is_non_compliant_qc;
	bool non_compliant_run_once;
	unsigned int use_sc_buck;
	int hw_bc12;
	int vbat_fg_to_pmic_ratio;
	int vote_buck_vterm_buf;
	int vote_buck_iterm_buf;
	int sw_cv_vterm_th;
	int full_replug_ichg_limit;
	bool dpdm_detect_done;
};

#endif /*__MCA_STRATEGY_BUCKCHG_H__ */
