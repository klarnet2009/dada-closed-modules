// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_quickchg.c
 *
 * mca quick charger strategy driver
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
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/reboot.h>
#include <linux/ktime.h>
#include <linux/stddef.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/protocol/protocol_qc_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_interface.h>
#include "mca_quick_charger.h"
#include <mca/smartchg/smart_chg_class.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"
#include <mca/shared_memory/charger_partition_class.h>
#include <mca/platform/platform_loadsw_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_quick_charge"
#endif

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#ifndef abs
#define abs(x) ((x)  > 0 ? (x) : -(x))
#endif

#define MCA_DTPT_MOLECULE_SCALE 8
#define MCA_DTPT_DENOM_SCALE 10
#define MCA_PPS_MAXFCC_PEAK_TIME_S 120
#define MCA_PPS_FCC_LIMIT 6000


/* G13n: both G13g TEMP-SHIM stubs (protocol_class_pd_reset_pps_stage, strategy_class_fg_get_high_temp_vterm) are removed: the stock imports neither */
#define QC3_STEP_SIZE 200		/* stock tune_vbus/open_path immediate 0xc8: confirmed */
#define QC2_FORCE_5V 2			/* stock mca_qc_quick_charge_open_path: mov w1,#2 before protocol_class_qc_set_volt_cmd: confirmed (was GUESS 0) */
#define QC3_SINGLE_INCREMENT 0x20	/* stock regulation: D+ pulse cmd 0x20 */
#define QC3_SINGLE_DECREMENT 0x10	/* stock regulation: D- pulse cmd 0x10 */

/* G13g: prototype missing from the stubs overlay: the platform_buckchg_class wrapper of the OS4 op set_cp_vendor_id(void *data, int *val) */
int platform_class_buckchg_ops_set_cp_vendor_id(unsigned int role, int *val);
static int mca_quick_charge_thermal_flip_voter_cb(struct mca_votable *votable, void *data, int effective_result, const char *effective_client);
static noinline __used int mca_quick_charge_update_jeita_fcc(struct mca_quick_charge_info *info);
static __always_inline bool mca_quick_charge_is_en_buck_hwid(void);
static noinline __used void update_batt_para(struct mca_quick_charge_info *info);

static void strategy_quickchg_map_ibus_to_fsw(struct mca_quick_charge_info *info, int *fsw_target);
static int strategy_quickchg_update_cp_fsw(struct mca_quick_charge_info *info, int fsw_target);
static void strategy_quickchg_enable_buck_charging(struct mca_quick_charge_info *info,
													int buck_icl_val, int buck_fcc_val, bool enable);

static noinline int mca_quick_charge_msleep(int ms, struct mca_quick_charge_info *info)
{
	unsigned int count = (unsigned int)ms / 10;
	int buck_online = 1;

	do {
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &buck_online);
		if (!info->online || info->force_stop || info->proc_data.type_chg || !buck_online ||
		    !info->sysfs_data.chg_enable) {
#line 97
			mca_log_err("sleep fast return, online: %d, force_stop: %d, type_chg: %d, buck_online: %d, sysfs_chg_enable %d\n",
				info->online, info->force_stop, info->proc_data.type_chg, buck_online,
				info->sysfs_data.chg_enable);
			return -1;
		}
		usleep_range(9900, 11000);
	} while (--count);

	return 0;
}

static int mca_quick_charge_update_vbat(struct mca_quick_charge_info *info)
{
	int ret;

	ret = strategy_class_fg_ops_get_voltage(&info->proc_data.vbat[FG_IC_MASTER]);
	if (ret)
		return -1;

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		ret = platform_fg_ops_get_volt(FG_IC_MASTER, &info->proc_data.parall_vbat[FG_IC_BASE]);
		if (ret)
			return -1;
		ret = platform_fg_ops_get_volt(FG_IC_SLAVE, &info->proc_data.parall_vbat[FG_IC_FLIP]);
		if (ret)
			return -1;
	}

	return 0;
}

static int mca_quick_charge_update_ibat(struct mca_quick_charge_info *info)
{
	int ret;
	ret = strategy_class_fg_ops_get_current(&info->proc_data.ibat[FG_IC_MASTER]);
	if (ret)
		return -1;

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		ret = platform_fg_ops_get_curr(FG_IC_MASTER, &info->proc_data.parall_ibat[FG_IC_BASE]);
		if (ret)
			return -1;
		ret = platform_fg_ops_get_curr(FG_IC_SLAVE, &info->proc_data.parall_ibat[FG_IC_FLIP]);
		if (ret)
			return -1;
	}

	info->proc_data.ibat[FG_IC_MASTER] = -1 * info->proc_data.ibat[FG_IC_MASTER] / 1000;
	info->proc_data.parall_ibat[FG_IC_BASE] = -1 * info->proc_data.parall_ibat[FG_IC_BASE] / 1000;
	info->proc_data.parall_ibat[FG_IC_FLIP] = -1 * info->proc_data.parall_ibat[FG_IC_FLIP] / 1000;
	info->proc_data.ibat_total = info->proc_data.ibat[FG_IC_MASTER];
	return 0;
}

/* only support 1S battery, 2P battery todo later */
static void mca_quick_charge_update_system_soc(struct mca_quick_charge_info *info)
{
	info->proc_data.soc = strategy_class_fg_ops_get_soc();
}

static int mca_quick_charge_update_ibus(struct mca_quick_charge_info *info)
{
	int ret;
	int ibus_buck = 0;
	int ibus[CP_ROLE_MAX] = { 0 };

	if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {

		ret = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &ibus[CP_ROLE_MASTER]);
		if (ret) {
			info->proc_data.error_num[CP_ROLE_MASTER]++;
			return -1;
		}
		ret = platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &ibus[CP_ROLE_SLAVE]);
		if (ret) {
			info->proc_data.error_num[CP_ROLE_SLAVE]++;
			return -1;
		}
	} else {
		ret = platform_class_cp_get_bus_current(info->proc_data.cur_work_cp, &ibus[info->proc_data.cur_work_cp]);
		if (ret) {
			info->proc_data.error_num[info->proc_data.cur_work_cp]++;
			return -1;
		}
	}
	info->proc_data.ibus = ibus[CP_ROLE_MASTER] + ibus[CP_ROLE_SLAVE];

	if (info->en_buck_parallel_chg) {
		ret = platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &ibus_buck);
		if (ret) {
#line 187
			mca_log_err("get buck ibus failed!\n");
			return -1;
		}
		info->proc_data.ibus += ibus_buck / 1000;
	}
	return 0;
}

static int mca_qc_quick_charge_update_ibus(struct mca_quick_charge_info *info)
{
	int ret;
	int ibus;

	(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, true);
	ret = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &ibus);
	if (ret) {
		info->proc_data.error_num[CP_ROLE_MASTER]++;
		return -1;
	}

	info->proc_data.ibus = ibus;

	return 0;
}

static int mca_qc_quick_charge_update_vbus(struct mca_quick_charge_info *info)
{
	int ret;
	int vbus_mv;

	ret = platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &vbus_mv);
	if (ret) {
		info->proc_data.error_num[CP_ROLE_MASTER]++;
		return -1;
	}

	info->proc_data.vbus = vbus_mv;

	return 0;
}

/* only support 1S battery now, 2P battery to do later */
/* G13n: rebuilt from the stock disassembly (inlined into monitor_work) */
static __always_inline bool mca_qc_check_if_goto_taper(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_volt_para_info *vinfo = info->proc_data.cur_volt_para[FG_IC_MASTER];
	int fv = vinfo->volt_para[vinfo->volt_para_size - 1].voltage;
	int delta_fv = info->smartchg_data.delta_fv;
	int taper_vol_hys_mv = (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3P5) ?
		info->qc3p5_taper_vol_hys : info->qc3_taper_vol_hys;
	int vbat_th;

	info->qc_normal_charge_fv_max_mv = fv;
	if (!info->support_taper_fv_from_volt_para) {
		vbat_th = fv - (taper_vol_hys_mv + delta_fv) - MCA_QUICK_CHG_FV_HYS;
	} else {
		fv = vinfo->volt_para[vinfo->volt_para_size - 1].voltage;
		if (info->is_qcom_qc) {
			info->qc_normal_charge_fv_max_mv = fv;
			vbat_th = fv - delta_fv - MCA_QUICK_CHG_FV_HYS - taper_vol_hys_mv;
		} else {
			vbat_th = fv - (delta_fv + info->pps_taper_vol_hys);
		}
	}
#line 267
	mca_log_info("qc_normal_charge_fv_max_mv: %d, vbat_th:%d\n",
		info->qc_normal_charge_fv_max_mv, vbat_th);

	if (info->proc_data.vbat[FG_IC_MASTER] > vbat_th &&
		(info->proc_data.ibat_total < info->qc_taper_fcc_ma ||
		 strategy_class_fg_ops_get_soc() > info->qc_taper_soc_thr)) {
		if (info->fc2_taper_timer++ > MCA_TAPER_TIMEOUT) {
			info->taper_done_no_retry = true;
#line 275
			mca_log_info("qc taper done\n");
			return true;
		}
	} else {
		info->fc2_taper_timer = 0;
	}

	return false;
}

static __always_inline bool mca_check_if_goto_hw_taper(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int cur_stage = proc->cur_stage[FG_IC_MASTER];
	struct mca_quick_charge_volt_para_info *vinfo = proc->cur_volt_para[FG_IC_MASTER];
	int section = cur_stage / 2;
	int ibat_th = info->pps_taper_fcc_ma;
	int delta_fv = info->smartchg_data.delta_fv;
	int vbat_th0 = vinfo->volt_para[section].voltage;
	int temp_flag = 0, temp = 0, raw_temp, vbat_th, vbat_now, ibat_now, last_cur_min;
	int ffc_flag = strategy_class_fg_get_fastcharge();

	vinfo = proc->cur_volt_para[FG_IC_MASTER];
	last_cur_min = vinfo->volt_para[vinfo->volt_para_size - 1].current_min;
	if (info->hardware_cv || (info->anti_overvoltage_strategy && info->cyclecount >= info->drop_fv_cycle)) {
		(void)strategy_class_fg_ops_get_temperature(&temp);
		raw_temp = temp;
		temp = temp / 10;
		if (info->base_flip_same) {
			(void)strategy_class_fg_get_temp_offset_flag(&temp_flag);
			if ((temp > 34 || (temp >= 33 && temp_flag)) && ffc_flag)
				ibat_th = info->pps_high_taper_fcc_ma;
		} else if (info->effective_on_p_generation) {
#line 311
			mca_log_info("iterm = %d, taper_fcc = %d\n", last_cur_min, info->pps_taper_fcc_ma);
			ibat_th = max(last_cur_min + 300, info->pps_taper_fcc_ma);
		} else if (raw_temp >= 300 && raw_temp < 400) {
			ibat_th = info->pps_middle_taper_fcc_ma;
		} else if (raw_temp >= 400) {
			ibat_th = info->pps_high_taper_fcc_ma;
		}
	}
	if (info->cyclecount >= info->drop_fv_cycle)
		ibat_th = max(last_cur_min + 300, info->cp_switch_pmic_th);

	vbat_now = proc->vbat[FG_IC_MASTER];
	ibat_now = proc->ibat_total;
	proc->taper_fcc_ma = ibat_th;
	if (section == vinfo->volt_para_size - 1) {
		vbat_th = vbat_th0 - (delta_fv + info->pps_taper_vol_hys + info->quick_chg_fv_hys) - 15;
#line 330
		mca_log_info("check if goto hw taper vbat: %d, vbat_th: %d, ibat: %d, ibat_th: %d, count: %d, quick_chg_fv_hys: %d\n", vbat_now, vbat_th, ibat_now, ibat_th, info->fc2_taper_timer, info->quick_chg_fv_hys);
		if (vbat_now >= vbat_th && ibat_now < ibat_th) {
			if (info->fc2_taper_timer++ > MCA_TAPER_TIMEOUT) {
				info->taper_done_no_retry = true;
#line 335
				mca_log_info("pps taper done\n");
				return true;
			}
		} else {
			info->fc2_taper_timer = 0;
		}
	}

	return false;
}

/* G13g: replaces the template-only mca_eu_pps_limit_power; restored from the OS4 stock (512 bytes, called from regulation and process_event) */
static noinline void mca_eu_pps_anti_disconnection_strategy(struct mca_quick_charge_info *info)
{
	static bool start_flag;
	static bool trigger;
	static int timer_start;
	static int count;
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int ibus = proc->ibus;
	int max_adp_curr = proc->max_adp_curr;
	s64 now;

	if (proc->adp_type != XM_CHARGER_TYPE_PPS)
		return;
	if (!mca_strategy_is_support_pps_100w())
		return;
	if (!info->online) {
		start_flag = false;
		trigger = false;
		count = 0;
		proc->max_adap_ibat = 0;
#line 368
		mca_log_info("reset param\n", timer_start);
		return;
	}
	if (proc->adp_type != XM_CHARGER_TYPE_PPS)
		return;
	if (ibus < max_adp_curr - 200) {
		if (start_flag && count >= 3) {
			start_flag = false;
			count = 0;
#line 397
			mca_log_info(" count %d reset timer for eu_pps max ibus\n", count);
		} else if (start_flag) {
			count++;
			mca_log_info("count %d \n", count);
		}
		return;
	}
	count = 0;
	if (trigger) {
#line 378
		mca_log_info("eu_pps has trigged\n");
		return;
	}
	now = ktime_get_coarse_boottime() / NSEC_PER_SEC;
	if (!start_flag) {
		timer_start = now;
#line 383
		mca_log_info("timer_start: %lld\n", now);
		start_flag = true;
	} else if (now - timer_start >= 120) {
		proc->max_adap_ibat = proc->max_ibat_final * 2 / 3;
		trigger = true;
#line 389
		mca_log_info("trigger:%d max_adap_ibat:[%d] max_ibat_final[%d]\n", 1, proc->max_adap_ibat,
			proc->max_ibat_final);
	}
}

#define PDO_9V_VOLATGE 9000
static bool stategy_quickchg_is_pdo_9v(struct mca_quick_charge_info *info)
{
	struct adapter_power_cap_info cap_info;
	bool is_support_9v = false;
	int ret = 0;

	if (info->proc_data.adp_type != XM_CHARGER_TYPE_PD &&
		info->proc_data.adp_type != XM_CHARGER_TYPE_PPS &&
		info->proc_data.adp_type != XM_CHARGER_TYPE_PD_VERIFY)
		return false;

	ret = protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD, &cap_info);
	if (ret)
		return false;

	if (cap_info.nums == 0) {
#line 427
		mca_log_info(" pd 2.0 pwr_cap.nums is null\n");
		return false;
	}

	for (int i = 0; i < cap_info.nums; i++) {
		if (cap_info.cap[i].max_voltage == cap_info.cap[i].min_voltage &&
			cap_info.cap[i].max_voltage == PDO_9V_VOLATGE) {
			is_support_9v = true;
#line 435
			mca_log_info("pdo[%d] can support 9v: %d\n", i, is_support_9v);
			break;
		}
	}

	return is_support_9v;

}

static int mca_quick_charge_req_adp_volt_and_cur(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int pd_request_volt;

#line 449
	mca_log_info("req adp volt=%d, cur=%d\n", proc->cur_adp_volt, proc->cur_adp_cur);

	/* G13l: stock: both limits must be set, otherwise -1 without a message */
	if (proc->max_adp_curr < 1 || proc->max_adp_volt < 1)
		return -1;
	if (proc->cur_adp_cur > proc->max_adp_curr)
		proc->cur_adp_cur = proc->max_adp_curr;
	if (proc->cur_adp_volt > proc->max_adp_volt)
		proc->cur_adp_volt = proc->max_adp_volt;
	if (proc->adp_type == XM_CHARGER_TYPE_PPS && info->support_third_pps_power &&
		info->third_pps_vbus_max && info->third_pps_ibus_max) {
		if (proc->cur_adp_cur > info->third_pps_ibus_max)
			proc->cur_adp_cur = info->third_pps_ibus_max;
		if (proc->cur_adp_volt > info->third_pps_vbus_max)
			proc->cur_adp_volt = info->third_pps_vbus_max;
#line 468
		mca_log_err("third pps is %d, cur req adp volt=%d, cur=%d\n", 1,
			proc->cur_adp_volt, proc->cur_adp_cur);
	}

	if (!info->pd_switch_to_pmic && proc->cur_adp_volt < proc->min_adp_volt) {
#line 472
		mca_log_err("volt req too low, return err\n");
		return -1;
	}

	switch (proc->adp_type) {
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (!info->pd_switch_to_pmic) {
			protocol_class_set_adapter_volt_and_curr(proc->cur_protocol,
				proc->cur_adp_volt, proc->cur_adp_cur);
		} else {
			if (stategy_quickchg_is_pdo_9v(info) && !info->trigger_antr_burn && !info->shutdown_mode)
				pd_request_volt = MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT;
			else
				pd_request_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
			(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, pd_request_volt);
#line 488
			mca_log_err("PD2.0 REQUEST VOLT = %d\n", pd_request_volt);
		}
		break;
	case XM_CHARGER_TYPE_PD:
		if (stategy_quickchg_is_pdo_9v(info) && !info->trigger_antr_burn && !info->shutdown_mode)
			pd_request_volt = MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT;
		else
			pd_request_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
		(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, pd_request_volt);
#line 497
		mca_log_err("PD2.0 REQUEST VOLT = %d\n", pd_request_volt);
		break;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		protocol_class_qc_set_volt(TYPEC_PORT_0, proc->cur_adp_volt);
		break;
	default:
		break;
	}

	return 0;
}

static void mca_quick_charge_reset_charge_para(struct mca_quick_charge_info *info)
{
	info->online = 0;
	info->is_qcom_qc = false;
	info->taper_done_no_retry = false;
	info->pd_switch_to_pmic = false;
	info->fc2_taper_timer = 0;
	info->proc_data.sw_ocp_curr = 0;
	mca_vote(info->chg_en_voter, "vbat_ovp", false, 1);
	mca_vote(info->chg_en_voter, "lpd", false, 0);
}

/*
 * G13g: CP work-mode helpers restored from the OS4 stock.  NOT WIRED: their stock callers (mca_check_cp_work_mode,
 * mca_cp_check_initial_mode, the 5200-byte regulation) are still the older template, so these two are kept as unused (__used) functions.
 */
static noinline __used int mca_get_cpmode_unrelated_fcc(struct mca_quick_charge_info *info, bool use_secure)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int cur_stage = proc->cur_stage[FG_IC_MASTER];
	int delta_ichg = info->smartchg_data.delta_ichg;
	int cur_max = proc->cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_max;
	int secure_cur, thermal, ret;

#line 1838
	mca_log_info("cur_stage %d cur_max %d delta_cur %d cur_work_cp %d\n",
		cur_stage, cur_max, delta_ichg, proc->cur_work_cp);
	secure_cur = use_secure ? proc->secure_info.secure_cur : 0;
	if (!info->dtpt_status) {
		cur_max = cur_max - delta_ichg;
	} else if (info->support_dtpt_ichg_dec) {
#line 1845
		mca_log_info("support_dtpt_ichg_dec:dtpt_ichg_dec %d\n", delta_ichg);
		cur_max = cur_max * 8 / 10 - delta_ichg;
	} else {
		cur_max = cur_max * 8 / 10;
	}
	thermal = proc->thermal_cur[proc->cur_work_cp == MCA_QUICK_CHG_CP_DUAL];
	if (thermal)
		cur_max = min(cur_max, thermal);
	if (secure_cur)
		cur_max = min(cur_max, secure_cur);
	ret = min(cur_max, proc->temp_max_cur[FG_IC_MASTER]);
#line 1865
	mca_log_info("cur_max %d secure_cur %d temp_max_cur %d thermal_cur %d\n",
		ret, secure_cur, proc->temp_max_cur[FG_IC_MASTER], thermal);
	return ret;
}

static inline bool mca_cp_mode_in_range(const struct cp_work_mode_unit *u, int cur)
{
	/* literal stock compare chain: h_thres <= cur <= l_thres (BTF member order l_thres, h_thres) and h_thres <= l_thres */
	return u->h_thres <= cur && u->l_thres >= cur && u->l_thres >= u->h_thres;
}

/* branch structure of the loops NOT matched to the stock (logic reconstructed from the compare chain) */
static noinline __used int mca_select_cp_work_mode_by_current(struct mca_quick_charge_info *info, int cur)
{
	struct mca_cp_work_mode *wm = &info->cp_work_mode;
	int mode = wm->cur_mode, i;

	if ((unsigned int)mode >= 3) {
#line 1877
		mca_log_err("current work mode abnormal, try to get from proc_data\n");
		switch (info->proc_data.work_mode) {
		case 1:
			mode = 0;
			break;
		case 4:
			mode = 2;
			break;
		case 2:
			mode = 1;
			break;
		default:
#line 1885
			mca_log_err("proc_data work mode abnormal!\n");
			return -1;
		}
		wm->cur_mode = mode;
	}
	if (mca_cp_mode_in_range(&wm->mode_table[mode], cur))
		return mode;
	if (cur > wm->mode_table[mode].l_thres) {
		for (i = mode - 1; i >= 0; i--)
			if (mca_cp_mode_in_range(&wm->mode_table[i], cur))
				return i;
	} else {
		for (i = mode + 1; i < 3; i++)
			if (mca_cp_mode_in_range(&wm->mode_table[i], cur))
				return i;
	}
	return -1;
}

static void mca_quick_charge_stop_charging(struct mca_quick_charge_info *info)
{
	int i, cp, n, ret;
	int vbus_mv = 0;
	int volt;

#line 531
	mca_log_err("stop charge\n");
	platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
	platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
	if (info->vfc_para.support_cp_vfc)
		cancel_delayed_work_sync(&info->vfc_work);
	cp = info->proc_data.cur_work_cp;
	if (cp == MCA_QUICK_CHG_CP_DUAL) {
		platform_class_cp_set_default_fsw(CP_ROLE_MASTER);
		cp = CP_ROLE_SLAVE;
	}
	platform_class_cp_set_default_fsw(cp);
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++)
		memset(&info->proc_data.adp_info[i].cap_info, 0, sizeof(struct adapter_power_cap));
	memset(&info->proc_data.ibus_queue, 0, sizeof(info->proc_data.ibus_queue));
	info->check_vbat_ov = false;
	info->pmih_fcc = 0;
	info->stop_pmic = false;
	info->proc_data.stop_charging = false;
	info->proc_data.dual_cp_count = 0;
	info->proc_data.count = 0;
	info->current_fcc = 0;
	memset(&info->proc_data.ibat_queue, 0, sizeof(info->proc_data.ibat_queue));

	if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B ||
	    info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3P5)
		volt = 9000;
	else if (info->proc_data.adp_type == XM_CHARGER_TYPE_PPS && mca_strategy_is_support_pps_100w())
		volt = 5000;
	else
		volt = 9000;
	info->proc_data.cur_adp_volt = info->trigger_antr_burn ? 5000 : volt;
	info->proc_data.cur_adp_cur = 2000;
	info->pd_switch_to_pmic = true;

	if (info->pmic_chg_need_fixed_volt &&
	    (info->proc_data.adp_type == XM_CHARGER_TYPE_PPS || info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY)) {
		protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, 5000);
		if (stategy_quickchg_is_pdo_9v(info) && !info->trigger_antr_burn)
			protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, 9000);
	} else {
		n = 0;
		while (n++ < 10) {
			platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &vbus_mv);
#line 584
			mca_log_info("avoid vusb_ovp. cp_vbus: %d\n", vbus_mv);
			if (vbus_mv < 12000)
				break;
			ret = mca_quick_charge_req_adp_volt_and_cur(info);
			if (ret)
#line 589
				mca_log_err("stop req volt failed\n");
			platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &vbus_mv);
#line 591
			mca_log_info("avoid vusb_ovp. after request, cp_vbus: %d\n", vbus_mv);
			if (vbus_mv < 12000)
				break;
			mca_quick_charge_msleep(300, info);
		}
	}

	cp = info->proc_data.cur_work_cp;
	if (cp == MCA_QUICK_CHG_CP_DUAL) {
		platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);
		cp = CP_ROLE_SLAVE;
	}
	platform_class_cp_set_mode(cp, CP_MODE_FORWARD_2_1);
	if (info->en_buck_parallel_chg)
		strategy_quickchg_enable_buck_charging(info, 0, 0, false);
#line 610
	mca_log_err("charge_flag = %d\n", info->proc_data.charge_flag);
	if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING && info->support_pmic_vterm_dynamics_adjust) {
		volt = mca_get_client_vote(info->buck_vterm_voter, "jeita");
		mca_vote_override(info->buck_vterm_voter, "pmic_temp_term", true, volt + 30);
		mca_vote_override(info->buck_input_voter, "pmic_temp_term", true, 500);
		mca_quick_charge_msleep(200, info);
	}
	mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
	if (info->support_base_flip || info->base_flip_same) {
		mca_vote_override(info->buck_input_voter, "icl_limit", false, 0);
		mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", false, 0);
		if (info->proc_data.base_batt_close) {
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER,
				mca_get_client_vote(info->buck_vterm_voter, "jeita") + 60);
			mca_vote(info->flip_fcc_voter, "base_close_curr", true, info->proc_data.base_close_curr);
			info->proc_data.base_batt_close = false;
		}
		mca_rerun_election(info->flip_fcc_voter);
	}
	platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
	platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
	memset(info->proc_data.cur_stage, 0, sizeof(info->proc_data.cur_stage));
	memset(&info->proc_data.secure_info, 0, sizeof(info->proc_data.secure_info));
	if (info->pmic_single_cp_chg) {
		mca_vote_override(info->buck_vterm_voter, "single_cp_pmic", false, 0);
		mca_rerun_election(info->buck_vterm_voter);
	}
	info->use_buck_flag = false;
	if (info->proc_data.charge_flag != MCA_QUICK_CHG_STS_CHARGE_DONE)
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_NO_CHARGING;
}

static int mca_quick_charge_adjust_adapter_mode(struct mca_quick_charge_info *info)
{
	int i, ret;
	int mode = 0;
	int ratio = 1;
	int max_volt;
	int adp_type = 0;
	struct adapter_power_cap_info cap_info = { 0 };

	ret = protocol_class_get_adapter_power_cap(info->proc_data.cur_protocol, &cap_info);
	if (ret)
		return ret;

	/* G13l: stock re-reads the adapter type for PPS adapters (PD_VERIFY detection) */
	if (info->proc_data.adp_type == XM_CHARGER_TYPE_PPS) {
		(void)protocol_class_get_adapter_type(TYPEC_PORT_0, &adp_type);
		info->proc_data.adp_type = adp_type;
	}

	if (info->batt_type == MCA_BATTERY_TYPE_SERIES ||
		0)
		ratio = 2;

	for (i = 0; i < cap_info.nums; i++) {
#line 671
		mca_log_info("volt max %d,volt min %d,cur %d\n", cap_info.cap[i].max_voltage,
			cap_info.cap[i].min_voltage, cap_info.cap[i].max_current);

		if (cap_info.cap[i].max_voltage == 0)
			break;

		info->proc_data.adp_info[i].adp_mode = 0;
		memcpy(&info->proc_data.adp_info[i].cap_info,  &cap_info.cap[i], sizeof(struct adapter_power_cap));
		if (cap_info.cap[i].max_voltage >= MCA_QUICK_CHG_DIV4_VOLT_TH_HIGH * ratio &&
			cap_info.cap[i].min_voltage <= MCA_QUICK_CHG_DIV4_VOLT_TH_LOW * ratio) {
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY || info->is_eu_model ||
				info->support_third_pps_power) {
				info->proc_data.adp_info[i].adp_mode |= MCA_QUICK_CHG_MODE_DIV_4;
				mode |= MCA_QUICK_CHG_MODE_DIV_4;
				max_volt = min(MCA_QUICK_CHG_DIV4_MAX_VOLT * ratio, cap_info.cap[i].max_voltage);
				info->proc_data.adp_mode_power[2] = max(info->proc_data.adp_mode_power[2],
					max_volt * cap_info.cap[i].max_current / 1000);
			}
		}

		if (cap_info.cap[i].max_voltage >= MCA_QUICK_CHG_DIV2_VOLT_TH_HIGH * ratio &&
			cap_info.cap[i].min_voltage <= MCA_QUICK_CHG_DIV2_VOLT_TH_LOW * ratio) {
			info->proc_data.adp_info[i].adp_mode |= MCA_QUICK_CHG_MODE_DIV_2;
			mode |= MCA_QUICK_CHG_MODE_DIV_2;
			max_volt = min(MCA_QUICK_CHG_DIV2_MAX_VOLT * ratio, cap_info.cap[i].max_voltage);
			info->proc_data.adp_mode_power[1] = max(info->proc_data.adp_mode_power[1],
				max_volt * cap_info.cap[i].max_current / 1000);
		}

		/* G13l: stock allows the DIV1 mode only for PD_VERIFY adapters */
		if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
			cap_info.cap[i].max_voltage >= MCA_QUICK_CHG_DIV1_VOLT_TH_HIGH * ratio &&
			cap_info.cap[i].min_voltage <= MCA_QUICK_CHG_DIV1_VOLT_TH_LOW * ratio) {
			info->proc_data.adp_info[i].adp_mode |= MCA_QUICK_CHG_MODE_DIV_1;
			mode |= MCA_QUICK_CHG_MODE_DIV_1;
			max_volt = min(MCA_QUICK_CHG_DIV1_MAX_VOLT * ratio, cap_info.cap[i].max_voltage);
			info->proc_data.adp_mode_power[0] = max(info->proc_data.adp_mode_power[0],
				max_volt * cap_info.cap[i].max_current / 1000);
		}

