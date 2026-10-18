// SPDX-License-Identifier: GPL-2.0
/*
 *mca_quick_wireless.c
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"
#include "mca_quick_wireless.h"
#include <mca/platform/platform_loadsw_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_quick_wireless"
#endif

#ifndef BIT
#define BIT(n) (1 << (n))
#endif

#define MCA_DTPT_MOLECULE_SCALE 8
#define MCA_DTPT_DENOM_SCALE 10

static void mca_wireless_quick_charge_startup(struct mca_wireless_quick_charge_info *info);
static void mca_wireless_quick_charge_force_stop_charging(struct mca_wireless_quick_charge_info *info);

static void mca_wireless_quick_charge_icl_setting(struct mca_wireless_quick_charge_info *info, bool en, int mA)
{
#line 69
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (info->proc_data.magnet_limit && info->audio_phone_sts && info->support_hall) {
#line 72
		mca_log_info("magnet tx work around, not set icl, magnet limit = %d, audio_phone_sts = %d",
			info->proc_data.magnet_limit, info->audio_phone_sts);
		return;
	}

	if (en)
		mca_vote(info->input_limit_voter, "wireless_qc", true, mA);
	else
		mca_vote(info->input_limit_voter, "wireless_qc", false, 0);
}

/* G13e: the stock keeps one out-of-line copy (called from start_charging) */
static noinline int mca_wireless_quick_charge_vout_setting(struct mca_wireless_quick_charge_info *info, int mV)
{
	int ret = 0;
	if (info->proc_data.magnet_limit && info->audio_phone_sts && info->support_hall) {
#line 97
		mca_log_info("magnet tx work around, not set vout, magnet limit = %d, audio_phone_sts = %d",
			info->proc_data.magnet_limit, info->audio_phone_sts);
		return ret;
	}

	ret = platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, mV);
	return ret;
}

static void mca_wireless_quick_charge_switch_bridge_icl_setting(struct mca_wireless_quick_charge_info *info, bool en, int mA)
{
#line 85
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "qc_sw_bridge", true, mA);
	else
		mca_vote(info->input_limit_voter, "qc_sw_bridge", false, 0);
}

/*
 * G13e: recovered from the OS4 disassembly (stock function at .text+0x2efc, 636 bytes), NOT hardware-tested.
 * Only runs on platform_version 5 (the first word of struct mca_hwid).  The thresholds 9500/6500 mV and 400 mA, the 15 x 200 ms wait,
 * the 10000/6000 mV rx vout and the 300 mA icl cap come from the stock immediates.  The polarity of the brg_status test
 * (switch only when the reported status EQUALS the requested mode, else "no need switch") is as in the stock branch, not understood.
 */
static noinline void mca_wireless_quick_change_bridge(struct mca_wireless_quick_charge_info *info, int mode)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	u8 brg_status = 0;
	int rx_vout = 0, rx_iout = 0;
	int i;

#line 329
	mca_log_err("bridge_sw mode %d\n", mode);
	if (!hwid || hwid->platform_version != 5)
		return;

	platform_class_wireless_get_rx_brg_status(WIRELESS_ROLE_MASTER, &brg_status);
	if (brg_status != mode) {
#line 351
		mca_log_err("bridge_sw no need switch, mode %d, brg_status %d\n", mode, brg_status);
		return;
	}

#line 333
	mca_log_err("bridge_sw brg_status %d\n", brg_status);
	mca_wireless_quick_charge_vout_setting(info, mode ? 10000 : 6000);
	mca_wireless_quick_charge_switch_bridge_icl_setting(info, true, 300);
	for (i = 15; i > 0; i--) {
		msleep(200);
		platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout);
		platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
#line 340
		mca_log_err("bridge_sw wait, rx_vout %d, rx_iout %d \n", rx_vout, rx_iout);
		if ((mode && rx_vout >= 9500 && rx_iout <= 400) || (!mode && rx_vout <= 6500 && rx_iout <= 400)) {
			platform_class_wireless_switch_bridge(WIRELESS_ROLE_MASTER, mode);
			msleep(20);
			platform_class_wireless_get_rx_brg_status(WIRELESS_ROLE_MASTER, &brg_status);
			break;
		}
	}
	mca_wireless_quick_charge_switch_bridge_icl_setting(info, false, 0);
#line 349
	mca_log_err("bridge_sw switch_bridge over, brg_status %d\n", brg_status);
}

static int mca_wireless_quick_charge_thermal_flip_voter_cb(struct mca_votable *votable, void *data, int effective_result, const char *client)
{
	struct mca_wireless_quick_charge_info *info = data;

	if (!info)
		return -1;

#line 2519
	mca_log_info("target wls thermal flip effective_result :%d\n", effective_result);
	info->wls_thermal_flip_cur = effective_result;
	return 0;
}

static int mca_wireless_quick_charge_msleep(int ms, struct mca_wireless_quick_charge_info *info)
{
	int i, count;

	count = ms / 10;

	for (i = 0; i < count; i++) {
		if (!info->online || info->force_stop ||
		(info->proc_data.magnet_limit && info->audio_phone_sts && info->support_hall))
			return -1;
		usleep_range(9900, 11000);
	}

	return 0;
}

static int mca_wireless_quickchg_if_set_chg_cur(const char *user,
	char *value, void *data)
{
	int single_cur = 0, multi_cur = 0;
	struct mca_wireless_quick_charge_info *info = data;

	if (!user || !value || !data)
		return -1;

	if (sscanf(value, "%d#%d", &single_cur, &multi_cur) != 2)
		return -1;

#line 134
	mca_log_err("single: %d, multi: %d\n", single_cur, multi_cur);
	(void)mca_vote(info->single_chg_cur_voter, user, true, single_cur);
	(void)mca_vote(info->multi_chg_cur_voter, user, true, multi_cur);

	return 0;
}

static int mca_wireless_quickchg_if_get_chg_cur(char *buf, void *data)
{
	const char *single_client;
	const char *multi_client;
	int single_cur, multi_cur;
	struct mca_wireless_quick_charge_info *info = data;

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

static struct mca_charge_if_ops g_quick_wireless_if_ops = {
	.type_name = "wl_quick",
	.set_charge_current_limit = mca_wireless_quickchg_if_set_chg_cur,
	.get_charge_current_limit = mca_wireless_quickchg_if_get_chg_cur,
};

static int mca_wireless_quick_charge_update_vbat(struct mca_wireless_quick_charge_info *info)
{
	int ret;

	ret = strategy_class_fg_ops_get_voltage(&info->proc_data.vbat[FG_IC_MASTER]);
	if (ret)
		return -1;
	ret = platform_fg_ops_get_volt(FG_IC_MASTER, &info->proc_data.parall_vbat[FG_IC_BASE]);
	if (ret)
		return -1;
	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		ret = platform_fg_ops_get_volt(FG_IC_SLAVE, &info->proc_data.parall_vbat[FG_IC_FLIP]);
		if (ret)
			return -1;
	}

	return 0;
}

static int mca_wireless_quick_charge_update_ibat(struct mca_wireless_quick_charge_info *info)
{
	int ret;
	ret = strategy_class_fg_ops_get_current(&info->proc_data.ibat[FG_IC_MASTER]);
	if (ret)
		return -1;
	ret = platform_fg_ops_get_curr(FG_IC_MASTER, &info->proc_data.parall_ibat[FG_IC_BASE]);
	if (ret)
		return -1;
	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
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

static int mca_wireless_quick_charge_update_ibus(struct mca_wireless_quick_charge_info *info)
{
	int ret;
	int ibus[CP_ROLE_MAX] = { 0 };

	if (info->proc_data.cur_work_cp == MCA_WLS_QUICK_CHG_CP_DUAL) {
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

#line 238
	mca_log_info("ibus %d %d\n", ibus[0], ibus[1]);
	info->proc_data.ibus = ibus[CP_ROLE_MASTER] + ibus[CP_ROLE_SLAVE];

	return 0;
}

#define OVER_VOLATGE_COUNT 3
#define OVER_VOLATGE_DEC_FCC_MA 1000
static int mca_wireless_quick_charge_get_secure_cur(struct mca_wireless_quick_charge_info *info)
{
	int vbat = info->proc_data.vbat[FG_IC_MASTER];
	int ibat = info->proc_data.ibat[FG_IC_MASTER];
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int vbat_th = info->proc_data.temp_max_fv[FG_IC_MASTER];
	int ibat_min = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[volt_para_size - 1].current_min;
	int delta_fv = info->smartchg_data.delta_fv;
	static int over_volt_count;
	int secure_cur = 0;
	int max_vbat = vbat_th - delta_fv;

	if (vbat >= max_vbat)
		over_volt_count++;
	else
		over_volt_count = 0;

	if (over_volt_count > OVER_VOLATGE_COUNT) {
		if (!info->proc_data.secure_cur)
			info->proc_data.secure_cur = ibat;
		info->proc_data.secure_cur -= OVER_VOLATGE_DEC_FCC_MA;
		info->proc_data.secure_cur = max(info->proc_data.secure_cur, ibat_min);
		over_volt_count = 0;
#line 269
		mca_log_err("trigger over voltage secure_cur %d\n", info->proc_data.secure_cur);
	}

#line 272
	mca_log_info("max_vbatt:%d delta_fv:%d secure_cur:%d\n", max_vbat, delta_fv, info->proc_data.secure_cur);
	secure_cur = info->proc_data.secure_cur;
	return secure_cur;
}

static int mca_wireless_quick_charge_smartchg_baa_update_volt_info(
	struct mca_wireless_quick_charge_temp_para_info *qc_temp_para, struct smart_batt_spec *pbatt_spec)
{
	struct mca_wireless_quick_charge_volt_para_info *volt_info = NULL;
	struct mca_wireless_quick_charge_volt_para *old_volt_para = NULL, *new_volt_para = NULL;

	if (pbatt_spec->ffc)
		volt_info = &qc_temp_para->volt_ffc_info;
	else
		volt_info = &qc_temp_para->volt_info;

#line 288
	mca_log_info("wireless para type: %d, ffc: %d, idx: %d, size: %d => %d, temp: %d:%d => %d:%d\n",
		pbatt_spec->type, pbatt_spec->ffc, pbatt_spec->t_range.idx,
		volt_info->volt_para_size, pbatt_spec->step_size,
		qc_temp_para->temp_para.temp_low, qc_temp_para->temp_para.temp_high,
		pbatt_spec->t_range.min, pbatt_spec->t_range.max);

	new_volt_para = kcalloc(pbatt_spec->step_size, sizeof(*new_volt_para), GFP_KERNEL);
	if (!new_volt_para) {
#line 296
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}

	for (int i = 0; i < volt_info->volt_para_size; i++) {
#line 301
		mca_log_debug("volt_para[%d] voltage: %d, current_max: %d, current_min: %d\n",
			i, volt_info->volt_para[i].voltage,
			volt_info->volt_para[i].current_max, volt_info->volt_para[i].current_min);
	}

	for (int i = 0; i < pbatt_spec->step_size; i++) {
		new_volt_para[i].voltage = pbatt_spec->steps[i].mv;
		new_volt_para[i].current_max = pbatt_spec->steps[i].ma_h;
		new_volt_para[i].current_min = pbatt_spec->steps[i].ma_l;
#line 310
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
static int mca_wireless_quick_charge_smartchg_update_baa_para(void *data, char *baa_para, int ffc_size, int normal_size)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;
	struct smart_batt_spec *pbatt_spec = NULL;
	struct smart_batt_spec_curve curve[MCA_QUICK_CHARGE_MAX_VOLT_STEP_SIZE] = {0};
	int offset = 0;

	if (!data || !baa_para) {
#line 364
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
#line 378
			mca_log_err("temp_para_size: %d, temp_range.idx: %d is invalid\n",
				pbatt_spec->t_range.idx >= info->batt_para[FG_IC_MASTER].temp_para_size, pbatt_spec->t_range.idx);
		} else {
			mca_wireless_quick_charge_smartchg_baa_update_volt_info(
				&(info->batt_para[FG_IC_MASTER].temp_info[pbatt_spec->t_range.idx]), pbatt_spec);
		}
	}

	return 0;
}

static void mca_wireless_quick_charge_stop_charging(struct mca_wireless_quick_charge_info *info)
{
	int wls_plugin = 0;
	int chgr_stat = MCA_BATT_CHGR_STATUS_CHARGING_DISABLED;
	int effective_fcc_val = 0;
	int vbat_bq_mv = 0;
	int pmic_icl_target = 0, pmic_icl_temp = 0, temp = 0;

	info->proc_data.enable_quickchg = 0;

	platform_class_cp_get_battery_voltage(info->proc_data.cur_work_cp, &vbat_bq_mv);
	platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
	platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
	strategy_class_wireless_ops_set_parallel_charge(false);
	platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wls_plugin);
	if (wls_plugin) {
		strategy_wireless_enable_cp_error_irq(false);
		mca_wireless_quick_change_bridge(info, 0);
		mca_wireless_quick_charge_vout_setting(info, MCA_WLS_QUICK_CHG_EXIT_VOUT);
		msleep(200);

		if (info->thermal_phone_flag && vbat_bq_mv) {
			effective_fcc_val = mca_get_effective_result(info->charge_limit_voter);
			pmic_icl_target = (effective_fcc_val + 100) * vbat_bq_mv / MCA_WLS_QUICK_CHG_EXIT_VOUT;

			for (pmic_icl_temp = MCA_WLS_QUICK_CHG_EXIT_ICL; pmic_icl_temp < pmic_icl_target; ) {
				(void)mca_vote(info->input_limit_voter, "thermal_phone", true, pmic_icl_temp);
				msleep(100);
				pmic_icl_temp += 100;
			}
			(void)mca_vote(info->input_limit_voter, "thermal_phone", true, pmic_icl_target);
		}
	}

	platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);
	if (info->cp_type == MCA_CP_TYPE_PARALLEL)
		platform_class_cp_set_mode(CP_ROLE_SLAVE, CP_MODE_FORWARD_2_1);

	(void)mca_vote(info->input_limit_voter, "wireless_qc", false, 0);
	(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
	(void)platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
	memset(info->proc_data.cur_stage, 0, sizeof(info->proc_data.cur_stage));

	strategy_class_fg_ops_get_temperature(&temp);
	temp /= 10;
	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_stat);
	if (wls_plugin && (!info->soc_limit_enable) && temp > -10 &&
		(chgr_stat == MCA_BATT_CHGR_STATUS_TERMINATION || chgr_stat == MCA_BATT_CHGR_STATUS_CHARGING_DISABLED)) {
#line 443
		mca_log_info("chgr_stat: %d, disable/enable pmic\n", chgr_stat);
		platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, false);
		platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, true);
	}
	if (info->proc_data.charge_flag_pre == MCA_QUICK_CHG_STS_CHARGE_DONE) {
		info->proc_data.charge_flag = info->proc_data.charge_flag_pre;
		info->proc_data.charge_flag_pre = MCA_QUICK_CHG_STS_NO_CHARGING;
	} else
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_NO_CHARGING;
#line 455
	mca_log_info("stop quick charge\n");
}

