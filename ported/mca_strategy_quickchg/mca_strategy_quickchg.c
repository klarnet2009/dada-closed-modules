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

static void strategy_quickchg_map_ibus_to_fsw(struct mca_quick_charge_info *info, int *fsw_target);
static int strategy_quickchg_update_cp_fsw(struct mca_quick_charge_info *info, int fsw_target);
static void strategy_quickchg_enable_buck_charging(struct mca_quick_charge_info *info,
													int buck_icl_val, int buck_fcc_val, bool enable);

static int mca_quick_charge_msleep(int ms, struct mca_quick_charge_info *info)
{
	int i, count;
	int buck_online = 1;

	count = ms / 10;

	for (i = 0; i < count; i++) {
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_ONLINE, &buck_online);
		if (!info->online || info->force_stop || info->proc_data.type_chg || !buck_online) {
			mca_log_err("sleep fast return, online: %d, force_stop: %d, type_chg: %d, buck_online: %d\n",
				info->online, info->force_stop, info->proc_data.type_chg, buck_online);
			return -1;
		}
		usleep_range(9900, 11000);
	}

	return 0;
}

static int mca_quick_charge_update_vbat(struct mca_quick_charge_info *info)
{
	int ret;

	ret = strategy_class_fg_ops_get_voltage(&info->proc_data.vbat[FG_IC_MASTER]);
	ret |= platform_fg_ops_get_max_cell_volt(FG_IC_MASTER, &info->proc_data.max_vcell[FG_IC_MASTER]);
	if (ret)
		return -1;

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE_NUM_MAX) {
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

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE_NUM_MAX) {
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
static bool mca_qc_check_if_goto_taper(struct mca_quick_charge_info *info)
{
	int taper_vol_hys_mv, ibat_now_ma;
	int vbat_th;
	int delta_fv = info->smartchg_data.delta_fv;

	taper_vol_hys_mv = (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3P5) ?
		info->qc3p5_taper_vol_hys : info->qc3_taper_vol_hys;

	/* now only support single 1S battery, 1S2P battery to do*/
	info->qc_normal_charge_fv_max_mv = info->proc_data.temp_max_fv[FG_IC_MASTER];
	vbat_th = info->qc_normal_charge_fv_max_mv - delta_fv - taper_vol_hys_mv - MCA_QUICK_CHG_FV_HYS;
#line 267
	mca_log_info("qc_normal_charge_fv_max_mv: %d, vbat_th:%d\n",
		info->qc_normal_charge_fv_max_mv, vbat_th);
	ibat_now_ma = info->proc_data.ibat_total;

	if (info->proc_data.vbat[FG_IC_MASTER] > vbat_th && ibat_now_ma < info->qc_taper_fcc_ma) {
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

static bool mca_check_if_goto_hw_taper(struct mca_quick_charge_info *info)
{
	int vbat_now_mv, ibat_now_ma;
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].voltage;
	int ibat_th = info->pps_taper_fcc_ma;
	int delta_fv = info->smartchg_data.delta_fv;
	int temp = 0;

	if (!info->hardware_cv)
		return false;

	if (info->support_base_flip) {
		strategy_class_fg_ops_get_temperature(&temp);
		temp = temp / 10;
		if (temp > 40)
			ibat_th = info->pps_high_taper_fcc_ma;
	}
	vbat_now_mv = info->proc_data.vbat[FG_IC_MASTER];
	ibat_now_ma = info->proc_data.ibat_total;

	/* last quick charge stage */
	if ((cur_stage / 2) == (volt_para_size - 1)) {
		vbat_th = vbat_th - delta_fv - info->pps_taper_vol_hys - MCA_QUICK_CHG_FV_HYS;
		mca_log_info("check if goto hw taper vbat: %d, vbat_th: %d, ibat: %d, ibat_th: %d, count: %d\n",
			vbat_now_mv, vbat_th, ibat_now_ma, ibat_th, info->fc2_taper_timer);

		if (vbat_now_mv >= vbat_th && ibat_now_ma < ibat_th) {
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

static int mca_eu_pps_limit_power(struct mca_quick_charge_info *info, int target_cur)
{
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = 0;
	int battmax_cur = 0;
	int adptermax_cur = 0;
	int eu_pps_max_curr_c = 0;
	int eu_pps_max_curr_a = 0;
	static bool two_min_valid = false;
	static bool time_flag;
	ktime_t time_now_ms = ktime_get();
	static ktime_t time_last_ms;
	static ktime_t time_gap;

	if (info->proc_data.charge_flag != MCA_QUICK_CHG_STS_CHARGING) {
		time_last_ms = time_now_ms;
		time_flag = false;
		two_min_valid = false;
		time_gap = 0;
		mca_log_info("quit eu PPS PTF,charge_flag= %d\n", info->proc_data.charge_flag);
		return 0;
	}

	cur_stage = proc_data->cur_stage[FG_IC_MASTER];
	battmax_cur = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_max;
	adptermax_cur = proc_data->max_adp_curr * proc_data->ratio;
	eu_pps_max_curr_c = min(battmax_cur, adptermax_cur);
	eu_pps_max_curr_a = max(eu_pps_max_curr_c * 2 / 3, MCA_PPS_FCC_LIMIT);

	if (two_min_valid == true && target_cur > eu_pps_max_curr_a) {
		return eu_pps_max_curr_a;
	} else if (two_min_valid == true && target_cur <= eu_pps_max_curr_a) {
		return target_cur;
	}

	mca_log_info("eu PPS PTF,time_now_ms= %ld,time_last_ms= %ld,time_gap= %ld,eu_pps_max_curr_c= %d,target_cur= %d,pps_ptf= %d\n",
		time_now_ms, time_last_ms, time_gap, eu_pps_max_curr_c, target_cur, info->pps_ptf);

	if (target_cur >= eu_pps_max_curr_c && time_flag == false) {
		time_flag = true;
		time_last_ms = time_now_ms;
	} else if (time_flag == true && target_cur < eu_pps_max_curr_c) {
		time_last_ms = time_now_ms;
		time_flag = false;
		time_gap = 0;
	}
	time_gap = ktime_to_ms(ktime_sub(time_now_ms, time_last_ms));
	time_gap /= 1000;

	if (time_flag == true && target_cur > eu_pps_max_curr_a && time_gap > MCA_PPS_MAXFCC_PEAK_TIME_S) {
		target_cur = eu_pps_max_curr_a;
		two_min_valid = true;
		mca_log_info("2/3 max power when eu PPS hold maxpower 2min,curr= %d\n", target_cur);
	}

	return target_cur;
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
	int pd_request_volt;

#line 448
	mca_log_info("req adp volt=%d, cur=%d\n",
		info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);

	if (info->proc_data.cur_adp_cur > info->proc_data.max_adp_curr)
		info->proc_data.cur_adp_cur = info->proc_data.max_adp_curr;

	if (info->proc_data.cur_adp_volt > info->proc_data.max_adp_volt)
		info->proc_data.cur_adp_volt = info->proc_data.max_adp_volt;

	if (info->proc_data.cur_adp_volt < info->proc_data.min_adp_volt) {
#line 472
		mca_log_err("volt req too low, return err\n");
		return -1;
	}

	switch (info->proc_data.adp_type) {
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (!info->pd_switch_to_pmic) {
			protocol_class_set_adapter_volt_and_curr(info->proc_data.cur_protocol,
				info->proc_data.cur_adp_volt, info->proc_data.cur_adp_cur);
		} else {
			if (stategy_quickchg_is_pdo_9v(info) && !info->trigger_antr_burn)
				pd_request_volt = MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT;
			else
				pd_request_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
			(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0,
				pd_request_volt);
#line 488
			mca_log_err("PD2.0 REQUEST VOLT = %d\n", pd_request_volt);
		}
		break;
	case XM_CHARGER_TYPE_PD:
		if (stategy_quickchg_is_pdo_9v(info) && !info->trigger_antr_burn)
			pd_request_volt = MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT;
		else
			pd_request_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
		(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0,
			pd_request_volt);
#line 497
		mca_log_err("PD2.0 REQUEST VOLT = %d\n", pd_request_volt);
		break;
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		protocol_class_qc_set_volt(TYPEC_PORT_0, info->proc_data.cur_adp_volt);
		break;
	default:
		break;
	}

	return 0;
}

static void mca_quick_charge_reset_charge_para(struct mca_quick_charge_info *info)
{
	info->online = 0;
	info->is_platform_qc = false;
	info->taper_done_no_retry = false;
	info->pd_switch_to_pmic = false;
	info->fc2_taper_timer = 0;
	info->proc_data.sw_ocp_curr = 0;
	mca_vote(info->chg_en_voter, "vbat_ovp", false, 1);
	mca_vote(info->chg_en_voter, "lpd", false, 0);
	cancel_delayed_work_sync(&info->float_vbat_drop_work);
}

static void mca_quick_charge_stop_charging(struct mca_quick_charge_info *info)
{
	int ret;
	int vbus_mv = 0;
	int check_count = 0;

#line 531
	mca_log_err("stop charge\n");

	ret = platform_class_cp_check_iic_check(info->proc_data.cur_work_cp);
	if (ret < 0) {
		info->proc_data.cp_iic_ok = false;
	} else
		info->proc_data.cp_iic_ok = true;

	if (info->proc_data.cp_iic_ok) {
		protocol_class_pd_reset_pps_stage(TYPEC_PORT_0, true);
		platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
		platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
	}
	cancel_delayed_work_sync(&info->vfc_work);
	if (info->proc_data.cp_iic_ok) {
		if (info->proc_data.cur_work_cp == MCA_QUICK_CHG_CP_DUAL) {
			platform_class_cp_set_default_fsw(CP_ROLE_MASTER);
			platform_class_cp_set_default_fsw(CP_ROLE_SLAVE);
		} else
			platform_class_cp_set_default_fsw(info->proc_data.cur_work_cp);
	}

	memset(info->proc_data.ibus_queue.data, 0, sizeof(info->proc_data.ibus_queue.data));
	info->proc_data.ibus_queue.index = 0;
	info->proc_data.ibus_queue.count = 0;
	info->proc_data.ibus_queue.sum = 0;
	info->proc_data.ibus_queue.avg = 0;
	info->check_vbat_ov = false;
	info->proc_data.stop_charging = false;

	if (info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3_B ||
		info->proc_data.adp_type == XM_CHARGER_TYPE_HVDCP3P5)
		info->proc_data.cur_adp_volt = MCA_QUICK_QC_CHG_ADP_DEFAULT_VOLT;
	else
		info->proc_data.cur_adp_volt = MCA_QUICK_PD_FIXED_CHG_ADP_DEFAULT_VOLT;
	if (info->trigger_antr_burn)
		info->proc_data.cur_adp_volt = MCA_QUICK_CHG_ADP_DEFAULT_VOLT;
	info->proc_data.cur_adp_cur = MCA_QUICK_CHG_ADP_DEFAULT_CURR;
	info->pd_switch_to_pmic = true;
	while (check_count++ < 10 && info->proc_data.cp_iic_ok) {
		ret = mca_quick_charge_req_adp_volt_and_cur(info);
		platform_class_cp_get_bus_voltage(CP_ROLE_MASTER, &vbus_mv);
#line 584
		mca_log_info("avoid vusb_ovp. cp_vbus: %d\n", vbus_mv);
		if (vbus_mv < MCA_QUICK_CHG_VBUS_OK_HIGH_TH)
			break;

		if (ret)
#line 589
			mca_log_err("stop req volt failed\n");
		msleep(300);
	}

	if (info->proc_data.cp_iic_ok) {
		platform_class_cp_set_default_mode(CP_ROLE_MASTER);
		if (info->cp_type == MCA_CP_TYPE_PARALLEL)
			platform_class_cp_set_default_mode(CP_ROLE_SLAVE);
	}
	if (info->en_buck_parallel_chg && info->proc_data.cp_iic_ok) {
		strategy_quickchg_enable_buck_charging(info, 0, 0, false);
	}
	(void)mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
	(void)mca_vote(info->buck_charge_curr_voter, "wire_qc", false, 0);
	if (info->proc_data.cp_iic_ok) {
		(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
		(void)platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
	}
	memset(info->proc_data.cur_stage, 0, sizeof(info->proc_data.cur_stage));
	memset(&(info->proc_data.secure_info), 0,  sizeof(info->proc_data.secure_info));
	if (info->proc_data.charge_flag != MCA_QUICK_CHG_STS_CHARGE_DONE)
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_NO_CHARGING;
}

static int mca_quick_charge_adjust_adapter_mode(struct mca_quick_charge_info *info)
{
	int i, ret;
	int mode = 0;
	int ratio = 1;
	int max_volt;
	struct adapter_power_cap_info cap_info;

	ret = protocol_class_get_adapter_power_cap(info->proc_data.cur_protocol, &cap_info);
	if (ret)
		return ret;

	if (info->batt_type == MCA_BATTERY_TYPE_SERIES ||
		info->batt_type == MCA_BATTERY_TYPE_SINGLE_SERIES)
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
			if (info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY || info->is_eu_model) {
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

		if (cap_info.cap[i].max_voltage >= MCA_QUICK_CHG_DIV1_VOLT_TH_HIGH * ratio &&
			cap_info.cap[i].min_voltage <= MCA_QUICK_CHG_DIV1_VOLT_TH_LOW * ratio) {
			info->proc_data.adp_info[i].adp_mode |= MCA_QUICK_CHG_MODE_DIV_1;
			mode |= MCA_QUICK_CHG_MODE_DIV_1;
			max_volt = min(MCA_QUICK_CHG_DIV1_MAX_VOLT * ratio, cap_info.cap[i].max_voltage);
			info->proc_data.adp_mode_power[0] = max(info->proc_data.adp_mode_power[0],
				max_volt * cap_info.cap[i].max_current / 1000);
		}

		mca_log_info("index %d, max_power %d %d %d, adp_mode %d",
			i, info->proc_data.adp_mode_power[0],
			info->proc_data.adp_mode_power[1],
			info->proc_data.adp_mode_power[2],
			info->proc_data.adp_info[i].adp_mode);
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
static int mca_quick_get_parallel_volt_para(struct mca_quick_charge_info *info, int fg_index)
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
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			break;
		}
	}

	if (base_flip_para->temp_info[i].volt_ffc_info.volt_para_size) {
#line 969
		mca_log_info("select ffc volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_ffc_info;
	} else if (base_flip_para->temp_info[i].volt_info.volt_para_size) {
#line 972
		mca_log_info("select normal volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_info;
	} else {
#line 977
		mca_log_info("no volt para\n");
		return -1;
	}
	return ret;
}

static int mca_quick_charger_get_base_limit_cur(struct mca_quick_charge_info *info, int cur_max)
{
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = proc_data->parall_cur_stage[FG_IC_BASE];
	int ibat = info->proc_data.parall_ibat[FG_IC_BASE];
	int vbat = info->proc_data.parall_vbat[FG_IC_BASE];
	int base_cur_max;
	int final_curr = cur_max;
	static int over_curr_count;
	static bool runSwOcp;

	if (!proc_data->cur_volt_paraller[FG_IC_BASE]->volt_para)
		return 0;
	base_cur_max = proc_data->cur_volt_paraller[FG_IC_BASE]->volt_para[cur_stage / 2].current_max;
	if (ibat >= base_cur_max)
		over_curr_count++;

	if (over_curr_count > OVER_CURRENT_COUNT && final_curr) {
		final_curr -= BASE_FCC_MA_HYS;
		over_curr_count = 0;
		runSwOcp = true;
	} else if (ibat < base_cur_max - 500 && runSwOcp) {
		final_curr = 0;
		runSwOcp = false;
	}

	mca_log_err("runSwOcp:%d cur_base_stage:%d base_ibat:%d base_vbat:%d base_cur_max:%d over_curr_count:%d cur_max:%d final_cur:%d\n",
		runSwOcp, cur_stage, ibat, vbat, base_cur_max, over_curr_count, cur_max, final_curr);
	return final_curr;
}

static int mca_quick_charger_set_flip_cur(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = proc_data->parall_cur_stage[FG_IC_FLIP];
	int ibat = info->proc_data.parall_ibat[FG_IC_FLIP];
	int vbat = info->proc_data.parall_vbat[FG_IC_FLIP];
	int flip_curr_max = 0;

	if (!proc_data->cur_volt_paraller[FG_IC_FLIP]->volt_para)
		return 0;
	flip_curr_max = proc_data->cur_volt_paraller[FG_IC_FLIP]->volt_para[cur_stage / 2].current_max;
	platform_class_loadsw_set_ibat_limit(LOADSW_ROLE_MASTER, flip_curr_max);
	mca_log_err("flip_curr_max %d, cur_stage %d, ibat_slave %d, vbat_slave %d\n", flip_curr_max, cur_stage, ibat, vbat);

	return 0;
}

static int mca_quick_charge_can_tbat_do_charge(unsigned int role,
	struct mca_quick_charge_info *info, bool init)
{
	struct mca_quick_charge_batt_para_info *batt_para = &info->batt_para[role];
	struct mca_quick_charge_temp_para *temp_para;
	int temp = 0;
	int ret, i, flag = 0;
	int vterm = 0, high_temp_vterm = 0, vbat_now_mv = 0;
	//const struct mca_hwid *hwid = mca_get_hwid_info();

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1103
		mca_log_err("get %d batt temp fail\n", role);
		return ret;
	}
	temp /= 10;
#line 1107
	mca_log_info("temp=%d\n", temp);
	for (i = 0; i < batt_para->temp_para_size; i++) {
		temp_para = &batt_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			flag = 1;
			break;
		}
	}

	if (!flag) {
#line 1125
		mca_log_err("%d can not find temp_para\n", role);
		return -1;
	}

	if(info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY){
		vterm = temp_para->ffc_max_fv;
	} else {
		vterm = temp_para->normal_max_fv;
	}

	high_temp_vterm = strategy_class_fg_get_high_temp_vterm();
	vbat_now_mv = info->proc_data.vbat[FG_IC_MASTER];

#line 1129
	mca_log_info("temp_para_index new %d, old %d\n", i, info->proc_data.temp_para_index[role]);
	if (!temp_para->max_current || (vterm < high_temp_vterm && vbat_now_mv > vterm)) {
		mca_log_info("high temp stop charging, v[%d %d %d] max_cur: %d\n",
			vterm, high_temp_vterm, vbat_now_mv,temp_para->max_current);
		info->proc_data.ffc_flag = 0;
		strategy_class_fg_set_fastcharge(false);

		if (init)
			return -1;

		if (i > info->proc_data.temp_para_index[role])
			info->proc_data.temp_hys_en = MCA_QUICK_TEMP_HYS_HIGH;
		if (i < info->proc_data.temp_para_index[role])
			info->proc_data.temp_hys_en = MCA_QUICK_TEMP_HYS_LOW;

		return -1;
	}

	if (init) {
		switch (info->proc_data.temp_hys_en) {
		case MCA_QUICK_TEMP_HYS_DIS:
			break;
		case MCA_QUICK_TEMP_HYS_HIGH:
			if (temp > (temp_para->temp_high - temp_para->high_temp_hysteresis)) {
#line 1157
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
			break;
		case MCA_QUICK_TEMP_HYS_LOW:
			if (temp <= (temp_para->temp_low + temp_para->low_temp_hysteresis)) {
#line 1163
				mca_log_info("hys low not satisfied\n");
				return -1;
			}
			break;
		default:
			break;
		}

		info->proc_data.temp_hys_en = MCA_QUICK_TEMP_HYS_DIS;
	} else {
		if (i == info->proc_data.temp_para_index[role] ||
			info->proc_data.temp_max_cur[role] == 0)
			return 0;

		/* in qc charging, change to high temp need hys */
		if (i > info->proc_data.temp_para_index[role]) {
			if (temp < (temp_para->temp_low + temp_para->low_temp_hysteresis))
				return 0;
		}
	}

	info->proc_data.temp_para_index[role] = i;
	info->proc_data.zone_changed = true;
	info->proc_data.temp_max_cur[role] = temp_para->max_current;
	if ((info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY)
		&& batt_para->temp_info[i].volt_ffc_info.volt_para_size) {
#line 1197
		mca_log_info("select ffc volt para\n");
		info->proc_data.ffc_flag = 1;
		info->proc_data.temp_max_fv[role] = temp_para->ffc_max_fv;
		info->proc_data.cur_volt_para[role] = &batt_para->temp_info[i].volt_ffc_info;
	} else if (batt_para->temp_info[i].volt_info.volt_para_size) {
#line 1202
		mca_log_info("select normal volt para\n");
		info->proc_data.ffc_flag = 0;
		info->proc_data.temp_max_fv[role] = temp_para->normal_max_fv;
		info->proc_data.cur_volt_para[role] = &batt_para->temp_info[i].volt_info;
	} else {
#line 1209
		mca_log_info("no volt para\n");
		info->proc_data.ffc_flag = 0;
		return -1;
	}

	/*if (hwid && hwid->country_version == CountryCN) {
		if (info->support_curr_monitor && info->curr_monitor_time_s && temp_para->max_current == MCA_QUICK_CHG_MAX_CURR_MA) {
			info->fastchg_temp_flag = true;
#line 1222
			mca_log_info("enter fast charging temp range\n");
		} else {
			info->fastchg_temp_flag = false;
		}
	} else {
		info->fastchg_temp_flag = false;
	}*/
	if (info->support_curr_monitor && info->curr_monitor_time_s && temp_para->max_current == MCA_QUICK_CHG_MAX_CURR_MA) {
		info->fastchg_temp_flag = true;
#line 1222
		mca_log_info("enter fast charging temp range\n");
	} else {
		info->fastchg_temp_flag = false;
	}

#line 1230
	mca_log_info("temp_para_index=%d, max_cur=%d, max_fv = %d\n", i, temp_para->max_current, info->proc_data.temp_max_fv[role]);

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

static int mca_quick_charge_judge_temp(struct mca_quick_charge_info *info)
{
	int ret;

	ret = mca_quick_charge_can_tbat_do_charge(FG_IC_MASTER, info, true);
	if (ret)
		return ret;

	return 0;
}

static int mca_quick_charge_judge_vbat(struct mca_quick_charge_info *info)
{
	int vbat = 0;

	strategy_class_fg_ops_get_voltage(&vbat);

	if (vbat < info->min_vbat || vbat  > info->max_vbat) {
#line 1318
		mca_log_info("vbat %d is out of range, try next loop\n", vbat);
		return -1;
	}

	return 0;
}

#define QUICKCHG_HIGH_RAWSOC 10000
static int mca_quick_charge_judge_thermal_vote(struct mca_quick_charge_info *info, bool is_prechk)
{
	int thermal_curr = 0, is_rechg = 0, rawsoc = 0;
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);
	int volt_para_size = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_min = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para[volt_para_size - 1].current_min;

	if (info->cp_type <= MCA_BATTERY_TYPE_SINGLE_NUM_MAX)
		thermal_curr = info->proc_data.thermal_cur[MCA_QUICK_CHG_CH_SINGLE];

	strategy_class_fg_ops_get_recharge(&is_rechg);
	strategy_class_fg_ops_get_rawsoc(&rawsoc);

	mca_log_info("info->thermal_cur: %d %d rechg:%d, rawsoc:%d\n",
		info->proc_data.thermal_cur[MCA_QUICK_CHG_CH_SINGLE], info->proc_data.thermal_cur[MCA_QUICK_CHG_CH_MULTI],
		is_rechg, rawsoc);

	if(is_prechk) {
		if (thermal_curr < info->cp_switch_pmic_th && thermal_curr > 0) {
			mca_log_info("prechk, skip cp for thermal");
			return -1;
		}
	} else {
		/* thermal curr lower than cur_min may cause overcharge, so exit quickchg when rawsoc is high and this occurs. */
		if (rawsoc >= QUICKCHG_HIGH_RAWSOC && thermal_curr < cur_min && thermal_curr > 0) {
			mca_log_info("skip cp for thermal curr soc high");
			return -1;
		} else if (thermal_curr < info->cp_switch_pmic_th && thermal_curr > 0) {
#line 1336
			mca_log_info("skip cp for thermal");
			return -1;
		}
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

	if (!info->is_platform_qc)
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
	if (!info->is_platform_qc)
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
			mca_quick_get_parallel_volt_para(info, j);

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

static void mca_quick_charge_start_charging(struct mca_quick_charge_info *info)
{
	int ret;

	ret = mca_quick_charge_judge_soc(info);
	if (ret) {
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

	mca_quick_charge_select_stage(info);

	ret = mca_quick_charge_judge_temp(info);
	if (ret) {
#line 1972
		mca_log_info("temp range invalid\n");
		return;
	}

	ret = mca_quick_charge_judge_thermal_vote(info, true);
	if (ret) {
#line 1994
		mca_log_info("thermal fcc too low\n");
		return;
	}

	ret = platform_class_cp_check_iic_check(info->proc_data.cur_work_cp);
	if (ret < 0) {
		info->proc_data.cp_iic_ok = false;
		mca_log_err("check cp I2C error\n");
		goto OUT;
	} else
		info->proc_data.cp_iic_ok = true;

	ret = mca_quick_charge_select_cp(info);
	if (ret) {
#line 2000
		mca_log_err("select cp mode fail\n");
		return;
	}

	if (info->vfc_para.support_cp_vfc) {
		if (info->cp_type == MCA_CP_TYPE_PARALLEL) {
			platform_class_cp_set_fsw(CP_ROLE_MASTER, MCA_QUICK_CHG_CP_DEFAULT_FSW);
			platform_class_cp_set_fsw(CP_ROLE_SLAVE, MCA_QUICK_CHG_CP_DEFAULT_FSW);
		} else {
			platform_class_cp_set_fsw(CP_ROLE_MASTER, MCA_QUICK_CHG_CP_DEFAULT_FSW);
		}
	}

	ret = mca_eu_pps_limit_power(info, ret);

	platform_class_cp_enable_busucp(info->proc_data.cur_work_cp, false);
	if (!info->is_platform_qc)
		ret = mca_quick_charge_open_path(info);
	else
		ret = mca_qc_quick_charge_open_path(info);
	platform_class_cp_enable_busucp(info->proc_data.cur_work_cp, true);
	if (ret) {
		info->proc_data.total_err++;
		mca_vote(info->input_suspend_voter, "wire_qc", false, 0);
		mca_vote(info->buck_charge_curr_voter, "wire_qc", false, 0);
		platform_class_cp_set_charging_enable(info->proc_data.cur_work_cp, false);
		info->pd_switch_to_pmic = true;
		if (info->is_platform_qc)
			info->proc_data.cur_adp_volt = MCA_QUICK_QC_CHG_ADP_DEFAULT_VOLT;
		mca_quick_charge_req_adp_volt_and_cur(info);
		mca_quick_charge_stop_charging(info);
#line 2054
		mca_log_err("open path fail\n");
		return;
	}
	info->pd_switch_to_pmic = false;
	info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGING;
	info->boost_done = false;
	if (info->proc_data.ffc_flag)
		strategy_class_fg_set_fastcharge(true);

	schedule_delayed_work(&info->monitor_work, 0);
	if (info->vfc_para.support_cp_vfc)
		schedule_delayed_work(&info->vfc_work, msecs_to_jiffies(MCA_QUICK_CHG_VFC_INTERVAL));
	return;
OUT:
	info->pd_switch_to_pmic = true;
	(void)protocol_class_pd_set_fixed_volt(TYPEC_PORT_0, MCA_QUICK_CHG_ADP_DEFAULT_VOLT);
	mca_quick_charge_stop_charging(info);
	mca_log_err("cp iic error exit quick chg\n");
	return;
}

static bool mca_quick_charge_check_chg_done(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_batt_para_info *batt_para = &info->batt_para[FG_IC_MASTER];
	struct mca_quick_charge_temp_para *temp_para;
	bool found_temp_para = false;
	int temp = 0, i= 0, ret = 0, eff_idx = 0, idx_now = 0;
	int recharge_vbat = 0;
	int high_temp_vterm;
	int vterm = 0;

	if (info->proc_data.charge_flag != MCA_QUICK_CHG_STS_CHARGE_DONE)
		return false;

	if (info->hardware_cv)
		return false;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
		mca_log_err("get batt temp fail\n");
		return ret;
	}
	temp /= 10;
	for (i = 0; i < batt_para->temp_para_size; i++) {
		temp_para = &batt_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			found_temp_para = true;
			break;
		}
	}

	if (!found_temp_para) {
		mca_log_err("%d can not find temp_para\n", FG_IC_MASTER);
		return false;
	}

	idx_now = info->proc_data.temp_para_index[FG_IC_MASTER];
	if ((i > idx_now && temp <= (temp_para->temp_low + temp_para->low_temp_hysteresis)) ||
		(i < idx_now && temp > (temp_para->temp_high - temp_para->high_temp_hysteresis))) {
			eff_idx = info->proc_data.temp_para_index[FG_IC_MASTER];
	} else {
			eff_idx = i;
	}

	if(info->proc_data.adp_type == XM_CHARGER_TYPE_PD_VERIFY){
		vterm = batt_para->temp_info[eff_idx].temp_para.ffc_max_fv;
	} else {
		vterm = batt_para->temp_info[eff_idx].temp_para.normal_max_fv;
	}

	high_temp_vterm = strategy_class_fg_get_high_temp_vterm();
	if(vterm > high_temp_vterm)
		return false;

	recharge_vbat = vterm - info->recharge_vbat_delta;

	mca_log_info("v[%d %d %d %d], temp idx[%d %d %d]\n",
		vterm, info->recharge_vbat_delta, recharge_vbat, info->proc_data.vbat[FG_IC_MASTER],
		idx_now, i, eff_idx);

	mca_quick_charge_update_vbat(info);
	if (info->batt_type <= MCA_BATTERY_TYPE_SINGLE_NUM_MAX)
		return (info->proc_data.vbat[FG_IC_MASTER] < recharge_vbat);

	return ((info->proc_data.vbat[FG_IC_MASTER] > recharge_vbat) &&
		(info->proc_data.vbat[FG_IC_SLAVE] > recharge_vbat));
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

static int mca_quick_charge_select_volt_para(struct mca_quick_charge_info *info)
{
	int ret;

	ret = mca_quick_charge_can_tbat_do_charge(FG_IC_MASTER, info, false);
	if (ret)
		return ret;

	return 0;
}

#define QUICK_CHARGE_TERMATIN_TIMEOUT 5
#define QUICK_CHARGE_VTERM_HYS	3
#define QUICK_RAWSOC_HIGH_DONE_LIMIT  400
static void mca_quick_charge_check_charge_done(struct mca_quick_charge_info *info)
{
	if (info->hardware_cv)
		return;
	int last_volt_stage = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size - 1;
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[last_volt_stage].voltage;
	int smartchg_delta_fv = info->smartchg_data.delta_fv;
	int hys_delta_mv = info->fv_hys_delta_mv;
	int vterm = vbat_th - smartchg_delta_fv - hys_delta_mv;
	int iterm = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[last_volt_stage].current_min;
	static int count;
	int charging_done = 0, ffc_flag = 0, rawsoc = 0;
	bool high_rawsoc_exit = false;

	iterm = (iterm * info->curr_terminate_ratio) / 100;

	/* judge chg termination by vbat or max cell volt based on battery type*/
	switch(info->batt_type) {
	case MCA_BATTERY_TYPE_SINGLE:
	case MCA_BATTERY_TYPE_PARALLEL:
	case MCA_BATTERY_TYPE_SERIES:
		if(info->proc_data.vbat[FG_IC_MASTER] >= vterm && info->proc_data.ibat[FG_IC_MASTER] <= iterm)
			count++;
		else
			count = 0;
		break;
	case MCA_BATTERY_TYPE_SINGLE_SERIES:
		if(info->proc_data.max_vcell[FG_IC_MASTER] * 2 >= vterm && info->proc_data.ibat[FG_IC_MASTER] <= iterm) {
			count++;
			mca_log_debug("v:%d/%d/%d, i:%d/%d, count:%d done:%d\n",
				vterm, info->proc_data.vbat[FG_IC_MASTER], info->proc_data.max_vcell[FG_IC_MASTER] * 2,
				iterm, info->proc_data.ibat[FG_IC_MASTER], count, charging_done);
		} else
			count = 0;
		break;
	default:
		mca_log_err("error! batt type unknown!\n");
		if(info->proc_data.vbat[FG_IC_MASTER] >= vterm && info->proc_data.ibat[FG_IC_MASTER] <= iterm)
			count++;
		else
			count = 0;
		break;
	}

	charging_done = strategy_class_fg_ops_get_charging_done();
	ffc_flag = strategy_class_fg_get_fastcharge();

	if (info->rawsoc_swith_pmic_th) {
		strategy_class_fg_ops_get_rawsoc(&rawsoc);
		if(rawsoc >= info->rawsoc_swith_pmic_th) {
			mca_log_err("switch buck: %d %d\n", rawsoc, info->rawsoc_swith_pmic_th);
			high_rawsoc_exit = true;
		}
	}

	if(!charging_done && high_rawsoc_exit) {
		mca_log_err("high soc swith buck\n");
		/* high ibat may causes vbat too high if switch to pmic and normal charging */
		mca_vote(info->buck_charge_curr_voter, "qc_done", true, QUICK_RAWSOC_HIGH_DONE_LIMIT);
		schedule_delayed_work(&info->float_vbat_drop_work, msecs_to_jiffies(5*60*1000));
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGE_DONE;
	}

	if (count > QUICK_CHARGE_TERMATIN_TIMEOUT || charging_done) {
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGE_DONE;
		if(ffc_flag)
			strategy_class_fg_ops_set_charging_done(true);

		mca_log_err("quick charge done exit, v:%d/%d/%d, i:%d/%d, count:%d done:%d\n",
		vterm, info->proc_data.vbat[FG_IC_MASTER], info->proc_data.max_vcell[FG_IC_MASTER],
		iterm, info->proc_data.ibat[FG_IC_MASTER], count, charging_done);
		count = 0;
	}
}

static int mca_quick_charge_get_vstep(int cur_cap, struct mca_quick_charge_info *info)
{
	int i;
	int vstep = 0;
	bool decrease = false;

	if (cur_cap <= 0) {
		cur_cap = -cur_cap;
		decrease = true;
	}

	/* cv can not add step */
	if (info->proc_data.cur_stage[0] % 2 && !decrease)
		return MCA_QUICK_CHG_DEFAULT_VSTEP;

	for (i = 0; i < VSTEP_PARA_MAX_GROUP; i++) {
		if (!info->vstep_para[i].volt_ratio)
			break;

		if (info->vstep_para[i].volt_ratio != info->proc_data.ratio)
			continue;

		if (info->vstep_para[i].cur_gap <= cur_cap) {
			vstep = MCA_QUICK_CHG_DEFAULT_VSTEP * info->vstep_para[i].vstep_ratio;
			break;
		}
	}

	if (!vstep)
		vstep = MCA_QUICK_CHG_DEFAULT_VSTEP;

	return vstep;
}

#define OVER_VOLATGE_COUNT 3
#define DEC_FCC_MA_HYS 1000
#define MIN_FLOAT_VOLATGE 4400
static int mca_quick_charger_get_secure_cur(struct mca_quick_charge_info *info)
{
	int vbat = info->proc_data.vbat[FG_IC_MASTER];
	int ibatt = info->proc_data.ibat[FG_IC_MASTER];
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int vbat_th = info->proc_data.temp_max_fv[FG_IC_MASTER];
	int ibatt_min = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[volt_para_size - 1].current_min;
	int delta_fv = info->smartchg_data.delta_fv;
	static int over_volt_count;
	int secure_cur = 0;

	info->proc_data.secure_info.max_vbatt = (vbat_th - delta_fv) > MIN_FLOAT_VOLATGE ? (vbat_th - delta_fv) : vbat_th;

	if (vbat >= info->proc_data.secure_info.max_vbatt)
		over_volt_count++;
	else
		over_volt_count = 0;

	if (over_volt_count > OVER_VOLATGE_COUNT && !info->check_vbat_ov) {
		if (!info->proc_data.secure_info.secure_cur)
			info->proc_data.secure_info.secure_cur = ibatt;
		info->proc_data.secure_info.secure_cur -= DEC_FCC_MA_HYS;
		info->proc_data.secure_info.secure_cur = max(info->proc_data.secure_info.secure_cur, ibatt_min);
		over_volt_count = 0;
		info->check_vbat_ov = true;
		if (info->proc_data.secure_info.secure_cur <= info->pps_taper_fcc_ma)
			info->proc_data.stop_charging = true;
		mca_log_err("trigger over voltage secure_cur: %d, pps_taper_fcc_ma: %d\n",
			info->proc_data.secure_info.secure_cur, info->pps_taper_fcc_ma);
	}

	mca_log_info("max_vbatt:%d delta_fv:%d secure_cur:%d\n",
		info->proc_data.secure_info.max_vbatt, delta_fv, info->proc_data.secure_info.secure_cur);
	secure_cur = info->proc_data.secure_info.secure_cur;
	return secure_cur;
}

/* todo:add multi battery process */
static int mca_quick_charge_select_max_ibat(struct mca_quick_charge_info *info)
{
	struct mca_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = proc_data->cur_stage[FG_IC_MASTER];
	int cur_max = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_max;
	int volt_para_size = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_min = proc_data->cur_volt_para[FG_IC_MASTER]->volt_para[volt_para_size - 1].current_min;
	int thermal_cur;
	int channel_cur;
	int secure_cur = mca_quick_charger_get_secure_cur(info);
	int delta_cur = info->smartchg_data.delta_ichg;
	int base_limit_curr;

#line 2538
	mca_log_err("cur_stage %d cur_max %d delta_cur %d cur_work_cp %d\n", cur_stage, cur_max, delta_cur, proc_data->cur_work_cp);
	if (info->dtpt_status)
		cur_max = cur_max * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE;
	else
		cur_max = cur_max - delta_cur;

	if (proc_data->cur_work_cp != MCA_QUICK_CHG_CP_DUAL) {
		thermal_cur = proc_data->thermal_cur[MCA_QUICK_CHG_CH_SINGLE];
		channel_cur = proc_data->single_curr;
	} else {
		thermal_cur = proc_data->thermal_cur[MCA_QUICK_CHG_CH_MULTI];
		channel_cur = proc_data->max_curr;
	}
	mca_log_info("cur_max %d secure_cur %d channel_cur %d thermal_cur %d\n",
		cur_max, secure_cur, channel_cur, thermal_cur);
	if (thermal_cur)
		cur_max = min(cur_max, thermal_cur);

	if (secure_cur)
		cur_max = min(cur_max, secure_cur);

	cur_max = min(cur_max, channel_cur);
	cur_max = min(cur_max, proc_data->temp_max_cur[FG_IC_MASTER]);
	cur_max = min(cur_max, proc_data->max_adp_curr * proc_data->ratio);
	if (info->support_base_flip && proc_data->sw_ocp_curr)
		cur_max = min(cur_max, proc_data->sw_ocp_curr);
	/* will overcharge when cur_max < cur_in and terminate by cp. */
	if(!info->rawsoc_swith_pmic_th && !info->hardware_cv)
		cur_max = max(cur_max, cur_min);

	if ((info->proc_data.adp_type == XM_CHARGER_TYPE_PPS && info->is_eu_model) || info->fake_pps_ptf) {
		cur_max = mca_eu_pps_limit_power(info, cur_max);
	} else if (info->proc_data.adp_type == XM_CHARGER_TYPE_PPS && !info->is_eu_model) {
		/* for third party pps, beside set ibus compensation to 0, also reduce 100mA max target current */
		cur_max -= 200;
		if (info->smartchg_data.pwr_boost_state && cur_max > MCA_QUICK_CHG_PPS_BOOST_FCC_CURR_TH)
			cur_max = MCA_QUICK_CHG_PPS_BOOST_FCC_CURR_TH;
		else if (!info->smartchg_data.pwr_boost_state && cur_max > MCA_PPS_FCC_LIMIT)
			cur_max = MCA_PPS_FCC_LIMIT;
	}
	mca_log_info("support_base_flip:[]: %d\n", info->support_base_flip);
	if (info->support_base_flip) {
		mca_quick_get_parallel_volt_para(info, FG_IC_BASE);
		mca_quick_get_parallel_volt_para(info, FG_IC_FLIP);
		base_limit_curr = mca_quick_charger_get_base_limit_cur(info, cur_max);
		if (base_limit_curr)
			cur_max = min(cur_max, base_limit_curr);
		mca_quick_charger_set_flip_cur(info);
	}
	return cur_max;
}

/* now only support single 1S battery, 1S2P battery to do */
static int mca_qc_get_vbus_change_trend(struct mca_quick_charge_info *info)
{
	static int ibatt_count;
	static int vbatt_count;
	static int ibatt_change = 1;
	static int vbatt_change = 1;
	static int vbat_now_mv_before;
	static int ibat_now_ma_before;
	static int decrement_count;
	int dir = DIR_HOLD, ibus = 0, vbus = 0, ret = 0;
	int vbat_now_mv = info->proc_data.vbat[FG_IC_MASTER];
	int ibat_now_ma = info->proc_data.ibat_total;
	int target_limit_fcc_ma, target_limit_ibus_ma, ibat_qc_max_ma, ibus_qc_max_ma;
	int delta_fv = info->smartchg_data.delta_fv;

	/* now only support single 1S battery, 1S2P battery to do*/
	info->qc_normal_charge_fv_max_mv = info->proc_data.temp_max_fv[FG_IC_MASTER];
	ret = mca_qc_quick_charge_update_ibus(info);
	if (ret) {
#line 2664
		mca_log_err("regulation qc get ibus failed\n");
		return -1;
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

	if (info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE] >= 1500) {
		target_limit_fcc_ma = min(info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE], ibat_qc_max_ma);
		target_limit_ibus_ma = min((info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE] / 2 + 100),
			ibus_qc_max_ma);
		mca_log_info("target_limit_fcc_ma: %d, target_limit_ibus_ma:%d\n",
			target_limit_fcc_ma, target_limit_ibus_ma);
	} else if (!info->thermal_cur[CHG_MODE_DIV2][MCA_QUICK_CHG_CH_SINGLE]) {
		target_limit_fcc_ma = ibat_qc_max_ma;
		target_limit_ibus_ma = ibus_qc_max_ma;
	} else {
		return DIR_HOLD;
	}

	if (ibat_now_ma == ibat_now_ma_before && ibatt_count < 5) {
		ibatt_count++;
		ibatt_change = 0;
	} else {
		ibatt_change = 1;
		ibatt_count = 0;
		ibat_now_ma_before = ibat_now_ma;
	}

	if (vbat_now_mv == vbat_now_mv_before && vbatt_count < 5) {
		vbatt_count++;
		vbatt_change = 0;
	} else {
		vbatt_change = 1;
		vbatt_count = 0;
		vbat_now_mv_before = vbat_now_mv;
	}

	switch (info->proc_data.adp_type) {
	case XM_CHARGER_TYPE_HVDCP3_B:
		if (vbus <= info->qc3_max_vbus_limit_mv && ibus < target_limit_ibus_ma - 500 &&
			(vbatt_change && vbat_now_mv < info->qc_normal_charge_fv_max_mv - 50 - delta_fv) &&
			(ibatt_change && ibat_now_ma < target_limit_fcc_ma - 700)) {
			dir = DIR_UP;
#line 2727
			mca_log_info("QC3 D plus pluse sent\n");
		}
		if ((vbatt_change && vbat_now_mv > info->qc_normal_charge_fv_max_mv - delta_fv - 15) ||
			(ibatt_change && ibat_now_ma > target_limit_fcc_ma) ||
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
			(vbatt_change && vbat_now_mv < info->qc_normal_charge_fv_max_mv - 20 - delta_fv) &&
			(ibatt_change && ibat_now_ma < target_limit_fcc_ma - 50)) {
			dir = DIR_UP;
#line 2745
			mca_log_info("QC3P5 D plus pluse sent\n");
		}
		if ((vbatt_change && vbat_now_mv > info->qc_normal_charge_fv_max_mv - 10 - delta_fv)
			|| (ibatt_change && ibat_now_ma > target_limit_fcc_ma + 50)
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

static int mca_qc_quick_charge_regulation(struct mca_quick_charge_info *info)
{
	int qc_pluse_direction = DIR_HOLD, hvdcp_cmd;
	int ret = 0;

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

static int mca_quick_charge_select_min_vbatt_th(struct mca_quick_charge_info *info)
{
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].voltage;
	int delta_fv = info->smartchg_data.delta_fv;

	if ((cur_stage / 2) == (volt_para_size - 1)) {
#line 2798
		mca_log_err("vbatt_th %d delta_fv %d pps_taper_vol_hys:%d\n",
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

#define MCA_QUICK_CHG_O11U_P0GL  0x100000
#define MCA_QUICK_CHG_O11U_P01CN 0x00001
#define MCA_QUICK_CHG_O11U_P01GL 0x100001
#define MCA_QUICK_CHG_O11U_P1CN  0x10000
#define MCA_QUICK_CHG_O11U_P1GL  0x110000
#define MCA_QUICK_CHG_O11U_P1CN2 0x10002
#define MCA_QUICK_CHG_O11U_P1CN3 0x10003

/* todo: add mutil battery process */
static int mca_quick_charge_regulation(struct mca_quick_charge_info *info)
{
	int vbat = info->proc_data.vbat[FG_IC_MASTER];
	int vcell_max = info->proc_data.max_vcell[FG_IC_MASTER];
	int ibat = info->proc_data.ibat_total;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int cur_min = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_min;
	int ret, ibus, cur_max, vstep;
	int vbat_th;
	int final_vstep;
	u64 time_now_ms = ktime_get_boottime_ns() / 1000000;
	static u64 last_boost_time_ms;
	static u64 last_buck_time_ms;
	int vbus_mv;
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	u64 boost_time_diff_ms = time_now_ms - last_boost_time_ms;
	u64 buck_time_diff_ms = time_now_ms - last_buck_time_ms;

	ret = mca_quick_charge_update_ibus(info);
	if (ret) {
#line 3194
		mca_log_err("regulation get ibus failed\n");
		return -1;
	}
	ibus = info->proc_data.ibus;

	cur_max = mca_quick_charge_select_max_ibat(info);
	if (cur_max <= info->cp_switch_pmic_th) {
#line 3203
		mca_log_info("cur_max too low, stop quick charging: %d <= %d\n",
			cur_max, info->cp_switch_pmic_th);
		info->proc_data.stop_charging = true;
	}

	vbat_th = mca_quick_charge_select_min_vbatt_th(info);
	vstep = mca_quick_charge_get_vstep(cur_max - ibat, info);

	if (info->proc_data.adp_type != XM_CHARGER_TYPE_PD_VERIFY) {
		info->proc_data.ibus_compensation = 0;
	} else {
		if (cur_stage == (2 * volt_para_size - 1))
			info->proc_data.ibus_compensation = 0;
		else {
			if (!info->proc_data.ibus_compensation) {
				switch (info->proc_data.work_mode) {
				case MCA_QUICK_CHG_MODE_DIV_1:
					info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV1];
					break;
				case MCA_QUICK_CHG_MODE_DIV_2:
					info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV2];
					break;
				case MCA_QUICK_CHG_MODE_DIV_4:
					info->proc_data.ibus_compensation = info->ibus_compensation[CHG_MODE_DIV4];
					break;
				default:
					break;
				}
#line 3252
				mca_log_info("reset ibus_compensation\n");
			}
		}
	}

	/* cv:cur_stage % 2 == 1 */
	if (cur_stage % 2) {
		if (vbat >= vbat_th) {
			if (abs(vbat_th - vbat) * info->proc_data.ratio >= MCA_QUICK_CHG_DEFAULT_VSTEP)
				final_vstep = max(info->proc_data.ratio * (vbat_th - vbat), -5 * MCA_QUICK_CHG_DEFAULT_VSTEP);
			else
				final_vstep = -MCA_QUICK_CHG_DEFAULT_VSTEP;
		} else if (ibus - info->proc_data.ibus_compensation > cur_max / info->proc_data.ratio || ibat > cur_max) {
			final_vstep = -vstep;
		} else if (ibus - info->proc_data.ibus_compensation < (cur_max - info->proc_data.delta_ibat) / info->proc_data.ratio &&
			ibat < cur_max - info->proc_data.delta_ibat) {
			final_vstep = vstep;
		} else {
			final_vstep = 0;
		}
	} else { // CC
		if (ibus - info->proc_data.ibus_compensation > cur_max / info->proc_data.ratio || ibat > cur_max) {
			final_vstep = -vstep;
		} else if (ibus - info->proc_data.ibus_compensation < (cur_max - info->proc_data.delta_ibat) / info->proc_data.ratio &&
			ibat < cur_max - info->proc_data.delta_ibat) {
			final_vstep = vbat < vbat_th - 10 ? vstep : MCA_QUICK_CHG_DEFAULT_VSTEP;
		} else {
			final_vstep = 0;
		}
	}

	if (info->boost_done) {
		if ((final_vstep == MCA_QUICK_CHG_DEFAULT_VSTEP && boost_time_diff_ms < 2000) ||
			(final_vstep == MCA_QUICK_CHG_DEFAULT_VSTEP && buck_time_diff_ms < 1000))
			final_vstep = 0;
		else if (final_vstep == -MCA_QUICK_CHG_DEFAULT_VSTEP && buck_time_diff_ms < 1000)
			final_vstep = 0;
	}

	if (info->en_buck_parallel_chg && cur_max > 16000) {
		if (0 /*mca_quick_charge_is_en_buck_hwid()*/) {
			strategy_quickchg_enable_buck_charging(info,
				info->buck_icl_fcc_curr[0], info->buck_icl_fcc_curr[1], true);
		} else {
			strategy_quickchg_enable_buck_charging(info,
				info->buck_icl_fcc_curr[0] - 500, info->buck_icl_fcc_curr[1] - 2000, true);
		}
	} else if (info->en_buck_parallel_chg) {
		strategy_quickchg_enable_buck_charging(info, 0, 0, false);
	}

	if (final_vstep > 0) {
		last_boost_time_ms = time_now_ms;
	} else if (final_vstep < 0) {
		info->boost_done = true;
		last_buck_time_ms = time_now_ms;
	}

	info->proc_data.cur_adp_volt += final_vstep;
	info->proc_data.cur_adp_cur = info->proc_data.max_adp_curr;
	platform_class_cp_get_bus_voltage(MCA_QUICK_CHG_CP_MASTER, &vbus_mv);
	mca_log_err("cur_stage[%d]: adp_volt: %d/%d, ibat: %d/%d/%d/%d, vbat: %d/%d, vcell_max:%d, ibus: %d/%d, time_diff: %llu/%llu, final_vstep: %d\n",
		cur_stage, info->proc_data.cur_adp_volt, vbus_mv, cur_max, cur_min, ibat, cur_max - ibat,
		vbat_th, vbat, vcell_max, ibus, info->proc_data.ibus_compensation, boost_time_diff_ms, buck_time_diff_ms, final_vstep);
	ret = mca_quick_charge_req_adp_volt_and_cur(info);
	if (ret)
#line 3373
		mca_log_err("regulation req adp volt failed\n");

	return ret;
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

	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE_NUM_MAX) {
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
	struct mca_quick_charge_process_data *proc_data = &info->proc_data;

	ret = mca_quick_charge_update_ibus(info);
	if (ret)
		return;

	ibus = proc_data->ibus;
	if (ibus < proc_data->multi_ibus_th + proc_data->ibus_inc)
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
}

static void mca_quick_charge_change_path(struct mca_quick_charge_info *info)
{
	if (info->cp_type != MCA_CP_TYPE_PARALLEL || info->is_platform_qc)
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

static void mca_quick_charge_monitor_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info =  container_of(work,
		struct mca_quick_charge_info, monitor_work.work);
	int ret;
	bool need_goto_taper = false;
	static int last_ffc_flag;

	if (!strategy_class_fg_is_chip_ok()) {
#line 3651
		mca_log_info("fg i2c error, exit quick charging\n");
		goto out;
	}

	if (info->vfc_para.support_cp_vfc)
		mca_quick_charge_ibus_queue_push(info, info->proc_data.ibus);

	ret = mca_quick_charge_judge_thermal_vote(info, false);
	if (ret)
		goto out;

	if (info->is_platform_qc) {
		ret = mca_qc_quick_charge_is_charge_abnormal(info);
		need_goto_taper = mca_qc_check_if_goto_taper(info);
	} else {
		ret = mca_quick_charge_is_charge_abnormal(info);
		need_goto_taper = mca_check_if_goto_hw_taper(info);
	}
	if (ret || need_goto_taper)
		goto out;

	ret = mca_quick_charge_select_volt_para(info);
	if (ret) {
#line 3684
		mca_log_info("monitor temp out of range\n");
		goto out;
	}

	mca_quick_charge_select_stage(info);

	if (info->proc_data.ffc_flag != last_ffc_flag)
		strategy_class_fg_set_fastcharge(info->proc_data.ffc_flag);

	mca_quick_charge_check_charge_done(info);
	if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGE_DONE) {
#line 3702
		mca_log_info("monitor qc charge done\n");
		goto out;
	}
	mca_quick_charge_change_path(info);

	if (info->is_platform_qc)
		ret = mca_qc_quick_charge_regulation(info);
	else
		ret = mca_quick_charge_regulation(info);

	if (ret) {
		info->proc_data.total_err++;
#line 3731
		mca_log_info("regulation total_err: %d, ret: %d\n", info->proc_data.total_err, ret);
		goto out;
	}
	if (info->proc_data.stop_charging) {
#line 3735
		mca_log_info("monitor stop charging\n");
		goto out;
	}

	last_ffc_flag = info->proc_data.ffc_flag;
	schedule_delayed_work(&info->monitor_work,
		msecs_to_jiffies(MCA_QUICK_CHG_FAST_INTERVAL));
	return;
out:
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

static void mca_quick_charge_fvbat_drop_work(struct work_struct *work)
{
	struct mca_quick_charge_info *info =  container_of(work,
		struct mca_quick_charge_info, float_vbat_drop_work.work);

	mca_vote(info->buck_charge_curr_voter, "qc_done", false, 0);
	mca_log_info("release buck chg curr limit\n");
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

	if (!data)
		return -1;

#line 3885
	mca_log_info("event: %d, value: %d\n", event, value);
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
		info->online = 1;
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
			info->is_platform_qc = true;
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
		protocol_class_pd_reset_pps_stage(TYPEC_PORT_0, true);
		mca_log_info("sc6601a_reset_pps_stage\n");
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
	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE_NUM_MAX) {
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
	// 	info->batt_type > MCA_BATTERY_TYPE_SINGLE_NUM_MAX) {
	// 	mca_log_err("parse slave batt para failed\n");
	// 	return -1;
	// }
	return 0;
}

static void mca_quick_charge_parse_vstep_para(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, col, len;
	int data[VSTEP_PARA_MAX_GROUP * MCA_QUICK_CHG_VSTEP_PARA_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "vstep_para", data,
		VSTEP_PARA_MAX_GROUP, MCA_QUICK_CHG_VSTEP_PARA_MAX);
	if (len < 0)
		return;

	for (row = 0;  row < len / MCA_QUICK_CHG_VSTEP_PARA_MAX; row++) {
		col = row * MCA_QUICK_CHG_VSTEP_PARA_MAX + MCA_QUICK_CHG_VOLT_RATIO;
		info->vstep_para[row].volt_ratio = data[col];
		col = row * MCA_QUICK_CHG_VSTEP_PARA_MAX + MCA_QUICK_CHG_CURRENT;
		info->vstep_para[row].cur_gap = data[col];
		col = row * MCA_QUICK_CHG_VSTEP_PARA_MAX + MCA_QUICK_CHG_VSTEP_RATIO;
		info->vstep_para[row].vstep_ratio = data[col];
	}
}

static void mca_quick_charge_parse_chg_mode_info(struct device_node *node,
	struct mca_quick_charge_info *info)
{
	u32 idata[CHG_MODE_MAX] = { 0 };
	u32 curr_data[2] = { 0 };
	int ret;

	ret = mca_parse_dts_u32_array(node, "div_delta_volt", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_delta_volt[CHG_MODE_DIV1] = MCA_QUICK_CHG_DIV1_VOLT_DELTA_DEFAULT;
		info->div_delta_volt[CHG_MODE_DIV2] = MCA_QUICK_CHG_DIV2_VOLT_DELTA_DEFAULT;
		info->div_delta_volt[CHG_MODE_DIV4] = MCA_QUICK_CHG_DIV4_VOLT_DELTA_DEFAULT;
	} else {
		memcpy(info->div_delta_volt, idata, sizeof(idata));
	}
#line 4540
	mca_log_info("div_delta_volt %d %d %d\n", info->div_delta_volt[CHG_MODE_DIV1],
		info->div_delta_volt[CHG_MODE_DIV2], info->div_delta_volt[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_delta_ibat", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_delta_ibat[CHG_MODE_DIV1] = MCA_QUICK_CHG_DEFAULT_IBAT_DELTA;
		info->div_delta_ibat[CHG_MODE_DIV2] = MCA_QUICK_CHG_DEFAULT_IBAT_DELTA;
		info->div_delta_ibat[CHG_MODE_DIV4] = MCA_QUICK_CHG_DEFAULT_IBAT_DELTA;
	} else {
		memcpy(info->div_delta_ibat, idata, sizeof(idata));
	}
#line 4551
	mca_log_info("div_delta_ibat %d %d %d\n", info->div_delta_ibat[CHG_MODE_DIV1],
		info->div_delta_ibat[CHG_MODE_DIV2], info->div_delta_ibat[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_single_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_single_curr[CHG_MODE_DIV1] = MCA_QUICK_CHG_SINGLE_DIV1_CURR_TH;
		info->div_single_curr[CHG_MODE_DIV2] = MCA_QUICK_CHG_SINGLE_DIV2_CURR_TH;
		info->div_single_curr[CHG_MODE_DIV4] = MCA_QUICK_CHG_SINGLE_DIV4_CURR_TH;
	} else {
		memcpy(info->div_single_curr, idata, sizeof(idata));
		if (info->en_buck_parallel_chg) {
			if (0 /*!mca_quick_charge_is_en_buck_hwid()*/) {
				info->div_single_curr[CHG_MODE_DIV4] = 19000;
			}
		}
	}
#line 4567
	mca_log_info("div_single_curr %d %d %d\n", info->div_single_curr[CHG_MODE_DIV1],
		info->div_single_curr[CHG_MODE_DIV2], info->div_single_curr[CHG_MODE_DIV4]);
	ret = mca_parse_dts_u32_array(node, "buck_icl_fcc_curr", curr_data, 2);
	if (ret) {
		info->buck_icl_fcc_curr[0] = MCA_QUICK_CHG_BUCK_ICL_CURR_TH;
		info->buck_icl_fcc_curr[1] = MCA_QUICK_CHG_BUCK_FCC_CURR_TH;
	} else {
		memcpy(info->buck_icl_fcc_curr, curr_data, sizeof(curr_data));
	}
#line 4576
	mca_log_info("buck_icl_fcc_curr %d %d\n", info->buck_icl_fcc_curr[0],
		info->buck_icl_fcc_curr[1]);
	ret = mca_parse_dts_u32_array(node, "div_max_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_max_curr[CHG_MODE_DIV1] = info->div_single_curr[CHG_MODE_DIV1];
		info->div_max_curr[CHG_MODE_DIV2] = info->div_single_curr[CHG_MODE_DIV2];
		info->div_max_curr[CHG_MODE_DIV4] = info->div_single_curr[CHG_MODE_DIV4];
	} else {
		memcpy(info->div_max_curr, idata, sizeof(idata));
	}
#line 4540
	mca_log_info("div_delta_volt %d %d %d\n", info->div_max_curr[CHG_MODE_DIV1],
		info->div_max_curr[CHG_MODE_DIV2], info->div_max_curr[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "multi_ibus_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->multi_ibus_th[CHG_MODE_DIV1] = MCA_QUICK_CHG_IBUS_TH_DEFAULT;
		info->multi_ibus_th[CHG_MODE_DIV2] = MCA_QUICK_CHG_IBUS_TH_DEFAULT;
		info->multi_ibus_th[CHG_MODE_DIV4] = MCA_QUICK_CHG_IBUS_TH_DEFAULT;
	} else {
		memcpy(info->multi_ibus_th, idata, sizeof(idata));
	}
#line 4610
	mca_log_info("multi_ibus_th %d %d %d\n", info->multi_ibus_th[CHG_MODE_DIV1],
		info->multi_ibus_th[CHG_MODE_DIV2], info->multi_ibus_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_inc_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_inc_hysteresis[CHG_MODE_DIV1] = MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_inc_hysteresis[CHG_MODE_DIV2] = MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_inc_hysteresis[CHG_MODE_DIV4] = MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
	} else {
		memcpy(info->ibus_inc_hysteresis, idata, sizeof(idata));
	}
#line 4632
	mca_log_info("ibus_inc_hysteresis %d %d %d\n", info->ibus_inc_hysteresis[CHG_MODE_DIV1],
		info->ibus_inc_hysteresis[CHG_MODE_DIV2], info->ibus_inc_hysteresis[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_dec_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_dec_hysteresis[CHG_MODE_DIV1] = MCA_QUICK_CHG_IBUS_DEC_HYS_DEFAULT;
		info->ibus_dec_hysteresis[CHG_MODE_DIV2] = MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_dec_hysteresis[CHG_MODE_DIV4] = MCA_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
	} else {
		memcpy(info->ibus_dec_hysteresis, idata, sizeof(idata));
	}
#line 4643
	mca_log_info("ibus_dec_hysteresis %d %d %d\n", info->ibus_dec_hysteresis[CHG_MODE_DIV1],
		info->ibus_dec_hysteresis[CHG_MODE_DIV2], info->ibus_dec_hysteresis[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "open_path_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->open_path_th[CHG_MODE_DIV1] = MCA_QUICK_CHG_OPEN_PATH_IBUS_TH;
		info->open_path_th[CHG_MODE_DIV2] = MCA_QUICK_CHG_OPEN_PATH_IBUS_TH;
		info->open_path_th[CHG_MODE_DIV4] = MCA_QUICK_CHG_OPEN_PATH_IBUS_TH;
	} else {
		memcpy(info->open_path_th, idata, sizeof(idata));
	}
#line 4654
	mca_log_info("open_path_th %d %d %d\n", info->open_path_th[CHG_MODE_DIV1],
		info->open_path_th[CHG_MODE_DIV2], info->open_path_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_compensation", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_compensation[CHG_MODE_DIV1] = MCA_QUICK_CHG_DEFAULT_IBUS_COMPENSATION;
		info->ibus_compensation[CHG_MODE_DIV2] = MCA_QUICK_CHG_DEFAULT_IBUS_COMPENSATION;
		info->ibus_compensation[CHG_MODE_DIV4] = MCA_QUICK_CHG_DEFAULT_IBUS_COMPENSATION;
	} else {
		memcpy(info->ibus_compensation, idata, sizeof(idata));
	}
#line 4665
	mca_log_info("ibus_compensation: %d %d %d\n", info->ibus_compensation[CHG_MODE_DIV1],
		info->ibus_compensation[CHG_MODE_DIV2], info->ibus_compensation[CHG_MODE_DIV4]);
}

static void mca_quick_charge_parse_vfc_info(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, col, len;
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
		col = row * MCA_QUICK_CHG_VFC_PARA_SIZE + MCA_QUICK_CHG_VFC_PARA_IOUT;
		info->vfc_para.iout_fsw_map[row].iout = data[col];
		col = row * MCA_QUICK_CHG_VFC_PARA_SIZE + MCA_QUICK_CHG_VFC_PARA_FSW;
		info->vfc_para.iout_fsw_map[row].fsw = data[col];
	}
}

static int mca_quick_charge_parse_dt(struct mca_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int ret;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	/* basic quick charge para */
	(void)mca_parse_dts_u32(node, "batt_type", &info->batt_type, 0);
	(void)mca_parse_dts_u32(node, "cp_type", &info->cp_type, 0);
	(void)mca_parse_dts_u32(node, "en_buck_parallel_chg",
		&info->en_buck_parallel_chg, 0);
	(void)mca_parse_dts_u32(node, "fv_hys_delta_mv",
		&info->fv_hys_delta_mv, 0);
	(void)mca_parse_dts_u32(node, "curr_terminate_ratio",
		&info->curr_terminate_ratio, 110);
	(void)mca_parse_dts_u32(node, "min_vbat", &info->min_vbat,
		MCA_QUICK_CHG_MIN_VBAT_DEFAULT);
	(void)mca_parse_dts_u32(node, "max_vbat", &info->max_vbat,
		MCA_QUICK_CHG_MAX_VBAT_DEFAULT);
	(void)mca_parse_dts_u32(node, "recharge_vbat_delta", &info->recharge_vbat_delta,
		MCA_QUICK_CHG_RECHARGE_VBAT_DELTA_DEFAULT);
	(void)mca_parse_dts_u32(node, "die_temp_max", &info->die_temp_max,
		MCA_QUICK_CHG_MAX_TDIE_DEFAULT);
	(void)mca_parse_dts_u32(node, "adp_temp_max", &info->adp_temp_max,
		MCA_QUICK_CHG_MAX_TADP_DEFAULT);
	(void)mca_parse_dts_u32(node, "support_mode", &info->support_mode, 0);
	(void)mca_parse_dts_u32(node, "max_power", &info->max_power, 0);
	(void)mca_parse_dts_u32(node, "ui_max_power_limit", &info->ui_max_power_limit,
		info->max_power);
	(void)mca_parse_dts_u32(node, "qc3_max_vbus_limit", &info->qc3_max_vbus_limit_mv,
		MCA_QUICK_CHG_QC3B_VBUS_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3_max_ibus_limit", &info->qc3_max_ibus_limit_ma,
		MCA_QUICK_CHG_QC3B_IBUS_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3p5_max_vbus_limit", &info->qc3p5_max_vbus_limit_mv,
		MCA_QUICK_CHG_QC3P5_VBUS_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3p5_max_ibus_limit", &info->qc3p5_max_ibus_limit_ma,
		MCA_QUICK_CHG_QC3P5_IBUS_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3_ibat_max_limit", &info->qc3_ibat_max_limit_ma,
		MCA_QUICK_CHG_QC3_IBAT_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3p5_ibat_max_limit", &info->qc3p5_max_ibat_limit_ma,
		MCA_QUICK_CHG_QC3_IBAT_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc_normal_charge_fv", &info->qc_normal_charge_fv_max_mv,
		MCA_QUICK_CHG_QC_FV_LIMIT_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc_taper_fcc_thr", &info->qc_taper_fcc_ma,
		MCA_QUICK_CHG_QC_TAPER_FCC_THR_DEFAULT);
	(void)mca_parse_dts_u32(node, "pps_taper_fcc_thr", &info->pps_taper_fcc_ma,
		MCA_QUICK_CHG_QC_TAPER_FCC_THR_DEFAULT);
	(void)mca_parse_dts_u32(node, "pps_high_taper_fcc_thr", &info->pps_high_taper_fcc_ma,
		MCA_QUICK_CHG_QC_TAPER_FCC_THR_DEFAULT);
	(void)mca_parse_dts_u32(node, "qc3_taper_vol_hys", &info->qc3_taper_vol_hys,
		MCA_QUICK_CHG_QC_TAPER_HYS_QC3B);
	(void)mca_parse_dts_u32(node, "qc3p5_taper_vol_hys", &info->qc3p5_taper_vol_hys,
		MCA_QUICK_CHG_QC_TAPER_HYS_QC3P5);
	(void)mca_parse_dts_u32(node, "pps_taper_vol_hys", &info->pps_taper_vol_hys,
		MCA_QUICK_CHG_PPS_TAPER_HYS);
	(void)mca_parse_dts_u32(node, "hardware_cv", &info->hardware_cv, 0);
	(void)mca_parse_dts_u32(node, "rawsoc_swith_pmic_th", &info->rawsoc_swith_pmic_th, 0);
	(void)mca_parse_dts_u32(node, "support_curr_monitor", &info->support_curr_monitor, 0);
	(void)mca_parse_dts_u32(node, "curr_monitor_time_s", &info->curr_monitor_time_s, 0);
	(void)mca_parse_dts_u32(node, "cp_switch_pmic_th", &info->cp_switch_pmic_th,
		MCA_QUICK_CHG_SWITCH_PMIC_TH);
	info->has_gbl_batt_para = of_property_read_bool(node, "has-global-batt-para");
	info->support_base_flip = of_property_read_bool(node, "support-base-flip");
	mca_quick_charge_parse_chg_mode_info(node, info);
	/* charge para */
	mca_quick_charge_parse_vstep_para(info);
	ret = mca_quick_charge_parse_batt_info(info, 0);
	if (info->support_base_flip) {
#line 4950
		mca_log_err("parse mode for paraller batt\n");
		ret = mca_quick_charge_parse_batt_info(info, info->batt_type);
	}
	mca_quick_charge_parse_vfc_info(info);

	return ret;
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

static void mca_quick_charge_init_data(struct mca_quick_charge_info *info)
{
	int i, j;

	info->cur_support_mode = info->support_mode;
	info->sysfs_data.chg_enable = 1;
	for (i = 0; i < CHG_MODE_MAX; i++) {
		info->sysfs_data.mode_enable[i] = 1;
		for (j = 0; j < CP_ROLE_MAX; j++)
			info->sysfs_data.cp_path_enable[i][j] = 1;
	}
}

static int mca_quick_charge_shutdown(struct notifier_block *nb, unsigned long action,
	void *data)
{
	struct mca_quick_charge_info *info = container_of(nb, struct mca_quick_charge_info, shutdown_notifier);
	struct mca_event_notify_data n_data = { 0 };

#line 5423
	mca_log_err("receive shutdown\n");

	if (info->online)
		mca_vote(info->chg_disable_voter, "shutdown", true, 1);

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

static struct mca_smartchg_if_ops g_quickchg_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_QC,
	.data = NULL,
	.set_delta_fv = strategy_quickchg_smartchg_delta_fv_callback,
	.set_delta_ichg = strategy_quickchg_smartchg_delta_ichg_callback,
	.set_soc_limit_sts = strategy_quickchg_smartchg_soc_limit_callback,
	.update_baa_para = strategy_quickchg_smartchg_update_baa_para,
	.set_pwr_boost_sts = strategy_quickchg_smartchg_pwr_boost_sts_callback,
};

static int mca_quick_charge_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct mca_quick_charge_info *info;

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
	ret = mca_quick_charge_create_voter(info);
	if (ret)
		return -EPROBE_DEFER;
	mca_quick_charge_init_data(info);
	mca_quick_charge_create_group(info->dev);

	INIT_DELAYED_WORK(&info->monitor_work, mca_quick_charge_monitor_work);
	INIT_DELAYED_WORK(&info->pps_ptf_work, mca_quick_charge_pps_ptf_work);
	INIT_DELAYED_WORK(&info->vfc_work, mca_quick_charge_vfc_work);
	INIT_DELAYED_WORK(&info->float_vbat_drop_work, mca_quick_charge_fvbat_drop_work);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
		mca_quick_charge_process_event, mca_quick_charge_get_status, NULL, info);
	g_strategy_quickchg_if_ops.data = info;
	mca_charge_if_ops_register(&g_strategy_quickchg_if_ops);
	g_quickchg_smartchg_if_ops.data	= info;
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