#line 710
		mca_log_info("index %d, max_power %d %d %d\n", i, info->proc_data.adp_mode_power[0], info->proc_data.adp_mode_power[1], info->proc_data.adp_mode_power[2]);
	}

	if (!mode)
		return -1;

	info->proc_data.adp_mode = mode;

	return 0;
}


static int mca_quick_charge_select_cur_work_mode(struct mca_quick_charge_info *info)
{
	int i;
	int ibat_tmp;
	int ibat_max[CHG_MODE_MAX] = { 0 };
	int adp_max_cur[CHG_MODE_MAX] = { 0 };
	int matched[CHG_MODE_MAX] = { 0 };
	int index = 0;
	int adp_index;
	int adp_mode;
	int zimi_cypress_flag = 0;
	struct adapter_power_cap *cap_info;

	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		if (info->proc_data.adp_info[i].cap_info.max_voltage == 0)
			break;

		cap_info = &info->proc_data.adp_info[i].cap_info;
		adp_mode = info->proc_data.adp_info[i].adp_mode;
		if (info->cur_support_mode & adp_mode &	MCA_QUICK_CHG_MODE_DIV_4) {
			ibat_tmp = min(info->div_max_curr[CHG_MODE_DIV4], cap_info->max_current * 4);
			if (ibat_tmp >= ibat_max[CHG_MODE_DIV4] && cap_info->max_current >= adp_max_cur[CHG_MODE_DIV4]) {
				info->proc_data.adp_info_index[CHG_MODE_DIV4] = i;
				ibat_max[CHG_MODE_DIV4] = ibat_tmp;
				adp_max_cur[CHG_MODE_DIV4] = cap_info->max_current;
				matched[CHG_MODE_DIV4]++;
			}
		}

		if (info->cur_support_mode & adp_mode & MCA_QUICK_CHG_MODE_DIV_2) {
			ibat_tmp = min(info->div_max_curr[CHG_MODE_DIV2], cap_info->max_current * 2);
			if (ibat_tmp >= ibat_max[CHG_MODE_DIV2] && cap_info->max_current >= adp_max_cur[CHG_MODE_DIV2]) {
				info->proc_data.adp_info_index[CHG_MODE_DIV2] = i;
				ibat_max[CHG_MODE_DIV2] = ibat_tmp;
				adp_max_cur[CHG_MODE_DIV2] = cap_info->max_current;
				matched[CHG_MODE_DIV2]++;
			}
		}

		if (info->cur_support_mode & adp_mode &	MCA_QUICK_CHG_MODE_DIV_1) {
			ibat_tmp = min(info->div_max_curr[CHG_MODE_DIV1], cap_info->max_current * 1);
			if (ibat_tmp >= ibat_max[CHG_MODE_DIV1] && cap_info->max_current >= adp_max_cur[CHG_MODE_DIV1]) {
				info->proc_data.adp_info_index[CHG_MODE_DIV1] = i;
				ibat_max[CHG_MODE_DIV1] = ibat_tmp;
				adp_max_cur[CHG_MODE_DIV1] = cap_info->max_current;
				matched[CHG_MODE_DIV1]++;
			}
		}
	}

	if(!matched[CHG_MODE_DIV1] && !matched[CHG_MODE_DIV2] && !matched[CHG_MODE_DIV4]) {
		mca_log_err("no cap and cp mode matched!\n");
		return -1;
	}

	for (i = 0; i < CHG_MODE_MAX; i++) {
		mca_log_info("div%ld ibat_max %d index %d matched:%d", BIT(i), ibat_max[i],
			info->proc_data.adp_info_index[i], matched[i]);
		if (ibat_max[i] > ibat_max[index])
			index = i;
	}

	adp_index = info->proc_data.adp_info_index[index];
	info->proc_data.max_ibat_final = ibat_max[index];
#line 779
	mca_log_info("index: %d, adp_index: %d\n", index, adp_index);
	info->proc_data.work_mode = BIT(index);
	info->proc_data.min_adp_volt = info->proc_data.adp_info[adp_index].cap_info.min_voltage;
	info->proc_data.max_adp_volt = info->proc_data.adp_info[adp_index].cap_info.max_voltage;
	info->proc_data.max_adp_curr = info->proc_data.adp_info[adp_index].cap_info.max_current;

	(void)protocol_class_pd_get_zimi_cypress_flag(TYPEC_PORT_0, &zimi_cypress_flag);
	if (zimi_cypress_flag == 1 && info->proc_data.min_adp_volt != info->proc_data.max_adp_volt)
		info->proc_data.max_adp_volt -= MCA_ZIMI_CYPRESS_HYS_MV;

	if (info->proc_data.adp_type == XM_CHARGER_TYPE_PPS && !zimi_cypress_flag
		&& info->proc_data.min_adp_volt != info->proc_data.max_adp_volt
		&& info->proc_data.max_adp_volt > MCA_PPS_MAX_VOLT)
		info->proc_data.max_adp_volt -= MCA_THIRD_PARTY_PPS_HYS_MV;

	mca_log_info("min_adp_volt: %d, max_adp_volt: %d, max_adp_curr: %d, zimi_cypress_flag: %d\n",
		info->proc_data.min_adp_volt, info->proc_data.max_adp_volt,
		info->proc_data.max_adp_curr, zimi_cypress_flag);

	return 0;
}

static void mca_quick_charge_update_work_mode_para(struct mca_quick_charge_info *info)
{
	switch (info->proc_data.work_mode) {
	case MCA_QUICK_CHG_MODE_DIV_1:
		info->proc_data.ratio = MCA_QUICK_CHG_MODE_DIV_1;
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_1_1;
		info->proc_data.delta_volt = info->div_delta_volt[CHG_MODE_DIV1];
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV1];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV1];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV1];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV1];
		info->proc_data.open_path = info->open_path_th[CHG_MODE_DIV1];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV1];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV1];
		info->proc_data.thermal_cur = info->thermal_cur[CHG_MODE_DIV1];
		info->proc_data.cp_path_enable = info->sysfs_data.cp_path_enable[CHG_MODE_DIV1];
		info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV1];
		break;
	case MCA_QUICK_CHG_MODE_DIV_2:
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_2_1;
		info->proc_data.ratio = MCA_QUICK_CHG_MODE_DIV_2;
		info->proc_data.delta_volt = info->div_delta_volt[CHG_MODE_DIV2];
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV2];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV2];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV2];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV2];
		info->proc_data.open_path = info->open_path_th[CHG_MODE_DIV2];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV2];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV2];
		info->proc_data.thermal_cur = info->thermal_cur[CHG_MODE_DIV2];
		info->proc_data.cp_path_enable = info->sysfs_data.cp_path_enable[CHG_MODE_DIV2];
		info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV2];
		break;
	case MCA_QUICK_CHG_MODE_DIV_4:
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_4_1;
		info->proc_data.ratio = MCA_QUICK_CHG_MODE_DIV_4;
		info->proc_data.delta_volt = info->div_delta_volt[CHG_MODE_DIV4];
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV4];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV4];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV4];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV4];
		info->proc_data.open_path = info->open_path_th[CHG_MODE_DIV4];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV4];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV4];
		info->proc_data.thermal_cur = info->thermal_cur[CHG_MODE_DIV4];
		info->proc_data.cp_path_enable = info->sysfs_data.cp_path_enable[CHG_MODE_DIV4];
		info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV4];
		break;
	default:
		break;
	}
	mca_log_info("delta_volt: %d, single_curr: %d, max_curr: %d, work_mode: %d\n",
		info->proc_data.delta_volt, info->proc_data.single_curr,
		info->proc_data.max_curr, info->proc_data.work_mode);
}

#define OVER_CURRENT_COUNT 3
#define BELOW_CURRENT_COUNT 3
#define BASE_FCC_MA_HYS 100
#define BASE_BELOW_FCC_MA_HYS 500
static int mca_quick_get_parallel_volt_para(struct mca_quick_charge_info *info, int fg_index, bool init)
{
	struct mca_quick_charge_batt_para_info *base_flip_para = &(info->base_flip_para[fg_index]);
	struct mca_quick_charge_temp_para *temp_para;
	int i = 0;
	int ret = 0, temp = 0;

	ret = platform_fg_ops_get_temp(fg_index, &temp);
	if (ret) {
#line 894
		mca_log_err("get flip batt temp fail\n");
	}
	temp /= 10;
#line 897
	mca_log_info("fg[%d]_temp = %d\n", fg_index, temp);
	for (i = 0; i < base_flip_para->temp_para_size; i++) {
		temp_para = &base_flip_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high)
			break;
	}
	if (i == base_flip_para->temp_para_size) {
#line 907
		mca_log_err("%d can not find temp_para\n", fg_index);
		return -1;
	}

#line 911
	mca_log_info("fg_index %d, temp_para_index new %d, old %d\n", fg_index, i,
		info->proc_data.parall_temp_para_index[fg_index]);
	if (!temp_para->max_current) {
#line 913
		mca_log_info("%d temp is out of range\n", fg_index);
		if (init)
			return -1;
		if (info->proc_data.parall_temp_para_index[fg_index] < i)
			info->proc_data.parallel_temp_hys_en[fg_index] = MCA_QUICK_TEMP_HYS_HIGH;
		if (info->proc_data.parall_temp_para_index[fg_index] > i)
			info->proc_data.parallel_temp_hys_en[fg_index] = MCA_QUICK_TEMP_HYS_LOW;
		return -1;
	}

	if (init) {
		if (info->proc_data.parallel_temp_hys_en[fg_index] == MCA_QUICK_TEMP_HYS_LOW) {
			if (temp <= temp_para->temp_low + temp_para->low_temp_hysteresis) {
#line 938
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
		} else if (info->proc_data.parallel_temp_hys_en[fg_index] == MCA_QUICK_TEMP_HYS_HIGH) {
			if (temp >= temp_para->temp_high - temp_para->high_temp_hysteresis) {
#line 932
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
		}
		info->proc_data.parallel_temp_hys_en[fg_index] = MCA_QUICK_TEMP_HYS_DIS;
	} else {
		if (info->proc_data.parall_temp_para_index[fg_index] == i)
			return 0;
		if (info->proc_data.temp_para_index[fg_index] < i) {
			if (temp <= temp_para->temp_low + temp_para->low_temp_hysteresis) {
#line 954
				mca_log_info("temperature not low enough, zone not change\n");
				return 0;
			}
		} else if (info->proc_data.temp_para_index[fg_index] > i) {
			if (temp >= temp_para->temp_high - temp_para->high_temp_hysteresis) {
#line 959
				mca_log_info("temperature not high enough, zone not change\n");
				return 0;
			}
		}
	}

	info->proc_data.parall_temp_para_index[fg_index] = i;
	info->proc_data.parall_zone_changed[fg_index] = 1;
	if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
		base_flip_para->temp_info[i].volt_ffc_info.volt_para_size &&
		!info->parallel_fcc_too_low[fg_index]) {
#line 969
		mca_log_info("select ffc volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_ffc_info;
	} else if (base_flip_para->temp_info[i].volt_info.volt_para_size) {
#line 972
		mca_log_info("select normal volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_info;
		if (info->parallel_fcc_too_low[fg_index])
			info->parallel_fcc_too_low[fg_index] = false;
	} else {
#line 977
		mca_log_err("no volt para\n");
		if (info->parallel_fcc_too_low[fg_index])
			info->parallel_fcc_too_low[fg_index] = false;
		return -1;
	}
	return ret;
}

static noinline __used int mca_quick_charge_parallel_judge_temp(struct mca_quick_charge_info *info)
{
	int ret;

	ret = mca_quick_get_parallel_volt_para(info, FG_IC_BASE, true);
	ret |= mca_quick_get_parallel_volt_para(info, FG_IC_FLIP, true);
	return ret;
}


/* G13n: stock inline (monitor_work third-pps/EU block and can_tbat_do_charge base_flip_same block): read the battery current(s) in uA and store mA */
static __always_inline void mca_quick_charge_read_ibat_ma(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;

	if (strategy_class_fg_ops_get_current(&proc->ibat[FG_IC_MASTER]))
		return;
	if (info->batt_type >= 1) {
		if (platform_fg_ops_get_curr(FG_IC_BASE, &proc->parall_ibat[FG_IC_BASE]))
			return;
		if (platform_fg_ops_get_curr(FG_IC_FLIP, &proc->parall_ibat[FG_IC_FLIP]))
			return;
	}
	proc->ibat[FG_IC_MASTER] = -proc->ibat[FG_IC_MASTER] / 1000;
	proc->parall_ibat[FG_IC_BASE] = -proc->parall_ibat[FG_IC_BASE] / 1000;
	proc->parall_ibat[FG_IC_FLIP] = -proc->parall_ibat[FG_IC_FLIP] / 1000;
	proc->ibat_total = proc->ibat[FG_IC_MASTER];
}

/* G13n: rebuilt from the stock disassembly: (info, flag), the flag is "to_pd_verify" and also selects the init branch */
static noinline int mca_quick_charge_can_tbat_do_charge(struct mca_quick_charge_info *info, bool init)
{
	struct mca_quick_charge_batt_para_info *batt_para = &info->batt_para[FG_IC_MASTER];
	struct mca_quick_charge_temp_para *temp_para = NULL;
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int temp = 0, vbat = 0;
	int ret, i;
	bool found = false;

	if (init && proc->adp_type == XM_CHARGER_TYPE_PD_VERIFY) {
		proc->to_pd_verify = false;
#line 1098
		mca_log_info("PD VERIFY SUCCESS\n");
	}

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1103
		mca_log_err("get %d batt temp fail\n", 0);
		return ret;
	}
	temp /= 10;
#line 1107
	mca_log_info("temp=%d\n", temp);
	for (i = 0; i < batt_para->temp_para_size; i++) {
		temp_para = &batt_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			found = true;
			break;
		}
	}

	if (info->base_flip_same) {
		mca_quick_charge_read_ibat_ma(info);
		if (temp >= 48 && proc->ibat_total > temp_para->max_current) {
#line 1119
			mca_log_err("High temperature, high battery current - CP reset\n");
			return -1;
		}
	}

	if (!found) {
#line 1125
		mca_log_err("%d can not find temp_para\n", 0);
		return -1;
	}
#line 1129
	mca_log_info("temp_para_index new %d, old %d\n", i, proc->temp_para_index[FG_IC_MASTER]);
	(void)strategy_class_fg_ops_get_voltage(&vbat);
#line 1131
	mca_log_info("vbat=%d\n", vbat);

	if (!temp_para->max_current ||
		(info->avoid_overvolt_at_high_temp && i >= batt_para->temp_para_size - 2 && vbat >= 4061)) {
#line 1135
		mca_log_info("%d temp is out of range\n", 0);
#line 1136
		mca_log_info("max_current is 0 or avoid over voltage, should set fast charge to false\n");
		proc->ffc_flag = 0;
		strategy_class_fg_set_fastcharge(false);

		if (init)
			return -1;

		if (i > proc->temp_para_index[FG_IC_MASTER])
			proc->temp_hys_en = MCA_QUICK_TEMP_HYS_HIGH;
		if (i < proc->temp_para_index[FG_IC_MASTER]) {
			proc->temp_hys_en = MCA_QUICK_TEMP_HYS_LOW;
			return -1;
		}
		return -1;
	}

	if (init) {
		switch (proc->temp_hys_en) {
		case MCA_QUICK_TEMP_HYS_DIS:
			break;
		case MCA_QUICK_TEMP_HYS_HIGH:
			if (temp >= temp_para->temp_high - temp_para->high_temp_hysteresis) {
#line 1157
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
			break;
		case MCA_QUICK_TEMP_HYS_LOW:
			if (temp <= temp_para->temp_low + temp_para->low_temp_hysteresis) {
#line 1163
				mca_log_info("hys low not satisfied\n");
				return -1;
			}
			break;
		default:
			break;
		}
		proc->temp_hys_en = MCA_QUICK_TEMP_HYS_DIS;
	} else {
		if (i == proc->temp_para_index[FG_IC_MASTER] || proc->temp_max_cur[FG_IC_MASTER] == 0)
			return 0;

		if (i > proc->temp_para_index[FG_IC_MASTER]) {
			if (temp <= temp_para->temp_low + temp_para->low_temp_hysteresis) {
#line 1180
				mca_log_info("temperature not low enough, zone not change\n");
				return 0;
			}
		} else if (i < proc->temp_para_index[FG_IC_MASTER]) {
			if (temp >= temp_para->temp_high - temp_para->high_temp_hysteresis) {
#line 1185
				mca_log_info("temperature not high enough, zone not change\n");
				return 0;
			}
		}
	}

	proc->temp_para_index[FG_IC_MASTER] = i;
	proc->zone_changed = 1;
	proc->temp_max_cur[FG_IC_MASTER] = temp_para->max_current;
	if (proc->adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
		batt_para->temp_info[i].volt_ffc_info.volt_para_size &&
		!info->fcc_too_low && !info->switch_normal) {
#line 1197
		mca_log_info("select ffc volt para\n");
		proc->ffc_flag = 1;
		proc->temp_max_fv[FG_IC_MASTER] = temp_para->ffc_max_fv;
		proc->cur_volt_para[FG_IC_MASTER] = &batt_para->temp_info[i].volt_ffc_info;
	} else if (batt_para->temp_info[i].volt_info.volt_para_size) {
#line 1202
		mca_log_info("select normal volt para\n");
		proc->ffc_flag = 0;
		proc->temp_max_fv[FG_IC_MASTER] = temp_para->normal_max_fv;
		proc->cur_volt_para[FG_IC_MASTER] = &batt_para->temp_info[i].volt_info;
		if (info->fcc_too_low)
			info->fcc_too_low = false;
	} else {
#line 1209
		mca_log_info("no volt para\n");
		proc->ffc_flag = 0;
		if (info->fcc_too_low)
			info->fcc_too_low = false;
		return -1;
	}

	if (hwid && ((hwid->platform_version <= 12 && ((1 << hwid->platform_version) & 0x1810)) ||
		hwid->country_version == 0) &&
		info->support_curr_monitor && info->curr_monitor_time_s &&
		temp_para->max_current >= MCA_QUICK_CHG_MAX_CURR_MA) {
		info->fastchg_temp_flag = true;
#line 1222
		mca_log_info("enter fast charging temp range\n");
	} else {
		info->fastchg_temp_flag = false;
	}

#line 1230
	mca_log_info("temp_para_index=%d, max_cur=%d, max_fv = %d\n", i, temp_para->max_current, proc->temp_max_fv[FG_IC_MASTER]);

	return 0;
}

static int mca_quick_charge_judge_soc(struct mca_quick_charge_info *info)
{
	int system_soc = 0;

	mca_quick_charge_update_system_soc(info);
	system_soc = info->proc_data.soc;

	if (system_soc <= ALLOW_ENABLE_CP_BATT_SOC_THR) {
		return 0;
	}
	else {
		return -1;
	}
}

static __maybe_unused int mca_quick_charge_judge_temp(struct mca_quick_charge_info *info)
{
	int ret;

	ret = mca_quick_charge_can_tbat_do_charge(info, true);
	if (ret)
		return ret;

	return 0;
}

/* G13n: rebuilt from the stock start_charging (inlined there) */
static __always_inline int mca_quick_charge_judge_vbat(struct mca_quick_charge_info *info)
{
	int vbat = 0, vbat_base = 0, vbat_flip = 0;

	(void)strategy_class_fg_ops_get_voltage(&vbat);
	if (info->support_base_flip || info->base_flip_same) {
		(void)platform_fg_ops_get_volt(FG_IC_BASE, &vbat_base);
		(void)platform_fg_ops_get_volt(FG_IC_FLIP, &vbat_flip);
		vbat = min(vbat_base, vbat_flip);
	}
	if (vbat < info->min_vbat || vbat > info->max_vbat) {
#line 1318
		mca_log_info("vbat %d is out of range, try next loop\n", vbat);
		return -1;
	}
	return 0;
}

static __always_inline int mca_quick_charge_judge_enable_cp(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_volt_para_info *vinfo;
	int soc, fv, vbat = 0, ret, i;

	if (!info->anti_overvoltage_strategy)
		return 0;
	soc = strategy_class_fg_ops_get_soc();
	vinfo = info->proc_data.cur_volt_para[FG_IC_MASTER];
	fv = vinfo->volt_para[vinfo->volt_para_size - 1].voltage;
	ret = strategy_class_fg_ops_get_voltage(&vbat);
	ret |= strategy_class_fg_ops_get_cyclecount(&info->cyclecount);
	if (ret) {
#line 1270
		mca_log_err("dbcgx get vbat or cycle count failed\n");
		return -1;
	}
	if (info->cyclecount >= info->drop_fv_cycle)
		fv += info->drop_fv_value;
#line 1277
	mca_log_info("dbcgx ovp_check:vfloat = %d, vbat = %d, cycle_count = %d\n", fv, vbat, info->cyclecount);
	for (i = 0; i < info->enable_cp_para_size; i++) {
		if (info->cyclecount < info->enable_cp_para[i].cyclecount) {
			if (fv - vbat <= info->enable_cp_para[i].vbat_diff || soc >= info->enable_cp_para[i].soc_threshold)
				return -1;
			break;
		}
	}
	return 0;
}

#define QUICKCHG_HIGH_RAWSOC 10000
/* G13n: rebuilt from the stock monitor_work inline (the is_prechk branch is still the template one, stock site not yet decoded) */
static __always_inline int mca_quick_charge_judge_thermal_vote(struct mca_quick_charge_info *info, bool is_prechk)
{
	int thermal_curr = 0;
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);

	if (info->cp_type == 0)
		thermal_curr = proc_data->thermal_cur[MCA_QUICK_CHG_CH_SINGLE];
	else if (info->cp_type == 1)
		thermal_curr = proc_data->thermal_cur[MCA_QUICK_CHG_CH_MULTI];

#line 1333
	mca_log_info("info->thermal_cur: %d %d\n",
		proc_data->thermal_cur[MCA_QUICK_CHG_CH_SINGLE], proc_data->thermal_cur[MCA_QUICK_CHG_CH_MULTI]);

	if (is_prechk) {
		if (thermal_curr < info->cp_switch_pmic_th && thermal_curr > 0) {
			mca_log_info("prechk, skip cp for thermal");
			return -1;
		}
	} else if (thermal_curr > 0 && thermal_curr < info->cp_switch_pmic_th) {
#line 1336
		mca_log_info("skip cp for thermal");
		return -1;
	}

	return 0;
}


static int mca_quick_charge_select_cp(struct mca_quick_charge_info *info)
{
	int i;

	if (info->cp_type == MCA_CP_TYPE_SINGLE) {
		info->proc_data.cur_work_cp = MCA_QUICK_CHG_CP_MASTER;
		return 0;
	}

	for (i = 0; i < MCA_QUICK_CHG_CP_DUAL; i++) {
		if (info->proc_data.error_num[i] < MCA_QUICK_CHG_SINGLE_MAX_ERR_COUNT) {
			info->proc_data.cur_work_cp = i;
			return 0;
		}
	}

	return -1;
}

static int mca_quick_charge_tune_vbus(struct mca_quick_charge_info *info, int *data)
{
	int ret = 0;
	int i = 0;
	int stat = 0;
	int vbus = 0;
	int vbus_high = info->proc_data.cur_adp_volt + 200;
	int vbus_low = info->proc_data.cur_adp_volt - 50;
	int vbus_stat =  XM_VBUS_TUNE_INIT;

	for (i = 0; i < MCA_QUICK_CHG_TUNE_VBUS_MAX_COUNT; i++) {
		ret = platform_class_cp_get_bus_voltage(info->proc_data.cur_work_cp, &vbus);
		ret = platform_class_cp_get_errorhl_stat(info->proc_data.cur_work_cp, &stat);
		if (ret)
			return ret;
#line 1378
		mca_log_info("open path tune vbus: %d, pmid_errorhl_stat: %d\n", vbus, stat);

		if (vbus >= vbus_low && vbus <= vbus_high)  {
#line 1381
			mca_log_info("tune vbus OK: %d\n", vbus);
			vbus_stat = XM_VBUS_TUNE_OK;
			break;
		} else if (vbus < vbus_low) {
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
				info->proc_data.cur_adp_volt += QC3_STEP_SIZE;
			else
				info->proc_data.cur_adp_volt += MCA_QUICK_CHG_DEFAULT_VSTEP * 2;
			vbus_stat = XM_VBUS_TUNE_READY_UP;
		} else {
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
				info->proc_data.cur_adp_volt -= QC3_STEP_SIZE;
			else
				info->proc_data.cur_adp_volt -= MCA_QUICK_CHG_DEFAULT_VSTEP * 2;

			vbus_stat = XM_VBUS_TUNE_READY_DOWN;
		}

		(void)protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
			info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);
		if (mca_quick_charge_msleep(MCA_QUICK_CHG_TUNE_VBUS_INTERVAL, info))
			return -1;
	}

	if (vbus_stat != XM_VBUS_TUNE_OK)
		vbus_stat = XM_VBUS_TUNE_FAIL;
	*data = vbus_stat;

#line 1409
	mca_log_info("tune vbus end vbus_stat: %d\n", vbus_stat);
	return ret;
}

static int mca_quick_charge_open_path(struct mca_quick_charge_info *info)
{
	int ret, i;
	int vbat = 0, ibus = 0, vbus = 0;
	bool cp_enabled = false;
	int vbus_stat = 0;
	int cur_adapt_volt;

	ret = platform_class_cp_enable_adc(info->proc_data.cur_work_cp, true);
	if (info->cp_type == MCA_CP_TYPE_PARALLEL) {
		(void)platform_class_cp_set_mode(CP_ROLE_MASTER, info->proc_data.cur_cp_mode);
		(void)platform_class_cp_set_mode(CP_ROLE_SLAVE, info->proc_data.cur_cp_mode);
	} else {
		ret |= platform_class_cp_set_mode(CP_ROLE_MASTER, info->proc_data.cur_cp_mode);
	}

	if (ret) {
#line 1430
		mca_log_err("set cp_mode or adc failed\n");
		return ret;
	}

	/*step 1. tune vbus = ratio * vbatt + delta_volt*/
	ret = platform_class_cp_get_battery_voltage(info->proc_data.cur_work_cp, &vbat);
	if (ret) {
#line 1442
		mca_log_err("open path get vbat fail\n");
		return ret;
	}

	(void)mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
	(void)mca_vote(info->buck_charge_curr_voter, "wire_qc", true, 0);

	info->proc_data.cur_adp_volt = vbat * info->proc_data.ratio + info->proc_data.delta_volt;
	info->proc_data.cur_adp_cur = MCA_QUICK_CHG_OPEN_PATH_CURR;
	protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
		info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);

	if (mca_quick_charge_msleep(MCA_QUICK_CHG_TUNE_VBUS_INTERVAL, info))
		return -1;

	if (!info->is_qcom_qc)
		mca_quick_charge_tune_vbus(info, &vbus_stat);

	if (vbus_stat != XM_VBUS_TUNE_OK)
		return -1;

	/*step 2. enable cp*/
	ret = platform_class_cp_set_charging_enable(info->proc_data.cur_work_cp, true);
	if (mca_quick_charge_msleep(300, info))
		return -1;
	for (i = 0; i < MCA_QUICK_CHG_OPEN_PATH_COUNT; i++) {
		ret |= platform_class_cp_get_bus_current(info->proc_data.cur_work_cp, &ibus);
		ret |= platform_class_cp_get_battery_voltage(info->proc_data.cur_work_cp, &vbat);
		ret |= platform_class_cp_get_bus_voltage(info->proc_data.cur_work_cp, &vbus);
		ret |= platform_class_cp_get_charging_enabled(info->proc_data.cur_work_cp, &cp_enabled);
		if (ret) {
#line 1475
			mca_log_err("open path get ibus and cp anbale status fail\n");
			return ret;
		}

		if (!cp_enabled) {
			cur_adapt_volt = vbat * info->proc_data.ratio + info->proc_data.delta_volt;
#line 1481
			mca_log_info("open_path_th: cur_adapt_volt %d, last_cur_adp_volt %d\n", cur_adapt_volt, info->proc_data.cur_adp_volt);
			info->proc_data.cur_adp_volt = cur_adapt_volt > info->proc_data.cur_adp_volt ? cur_adapt_volt : info->proc_data.cur_adp_volt;
			ret = platform_class_cp_set_charging_enable(info->proc_data.cur_work_cp, true);
			if (mca_quick_charge_msleep(300, info))
				return -1;
		}
#line 1487
		mca_log_info("open_path_th: %d, ibus[%d]: %d, cp_enabled: %d, vbus: %d, vbat:%d\n",
			info->proc_data.open_path, info->proc_data.cur_work_cp, ibus, cp_enabled, vbus, vbat);

		if (ibus > info->proc_data.open_path)
			break;

		if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
			info->proc_data.cur_adp_volt += QC3_STEP_SIZE;
		else
			info->proc_data.cur_adp_volt += MCA_QUICK_CHG_DEFAULT_VSTEP * 2;

		(void)protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
			info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);
		if (mca_quick_charge_msleep(MCA_QUICK_CHG_OPEN_PATH_INTERVAL, info))
			return -1;
	}
	if (info->fastchg_temp_flag && cp_enabled) {
		info->time_start = ktime_get_boottime_seconds();
	}

	if (!cp_enabled) {
		mca_charge_mievent_report(CHARGE_DFX_CP_OPEN_FAILED, &info->proc_data.cur_work_cp, 1);
		info->proc_data.error_num[info->proc_data.cur_work_cp]++;
		return -1;
	}

	return 0;
}