#define OVER_CURRENT_COUNT 3
#define BELOW_CURRENT_COUNT 3
#define BASE_FCC_MA_HYS 100
#define BASE_BELOW_FCC_MA_HYS 500
static int mca_wireless_quick_get_parallel_volt_para(struct mca_wireless_quick_charge_info *info, int fg_index)
{
	struct mca_wireless_quick_charge_batt_para_info *base_flip_para = &(info->base_flip_para[fg_index]);
	struct mca_wireless_quick_charge_temp_para *temp_para;
	int i = 0;
	int ret = 0, temp = 0;

	ret = platform_fg_ops_get_temp(fg_index, &temp);
	if (ret) {
#line 471
		mca_log_err("get flip batt temp fail\n");
	}
	temp /= 10;
#line 474
	mca_log_info("fg[%d]_temp = %d\n", fg_index, temp);
	for (i = 0; i < base_flip_para->temp_para_size; i++) {
		temp_para = &base_flip_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			break;
		}
	}

	if (base_flip_para->temp_info[i].volt_ffc_info.volt_para_size) {
#line 483
		mca_log_info("select ffc volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_ffc_info;
	} else if (base_flip_para->temp_info[i].volt_info.volt_para_size) {
#line 486
		mca_log_info("select normal volt para\n");
		info->proc_data.cur_volt_paraller[fg_index] = &base_flip_para->temp_info[i].volt_info;
	} else {
#line 489
		mca_log_info("no volt para\n");
		return -1;
	}
	return ret;
}

static int mca_wireless_quick_charger_get_base_limit_cur(struct mca_wireless_quick_charge_info *info, int cur_max)
{
	struct mca_wireless_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = proc_data->cur_stage[FG_IC_BASE];
	int ibat = info->proc_data.parall_ibat[FG_IC_BASE];
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

#line 521
	mca_log_err("runSwOcp:%d cur_base_stage:%d base_ibat:%d base_cur_max:%d over_curr_count:%d cur_max:%d final_cur:%d\n",
		runSwOcp, cur_stage, ibat, base_cur_max, over_curr_count, cur_max, final_curr);
	return final_curr;
}

static int mca_wireless_quick_charger_set_flip_cur(struct mca_wireless_quick_charge_info *info)
{
	struct mca_wireless_quick_charge_process_data *proc_data = &(info->proc_data);
	int cur_stage = proc_data->parall_cur_stage[FG_IC_FLIP];
	int ibat = info->proc_data.parall_ibat[FG_IC_FLIP];
	int flip_curr_max = 0;

	if (!proc_data->cur_volt_paraller[FG_IC_FLIP]->volt_para)
		return 0;
	flip_curr_max = proc_data->cur_volt_paraller[FG_IC_FLIP]->volt_para[cur_stage / 2].current_max;
	platform_class_loadsw_set_ibat_limit(LOADSW_ROLE_MASTER, flip_curr_max);
	mca_log_err("flip_curr_max %d, cur_stage %d, ibat_slave %d\n", flip_curr_max, cur_stage, ibat);

	return 0;
}

static int mca_wireless_quick_charge_select_max_ibat(struct mca_wireless_quick_charge_info *info)
{
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int cur_max = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_max;
	int tx_adapter_max = info->proc_data.wls_power.max_fcc;
	int channel_cur;
	int delta_cur = info->smartchg_data.delta_ichg;
	int secure_cur = mca_wireless_quick_charge_get_secure_cur(info);
	int base_limit_curr;

#line 572
	mca_log_err("cur_max:[a] %d delta_cur %d\n", cur_max, delta_cur);
	if (info->proc_data.debug_qc_ichg > 0 && info->proc_data.debug_qc_ichg <= tx_adapter_max) {
		cur_max = info->proc_data.debug_qc_ichg;
#line 575
		mca_log_err("wls debug fcc = %d\n", cur_max);
		if (cur_max <= MCA_WLS_QUICK_CHG_EXIT_FCC) {
			mca_log_err("fcc less than 2.2A, stop quick charging = %d\n", cur_max);
			mca_wireless_quick_charge_stop_charging(info);
		}
		return cur_max;
	}

	if (info->dtpt_status)
		cur_max = cur_max * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE;
	else
		cur_max = cur_max - delta_cur;

	if (info->proc_data.cur_work_cp != MCA_WLS_QUICK_CHG_CP_DUAL)
		channel_cur = info->proc_data.single_curr;
	else
		channel_cur = info->proc_data.max_curr;

	if (secure_cur)
		cur_max = min(cur_max, secure_cur);

#line 592
	mca_log_info("cur_max:[b]: %d\n", cur_max);

	if (info->proc_data.sw_cv_cur)
		cur_max = min(cur_max, info->proc_data.sw_cv_cur);
	if (info->proc_data.sw_thermal_ichg)
		cur_max = min(cur_max, info->proc_data.sw_thermal_ichg);

#line 599
	mca_log_info("cur_max:[c]: %d\n", cur_max);

	cur_max = min(cur_max, channel_cur);
	cur_max = min(cur_max, info->proc_data.temp_max_cur[FG_IC_MASTER]);
	cur_max = min(cur_max, tx_adapter_max);
	cur_max = min(cur_max, info->proc_data.sw_qc_ichg);
	if (info->support_base_flip) {
		mca_wireless_quick_get_parallel_volt_para(info, FG_IC_BASE);
		mca_wireless_quick_get_parallel_volt_para(info, FG_IC_FLIP);
		base_limit_curr = mca_wireless_quick_charger_get_base_limit_cur(info, cur_max);
		if (base_limit_curr)
			cur_max = min(cur_max, base_limit_curr);
		mca_wireless_quick_charger_set_flip_cur(info);
#line 610
		mca_log_info("support_base_flip:[]: %d\n", info->support_base_flip);
	}

#line 613
	mca_log_info("[channel_cur:%d], [temp_max_cur:%d], [tx_adapter_max:%d], [sw_qc_ichg:%d],[sw_thermal_ichg:%d]\n",
				channel_cur, info->proc_data.temp_max_cur[FG_IC_MASTER], tx_adapter_max, info->proc_data.sw_qc_ichg, info->proc_data.sw_thermal_ichg);
#line 615
	mca_log_info("cur_max:[Final]: %d\n", cur_max);
	return cur_max;
}

static void mca_wireless_quick_charge_exit_charging(struct mca_wireless_quick_charge_info *info)
{
	mca_wireless_quick_charge_force_stop_charging(info);
	memset(&info->proc_data, 0, sizeof(info->proc_data));
}

static int mca_wireless_quick_charge_parse_batt_info(struct mca_wireless_quick_charge_info *info, int mode);

/* OS4 (base/flip phones): pick the battery parameter row from the battery cycle count of both cells; the statics are named as in the stock
 * (update_batt_para.curr_batt_index.N / .last_batt_index.N).  Thresholds read from the compare constants 0x33,0x65,0x97,0x12d,0x321 of the stock
 * (cc > 50, 100, 150, 300, 800); the exact loop shape is a GUESS (the stock is unrolled). */
static void update_batt_para(struct mca_wireless_quick_charge_info *info)
{
	static int curr_batt_index[FG_IC_MAX];
	static int last_batt_index[FG_IC_MAX];
	static const int cycle_th[] = { 50, 100, 150, 300, 800 };
	int cc[FG_IC_MAX] = { 0 };
	int i;

	platform_fg_ops_get_cyclecount(FG_IC_SLAVE, &cc[FG_IC_SLAVE]);
	platform_fg_ops_get_cyclecount(FG_IC_MASTER, &cc[FG_IC_MASTER]);
#line 635
	mca_log_err("base_flip_cc, update_batt_para, master:%d, slave:%d\n", cc[FG_IC_MASTER], cc[FG_IC_SLAVE]);

	curr_batt_index[FG_IC_SLAVE] = 0;
	info->curr_batt_cycle[FG_IC_SLAVE] = 0;
	curr_batt_index[FG_IC_MASTER] = 0;
	info->curr_batt_cycle[FG_IC_MASTER] = 0;
	for (i = 0; i < ARRAY_SIZE(cycle_th); i++) {
		if (cc[FG_IC_SLAVE] > cycle_th[i]) {
			curr_batt_index[FG_IC_SLAVE] = i + 1;
			info->curr_batt_cycle[FG_IC_SLAVE] = cycle_th[i];
		}
		if (cc[FG_IC_MASTER] > cycle_th[i]) {
			curr_batt_index[FG_IC_MASTER] = i + 1;
			info->curr_batt_cycle[FG_IC_MASTER] = cycle_th[i];
		}
	}

	if (last_batt_index[FG_IC_SLAVE] != curr_batt_index[FG_IC_SLAVE] ||
	    last_batt_index[FG_IC_MASTER] != curr_batt_index[FG_IC_MASTER]) {
		mca_wireless_quick_charge_parse_batt_info(info, info->batt_type);
#line 652
		mca_log_err("base_flip_cc, update cc data\n");
#line 657
		mca_log_err("base_flip_cc, curr_batt_index_master:%d, curr_batt_index_slave:%d\n",
			curr_batt_index[FG_IC_MASTER], curr_batt_index[FG_IC_SLAVE]);
		last_batt_index[FG_IC_SLAVE] = curr_batt_index[FG_IC_SLAVE];
		last_batt_index[FG_IC_MASTER] = curr_batt_index[FG_IC_MASTER];
	}
}