static int mca_get_qc_vbus_tune_status(struct mca_quick_charge_info *info, enum VBUS_TUNE_STAT *stat)
{
	int ret = 0;
	int vbus_cp_mv = 0;
	static int vbus_cp_mv_before;
	static int vbus_counts;
	int cp_vbus_hl = 0;

	*stat = XM_VBUS_TUNE_INIT;
	ret = platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &vbus_cp_mv);

	if (vbus_cp_mv == vbus_cp_mv_before && vbus_counts++ < 5) {
		*stat = XM_VBUS_TUNE_WAIT;
		return ret;
	}

	vbus_cp_mv_before = vbus_cp_mv;
	vbus_counts = 0;

	if (vbus_cp_mv < BUS_VOLTAGE_MIN) {
		*stat = XM_VBUS_TUNE_VBUS_LOW;
		return ret;
	}

	(void)platform_class_cp_get_errorhl_stat(CP_ROLE_MASTER, &cp_vbus_hl);
	if (cp_vbus_hl == CP_PMID_ERROR_OK &&
		abs(vbus_cp_mv - info->proc_data.cur_adp_volt) <= MCA_QUICK_CHG_TUNE_VBUS_WINDOW_MV) {
		*stat = XM_VBUS_TUNE_OK;
		return ret;
	} else if (cp_vbus_hl == CP_PMID_ERROR_LOW ||
		info->proc_data.cur_adp_volt - vbus_cp_mv >= MCA_QUICK_CHG_TUNE_VBUS_WINDOW_MV) {
		*stat = XM_VBUS_TUNE_READY_UP;
		info->tune_vbus_retry++;
	} else if (cp_vbus_hl == CP_PMID_ERROR_HIGH ||
		vbus_cp_mv - info->proc_data.cur_adp_volt >= MCA_QUICK_CHG_TUNE_VBUS_WINDOW_MV) {
		*stat = XM_VBUS_TUNE_READY_DOWN;
		info->tune_vbus_retry++;
	}

	if (info->tune_vbus_retry >= MCA_QC_QUICK_CHG_OPEN_PATH_COUNT) {
		info->tune_vbus_retry = 0;
		*stat = XM_VBUS_TUNE_FAIL;
#line 1563
		mca_log_err("XM_VBUS_TUNE_FAIL Reset QC 5V\n");
		protocol_class_qc_set_volt_cmd(TYPEC_PORT_0, QC2_FORCE_5V);
		return ret;
	}

	return ret;
}

static int mca_qc_retry_to_enable_cp(struct mca_quick_charge_info *info)
{
	int i = 0;

	(void)platform_class_cp_set_charging_enable(CP_ROLE_MASTER, true);
	if (mca_quick_charge_msleep(200, info))
		return -1;

	mca_qc_quick_charge_update_ibus(info);
	while (i++ < 5 && info->proc_data.ibus < CP_ENABLE_IBUS_UCP_RISING_MA) {
		if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
			info->proc_data.cur_adp_volt += MCA_QUICK_CHG_QC3B_DEFAULT_VSTEP;
		else
			info->proc_data.cur_adp_volt += MCA_QUICK_CHG_DEFAULT_VSTEP;
		(void)protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
			info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);
		if (mca_quick_charge_msleep(100, info))
			return -1;
		mca_qc_quick_charge_update_ibus(info);
	}
	return 0;
}

#define QC_VBUS_TUNE_MS 3000
static int mca_qc_quick_charge_open_path(struct mca_quick_charge_info *info)
{
	int ret, i;
	int vbat = 0;
	bool chg_enable = false;
	enum VBUS_TUNE_STAT status;

	ret = platform_class_cp_enable_adc(CP_ROLE_MASTER, true);
	ret |= platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);

	if (ret) {
#line 1606
		mca_log_err("set cp_mode or adc failed\n");
		return ret;
	}

	ret = platform_class_cp_get_battery_voltage(CP_ROLE_MASTER, &vbat);
	if (ret) {
#line 1612
		mca_log_err("open path get vbat fail\n");
		return ret;
	}

	info->proc_data.cur_adp_volt = vbat * 2 + info->proc_data.delta_volt;
	info->proc_data.cur_adp_cur = MCA_QUICK_CHG_OPEN_PATH_CURR;

	(void)mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
#line 1620
	mca_log_info("set adp volt %d cur %d\n",
		info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);
	protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
		info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);

	if (mca_quick_charge_msleep(QC_VBUS_TUNE_MS, info))
		return -1;

	for (i = 0; i < MCA_QC_QUICK_CHG_OPEN_PATH_COUNT; i++) {
		ret = mca_get_qc_vbus_tune_status(info, &status);
#line 1630
		mca_log_info("qc vbus tune status[%d]: %d\n", i, status);

		switch (status) {
		case XM_VBUS_TUNE_INIT:
		case XM_VBUS_TUNE_WAIT:
			break;
		case XM_VBUS_TUNE_VBUS_LOW:
		case XM_VBUS_TUNE_FAIL:
			ret = -1;
			break;
		case XM_VBUS_TUNE_READY_UP:
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
				info->proc_data.cur_adp_volt += MCA_QUICK_CHG_QC3B_DEFAULT_VSTEP;
			else
				info->proc_data.cur_adp_volt += MCA_QUICK_CHG_DEFAULT_VSTEP;
			mca_quick_charge_req_adp_volt_and_cur(info);
			break;
		case XM_VBUS_TUNE_READY_DOWN:
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B)
				info->proc_data.cur_adp_volt -= MCA_QUICK_CHG_QC3B_DEFAULT_VSTEP;
			else
				info->proc_data.cur_adp_volt -= MCA_QUICK_CHG_DEFAULT_VSTEP;
			mca_quick_charge_req_adp_volt_and_cur(info);
			break;
		case XM_VBUS_TUNE_OK:
			ret = platform_class_cp_set_charging_enable(CP_ROLE_MASTER, true);
			if (ret) {
#line 1657
				mca_log_err("open path enable cp charging fail\n");
				return ret;
			}
			break;
		default:
			break;
		}

		if (status == XM_VBUS_TUNE_OK || ret)
			break;

		if (mca_quick_charge_msleep(100, info))
			return -1;
	}

	if (status == XM_VBUS_TUNE_OK) {
		if (mca_quick_charge_msleep(300, info))
			return -1;
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &chg_enable);
		if (!chg_enable) {
#line 1677
			mca_log_err("open path enable cp charging failed, retry\n");
			info->master_cp_enable_count++;
			ret = mca_qc_retry_to_enable_cp(info);
			if (!ret) {
				(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &chg_enable);
				if (chg_enable) {
					info->master_cp_enable_count = 0;
#line 1684
					mca_log_info("retry enable cp charging success\n");
					return 0;
				}

				if (info->master_cp_enable_count >= 5)
					info->master_cp_enable_count = 0;
				mca_charge_mievent_report(CHARGE_DFX_CP_OPEN_FAILED, &info->proc_data.cur_work_cp, 1);
				return -1;
			} else {
				return -1;
			}
		} else {
#line 1696
			mca_log_info("open path enable cp charging success\n");
		}
	}

	return ret;
}

/* G13l: restored from the stock disassembly (stock: out-of-line, called from start_charging; here UNWIRED until start_charging is rewritten) */
static noinline __used void mca_cp_check_initial_mode(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_cp_work_mode *wm = &info->cp_work_mode;
	int volt_para_size = proc->cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_stage = proc->cur_stage[FG_IC_MASTER];
	int vbat = proc->vbat[FG_IC_MASTER];
	int adp_min_volt = proc->adp_info[proc->adp_info_index[CHG_MODE_DIV1]].cap_info.min_voltage;
	int mode_unrelated_fcc, sel, def_mode;

#line 1917
	mca_log_info("volt_para_size: %d, cur_stage: %d, vbat: %d, adp_min_volt: %d",
		volt_para_size, cur_stage, vbat, adp_min_volt);
	wm->div1_vbat_low = (vbat <= adp_min_volt);
	if (!wm->last_stage && cur_stage >= volt_para_size * 2 - 2)
		wm->last_stage = true;
	if (wm->last_stage) {
#line 1928
		mca_log_info("last charging stage, use default mode\n");
		return;
	}

	mode_unrelated_fcc = mca_get_cpmode_unrelated_fcc(info, false);
	sel = mca_select_cp_work_mode_by_current(info, mode_unrelated_fcc);
#line 1933
	mca_log_info("mode_unrelated_fcc: %d, current mode: %d, select mode: %d\n",
		mode_unrelated_fcc, wm->cur_mode, sel);
	if (sel == 0 && wm->div1_vbat_low) {
#line 1936
		mca_log_info("vbat too low, won't use div1 mode\n");
		return;
	}
	def_mode = wm->default_mode;
	if (sel > def_mode) {
#line 1941
		mca_log_info("selected mode higher than default mode, use default mode\n");
		sel = def_mode;
	}
	if ((unsigned int)sel >= 3) {
#line 1944
		mca_log_err("selected mode abnormal!\n");
		return;
	}
	if (sel != wm->cur_mode) {
		wm->cur_mode = sel;
		proc->work_mode = BIT(sel);
		mca_quick_charge_update_work_mode_para(info);
	}
}

static void mca_cp_switch_mode_stop_charging(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int cp_vbus = 0, ret, i;

#line 2984
	mca_log_err("stop charge\n");
	(void)platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
	(void)platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
	cancel_delayed_work_sync(&info->vfc_work);
	if (proc->cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
		(void)platform_class_cp_set_default_fsw(CP_ROLE_MASTER);
		(void)platform_class_cp_set_default_fsw(CP_ROLE_SLAVE);
	} else {
		(void)platform_class_cp_set_default_fsw(proc->cur_work_cp);
	}
	info->pd_switch_to_pmic = true;
	info->check_vbat_ov = false;
	memset(&proc->ibus_queue, 0, sizeof(proc->ibus_queue));
	for (i = 0; i < 10; i++) {
		(void)platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &cp_vbus);
#line 3006
		mca_log_info("avoid vusb_ovp. cp_vbus: %d\n", cp_vbus);
		if (cp_vbus < 12000)
			break;
		ret = mca_quick_charge_req_adp_volt_and_cur(info);
		if (ret) {
#line 3012
			mca_log_err("stop req volt failed\n");
		}
		(void)platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &cp_vbus);
#line 3014
		mca_log_info("avoid vusb_ovp. after request, cp_vbus: %d\n", cp_vbus);
		if (cp_vbus < 12000)
			break;
		mca_quick_charge_msleep(100, info);
	}
}

static int mca_cp_switch_mode_start_charging(struct mca_quick_charge_info *info, int mode)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int ret;

	if (proc->cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
		(void)platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);
		(void)platform_class_cp_set_mode(CP_ROLE_SLAVE, CP_MODE_FORWARD_2_1);
	} else {
		(void)platform_class_cp_set_mode(proc->cur_work_cp, CP_MODE_FORWARD_2_1);
	}
	(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
	(void)platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
	proc->work_mode = 1 << mode;
	mca_quick_charge_update_work_mode_para(info);
	if (info->vfc_para.support_cp_vfc) {
		(void)platform_class_cp_set_fsw(CP_ROLE_MASTER, 480);
		if (info->cp_type == MCA_CP_TYPE_PARALLEL)
			(void)platform_class_cp_set_fsw(CP_ROLE_SLAVE, 480);
	}
	(void)platform_class_cp_enable_busucp(proc->cur_work_cp, false);
	ret = mca_quick_charge_open_path(info);
	(void)platform_class_cp_enable_busucp(proc->cur_work_cp, true);
	if (ret) {
		proc->total_err++;
		mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
		(void)platform_class_cp_set_charging_enable(proc->cur_work_cp, false);
		info->pd_switch_to_pmic = true;
		mca_quick_charge_req_adp_volt_and_cur(info);
#line 3054
		mca_log_err("open path fail\n");
		return ret;
	}
	info->pd_switch_to_pmic = false;
	info->boost_done = false;
	if (info->vfc_para.support_cp_vfc)
		schedule_delayed_work(&info->vfc_work, msecs_to_jiffies(5000));
	return 0;
}

static noinline __used void mca_check_cp_work_mode(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_cp_work_mode *wm = &info->cp_work_mode;
	int vbat = proc->vbat[FG_IC_MASTER];
	int cur_stage = proc->cur_stage[FG_IC_MASTER];
	int adp_min_volt = proc->adp_info[proc->adp_info_index[CHG_MODE_DIV1]].cap_info.min_voltage;
	int volt_para_size = proc->cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int mode_unrelated_fcc, sel, def_mode;

#line 3076
	mca_log_info("volt_para_size: %d, cur_stage:%d, vbat: %d, adp_min_volt: %d",
		volt_para_size, cur_stage, vbat, adp_min_volt);
	if (!wm->last_stage && cur_stage >= volt_para_size * 2 - 2)
		wm->last_stage = true;
	if (wm->last_stage) {
		sel = wm->default_mode;
#line 3084
		mca_log_info("last charging stage, use default mode: %d\n", sel);
		goto check_sel;
	}

	mode_unrelated_fcc = mca_get_cpmode_unrelated_fcc(info, true);
	sel = mca_select_cp_work_mode_by_current(info, mode_unrelated_fcc);
	def_mode = wm->default_mode;
	if (sel > def_mode) {
#line 3090
		mca_log_info("selected mode higher than default mode, use default mode\n");
		sel = def_mode;
	}
#line 3092
	mca_log_info("mode_unrelated_fcc: %d, current mode: %d, select mode: %d\n",
		mode_unrelated_fcc, wm->cur_mode, sel);
	if (wm->div1_vbat_low && vbat > adp_min_volt) {
		wm->div1_vbat_low = false;
	} else if (sel == 0) {
		if (wm->div1_vbat_low) {
#line 3099
			mca_log_info("vbat too low, won't use mode div1\n");
			if (wm->cur_mode != 0)
				return;
			sel = wm->default_mode;
		}
	}

check_sel:
	if ((unsigned int)sel >= 3) {
#line 3108
		mca_log_err("cp mode selected result abnormal!\n");
		return;
	}
	if (sel == wm->cur_mode)
		return;

	mca_cp_switch_mode_stop_charging(info);
	if (mca_cp_switch_mode_start_charging(info, sel)) {
#line 3119
		__mca_log_err("[ARCH-TF-CHARGER]" "[" MCA_LOG_TAG "]%s:%d mode switch start charging failed\n", __func__, __LINE__);
		proc->stop_charging = true;
		return;
	}
	wm->cur_mode = sel;
}

static int mca_quick_charge_jump_stage(int stage,
	struct mca_quick_charge_volt_para_info *volt_info)
{
	if (!volt_info->stage_para)
		return stage;

	if (stage >= volt_info->volt_para_size * 2)
		return stage - 1;

	if (volt_info->stage_para[stage])
		return mca_quick_charge_jump_stage(stage + 1, volt_info);

	return stage;
}

static void mca_quick_charge_select_stage(struct mca_quick_charge_info *info)
{
	int i = 0;
	int j, k;
	int stage = 0;
	struct mca_quick_charge_volt_para *volt_para;
	int ffc_flag;
	int cc_max_volatge;
	int cv_min_current;
	ktime_t time_now;
	bool cp_enabled = false;
	struct mca_quick_charge_batt_para_info *base_flip_para;

	mca_quick_charge_update_vbat(info);
	mca_quick_charge_update_ibat(info);
	if (!info->is_qcom_qc)
		mca_quick_charge_update_ibus(info);
	else
		mca_qc_quick_charge_update_ibus(info);

	while ((i < FG_IC_MAX) && info->proc_data.cur_volt_para[i]) {
		volt_para = info->proc_data.cur_volt_para[i]->volt_para;
		for (j = info->proc_data.cur_volt_para[i]->volt_para_size - 1; j >= 0; --j) {
			/*solve decrease fv logic for */
			if (j == info->proc_data.cur_volt_para[i]->volt_para_size - 1) {
				cc_max_volatge = volt_para[j].voltage - info->smartchg_data.delta_fv;
				cv_min_current = volt_para[j].current_min;
			} else {
				cc_max_volatge = volt_para[j].voltage;
				cv_min_current = volt_para[j].current_min;
			}

			if (info->proc_data.vbat[i] >= cc_max_volatge &&
				info->proc_data.ibat[i] <= cv_min_current) {
				stage = 2 * j + 2;
				break;
			} else if (info->proc_data.vbat[i] >= cc_max_volatge) {
				stage = 2 * j + 1;
				break;
			}
#line 1759
			mca_log_info("cc_max_volatge %d cv_min_current %d stage %d", cc_max_volatge, cv_min_current, stage);
		}
		if (j < 0)
			stage = 0;

		time_now = ktime_get_boottime_seconds();
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &cp_enabled);
		if (info->fastchg_temp_flag && info->proc_data.cur_stage[i] <= 1 && cp_enabled && info->proc_data.ffc_flag) {
			if (time_now - info->time_start >= info->curr_monitor_time_s) {
				stage = 2;
#line 1775
				mca_log_info("max current duration too long, switch next stage\n");
			}
		}
		mca_log_info("fastchg_temp_flag %d, stage %d, cp_en %d, time_now %d, time_start %d, time_diff %lld\n",
			info->fastchg_temp_flag, stage, cp_enabled, time_now, info->time_start, time_now - info->time_start);

		stage = mca_quick_charge_jump_stage(stage, info->proc_data.cur_volt_para[i]);
		ffc_flag = strategy_class_fg_get_fastcharge();
		if (stage > info->proc_data.cur_stage[i] || (ffc_flag != info->proc_data.ffc_flag) || info->proc_data.zone_changed) {
			info->proc_data.cur_stage[i] = stage;
			info->proc_data.zone_changed = false;
		}
		i++;
	};
	if (info->support_base_flip) {
#line 1790
		mca_log_info("get base and flip stage for ocp");
		for (j = 0; j < FG_SITE_MAX; j++) {
			base_flip_para = &info->base_flip_para[j];
			mca_quick_get_parallel_volt_para(info, j, false);

			volt_para = info->proc_data.cur_volt_paraller[j]->volt_para;
			for (k = info->proc_data.cur_volt_paraller[j]->volt_para_size - 1; k >= 0; --k) {
				/*solve decrease fv logic for */
				if (k == info->proc_data.cur_volt_paraller[j]->volt_para_size - 1) {
					cc_max_volatge = volt_para[k].voltage - info->smartchg_data.delta_fv;
					cv_min_current = volt_para[k].current_min;
				} else {
					cc_max_volatge = volt_para[k].voltage;
					cv_min_current = volt_para[k].current_min;
				}

				if (info->proc_data.parall_vbat[j] >= cc_max_volatge &&
					info->proc_data.parall_ibat[j] <= cv_min_current) {
					stage = 2 * k + 2;
					break;
				} else if (info->proc_data.parall_vbat[j] >= cc_max_volatge) {
					stage = 2 * k + 1;
					break;
				}
#line 1812
				mca_log_info("cc_max_volatge %d cv_min_current %d stage %d", cc_max_volatge, cv_min_current, stage);
			}
			if (k < 0)
				stage = 0;
			stage = mca_quick_charge_jump_stage(stage, info->proc_data.cur_volt_paraller[j]);
			ffc_flag = strategy_class_fg_get_fastcharge();
			if (stage > info->proc_data.parall_cur_stage[j] || (ffc_flag != info->proc_data.ffc_flag))
				info->proc_data.parall_cur_stage[j] = stage;
#line 1823
			mca_log_info("parall_cur_stage[%d] %d", j, info->proc_data.parall_cur_stage[j]);
		}
	}
}

/* G13n: rebuilt from the stock disassembly (1504 bytes); the stock calls can_tbat_do_charge(info, true), parallel_judge_temp and cp_check_initial_mode here */
static void mca_quick_charge_start_charging(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int ret;

	proc->soc = strategy_class_fg_ops_get_soc();
	if (proc->soc >= 91) {
#line 1960
		mca_log_info("soc too high\n");
		return;
	}

	ret = mca_quick_charge_judge_vbat(info);
	if (ret) {
#line 1966
		mca_log_info("vbatt range invalid\n");
		return;
	}

	ret = mca_quick_charge_can_tbat_do_charge(info, true);
	if (ret) {
#line 1972
		mca_log_info("temp range invalid\n");
		return;
	}

	if (mca_quick_charge_judge_enable_cp(info)) {
#line 1980
		mca_log_info("dbcgx exist over voltage risk, exit quick charging\n");
		return;
	}

	ret = mca_quick_charge_judge_thermal_vote(info, false);
	if (ret) {
#line 1994
		mca_log_info("thermal fcc too low\n");
		return;
	}

	ret = mca_quick_charge_select_cp(info);
	if (ret) {
#line 2000
		mca_log_err("select cp mode fail\n");
		return;
	}

	if (info->support_base_flip && mca_quick_charge_parallel_judge_temp(info)) {
#line 2007
		mca_log_err("get_parallel_volt_para fail\n");
		return;
	}

	if (info->bypass_curr && info->bypass_curr < info->cp_switch_pmic_th) {
#line 2013
		mca_log_info("bypass curr %d is low, exit cp\n", info->bypass_curr);
		return;
	}

	mca_quick_charge_select_stage(info);

	if (info->vfc_para.support_cp_vfc) {
		platform_class_cp_set_fsw(CP_ROLE_MASTER, MCA_QUICK_CHG_CP_DEFAULT_FSW);
		if (info->cp_type == 1)
			platform_class_cp_set_fsw(CP_ROLE_SLAVE, MCA_QUICK_CHG_CP_DEFAULT_FSW);
	}

#line 2028
	mca_log_info("adp_mode: %d, cur_support_mode: %d\n", proc->adp_mode, info->cur_support_mode);
	if (info->cp_work_mode.support_mode_switch && proc->adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
		proc->max_power >= 65 && !(info->cur_support_mode & ~proc->adp_mode))
		mca_cp_check_initial_mode(info);

	if (info->support_pmic_vterm_dynamics_adjust) {
		mca_vote_override(info->vterm_voter, "pmic_temp_term", false, 0);
		mca_vote_override(info->buck_input_voter, "pmic_temp_term", false, 0);
	}

	platform_class_cp_enable_busucp(proc->cur_work_cp, false);
	if (!info->is_qcom_qc)
		ret = mca_quick_charge_open_path(info);
	else
		ret = mca_qc_quick_charge_open_path(info);
	platform_class_cp_enable_busucp(proc->cur_work_cp, true);
	if (ret) {
		proc->total_err++;
		mca_vote(info->buck_input_voter, "wire_qc", false, 0);
		mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
		platform_class_cp_set_charging_enable(proc->cur_work_cp, false);
		info->pd_switch_to_pmic = true;
		if (info->is_qcom_qc)
			proc->cur_adp_volt = 9000;
		mca_quick_charge_req_adp_volt_and_cur(info);
#line 2054
		mca_log_err("open path fail\n");
		return;
	}
	info->pd_switch_to_pmic = false;
	proc->charge_flag = MCA_QUICK_CHG_STS_CHARGING;
	info->boost_done = false;
	if (proc->ffc_flag)
		strategy_class_fg_set_fastcharge(true);

	proc->exit_cp_charging = false;
	schedule_delayed_work(&info->monitor_work, 0);
	if (info->anti_overvoltage_strategy) {
		proc->exit_cp_charging = false;
		schedule_delayed_work(&info->ovp_monitor_work, 0);
	}
	if (info->vfc_para.support_cp_vfc)
		schedule_delayed_work(&info->vfc_work, msecs_to_jiffies(MCA_QUICK_CHG_VFC_INTERVAL));
}

/* G13l: rewritten from the stock disassembly (176 bytes; the XRING template had a temp-zone/vterm based variant that the stock does not contain) */
static noinline bool mca_quick_charge_check_chg_done(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;

	if (proc->charge_flag != MCA_QUICK_CHG_STS_CHARGE_DONE)
		return false;

	if (info->hardware_cv)
		return false;

	/* G13n (review QC-2): the stock reads the cell voltages when get_voltage SUCCEEDS (cbz w0) */
	if (!strategy_class_fg_ops_get_voltage(&proc->vbat[FG_IC_MASTER])) {
		if (info->batt_type >= MCA_BATTERY_TYPE_PARALLEL) {
			if (!platform_fg_ops_get_volt(FG_IC_BASE, &proc->parall_vbat[FG_IC_BASE]))
				platform_fg_ops_get_volt(FG_IC_FLIP, &proc->parall_vbat[FG_IC_FLIP]);
		}
	}

	if (info->batt_type == MCA_BATTERY_TYPE_SINGLE)
		return proc->vbat[FG_IC_MASTER] < info->recharge_vbat;

	return proc->vbat[FG_IC_MASTER] < info->recharge_vbat &&
		proc->vbat[FG_IC_SLAVE] < info->recharge_vbat;
}

static void mca_quick_charge_pre_charge_check(struct mca_quick_charge_info *info)
{
	int ret;

	if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING)
		return;

	// quick charge driver is slow to load on power up, may miss the first charger plug-in event
	if (!info->online) {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &info->online);
#line 2104
		mca_log_err("check charger online: %d\n", info->online);
		if (!info->online)
			return;
	}

	/* if fg i2c error, don't do pre charge check */
	if (!strategy_class_fg_is_chip_ok()) {
#line 2111
		mca_log_info("fg i2c error, exit pre charge check\n");
		return;
	}

	/* if battery soc is high, do not do pre charge check */
	ret = mca_quick_charge_judge_soc(info);
	if (ret)
		return;

	if (info->proc_data.total_err > MCA_QUICK_CHG_MAX_ERR_COUNT) {
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGE_FAILED;
		return;
	}
	if (info->force_stop || !info->cur_support_mode ||
		!info->sysfs_data.chg_enable || info->taper_done_no_retry ||
		!info->batt_auth)
		return;

	if (mca_quick_charge_check_chg_done(info)) {
#line 2170
		mca_log_info("chg done, check next loop\n");
		return;
	}

	switch (info->proc_data.adp_type) {
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		ret = mca_quick_charge_adjust_adapter_mode(info);
		if (ret) {
			info->proc_data.total_err++;
#line 2192
			mca_log_err("get adapt mode fail,err=%d\n", info->proc_data.total_err);
			return;
		}
		ret = mca_quick_charge_select_cur_work_mode(info);
		if (ret) {
			mca_log_err("cannot find matched cap for supported work mode!\n");
			return;
		}
		break;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		info->proc_data.work_mode = MCA_QUICK_CHG_MODE_DIV_2;
		info->proc_data.min_adp_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
		info->proc_data.max_adp_volt = MCA_QUICK_CHG_ADP_QC_MAX_VOLT;
		info->proc_data.max_adp_curr = MCA_QUICK_CHG_ADP_QC_MAX_CURR;
		break;
	default:
		return;
	}

	mca_quick_charge_update_work_mode_para(info);
	mca_quick_charge_start_charging(info);
}

/* G13n: stock monitor_work: can_tbat_do_charge(info, to_pd_verify) (the stock function takes the info and the flag) */
static __always_inline int mca_quick_charge_select_volt_para(struct mca_quick_charge_info *info)
{
	return mca_quick_charge_can_tbat_do_charge(info, info->proc_data.to_pd_verify);
}

/* G13n: stock inline of monitor_work (ffc volt para -> normal volt para); -1 when the ffc tables are missing */
static __always_inline int mca_quick_charge_check_switch_ffc_mode(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_quick_charge_batt_para_info *batt_para = &info->batt_para[FG_IC_MASTER];
	struct mca_quick_charge_temp_para_info *tinfo;
	int idx = proc->temp_para_index[FG_IC_MASTER];
	int ffc_iterm, vbat_sw, temp = 0, raw_temp;

	if (!proc->ffc_flag)
		return 0;
	if (!batt_para->temp_info)
		return -1;
	tinfo = &batt_para->temp_info[idx];
	if (!tinfo->volt_info.volt_para_size || !tinfo->volt_ffc_info.volt_para_size ||
		!tinfo->volt_info.volt_para || !tinfo->volt_ffc_info.volt_para)
		return -1;

	vbat_sw = tinfo->volt_info.volt_para[tinfo->volt_info.volt_para_size - 1].voltage - info->smartchg_data.delta_fv;
	ffc_iterm = tinfo->volt_ffc_info.volt_para[tinfo->volt_ffc_info.volt_para_size - 1].current_min;
	if (info->cyclecount >= info->drop_fv_cycle) {
		vbat_sw += info->drop_fv_value;
		(void)strategy_class_fg_ops_get_temperature(&temp);
		raw_temp = temp;
		temp = temp / 10;
		if (raw_temp < 350)
			ffc_iterm -= 200;
		else
			ffc_iterm -= 500;
	}
	if (proc->vbat[FG_IC_MASTER] < vbat_sw)
		return 0;
	if (!(info->curr_max < ffc_iterm)) {
		if (!(proc->ibat_queue.avg < ffc_iterm))
			return 0;
		if (ktime_get_coarse_boottime() / NSEC_PER_SEC - info->time_start < 60)
			return 0;
	}
#line 2258
	mca_log_info("switch to normal mode, vbat: %d >= %d, ffc_iterm: %d, curr_max: %d, ibat_avg: %d\n",
		proc->vbat[FG_IC_MASTER], vbat_sw, ffc_iterm, info->curr_max, proc->ibat_queue.avg);
	if (info->cyclecount >= info->drop_fv_cycle)
		info->taper_done_no_retry = true;
	proc->ffc_flag = 0;
	info->switch_normal = true;
	proc->zone_changed = 1;
	proc->cur_volt_para[FG_IC_MASTER] = &tinfo->volt_info;
	proc->temp_max_fv[FG_IC_MASTER] = tinfo->temp_para.normal_max_fv;
	return 0;
}