static int mca_wireless_quick_charge_process_event(int event, int value, void *data)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;

	if (!data)
		return -1;

#line 669
	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
		info->online = 1;
		if (info->support_base_flip)
			update_batt_para(info);
		break;
	case MCA_EVENT_WIRELESS_DISCONNECT:
		if (!value) {
#line 678
			mca_log_info("stop quick charge\n");
			info->online = 0;
			info->charge_abnormal = false;
			mca_wireless_quick_charge_exit_charging(info);
		}
		break;
	case MCA_EVENT_CHARGE_ACTION:
		info->proc_data.qc_enable = value;
		mca_wireless_quick_charge_startup(info);
		break;
	case MCA_EVENT_WIRELESS_SW_SET_QC_ICHG:
		info->proc_data.sw_qc_ichg = value;
		break;
	case MCA_EVENT_WIRELESS_SW_SET_THERMAL_ICHG:
		info->proc_data.sw_thermal_ichg = value;
		break;
	case MCA_EVENT_WIRELESS_WLS_DEBUG:
		info->proc_data.debug_qc_ichg = value;
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
		info->batt_auth = 1;
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
#line 708
		mca_log_info("batt-btb change event %d\n", value);
		mca_vote(info->chg_disable_voter, "batt_miss", true, value);
		break;
	case MCA_EVENT_BATTERY_DTPT:
		info->dtpt_status = value;
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		info->charge_abnormal = true;
#line 717
		mca_log_info("adsp crash, stop quick charge\n");
		if (info->proc_data.enable_quickchg)
			mca_wireless_quick_charge_force_stop_charging(info);
		break;
	case MCA_EVENT_CHARGE_RESTORE:
#line 722
		mca_log_info("adsp restore, going quick charge\n");
		info->charge_abnormal = false;
		break;
	case MCA_EVENT_WIRELESS_MAGNETIC_TX_F2:
		if (info->proc_data.enable_quickchg) {
#line 727
			mca_log_info("magnet tx at f2, stop quick charge\n");
			cancel_delayed_work_sync(&info->monitor_work);
			mca_wireless_quick_charge_stop_charging(info);
		}
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value) {
			info->charge_abnormal = true;
#line 735
			mca_log_info("vbat ovp triggered, stop quick charge\n");
			if (info->proc_data.enable_quickchg)
				mca_wireless_quick_charge_force_stop_charging(info);
		} else {
#line 739
			mca_log_info("vbat ovp cleared, going quick charge\n");
			info->charge_abnormal = false;
		}
		break;
	case MCA_EVENT_IBAT_OCP_CHANGE:
		break;
	case MCA_EVENT_WIRELESS_AUDIO_PHONE_STS:
		info->audio_phone_sts = value;
		if (info->proc_data.enable_quickchg) {
			if (info->proc_data.magnet_limit && info->audio_phone_sts && info->support_hall) {
#line 749
				mca_log_info("magnet tx with phone, stop quick charge %d\n", value);
				cancel_delayed_work_sync(&info->monitor_work);
				mca_wireless_quick_charge_stop_charging(info);
			}
		}
		break;
	case MCA_EVENT_WIRELESS_THERMAL_PHONE_FLAG:
#line 754
		mca_log_info("thermal_phone_flag: %d\n", value);
		info->thermal_phone_flag = value;
		break;
	default:
		break;
	}
	return 0;
}

static int mca_wireless_quick_charge_jump_stage(int stage,
	struct mca_wireless_quick_charge_volt_para_info *volt_info)
{
	if (!volt_info->stage_para)
		return stage;

	if (stage >= volt_info->volt_para_size * 2)
		return stage - 1;

	if (volt_info->stage_para[stage])
		return mca_wireless_quick_charge_jump_stage(stage + 1, volt_info);

	return stage;
}

static int mca_wireless_quick_charge_pre_regulate_vol(struct mca_wireless_quick_charge_info *info)
{
	int cur_max = 0, icl_setting = 0;
	int vbat_bq_mv = 0, rx_vout = 0;
	int i;
	int ret = 0;

	//need to optimization
	cur_max = mca_wireless_quick_charge_select_max_ibat(info);
	if (cur_max <= MCA_WLS_QUICK_CHG_EXIT_FCC) {
#line 791
		mca_log_info("fcc <= 2.2A, stop quick charging, cur_max: %d\n", cur_max);
		return 1;
	}

	icl_setting = mca_get_effective_result(info->input_limit_voter);

	if (cur_max >= MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_A) {
		info->proc_data.startup_icl_target_ma = MCA_WLS_QUICK_CHG_STARTUP_BIG_ICL_MA;
		info->proc_data.startup_init_up_mv = MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_BIG;
	} else if (cur_max >= MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_B + MCA_WLS_QUICK_CHG_IBAT_THRESHOLD_B_HY) {
		info->proc_data.startup_icl_target_ma = MCA_WLS_QUICK_CHG_STARTUP_NORMAL_ICL_MA;
		info->proc_data.startup_init_up_mv = MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_NORMAL;
	} else {
		info->proc_data.startup_icl_target_ma = MCA_WLS_QUICK_CHG_STARTUP_SMALL_ICL_MA;
		info->proc_data.startup_init_up_mv = MCA_WLS_QUICK_CHG_BUS_VOLT_INIT_UP_SMALL;
	}

	while (icl_setting > info->proc_data.startup_icl_target_ma) {
		icl_setting -= 200;
		mca_wireless_quick_charge_icl_setting(info, true, icl_setting);
		if (mca_wireless_quick_charge_msleep(500, info))
			return -1;
	}
	info->proc_data.icl_setting_ma = info->proc_data.startup_icl_target_ma;
	mca_wireless_quick_charge_icl_setting(info, true, info->proc_data.startup_icl_target_ma);

	ret = platform_class_cp_get_battery_voltage(info->proc_data.cur_work_cp, &vbat_bq_mv);
	info->proc_data.cur_adp_volt = vbat_bq_mv * info->proc_data.ratio + info->proc_data.startup_init_up_mv;
	rx_vout = info->proc_data.cur_adp_volt - info->proc_data.startup_init_up_mv;
	ret = mca_wireless_quick_charge_vout_setting(info, rx_vout);
	for (i = 0; i < 10; i++) {
		if (mca_wireless_quick_charge_msleep(200, info))
			return -1;
	}

	ret = platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout);
	info->proc_data.cur_adp_curr = info->proc_data.startup_icl_target_ma;
	while (rx_vout < info->proc_data.cur_adp_volt) {
		ret = mca_wireless_quick_charge_vout_setting(info, rx_vout);
		rx_vout += 25 * MCA_WLS_QUICK_CHG_DEFAULT_VSTEP;
		if (mca_wireless_quick_charge_msleep(200, info) || ret)
			return -1;
	}
	ret = mca_wireless_quick_charge_vout_setting(info, rx_vout);

#line 843
	mca_log_info("wls_curr_limit[%d], i_adpt_ma[%d], v_adpt_mv[%d]\n",
					icl_setting, info->proc_data.cur_adp_curr, info->proc_data.cur_adp_volt);

	return ret;
}

static int mca_wireless_quick_charge_get_vbus_tune_status(struct mca_wireless_quick_charge_info *info,
	enum MCA_VBUS_TUNE_STAT *stat)
{
	int ret = 0;
	int vbus_cp_mv = 0;
	static int vbus_cp_mv_before;
	static int vbus_counts;
	int cp_vbus_hl = 0;

	*stat = MCA_VBUS_TUNE_INIT;
	info->tune_vbus_retry = 0;
	strategy_class_wireless_ops_set_parallel_charge(true);
	ret = platform_class_cp_get_bus_voltage(info->proc_data.cur_work_cp, &vbus_cp_mv);

	if (vbus_cp_mv == vbus_cp_mv_before && vbus_counts++ < 5) {
		*stat = MCA_VBUS_TUNE_WAIT;
#line 865
		mca_log_info("MCA_VBUS_TUNE_WAIT\n");
		return ret;
	}

	vbus_cp_mv_before = vbus_cp_mv;
	vbus_counts = 0;

	if (vbus_cp_mv < MCA_WLS_QUICK_CHG_VOUT_ERROR_EXIT_CV_MV) {
		*stat = MCA_VBUS_TUNE_VBUS_LOW;
#line 874
		mca_log_info("MCA_VBUS_TUNE_VBUS_LOW\n");
		return ret;
	}

	(void)platform_class_cp_get_errorhl_stat(info->proc_data.cur_work_cp, &cp_vbus_hl);
	if (cp_vbus_hl == CP_PMID_ERROR_OK) {
		*stat = MCA_VBUS_TUNE_OK;
#line 881
		mca_log_info("MCA_VBUS_TUNE_OK\n");
		return ret;
	} else if (cp_vbus_hl == CP_PMID_ERROR_LOW) {
		*stat = MCA_VBUS_TUNE_READY_UP;
#line 885
		mca_log_info("MCA_VBUS_TUNE_READY_UP\n");
		info->tune_vbus_retry++;
	} else { /* if (cp_vbus_hl == CP_PMID_ERROR_HIGH) */
		*stat = MCA_VBUS_TUNE_READY_DOWN;
#line 889
		mca_log_info("MCA_VBUS_TUNE_READY_DOWN\n");
		info->tune_vbus_retry++;
	}

	if (info->tune_vbus_retry >= MCA_WLS_QUICK_CHG_CP_VBUS_READY_COUNT) {
		info->tune_vbus_retry = 0;
		*stat = MCA_VBUS_TUNE_FAIL;
#line 896
		mca_log_err("MCA_VBUS_TUNE_FAIL\n");
		return ret;
	}

	return ret;
}

static int mca_wireless_quick_charge_tune_vbus(struct mca_wireless_quick_charge_info *info)
{
	int ret = 0;
	int i;
	int vbat_bq_mv = 0, target_vol = 0;
	int vout_setted;
	enum MCA_VBUS_TUNE_STAT status;

	for (i = 0; i < MCA_WLS_QUICK_CHG_CP_VBUS_READY_COUNT; i++) {
		ret = mca_wireless_quick_charge_get_vbus_tune_status(info, &status);
		ret = platform_class_cp_get_battery_voltage(info->proc_data.cur_work_cp, &vbat_bq_mv);
		target_vol = vbat_bq_mv * info->proc_data.ratio + info->proc_data.startup_init_up_mv;

		switch (status) {
		case MCA_VBUS_TUNE_INIT:
		case MCA_VBUS_TUNE_WAIT:
			break;
		case MCA_VBUS_TUNE_VBUS_LOW:
		case MCA_VBUS_TUNE_FAIL:
			break;
		case MCA_VBUS_TUNE_READY_UP:
			info->proc_data.cur_adp_volt += MCA_WLS_QUICK_CHG_VADPT_STEP_MV;
			if (info->proc_data.cur_adp_volt > (target_vol + 200))
				info->proc_data.cur_adp_volt = target_vol + 200;
			break;
		case MCA_VBUS_TUNE_READY_DOWN:
			info->proc_data.cur_adp_volt -= MCA_WLS_QUICK_CHG_VADPT_STEP_MV;
			if (info->proc_data.cur_adp_volt < target_vol)
				info->proc_data.cur_adp_volt = target_vol;
			break;
		case MCA_VBUS_TUNE_OK:
#line 935
			mca_log_info("adapter volt tune ok, retry %d times\n", info->tune_vbus_retry);
			break;
		default:
			break;
		}

		ret = mca_wireless_quick_charge_vout_setting(info, info->proc_data.cur_adp_volt);
		ret = platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout_setted);
		if (!ret) {
			info->proc_data.cur_adp_volt = vout_setted;
#line 945
			mca_log_info("vout_setted %d\n", vout_setted);
		}

		if (status == MCA_VBUS_TUNE_OK || ret)
			break;

		if (mca_wireless_quick_charge_msleep(200, info))
			return -1;
	}

	if (i == MCA_WLS_QUICK_CHG_CP_VBUS_READY_COUNT) {
#line 956
		mca_log_info("retry 100 times, tune vbus fail\n");
		return -1;
	}

	return ret;
}