/* G13n: rebuilt from the stock monitor_work inline (termination counter .bss+0x64) */
static __always_inline void mca_quick_charge_check_charge_done(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vinfo;
	static int count;
	int last, vterm, iterm, ratio, force_full_status = 0, rsoc = 0;
	const struct mca_hwid *hwid;

	if (info->hardware_cv || info->cyclecount >= info->drop_fv_cycle)
		return;
	vinfo = proc->cur_volt_para[FG_IC_MASTER];
	last = vinfo->volt_para_size - 1;
	vterm = vinfo->volt_para[last].voltage - (info->smartchg_data.delta_fv + info->fv_hys_delta_mv);
	iterm = vinfo->volt_para[last].current_min;
	hwid = mca_get_hwid_info();
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_FG, STRATEGY_STATUS_TYPE_FORCE_FULL_STATUS, &force_full_status);
	(void)strategy_class_fg_ops_get_rsoc(&rsoc);

	ratio = iterm > info->curr_terminate_section ? info->curr_terminate_ratio - 10 : info->curr_terminate_ratio;
	if (proc->vbat[FG_IC_MASTER] >= vterm && proc->ibat[FG_IC_MASTER] <= ratio * iterm / 100) {
		count++;
	} else if (force_full_status && proc->ibat[FG_IC_MASTER] <= iterm) {
#line 2356
		mca_log_err("triggle overcharging for termination force_full_status = %d, rsoc =%d\n", force_full_status, rsoc);
		count = 6;
	} else {
		count = 0;
	}
#line 2362
	mca_log_info("termination iterm: %d, vterm: %d, count:%d, force_full_status:%d, rsoc:%d\n", iterm, vterm, count, force_full_status, rsoc);
	if (count > 5) {
		proc->charge_flag = MCA_QUICK_CHG_STS_CHARGE_DONE;
		if (proc->ffc_flag || (hwid && hwid->platform_version <= 10 && ((1 << hwid->platform_version) & 0x496)))
			strategy_class_fg_ops_set_charging_done(true);
#line 2371
		mca_log_err("quick charge done exit\n");
		count = 0;
	}
}

static noinline int mca_quick_charge_get_vstep(int cur_cap, struct mca_quick_charge_info *info, int vbat_gap)
{
	int i, vstep, ratio;
	bool decrease = false;
	int abs_cap = cur_cap;

	if (cur_cap <= 0) {
		abs_cap = -cur_cap;
		decrease = true;
	}

	/* cv can not add step */
	if (info->proc_data.cur_stage[FG_IC_MASTER] % 2 && !decrease)
		return 20;

	for (i = 0; i < VSTEP_PARA_MAX_GROUP; i++) {
		if (!info->vstep_para[i].volt_ratio)
			break;
		if (info->vstep_para[i].volt_ratio != info->proc_data.ratio)
			continue;
		if (info->anti_overvoltage_strategy) {
			if (info->cyclecount > info->vstep_para[i].cycle_count)
				continue;
			if (info->vstep_para[i].cur_gap > abs_cap)
				continue;
			if (info->vstep_para[i].vbat_gap > vbat_gap)
				continue;
		} else if (info->vstep_para[i].cur_gap > abs_cap) {
			continue;
		}
		ratio = info->vstep_para[i].vstep_ratio;
		vstep = ratio * 20;
		if (info->base_flip_same && info->proc_data.temp_para_index[FG_IC_MASTER] == 1 && ratio >= 5)
			vstep = (vstep / ratio) * 4;
#line 2412
		mca_log_info("dbcgx cur_cap: %d, vbat_gap: %d, vstep_ratio: %d, vstep: %d\n", cur_cap, vbat_gap, ratio, vstep);
		if (vstep)
			return vstep;
		break;
	}
	return 20;
}

/*
 * G13n: the stock regulation inlines seven helpers (select_max_ibat, get_base_curr, set_flip_cur,
 * get_base_limit_curr, select_min_vbatt_th, calculate_vstep_ex, open_buck_charging,
 * pmic_single_cp_charging); each is rebuilt from the stock disassembly (map in STATUS.md) and
 * keeps the stock function name (log text, function-local statics).
 */
static __always_inline int mca_quick_charger_get_base_curr(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vinfo = proc_data->cur_volt_paraller[FG_IC_BASE];
	int stage, size, section, base_curr_max;

	if (!vinfo->volt_para)
		return 0;
	size = vinfo->volt_para_size;
	stage = proc_data->parall_cur_stage[FG_IC_BASE];
	section = stage > 2 * (size - 1) ? size - 1 : (stage + 1) / 2;
	base_curr_max = vinfo->volt_para[section].current_max;
#line 1002
	mca_log_err("cur_base_stage:%d cur_volt_section:%d base_curr_max:%d\n", stage, section, base_curr_max);
	return base_curr_max;
}

static __always_inline int mca_quick_charger_set_flip_cur(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vinfo = proc_data->cur_volt_paraller[FG_IC_FLIP];
	static int last_flip_curr_max = -1;
	int stage, size, section, vbat, ibat, flip_curr_max;
	int thermal_flip_cur = info->thermal_flip_cur;

	if (!vinfo->volt_para)
		return 0;
	size = vinfo->volt_para_size;
	stage = proc_data->parall_cur_stage[FG_IC_FLIP];
	vbat = proc_data->parall_vbat[FG_IC_FLIP];
	section = stage > 2 * (size - 1) ? size - 1 : (stage + 1) / 2;
	ibat = proc_data->parall_ibat[FG_IC_FLIP];
	flip_curr_max = vinfo->volt_para[section].current_max;
	if (thermal_flip_cur)
		flip_curr_max = min(flip_curr_max, thermal_flip_cur);
	/* do not raise the flip current again near the end of charge (30 mV below the last stage voltage) */
	if (last_flip_curr_max < flip_curr_max) {
		if (last_flip_curr_max != -1 && vbat > vinfo->volt_para[size - 1].voltage - 30)
			flip_curr_max = last_flip_curr_max;
	}
	last_flip_curr_max = flip_curr_max;
	platform_class_loadsw_set_ibat_limit(LOADSW_ROLE_MASTER, flip_curr_max);
#line 1079
	mca_log_err("last_flip_curr_max %d, thermal_flip_cur %d, flip_curr_max %d, cur_stage %d, cur_volt_section:%d, ibat_slave %d, vbat_slave %d\n",
		last_flip_curr_max, thermal_flip_cur, flip_curr_max, stage, section, ibat, vbat);
	return flip_curr_max;
}

/* stock rodata table at .rodata+0x6280: {over-current threshold mA, decrement mA} */
static const struct { int thr; int dec; } base_ocp_step[4] = {
	{ 1000, 900 }, { 500, 600 }, { 300, 300 }, { 100, 150 },
};

static __always_inline int mca_quick_charger_get_base_limit_curr(struct mca_quick_charge_info *info,
	int cur_max, int base_curr_max, int flip_curr_max)
{
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;
	static int over_curr_count;
	static bool runSwOcp;
	int ibat = proc_data->parall_ibat[FG_IC_BASE];
	int vbat = proc_data->parall_vbat[FG_IC_BASE];
	int sum = flip_curr_max + base_curr_max;
	int limit = sum ? min(cur_max, sum) : cur_max;
	int diff = ibat - base_curr_max;
	int result = limit, final_curr, i;

	over_curr_count = diff < 0 ? 0 : over_curr_count + 1;
	if (limit && over_curr_count > 1) {
		for (i = 0; i < 4; i++) {
			if (diff > base_ocp_step[i].thr) {
				result = limit - base_ocp_step[i].dec;
				break;
			}
		}
		over_curr_count = 0;
		runSwOcp = true;
	} else if (ibat < base_curr_max - 150 && runSwOcp) {
		result = 0;
		runSwOcp = false;
	}
	final_curr = max(result, 0);
#line 1043
	mca_log_err("runSwOcp:%d base_ibat:%d base_vbat:%d base_curr_max:%d over_curr_count:%d cur_max:%d final_cur:%d\n",
		runSwOcp, ibat, vbat, base_curr_max, over_curr_count, limit, final_curr);
	return result < 1 ? limit : min(limit, final_curr);
}

/* stock regulation: select_max_ibat is inlined, it also publishes current_fcc and curr_max */
static __always_inline int mca_quick_charge_select_max_ibat(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vinfo = proc_data->cur_volt_para[FG_IC_MASTER];
	int cur_stage = proc_data->cur_stage[FG_IC_MASTER];
	int last_cur_min = vinfo->volt_para[vinfo->volt_para_size - 1].current_min;
	int delta_cur = info->smartchg_data.delta_ichg;
	int reduce_max_cur = info->reduce_max_cur;
	int jeita_fcc, cur_max, thermal_cur, channel_cur;

	if (info->anti_overvoltage_strategy)
		(void)mca_quick_charge_update_jeita_fcc(info);
	jeita_fcc = mca_quick_charge_update_jeita_fcc(info);
#line 2538
	mca_log_info("cur_stage %d cur_max %d delta_cur %d cur_work_cp %d\n", cur_stage, jeita_fcc, delta_cur, proc_data->cur_work_cp);
	if (info->dtpt_status) {
		if (info->support_dtpt_ichg_dec) {
#line 2541
			mca_log_info("support_dtpt_ichg_dec:dtpt_ichg_dec %d\n", delta_cur);
			cur_max = jeita_fcc * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE - delta_cur;
		} else if (info->support_dtpt_reduce_max_cur) {
#line 2545
			mca_log_info("support_dtpt_reduce_max_cur:dtpt_max_cur %d\n", reduce_max_cur);
			cur_max = jeita_fcc * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE - reduce_max_cur;
		} else {
			cur_max = jeita_fcc * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE;
		}
	} else {
		cur_max = jeita_fcc - delta_cur;
	}

	thermal_cur = proc_data->thermal_cur[proc_data->cur_work_cp == MCA_QUICK_CHG_CP_DUAL];
	channel_cur = proc_data->cur_work_cp == MCA_QUICK_CHG_CP_DUAL ? proc_data->max_curr : proc_data->single_curr;
#line 2562
	mca_log_info("cur_max %d channel_cur %d thermal_cur %d bypass_curr %d\n",
		cur_max, channel_cur, thermal_cur, info->bypass_curr);
	if (thermal_cur)
		cur_max = min(cur_max, thermal_cur);
	if (proc_data->max_adap_ibat)
		cur_max = min(cur_max, proc_data->max_adap_ibat);
	if (mca_strategy_is_support_pps_100w() && proc_data->adp_type == XM_CHARGER_TYPE_PPS)
		cur_max = min(cur_max, proc_data->non_mi_pps_curr);
	cur_max = min(cur_max, channel_cur);
	cur_max = min(cur_max, proc_data->temp_max_cur[FG_IC_MASTER]);
	cur_max = min(cur_max, proc_data->max_adp_curr * proc_data->ratio);
	if ((info->support_base_flip || info->base_flip_same) && proc_data->sw_ocp_curr)
		cur_max = min(cur_max, proc_data->sw_ocp_curr);
	if (info->bypass_curr)
		cur_max = min(cur_max, info->bypass_curr);
	if (info->support_ichg_cutoff_priority)
		cur_max = max(cur_max, last_cur_min);

	/* the ibus/ibat queue of the EU path is handled by mca_eu_pps_anti_disconnection_strategy (called by regulation) */
	if (proc_data->adp_type == XM_CHARGER_TYPE_PPS && !info->is_eu_model && !mca_strategy_is_support_pps_100w()) {
		/* third-party PPS: 200 mA less, boost clamp or third_pps_ibat_limit */
		cur_max -= 200;
		if (cur_max > MCA_QUICK_CHG_PPS_BOOST_FCC_CURR_TH && info->smartchg_data.pwr_boost_state)
			cur_max = MCA_QUICK_CHG_PPS_BOOST_FCC_CURR_TH;
		else if (!info->smartchg_data.pwr_boost_state)
			cur_max = min(cur_max, info->third_pps_ibat_limit);
	}

	if (info->support_base_flip) {
		int base_curr_max = mca_quick_charger_get_base_curr(info);
		int flip_curr_max = mca_quick_charger_set_flip_cur(info);

		cur_max = mca_quick_charger_get_base_limit_curr(info, cur_max, base_curr_max, flip_curr_max);
#line 2604
		mca_log_info("cur_max: %d\n", cur_max);
	}
	info->current_fcc = cur_max;
	info->curr_max = cur_max;
	return cur_max;
}
static int mca_quick_charge_update_decrease_current_buff(struct mca_quick_charge_info *info)
{
	int i, drop_curr = 0;

	(void)strategy_class_fg_ops_get_cyclecount(&info->cyclecount);
	for (i = 0; i < info->drop_fcc_para_size; i++) {
		if (info->cyclecount < info->drop_fcc_para[i].cyclecount) {
			drop_curr = info->drop_fcc_para[i].drop_fcc_step;
#line 2437
			mca_log_info("cur_cycle: %d, drop_fcc_step: %d\n", info->cyclecount, drop_curr);
			break;
		}
	}
	return drop_curr;
}

/* G13l: from the stock disassembly (called twice from regulation) */
static noinline __used int mca_quick_charge_update_jeita_fcc(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vinfo = proc->cur_volt_para[FG_IC_MASTER];
	int jeita_fcc = vinfo->volt_para[proc->cur_stage[FG_IC_MASTER] / 2].current_max;
	int drop_curr = 0, new_fcc;
	bool update_buff;

	if (info->cyclecount < info->drop_fv_cycle)
		return jeita_fcc;

	if (proc->vbat[FG_IC_MASTER] >= vinfo->volt_para[vinfo->volt_para_size - 1].voltage - 20)
		drop_curr = mca_quick_charge_update_decrease_current_buff(info);
#line 2456
	mca_log_info("drop_curr %d old_jeita_fcc %d\n", drop_curr, jeita_fcc);
	new_fcc = jeita_fcc - proc->decrease_sum;
	update_buff = new_fcc <= proc->taper_fcc_ma;
	if (update_buff) {
		new_fcc = proc->taper_fcc_ma + 100;
	} else if (proc->ibat_total <= new_fcc) {
		proc->decrease_sum += drop_curr;
	}
#line 2467
	mca_log_info("new_jeita_fcc %d decrease_sum %d update_buff %d\n", new_fcc, proc->decrease_sum, update_buff);
	return new_fcc;
}

/* now only support single 1S battery, 1S2P battery to do */
static __always_inline int mca_qc_get_vbus_change_trend(struct mca_quick_charge_info *info)
{
	static int ibatt_count;
	static int vbatt_count;
	static bool ibatt_change;
	static bool vbatt_change;
	static int vbat_now_mv_before;
	static int ibat_now_ma_before;
	static int decrement_count;
	int dir = DIR_HOLD, ibus = 0, vbus = 0, ret = 0;
	int vbat_now_mv = info->proc_data.vbat[FG_IC_MASTER];
	int ibat_now_ma = info->proc_data.ibat_total;
	int target_limit_fcc_ma, target_limit_ibus_ma, ibat_qc_max_ma, ibus_qc_max_ma;
	int delta_fv = info->smartchg_data.delta_fv;
	struct mca_quick_charge_volt_para_info *vinfo = info->proc_data.cur_volt_para[FG_IC_MASTER];

	/* now only support single 1S battery, 1S2P battery to do*/
	info->qc_normal_charge_fv_max_mv = vinfo->volt_para[vinfo->volt_para_size - 1].voltage;
	ret = mca_qc_quick_charge_update_ibus(info);
	if (ret) {
#line 2664
		mca_log_err("regulation qc get ibus failed\n");
		return DIR_HOLD;
	}

	ibus = info->proc_data.ibus;

	ret = mca_qc_quick_charge_update_vbus(info);
	if (ret) {
#line 2672
		mca_log_err("regulation qc get vbus failed\n");
		return DIR_HOLD;
	}
	vbus = info->proc_data.vbus;

#line 2677
	mca_log_info("ibat: %d, ibus: %d, vbus:%d\n", ibat_now_ma, ibus, vbus);

	if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B) {
		ibat_qc_max_ma = info->qc3_ibat_max_limit_ma;
		ibus_qc_max_ma = info->qc3_max_ibus_limit_ma;
	} else {
		ibat_qc_max_ma = info->qc3p5_max_ibat_limit_ma;
		ibus_qc_max_ma = info->qc3p5_max_ibus_limit_ma;
	}
	if (info->bypass_curr)
		ibat_qc_max_ma = min(ibat_qc_max_ma, info->bypass_curr);

	if (info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE] >= 1500) {
		target_limit_fcc_ma = min(info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE], ibat_qc_max_ma);
		target_limit_ibus_ma = min((info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE] / 2 + 100),
			ibus_qc_max_ma);
#line 2694
		mca_log_info("target_limit_fcc_ma: %d, target_limit_ibus_ma:%d, ibat_qc_max_ma: %d\n",
			target_limit_fcc_ma, target_limit_ibus_ma, ibat_qc_max_ma);
	} else if (!info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE]) {
		target_limit_fcc_ma = ibat_qc_max_ma;
		target_limit_ibus_ma = ibus_qc_max_ma;
	} else {
		return DIR_HOLD;
	}

	/* NOTE: the stored byte is the "unchanged" condition (stock stores the cset result) */
	{
		bool same_i = (ibat_now_ma == ibat_now_ma_before && ibatt_count < 5);
		bool same_v;

		if (same_i)
			ibatt_count++;
		else {
			ibatt_count = 0;
			ibat_now_ma_before = ibat_now_ma;
		}
		ibatt_change = same_i;

		same_v = (vbat_now_mv == vbat_now_mv_before && vbatt_count < 5);
		if (same_v)
			vbatt_count++;
		else {
			vbatt_count = 0;
			vbat_now_mv_before = vbat_now_mv;
		}
		vbatt_change = same_v;
	}

	switch (info->proc_data.adp_type) {
	case XM_CHARGER_TYPE_HVDCP3_B:
		if (vbus <= info->qc3_max_vbus_limit_mv && ibus < target_limit_ibus_ma - 800 &&
			(!vbatt_change && vbat_now_mv < info->qc_normal_charge_fv_max_mv - 50 - delta_fv) &&
			(!ibatt_change && ibat_now_ma < target_limit_fcc_ma - 700)) {
			dir = DIR_UP;
#line 2727
			mca_log_info("QC3 D plus pluse sent\n");
		}
		if ((!vbatt_change && vbat_now_mv > info->qc_normal_charge_fv_max_mv - delta_fv - 15) ||
			(!ibatt_change && ibat_now_ma > target_limit_fcc_ma) ||
			ibus > target_limit_ibus_ma + 300) {
			decrement_count++;
			if (decrement_count == 2) {
				decrement_count = 0;
				dir = DIR_DOWN;
#line 2736
				mca_log_info("QC3 D mius pluse sent\n");
			}
		}
		break;
	case XM_CHARGER_TYPE_HVDCP3P5:
		if (vbus <= info->qc3p5_max_vbus_limit_mv && ibus < target_limit_ibus_ma - 100 &&
			(!vbatt_change && vbat_now_mv < info->qc_normal_charge_fv_max_mv - 20 - delta_fv) &&
			(!ibatt_change && ibat_now_ma < target_limit_fcc_ma - 50)) {
			dir = DIR_UP;
#line 2745
			mca_log_info("QC3P5 D plus pluse sent\n");
		}
		if ((!vbatt_change && vbat_now_mv > info->qc_normal_charge_fv_max_mv - 10 - delta_fv)
			|| (!ibatt_change && ibat_now_ma > target_limit_fcc_ma + 50)
			|| (ibus > target_limit_ibus_ma + 50)) {
			dir = DIR_DOWN;
#line 2751
			mca_log_info("QC3P5 D mius pluse sent\n");
		}
		break;
	default:
		break;
	}

	return dir;
}

static noinline int mca_qc_quick_charge_regulation(struct mca_quick_charge_info *info)
{
	int qc_pluse_direction = DIR_HOLD, hvdcp_cmd;
	int ret = 0;

	if (info->bypass_curr && info->bypass_curr < info->cp_switch_pmic_th) {
#line 2767
		mca_log_info("bypass curr %d is low, exit cp\n", info->bypass_curr);
		return -1;
	}

	qc_pluse_direction = mca_qc_get_vbus_change_trend(info);

	if (qc_pluse_direction == DIR_UP)
		hvdcp_cmd = QC3_SINGLE_INCREMENT;
	else if (qc_pluse_direction == DIR_DOWN)
		hvdcp_cmd = QC3_SINGLE_DECREMENT;
	else
		return 0;

#line 2780
	mca_log_info("QC3/3+ set hvdcp cmd %d\n", hvdcp_cmd);
	ret = protocol_class_qc_set_volt_cmd(TYPEC_PORT_0, hvdcp_cmd);
	if (ret) {
#line 2783
		mca_log_err("QC3/3+ set pluse hvdcp cmd failed\n");
		return ret;
	}

	return 0;
}

static __always_inline int mca_quick_charge_select_min_vbatt_th(struct mca_quick_charge_info *info)
{
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].voltage;
	int delta_fv = info->smartchg_data.delta_fv;

	if ((cur_stage / 2) == (volt_para_size - 1)) {
#line 2798
		mca_log_info("vbatt_th %d delta_fv %d pps_taper_vol_hys:%d\n",
				vbat_th, delta_fv, info->pps_taper_vol_hys);
		vbat_th -= delta_fv;
		vbat_th -= info->pps_taper_vol_hys;
	}

	return vbat_th;
}

static void strategy_quickchg_enable_buck_charging(struct mca_quick_charge_info *info,
	int buck_icl_val, int buck_fcc_val, bool enable)
{
	int ret = 0;
	bool cp_enabled = false;
	static bool enable_once = true;
	static bool disable_once = false;
	ret |= platform_class_cp_get_charging_enabled(info->proc_data.cur_work_cp, &cp_enabled);
	mca_quick_charge_update_ibus(info);
	if (enable && cp_enabled && enable_once &&
			info->proc_data.ibus > 2500) {
		enable_once = false;
		disable_once = true;
		platform_class_buckchg_ops_set_buck_fsw(MAIN_BUCK_CHARGER, 18000);
		mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
		mca_vote_override(info->buck_input_voter, "icl_limit", true, buck_icl_val); // ICL
		mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", true, buck_fcc_val); // FCC
#line 2824
		mca_log_err("enable buck parallel charging! ibus :%d\n", info->proc_data.ibus);
	} else if (!enable && disable_once) {
		if (info->proc_data.ibus > 0 && info->proc_data.ibus < 3850) {
			disable_once = false;
			enable_once = true;
			platform_class_buckchg_ops_set_buck_fsw(MAIN_BUCK_CHARGER, 5000);
			mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
			mca_vote_override(info->buck_input_voter, "icl_limit", false, buck_icl_val); // ICL
			mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", false, buck_fcc_val); // FCC
#line 2834
			mca_log_err("disable buck parallel charging!, ibus: %d\n", info->proc_data.ibus);
		}
	}
}

/* stock: inlined into regulation; last_vbat in .bss+0xac, stage in .data+0x354 (initial -1) */
static __always_inline int mca_quick_charge_calculate_vstep_ex(struct mca_quick_charge_info *info,
	int delta_v, int vbat, u64 boost_time_diff_ms)
{
	static int last_vbat;
	static int stage = -1;
	int dv = vbat - last_vbat;
	int vstep_ex = MCA_QUICK_CHG_DEFAULT_VSTEP;
	int i;

	if (dv > 0) {
		for (i = 0; i < VSTEP_PARA_EX_MAX_GROUP; i++) {
			if (delta_v > info->vstep_para_ex[i].delta_v_thr)
				continue;
			stage = i;
			if (abs(dv) > info->vstep_para_ex[i].vbat_change_thr) {
				vstep_ex = info->vstep_para_ex[i].vstep_multiplier * MCA_QUICK_CHG_DEFAULT_VSTEP;
				break;
			}
		}
	}
	if (stage >= 0 && dv < info->vstep_para_ex[stage].vbat_change_thr &&
		info->vstep_para_ex[stage].boost_time_thr > boost_time_diff_ms) {
#line 3157
		mca_log_info("vbat change too small (%d < %d) within %llu ms (threshold: %llu ms), stopping boost\n", dv, info->vstep_para_ex[stage].vbat_change_thr, boost_time_diff_ms, info->vstep_para_ex[stage].boost_time_thr);
		vstep_ex = 0;
	}
#line 3164
	mca_log_info("Returning final_vstep: %d\n", vstep_ex);
	last_vbat = vbat;
	return vstep_ex;
}

/* stock: inlined into regulation (called under support_base_flip) */
static __always_inline void strategy_quickchg_open_buck_charging(struct mca_quick_charge_info *info,
	int cur_max, int ibat)
{
	int ibus_cp[2] = { 0, 0 };
	int ibus_buck = 0, aicl_status = 0, pvbat = 0;
	int vterm, fcc, icl;

	(void)platform_class_cp_get_bus_current(CP_ROLE_MASTER, &ibus_cp[0]);
	(void)platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &ibus_cp[1]);
	(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &ibus_buck);
	(void)platform_class_buckchg_ops_get_aicl_status(MAIN_BUCK_CHARGER, &aicl_status);
	vterm = mca_get_effective_result(info->vterm_voter);
	(void)platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER, &pvbat);
	if (pvbat >= vterm - 30)
		info->stop_pmic = true;
#line 2857
	mca_log_info("ibus_buck :%d, aicl_status :%d, master_ibus :%d, slave_ibus :%d, vterm :%d, pvbat :%d, stop_pmic: %d\n", ibus_buck, aicl_status, ibus_cp[0], ibus_cp[1], vterm, pvbat, info->stop_pmic);

	if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
		info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL && !info->stop_pmic) {
		int ibus = info->proc_data.ibus;

		/* hard-coded tiers of the stock: (cur_max, ibus) -> pmic fcc */
		if (cur_max > 11000 && ibus > 5200)
			fcc = 3000;
		else if (cur_max > 8000 && ibus > 4200)
			fcc = 2000;
		else if (cur_max > 5000 && ibus > 2700)
			fcc = 1000;
		else
			fcc = 400;
		if (cur_max > ibat && fcc != info->pmih_fcc) {
			icl = fcc / 2 + 300;
			info->pmih_fcc = fcc;
			mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
			mca_vote_override(info->buck_input_voter, "icl_limit", true, icl);
			mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", true, fcc);
			(void)platform_class_buckchg_ops_set_restart_aicl(MAIN_BUCK_CHARGER, true);
#line 2877
			mca_log_err("enable buck parallel charging, pmih_fcc :%d, pmih_icl :%d\n", fcc, icl);
		}
	} else {
		info->pmih_fcc = 0;
		mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
		mca_vote_override(info->buck_input_voter, "icl_limit", false, 0);
		mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", false, 0);
	}
}

/* stock: inlined into regulation (called when pmic_single_cp_chg is set) */
static __always_inline void strategy_quickchg_pmic_single_cp_charging(struct mca_quick_charge_info *info,
	int cur_max)
{
	int ibus_cp = 0, ibus_buck = 0, pvbat = 0, curr_vbat = 0, chg_status = 0;
	int ffc_flag = strategy_class_fg_get_fastcharge();
	int fcc, icl;

	if (ffc_flag || (cur_max >= 9000 && info->proc_data.adp_type == XM_CHARGER_TYPE_PPS)) {
		(void)platform_class_cp_get_bus_current(CP_ROLE_MASTER, &ibus_cp);
		(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &ibus_buck);
		(void)platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER, &pvbat);
		(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chg_status);
		(void)strategy_class_fg_ops_get_voltage(&curr_vbat);
#line 2911
		mca_log_info("ffc_flag :%d\n", ffc_flag);
		if (ffc_flag)
			mca_vote_override(info->buck_vterm_voter, "single_cp_pmic", true, 4650);
	} else {
		mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
		mca_vote_override(info->buck_input_voter, "icl_limit", false, 0);
		mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", false, 0);
		mca_vote_override(info->buck_vterm_voter, "single_cp_pmic", false, 0);
#line 2901
		mca_log_info("cancel cp + pmic charging in normal charging!\n");
		return;
	}
#line 2917
	mca_log_info("ibus :%d, ibus_buck :%d, ibus_cp :%d, curr_vbat :%d, pvbat :%d, pmic_chg_status :%d\n", info->proc_data.ibus, ibus_buck, ibus_cp, curr_vbat, pvbat, chg_status);
	if (curr_vbat >= info->vbat_threshold || info->use_buck_flag) {
		mca_vote(info->input_suspend_voter, "wire_qc", true, 1);
		info->use_buck_flag = true;
		info->pmih_fcc = 0;
		mca_vote_override(info->buck_input_voter, "icl_limit", false, 0);
		mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", false, 0);
		return;
	}

	if (info->cur_max_threshold[0] < cur_max && info->proc_data.ibus > info->ibus_threshold[0])
		fcc = info->pmih_fcc_value[0];
	else if (info->cur_max_threshold[1] < cur_max && info->proc_data.ibus > info->ibus_threshold[1])
		fcc = info->pmih_fcc_value[1];
	else if (info->cur_max_threshold[2] < cur_max && info->proc_data.ibus > info->ibus_threshold[2])
		fcc = info->pmih_fcc_value[2];
	else
		fcc = info->pmih_fcc_value[3];
	if (fcc == info->pmih_fcc)
		return;
	icl = fcc / 2;
	if (fcc != info->pmih_fcc_value[3])
		icl += 500;
	info->pmih_fcc = fcc;
	mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
	mca_vote_override(info->buck_input_voter, "icl_limit", true, icl);
	mca_vote_override(info->buck_charge_curr_voter, "fcc_limit", true, fcc);
	(void)platform_class_buckchg_ops_set_restart_aicl(MAIN_BUCK_CHARGER, true);