static int mca_wireless_quick_charge_open_path(struct mca_wireless_quick_charge_info *info)
{
	int ret = 0;
	int i;
	int ibus = 0, cnt = 0;
	int vout_setted;
	int temp = 0;

	ret = platform_class_cp_enable_adc(info->proc_data.cur_work_cp, true);
	if (info->cp_type == MCA_CP_TYPE_PARALLEL) {
		(void)platform_class_cp_set_mode(CP_ROLE_MASTER, info->proc_data.cur_cp_mode);
		(void)platform_class_cp_set_mode(CP_ROLE_SLAVE, info->proc_data.cur_cp_mode);
	} else
		ret |= platform_class_cp_set_mode(CP_ROLE_MASTER, info->proc_data.cur_cp_mode);

	if (ret) {
#line 986
		mca_log_err("open path set cp mode fail\n");
		return ret;
	}

	ret = mca_wireless_quick_charge_pre_regulate_vol(info);
	if (ret) {
#line 993
		mca_log_err("pre regulate vol fail\n");
		return ret;
	}

	ret = mca_wireless_quick_charge_tune_vbus(info);
	if (ret) {
#line 999
		mca_log_err("tune vbus fail\n");
		return ret;
	}

	platform_class_cp_enable_busucp(info->proc_data.cur_work_cp, false);

	ret = platform_class_cp_set_charging_enable(info->proc_data.cur_work_cp, true);
	if (ret) {
#line 1006
		mca_log_err("open path set cp mode fail\n");
		goto out;
	}

	ret = mca_wireless_quick_charge_msleep(200, info);
	if (ret)
		goto out;

	for (i = 0; i < MCA_WLS_QUICK_CHG_OPEN_PATH_COUNT; i++) {
		if (cnt > 3)
			break;
		ret = platform_class_cp_get_bus_current(info->proc_data.cur_work_cp, &ibus);
		if (ret) {
#line 1019
			mca_log_err("open path get ibus fail\n");
			goto out;
		}
#line 1022
		mca_log_err("open path ibus %d\n", ibus);
		if (ibus > MCA_WLS_QUICK_CHG_OPEN_PATH_IBUS_TH)
			cnt++;
		else {
			ret = strategy_class_fg_ops_get_temperature(&temp);
			if (ret) {
				mca_log_err("get batt temp fail\n");
				return ret;
			}
			temp /= 10;
			mca_log_info("temp=%d\n", temp);
			if(temp > MCA_WLS_QUICK_CHG_PRE_VOLTAGE_LOW_TEMP_TH)
				info->proc_data.cur_adp_volt += (3 * MCA_WLS_QUICK_CHG_DEFAULT_VSTEP);
			else
				info->proc_data.cur_adp_volt += (5 * MCA_WLS_QUICK_CHG_DEFAULT_VSTEP);
			ret = mca_wireless_quick_charge_vout_setting(info, info->proc_data.cur_adp_volt);
			(void)platform_class_wireless_get_vout_setted(WIRELESS_ROLE_MASTER, &vout_setted);
			info->proc_data.cur_adp_volt = vout_setted;
		}

		ret |= mca_wireless_quick_charge_msleep(200, info);
		if (ret)
			goto out;
	}

	if (i == MCA_WLS_QUICK_CHG_OPEN_PATH_COUNT) {
		info->proc_data.error_num[info->proc_data.cur_work_cp]++;
		ret = -1;
		goto out;
	}

out:
	platform_class_cp_enable_busucp(info->proc_data.cur_work_cp, true);
	return ret;
}

static void mca_wireless_quick_charge_select_stage(struct mca_wireless_quick_charge_info *info)
{
	int i = 0;
	int j, k;
	int stage = 0;
	struct mca_wireless_quick_charge_volt_para *volt_para;
	int ffc_flag;
	int cc_max_volatge;
	int cv_min_current;
	struct mca_wireless_quick_charge_batt_para_info *base_flip_para;

	mca_wireless_quick_charge_update_vbat(info);
	mca_wireless_quick_charge_update_ibat(info);
	mca_wireless_quick_charge_update_ibus(info);

	while (info->proc_data.cur_volt_para[i]) {
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
#line 1083
			mca_log_info("cc_max_volatge %d cv_min_current %d stage %d", cc_max_volatge, cv_min_current, stage);
		}
		if (j < 0)
			stage = 0;

		stage = mca_wireless_quick_charge_jump_stage(stage, info->proc_data.cur_volt_para[i]);
		ffc_flag = strategy_class_fg_get_fastcharge();
		if (stage > info->proc_data.cur_stage[i] || (ffc_flag != info->proc_data.ffc_flag))
			info->proc_data.cur_stage[i] = stage;
		i++;
	};

	if (info->support_base_flip) {
#line 1098
		mca_log_info("get base and flip stage for ocp");
		for (j = 0; j < FG_SITE_MAX; j++) {
			base_flip_para = &info->base_flip_para[j];
			mca_wireless_quick_get_parallel_volt_para(info, j);

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
#line 1120
				mca_log_info("cc_max_volatge %d cv_min_current %d stage %d", cc_max_volatge, cv_min_current, stage);
			}
			stage = mca_wireless_quick_charge_jump_stage(stage, info->proc_data.cur_volt_paraller[j]);
			ffc_flag = strategy_class_fg_get_fastcharge();
			if (stage > info->proc_data.parall_cur_stage[j] || (ffc_flag != info->proc_data.ffc_flag))
				info->proc_data.parall_cur_stage[j] = stage;
#line 1128
			mca_log_err("parall_cur_stage[%d] %d", j, info->proc_data.parall_cur_stage[j]);
		}

	}
}

static int mca_wireless_quick_charge_select_cp(struct mca_wireless_quick_charge_info *info)
{
	int i;

	if (info->cp_type == MCA_CP_TYPE_SINGLE) {
		info->proc_data.cur_work_cp = MCA_WLS_QUICK_CHG_CP_MASTER;
		return 0;
	}

	for (i = 0; i < MCA_WLS_QUICK_CHG_CP_DUAL; i++) {
		if (info->proc_data.error_num[i] < MCA_WLS_QUICK_CHG_SINGLE_MAX_ERR_COUNT) {
			info->proc_data.cur_work_cp = i;
			return 0;
		}
	}

	return -1;
}

static int mca_wireless_quick_charge_can_tbat_do_charge(unsigned int role,
	struct mca_wireless_quick_charge_info *info, bool init)
{
	struct mca_wireless_quick_charge_batt_para_info *batt_para = &info->batt_para[role];
	struct mca_wireless_quick_charge_temp_para *temp_para;
	int temp = 0;
	int ret, i, flag = 0;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1163
		mca_log_err("get %d batt temp fail\n", role);
		return ret;
	}
	temp /= 10;
#line 1167
	mca_log_info("temp=%d\n", temp);
	for (i = 0; i < batt_para->temp_para_size; i++) {
		temp_para = &batt_para->temp_info[i].temp_para;
		if (temp >= temp_para->temp_low && temp < temp_para->temp_high) {
			flag = 1;
			break;
		}
	}

	if (!flag) {
#line 1177
		mca_log_err("%d can not find temp_para\n", role);
		return -1;
	}

#line 1181
	mca_log_info("temp_para_index new %d, old %d\n", i, info->proc_data.temp_para_index[role]);
	if (!temp_para->max_current) {
#line 1183
		mca_log_info("%d temp is out of range\n", role);

		if (init)
			return -1;

		if (i > info->proc_data.temp_para_index[role])
			info->proc_data.temp_hys_en = MCA_WLS_QUICK_TEMP_HYS_HIGH;
		if (i < info->proc_data.temp_para_index[role])
			info->proc_data.temp_hys_en = MCA_WLS_QUICK_TEMP_HYS_LOW;

		return -1;
	}

	if (init) {
		switch (info->proc_data.temp_hys_en) {
		case MCA_WLS_QUICK_TEMP_HYS_DIS:
			break;
		case MCA_WLS_QUICK_TEMP_HYS_HIGH:
			if (temp > (temp_para->temp_high - temp_para->high_temp_hysteresis)) {
#line 1202
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
			break;
		case MCA_WLS_QUICK_TEMP_HYS_LOW:
			if (temp < (temp_para->temp_low + temp_para->low_temp_hysteresis)) {
#line 1208
				mca_log_info("hys high not satisfied\n");
				return -1;
			}
			break;
		default:
			break;
		}

		info->proc_data.temp_hys_en = MCA_WLS_QUICK_TEMP_HYS_DIS;
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
	info->proc_data.temp_max_cur[role] = temp_para->max_current;
#line 1232
	mca_log_info("temp_para_index=%d, max_cur=%d\n", i, temp_para->max_current);
	if (batt_para->temp_info[i].volt_ffc_info.volt_para_size) {
#line 1234
		mca_log_info("select ffc volt para\n");
		info->proc_data.ffc_flag = 1;
		info->proc_data.temp_max_fv[role] = temp_para->ffc_max_fv;
		info->proc_data.cur_volt_para[role] = &batt_para->temp_info[i].volt_ffc_info;
	} else if (batt_para->temp_info[i].volt_info.volt_para_size) {
#line 1239
		mca_log_info("select normal volt para\n");
		info->proc_data.ffc_flag = 0;
		info->proc_data.temp_max_fv[role] = temp_para->normal_max_fv;
		info->proc_data.cur_volt_para[role] = &batt_para->temp_info[i].volt_info;
	} else {
#line 1244
		mca_log_info("no volt para\n");
		info->proc_data.ffc_flag = 0;
		return -1;
	}

#line 1249
	mca_log_info("temp_para_index=%d, max_cur=%d, max_fv = %d\n",
		i, temp_para->max_current, info->proc_data.temp_max_fv[role]);

	return 0;
}

static int mca_wireless_quick_charge_judge_temp(struct mca_wireless_quick_charge_info *info)
{
	int ret;

	ret = mca_wireless_quick_charge_can_tbat_do_charge(FG_IC_MASTER, info, true);
	if (ret)
		return ret;

	return 0;
}

static int mca_wireless_quick_charge_judge_vbat(struct mca_wireless_quick_charge_info *info)
{
	int ret;
	int vbat = 0;
	// mca_wireless_quick_charge_update_vbat(info);
	ret = strategy_class_fg_ops_get_voltage(&vbat);
	if (vbat < info->min_vbat || vbat  > info->max_vbat) {
#line 1273
		mca_log_info("vbat %d is out of range, try next loop\n",
			vbat);
		return -1;
	}

	return 0;
}

static int mca_wireless_quick_charge_check_condition(struct mca_wireless_quick_charge_info *info)
{
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_WLS_MAGNET_LIMIT, &info->proc_data.magnet_limit);
	if (info->force_stop || info->charge_abnormal || !info->online || !info->batt_auth
		|| !info->proc_data.qc_enable || !info->support_mode || info->taper_done_no_retry ||
		(info->proc_data.magnet_limit && info->audio_phone_sts && info->support_hall)) {
#line 1290
		mca_log_info("[online:%d],[force_stop:%d],[phone_flag:%d],[batt_auth:%d],[qc_enable:%d],[support_mode:%d],[charge_abnormal:%d], [taper_done_no_retry %d]\n",
			info->online, info->force_stop, info->thermal_phone_flag, info->batt_auth, info->proc_data.qc_enable, info->support_mode, info->charge_abnormal, info->taper_done_no_retry);
		return -1;
	}
	return 0;
}

static void mca_wireless_quick_charge_start_charging(struct mca_wireless_quick_charge_info *info)
{
	int ret, cur_max = 0;

	ret = mca_wireless_quick_charge_judge_vbat(info);
	if (ret)
		goto out;

	ret = mca_wireless_quick_charge_judge_temp(info);
	if (ret)
		goto out;

	ret = mca_wireless_quick_charge_select_cp(info);
	if (ret) {
#line 1311
		mca_log_err("select cp mode fail\n");
		goto out;
	}

	mca_wireless_quick_charge_select_stage(info);

	cur_max = mca_wireless_quick_charge_select_max_ibat(info);
	if (cur_max <= MCA_WLS_QUICK_CHG_EXIT_FCC) {
		mca_log_info("fcc <= 2.2A, stop quick charging, cur_max: %d\n", cur_max);
		goto out;
	}

	ret = mca_wireless_quick_charge_open_path(info);
	if (ret) {
#line 1329
		mca_log_err("open path fail\n");
		goto out;
	}

	ret = mca_wireless_quick_charge_check_condition(info);
	if (ret) {
#line 1335
		mca_log_err("check condition fail\n");
		goto out;
	}

	info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGING;
	if (info->proc_data.ffc_flag)
		strategy_class_fg_set_fastcharge(true);

	info->proc_data.tune_polling_interval = MCA_WLS_QUICK_CHG_FAST_INTERVAL;
	schedule_delayed_work(&info->monitor_work,
				msecs_to_jiffies(info->proc_data.tune_polling_interval));
	return;

out:
	mca_wireless_quick_charge_force_stop_charging(info);
}

static void mca_wireless_quick_charge_select_cur_work_mode(struct mca_wireless_quick_charge_info *info)
{
	int i;
	int index = 0;
	int ibat_tmp;
	int ibat_max[CHG_MODE_MAX] = { 0 };

	if (info->support_mode & info->proc_data.adp_mode &
		MCA_WLS_QUICK_CHG_MODE_DIV_4) {
		ibat_tmp = min(info->proc_data.wls_power.max_fcc,
			info->div_max_curr[CHG_MODE_DIV4]);
		if (ibat_tmp > ibat_max[2])
			ibat_max[2] = ibat_tmp;
	}

	if (info->support_mode & info->proc_data.adp_mode &
		MCA_WLS_QUICK_CHG_MODE_DIV_2) {
		ibat_tmp = min(info->proc_data.wls_power.max_fcc,
			info->div_max_curr[CHG_MODE_DIV2]);
		if (ibat_tmp > ibat_max[1])
			ibat_max[1] = ibat_tmp;
	}

	if (info->support_mode & info->proc_data.adp_mode &
		MCA_WLS_QUICK_CHG_MODE_DIV_1) {
		ibat_tmp = min(info->proc_data.wls_power.max_fcc * 1,
			info->div_max_curr[CHG_MODE_DIV1]);
		if (ibat_tmp > ibat_max[0])
			ibat_max[0] = ibat_tmp;
	}

	for (i = 0; i < CHG_MODE_MAX; i++) {
#line 1384
		mca_log_info("div%ld ibat_max %d\n", BIT(i), ibat_max[i]);
		if (ibat_max[i] > ibat_max[index])
			index = i;
	}

	info->proc_data.work_mode = BIT(index);
	switch (info->proc_data.work_mode) {
	case MCA_WLS_QUICK_CHG_MODE_DIV_1:
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_1_1;
		info->proc_data.ratio = MCA_WLS_QUICK_CHG_MODE_DIV_1;
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV1];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV1];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV1];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV1];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV1];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV1];
		break;
	case MCA_WLS_QUICK_CHG_MODE_DIV_2:
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_2_1;
		info->proc_data.ratio = MCA_WLS_QUICK_CHG_MODE_DIV_2;
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV2];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV2];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV2];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV2];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV2];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV2];
		break;
	case MCA_WLS_QUICK_CHG_MODE_DIV_4:
		info->proc_data.cur_cp_mode = CP_MODE_FORWARD_4_1;
		info->proc_data.ratio = MCA_WLS_QUICK_CHG_MODE_DIV_4;
		info->proc_data.single_curr = info->div_single_curr[CHG_MODE_DIV4];
		info->proc_data.max_curr = info->div_max_curr[CHG_MODE_DIV4];
		info->proc_data.delta_ibat = info->div_delta_ibat[CHG_MODE_DIV4];
		info->proc_data.multi_ibus_th = info->multi_ibus_th[CHG_MODE_DIV4];
		info->proc_data.ibus_inc = info->ibus_inc_hysteresis[CHG_MODE_DIV4];
		info->proc_data.ibus_dec = info->ibus_dec_hysteresis[CHG_MODE_DIV4];
		break;
	default:
		break;
	}
#line 1424
	mca_log_info("work_mode=%d, s_cur=%d, m_cur=%d\n",
		info->proc_data.work_mode, info->proc_data.single_curr, info->proc_data.max_curr);
}

static int mca_wireless_quick_charge_adjust_adapter_mode(struct mca_wireless_quick_charge_info *info)
{
	int ret = 0;
	int cp_mode = 0;

	ret = strategy_class_wireless_ops_get_adapter_power(&info->proc_data.wls_power);
	strategy_class_wireless_ops_get_adapter_charger_mode(&cp_mode);
	strategy_class_wireless_op_get_rx_iout_limit(&info->proc_data.rx_iout_limit);
	info->proc_data.adp_mode = cp_mode;

	return ret;
}

static bool mca_wireless_quick_charge_check_chg_done(struct mca_wireless_quick_charge_info *info)
{
	if (info->proc_data.charge_flag_pre != MCA_QUICK_CHG_STS_CHARGE_DONE)
		return false;

	mca_wireless_quick_charge_update_vbat(info);
	if (info->batt_type == MCA_BATTERY_TYPE_SINGLE)
		return (info->proc_data.vbat[FG_IC_MASTER] < info->recharge_vbat);

	return ((info->proc_data.vbat[FG_IC_MASTER] < info->recharge_vbat) &&
		(info->proc_data.vbat[FG_IC_SLAVE] < info->recharge_vbat));
}

static void mca_wireless_quick_charge_startup(struct mca_wireless_quick_charge_info *info)
{
	int ret = 0;

#line 1458
	mca_log_info("quick charge pre check\n");

	info->proc_data.soc = strategy_class_fg_ops_get_soc();
	if (info->proc_data.soc > MCA_WLS_QUICK_CHG_ENABLE_CP_SOC_TH) {
		mca_log_info("soc = %d, exit quick charge\n", info->proc_data.soc);
		return;
	}

	ret = mca_wireless_quick_charge_check_condition(info);
	if (ret)
		return;

	if (info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING ||
		info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGE_FAILED)
		return;

	if (info->proc_data.total_err > MCA_WLS_QUICK_CHG_MAX_ERR_COUNT) {
		mca_wireless_quick_charge_stop_charging(info);
		info->proc_data.charge_flag = MCA_QUICK_CHG_STS_CHARGE_FAILED;
		return;
	}

	if (mca_wireless_quick_charge_check_chg_done(info)) {
#line 1482
		mca_log_info("chg done, check next loop\n");
		return;
	}

	info->proc_data.icl_target_ma = MCA_WLS_QUICK_CHG_ICL_AFTER_BQ_SMALL_MA;
	info->proc_data.enable_quickchg = 1;

	ret = mca_wireless_quick_charge_adjust_adapter_mode(info);
	if (ret) {
		info->proc_data.total_err++;
		info->proc_data.enable_quickchg = 0;
#line 1493
		mca_log_err("get adapt mode fail,err=%d\n", info->proc_data.total_err);
		return;
	}

	mca_wireless_quick_charge_select_cur_work_mode(info);
	mca_wireless_quick_charge_start_charging(info);
}

static int mca_wireless_quick_charge_req_adp_volt(struct mca_wireless_quick_charge_info *info)
{
#line 1506
	mca_log_info("req adp volt=%d\n", info->proc_data.cur_adp_volt);

	if (info->proc_data.cur_adp_volt > info->vout_set_max)
		info->proc_data.cur_adp_volt = info->vout_set_max;

	return mca_wireless_quick_charge_vout_setting(info, info->proc_data.cur_adp_volt);
}

static int mca_wireless_quick_charge_get_vstep(int cur_cap, struct mca_wireless_quick_charge_info *info)
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
		return MCA_WLS_QUICK_CHG_DEFAULT_VSTEP;

	for (i = 0; i < VSTEP_PARA_MAX_GROUP; i++) {
		if (!info->vstep_para[i].volt_ratio)
			break;

		if (info->vstep_para[i].volt_ratio != info->proc_data.ratio)
			continue;

		if (info->vstep_para[i].cur_gap <= cur_cap) {
			vstep = MCA_WLS_QUICK_CHG_DEFAULT_VSTEP * info->vstep_para[i].vstep_ratio;
			break;
		}
	}

	if (!vstep)
		vstep = MCA_WLS_QUICK_CHG_DEFAULT_VSTEP;

	return vstep;
}

#define MCA_WLS_OC_DECLINE_CV_5MV 5
#define MCA_WLS_OC_DECLINE_CV_2MV 2
static int mca_wireless_quick_charge_select_min_vbatt_th(struct mca_wireless_quick_charge_info *info, int cur_max)
{
	int volt_para_size = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].voltage;
	int delta_fv = info->smartchg_data.delta_fv;
	int ibat = info->proc_data.ibat_total;

	if ((cur_stage / 2) == (volt_para_size - 1)) {
#line 1559
		mca_log_err("vbatt_th %d delta_fv %d\n", vbat_th, delta_fv);
		vbat_th -= delta_fv;
	}
	if (ibat - cur_max > 1500) {
		vbat_th -= MCA_WLS_OC_DECLINE_CV_5MV;
#line 1564
		mca_log_err("vbatt_th decline 5mv, vbat_th=%d\n", vbat_th);
	} else if (ibat - cur_max > 500) {
		vbat_th -= MCA_WLS_OC_DECLINE_CV_2MV;
#line 1567
		mca_log_err("vbatt_th decline 2mv, vbat_th=%d\n", vbat_th);
	}

	return vbat_th;
}