#line 2940
	mca_log_err("enable cp and buck charging, pmih_fcc :%d, pmih_icl :%d\n", fcc, icl);
}

/* todo: add mutil battery process */
static noinline int mca_quick_charge_regulation(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int cur_stage = proc->cur_stage[FG_IC_MASTER];
	struct mca_quick_charge_volt_para_info *vinfo = proc->cur_volt_para[FG_IC_MASTER];
	int cur_min = vinfo->volt_para[cur_stage / 2].current_min;
	int vbat = proc->vbat[FG_IC_MASTER];
	int ibat = proc->ibat_total;
	u64 time_now_ms = ktime_get_boottime_ns() / 1000000;
	static u64 last_boost_time_ms;
	static u64 last_buck_time_ms;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int volt_para_size = vinfo->volt_para_size;
	int fv_max = vinfo->volt_para[volt_para_size - 1].voltage;
	u64 boost_time_diff_ms = time_now_ms - last_boost_time_ms;
	u64 buck_time_diff_ms = time_now_ms - last_buck_time_ms;
	int ret, ibus, cur_max, vbat_th, vstep, final_vstep, fv_cmp, ibus_adj, ratio, lim, vbus_mv = 0;
	int comp_extra = 0;

	ret = mca_quick_charge_update_ibus(info);
	if (ret) {
#line 3194
		mca_log_err("regulation get ibus failed\n");
		return -1;
	}
	ibus = proc->ibus;

	mca_eu_pps_anti_disconnection_strategy(info);
	cur_max = mca_quick_charge_select_max_ibat(info);
	if (cur_max < info->cp_switch_pmic_th) {
#line 3203
		mca_log_info("cur_max too low, stop quick charging: %d <= %d\n",
			cur_max, info->cp_switch_pmic_th);
		proc->stop_charging = true;
	}

	vbat_th = mca_quick_charge_select_min_vbatt_th(info);
	(void)strategy_class_fg_ops_get_cyclecount(&info->cyclecount);
	fv_cmp = fv_max;
	/* G13n: stock calls get_vstep twice here (first result unused), see STATUS.md */
	if (info->anti_overvoltage_strategy) {
		if (info->cyclecount >= info->drop_fv_cycle)
			fv_cmp = info->drop_fv_value + vbat_th;
		if (info->cyclecount > 799 || vbat > fv_cmp - 100)
			goto skip_first_vstep;
	}
	(void)mca_quick_charge_get_vstep(cur_max - ibat, info, vbat_th - vbat);
skip_first_vstep:
	if (info->cyclecount >= info->drop_fv_cycle)
		fv_cmp = info->drop_fv_value + vbat_th;
	vstep = MCA_QUICK_CHG_DEFAULT_VSTEP;
	if (info->cyclecount <= 799 && vbat <= fv_cmp - 100)
		vstep = mca_quick_charge_get_vstep(cur_max - ibat, info, vbat_th - vbat);

	if (proc->adp_type == XM_CHARGER_TYPE_PD_VERIFY) {
		if (cur_stage == 2 * volt_para_size - 1) {
			proc->ibus_compensation = 0;
		} else if (info->support_cancel_compensation && vbat > info->max_vbat_threshold) {
#line 3236
			mca_log_info("max_vbat_threshold %d, vbat %d\n", info->max_vbat_threshold, vbat);
			proc->ibus_compensation = 0;
		} else if (!proc->ibus_compensation) {
			switch (proc->work_mode) {
			case MCA_QUICK_CHG_MODE_DIV_4:
				proc->ibus_compensation = info->ibus_compensation[CHG_MODE_DIV4];
				break;
			case MCA_QUICK_CHG_MODE_DIV_2:
				proc->ibus_compensation = info->ibus_compensation[CHG_MODE_DIV2];
				break;
			case MCA_QUICK_CHG_MODE_DIV_1:
				proc->ibus_compensation = info->ibus_compensation[CHG_MODE_DIV1];
				break;
			default:
				break;
			}
#line 3252
			mca_log_info("reset ibus_compensation\n");
		}
	} else {
		proc->ibus_compensation = 0;
	}

	ratio = proc->ratio;
	if (cur_stage == 0 && info->effective_on_p_generation && info->en_buck_parallel_chg) {
#line 3258
		mca_log_info("compensation for first step chg need pmic and cp work together\n");
		comp_extra = -200;
	}

	if (cur_stage % 2) {
		/* CV */
		if (vbat_th - vbat <= 0) {
			if (ratio * abs(vbat_th - vbat) < MCA_QUICK_CHG_DEFAULT_VSTEP)
				final_vstep = -MCA_QUICK_CHG_DEFAULT_VSTEP;
			else
				final_vstep = max(((vbat_th - vbat) << (info->anti_overvoltage_strategy != 0)) * ratio, -100);
		} else {
			ibus_adj = ibus - proc->ibus_compensation;
			if (ibus_adj > cur_max / ratio || cur_max < ibat) {
				final_vstep = -vstep;
			} else {
				lim = cur_max - proc->delta_ibat;
				final_vstep = (ibus_adj < lim / ratio && ibat < lim) ? vstep : 0;
			}
		}
	} else {
		/* CC */
		ibus_adj = ibus - proc->ibus_compensation + comp_extra;
		if (ibus_adj > cur_max / ratio || cur_max < ibat) {
			final_vstep = -vstep;
		} else {
			lim = cur_max - proc->delta_ibat;
			if (ibus_adj >= lim / ratio || ibat >= lim || vbat_th - vbat <= 0) {
				final_vstep = 0;
			} else {
				final_vstep = vbat < vbat_th - 10 ? vstep : MCA_QUICK_CHG_DEFAULT_VSTEP;
				if (info->enable_vstep_ex &&
					(cur_stage == 2 * (volt_para_size - 1) ||
					 (info->support_base_flip && vbat > vbat_th - 100)))
					final_vstep = min(final_vstep,
						mca_quick_charge_calculate_vstep_ex(info, vbat_th - vbat, vbat, boost_time_diff_ms));
			}
		}
	}
	if (info->cyclecount > 799)
		final_vstep = min(max(final_vstep, -MCA_QUICK_CHG_DEFAULT_VSTEP), MCA_QUICK_CHG_DEFAULT_VSTEP);

	if (info->boost_done) {
		if (final_vstep == MCA_QUICK_CHG_DEFAULT_VSTEP &&
			(boost_time_diff_ms < 2000 || buck_time_diff_ms < 1000))
			final_vstep = 0;
		else if (final_vstep == -MCA_QUICK_CHG_DEFAULT_VSTEP && buck_time_diff_ms < 1000)
			final_vstep = 0;
		/* hwid platform_version 7, country_version 1: no big boost step within 2 s (hwid values are raw, names unknown) */
		if (hwid && hwid->country_version == 1 && hwid->platform_version == 7 &&
			final_vstep >= 80 && boost_time_diff_ms < 2000) {
#line 3316
			mca_log_err("final_vstep >= 80, boost_time_diff_ms < 2000, set final_vstep = 0\n");
			final_vstep = 0;
		}
	}

	if (cur_stage == 2 * volt_para_size - 1 && vbat_th < vbat && final_vstep >= 1) {
#line 3327
		mca_log_info("prevent ovp: vth - vbat < 20mv, not allow boost\n");
		final_vstep = 0;
	}

	if (info->en_buck_parallel_chg) {
		if (cur_max > info->use_buck_fcc_threshold) {
			if (!(info->use_buck_chg_once && info->use_buck_chg_once_flag)) {
				/* stock: is_en_buck_hwid inlined; for the special hwid set the early hwid platform_version 11/12 selects the full values */
				if (mca_quick_charge_is_en_buck_hwid() || hwid->platform_version - 11 <= 1) {
					strategy_quickchg_enable_buck_charging(info,
						info->buck_icl_fcc_curr[0], info->buck_icl_fcc_curr[1], true);
					if (final_vstep > 200 && info->use_buck_chg_once_flag &&
						!info->clear_vstep_open_buckchg) {
						info->clear_vstep_open_buckchg = true;
						final_vstep = 0;
					}
				} else {
					strategy_quickchg_enable_buck_charging(info,
						info->buck_icl_fcc_curr[0] - 500, info->buck_icl_fcc_curr[1] - 2000, true);
				}
			}
		} else {
			strategy_quickchg_enable_buck_charging(info, 0, 0, false);
		}
	}

	if (info->support_base_flip)
		strategy_quickchg_open_buck_charging(info, cur_max, ibat);
	if (info->pmic_single_cp_chg)
		strategy_quickchg_pmic_single_cp_charging(info, cur_max);

	if (final_vstep > 0) {
		last_boost_time_ms = time_now_ms;
	} else if (final_vstep < 0) {
		info->boost_done = true;
		last_buck_time_ms = time_now_ms;
	}

	proc->cur_adp_volt += final_vstep;
	proc->cur_adp_cur = proc->max_adp_curr;
	platform_class_cp_get_bus_voltage(MCA_QUICK_CHG_CP_MASTER, &vbus_mv);
#line 3368
	mca_log_info("cur_stage[%d]: adp_volt: %d/%d, ibat: %d/%d/%d/%d, vbat: %d/%d, ibus: %d/%d, time_diff: %llu/%llu, final_vstep: %d\n",
		cur_stage, proc->cur_adp_volt, vbus_mv, cur_max, cur_min, ibat, cur_max - ibat,
		vbat_th, vbat, ibus, proc->ibus_compensation, boost_time_diff_ms, buck_time_diff_ms, final_vstep);
	ret = mca_quick_charge_req_adp_volt_and_cur(info);
	if (ret) {
#line 3373
		mca_log_err("regulation req adp volt failed\n");
		return -1;
	}

	return 0;
}

static int mca_quick_charge_is_charge_abnormal(struct mca_quick_charge_info *info)
{
	bool chg_enable = false;
	bool chg_enable_slave = false;
	bool fg_err = false;
	bool fg_err_slave = false;

	if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &chg_enable);
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_SLAVE, &chg_enable_slave);
		if (chg_enable && chg_enable_slave)
			return 0;

#line 3391
		mca_log_err("cp work abnormal, master %d slave %d\n",
			chg_enable, chg_enable_slave);
		if (!chg_enable) {
			info->proc_data.total_err++;
			info->proc_data.error_num[MCA_QUICK_CHG_CP_MASTER]++;
		} else {
			info->proc_data.total_err++;
			info->proc_data.error_num[MCA_QUICK_CHG_CP_SLAVE]++;
		}
		platform_class_cp_dump_register(CP_ROLE_MASTER);
		platform_class_cp_dump_register(CP_ROLE_SLAVE);

		return -1;
	}
	(void)platform_class_cp_get_charging_enabled(info->proc_data.cur_work_cp,
		&chg_enable);
	if (!chg_enable) {
#line 3408
		mca_log_err("cp work abnormal, role %d enbale %d\n",
			info->proc_data.cur_work_cp, chg_enable);
		platform_class_cp_dump_register(CP_ROLE_MASTER);
		info->proc_data.total_err++;
		info->proc_data.error_num[info->proc_data.cur_work_cp]++;
		return -1;
	}

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		(void)platform_fg_ops_get_error_state(FG_IC_MASTER, &fg_err);
		(void)platform_fg_ops_get_error_state(FG_IC_SLAVE, &fg_err_slave);
		if (!fg_err && !fg_err_slave)
			return 0;
#line 3421
		mca_log_err("fg work abnormal, fg_err %d fg_err_slave %d\n",
			fg_err, fg_err_slave);
		return -1;
	}
	(void)platform_fg_ops_get_error_state(FG_IC_MASTER, &fg_err);
	if (fg_err) {
#line 3427
		mca_log_err("fg work abnormal, fg_err %d\n", fg_err);
		return -1;
	}

	return 0;
}

static int mca_qc_quick_charge_is_charge_abnormal(struct mca_quick_charge_info *info)
{
	bool chg_enable = false;

	(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER,
		&chg_enable);
	if (!chg_enable) {
#line 3441
		mca_log_err("cp work abnormal, enbale %d\n", chg_enable);
		info->proc_data.total_err++;
		info->proc_data.error_num[MCA_QUICK_CHG_CP_MASTER]++;
		return -1;
	}

	return 0;
}

static void mca_quick_charge_try2_single_path(struct mca_quick_charge_info *info)
{
	int ret;
	int ibus;
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;

	ret = mca_quick_charge_update_ibus(info);
	if (ret)
		return;

	ibus = proc_data->ibus;
	if (ibus > proc_data->multi_ibus_th - proc_data->ibus_dec)
		return;

	/* G13l: stock counts the consecutive low-current rounds (proc_data.count, name GUESS) */
	proc_data->count++;
	if (proc_data->count <= info->mult_to_single_count)
		return;

	if (proc_data->cp_path_enable[CP_ROLE_MASTER]) {
		(void)platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
		(void)platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
		proc_data->cur_work_cp = MCA_QUICK_CHG_CP_MASTER;
	} else {
		(void)platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
		(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
		proc_data->cur_work_cp = MCA_QUICK_CHG_CP_SLAVE;
	}
}

static void mca_quick_charge_try2_multi_path(struct mca_quick_charge_info *info)
{
	int ret;
	int ibus;
	int cp_role;
	int soc = 0, th;
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;

	ret = mca_quick_charge_update_ibus(info);
	if (ret)
		return;

	ibus = proc_data->ibus;
	th = proc_data->dual_cp_count ? proc_data->again_multi_ibus_th : proc_data->multi_ibus_th;
	if (info->support_base_flip) {
		soc = strategy_class_fg_ops_get_soc();
		if (soc <= 60)
			th = proc_data->multi_ibus_th;
	}
#line 3503
	mca_log_info("soc:%d, multi_ibus_th:%d, dual_cp_count:%d\n", soc, th, proc_data->dual_cp_count);
	if (ibus < th + proc_data->ibus_inc)
		return;

	if (proc_data->cur_work_cp == MCA_QUICK_CHG_CP_MASTER)
		cp_role = CP_ROLE_SLAVE;
	else
		cp_role = CP_ROLE_MASTER;

	if (proc_data->error_num[cp_role] >= MCA_QUICK_CHG_SINGLE_MAX_ERR_COUNT)
		return;

	(void)platform_class_cp_enable_adc(cp_role, true);
	ret = platform_class_cp_set_mode(cp_role, info->proc_data.cur_cp_mode);
	if (ret) {
#line 3518
		mca_log_err("open path set cp mode fail\n");
		return;
	}
	(void)platform_class_cp_set_charging_enable(cp_role, true);
	ret = mca_quick_charge_msleep(500, info);
	if (ret)
		return;

	ret = platform_class_cp_get_bus_current(cp_role, &ibus);
	if (ret) {
		proc_data->error_num[cp_role]++;
		(void)platform_class_cp_set_charging_enable(cp_role, false);
		return;
	}

	if (ibus < MCA_QUICK_CHG_OPEN_PATH_IBUS_TH)
		return;

	proc_data->cur_work_cp = MCA_QUICK_CHG_CP_DUAL;
	proc_data->dual_cp_count++;
}

static noinline void mca_quick_charge_change_path(struct mca_quick_charge_info *info)
{
	if (info->cp_type != MCA_CP_TYPE_PARALLEL || info->is_qcom_qc)
		return;

#line 3545
	mca_log_debug("cur_work_cp: %d\n", info->proc_data.cur_work_cp);
	if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL)
		mca_quick_charge_try2_single_path(info);
	else
		mca_quick_charge_try2_multi_path(info);
}

static void mca_quick_charge_pps_ptf_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info =  container_of(work,
		struct mca_quick_charge_info, pps_ptf_work.work);

	if ((info->proc_data.adp_type == XM_CHARGER_TYPE_PPS && info->is_eu_model) || info->fake_pps_ptf) {
		protocol_class_get_adapter_pps_ptf(info->proc_data.cur_protocol, &info->pps_ptf);
		if (!info->pps_ptf) {
			info->pps_ptf = info->fake_pps_ptf;
			mca_log_info("pps_ptf= %d\n", info->pps_ptf);
		}

		schedule_delayed_work(&info->pps_ptf_work,
			msecs_to_jiffies(MCA_QUICK_CHG_PPS_PTF_INTERVAL));
	} else {
		cancel_delayed_work_sync(&info->pps_ptf_work);
	}
}

static void mca_quick_charge_ibus_queue_push(struct mca_quick_charge_info *info, int ibus)
{
	if (!info)
		return;

	info->proc_data.ibus_queue.sum -=
		info->proc_data.ibus_queue.data[info->proc_data.ibus_queue.index];
	info->proc_data.ibus_queue.data[info->proc_data.ibus_queue.index] = ibus;
	info->proc_data.ibus_queue.sum += ibus;
	info->proc_data.ibus_queue.index =
		(info->proc_data.ibus_queue.index + 1) % MCA_QUICK_CHG_IBUS_QUENE_SIZE;
	info->proc_data.ibus_queue.count =
		min(info->proc_data.ibus_queue.count + 1, MCA_QUICK_CHG_IBUS_QUENE_SIZE);

	info->proc_data.ibus_queue.avg = info->proc_data.ibus_queue.sum /
		info->proc_data.ibus_queue.count;
#line 3597
	mca_log_info("push ibus: %d, index: %d, count: %d, sum: %d, avg: %d",
		ibus, info->proc_data.ibus_queue.index, info->proc_data.ibus_queue.count,
		info->proc_data.ibus_queue.sum, info->proc_data.ibus_queue.avg);
}

/* G13n: stock monitor_work pushes the total battery current into a 5-entry queue (inlined) */
static __always_inline void mca_quick_charge_ibat_queue_push(struct mca_quick_charge_info *info, int ibat)
{
	if (!info)
		return;

	info->proc_data.ibat_queue.sum -=
		info->proc_data.ibat_queue.data[info->proc_data.ibat_queue.index];
	info->proc_data.ibat_queue.data[info->proc_data.ibat_queue.index] = ibat;
	info->proc_data.ibat_queue.sum += ibat;
	info->proc_data.ibat_queue.index =
		(info->proc_data.ibat_queue.index + 1) % MCA_QUICK_CHG_IBAT_QUENE_SIZE;
	info->proc_data.ibat_queue.count =
		min(info->proc_data.ibat_queue.count + 1, MCA_QUICK_CHG_IBAT_QUENE_SIZE);

	info->proc_data.ibat_queue.avg = info->proc_data.ibat_queue.sum /
		info->proc_data.ibat_queue.count;
#line 3620
	mca_log_info("push ibat: %d, index: %d, count: %d, sum: %d, avg: %d",
		ibat, info->proc_data.ibat_queue.index, info->proc_data.ibat_queue.count,
		info->proc_data.ibat_queue.sum, info->proc_data.ibat_queue.avg);
}

/* G13n: stock inline of monitor_work (support_base_flip) */
static __always_inline int mca_quick_charge_select_parallel_volt_para(struct mca_quick_charge_info *info)
{
	int ret;

	if (info->proc_data.parallel_to_pd_verify) {
		ret = mca_quick_get_parallel_volt_para(info, FG_IC_BASE, true);
		ret |= mca_quick_get_parallel_volt_para(info, FG_IC_FLIP, true);
		if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY) {
			info->proc_data.parallel_to_pd_verify = false;
#line 2297
			mca_log_info("PD VERIFY SUCCESS\n");
		}
	} else {
		ret = mca_quick_get_parallel_volt_para(info, FG_IC_BASE, false);
		ret |= mca_quick_get_parallel_volt_para(info, FG_IC_FLIP, false);
	}
	return ret;
}

void strategy_class_fg_set_allow_new_soc_smooth(bool allow);

static void mca_quick_charge_monitor_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info = container_of(work,
		struct mca_quick_charge_info, monitor_work.work);
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	int ret, ret2;
	static int last_ffc_flag;

	if (!strategy_class_fg_is_chip_ok()) {
#line 3651
		mca_log_info("fg i2c error, exit quick charging\n");
		goto out;
	}

	if (info->vfc_para.support_cp_vfc)
		mca_quick_charge_ibus_queue_push(info, proc->ibus);
	mca_quick_charge_ibat_queue_push(info, proc->ibat_total);

	if ((info->is_eu_model || info->support_third_pps_power) && !proc->ffc_flag &&
		info->third_pps_switch_pmic) {
		mca_quick_charge_read_ibat_ma(info);
		proc->soc = strategy_class_fg_ops_get_soc();
		if (proc->ibat_total < info->cp_switch_pmic_th && proc->soc >= 94) {
#line 3664
			mca_log_err("Normal FFC switch to PMIC Now!\n");
			goto out;
		}
	}

	ret = mca_quick_charge_judge_thermal_vote(info, false);
	if (ret)
		goto out;

	if (info->is_qcom_qc) {
		ret = mca_qc_quick_charge_is_charge_abnormal(info);
		(void)mca_qc_check_if_goto_taper(info);
	} else {
		ret = mca_quick_charge_is_charge_abnormal(info);
		(void)mca_check_if_goto_hw_taper(info);
	}
	if (ret)
		goto out;

	ret = mca_quick_charge_select_volt_para(info);
	ret2 = mca_quick_charge_check_switch_ffc_mode(info);
	if (ret2 | ret) {
#line 3684
		mca_log_info("monitor temp out of range\n");
		goto out;
	}

	if (info->support_base_flip) {
		ret = mca_quick_charge_select_parallel_volt_para(info);
		if (ret) {
#line 3691
			mca_log_err("get_parallel_volt_para fail\n");
			goto out;
		}
	}

	mca_quick_charge_select_stage(info);

	if (proc->ffc_flag != last_ffc_flag)
		strategy_class_fg_set_fastcharge(proc->ffc_flag != 0);

	mca_quick_charge_check_charge_done(info);
	if (proc->charge_flag == MCA_QUICK_CHG_STS_CHARGE_DONE || info->taper_done_no_retry) {
#line 3702
		mca_log_info("monitor qc charge done\n");
		strategy_class_fg_set_allow_new_soc_smooth(true);
		if (info->cyclecount >= info->drop_fv_cycle) {
			mca_vote(info->buck_charge_enable_voter, "cp_chg_done", true, 0);
			mca_vote(info->buck_input_voter, "cp_chg_done", true, 1600);
#line 3710
			mca_log_err("cp quick charge done, enable pmic for 30s\n");
			schedule_delayed_work(&info->delay_enable_pmic_work, msecs_to_jiffies(30000));
		}
		goto out;
	}

	if (!info->pmic_single_cp_chg)
		mca_quick_charge_change_path(info);

	if (info->cp_work_mode.support_mode_switch && proc->adp_type == XM_CHARGER_TYPE_PD_VERIFY &&
		proc->max_power >= 65 && !(info->cur_support_mode & ~proc->adp_mode))
		mca_check_cp_work_mode(info);

	if (info->is_qcom_qc)
		ret = mca_qc_quick_charge_regulation(info);
	else
		ret = mca_quick_charge_regulation(info);

	if (ret) {
		proc->total_err++;
#line 3731
		mca_log_info("regulation total_err: %d, ret: %d\n", proc->total_err, ret);
		goto out;
	}
	if (proc->stop_charging) {
#line 3735
		mca_log_info("monitor stop charging\n");
		goto out;
	}

	last_ffc_flag = proc->ffc_flag;
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(info->monitor_interval));
	return;
out:
	proc->exit_cp_charging = true;
	mca_quick_charge_stop_charging(info);
}

static void mca_quick_charge_vfc_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info =  container_of(work,
		struct mca_quick_charge_info, vfc_work.work);
	int fsw_target;

	if (!info->vfc_para.support_cp_vfc)
		return;

	strategy_quickchg_map_ibus_to_fsw(info, &fsw_target);
	strategy_quickchg_update_cp_fsw(info, fsw_target);

	schedule_delayed_work(&info->vfc_work, msecs_to_jiffies(MCA_QUICK_CHG_VFC_INTERVAL));
}

static void mca_quick_charge_force_stop_charging(struct mca_quick_charge_info *info)
{
	cancel_delayed_work_sync(&info->monitor_work);
	mca_quick_charge_stop_charging(info);
}

static void mca_quick_charge_exit_charging(struct mca_quick_charge_info *info)
{
#line 3776
	mca_log_err("exit quick charge\n");
	mca_quick_charge_force_stop_charging(info);
	memset(&info->proc_data, 0, sizeof(info->proc_data));
}

static int mca_quick_charge_process_event(int event, int value, void *data)
{
	struct mca_quick_charge_info *info = data;
	int ret = 0;

	if (!data)
		return -1;

#line 3885
	mca_log_info("event: %d, value: %d\n", event, value);
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		info->online = 1;
		if (info->support_base_flip)
			update_batt_para(info);
		break;
	case MCA_EVENT_FG_OTA_PROCESS:
#line 4037
		mca_log_info("fg_ota_process event %d\n", value);
		mca_vote(info->chg_disable_voter, "fg_ota_process", true, value);
		break;
	case MCA_EVENT_FCC_TOO_LOW:
#line 4041
		mca_log_info("fcc too low event, update normal volt para\n");
		info->fcc_too_low = true;
		ret = mca_quick_charge_can_tbat_do_charge(info, true);
		if (info->support_base_flip) {
			info->parallel_fcc_too_low[FG_IC_BASE] = true;
			info->parallel_fcc_too_low[FG_IC_FLIP] = true;
			ret = mca_quick_charge_parallel_judge_temp(info);
		}
		if (ret) {
#line 4050
			mca_log_err("temp range invalid, update fail!\n");
		}
		break;
	case MCA_EVENT_USB_DISCONNECT:
		mca_quick_charge_reset_charge_para(info);
		mca_quick_charge_exit_charging(info);
		(void)strategy_class_fg_set_fastcharge(false);
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
		info->batt_auth = 1;
		break;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
#line 3902
		mca_log_info("change type change event %d cur %d\n",
			value, info->proc_data.adp_type);
		if (value == info->proc_data.adp_type)
			return 0;
		info->proc_data.adp_type = value;
		if (value == XM_CHARGER_TYPE_PD_VERIFY || value == XM_CHARGER_TYPE_PPS) {
			info->proc_data.cur_protocol = ADAPTER_PROTOCOL_PPS;
		} else if (value == XM_CHARGER_TYPE_HVDCP3_B || value == XM_CHARGER_TYPE_HVDCP3P5) {
			info->proc_data.adp_type = value;
			info->is_qcom_qc = true;
			info->proc_data.cur_protocol = ADAPTER_PROTOCOL_QC;
		} else {
			return 0;
		}
		if (info->fake_pps_ptf != 0 && (value == XM_CHARGER_TYPE_PD_VERIFY || value == XM_CHARGER_TYPE_PPS))
			schedule_delayed_work(&info->pps_ptf_work, msecs_to_jiffies(MCA_QUICK_CHG_PPS_PTF_INTERVAL));

		if (value == XM_CHARGER_TYPE_PPS && info->is_eu_model)
			schedule_delayed_work(&info->pps_ptf_work, msecs_to_jiffies(MCA_QUICK_CHG_PPS_PTF_INTERVAL));
		break;
	case MCA_EVENT_CHARGE_CAP_CHANGE:
		info->proc_data.type_chg = 1;
		mca_quick_charge_force_stop_charging(info);
		info->proc_data.type_chg = 0;
		break;
	case MCA_EVENT_SINK_PWR_SUSPEND_CHANGE:
		if (value) {
			platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
			platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
#line 3939
			mca_log_info("Enter bSinkPowerSuspend\n");
		}
		mca_vote(info->chg_disable_voter, "bSinkPowerSuspend", true, value);
		break;
	case MCA_EVENT_CONN_ANTIBURN_CHANGE:
		info->trigger_antr_burn = value;
#line 3945
	    mca_log_info("anti-burn change event %d\n", value);
		mca_vote(info->chg_disable_voter, "antiburn", true, value);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		mca_log_info("batt-btb change event %d\n", value);
		mca_vote(info->chg_disable_voter, "batt_miss", true, value);
		break;
	case MCA_EVENT_BATTERY_DTPT:
		info->dtpt_status = value;
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		mca_quick_charge_force_stop_charging(info);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
		if (value)
			mca_vote(info->chg_en_voter, "lpd", true, 0);
		else
			mca_vote(info->chg_en_voter, "lpd", false, 1);
#line 3963
		mca_log_info("check lpd status to determine whether to exit quick charge");
		break;
	case MCA_EVENT_WIRELESS_REVCHG:
#line 3972
		mca_log_info("wireless revchg event %d\n", value);
		mca_vote(info->chg_disable_voter, "wireless_revchg", true, value);
		break;
	case MCA_EVENT_CHARGE_ACTION:
		mca_quick_charge_pre_charge_check(info);
		break;
	case MCA_EVENT_CC_SHORT_VBUS:
		if (value)
			mca_vote(info->chg_en_voter, "cc_short_vbus", true, 0);
		else
			mca_vote(info->chg_en_voter, "cc_short_vbus", false, 1);
#line 3983
		mca_log_info("check cc short vbus to determine whether to exit quick charge");
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value)
			mca_vote(info->chg_en_voter, "vbat_ovp", true, 0);
		else
			mca_vote(info->chg_en_voter, "vbat_ovp", false, 1);
		break;
	case MCA_EVENT_IBAT_OCP_CHANGE:
		if (value == 0x03)
			info->proc_data.sw_ocp_curr = min(OCP_THRESHOLD_MAINT,OCP_THRESHOLD_FLIP) / 1000;
		else if (value == 0x02)
			info->proc_data.sw_ocp_curr = OCP_THRESHOLD_MAINT / 1000;
		else if (value == 0x01)
			info->proc_data.sw_ocp_curr = OCP_THRESHOLD_FLIP / 1000;
		else
			info->proc_data.sw_ocp_curr = 0;