static int mca_wireless_quick_charge_regulation(struct mca_wireless_quick_charge_info *info)
{
	int vbat = info->proc_data.vbat[FG_IC_MASTER];
	int ibat = info->proc_data.ibat_total;
	int cur_stage = info->proc_data.cur_stage[FG_IC_MASTER];
	int cur_min = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[cur_stage / 2].current_min;
	int ret, ibus, cur_max, vstep, rx_iout;
	int vout_setted = 0;
	int vbat_th;

	ret = mca_wireless_quick_charge_update_ibus(info);
	if (ret) {
#line 1585
		mca_log_err("regulation get ibus failed\n");
		return -1;
	}
	ibus = info->proc_data.ibus;

	//need to optimization
	cur_max = mca_wireless_quick_charge_select_max_ibat(info);
	if (cur_max <= MCA_WLS_QUICK_CHG_EXIT_FCC) {
#line 1592
		mca_log_info("fcc <= 2.2A, stop quick charging, cur_max: %d\n", cur_max);
		return 1;
	}

	vbat_th = mca_wireless_quick_charge_select_min_vbatt_th(info, cur_max);
#line 1597
	mca_log_info("cur_stage[%d]:vbat=%d,ibat=%d,vbat_th=%d,c_max=%d,c_min=%d,ibus=%d\n",
		cur_stage, vbat, ibat, vbat_th, cur_max, cur_min, ibus);

	ret = platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
	vstep = mca_wireless_quick_charge_get_vstep(cur_max - ibat, info);
	if (vstep > MCA_WLS_QUICK_CHG_STEP_THRESHOLD_MV)
		info->proc_data.tune_polling_interval = MCA_WLS_QUICK_CHG_FAST_INTERVAL_MS;
	else
		info->proc_data.tune_polling_interval = MCA_WLS_QUICK_CHG_NORMAL_INTERVAL_MS;
#line 1606
	mca_log_info("vstep: %d, rx_iout: %d,tune_polling: %d\n", vstep, rx_iout, info->proc_data.tune_polling_interval);
	/* cv:cur_stage % 2 == 1 */
	if (cur_stage % 2) {
		if (ibus > cur_max / info->proc_data.ratio || ibat > (cur_max - 50)) {
			info->proc_data.cur_adp_volt -= vstep;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1613
				mca_log_err("[a]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}

		if (ibat > cur_max - info->proc_data.delta_ibat) {
#line 1620
			mca_log_info("keep cur status\n");
			return 0;
		}

		if (ibus < (cur_max - info->proc_data.delta_ibat) / info->proc_data.ratio) {
			info->proc_data.cur_adp_volt += vstep;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1633
				mca_log_err("[b]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}
	} else {
		if (rx_iout >= info->proc_data.rx_iout_limit + 50) {
			info->proc_data.cur_adp_volt -= 100;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1643
				mca_log_err("[c]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}

		if (rx_iout > (cur_max / info->proc_data.ratio + 100) || ibat > (cur_max - 50)) {
			info->proc_data.cur_adp_volt -= vstep;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1653
				mca_log_err("[d]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}

		if (ibat < cur_max - info->proc_data.delta_ibat) {
			info->proc_data.cur_adp_volt += vstep;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1664
				mca_log_err("[e]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}

		if (rx_iout < (cur_max - info->proc_data.delta_ibat) / info->proc_data.ratio) {
			info->proc_data.cur_adp_volt += vstep;
			ret = mca_wireless_quick_charge_req_adp_volt(info);
			if (ret)
#line 1674
				mca_log_err("[f]regulation req adp volt failed\n");
			else
				goto out;
			return ret;
		}
	}

out:
	(void)platform_class_wireless_get_vout_setted(WIRELESS_ROLE_MASTER, &vout_setted);
	info->proc_data.cur_adp_volt = vout_setted;

	return ret;
}

#define MCA_WLS_QUICK_CHG_TARGET_DIFF_THRESHOLD_MA 300
#define MCA_WLS_QUICK_CHG_TAPER_DECREASE_STEP_MA 100
#define MCA_WLS_QUICK_CHG_MAX_CURRENT_IBAT_OPENGO 500
#define HALF_TAPER_REDUNDANCY_MV 5
#define QUARTER_TAPER_REDUNDANCY_MV 7
#define MCA_WLS_QUICK_CHG_TAPER_TIMEOUT 3
static int mca_wireless_quick_charge_check_charge_done(struct mca_wireless_quick_charge_info *info)
{
	int last_volt_stage = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para_size - 1;
	int vbat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[last_volt_stage].voltage;
	int ibat_th = info->proc_data.cur_volt_para[FG_IC_MASTER]->volt_para[last_volt_stage].current_min;
	int delta_fv = info->smartchg_data.delta_fv;
	static int taper_cnt;
	int cur_max = mca_wireless_quick_charge_select_max_ibat(info);
	int ret = 0;

	if (cur_max <= MCA_WLS_QUICK_CHG_EXIT_FCC) {
#line 1714
		mca_log_info("fcc <= 2.2A, stop quick charging, cur_max: %d\n", cur_max);
		return 1;
	}

	mca_log_info("[vbat: %d],[ibat: %d],[vbat_th: %d],[ibat_th: %d],[cur_max: %d],[delta_fv: %d],[taper_cnt: %d]\n",
		info->proc_data.vbat[FG_IC_MASTER], info->proc_data.ibat[FG_IC_MASTER],
		vbat_th, ibat_th, cur_max, delta_fv, taper_cnt);

	if (info->proc_data.vbat[FG_IC_MASTER] > vbat_th - delta_fv) {
		if (info->proc_data.ibat[FG_IC_MASTER] >= ibat_th)
			taper_cnt = 0;
		else {
			if (info->proc_data.ibat[FG_IC_MASTER] > ibat_th - MCA_WLS_QUICK_CHG_TARGET_DIFF_THRESHOLD_MA) {
				if (info->proc_data.vbat[FG_IC_MASTER] > vbat_th - delta_fv + HALF_TAPER_REDUNDANCY_MV)
					taper_cnt++;
				else
					taper_cnt = 0;
			} else
				taper_cnt++;

			if (taper_cnt > MCA_WLS_QUICK_CHG_TAPER_TIMEOUT) {
				info->proc_data.charge_flag_pre = MCA_QUICK_CHG_STS_CHARGE_DONE;
#line 1736
				mca_log_info("quick charge done exit\n");
				taper_cnt = 0;
			}
		}

		if (info->proc_data.ibat[FG_IC_MASTER] > ibat_th || cur_max > ibat_th + 200 ||
			info->proc_data.vbat[FG_IC_MASTER] > vbat_th - delta_fv + QUARTER_TAPER_REDUNDANCY_MV) {
			if (cur_max - info->proc_data.ibat[FG_IC_MASTER] >= MCA_WLS_QUICK_CHG_MAX_CURRENT_IBAT_OPENGO)
				info->proc_data.sw_cv_cur = info->proc_data.ibat[FG_IC_MASTER]
					- MCA_WLS_QUICK_CHG_TAPER_DECREASE_STEP_MA;
			else		
				info->proc_data.sw_cv_cur = cur_max - MCA_WLS_QUICK_CHG_TAPER_DECREASE_STEP_MA;

#line 1744
			mca_log_info("vbat or ibat over thre, cur_max dec 100mA, sw_cv_cur: %d\n", info->proc_data.sw_cv_cur);
		}
	} else
		taper_cnt = 0;

	return ret;
}

static int mca_wireless_quick_charge_select_volt_para(struct mca_wireless_quick_charge_info *info)
{
	int ret;

	ret = mca_wireless_quick_charge_can_tbat_do_charge(FG_IC_MASTER, info, false);
	if (ret)
		return ret;

	return 0;
}

static int mca_wireless_quick_charge_is_charge_abnormal(struct mca_wireless_quick_charge_info *info)
{
	bool chg_enable = false;
	bool chg_enable_slave = false;

	if (info->proc_data.cur_work_cp == MCA_WLS_QUICK_CHG_CP_DUAL) {
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &chg_enable);
		(void)platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &chg_enable_slave);
		if (chg_enable && chg_enable_slave)
			return 0;

#line 1815
		mca_log_err("cp work abnormal, master %d slave %d\n",
			chg_enable, chg_enable_slave);
		if (!chg_enable) {
			info->proc_data.total_err++;
			info->proc_data.error_num[MCA_WLS_QUICK_CHG_CP_MASTER]++;
		} else {
			info->proc_data.total_err++;
			info->proc_data.error_num[MCA_WLS_QUICK_CHG_CP_SLAVE]++;
		}

		platform_class_cp_dump_register(CP_ROLE_MASTER);
		platform_class_cp_dump_register(CP_ROLE_SLAVE);

		return -1;
	}
	(void)platform_class_cp_get_charging_enabled(info->proc_data.cur_work_cp,
		&chg_enable);
	if (!chg_enable) {
#line 1833
		mca_log_err("cp work abnormal, role %d enbale %d\n",
			info->proc_data.cur_work_cp, chg_enable);
		platform_class_cp_dump_register(info->proc_data.cur_work_cp);
		info->proc_data.total_err++;
		info->proc_data.error_num[info->proc_data.cur_work_cp]++;
		return -1;
	}

	return 0;
}

static void mca_wireless_quick_charge_try2_single_path(struct mca_wireless_quick_charge_info *info)
{
	int ret;
	int ibus;
	struct mca_wireless_quick_charge_process_data *proc_data = &info->proc_data;

	ret = mca_wireless_quick_charge_update_ibus(info);
	if (ret)
		return;

	ibus = proc_data->ibus;
	if (ibus > proc_data->multi_ibus_th - proc_data->ibus_dec)
		return;

	// TODO:add sysfs
	if (1/*proc_data->cp_path_enable[CP_ROLE_MASTER]*/) {
		(void)platform_class_cp_set_charging_enable(CP_ROLE_SLAVE, false);
		(void)platform_class_cp_enable_adc(CP_ROLE_SLAVE, false);
		proc_data->cur_work_cp = MCA_WLS_QUICK_CHG_CP_MASTER;
	} else {
		(void)platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
		(void)platform_class_cp_enable_adc(CP_ROLE_MASTER, false);
		proc_data->cur_work_cp = MCA_WLS_QUICK_CHG_CP_SLAVE;
	}
}

static void mca_wireless_quick_charge_try2_multi_path(struct mca_wireless_quick_charge_info *info)
{
	int ret;
	int ibus;
	int cp_role;
	struct mca_wireless_quick_charge_process_data *proc_data = &info->proc_data;

	ret = mca_wireless_quick_charge_update_ibus(info);
	if (ret)
		return;

	ibus = proc_data->ibus;
	if (ibus < proc_data->multi_ibus_th + proc_data->ibus_inc)
		return;

	if (proc_data->cur_work_cp == MCA_WLS_QUICK_CHG_CP_MASTER)
		cp_role = CP_ROLE_SLAVE;
	else
		cp_role = CP_ROLE_MASTER;

	if (proc_data->error_num[cp_role] >= MCA_WLS_QUICK_CHG_SINGLE_MAX_ERR_COUNT)
		return;

	(void)platform_class_cp_enable_adc(cp_role, true);
	(void)platform_class_cp_set_charging_enable(cp_role, true);
	ret = mca_wireless_quick_charge_msleep(500, info);
	if (ret)
		return;

	ret = platform_class_cp_get_bus_current(cp_role, &ibus);
	if (ret) {
		proc_data->error_num[cp_role]++;
		(void)platform_class_cp_set_charging_enable(cp_role, false);
		return;
	}

	if (ibus < MCA_WLS_QUICK_CHG_OPEN_PATH_IBUS_TH)
		return;

	proc_data->cur_work_cp = MCA_WLS_QUICK_CHG_CP_DUAL;
}

static void mca_wireless_quick_charge_change_path(struct mca_wireless_quick_charge_info *info)
{
	if (info->cp_type != MCA_CP_TYPE_PARALLEL)
		return;

	if (info->proc_data.cur_work_cp == MCA_WLS_QUICK_CHG_CP_DUAL)
		mca_wireless_quick_charge_try2_single_path(info);
	else
		mca_wireless_quick_charge_try2_multi_path(info);
}

static void mca_wireless_quick_charge_monitor_work(struct work_struct *work)
{
	struct mca_wireless_quick_charge_info *info =  container_of(work,
		struct mca_wireless_quick_charge_info, monitor_work.work);
	int ret;

	if (!info->proc_data.qc_enable)
		goto out;

	ret = mca_wireless_quick_charge_is_charge_abnormal(info);
	if (ret)
		goto out;

	if (info->proc_data.icl_setting_ma > info->proc_data.icl_target_ma) {
		if (info->proc_data.icl_setting_ma >= info->proc_data.icl_target_ma + 100) {
			info->proc_data.icl_setting_ma -= 100;
			mca_wireless_quick_charge_icl_setting(info, true, info->proc_data.icl_setting_ma);
		} else {
			info->proc_data.icl_setting_ma = info->proc_data.icl_target_ma;
			mca_wireless_quick_charge_icl_setting(info, true, info->proc_data.icl_setting_ma);
		}
		mca_wireless_quick_charge_icl_setting(info, true, info->proc_data.icl_setting_ma);
#line 1958
		mca_log_info("minus icl to %d, icl_target_ma:%d\n", info->proc_data.icl_setting_ma, info->proc_data.icl_target_ma);
	}

	ret = mca_wireless_quick_charge_select_volt_para(info);
	if (ret) {
#line 1963
		mca_log_info("monitor temp out of range\n");
		strategy_class_fg_set_fastcharge(false);
		goto out;
	}

	mca_wireless_quick_charge_select_stage(info);
	if (info->proc_data.ffc_flag)
		strategy_class_fg_set_fastcharge(true);
	else
		strategy_class_fg_set_fastcharge(false);
	ret = mca_wireless_quick_charge_check_charge_done(info);
	if (info->proc_data.charge_flag_pre == MCA_QUICK_CHG_STS_CHARGE_DONE) {
#line 1988
		mca_log_info("monitor qc charge done\n");
		goto out;
	}
	if (ret == 1)
		goto out;
	mca_wireless_quick_charge_change_path(info);
	ret = mca_wireless_quick_charge_regulation(info);
	if (ret) {
		if (ret == 1)
			goto out;
		info->proc_data.total_err++;
		goto out;
	}
	schedule_delayed_work(&info->monitor_work,
		msecs_to_jiffies(info->proc_data.tune_polling_interval));

	return;
out:
	mca_wireless_quick_charge_stop_charging(info);
}


static int mca_wireless_quick_charge_get_status(int status, void *value, void *data)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;
	int *cur_val = (int *)value;
	int last_volt_stage = -1;

	if (!info)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_CHARGING:
		*cur_val = info->proc_data.charge_flag;
		break;
	case STRATEGY_STATUS_TYPE_QC_IBAT_MAX:
		*cur_val = mca_wireless_quick_charge_select_max_ibat(info);
		break;
	case STRATEGY_STATUS_TYPE_QC_START_FLAG:
		*cur_val = info->proc_data.enable_quickchg;
		break;
	case STRATEGY_STATUS_TYPE_ENABLE:
		*cur_val = !info->force_stop;
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

static void mca_wireless_quick_charge_parse_stage_para(struct device_node *node,
	const char *name, struct mca_wireless_quick_charge_volt_para_info *volt_info)
{
	int i;

	if (strcmp(name, "null") == 0) {
#line 2062
		mca_log_info("no need parse stage para\n");
		return;
	}

	volt_info->stage_para = kcalloc(volt_info->volt_para_size * 2, sizeof(int), GFP_KERNEL);
	if (!volt_info->volt_para) {
#line 2068
		mca_log_err("volt para no mem\n");
		return;
	}

	mca_parse_dts_u32_array(node, name, volt_info->stage_para,
		volt_info->volt_para_size * 2);

	for (i = 0; i < volt_info->volt_para_size * 2; i++)
#line 2076
		mca_log_debug("[%d]stage_para %d\n", i, volt_info->stage_para[i]);
}

static int mca_wireless_quick_charge_parse_volt_para(struct device_node *node,
	const char *name, struct mca_wireless_quick_charge_volt_para_info *volt_info)
{
	struct mca_wireless_quick_charge_volt_para *volt_para;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	if (strcmp(name, "null") == 0) {
#line 2087
		mca_log_info("no need parse volt para\n");
		return 0;
	}
	array_len = mca_parse_dts_count_strings(node, name,
		VOLT_PARA_MAX_GROUP,
		MCA_WLS_QUICK_CHG_VOLT_PARA_MAX);
	if (array_len < 0) {
#line 2094
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	volt_info->volt_para_size = array_len / MCA_WLS_QUICK_CHG_VOLT_PARA_MAX;
	volt_info->volt_para = kcalloc(volt_info->volt_para_size, sizeof(*volt_para), GFP_KERNEL);
	if (!volt_info->volt_para) {
#line 2101
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}
	volt_para = volt_info->volt_para;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

#line 2109
		mca_log_debug("[%d]volt_para %s\n", i, tmp_string);
		row = i / MCA_WLS_QUICK_CHG_VOLT_PARA_MAX;
		col = i % MCA_WLS_QUICK_CHG_VOLT_PARA_MAX;

		switch (col) {
		case MCA_WLS_QUICK_CHG_VOLTAGE:
			if (kstrtoint(tmp_string, 10, &volt_para[row].voltage))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_CURRENT_MAX:
			if (kstrtoint(tmp_string, 10, &volt_para[row].current_max))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_CURRENT_MIN:
			if (kstrtoint(tmp_string, 10, &volt_para[row].current_min))
				goto error;
			break;
		default:
			break;
		}
	}
	for (i = 0; i < volt_info->volt_para_size; i++)
#line 2132
		mca_log_err("volt_para %d %d %d\n",
			volt_para[i].voltage, volt_para[i].current_max, volt_para[i].current_min);
	return 0;
error:
	kfree(volt_para);
	volt_para = NULL;
	return -1;
}

static int mca_wireless_quick_charge_parse_temp_para(struct device_node *node,
	int batt_role, const char *name, struct mca_wireless_quick_charge_info *info, int mode)
{
	struct mca_wireless_quick_charge_batt_para_info *batt_para_info = &(info->batt_para[batt_role]);
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	struct mca_wireless_quick_charge_temp_para_info *temp_info = NULL;

	array_len = mca_parse_dts_count_strings(node, name,
		TEMP_PARA_MAX_GROUP,
		MCA_WLS_QUICK_CHG_TEMP_PARA_MAX);
	if (array_len < 0) {
#line 2165
		mca_log_err("parse %s failed\n", name);
		return -1;
	}
	if (mode == 0)
		batt_para_info = &(info->batt_para[batt_role]);
	else
		batt_para_info = &(info->base_flip_para[batt_role]);

	batt_para_info->temp_para_size = array_len / MCA_WLS_QUICK_CHG_TEMP_PARA_MAX;
	batt_para_info->temp_info = kcalloc(batt_para_info->temp_para_size, sizeof(*temp_info), GFP_KERNEL);
	if (!batt_para_info->temp_info) {
#line 2176
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	temp_info = batt_para_info->temp_info;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_WLS_QUICK_CHG_TEMP_PARA_MAX;
		col = i % MCA_WLS_QUICK_CHG_TEMP_PARA_MAX;
#line 2186
		mca_log_debug("[%d]temp_para %s\n", i, tmp_string);
		switch (col) {
		case MCA_WLS_QUICK_CHG_TEMP_LOW:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_low))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_high))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.low_temp_hysteresis))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.high_temp_hysteresis))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_TEMP_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.max_current))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_TEMP_NROMAL_FV:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.normal_max_fv))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_TEMP_FFC_FV:
			if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.ffc_max_fv))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_VOLT_PARA_NAME:
			if (mca_wireless_quick_charge_parse_volt_para(node, tmp_string,
				&temp_info[row].volt_info))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_VOLT_FFC_PARA_NAME:
			if (mca_wireless_quick_charge_parse_volt_para(node, tmp_string,
				&temp_info[row].volt_ffc_info))
				goto error;
			break;
		case MCA_WLS_QUICK_CHG_STAGE_PARA_NAME:
			mca_wireless_quick_charge_parse_stage_para(node, tmp_string,
				&temp_info[row].volt_info);
			break;
		case MCA_WLS_QUICK_CHG_FFC_STAGE_PARA_NAME:
			mca_wireless_quick_charge_parse_stage_para(node, tmp_string,
				&temp_info[row].volt_ffc_info);
			break;
		default:
			break;
		}
	}
	for (i = 0; i < batt_para_info->temp_para_size; i++)
#line 2252
		mca_log_err("temp_para %d %d %d %d %d %d %d %d %d\n",
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

static void mca_wireless_quick_charge_check_model_name(const char **name)
{
	const char *device_name = NULL;
	(void)platform_fg_ops_get_device_name(FG_IC_MASTER, &device_name);
	if (!device_name) {
#line 2269
		mca_log_err("get device name  fail\n");
		return;
	}
#line 2272
	mca_log_err("device name: %s, name: %s\n", device_name, *name);
	// 3XM31 is specific to O11U
	if (!strcmp("atl", *name) && !strcmp("3XM31", device_name)) {
		*name = "atl2";
	}
}

static int mca_wireless_quick_charge_parse_batt_info(struct mca_wireless_quick_charge_info *info, int mode)
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
		node = of_find_node_by_name(NULL, "mca_quick_wireless_batt_para_gbl");

	if (!node) {
#line 2294
		mca_log_err("node in null\n");
		return -1;
	}

	array_len = mca_parse_dts_count_strings(node, battpara[mode],
			BATT_PARA_MAX_GROUP, MCA_WLS_QUICK_CHG_BATT_PARA_MAX);
	if (array_len < 0) {
#line 2301
		mca_log_err("parse batt_para failed\n");
		return -1;
	}

	(void)platform_fg_ops_get_batt_cell_info(FG_IC_MASTER, &main_cell_name);
	if (!main_cell_name) {
#line 2307
		mca_log_err("get main cell name fail\n");
		return -1;
	}
	mca_wireless_quick_charge_check_model_name(&main_cell_name);
#line 2311
	mca_log_info("main_cell_name %s\n", main_cell_name);
	if (info->batt_type > MCA_BATTERY_TYPE_SINGLE) {
		(void)platform_fg_ops_get_batt_cell_info(FG_IC_SLAVE, &slave_cell_name);
		if (!slave_cell_name) {
#line 2315
			mca_log_err("get slave cell name fail\n");
			return -1;
		}
#line 2318
		mca_log_info("slave_cell_name %s\n", slave_cell_name);
	}
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, battpara[mode], i, &tmp_string))
			return -1;

		row = i / MCA_WLS_QUICK_CHG_BATT_PARA_MAX;
		col = i % MCA_WLS_QUICK_CHG_BATT_PARA_MAX;