#line 4000
		mca_log_info("sw_ocp_curr: %d", info->proc_data.sw_ocp_curr);
		break;
	case MCA_EVENT_CP_CBOOT_FAIL:
		if (value)
			mca_vote(info->chg_en_voter, "cp_cboot_short", true, 0);
		else
			mca_vote(info->chg_en_voter, "cp_cboot_short", false, 1);
#line 4007
		mca_log_info("check cp cboot short to determine whether to exit quick charge");
		break;
	case MCA_EVENT_PPS_PTF:
		info->pps_ptf = value;
#line 4011
		mca_log_info("pps_ptf= %d\n", info->pps_ptf);
		if (info->pps_ptf == USBPD_QUICK_DPM_PORT_PPS_PTF_NOT_OVERTEMP) {
#line 4013
			mca_log_info("exit quick charge when PPS_PTF too high %d\n", value);
			mca_vote(info->chg_en_voter, "ptf", true, 0);
		} else {
			mca_vote(info->chg_en_voter, "ptf", false, 1);
		}
		break;
	case MCA_EVENT_IS_EU_MODEL:
#line 4020
		mca_log_err("set is_eu_model %d\n", value);
		info->is_eu_model = value;
		break;
	case MCA_EVENT_PLATE_SHOCK:
		if (value)
			mca_vote(info->chg_en_voter, "plate_shock", true, 0);
		else
			mca_vote(info->chg_en_voter, "plate_shock", false, 0);
		break;
	case MCA_EVENT_CSD_SEND_PULSE:
#line 4030
		mca_log_info("csd pulse event %d\n", value);
		mca_vote(info->chg_disable_voter, "csd_pulse", true, value);
		break;
	default:
		break;
	}

	return 0;
}

static int mca_quick_charge_get_status(int status, void *value, void *data)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;
	int *cur_val = (int *)value;
	int last_volt_stage = -1;

	if (!info)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_CHARGING:
		*cur_val = info->proc_data.charge_flag;
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*cur_val = info->proc_data.quick_charge_type;
		break;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
		*cur_val = info->proc_data.ui_power / 1000;
		break;
	case STRATEGY_STATUS_TYPE_ENABLE:
		if (info->force_stop)
			*cur_val = 0;
		else
			*cur_val = (info->sysfs_data.chg_enable << 9) |
				(info->sysfs_data.mode_enable[0] << 8) |
				(info->sysfs_data.cp_path_enable[0][0] << 7) |
				(info->sysfs_data.cp_path_enable[0][1] << 6) |
				(info->sysfs_data.mode_enable[1] << 5) |
				(info->sysfs_data.cp_path_enable[1][0] << 4) |
				(info->sysfs_data.cp_path_enable[1][1] << 3) |
				(info->sysfs_data.mode_enable[2] << 2) |
				(info->sysfs_data.cp_path_enable[2][0] << 1) |
				(info->sysfs_data.cp_path_enable[2][0] << 0);
		break;
	case STRATEGY_STATUS_TYPE_MODE:
		*cur_val = info->proc_data.cur_cp_mode;
		break;
	case STRATEGY_STATUS_TYPE_VBUS:
		*cur_val = info->proc_data.vbus;
		break;
	case STRATEGY_STATUS_TYPE_IBUS:
		*cur_val = info->proc_data.ibus;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM:
		last_volt_stage = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size - 1;
		if (last_volt_stage == -1) {
                	*cur_val = 1000;
			break;
		}
		*cur_val = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[last_volt_stage].current_min;
		break;
	default:
		return -1;
	}

	return 0;
}

static int mca_quick_charge_parse_volt_para(struct device_node *node,
	const char *name, struct mca_quick_charge_volt_para_info *volt_info)
{
	struct mca_quick_charge_volt_para *volt_para;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	if (strcmp(name, "null") == 0) {
#line 4172
		mca_log_info("no need parse volt para\n");
		return 0;
	}
	array_len = mca_parse_dts_count_strings(node, name,
		VOLT_PARA_MAX_GROUP,
		MCA_QUICK_CHG_VOLT_PARA_MAX);
	if (array_len < 0) {
#line 4179
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	volt_info->volt_para_size = array_len / MCA_QUICK_CHG_VOLT_PARA_MAX;
	volt_info->volt_para = kcalloc(volt_info->volt_para_size, sizeof(*volt_para), GFP_KERNEL);
	if (!volt_info->volt_para) {
#line 4186
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}
	volt_para = volt_info->volt_para;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_QUICK_CHG_VOLT_PARA_MAX;
		col = i % MCA_QUICK_CHG_VOLT_PARA_MAX;

		switch (col) {
		case MCA_QUICK_CHG_VOLTAGE:
			if (kstrtoint(tmp_string, 10, &volt_para[row].voltage))
				goto error;
			break;
		case MCA_QUICK_CHG_CURRENT_MAX:
			if (kstrtoint(tmp_string, 10, &volt_para[row].current_max))
				goto error;
			break;
		case MCA_QUICK_CHG_CURRENT_MIN:
			if (kstrtoint(tmp_string, 10, &volt_para[row].current_min))
				goto error;
			break;
		default:
			break;
		}
	}
	for (i = 0; i < volt_info->volt_para_size; i++)
#line 4215
		mca_log_info("volt_para %d %d %d\n",
			volt_para[i].voltage, volt_para[i].current_max, volt_para[i].current_min);
	return 0;
error:
	kfree(volt_para);
	volt_para = NULL;
	return -1;
}

static void mca_quick_charge_parse_stage_para(struct device_node *node,
	const char *name, struct mca_quick_charge_volt_para_info *volt_info)
{
	if (strcmp(name, "null") == 0) {
#line 4228
		mca_log_info("no need parse stage para\n");
		return;
	}

	volt_info->stage_para = kcalloc(volt_info->volt_para_size * 2, sizeof(int), GFP_KERNEL);
	if (!volt_info->volt_para) {
#line 4234
		mca_log_err("volt para no mem\n");
		return;
	}

	mca_parse_dts_u32_array(node, name, volt_info->stage_para,
		volt_info->volt_para_size * 2);
}

static int mca_quick_charge_parse_temp_para(struct device_node *node,
	int batt_role, const char *name, struct mca_quick_charge_info *info, int mode)
{
	struct mca_quick_charge_batt_para_info *batt_para_info = &(info->batt_para[batt_role]);
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	struct mca_quick_charge_temp_para_info *temp_info = NULL;

	array_len = mca_parse_dts_count_strings(node, name,
		TEMP_PARA_MAX_GROUP,
		MCA_QUICK_CHG_TEMP_PARA_MAX);
	if (array_len < 0) {
#line 4265
		mca_log_err("parse %s failed\n", name);
		return -1;
	}
	if(mode == 0)
		batt_para_info = &(info->batt_para[batt_role]);
	else
		batt_para_info = &(info->base_flip_para[batt_role]);

	batt_para_info->temp_para_size = array_len / MCA_QUICK_CHG_TEMP_PARA_MAX;
	batt_para_info->temp_info = kcalloc(batt_para_info->temp_para_size, sizeof(*temp_info), GFP_KERNEL);
	if (!batt_para_info->temp_info) {
#line 4276
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	temp_info = batt_para_info->temp_info;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_QUICK_CHG_TEMP_PARA_MAX;
		col = i % MCA_QUICK_CHG_TEMP_PARA_MAX;
		switch (col) {
		case MCA_QUICK_CHG_TEMP_LOW:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_low))
				goto error;
			break;
		case MCA_QUICK_CHG_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_high))
				goto error;
			break;
		case MCA_QUICK_CHG_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.low_temp_hysteresis))
				goto error;
			break;
		case MCA_QUICK_CHG_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.high_temp_hysteresis))
				goto error;
			break;
		case MCA_QUICK_CHG_TEMP_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.max_current))
				goto error;
			break;
		case MCA_QUICK_CHG_TEMP_NROMAL_FV:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.normal_max_fv))
				goto error;
			break;
		case MCA_QUICK_CHG_TEMP_FFC_FV:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.ffc_max_fv))
				goto error;
			break;
		case MCA_QUICK_CHG_VOLT_PARA_NAME:
			if (mca_quick_charge_parse_volt_para(node, tmp_string,
				&temp_info[row].volt_info))
				goto error;
			break;
		case MCA_QUICK_CHG_VOLT_FFC_PARA_NAME:
			if (mca_quick_charge_parse_volt_para(node, tmp_string,
				&temp_info[row].volt_ffc_info))
				goto error;
			break;
		case MCA_QUICK_CHG_STAGE_PARA_NAME:
			mca_quick_charge_parse_stage_para(node, tmp_string,
				&temp_info[row].volt_info);
			break;
		case MCA_QUICK_CHG_FFC_STAGE_PARA_NAME:
			mca_quick_charge_parse_stage_para(node, tmp_string,
				&temp_info[row].volt_ffc_info);
			break;
		default:
			break;
		}
	}
	for (i = 0; i < batt_para_info->temp_para_size; i++)
#line 4351
		mca_log_info("temp_para %d %d %d %d %d %d %d %d %d\n",
			temp_info[i].temp_para.temp_low, temp_info[i].temp_para.temp_high, temp_info[i].temp_para.low_temp_hysteresis,
			temp_info[i].temp_para.high_temp_hysteresis, temp_info[i].temp_para.max_current, temp_info[i].temp_para.normal_max_fv,
			temp_info[i].temp_para.ffc_max_fv,
			temp_info[i].volt_info.volt_para_size, temp_info[i].volt_ffc_info.volt_para_size);
	return 0;
error:
	kfree(temp_info);
	temp_info = NULL;
	return -1;
}

static int mca_quick_charge_parse_batt_info(struct mca_quick_charge_info *info, int mode)
{
	struct device_node *node = info->dev->of_node;
	int array_len, row, col, i;
	int batt_role = 0;
	const char *tmp_string = NULL;
	const char *main_cell_name = NULL;
	const char *slave_cell_name = NULL;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	char *battpara[] = {"batt_para", "base_flip_para"};

	if (hwid && info->has_gbl_batt_para && hwid->country_version != CountryCN)
		node = of_find_node_by_name(NULL, "mca_quick_charge_batt_para_gbl");

	if (!node) {
#line 4397
		mca_log_err("node in null\n");
		return -1;
	}

	array_len = mca_parse_dts_count_strings(node, battpara[mode],
		BATT_PARA_MAX_GROUP,
		MCA_QUICK_CHG_BATT_PARA_MAX);
	if (array_len < 0) {
#line 4405
		mca_log_err("parse batt_para failed\n");
		return -1;
	}

	(void)platform_fg_ops_get_batt_cell_info(FG_IC_MASTER, &main_cell_name);
	if (!main_cell_name) {
#line 4411
		mca_log_err("get main cell name fail\n");
		return -EINVAL;
	}
	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		(void)platform_fg_ops_get_batt_cell_info(FG_IC_SLAVE, &slave_cell_name);
		if (!slave_cell_name) {
#line 4419
			mca_log_err("get slave cell name fail\n");
			return -EINVAL;
		}
#line 4422
		mca_log_info("slave_cell_name %s\n", slave_cell_name);
	}
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, battpara[mode], i, &tmp_string))
			return -1;

		row = i / MCA_QUICK_CHG_BATT_PARA_MAX;
		col = i % MCA_QUICK_CHG_BATT_PARA_MAX;
		switch (col) {
		case MCA_QUICK_CHG_BATT_ROLE:
			if (kstrtoint(tmp_string, 10, &batt_role))
				return -1;
			if (batt_role > FG_IC_SLAVE)
				i += 2;
			break;
		case MCA_QUICK_CHG_BATT_ID:
			/* find same batt info */
			if ((batt_role == FG_IC_MASTER && main_cell_name && strcmp(tmp_string, main_cell_name)) ||
				(batt_role == FG_IC_SLAVE && slave_cell_name && strcmp(tmp_string, slave_cell_name))) {
#line 4448
				mca_log_info("can not match cell name\n");
				i++;
			}
			break;
		case MCA_QUICK_CHG_TEMP_PARA_NAME:
			if (mca_quick_charge_parse_temp_para(node, batt_role, tmp_string, info, mode))
				return -1;
			break;
		default:
			break;
		}
	}

	if (!info->batt_para[FG_IC_MASTER].temp_info) {
#line 4462
		mca_log_err("parse master batt para failed\n");
		return -1;
	}

	// if (!info->batt_para[FG_IC_SLAVE].temp_info &&
	// 	info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
	// 	mca_log_err("parse slave batt para failed\n");
	// 	return -1;
	// }
	return 0;
}

/*
 * G13g: the DT parse family below is rewritten from the OS4 stock disassembly (keys, defaults and log strings are the stock ones).
 * The XRING template had these inlined / different.  Member names that BTF keeps as "?strNNN" are GUESS-NAMEs.
 */
/* G13l: from the stock disassembly (stock: called from process_event; here UNWIRED) */
static noinline __used void update_batt_para(struct mca_quick_charge_info *info)
{
	static int last_batt_index[2];
	static int curr_batt_index[2];
	int cycle[2] = { 0 };

	(void)platform_fg_ops_get_cyclecount(FG_IC_FLIP, &cycle[1]);
	(void)platform_fg_ops_get_cyclecount(FG_IC_BASE, &cycle[0]);
#line 3849
	mca_log_err("base_flip_cc, update_batt_para, master:%d, slave:%d\n", cycle[0], cycle[1]);

	if (cycle[1] >= 1) {
		curr_batt_index[1] = 0;
		info->curr_batt_cycle[1] = 0;
	}
	if (cycle[0] >= 1) {
		curr_batt_index[0] = 0;
		info->curr_batt_cycle[0] = 0;
	}
	if (cycle[1] >= 51) {
		curr_batt_index[1] = 1;
		info->curr_batt_cycle[1] = 50;
	}
	if (cycle[0] >= 51) {
		curr_batt_index[0] = 1;
		info->curr_batt_cycle[0] = 50;
	}
	if (cycle[1] >= 101) {
		curr_batt_index[1] = 2;
		info->curr_batt_cycle[1] = 100;
	}
	if (cycle[0] >= 101) {
		curr_batt_index[0] = 2;
		info->curr_batt_cycle[0] = 100;
	}
	if (cycle[1] >= 151) {
		curr_batt_index[1] = 3;
		info->curr_batt_cycle[1] = 150;
	}
	if (cycle[0] >= 151) {
		curr_batt_index[0] = 3;
		info->curr_batt_cycle[0] = 150;
	}
	if (cycle[1] >= 301) {
		curr_batt_index[1] = 4;
		info->curr_batt_cycle[1] = 300;
	}
	if (cycle[0] >= 301) {
		curr_batt_index[0] = 4;
		info->curr_batt_cycle[0] = 300;
	}
	if (cycle[1] >= 801) {
		curr_batt_index[1] = 5;
		info->curr_batt_cycle[1] = 800;
	}
	if (cycle[0] >= 801) {
		curr_batt_index[0] = 5;
		info->curr_batt_cycle[0] = 800;
	}

	if (last_batt_index[1] != curr_batt_index[1] || last_batt_index[0] != curr_batt_index[0]) {
		mca_quick_charge_parse_batt_info(info, info->batt_type);
#line 3866
		mca_log_err("base_flip_cc, update cc data\n");
	}
	last_batt_index[1] = curr_batt_index[1];
	last_batt_index[0] = curr_batt_index[0];
#line 3872
	mca_log_err("base_flip_cc, curr_batt_index_master:%d, curr_batt_index_slave:%d\n",
		curr_batt_index[0], curr_batt_index[1]);
}

static noinline int mca_quick_charge_parse_vstep_para(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, len;
	int data[VSTEP_PARA_MAX_GROUP * MCA_QUICK_CHG_VSTEP_PARA_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "vstep_para", data,
		VSTEP_PARA_MAX_GROUP, MCA_QUICK_CHG_VSTEP_PARA_MAX);
	if (len < 0) {
#line 4483
		mca_log_err("parse vstep_para failed\n");
		return -1;
	}

	for (row = 0; row < len / MCA_QUICK_CHG_VSTEP_PARA_MAX; row++) {
		int col = row * MCA_QUICK_CHG_VSTEP_PARA_MAX;

		info->vstep_para[row].volt_ratio = data[col + MCA_QUICK_CHG_VOLT_RATIO];
		info->vstep_para[row].cycle_count = data[col + MCA_QUICK_CHG_CYCLE_COUNT];
		info->vstep_para[row].cur_gap = data[col + MCA_QUICK_CHG_CURRENT];
		info->vstep_para[row].vbat_gap = data[col + MCA_QUICK_CHG_VBAT_GAP];
		info->vstep_para[row].vstep_ratio = data[col + MCA_QUICK_CHG_VSTEP_RATIO];
	}

	return 0;
}

static noinline int mca_quick_charge_parse_drop_fcc_para(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, len;
	int data[4 * MCA_QUICK_CHG_PARA_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "drop_fcc_para", data, 4, MCA_QUICK_CHG_PARA_MAX);
	if (len < 0) {
#line 4786
		mca_log_err("parse drop_fcc_para failed\n");
		return -1;
	}

	info->drop_fcc_para_size = len / MCA_QUICK_CHG_PARA_MAX;
	for (row = 0; row < info->drop_fcc_para_size; row++) {
		int col = row * MCA_QUICK_CHG_PARA_MAX;

		info->drop_fcc_para[row].cyclecount = data[col + MCA_QUICK_CHG_CYCLE];
		info->drop_fcc_para[row].drop_fcc_step = data[col + MCA_QUICK_CHG_DROP_FCC_STEP];
	}

	return 0;
}

static noinline int mca_quick_charge_parse_enable_cp_para(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, len;
	int data[4 * MCA_QUICK_CHG_EN_CP_PARA_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "enable_cp_para", data, 4, MCA_QUICK_CHG_EN_CP_PARA_MAX);
	if (len < 0) {
#line 4809
		mca_log_err("parse enable_cp_para failed\n");
		return -1;
	}

	info->enable_cp_para_size = len / MCA_QUICK_CHG_EN_CP_PARA_MAX;
	for (row = 0; row < info->enable_cp_para_size; row++) {
		int col = row * MCA_QUICK_CHG_EN_CP_PARA_MAX;

		info->enable_cp_para[row].cyclecount = data[col + MCA_QUICK_CHG_CYCLECOUNT];
		info->enable_cp_para[row].vbat_diff = data[col + MCA_QUICK_CHG_VBAT_DIFF];
		info->enable_cp_para[row].soc_threshold = data[col + MCA_QUICK_CHG_SOC_THR];
	}

	return 0;
}

static noinline void mca_quick_charge_parse_vstep_ex_para(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, len;
	int data[VSTEP_PARA_EX_MAX_GROUP * MCA_QUICK_CHG_VSTEP_PARA_EX_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "vstep_para_ex", data,
		VSTEP_PARA_EX_MAX_GROUP, MCA_QUICK_CHG_VSTEP_PARA_EX_MAX);
	if (len < MCA_QUICK_CHG_VSTEP_PARA_EX_MAX)
		return;

	for (row = 0; row < len / MCA_QUICK_CHG_VSTEP_PARA_EX_MAX; row++) {
		int col = row * MCA_QUICK_CHG_VSTEP_PARA_EX_MAX;

		info->vstep_para_ex[row].delta_v_thr = data[col + MCA_QUICK_CHG_DELTA_V_THR];
		info->vstep_para_ex[row].vstep_multiplier = data[col + MCA_QUICK_CHG_VSTEP_MULTIPLIER];
		info->vstep_para_ex[row].vbat_change_thr = data[col + MCA_QUICK_CHG_VBAT_CHANGE_THR];
		info->vstep_para_ex[row].boost_time_thr = data[col + MCA_QUICK_CHG_BOOST_TIME_THR];
	}
}

/* hwid values (platform_version 3 = O11U): the template names them MCA_QUICK_CHG_O11U_P*; the set is taken from the stock compare chain */
static __always_inline bool mca_quick_charge_is_en_buck_hwid(void)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!hwid) {
#line 2962
		mca_log_err("get hwid info failed\n");
		return false;
	}
	if (hwid->platform_version == 3) {
		switch (hwid->hwid_value) {
		case 0x00001:
		case 0x10000:
		case 0x10002:
		case 0x10003:
		case 0x100000:
		case 0x100001:
		case 0x110000:
			return false;
		}
	}
	return true;
}

static noinline void mca_quick_charge_parse_chg_mode_info(struct device_node *node,
	struct mca_quick_charge_info *info)
{
	u32 idata[CHG_MODE_MAX] = { 0 };
	u32 curr_data[2] = { 0 };
	int ret;