#line 2326
		mca_log_debug("[%d]batt para %s\n", i, tmp_string);
		switch (col) {
		case MCA_WLS_QUICK_CHG_BATT_ROLE:
			if (kstrtoint(tmp_string, 10, &batt_role))
				return -1;
			if (batt_role > FG_IC_SLAVE)
				i += 2;
			break;
		case MCA_WLS_QUICK_CHG_BATT_ID:
			/* find same batt info */
			if ((batt_role == FG_IC_MASTER && main_cell_name && strcmp(tmp_string, main_cell_name)) ||
				(batt_role == FG_IC_SLAVE && slave_cell_name && strcmp(tmp_string, slave_cell_name))) {
				mca_log_info("can not match cell name\n");
				i++;
			}
			break;
		case MCA_WLS_QUICK_CHG_TEMP_PARA_NAME:
			if (mca_wireless_quick_charge_parse_temp_para(node, batt_role, tmp_string, info, mode))
				return -1;
			break;
		default:
			break;
		}
	}

	if (!info->batt_para[FG_IC_MASTER].temp_info) {
#line 2352
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


static void mca_wireless_quick_charge_parse_vstep_para(struct mca_wireless_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int row, col, len;
	int data[VSTEP_PARA_MAX_GROUP * MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX] = { 0 };

	len = mca_parse_dts_string_array(node, "vstep_para", data,
		VSTEP_PARA_MAX_GROUP, MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX);
	if (len < 0)
		return;

	for (row = 0;  row < len / MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX; row++) {
		col = row * MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX + MCA_WLS_QUICK_CHG_VOLT_RATIO;
		info->vstep_para[row].volt_ratio = data[col];
		col = row * MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX + MCA_WLS_QUICK_CHG_CURRENT;
		info->vstep_para[row].cur_gap = data[col];
		col = row * MCA_WLS_QUICK_CHG_VSTEP_PARA_MAX + MCA_WLS_QUICK_CHG_VSTEP_RATIO;
		info->vstep_para[row].vstep_ratio = data[col];
	}
}

static void mca_wireless_quick_charge_parse_chg_mode_info(struct device_node *node,
	struct mca_wireless_quick_charge_info *info)

{
	u32 idata[CHG_MODE_MAX] = { 0 };
	int ret;

	ret = mca_parse_dts_u32_array(node, "div_delta_ibat", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_delta_ibat[CHG_MODE_DIV1] = MCA_WLS_QUICK_CHG_DEFAULT_IBAT_DELTA;
		info->div_delta_ibat[CHG_MODE_DIV2] = MCA_WLS_QUICK_CHG_DEFAULT_IBAT_DELTA;
		info->div_delta_ibat[CHG_MODE_DIV4] = MCA_WLS_QUICK_CHG_DEFAULT_IBAT_DELTA;
	} else {
		memcpy(info->div_delta_ibat, idata, sizeof(idata));
	}
#line 2401
	mca_log_debug("div_delta_ibat %d %d %d\n", info->div_delta_ibat[CHG_MODE_DIV1],
		info->div_delta_ibat[CHG_MODE_DIV2], info->div_delta_ibat[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_single_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_single_curr[CHG_MODE_DIV1] = MCA_WLS_QUICK_CHG_SINGLE_DIV1_CURR_TH;
		info->div_single_curr[CHG_MODE_DIV2] = MCA_WLS_QUICK_CHG_SINGLE_DIV2_CURR_TH;
		info->div_single_curr[CHG_MODE_DIV4] = MCA_WLS_QUICK_CHG_SINGLE_DIV4_CURR_TH;
	} else {
		memcpy(info->div_single_curr, idata, sizeof(idata));
	}
#line 2412
	mca_log_debug("div_single_curr %d %d %d\n", info->div_single_curr[CHG_MODE_DIV1],
		info->div_single_curr[CHG_MODE_DIV2], info->div_single_curr[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "div_max_curr", idata, CHG_MODE_MAX);
	if (ret) {
		info->div_max_curr[CHG_MODE_DIV1] = info->div_single_curr[CHG_MODE_DIV1];
		info->div_max_curr[CHG_MODE_DIV2] = info->div_single_curr[CHG_MODE_DIV2];
		info->div_max_curr[CHG_MODE_DIV4] = info->div_single_curr[CHG_MODE_DIV4];
	} else {
		memcpy(info->div_max_curr, idata, sizeof(idata));
	}
#line 2423
	mca_log_debug("div_max_curr %d %d %d\n", info->div_max_curr[CHG_MODE_DIV1],
		info->div_max_curr[CHG_MODE_DIV2], info->div_max_curr[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "multi_ibus_th", idata, CHG_MODE_MAX);
	if (ret) {
		info->multi_ibus_th[CHG_MODE_DIV1] = MCA_WLS_QUICK_CHG_IBUS_TH_DEFAULT;
		info->multi_ibus_th[CHG_MODE_DIV2] = MCA_WLS_QUICK_CHG_IBUS_TH_DEFAULT;
		info->multi_ibus_th[CHG_MODE_DIV4] = MCA_WLS_QUICK_CHG_IBUS_TH_DEFAULT;
	} else {
		memcpy(info->multi_ibus_th, idata, sizeof(idata));
	}
#line 2434
	mca_log_debug("multi_ibus_th %d %d %d\n", info->multi_ibus_th[CHG_MODE_DIV1],
		info->multi_ibus_th[CHG_MODE_DIV2], info->multi_ibus_th[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_inc_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_inc_hysteresis[CHG_MODE_DIV1] = MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_inc_hysteresis[CHG_MODE_DIV2] = MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_inc_hysteresis[CHG_MODE_DIV4] = MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
	} else {
		memcpy(info->ibus_inc_hysteresis, idata, sizeof(idata));
	}
#line 2445
	mca_log_debug("ibus_inc_hysteresis %d %d %d\n", info->ibus_inc_hysteresis[CHG_MODE_DIV1],
		info->ibus_inc_hysteresis[CHG_MODE_DIV2], info->ibus_inc_hysteresis[CHG_MODE_DIV4]);

	ret = mca_parse_dts_u32_array(node, "ibus_dec_hysteresis", idata, CHG_MODE_MAX);
	if (ret) {
		info->ibus_dec_hysteresis[CHG_MODE_DIV1] = MCA_WLS_QUICK_CHG_IBUS_DEC_HYS_DEFAULT;
		info->ibus_dec_hysteresis[CHG_MODE_DIV2] = MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
		info->ibus_dec_hysteresis[CHG_MODE_DIV4] = MCA_WLS_QUICK_CHG_IBUS_INC_HYS_DEFAULT;
	} else {
		memcpy(info->ibus_dec_hysteresis, idata, sizeof(idata));
	}
#line 2456
	mca_log_debug("ibus_dec_hysteresis %d %d %d\n", info->ibus_dec_hysteresis[CHG_MODE_DIV1],
		info->ibus_dec_hysteresis[CHG_MODE_DIV2], info->ibus_dec_hysteresis[CHG_MODE_DIV4]);
}

static int mca_wireless_quick_charge_parse_dt(struct mca_wireless_quick_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int ret;

	if (!node) {
#line 2466
		mca_log_err("device tree node missing\n");
		return -EINVAL;
	}

	(void)mca_parse_dts_u32(node, "batt_type", &info->batt_type, 0);
	(void)mca_parse_dts_u32(node, "cp_type", &info->cp_type, 0);
	(void)mca_parse_dts_u32(node, "min_vbat", &info->min_vbat,
		MCA_WLS_QUICK_CHG_MIN_VBAT_DEFAULT);
	(void)mca_parse_dts_u32(node, "max_vbat", &info->max_vbat,
		MCA_WLS_QUICK_CHG_MAX_VBAT_DEFAULT);
	(void)mca_parse_dts_u32(node, "recharge_vbat", &info->recharge_vbat,
		MCA_WLS_QUICK_CHG_RECHARGE_VBAT_DEFAULT);
	(void)mca_parse_dts_u32(node, "die_temp_max", &info->die_temp_max,
		MCA_WLS_QUICK_CHG_MAX_TDIE_DEFAULT);
	(void)mca_parse_dts_u32(node, "adp_temp_max", &info->adp_temp_max,
		MCA_WLS_QUICK_CHG_MAX_TADP_DEFAULT);
	(void)mca_parse_dts_u32(node, "support_mode", &info->support_mode, 0);
	info->has_gbl_batt_para = of_property_read_bool(node, "has-global-batt-para");
	(void)mca_parse_dts_u32(node, "support-hall", &info->support_hall, 0);
	info->support_base_flip = of_property_read_bool(node, "support-base-flip");
	mca_wireless_quick_charge_parse_chg_mode_info(node, info);
	/* charge para */
	mca_wireless_quick_charge_parse_vstep_para(info);
	ret = mca_wireless_quick_charge_parse_batt_info(info, 0);
	if (info->support_base_flip) {
#line 2498
		mca_log_err("parse mode for paraller batt\n");
		ret = mca_wireless_quick_charge_parse_batt_info(info, info->batt_type);
	}

	return ret;
}

static void mca_wireless_quick_charge_force_stop_charging(struct mca_wireless_quick_charge_info *info)
{
	cancel_delayed_work_sync(&info->monitor_work);
	mca_wireless_quick_charge_stop_charging(info);
}

static int mca_wireless_quick_charge_chg_disable_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_wireless_quick_charge_info *info = data;

	if (!data)
		return -1;

	info->force_stop = effective_result;
#line 2533
	mca_log_info("force_stop %d\n", info->force_stop);
	if (info->force_stop && info->proc_data.charge_flag == MCA_QUICK_CHG_STS_CHARGING)
		mca_wireless_quick_charge_force_stop_charging(info);

	return 0;
}

static int mca_wireless_quick_charge_single_chg_cur_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_wireless_quick_charge_info *info = data;
	int i;

	if (!data)
		return -1;

	//info->sysfs_data.chg_limit[MCA_QUICK_CHG_CH_SINGLE] = effective_result;
	for (i = 0; i < MCA_WLS_QUICK_CHG_CH_MAX * CHG_MODE_MAX; i = i + 2) {
		if (effective_result)
			(void)mca_vote(info->voter[i], "if_ops", true, effective_result);
		else
			(void)mca_vote(info->voter[i], "if_ops", false, 0);
	}

	return 0;
}

static int mca_wireless_quick_charge_multi_chg_cur_voter_cb(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct mca_wireless_quick_charge_info *info = data;
	int i;

	if (!data)
		return -1;

	//info->sysfs_data.chg_limit[MCA_QUICK_CHG_CH_MULTI] = effective_result;
	for (i = 1; i < MCA_WLS_QUICK_CHG_CH_MAX * CHG_MODE_MAX; i = i + 2) {
		if (effective_result)
			(void)mca_vote(info->voter[i], "if_ops", true, effective_result);
		else
			(void)mca_vote(info->voter[i], "if_ops", false, 0);
	}

	return 0;
}

static int mca_wireless_quick_charge_create_voter(struct mca_wireless_quick_charge_info *info)
{
	struct mca_votable *smartchg_ichg_voter = NULL;

	info->input_limit_voter = mca_find_votable("wireless_buck_input");
	if (!info->input_limit_voter)
		goto error;
	info->charge_limit_voter = mca_find_votable("buck_charge_curr");
	if (!info->charge_limit_voter)
		goto error;
	info->chg_disable_voter = mca_create_votable("wls_quick_chg_disable",
		MCA_VOTE_OR, mca_wireless_quick_charge_chg_disable_voter_cb, 0, info);
	if (IS_ERR(info->chg_disable_voter))
		goto error;
	info->single_chg_cur_voter = mca_create_votable("wls_single_chg_cur",
		MCA_VOTE_MIN, mca_wireless_quick_charge_single_chg_cur_voter_cb, 0, info);
	if (IS_ERR(info->single_chg_cur_voter))
		goto error;
	info->multi_chg_cur_voter = mca_create_votable("wls_multi_chg_cur",
		MCA_VOTE_MIN, mca_wireless_quick_charge_multi_chg_cur_voter_cb, 0, info);
	if (IS_ERR(info->multi_chg_cur_voter))
		goto error;
	smartchg_ichg_voter = mca_find_votable("smartchg_delta_ichg");
	if (smartchg_ichg_voter)
		info->smartchg_data.delta_ichg = mca_get_effective_result(smartchg_ichg_voter);

	info->wls_thermal_flip_voter = mca_create_votable("wls_thermal_flip", MCA_VOTE_MIN,
		mca_wireless_quick_charge_thermal_flip_voter_cb, 0, info);
	if (IS_ERR(info->wls_thermal_flip_voter))
		goto error;
	info->flip_fcc_voter = mca_find_votable("flip_charge_curr");
	if (!info->flip_fcc_voter)
		goto error;

	mca_log_info("create voter succ\n");
	return 0;

error:
#line 2620
	mca_log_err("init voter failed\n");
	return -1;
}


static int mca_wireless_quick_charge_smartchg_delta_fv_callback(void *data, int effective_result)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;

	if (!data)
		return -1;

#line 2632
	mca_log_err("effective_result: %d\n", effective_result);
	info->smartchg_data.delta_fv = effective_result;
	return 0;
}

static int mca_wireless_quick_charge_smartchg_delta_ichg_callback(void *data, int effective_result)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;

	if (!data)
		return -1;

#line 2644
	mca_log_err("effective_result: %d\n", effective_result);
	info->smartchg_data.delta_ichg = effective_result;
	return 0;
}

static int mca_wireless_quick_charge_smartchg_soc_limit_callback(void *data, int effective_result)
{
	struct mca_wireless_quick_charge_info *info = (struct mca_wireless_quick_charge_info *)data;

	if (!data)
		return -1;

#line 2656
	mca_log_err("effective_result: %d\n", effective_result);
	info->soc_limit_enable = effective_result;
	mca_vote(info->chg_disable_voter, "soc_limit", true, effective_result);
	return 0;
}

static struct mca_smartchg_if_ops g_quick_wireless_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_WL_QC,
	.data = NULL,
	.set_delta_fv = mca_wireless_quick_charge_smartchg_delta_fv_callback,
	.set_delta_ichg = mca_wireless_quick_charge_smartchg_delta_ichg_callback,
	.set_soc_limit_sts = mca_wireless_quick_charge_smartchg_soc_limit_callback,
	.update_baa_para = mca_wireless_quick_charge_smartchg_update_baa_para,
};

static int mca_wireless_quick_charge_probe(struct platform_device *pdev)
{
	int ret = 0;
	static int probe_cnt;
	struct mca_wireless_quick_charge_info *info;

#line 2678
	mca_log_info("probe start probe_cnt: %d\n", ++probe_cnt);

	if (strategy_class_fg_ops_is_init_ok() <= 0) {
#line 2681
		mca_log_info("fg is not ready, wait for it\n");
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	ret = mca_wireless_quick_charge_parse_dt(info);
	if (ret) {
#line 2693
		mca_log_err("parst dts failed\n");
		return ret;
	}

	ret = mca_wireless_quick_charge_create_voter(info);
	if (ret) {
#line 2710
		mca_log_info("init voter fail, wait for it\n");
		return -EPROBE_DEFER;
	}

	INIT_DELAYED_WORK(&info->monitor_work, mca_wireless_quick_charge_monitor_work);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		mca_wireless_quick_charge_process_event, mca_wireless_quick_charge_get_status, NULL, info);

	g_quick_wireless_if_ops.data = info;
	(void)mca_charge_if_ops_register(&g_quick_wireless_if_ops);
	g_quick_wireless_smartchg_if_ops.data = info;
	mca_smartchg_if_ops_register(&g_quick_wireless_smartchg_if_ops);

	info->proc_data.sw_qc_ichg = 15600;
	info->proc_data.sw_thermal_ichg = 15600;

#line 2728
	mca_log_err("probe End\n");

	return 0;
}

static int mca_wireless_quick_charge_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_wireless_quick_charge_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,quick_wireless"},
	{},
};

static struct platform_driver mca_wireless_quick_charge_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_wireless_quick_charge",
		.of_match_table = match_table,
	},
	.probe = mca_wireless_quick_charge_probe,
	.remove = mca_wireless_quick_charge_remove,
	.shutdown = mca_wireless_quick_charge_shutdown,
};

static int __init mca_wireless_quick_charge_init(void)
{
	return platform_driver_register(&mca_wireless_quick_charge_driver);
}
late_initcall(mca_wireless_quick_charge_init);

static void __exit mca_wireless_quick_charge_exit(void)
{
	platform_driver_unregister(&mca_wireless_quick_charge_driver);
}
module_exit(mca_wireless_quick_charge_exit);

MODULE_DESCRIPTION("Xiaomi Wireless Quick Charge");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