	ret = mca_parse_dts_u32_array(node, "div_delta_volt", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_delta_volt[CHG_MODE_DIV1] = 300;
		info->div_delta_volt[CHG_MODE_DIV2] = 300;
		info->div_delta_volt[CHG_MODE_DIV4] = 500;
	} else {
		memcpy(info->div_delta_volt, idata, sizeof(idata));
	}
#line 4540
	mca_log_info("div_delta_volt %d %d %d\n", info->div_delta_volt[CHG_MODE_DIV1],
		info->div_delta_volt[CHG_MODE_DIV2], info->div_delta_volt[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_delta_ibat", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_delta_ibat[CHG_MODE_DIV1] = 200;
		info->div_delta_ibat[CHG_MODE_DIV2] = 200;
		info->div_delta_ibat[CHG_MODE_DIV4] = 200;
	} else {
		memcpy(info->div_delta_ibat, idata, sizeof(idata));
	}
#line 4551
	mca_log_info("div_delta_ibat %d %d %d\n", info->div_delta_ibat[CHG_MODE_DIV1],
		info->div_delta_ibat[CHG_MODE_DIV2], info->div_delta_ibat[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_single_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_single_curr[CHG_MODE_DIV1] = 5000;
		info->div_single_curr[CHG_MODE_DIV2] = 12000;
		info->div_single_curr[CHG_MODE_DIV4] = 14000;
	} else {
		memcpy(info->div_single_curr, idata, sizeof(idata));
		if (info->en_buck_parallel_chg && !mca_quick_charge_is_en_buck_hwid())
			info->div_single_curr[CHG_MODE_DIV4] = 19000;
	}
#line 4567
	mca_log_info("div_single_curr %d %d %d\n", info->div_single_curr[CHG_MODE_DIV1],
		info->div_single_curr[CHG_MODE_DIV2], info->div_single_curr[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "buck_icl_fcc_curr", curr_data, 2);
	if (ret) {
		info->buck_icl_fcc_curr[0] = 0;
		info->buck_icl_fcc_curr[1] = 0;
	} else {
		memcpy(info->buck_icl_fcc_curr, curr_data, sizeof(curr_data));
	}
#line 4577
	mca_log_info("buck_icl_fcc_curr %d %d\n", info->buck_icl_fcc_curr[0], info->buck_icl_fcc_curr[1]);

	ret = mca_parse_dts_u32_array(node, "div_max_curr", idata, CHG_MODE_MAX);
	if (ret) {
		memcpy(info->div_max_curr, info->div_single_curr, sizeof(info->div_max_curr));
	} else {
		memcpy(info->div_max_curr, idata, sizeof(idata));
	}

	ret = mca_parse_dts_u32_array(node, "non_mi_pps_max_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->non_mi_pps_max_curr[CHG_MODE_DIV1] = 5000;
		info->non_mi_pps_max_curr[CHG_MODE_DIV2] = 12000;
		info->non_mi_pps_max_curr[CHG_MODE_DIV4] = 14000;
	} else {
		memcpy(info->non_mi_pps_max_curr, idata, sizeof(idata));
	}
	/* stock label says div_delta_volt for the div_max_curr triple (copy/paste in the stock) */
#line 4597
	mca_log_info("div_delta_volt %d %d %d, non_mi_pps_max_curr: %d %d %d\n",
		info->div_max_curr[CHG_MODE_DIV1], info->div_max_curr[CHG_MODE_DIV2], info->div_max_curr[CHG_MODE_DIV4],
		info->non_mi_pps_max_curr[CHG_MODE_DIV1], info->non_mi_pps_max_curr[CHG_MODE_DIV2],
		info->non_mi_pps_max_curr[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "multi_ibus_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->multi_ibus_th[CHG_MODE_DIV1] = 1500;
		info->multi_ibus_th[CHG_MODE_DIV2] = 1500;
		info->multi_ibus_th[CHG_MODE_DIV4] = 1500;
	} else {
		memcpy(info->multi_ibus_th, idata, sizeof(idata));
	}
#line 4610
	mca_log_info("multi_ibus_th %d %d %d\n", info->multi_ibus_th[CHG_MODE_DIV1],
		info->multi_ibus_th[CHG_MODE_DIV2], info->multi_ibus_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "again_multi_ibus_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->again_multi_ibus_th[CHG_MODE_DIV1] = 2800;
		info->again_multi_ibus_th[CHG_MODE_DIV2] = 2800;
		info->again_multi_ibus_th[CHG_MODE_DIV4] = 2800;
	} else {
		memcpy(info->again_multi_ibus_th, idata, sizeof(idata));
	}
#line 4621
	mca_log_info("again_multi_ibus_th %d %d %d\n", info->again_multi_ibus_th[CHG_MODE_DIV1],
		info->again_multi_ibus_th[CHG_MODE_DIV2], info->again_multi_ibus_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_inc_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_inc_hysteresis[CHG_MODE_DIV1] = 200;
		info->ibus_inc_hysteresis[CHG_MODE_DIV2] = 200;
		info->ibus_inc_hysteresis[CHG_MODE_DIV4] = 200;
	} else {
		memcpy(info->ibus_inc_hysteresis, idata, sizeof(idata));
	}
#line 4632
	mca_log_info("ibus_inc_hysteresis %d %d %d\n", info->ibus_inc_hysteresis[CHG_MODE_DIV1],
		info->ibus_inc_hysteresis[CHG_MODE_DIV2], info->ibus_inc_hysteresis[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_dec_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_dec_hysteresis[CHG_MODE_DIV1] = 200;
		info->ibus_dec_hysteresis[CHG_MODE_DIV2] = 200;
		info->ibus_dec_hysteresis[CHG_MODE_DIV4] = 200;
	} else {
		memcpy(info->ibus_dec_hysteresis, idata, sizeof(idata));
	}
#line 4643
	mca_log_info("ibus_dec_hysteresis %d %d %d\n", info->ibus_dec_hysteresis[CHG_MODE_DIV1],
		info->ibus_dec_hysteresis[CHG_MODE_DIV2], info->ibus_dec_hysteresis[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "open_path_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->open_path_th[CHG_MODE_DIV1] = 300;
		info->open_path_th[CHG_MODE_DIV2] = 300;
		info->open_path_th[CHG_MODE_DIV4] = 300;
	} else {
		memcpy(info->open_path_th, idata, sizeof(idata));
	}
#line 4654
	mca_log_info("open_path_th %d %d %d\n", info->open_path_th[CHG_MODE_DIV1],
		info->open_path_th[CHG_MODE_DIV2], info->open_path_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_compensation", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_compensation[CHG_MODE_DIV1] = 0;
		info->ibus_compensation[CHG_MODE_DIV2] = 0;
		info->ibus_compensation[CHG_MODE_DIV4] = 0;
	} else {
		memcpy(info->ibus_compensation, idata, sizeof(idata));
	}
#line 4665
	mca_log_info("ibus_compensation: %d %d %d\n", info->ibus_compensation[CHG_MODE_DIV1],
		info->ibus_compensation[CHG_MODE_DIV2], info->ibus_compensation[CHG_MODE_DIV4]);

	info->cp_work_mode.support_mode_switch = of_find_property(node, "support-cp-mode-switch", NULL) != NULL;
	if (info->cp_work_mode.support_mode_switch) {
		ret = mca_parse_dts_u32_array(node, "cp-mode-switch-threshold",
			(u32 *)info->cp_work_mode.mode_table, 6);
		if (ret)
			info->cp_work_mode.support_mode_switch = false;
	}
#line 4675
	mca_log_info("support_mode_switch: %d, threshold: [%d %d] [%d %d] [%d %d]\n",
		info->cp_work_mode.support_mode_switch,
		info->cp_work_mode.mode_table[0].l_thres, info->cp_work_mode.mode_table[0].h_thres,
		info->cp_work_mode.mode_table[1].l_thres, info->cp_work_mode.mode_table[1].h_thres,
		info->cp_work_mode.mode_table[2].l_thres, info->cp_work_mode.mode_table[2].h_thres);
}

static noinline void mca_quick_charge_parse_pmic_single_cp_charging_info(struct device_node *node,
	struct mca_quick_charge_info *info)
{
	u32 data[4] = { 0 };
	int ret;

	ret = mca_parse_dts_u32_array(node, "cur_max_threshold", data, 3);
	if (ret) {
		info->cur_max_threshold[0] = 10000;
		info->cur_max_threshold[1] = 8000;
		info->cur_max_threshold[2] = 5000;
	} else {
		memcpy(info->cur_max_threshold, data, sizeof(info->cur_max_threshold));
	}
#line 4723
	mca_log_info("cur_max_threshold %d %d %d\n", info->cur_max_threshold[0],
		info->cur_max_threshold[1], info->cur_max_threshold[2]);

	ret = mca_parse_dts_u32_array(node, "ibus_threshold", data, 3);
	if (ret) {
		info->ibus_threshold[0] = 5000;
		info->ibus_threshold[1] = 4200;
		info->ibus_threshold[2] = 2700;
	} else {
		memcpy(info->ibus_threshold, data, sizeof(info->ibus_threshold));
	}
#line 4733
	mca_log_info("ibus_threshold %d %d %d\n", info->ibus_threshold[0],
		info->ibus_threshold[1], info->ibus_threshold[2]);

	ret = mca_parse_dts_u32_array(node, "pmih_fcc_value", data, 4);
	if (ret) {
		info->pmih_fcc_value[0] = 4000;
		info->pmih_fcc_value[1] = 3000;
		info->pmih_fcc_value[2] = 2000;
		info->pmih_fcc_value[3] = 1000;
	} else {
		memcpy(info->pmih_fcc_value, data, sizeof(info->pmih_fcc_value));
	}
#line 4745
	mca_log_info("pmih_fcc_value %d %d %d %d\n", info->pmih_fcc_value[0], info->pmih_fcc_value[1],
		info->pmih_fcc_value[2], info->pmih_fcc_value[3]);

	(void)mca_parse_dts_u32(node, "vbat_threshold", &info->vbat_threshold, 4480);
#line 4749
	mca_log_info("vbat_threshold :%d\n", info->vbat_threshold);
}

static noinline void mca_quick_charge_parse_vfc_info(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, len;
	int data[MCA_VFC_PARA_MAX_GROUP * MCA_QUICK_CHG_VFC_PARA_SIZE] = { 0 };

	mca_parse_dts_u32(node, "support_cp_vfc", &info->vfc_para.support_cp_vfc, 0);
	if (!info->vfc_para.support_cp_vfc) {
#line 4690
		mca_log_err("not support cp vfc\n");
		return;
	}

	len = mca_parse_dts_string_array(node, "vfc_iout_fsw_map", data,
		MCA_VFC_PARA_MAX_GROUP, MCA_QUICK_CHG_VFC_PARA_SIZE);
	if (len < 0) {
#line 4697
		mca_log_err("parse vfc_iout_fsw_map failed\n");
		return;
	}

	info->vfc_para.vfc_para_size = len / MCA_QUICK_CHG_VFC_PARA_SIZE;
	for (row = 0; row < info->vfc_para.vfc_para_size; row++) {
		int col = row * MCA_QUICK_CHG_VFC_PARA_SIZE;

		info->vfc_para.iout_fsw_map[row].iout = data[col + MCA_QUICK_CHG_VFC_PARA_IOUT];
		info->vfc_para.iout_fsw_map[row].fsw = data[col + MCA_QUICK_CHG_VFC_PARA_FSW];
	}
}

static noinline void mca_quick_charge_parse_third_pps_power_info(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (hwid) {
		if (!hwid->country_version)
			goto cn;
		mca_parse_dts_u32(node, "gl_third_pps_vbus_max", &info->third_pps_vbus_max, 0);
		mca_parse_dts_u32(node, "gl_third_pps_ibus_max", &info->third_pps_ibus_max, 0);
		mca_parse_dts_u32(node, "gl_third_pps_ibat_limit", &info->third_pps_ibat_limit, 6000);
#line 4761
		mca_log_info("Country_version is %d, gl_third_pps_vbus_max is %d, gl_third_pps_ibus_max is %d, third_pps_ibat_limit is %d\n",
			hwid->country_version, info->third_pps_vbus_max, info->third_pps_ibus_max,
			info->third_pps_ibat_limit);
		if (!hwid->country_version)
			goto cn;
	}
	if (info->third_pps_vbus_max || info->third_pps_ibus_max)
		goto out;
cn:
	mca_parse_dts_u32(node, "third_pps_vbus_max", &info->third_pps_vbus_max, 0);
	mca_parse_dts_u32(node, "third_pps_ibus_max", &info->third_pps_ibus_max, 0);
out:
#line 4768
	mca_log_info("Country_version is %d, third_pps_vbus_max is %d, third_pps_ibus_max is %d, third_pps_ibat_limit is %d\n",
		hwid->country_version, info->third_pps_vbus_max, info->third_pps_ibus_max,
		info->third_pps_ibat_limit);
}

static int mca_quick_charge_parse_dt(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int ret, r2;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	(void)mca_parse_dts_u32(node, "batt_type", &info->batt_type, 0);
	(void)mca_parse_dts_u32(node, "cp_type", &info->cp_type, 0);
	(void)mca_parse_dts_u32(node, "en_buck_parallel_chg", &info->en_buck_parallel_chg, 0);
	(void)mca_parse_dts_u32(node, "fv_hys_delta_mv", &info->fv_hys_delta_mv, 0);
	(void)mca_parse_dts_u32(node, "curr_terminate_ratio", &info->curr_terminate_ratio, 110);
	(void)mca_parse_dts_u32(node, "curr_terminate_section", &info->curr_terminate_section, 3000);
	(void)mca_parse_dts_u32(node, "min_vbat", &info->min_vbat, 3300);
	(void)mca_parse_dts_u32(node, "max_vbat", &info->max_vbat, 4400);
	(void)mca_parse_dts_u32(node, "recharge_vbat", &info->recharge_vbat, 4300);
	(void)mca_parse_dts_u32(node, "die_temp_max", &info->die_temp_max, 125);
	(void)mca_parse_dts_u32(node, "adp_temp_max", &info->adp_temp_max, 125);
	(void)mca_parse_dts_u32(node, "support_mode", &info->support_mode, 0);
	(void)mca_parse_dts_u32(node, "support_ichg_cutoff_priority", &info->support_ichg_cutoff_priority, 1);
	(void)mca_parse_dts_u32(node, "max_power", &info->max_power, 0);
	(void)mca_parse_dts_u32(node, "pmic_fv_compensation", &info->pmic_fv_compensation, 0);
	(void)mca_parse_dts_u32(node, "ui_max_power_limit", &info->ui_max_power_limit, info->max_power);
	(void)mca_parse_dts_u32(node, "qc3_max_vbus_limit", &info->qc3_max_vbus_limit_mv, 9500);
	(void)mca_parse_dts_u32(node, "qc3_max_ibus_limit", &info->qc3_max_ibus_limit_ma, 2000);
	(void)mca_parse_dts_u32(node, "qc3p5_max_vbus_limit", &info->qc3p5_max_vbus_limit_mv, 10000);
	(void)mca_parse_dts_u32(node, "qc3p5_max_ibus_limit", &info->qc3p5_max_ibus_limit_ma, 2000);
	(void)mca_parse_dts_u32(node, "qc3_ibat_max_limit", &info->qc3_ibat_max_limit_ma, 4000);
	(void)mca_parse_dts_u32(node, "qc3p5_ibat_max_limit", &info->qc3p5_max_ibat_limit_ma, 4000);
	(void)mca_parse_dts_u32(node, "qc_normal_charge_fv", &info->qc_normal_charge_fv_max_mv, 4440);
	(void)mca_parse_dts_u32(node, "qc_taper_fcc_thr", &info->qc_taper_fcc_ma, 2500);
	(void)mca_parse_dts_u32(node, "qc_taper_soc_thr", &info->qc_taper_soc_thr, 100);
	(void)mca_parse_dts_u32(node, "pps_taper_fcc_thr", &info->pps_taper_fcc_ma, 2500);
	(void)mca_parse_dts_u32(node, "pps_middle_taper_fcc_thr", &info->pps_middle_taper_fcc_ma, info->pps_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "pps_high_taper_fcc_thr", &info->pps_high_taper_fcc_ma, info->pps_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "qc3_taper_vol_hys", &info->qc3_taper_vol_hys, 30);
	(void)mca_parse_dts_u32(node, "qc3p5_taper_vol_hys", &info->qc3p5_taper_vol_hys, 20);
	(void)mca_parse_dts_u32(node, "pps_taper_vol_hys", &info->pps_taper_vol_hys, 10);
	(void)mca_parse_dts_u32(node, "hardware_cv", &info->hardware_cv, 0);
	(void)mca_parse_dts_u32(node, "support_curr_monitor", &info->support_curr_monitor, 0);
	(void)mca_parse_dts_u32(node, "curr_monitor_time_s", &info->curr_monitor_time_s, 0);
	(void)mca_parse_dts_u32(node, "cp_switch_pmic_th", &info->cp_switch_pmic_th, 1500);
	(void)mca_parse_dts_u32(node, "third_pps_switch_pmic", &info->third_pps_switch_pmic, 0);
	(void)mca_parse_dts_u32(node, "quick_chg_fv_hys", &info->quick_chg_fv_hys, 1);
	(void)mca_parse_dts_u32(node, "max_vbat_threshold", &info->max_vbat_threshold, 5000);
	(void)mca_parse_dts_u32(node, "mult_to_single_count", &info->mult_to_single_count, 2);
	info->has_gbl_batt_para = of_find_property(node, "has-global-batt-para", NULL) != NULL;
	info->support_pmic_vterm_dynamics_adjust = of_find_property(node, "support-pmic-vterm-dynamics-adjust", NULL) != NULL;
	info->support_dtpt_ichg_dec = of_find_property(node, "support-dtpt-ichg-dec", NULL) != NULL;
	info->enable_vstep_ex = of_find_property(node, "enable-vstep-ex", NULL) != NULL;
	info->support_base_flip = of_find_property(node, "support-base-flip", NULL) != NULL;
	info->support_cancel_compensation = of_find_property(node, "support_cancel_compensation", NULL) != NULL;
	info->pmic_chg_need_fixed_volt = of_find_property(node, "pmic-chg-need-fixed-volt", NULL) != NULL;
	info->base_flip_same = of_find_property(node, "base-flip-same", NULL) != NULL;
	info->pmic_single_cp_chg = of_find_property(node, "pmic_single_cp_chg", NULL) != NULL;
	info->avoid_overvolt_at_high_temp = of_find_property(node, "avoid-overvolt-at-high-temp", NULL) != NULL;
	(void)mca_parse_dts_u32(node, "use_buck_fcc_threshold", &info->use_buck_fcc_threshold, 16000);
	info->support_third_pps_power = of_find_property(node, "support-third-pps-power", NULL) != NULL;
	(void)mca_parse_dts_u32(node, "third_pps_ibat_limit", &info->third_pps_ibat_limit, 6000);
	info->use_buck_chg_once = of_find_property(node, "use_buck_chg_once", NULL) != NULL;
	(void)mca_parse_dts_u32(node, "drop_fv_cycle", &info->drop_fv_cycle, 300);
	(void)mca_parse_dts_u32(node, "drop_fv_value", &info->drop_fv_value, 20);
	(void)mca_parse_dts_u32(node, "effective-on-p-generation", &info->effective_on_p_generation, 0);
	(void)mca_parse_dts_u32(node, "anti-overvoltage-strategy", &info->anti_overvoltage_strategy, 0);
	(void)mca_parse_dts_u32(node, "reduce_max_cur", &info->reduce_max_cur, 0);
	info->support_dtpt_reduce_max_cur = of_find_property(node, "support-dtpt-reduce-max-cur", NULL) != NULL;
	info->support_eu_power = of_find_property(node, "support-eu-power", NULL) != NULL;
	(void)mca_parse_dts_u32(node, "eu-vbus-max", &info->eu_vbus_max, 0);
	(void)mca_parse_dts_u32(node, "eu-ibus-max", &info->eu_ibus_max, 0);
	/* the stock parses these two keys a second time (copy/paste in the stock source) */
	(void)mca_parse_dts_u32(node, "drop_fv_cycle", &info->drop_fv_cycle, 300);
	(void)mca_parse_dts_u32(node, "drop_fv_value", &info->drop_fv_value, 20);

	mca_quick_charge_parse_chg_mode_info(node, info);
	ret = mca_quick_charge_parse_vstep_para(info);
	ret |= mca_quick_charge_parse_drop_fcc_para(info);
	ret |= mca_quick_charge_parse_enable_cp_para(info);
	if (ret < 0) {
#line 4940
		mca_log_err("parse charge para failed\n");
		return -EINVAL;
	}
	if (info->enable_vstep_ex) {
#line 4945
		mca_log_err("parse vstep_ex_para\n");
		mca_quick_charge_parse_vstep_ex_para(info);
	}
	r2 = mca_quick_charge_parse_batt_info(info, 0);
	if (info->support_base_flip) {
#line 4950
		mca_log_err("parse mode for paraller batt\n");
		r2 = mca_quick_charge_parse_batt_info(info, info->batt_type);
	}
	if (info->pmic_single_cp_chg)
		mca_quick_charge_parse_pmic_single_cp_charging_info(node, info);
	mca_quick_charge_parse_vfc_info(info);
	if (mca_parse_dts_u32_array(node, "ocp_threshold", (u32 *)info->proc_data.ocp_thr, 2)) {
		info->proc_data.ocp_thr[FG_IC_MASTER] = OCP_THRESHOLD_MAINT;
		info->proc_data.ocp_thr[FG_IC_SLAVE] = OCP_THRESHOLD_FLIP;
#line 4961
		mca_log_err("parse ocp_threshold failed, use default value\n");
	}
#line 4963
	mca_log_info("ocp_threshold : %d %d\n", info->proc_data.ocp_thr[0], info->proc_data.ocp_thr[1]);
	if (info->support_third_pps_power)
		mca_quick_charge_parse_third_pps_power_info(info);
	info->monitor_interval = of_find_property(node, "use-normal-interval", NULL) ? 1000 : 700;
#line 4972
	mca_log_err("monitor_interval = %d\n", info->monitor_interval);

	return r2;
}

static int mca_quick_charge_parse_dt_override(struct mca_quick_charge_info *info)
{
	struct device_node *orig = info->dev->of_node;
	struct device_node *node = of_get_child_by_name(orig, "strategy_quick_charge_override");
	int ret;

	if (!node) {
#line 4985
		mca_log_info("child node 'strategy_quick_charge_override' not found, using parent config\n");
		return 0;
	}
#line 4989
	mca_log_info("applying strategy_quickchg override from child node\n");
	(void)mca_parse_dts_u32(node, "batt_type", &info->batt_type, info->batt_type);
	(void)mca_parse_dts_u32(node, "cp_type", &info->cp_type, info->cp_type);
	(void)mca_parse_dts_u32(node, "en_buck_parallel_chg", &info->en_buck_parallel_chg, info->en_buck_parallel_chg);
	(void)mca_parse_dts_u32(node, "fv_hys_delta_mv", &info->fv_hys_delta_mv, info->fv_hys_delta_mv);
	(void)mca_parse_dts_u32(node, "curr_terminate_ratio", &info->curr_terminate_ratio, info->curr_terminate_ratio);
	(void)mca_parse_dts_u32(node, "curr_terminate_section", &info->curr_terminate_section, info->curr_terminate_section);
	(void)mca_parse_dts_u32(node, "min_vbat", &info->min_vbat, info->min_vbat);
	(void)mca_parse_dts_u32(node, "max_vbat", &info->max_vbat, info->max_vbat);
	(void)mca_parse_dts_u32(node, "recharge_vbat", &info->recharge_vbat, info->recharge_vbat);
	(void)mca_parse_dts_u32(node, "die_temp_max", &info->die_temp_max, info->die_temp_max);
	(void)mca_parse_dts_u32(node, "adp_temp_max", &info->adp_temp_max, info->adp_temp_max);
	(void)mca_parse_dts_u32(node, "support_mode", &info->support_mode, info->support_mode);
	(void)mca_parse_dts_u32(node, "support_ichg_cutoff_priority", &info->support_ichg_cutoff_priority, info->support_ichg_cutoff_priority);
	(void)mca_parse_dts_u32(node, "max_power", &info->max_power, info->max_power);
	(void)mca_parse_dts_u32(node, "pmic_fv_compensation", &info->pmic_fv_compensation, info->pmic_fv_compensation);
	(void)mca_parse_dts_u32(node, "ui_max_power_limit", &info->ui_max_power_limit, info->ui_max_power_limit);
	(void)mca_parse_dts_u32(node, "qc3_max_vbus_limit", &info->qc3_max_vbus_limit_mv, info->qc3_max_vbus_limit_mv);
	(void)mca_parse_dts_u32(node, "qc3_max_ibus_limit", &info->qc3_max_ibus_limit_ma, info->qc3_max_ibus_limit_ma);
	(void)mca_parse_dts_u32(node, "qc3p5_max_vbus_limit", &info->qc3p5_max_vbus_limit_mv, info->qc3p5_max_vbus_limit_mv);
	(void)mca_parse_dts_u32(node, "qc3p5_max_ibus_limit", &info->qc3p5_max_ibus_limit_ma, info->qc3p5_max_ibus_limit_ma);
	(void)mca_parse_dts_u32(node, "qc3_ibat_max_limit", &info->qc3_ibat_max_limit_ma, info->qc3_ibat_max_limit_ma);
	(void)mca_parse_dts_u32(node, "qc3p5_ibat_max_limit", &info->qc3p5_max_ibat_limit_ma, info->qc3p5_max_ibat_limit_ma);
	(void)mca_parse_dts_u32(node, "qc_normal_charge_fv", &info->qc_normal_charge_fv_max_mv, info->qc_normal_charge_fv_max_mv);
	(void)mca_parse_dts_u32(node, "qc_taper_fcc_thr", &info->qc_taper_fcc_ma, info->qc_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "qc_taper_soc_thr", &info->qc_taper_soc_thr, info->qc_taper_soc_thr);
	(void)mca_parse_dts_u32(node, "pps_taper_fcc_thr", &info->pps_taper_fcc_ma, info->pps_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "pps_middle_taper_fcc_thr", &info->pps_middle_taper_fcc_ma, info->pps_middle_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "pps_high_taper_fcc_thr", &info->pps_high_taper_fcc_ma, info->pps_high_taper_fcc_ma);
	(void)mca_parse_dts_u32(node, "qc3_taper_vol_hys", &info->qc3_taper_vol_hys, info->qc3_taper_vol_hys);
	(void)mca_parse_dts_u32(node, "qc3p5_taper_vol_hys", &info->qc3p5_taper_vol_hys, info->qc3p5_taper_vol_hys);
	(void)mca_parse_dts_u32(node, "pps_taper_vol_hys", &info->pps_taper_vol_hys, info->pps_taper_vol_hys);
	(void)mca_parse_dts_u32(node, "hardware_cv", &info->hardware_cv, info->hardware_cv);
	(void)mca_parse_dts_u32(node, "support_curr_monitor", &info->support_curr_monitor, info->support_curr_monitor);
	(void)mca_parse_dts_u32(node, "curr_monitor_time_s", &info->curr_monitor_time_s, info->curr_monitor_time_s);
	(void)mca_parse_dts_u32(node, "cp_switch_pmic_th", &info->cp_switch_pmic_th, info->cp_switch_pmic_th);
	(void)mca_parse_dts_u32(node, "third_pps_switch_pmic", &info->third_pps_switch_pmic, info->third_pps_switch_pmic);
	(void)mca_parse_dts_u32(node, "quick_chg_fv_hys", &info->quick_chg_fv_hys, info->quick_chg_fv_hys);
	(void)mca_parse_dts_u32(node, "max_vbat_threshold", &info->max_vbat_threshold, info->max_vbat_threshold);
	(void)mca_parse_dts_u32(node, "mult_to_single_count", &info->mult_to_single_count, info->mult_to_single_count);
	if (of_find_property(node, "support-dtpt-reduce-max-cur", NULL))
		info->support_dtpt_reduce_max_cur = 1;
	info->has_gbl_batt_para = of_find_property(node, "has-global-batt-para", NULL) != NULL;
	if (of_find_property(node, "support-pmic-vterm-dynamics-adjust", NULL))
		info->support_pmic_vterm_dynamics_adjust = 1;
	info->support_taper_fv_from_volt_para = of_find_property(node, "support-taper-fv-from-volt-para", NULL) != NULL;
	if (of_find_property(node, "support-dtpt-ichg-dec", NULL))
		info->support_dtpt_ichg_dec = 1;
	if (of_find_property(node, "enable-vstep-ex", NULL))
		info->enable_vstep_ex = 1;
	if (of_find_property(node, "support-base-flip", NULL))
		info->support_base_flip = 1;
	if (of_find_property(node, "support_cancel_compensation", NULL))
		info->support_cancel_compensation = true;
	if (of_find_property(node, "pmic-chg-need-fixed-volt", NULL))
		info->pmic_chg_need_fixed_volt = true;
	if (of_find_property(node, "base-flip-same", NULL))
		info->base_flip_same = 1;
	if (of_find_property(node, "pmic_single_cp_chg", NULL))
		info->pmic_single_cp_chg = 1;
	info->avoid_overvolt_at_high_temp = of_find_property(node, "avoid-overvolt-at-high-temp", NULL) != NULL;
	(void)mca_parse_dts_u32(node, "use_buck_fcc_threshold", &info->use_buck_fcc_threshold, info->use_buck_fcc_threshold);
	if (of_find_property(node, "support-third-pps-power", NULL))
		info->support_third_pps_power = true;
	(void)mca_parse_dts_u32(node, "third_pps_ibat_limit", &info->third_pps_ibat_limit, info->third_pps_ibat_limit);
	if (of_find_property(node, "use_buck_chg_once", NULL))
		info->use_buck_chg_once = true;
	(void)mca_parse_dts_u32(node, "drop_fv_cycle", &info->drop_fv_cycle, info->drop_fv_cycle);
	(void)mca_parse_dts_u32(node, "drop_fv_value", &info->drop_fv_value, info->drop_fv_value);
	(void)mca_parse_dts_u32(node, "effective-on-p-generation", &info->effective_on_p_generation, info->effective_on_p_generation);
	(void)mca_parse_dts_u32(node, "anti-overvoltage-strategy", &info->anti_overvoltage_strategy, info->anti_overvoltage_strategy);
	info->dev->of_node = node;
	mca_quick_charge_parse_chg_mode_info(node, info);
	mca_quick_charge_parse_vstep_para(info);
	mca_quick_charge_parse_drop_fcc_para(info);
	mca_quick_charge_parse_enable_cp_para(info);
	if (info->enable_vstep_ex) {
#line 5073
		mca_log_err("override parse vstep_ex_para\n");
		mca_quick_charge_parse_vstep_ex_para(info);
	}
	ret = mca_quick_charge_parse_batt_info(info, 0);
	(void)ret;
	if (info->support_base_flip) {
#line 5078
		mca_log_err("override parse mode for paraller batt\n");
		ret = mca_quick_charge_parse_batt_info(info, info->batt_type);
	}
	if (info->pmic_single_cp_chg)
		mca_quick_charge_parse_pmic_single_cp_charging_info(node, info);
	mca_quick_charge_parse_vfc_info(info);
	if (mca_parse_dts_u32_array(node, "ocp_threshold", (u32 *)info->proc_data.ocp_thr, 2)) {
		info->proc_data.ocp_thr[FG_IC_MASTER] = OCP_THRESHOLD_MAINT;
		info->proc_data.ocp_thr[FG_IC_SLAVE] = OCP_THRESHOLD_FLIP;
#line 5090
		mca_log_err("override parse ocp_threshold failed, use default value\n");
	}
#line 5094
	mca_log_info("override ocp_threshold : %d %d\n", info->proc_data.ocp_thr[0], info->proc_data.ocp_thr[1]);
	if (info->support_third_pps_power)
		mca_quick_charge_parse_third_pps_power_info(info);
	info->dev->of_node = orig;
#line 5101
	mca_log_info("strategy_quickchg override parse completed\n");
	return 0;
}

#ifdef CONFIG_SYSFS
static ssize_t mca_quick_charge_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t mca_quick_charge_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info mca_quick_charge_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_MODE_ENABLE, mode_enable),
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_CP_PATH_ENABLE, cp_path_enable),
	mca_sysfs_attr_ro(mca_quick_charge_sysfs, 0440, MCA_QUICK_CHG_POWER_MAX, power_max),
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_FAKE_PPS_PTF, fake_pps_ptf),
	mca_sysfs_attr_ro(mca_quick_charge_sysfs, 0440, MCA_QUICK_CHG_TYPE, quick_charge_type),
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_CURR_LIMIT, current_limit),
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_CURR_RATIO, current_ration),
	mca_sysfs_attr_rw(mca_quick_charge_sysfs, 0664, MCA_QUICK_CHG_VOLT_DEC, volt_dec),
	mca_sysfs_attr_ro(mca_quick_charge_sysfs, 0440, MCA_QUICK_CHG_VFC_IOUT, vfc_iout),
};

#define MCA_QUICK_CHG_ATTRS_SIZE ARRAY_SIZE(mca_quick_charge_sysfs_field_tbl)

static struct attribute *mca_quick_charge_sysfs_attrs[MCA_QUICK_CHG_ATTRS_SIZE + 1];

static const struct attribute_group mca_quick_charge_sysfs_attr_group = {
	.attrs = mca_quick_charge_sysfs_attrs,
};

static ssize_t mca_quick_charge_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mca_quick_charge_info *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int len = 0;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		mca_quick_charge_sysfs_field_tbl, MCA_QUICK_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_QUICK_CHG_MODE_ENABLE:
		len = snprintf(buf, PAGE_SIZE, "div4_en=%d div2_en=%d div1_en=%d\n",
			info->sysfs_data.mode_enable[CHG_MODE_DIV4],
			info->sysfs_data.mode_enable[CHG_MODE_DIV2],
			info->sysfs_data.mode_enable[CHG_MODE_DIV1]);
		break;
	case MCA_QUICK_CHG_CP_PATH_ENABLE:
		len = snprintf(buf, PAGE_SIZE, "div4_m=%d div4_s=%d div2_m=%d div2_s=%d div1_m=%d div1_s=%d\n",
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV4][CP_ROLE_MASTER],
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV4][CP_ROLE_SLAVE],
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV2][CP_ROLE_MASTER],
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV2][CP_ROLE_SLAVE],
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV1][CP_ROLE_MASTER],
			info->sysfs_data.cp_path_enable[CHG_MODE_DIV1][CP_ROLE_SLAVE]);
		break;
	case MCA_QUICK_CHG_FAKE_PPS_PTF:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->fake_pps_ptf);
		break;
	case MCA_QUICK_CHG_POWER_MAX:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.ui_power);
		break;
	case MCA_QUICK_CHG_TYPE:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.quick_charge_type);
		break;
	case MCA_QUICK_CHG_CURR_LIMIT:
		len = snprintf(buf, PAGE_SIZE, "div4_s=%d div4_m=%d div2_s=%d div2_m=%d div1_s=%d div1_m=%d\n",
			info->sysfs_data.curr_limit[CHG_MODE_DIV4][MCA_QUICK_CHG_CH_SINGLE],
			info->sysfs_data.curr_limit[CHG_MODE_DIV4][MCA_QUICK_CHG_CH_MULTI],
			info->sysfs_data.curr_limit[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE],
			info->sysfs_data.curr_limit[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_MULTI],
			info->sysfs_data.curr_limit[CHG_MODE_DIV1][MCA_QUICK_CHG_CH_SINGLE],
			info->sysfs_data.curr_limit[CHG_MODE_DIV1][MCA_QUICK_CHG_CH_MULTI]);
		break;
	case MCA_QUICK_CHG_CURR_RATIO:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->sysfs_data.cur_ratio);
		break;
	case MCA_QUICK_CHG_VOLT_DEC:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->sysfs_data.volt_dec);
		break;
	case MCA_QUICK_CHG_VFC_IOUT:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->proc_data.vfc_iout);
		break;
	default:
		break;
	}

	return len;
}

static void mca_quick_charge_set_single_mode_chg_enable(int mode,
	int value, struct mca_quick_charge_info *info)
{
	if (value == info->sysfs_data.mode_enable[mode])
		return;

	info->sysfs_data.mode_enable[mode] = value;
	if (value) {
		info->cur_support_mode |= BIT(mode);
	} else {
		info->cur_support_mode &= ~(BIT(mode));
		if (info->proc_data.work_mode != BIT(mode))
			return;

		if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING)
			mca_quick_charge_force_stop_charging(info);
	}
}

static void mca_quick_charge_set_chg_enable(const char *buf, struct mca_quick_charge_info *info)
{
	char type_name[MCA_QUICK_CHG_NAME_LEN] = { 0 };
	int mode = 0;
	int value;
	int i;

	if (sscanf(buf, "%s %d", type_name, &value) != 2) {
#line 5229
		mca_log_err("unable to parse input:%s\n", buf);
		return;
	}

	if (strcmp(type_name, "all") == 0) {
#line 5234
		mca_log_info("set all mode enable %d\n", value);
		for (i = 0; i < CHG_MODE_MAX; i++)
			info->sysfs_data.mode_enable[i] = value;

		if (!value) {
			info->cur_support_mode = 0;
			if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING)
				mca_quick_charge_force_stop_charging(info);
		} else {
			info->cur_support_mode = info->support_mode;
		}

		return;
	}

	if (strcmp(type_name, "div4") == 0)
		mode = CHG_MODE_DIV4;
	else if (strcmp(type_name, "div2") == 0)
		mode = CHG_MODE_DIV2;
	else if (strcmp(type_name, "div1") == 0)
		mode = CHG_MODE_DIV1;
	else
		return;

	mca_quick_charge_set_single_mode_chg_enable(mode, value, info);
}

static void mca_quick_charge_set_cp_path_enable(const char *buf,
	struct mca_quick_charge_info *info)
{
	char path_name[MCA_QUICK_CHG_NAME_LEN] = { 0 };
	int mode;
	int cp_role;
	int value;

	while (*buf) {
		if (*buf == ' ') {
			buf++;
			continue;
		}
		if (sscanf(buf, "div%d_%s %d", &mode, path_name, &value) != 3)
			return;

		switch (mode) {
		case MCA_QUICK_CHG_MODE_DIV_4:
			mode = CHG_MODE_DIV4;
			break;
		case MCA_QUICK_CHG_MODE_DIV_2:
			mode = CHG_MODE_DIV2;
			break;
		case MCA_QUICK_CHG_MODE_DIV_1:
			mode = CHG_MODE_DIV1;
			break;
		default:
			return;
		}

		if (strcmp(path_name, "master") == 0) {
			cp_role = CP_ROLE_MASTER;
			buf += strlen("div1_master 1");
		} else if (strcmp(path_name, "slave") == 0) {
			cp_role = CP_ROLE_SLAVE;
			buf += strlen("div1_slave 1");
		} else {
			return;
		}

		info->sysfs_data.cp_path_enable[mode][cp_role] = value;
	}
}

static ssize_t mca_quick_charge_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_quick_charge_info *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		mca_quick_charge_sysfs_field_tbl, MCA_QUICK_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_QUICK_CHG_MODE_ENABLE:
		mca_quick_charge_set_chg_enable(buf, info);
		break;
	case MCA_QUICK_CHG_CP_PATH_ENABLE:
		mca_quick_charge_set_cp_path_enable(buf, info);
		break;
	case MCA_QUICK_CHG_FAKE_PPS_PTF:
		(void)sscanf(buf, "%d", &info->fake_pps_ptf);
		info->pps_ptf = info->fake_pps_ptf;
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_PPS_PTF, info->pps_ptf);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_PPS_PTF, info->pps_ptf);
		break;
	case MCA_QUICK_CHG_CURR_LIMIT:
		break;
	case MCA_QUICK_CHG_CURR_RATIO:
		(void)sscanf(buf, "%d", &info->sysfs_data.cur_ratio);
		break;
	case MCA_QUICK_CHG_VOLT_DEC:
		(void)sscanf(buf, "%d", &info->sysfs_data.volt_dec);
		break;
	default:
		break;
	}

	return count;
}

static int mca_quick_charge_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(mca_quick_charge_sysfs_attrs, mca_quick_charge_sysfs_field_tbl,
		MCA_QUICK_CHG_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "quick_charge",
		dev, &mca_quick_charge_sysfs_attr_group);
}

static void mca_quick_charge_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "quick_charge",
		dev, &mca_quick_charge_sysfs_attr_group);
}

#else

static inline int mca_quick_charge_create_group(struct device *dev)
{
}

static void mca_quick_charge_remove_group(struct device *dev)
{
}

#endif /* CONFIG_SYSFS */

/* G13n: stock mca_quick_charge_shutdown_for_hw_action (inlined into shutdown) */
static __always_inline void mca_quick_charge_shutdown_for_hw_action(struct mca_quick_charge_info *info)
{
	platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
	platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
	platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
	platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
	if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
		platform_class_cp_set_default_fsw(CP_ROLE_MASTER);
		platform_class_cp_set_default_fsw(CP_ROLE_SLAVE);
	} else {
		platform_class_cp_set_default_fsw(info->proc_data.cur_work_cp);
	}
	info->pd_switch_to_pmic = true;
	info->proc_data.cur_adp_volt = 5000;
	info->shutdown_mode = 1;
	if (mca_quick_charge_req_adp_volt_and_cur(info)) {
#line 5408
		mca_log_err("stop req volt failed\n");
	}
	if (info->en_buck_parallel_chg)
		strategy_quickchg_enable_buck_charging(info, 0, 0, false);
	if (info->proc_data.ffc_flag)
		strategy_class_fg_set_fastcharge(false);
}

/* G13n: rebuilt from the stock disassembly */
static int mca_quick_charge_shutdown(struct notifier_block *nb, unsigned long action,
	void *data)
{
	struct mca_quick_charge_info *info = container_of(nb, struct mca_quick_charge_info, shutdown_notifier);
	struct mca_event_notify_data n_data = { 0 };

#line 5423
	mca_log_err("receive shutdown\n");

	mca_quick_charge_shutdown_for_hw_action(info);

	n_data.event = "MCA_LOG_FULL_EVENT";
	n_data.event_len = 18;
	mca_event_report_uevent(&n_data);
	msleep(20); // wait log dump

#line 5434
	mca_log_err("end shutdown\n");
	return 0;
}

static int mca_quick_charge_div1_single_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5446
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV1][MCA_QUICK_CHG_CH_SINGLE] = effective_result;
	return 0;
}

static int mca_quick_charge_div1_multi_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5459
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV1][MCA_QUICK_CHG_CH_MULTI] = effective_result;
	return 0;
}

static int mca_quick_charge_div2_single_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5472
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE] = effective_result;
	return 0;
}

static int mca_quick_charge_div2_multi_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5485
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_MULTI] = effective_result;
	return 0;
}

static int mca_quick_charge_div4_single_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5498
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV4][MCA_QUICK_CHG_CH_SINGLE] = effective_result;
	return 0;
}

static int mca_quick_charge_div4_multi_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5511
	mca_log_info("target thermal effective_result :%d\n", effective_result);
	info->thermal_cur[CHG_MODE_DIV4][MCA_QUICK_CHG_CH_MULTI] = effective_result;
	return 0;
}

static int mca_quick_charge_chg_disable_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

	info->force_stop = effective_result;
#line 5538
	mca_log_info("force_stop: %d\n", info->force_stop);

	if (info->force_stop)
		mca_quick_charge_force_stop_charging(info);

	return 0;
}

static int mca_quick_charge_chg_en_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

	info->sysfs_data.chg_enable = effective_result;
	if (!info->sysfs_data.chg_enable)
		mca_quick_charge_force_stop_charging(info);

	return 0;
}

static int mca_quick_charge_single_chg_cur_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;
	int i;

	if (!data)
		return -1;

	info->sysfs_data.chg_limit[MCA_QUICK_CHG_CH_SINGLE] = effective_result;
	for (i = 0; i < MCA_QUICK_CHG_CH_MAX * CHG_MODE_MAX; i = i + 2) {
		if (effective_result)
			(void)mca_vote(info->voter[i], "if_ops", true, effective_result);
		else
			(void)mca_vote(info->voter[i], "if_ops", false, 0);
	}

	return 0;
}

static int mca_quick_charge_multi_chg_cur_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;
	int i;

	if (!data)
		return -1;

	info->sysfs_data.chg_limit[MCA_QUICK_CHG_CH_MULTI] = effective_result;
	for (i = 1; i < MCA_QUICK_CHG_CH_MAX * CHG_MODE_MAX; i = i + 2) {
		if (effective_result)
			(void)mca_vote(info->voter[i], "if_ops", true, effective_result);
		else
			(void)mca_vote(info->voter[i], "if_ops", false, 0);
	}

	return 0;
}

static int mca_quick_charge_create_voter(struct mca_quick_charge_info *info)
{
	struct mca_votable *smartchg_ichg_voter = NULL;

	info->voter[0] = mca_create_votable("div1_single", MCA_VOTE_MIN,
		 mca_quick_charge_div1_single_voter_cb, 0, info);
	if (IS_ERR(info->voter[0]))
		goto error;
	info->voter[1] = mca_create_votable("div1_multi", MCA_VOTE_MIN,
		 mca_quick_charge_div1_multi_voter_cb, 0, info);
	if (IS_ERR(info->voter[1]))
		goto error;
	info->voter[2] = mca_create_votable("div2_single", MCA_VOTE_MIN,
		 mca_quick_charge_div2_single_voter_cb, 0, info);
	if (IS_ERR(info->voter[2]))
		goto error;
	info->voter[3] = mca_create_votable("div2_multi", MCA_VOTE_MIN,
		 mca_quick_charge_div2_multi_voter_cb, 0, info);
	if (IS_ERR(info->voter[3]))
		goto error;
	info->voter[4] = mca_create_votable("div4_single", MCA_VOTE_MIN,
		 mca_quick_charge_div4_single_voter_cb, 0, info);
	if (IS_ERR(info->voter[4]))
		goto error;
	info->voter[5] = mca_create_votable("div4_multi", MCA_VOTE_MIN,
		 mca_quick_charge_div4_multi_voter_cb, 0, info);
	if (IS_ERR(info->voter[5]))
		goto error;
	info->thermal_flip_voter = mca_create_votable("thermal_flip", MCA_VOTE_MIN,
		 mca_quick_charge_thermal_flip_voter_cb, 0, info);
	if (IS_ERR(info->thermal_flip_voter))
		goto error;

	info->chg_disable_voter = mca_create_votable("quick_chg_disable",
		MCA_VOTE_OR, mca_quick_charge_chg_disable_voter_cb, 0, info);
	if (IS_ERR(info->chg_disable_voter))
		goto error;
	info->chg_en_voter = mca_create_votable("quick_chg_en",
		MCA_VOTE_AND, mca_quick_charge_chg_en_voter_cb, 1, info);
	if (IS_ERR(info->chg_en_voter))
		goto error;
	info->single_chg_cur_voter = mca_create_votable("single_chg_cur",
		MCA_VOTE_MIN, mca_quick_charge_single_chg_cur_voter_cb, 0, info);
	if (IS_ERR(info->single_chg_cur_voter))
		goto error;
	info->multi_chg_cur_voter = mca_create_votable("multi_chg_cur",
		MCA_VOTE_MIN, mca_quick_charge_multi_chg_cur_voter_cb, 0, info);
	if (IS_ERR(info->multi_chg_cur_voter))
		goto error;
	smartchg_ichg_voter = mca_find_votable("smartchg_delta_ichg");
	if (smartchg_ichg_voter)
		info->smartchg_data.delta_ichg = mca_get_effective_result(smartchg_ichg_voter);
#line 5657
	mca_log_info("create voter success\n");

	return 0;

error:
#line 5662
	mca_log_err("init voter failed\n");
	return -1;
}

static int strategy_quickchg_if_set_chg_cur(const char *user,
	char *value, void *data)
{
	int single_cur = 0, multi_cur = 0;
	struct mca_quick_charge_info *info = data;

	if (!user || !value || !data)
		return -1;

	if (sscanf(value, "%d#%d", &single_cur, &multi_cur) != 2)
		return -1;


	(void)mca_vote(info->single_chg_cur_voter, user, true, single_cur);
	(void)mca_vote(info->multi_chg_cur_voter, user, true, multi_cur);

	return 0;
}

static int strategy_quickchg_if_get_chg_cur(char *buf, void *data)
{
	const char *single_client;
	const char *multi_client;
	int single_cur, multi_cur;
	struct mca_quick_charge_info *info = data;

	if (!buf || !data)
		return -1;

	single_client = mca_get_effective_client(info->single_chg_cur_voter);
	if (!single_client)
		return -1;
	multi_client = mca_get_effective_client(info->multi_chg_cur_voter);
	if (!multi_client)
		return -1;

	single_cur = mca_get_effective_result(info->single_chg_cur_voter);
	multi_cur = mca_get_effective_result(info->multi_chg_cur_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF,
		"single:eff_client:%s %d multi:eff_client:%s %d",
		single_client, single_cur, multi_client, multi_cur);

	return 0;
}

static int strategy_quickchg_if_set_chg_en(const char *user,
	unsigned int value, void *data)
{
	struct mca_quick_charge_info *info = data;

	if (!user || !data)
		return -1;

	if (value)
		(void)mca_vote(info->chg_en_voter, user, true, 1);
	else
		(void)mca_vote(info->chg_en_voter, user, true, 0);

	return 0;
}

static int strategy_quickchg_if_get_chg_en(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct mca_quick_charge_info *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->chg_en_voter);
	if (!client_str)
		return -1;
	value = (unsigned int)mca_get_effective_result(info->chg_en_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_quickchg_if_set_input_suspend(const char *user,
	char *value, void *data)
{
	int temp_value = 0;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

#line 5760
	mca_log_err("set_input_suspend: %d\n", temp_value);

	(void)strategy_quickchg_if_set_chg_en(user, !temp_value, data);

	return 0;
}

static int strategy_quickchg_if_get_input_suspend(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct mca_quick_charge_info *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->chg_en_voter);
	if (!client_str)
		return -1;
	value = (unsigned int)mca_get_effective_result(info->chg_en_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, !value);

	return 0;
}

static void strategy_quickchg_map_ibus_to_fsw(struct mca_quick_charge_info *info, int *fsw_target)
{
	int ibus_avg = info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL ?
		info->proc_data.ibus_queue.avg / 2 : info->proc_data.ibus_queue.avg;
	int cp_iout = ibus_avg * info->proc_data.ratio;
	int i = 0;

	*fsw_target = MCA_QUICK_CHG_CP_DEFAULT_FSW;
	for (i = 0; i < info->vfc_para.vfc_para_size; i++) {
		if (cp_iout > info->vfc_para.iout_fsw_map[i].iout) {
			*fsw_target = info->vfc_para.iout_fsw_map[i].fsw;
			break;
		}
	}
	info->proc_data.vfc_iout = cp_iout;
#line 5807
	mca_log_info("ibus_avg: %d, ratio: %d, cp_iout: %d, map_iout_fsw: [%d:%d], fsw_target: %d\n",
		ibus_avg, info->proc_data.ratio, cp_iout,
		info->vfc_para.iout_fsw_map[i].iout, info->vfc_para.iout_fsw_map[i].fsw, *fsw_target);
}

static int strategy_quickchg_update_cp_fsw(struct mca_quick_charge_info *info, int fsw_target)
{
	int ret = 0;
	int cur_fsw = 0;
	int fsw_tmp = 0;
	int fsw_step = 600;
	int step = 0;
	int direction = 0;

	platform_class_cp_get_fsw_step(CP_ROLE_MASTER, &fsw_step);
	if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL)
		platform_class_cp_get_fsw(CP_ROLE_MASTER, &cur_fsw);
	else
		platform_class_cp_get_fsw(info->proc_data.cur_work_cp, &cur_fsw);

#line 5827
	mca_log_info("fsw_target: %d, cur_fsw: %d, fsw_step: %d\n",
		fsw_target, cur_fsw, fsw_step);
	if (fsw_target != cur_fsw) {
		direction = fsw_target - cur_fsw >= 0 ? 1 : -1;
		step = abs(fsw_target - cur_fsw) / fsw_step;
		for (int count = 1; count < step + 1; count++) {
			fsw_tmp = cur_fsw + fsw_step * direction * count;
			if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
				platform_class_cp_set_fsw(CP_ROLE_MASTER, fsw_tmp);
				platform_class_cp_set_fsw(CP_ROLE_SLAVE, fsw_tmp);
			} else {
				platform_class_cp_set_fsw(info->proc_data.cur_work_cp, fsw_tmp);
			}
		}

		if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL)
			platform_class_cp_get_fsw(CP_ROLE_MASTER, &cur_fsw);
		else
			platform_class_cp_get_fsw(info->proc_data.cur_work_cp, &cur_fsw);
#line 5846
		mca_log_info("after: cur_fsw: %d, direction: %d\n", cur_fsw, direction);
	}

	return ret;
}

static struct mca_charge_if_ops g_strategy_quickchg_if_ops = {
	.type_name = "quick",
	.set_charge_enable = strategy_quickchg_if_set_chg_en,
	.get_charge_enable = strategy_quickchg_if_get_chg_en,
	.set_input_suspend = strategy_quickchg_if_set_input_suspend,
	.get_input_suspend = strategy_quickchg_if_get_input_suspend,
	.set_charge_current_limit = strategy_quickchg_if_set_chg_cur,
	.get_charge_current_limit = strategy_quickchg_if_get_chg_cur,
};

static int strategy_quickchg_smartchg_delta_fv_callback(void *data, int effective_result)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;

	if (!data)
		return -1;

	mca_log_err("effective_result: %d\n", effective_result);
	info->smartchg_data.delta_fv = effective_result;
	return 0;
}

static int strategy_quickchg_smartchg_delta_ichg_callback(void *data, int effective_result)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;

	if (!data)
		return -1;

#line 5888
	mca_log_err("effective_result: %d\n", effective_result);
	info->smartchg_data.delta_ichg = effective_result;
	return 0;
}

static int strategy_quickchg_smartchg_soc_limit_callback(void *data, int effective_result)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;

	if (!data)
		return -1;

#line 5915
	mca_log_err("effective_result: %d\n", effective_result);
	mca_vote(info->chg_disable_voter, "soc_limit", true, effective_result);
	return 0;
}

static int strategy_quickchg_smartchg_baa_update_volt_info(
	struct mca_quick_charge_temp_para_info *qc_temp_para, struct smart_batt_spec *pbatt_spec)
{
	struct mca_quick_charge_volt_para_info *volt_info = NULL;
	struct mca_quick_charge_volt_para *old_volt_para = NULL, *new_volt_para = NULL;

	if (pbatt_spec->ffc)
		volt_info = &qc_temp_para->volt_ffc_info;
	else
		volt_info = &qc_temp_para->volt_info;

#line 5931
	mca_log_info("wired para type: %d, ffc: %d, idx: %d, size: %d => %d, temp: %d:%d => %d:%d\n",
		pbatt_spec->type, pbatt_spec->ffc, pbatt_spec->t_range.idx,
		volt_info->volt_para_size, pbatt_spec->step_size,
		qc_temp_para->temp_para.temp_low, qc_temp_para->temp_para.temp_high,
		pbatt_spec->t_range.min, pbatt_spec->t_range.max);

	new_volt_para = kcalloc(pbatt_spec->step_size, sizeof(*new_volt_para), GFP_KERNEL);
	if (!new_volt_para) {
#line 5939
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}

	for (int i = 0; i < volt_info->volt_para_size; i++) {
#line 5944
		mca_log_debug("volt_para[%d] voltage: %d, current_max: %d, current_min: %d\n",
			i, volt_info->volt_para[i].voltage,
			volt_info->volt_para[i].current_max, volt_info->volt_para[i].current_min);
	}

	for (int i = 0; i < pbatt_spec->step_size; i++) {
		new_volt_para[i].voltage = pbatt_spec->steps[i].mv;
		new_volt_para[i].current_max = pbatt_spec->steps[i].ma_h;
		new_volt_para[i].current_min = pbatt_spec->steps[i].ma_l;
#line 5953
		mca_log_info("new_volt_para[%d] voltage: %d, current_max: %d, current_min: %d\n",
			i, new_volt_para[i].voltage, new_volt_para[i].current_max, new_volt_para[i].current_min);
	}

	old_volt_para = volt_info->volt_para;
	volt_info->volt_para = new_volt_para;
	volt_info->volt_para_size = pbatt_spec->step_size;
	kfree(old_volt_para);

	return 0;
}

#define MCA_QUICK_CHARGE_MAX_VOLT_STEP_SIZE 8
static int strategy_quickchg_smartchg_update_baa_para(void *data, char *baa_para, int ffc_size, int normal_size)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;
	struct smart_batt_spec *pbatt_spec = NULL;
	struct smart_batt_spec_curve curve[MCA_QUICK_CHARGE_MAX_VOLT_STEP_SIZE] = {0};
	int offset = 0;

	if (!data || !baa_para) {
#line 5974
		mca_log_err("data or baa_para is NULL\n");
		return -1;
	}

	for (int i = 0; i < ffc_size + normal_size; i++) {
		pbatt_spec = (struct smart_batt_spec *)(baa_para + offset);
		offset += offsetof(struct smart_batt_spec, steps);

		memset(curve, 0, sizeof(curve));
		memcpy(curve, baa_para + offset, pbatt_spec->step_size * sizeof(struct smart_batt_spec_curve));
		pbatt_spec->steps = curve;
		offset += pbatt_spec->step_size * sizeof(struct smart_batt_spec_curve);

		if (pbatt_spec->t_range.idx < 0 || pbatt_spec->t_range.idx >= info->batt_para[FG_IC_MASTER].temp_para_size) {
#line 5988
			mca_log_err("temp_para_size: %d, temp_range.idx: %d is invalid\n",
				pbatt_spec->t_range.idx >= info->batt_para[FG_IC_MASTER].temp_para_size, pbatt_spec->t_range.idx);
		} else {
			strategy_quickchg_smartchg_baa_update_volt_info(
				&(info->batt_para[FG_IC_MASTER].temp_info[pbatt_spec->t_range.idx]), pbatt_spec);
		}
	}

	return 0;
}

static int strategy_quickchg_smartchg_pwr_boost_sts_callback(void *data, int enable)
{
	struct mca_quick_charge_info *info = (struct mca_quick_charge_info *)data;

	if (!data)
		return -1;

	info->smartchg_data.pwr_boost_state = enable;

	return 0;
}

static int strategy_quickchg_smartchg_set_fcc_callback(void *data, int fcc)
{
	struct mca_quick_charge_info *info = data;

	if (!info)
		return -1;
	info->bypass_curr = fcc;
	return 0;
}

static struct mca_smartchg_if_ops g_quickchg_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_QC,
	.data = NULL,
	.set_delta_fv = strategy_quickchg_smartchg_delta_fv_callback,
	.set_delta_ichg = strategy_quickchg_smartchg_delta_ichg_callback,
	.set_fcc = strategy_quickchg_smartchg_set_fcc_callback,
	.set_soc_limit_sts = strategy_quickchg_smartchg_soc_limit_callback,
	.update_baa_para = strategy_quickchg_smartchg_update_baa_para,
	.set_pwr_boost_sts = strategy_quickchg_smartchg_pwr_boost_sts_callback,
};

static int mca_quick_charge_thermal_flip_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_quick_charge_info *info = data;

	if (!data)
		return -1;

#line 5524
	mca_log_info("target thermal flip effective_result :%d\n", effective_result);
	info->thermal_flip_cur = effective_result;
	return 0;
}

static void mca_quick_charge_delay_enable_pmic_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info = container_of(work,
		struct mca_quick_charge_info, delay_enable_pmic_work.work);
	int ffc = strategy_class_fg_get_fastcharge();

#line 3630
	mca_log_err("30s after cp quick charger done, rerun enable pmic\n");
	if (ffc || info->switch_normal) {
		info->switch_normal = false;
		info->proc_data.drop_float_volt_done = true;
	}
	strategy_class_fg_set_fastcharge(false);
	mca_quick_charge_msleep(2500, info);
	mca_vote(info->buck_charge_enable_voter, "cp_chg_done", false, 0);
	mca_vote(info->buck_input_voter, "cp_chg_done", false, 0);
}

static void mca_quick_charge_ovp_monitor_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info = container_of(work,
		struct mca_quick_charge_info, ovp_monitor_work.work);
	struct mca_quick_charge_process_data *proc = &info->proc_data;
	struct mca_quick_charge_volt_para_info *vp;
	int vfloat, vbat, cycle, ret;

	if (proc->exit_cp_charging)
		return;

	vp = proc->cur_volt_para[FG_IC_MASTER];
	vfloat = vp->volt_para[vp->volt_para_size - 1].voltage + info->drop_fv_value;
	if (vfloat < proc->last_vfloat)
		proc->ignore_ov_flag = true;
	proc->last_vfloat = vfloat;
	ret = strategy_class_fg_ops_get_voltage(&proc->vbat[FG_IC_MASTER]);
	if (!ret && info->batt_type >= 1) {
		if (!platform_fg_ops_get_volt(FG_IC_MASTER, &proc->parall_vbat[FG_IC_BASE]))
			platform_fg_ops_get_volt(FG_IC_SLAVE, &proc->parall_vbat[FG_IC_FLIP]);
	}
	vbat = proc->vbat[FG_IC_MASTER];
	if (vbat <= vfloat)
		proc->ignore_ov_flag = false;
#line 3819
	mca_log_info("ovp_check: vfloat = %d, vbat = %d, cyclecount = %d, ignore_ov_flag = %d\n",
		vfloat, vbat, info->cyclecount, proc->ignore_ov_flag);
	cycle = info->cyclecount;
	if (vbat > vfloat && cycle >= 801) {
		if (!proc->ignore_ov_flag) {
			proc->ban_cp_charging = true;
			cancel_delayed_work_sync(&info->monitor_work);
			proc->exit_cp_charging = true;
			goto stop;
		}
	} else if (vbat > vfloat && cycle <= 800) {
		if (!proc->ignore_ov_flag) {
			proc->try_en_cp_count++;
			cancel_delayed_work_sync(&info->monitor_work);
			proc->exit_cp_charging = true;
			goto stop;
		}
	}
	schedule_delayed_work(&info->ovp_monitor_work, msecs_to_jiffies(1000));
	return;
stop:
	mca_quick_charge_stop_charging(info);
	if (info->support_pmic_vterm_dynamics_adjust) {
		mca_vote_override(info->buck_vterm_voter, "pmic_temp_term", false, 0);
		mca_vote_override(info->buck_input_voter, "pmic_temp_term", false, 0);
	}
}

static int mca_quick_charge_probe(struct platform_device *pdev)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct mca_quick_charge_info *info;
	int ret, i, j, cp_pending;

#line 6029
	mca_log_info("probe begin\n");

	if (strategy_class_fg_ops_is_init_ok() <= 0) {
#line 6032
		mca_log_info("fg is not ready, wait for it\n");
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_class_cp_get_chip_vendor(CP_ROLE_MASTER, &info->cp_vendor);
	if (!info->cp_vendor) {
		cp_pending = platform_class_cp_get_probe_ok(CP_ROLE_MASTER);
		if (info->cp_type == MCA_CP_TYPE_PARALLEL)
			cp_pending |= platform_class_cp_get_probe_ok(CP_ROLE_SLAVE);
		if (cp_pending) {
#line 6048
			mca_log_err("cp is not ready, wait for it\n");
			return -EPROBE_DEFER;
		}
	}
	platform_class_buckchg_ops_set_cp_vendor_id(MAIN_BUCK_CHARGER, &info->cp_vendor);

	info->input_suspend_voter = mca_find_votable("input_suspend");
	if (!info->input_suspend_voter) {
#line 6056
		mca_log_info("get voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->buck_input_voter = mca_find_votable("buck_input");
	if (!info->buck_input_voter) {
#line 6061
		mca_log_info("get buck_input voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->buck_charge_curr_voter = mca_find_votable("buck_charge_curr");
	if (!info->buck_charge_curr_voter) {
#line 6066
		mca_log_info("get buck_charge_curr voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->buck_vterm_voter = mca_find_votable("term_volt");
	if (!info->buck_vterm_voter) {
#line 6071
		mca_log_info("get buck_charge_vterm voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->vterm_voter = mca_find_votable("term_volt");
	if (!info->vterm_voter) {
#line 6076
		mca_log_info("get vterm_voter voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->flip_fcc_voter = mca_find_votable("flip_charge_curr");
	if (!info->flip_fcc_voter) {
#line 6081
		mca_log_info("get flip_fcc_voter voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}
	info->buck_charge_enable_voter = mca_find_votable("chg_enable");
	if (!info->buck_charge_enable_voter) {
#line 6086
		mca_log_info("get buck_charge_enable voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	ret = mca_quick_charge_parse_dt(info);
	if (ret) {
#line 6094
		mca_log_err("parst dts failed, ret: %d\n", ret);
		if (ret != -1)
			ret = -EPROBE_DEFER;
		return ret;
	}
	if (hwid && (!strcmp(hwid->product_name, "donghai") || !strcmp(hwid->product_name, "yanan")))
		mca_quick_charge_parse_dt_override(info);
	ret = mca_quick_charge_create_voter(info);
	if (ret)
		return -EPROBE_DEFER;

	info->batt_missing = 1;
	info->cur_support_mode = info->support_mode;
	info->sysfs_data.chg_enable = 1;
	for (i = 0; i < CHG_MODE_MAX; i++) {
		info->sysfs_data.mode_enable[i] = 1;
		for (j = 0; j < CP_ROLE_MAX; j++)
			info->sysfs_data.cp_path_enable[i][j] = 1;
	}
	mca_quick_charge_create_group(info->dev);
	mutex_init(&info->data_lock);

	INIT_DELAYED_WORK(&info->monitor_work, mca_quick_charge_monitor_work);
	INIT_DELAYED_WORK(&info->pps_ptf_work, mca_quick_charge_pps_ptf_work);
	INIT_DELAYED_WORK(&info->vfc_work, mca_quick_charge_vfc_work);
	INIT_DELAYED_WORK(&info->delay_enable_pmic_work, mca_quick_charge_delay_enable_pmic_work);
	INIT_DELAYED_WORK(&info->ovp_monitor_work, mca_quick_charge_ovp_monitor_work);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
		mca_quick_charge_process_event, mca_quick_charge_get_status, NULL, info);
	g_strategy_quickchg_if_ops.data = info;
	mca_charge_if_ops_register(&g_strategy_quickchg_if_ops);
	g_quickchg_smartchg_if_ops.data = info;
	mca_smartchg_if_ops_register(&g_quickchg_smartchg_if_ops);
	info->shutdown_notifier.notifier_call = mca_quick_charge_shutdown;
	info->shutdown_notifier.priority = 255;
	ret = register_reboot_notifier(&info->shutdown_notifier);
	if (ret < 0)
#line 6127
		mca_log_err("register reboot notify failed\n");
#line 6128
	mca_log_err("probe end\n");

	return 0;
}

static int mca_quick_charge_remove(struct platform_device *pdev)
{
	mca_quick_charge_remove_group(&pdev->dev);
	return 0;
}

static const struct of_device_id mca_quick_charge_info_of_match[] = {
	{ .compatible = "mca,quick_charger", },
	{},
};

static struct platform_driver mca_quick_charge_info_driver = {
	.driver = {
		.name = "strtategy-quick-charge",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(mca_quick_charge_info_of_match),
	},
	.probe = mca_quick_charge_probe,
	.remove = mca_quick_charge_remove,
};

static int __init mca_quick_charge_info_init(void)
{
	return platform_driver_register(&mca_quick_charge_info_driver);
}

late_initcall(mca_quick_charge_info_init);

static void __exit mca_quick_charge_info_exit(void)
{
	return platform_driver_unregister(&mca_quick_charge_info_driver);
}
module_exit(mca_quick_charge_info_exit);

MODULE_DESCRIPTION("strategy quick charger");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_AUTHOR("lvchen@xiaomi.com");

