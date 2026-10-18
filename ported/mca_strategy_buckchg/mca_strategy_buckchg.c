// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_buckchg.c
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
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/strategy/strategy_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include "mca_strategy_buckchg.h"
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/common/mca_hwid.h>
//#include "hwid.h"
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_workqueue.h>
#include <mca/common/mca_smem.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/strategy/mca_wireless_revchg.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/common/mca_event.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_strategy_buckchg"
#endif

#define CHECK_VBUS_9V_HIGH_TH		7600
#define CHECK_VBUS_5V_LOW_TH		6000
#define DEFAULT_PD_CURRENT_MA		500

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info, int target_volt);
static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info);
static void strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info);
static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work);
static int strategy_buckchg_check_charger_change(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_start(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_stop(struct strategy_buckchg_dev *info);
static void strategy_buckchg_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work);
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work);
static void strategy_csd_pulse_process_workfunc(struct work_struct *work);
static void strategy_source_status_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work);
static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work);
static void strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work);
static void strategy_buckchg_otg_monitor_workfunc(struct work_struct *work);
static int strategy_buckchg_process_event(int event, int value, void *data);
static int strategy_buckchg_get_status(int status, void *value, void *data);
static int strategy_buckchg_set_config(int config, int value, void *data);
static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *nb, unsigned long event, void *data);
static int strategy_buckchg_panel_notifier_cb(struct notifier_block *nb, unsigned long event, void *data);

int protocol_class_get_bc12_adapter_detect_done(unsigned int protocol, bool *done);
static struct strategy_buckchg_dev *g_buckchg_info;

static int strategy_class_buckchg_ops_set_input_volt(struct strategy_buckchg_dev *info,
	int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_input_volt_lmt(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_input_volt_lmt(AUX_BUCK_CHARGER, mv);

	return ret;
}

static int strategy_class_buckchg_ops_set_chg(struct strategy_buckchg_dev *info, bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_chg(AUX_BUCK_CHARGER, en);

	return ret;
}

static int strategy_class_buckchg_ops_set_opt_fws(struct strategy_buckchg_dev *info, int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_opt_fws(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_opt_fws(AUX_BUCK_CHARGER, mv);

	return ret;
}

static int strategy_class_buckchg_ops_adc_enable(struct strategy_buckchg_dev *info, bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_adc_enable(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_adc_enable(AUX_BUCK_CHARGER, en);

	return ret;
}

static int strategy_class_buckchg_ops_set_usb_aicl_cont_thd(struct strategy_buckchg_dev *info, int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_usb_aicl_cont_thd(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_usb_aicl_cont_thd(AUX_BUCK_CHARGER, mv);

	return ret;
}

/*
 * G13b: parse_dt / parse_dt_override rebuilt from the OS4 stock (inlined into strategy_buckchg_class_probe):
 * property names, defaults and destinations are the stock ones (stock rodata strings + mov immediates).
 */
static void strategy_buckchg_parse_dt(struct strategy_buckchg_dev *info)
{
	struct device_node *np = info->dev->of_node;
	u32 idata[REV_USBIN_TYPE_MAX] = { 0 };
	struct otg_incold_threshold otg_thr[OTG_INCOLD_THRESHOLD_MAX];
	int ret = 0;
	int i, groups;

	mca_parse_dts_u32(np, "batt_type", &info->batt_type, 0);
	mca_parse_dts_u32(np, "support_multi_buck", &info->support_multi_buck, STATEGY_SUPPORT_MULTI_BUCK);
	mca_parse_dts_u32(np, "in_dcp", &info->in_dcp, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_pd", &info->in_pd, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_pps", &info->in_pps, info->in_pd);
	mca_parse_dts_u32(np, "in_hvdcp", &info->in_hvdcp, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_hvdcp3", &info->in_hvdcp3, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_hvdcp3p5", &info->in_hvdcp3p5, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_cdp", &info->in_cdp, CHARGE_CDP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_sdp", &info->in_sdp, CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "in_float", &info->in_float, CHARGE_FLOAT_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "chg_dcp", &info->chg_dcp, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_pd", &info->chg_pd, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_pps", &info->chg_pps, info->chg_pd);
	mca_parse_dts_u32(np, "chg_hvdcp", &info->chg_hvdcp, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_hvdcp3", &info->chg_hvdcp3, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_hvdcp3p5", &info->chg_hvdcp3p5, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_cdp", &info->chg_cdp, CHARGE_CDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_sdp", &info->chg_sdp, CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_float", &info->chg_float, CHARGE_FLOAT_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_batt_auth_failed", &info->chg_batt_auth_failed, CHARGE_BATT_AUTH_FAIL_DEFAULT);
	mca_parse_dts_u32(np, "ffc_temp_low", &info->ffc_temp_low, ALLOW_FFC_TEMP_LOW_THR);
	mca_parse_dts_u32(np, "ffc_temp_high", &info->ffc_temp_high, ALLOW_FFC_TEMP_HIGH_THR);
	mca_parse_dts_u32(np, "curr_terminate_compensation", &info->curr_term_compensation, 0);
	mca_parse_dts_u32(np, "allow_start_ffc_batt_soc_thr", &info->allow_start_ffc_batt_soc_thr, ALLOW_START_FFC_BATT_SOC_THR);
	mca_parse_dts_u32(np, "pmic_fv_compensation", &info->pmic_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_fv_compensation_cold", &info->pmic_fv_compensation_cold, 0);
	mca_parse_dts_u32(np, "bat_temp_fv_comp_cold_th", &info->bat_temp_fv_comp_cold_th, 0);
	mca_parse_dts_u32(np, "pmic_fv_compensation_hot", &info->pmic_fv_compensation_hot, 0);
	mca_parse_dts_u32(np, "bat_temp_fv_comp_hot_th", &info->bat_temp_fv_comp_hot_th, 50);
	mca_parse_dts_u32(np, "pmic_middle_fv_compensation", &info->pmic_middle_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_high_fv_compensation", &info->pmic_high_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_wls_fv_compensation", &info->pmic_wls_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_iterm_compensation", &info->pmic_iterm_compensation, 30);
	mca_parse_dts_u32(np, "vusb_ovp_location", &info->vusb_ovp_location, VUSB_OVP_BEFORE_CP);
	mca_parse_dts_u32(np, "terminated_by_cp", &info->terminated_by_cp, 0);
	mca_parse_dts_u32(np, "ffc_terminated_by_cp", &info->ffc_terminated_by_cp, 0);
	mca_parse_dts_u32(np, "sw_cv_fcc_step", &info->sw_cv_fcc_step, 50);
	mca_parse_dts_u32(np, "sw_cv_fv_step", &info->sw_cv_fv_step, 5);
	mca_parse_dts_u32(np, "sw_cv_fcc_limit_buffer", &info->sw_cv_fcc_limit_buffer, 0);
	mca_parse_dts_u32(np, "support_pps_100w", &info->support_pps_100w, 0);
	mca_parse_dts_u32(np, "drop_fv_cycle", &info->drop_fv_cycle, 0);
	info->support_diff_temp_comp = of_property_read_bool(np, "support_diff_temp_comp");
	info->support_reverse_quick_charge = of_property_read_bool(np, "support_reverse_quick_charge");
	info->support_revchg_screenon = of_property_read_bool(np, "support_revchg_screenon");
	info->support_base_flip = of_property_read_bool(np, "support-base-flip");
	info->base_flip_same = of_property_read_bool(np, "base-flip-same");
	info->support_pmic_vterm_dynamics_adjust = of_property_read_bool(np, "support-pmic-vterm-dynamics-adjust");
	info->need_cp_to_pmic = of_property_read_bool(np, "need-cp-to-pmic");
	info->support_charge_more = of_property_read_bool(np, "support-charge-more");
	info->support_disable_otg_incold = of_property_read_bool(np, "support-disable-otg-incold");

	memset(otg_thr, 0, sizeof(otg_thr));
	ret = mca_parse_dts_string_array(np, "otg_incold_thresholds", (int *)otg_thr, OTG_INCOLD_THRESHOLD_MAX, 2);
	if (ret >= 1) {
		groups = ret / 2;
		info->otg_threshold_size = groups;
		for (i = 0; i < groups && i < OTG_INCOLD_THRESHOLD_MAX; i++) {
			info->otg_temp_threshold[i] = otg_thr[i].temp_threshold;
			info->otg_soc_threshold[i] = otg_thr[i].soc_threshold;
		}
#line 247
		mca_log_info("OTG thresholds parsed: %d groups\n", info->otg_threshold_size);
	} else {
		/* defaults from the stock immediates: temp {0, -10, -20}, soc {10, 25, 35}, size 3 */
		info->otg_threshold_size = 3;
		info->otg_temp_threshold[0] = 0;
		info->otg_temp_threshold[1] = -10;
		info->otg_temp_threshold[2] = -20;
		info->otg_soc_threshold[0] = 10;
		info->otg_soc_threshold[1] = 25;
		info->otg_soc_threshold[2] = 35;
#line 257
		mca_log_info("Use default OTG thresholds\n");
	}

	ret = mca_parse_dts_u32_array(np, "rev_req_vadp", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_req_vadp[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
		info->rev_req_vadp[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
	} else
		memcpy(info->rev_req_vadp, idata, sizeof(idata));
#line 266
	mca_log_debug("rev_req_vadp %d %d\n", info->rev_req_vadp[REV_USBIN_TYPE_PPS],
		info->rev_req_vadp[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(np, "rev_vadp_valid_h", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
		info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
	} else
		memcpy(info->rev_vadp_valid_h, idata, sizeof(idata));
#line 275
	mca_log_debug("rev_vadp_valid_h %d %d\n", info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS],
		info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(np, "rev_vadp_valid_l", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
		info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
	} else
		memcpy(info->rev_vadp_valid_l, idata, sizeof(idata));
#line 284
	mca_log_debug("rev_vadp_valid_l %d %d\n", info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS],
		info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER]);
}

/* per-product override node (stock: only for product names "donghai" and "yanan"); every property keeps the current value as default */
static void strategy_buckchg_parse_dt_override(struct strategy_buckchg_dev *info, const struct mca_hwid *hwid)
{
	struct device_node *child;
	u32 idata[REV_USBIN_TYPE_MAX] = { 0 };
	struct otg_incold_threshold otg_thr[OTG_INCOLD_THRESHOLD_MAX];
	int ret = 0;
	int i, groups;

	if (!hwid)
		return;
	if (strcmp(hwid->product_name, "donghai") && strcmp(hwid->product_name, "yanan"))
		return;

	child = of_get_child_by_name(info->dev->of_node, "strategy_buckchg_class_override");
	if (!child) {
#line 297
		mca_log_info("child node 'strategy_buckchg_class_override' not found, using parent config\n");
		return;
	}
#line 301
	mca_log_info("applying strategy_buckchg override from child node\n");

	mca_parse_dts_u32(child, "batt_type", &info->batt_type, info->batt_type);
	mca_parse_dts_u32(child, "support_multi_buck", &info->support_multi_buck, info->support_multi_buck);
	mca_parse_dts_u32(child, "in_dcp", &info->in_dcp, info->in_dcp);
	mca_parse_dts_u32(child, "in_pd", &info->in_pd, info->in_pd);
	mca_parse_dts_u32(child, "in_pps", &info->in_pps, info->in_pps);
	mca_parse_dts_u32(child, "in_hvdcp", &info->in_hvdcp, info->in_hvdcp);
	mca_parse_dts_u32(child, "in_hvdcp3", &info->in_hvdcp3, info->in_hvdcp3);
	mca_parse_dts_u32(child, "in_hvdcp3p5", &info->in_hvdcp3p5, info->in_hvdcp3p5);
	mca_parse_dts_u32(child, "in_cdp", &info->in_cdp, info->in_cdp);
	mca_parse_dts_u32(child, "in_sdp", &info->in_sdp, info->in_sdp);
	mca_parse_dts_u32(child, "in_float", &info->in_float, info->in_float);
	mca_parse_dts_u32(child, "chg_dcp", &info->chg_dcp, info->chg_dcp);
	mca_parse_dts_u32(child, "chg_pd", &info->chg_pd, info->chg_pd);
	mca_parse_dts_u32(child, "chg_pps", &info->chg_pps, info->chg_pps);
	mca_parse_dts_u32(child, "chg_hvdcp", &info->chg_hvdcp, info->chg_hvdcp);
	mca_parse_dts_u32(child, "chg_hvdcp3", &info->chg_hvdcp3, info->chg_hvdcp3);
	mca_parse_dts_u32(child, "chg_hvdcp3p5", &info->chg_hvdcp3p5, info->chg_hvdcp3p5);
	mca_parse_dts_u32(child, "chg_cdp", &info->chg_cdp, info->chg_cdp);
	mca_parse_dts_u32(child, "chg_sdp", &info->chg_sdp, info->chg_sdp);
	mca_parse_dts_u32(child, "chg_float", &info->chg_float, info->chg_float);
	mca_parse_dts_u32(child, "chg_batt_auth_failed", &info->chg_batt_auth_failed, info->chg_batt_auth_failed);
	mca_parse_dts_u32(child, "ffc_temp_low", &info->ffc_temp_low, info->ffc_temp_low);
	mca_parse_dts_u32(child, "ffc_temp_high", &info->ffc_temp_high, info->ffc_temp_high);
	mca_parse_dts_u32(child, "curr_terminate_compensation", &info->curr_term_compensation, info->curr_term_compensation);
	mca_parse_dts_u32(child, "allow_start_ffc_batt_soc_thr", &info->allow_start_ffc_batt_soc_thr, info->allow_start_ffc_batt_soc_thr);
	mca_parse_dts_u32(child, "pmic_fv_compensation", &info->pmic_fv_compensation, info->pmic_fv_compensation);
	mca_parse_dts_u32(child, "pmic_fv_compensation_cold", &info->pmic_fv_compensation_cold, info->pmic_fv_compensation_cold);
	mca_parse_dts_u32(child, "bat_temp_fv_comp_cold_th", &info->bat_temp_fv_comp_cold_th, info->bat_temp_fv_comp_cold_th);
	mca_parse_dts_u32(child, "pmic_fv_compensation_hot", &info->pmic_fv_compensation_hot, info->pmic_fv_compensation_hot);
	mca_parse_dts_u32(child, "bat_temp_fv_comp_hot_th", &info->bat_temp_fv_comp_hot_th, info->bat_temp_fv_comp_hot_th);
	mca_parse_dts_u32(child, "pmic_middle_fv_compensation", &info->pmic_middle_fv_compensation, info->pmic_middle_fv_compensation);
	mca_parse_dts_u32(child, "pmic_high_fv_compensation", &info->pmic_high_fv_compensation, info->pmic_high_fv_compensation);
	mca_parse_dts_u32(child, "pmic_wls_fv_compensation", &info->pmic_wls_fv_compensation, info->pmic_wls_fv_compensation);
	mca_parse_dts_u32(child, "pmic_iterm_compensation", &info->pmic_iterm_compensation, info->pmic_iterm_compensation);
	mca_parse_dts_u32(child, "vusb_ovp_location", &info->vusb_ovp_location, info->vusb_ovp_location);
	mca_parse_dts_u32(child, "terminated_by_cp", &info->terminated_by_cp, info->terminated_by_cp);
	mca_parse_dts_u32(child, "ffc_terminated_by_cp", &info->ffc_terminated_by_cp, info->ffc_terminated_by_cp);
	mca_parse_dts_u32(child, "sw_cv_fcc_step", &info->sw_cv_fcc_step, info->sw_cv_fcc_step);
	mca_parse_dts_u32(child, "sw_cv_fv_step", &info->sw_cv_fv_step, info->sw_cv_fv_step);
	mca_parse_dts_u32(child, "sw_cv_fcc_limit_buffer", &info->sw_cv_fcc_limit_buffer, info->sw_cv_fcc_limit_buffer);
	mca_parse_dts_u32(child, "support_pps_100w", &info->support_pps_100w, info->support_pps_100w);
	if (of_property_read_bool(child, "support_diff_temp_comp"))
		info->support_diff_temp_comp = true;
	if (of_property_read_bool(child, "support_reverse_quick_charge"))
		info->support_reverse_quick_charge = 1;
	if (of_property_read_bool(child, "support_revchg_screenon"))
		info->support_revchg_screenon = 1;
	if (of_property_read_bool(child, "support-base-flip"))
		info->support_base_flip = 1;
	if (of_property_read_bool(child, "base-flip-same"))
		info->base_flip_same = 1;
	if (of_property_read_bool(child, "support-pmic-vterm-dynamics-adjust"))
		info->support_pmic_vterm_dynamics_adjust = 1;
	if (of_property_read_bool(child, "need-cp-to-pmic"))
		info->need_cp_to_pmic = 1;
	if (of_property_read_bool(child, "support-charge-more"))
		info->support_charge_more = 1;
	if (of_property_read_bool(child, "support-disable-otg-incold"))
		info->support_disable_otg_incold = true;

	memset(otg_thr, 0, sizeof(otg_thr));
	ret = mca_parse_dts_string_array(child, "otg_incold_thresholds", (int *)otg_thr, OTG_INCOLD_THRESHOLD_MAX, 2);
	if (ret >= 1) {
		groups = ret / 2;
		info->otg_threshold_size = groups;
		for (i = 0; i < groups && i < OTG_INCOLD_THRESHOLD_MAX; i++) {
			info->otg_temp_threshold[i] = otg_thr[i].temp_threshold;
			info->otg_soc_threshold[i] = otg_thr[i].soc_threshold;
		}
#line 373
		mca_log_info("overridden OTG thresholds: %d groups\n", info->otg_threshold_size);
	}

	if (!mca_parse_dts_u32_array(child, "rev_req_vadp", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_req_vadp, idata, sizeof(idata));
	if (!mca_parse_dts_u32_array(child, "rev_vadp_valid_h", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_vadp_valid_h, idata, sizeof(idata));
	if (!mca_parse_dts_u32_array(child, "rev_vadp_valid_l", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_vadp_valid_l, idata, sizeof(idata));
#line 388
	mca_log_info("strategy_buckchg override parse completed\n");
}

static int strategy_buckchg_enable(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
#line 398
	mca_log_err("%d\n", effective_result);
	if (effective_result)
		return strategy_class_buckchg_ops_set_chg(info, true);
	else
		return strategy_class_buckchg_ops_set_chg(info, false);
}

static int strategy_buckchg_input_suspend(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
#line 412
	mca_log_err("%d\n", effective_result);
	if (effective_result)
		mca_vote(info->input_limit_voter, "input_suspend", true, 0);
	else
		mca_vote(info->input_limit_voter, "input_suspend", false, 0);
	return 0;
}

static int strategy_buckchg_input_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	struct timespec64 ts;
	int is_zero_speed = 0;
	int result = effective_result;

	if (!data)
		return -1;

	ts = ns_to_timespec64(ktime_get_boottime());
	if ((u64)ts.tv_sec < 60) {
		get_smem_battery_info(&is_zero_speed);
		if (is_zero_speed && effective_result) {
			result = effective_result > 1500 ? effective_result : 1500;
#line 460
			mca_log_err("is_zero_speed, result = %d, effective_result = %d\n", result, effective_result);
		}
	}

#line 464
	mca_log_err("set input current %d\n", result);

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_input_curr_lmt(MAIN_BUCK_CHARGER, result);
}

static int strategy_buckchg_charge_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

#line 486
	mca_log_err("set chg current %d\n", effective_result);

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_ichg(MAIN_BUCK_CHARGER, effective_result);
}

/*
 * iterm compensation (pmic_iterm_compensation) for the base-flip / base-flip-same models.
 * Thresholds (30..39 C, 18..29 C, parallel_iterm 1046/598/187, results 104/82/50/20/14, 350 -> 75/68) are the stock immediates.
 */
static int strategy_buckchg_set_iterm(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	int temp = 0;

	if (!data)
		return -1;

	if (info->support_base_flip) {
		if (strategy_class_fg_get_fastcharge()) {
			strategy_class_fg_ops_get_temperature(&temp);
			temp = temp / 10;
			if (temp >= 30 && temp <= 39 && info->parallel_iterm == 1046)
				info->pmic_iterm_compensation = 104;
			else if (temp >= 18 && temp <= 29 && info->parallel_iterm == 598)
				info->pmic_iterm_compensation = 82;
			else
				info->pmic_iterm_compensation = 50;
		} else if (info->parallel_iterm == 187) {
			info->pmic_iterm_compensation = 20;
		} else {
			info->pmic_iterm_compensation = 14;
		}
	}

	if (info->base_flip_same) {
		if (strategy_class_fg_get_fastcharge()) {
			strategy_class_fg_ops_get_temperature(&temp);
			temp = temp / 10;
			info->pmic_iterm_compensation = temp < 35 ? 75 : 68;
		} else {
			info->pmic_iterm_compensation = 20;
		}
	}

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_term_curr(MAIN_BUCK_CHARGER, info->pmic_iterm_compensation + effective_result);
}

/*
 * vterm voter callback.  Compensation tables (pmic_fv_compensation + cold/hot/middle/high/wls) are applied depending on the
 * battery temperature (fg temperature / 10, 0.1 C units from the gauge); every threshold below is a stock immediate:
 * 180..299 / 300..399 / >= 400 (0.1 C), base-flip temperatures 33..34 / 48..54 C, compensations 10/20/30/40/45 mV.
 */
static int strategy_buckchg_set_vterm(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	int offset_flag = 0;
	int temp = 0;
	int *comp;
	int vterm;
	int v;

	if (!data)
		return -1;

	if (info->support_base_flip) {
		if (!strategy_class_fg_get_fastcharge())
			info->pmic_fv_compensation = 10;
		else if (info->wls_online)
			info->pmic_fv_compensation = info->pmic_wls_fv_compensation;
		else
			info->pmic_fv_compensation = 20;
	}

	strategy_class_fg_ops_get_temperature(&temp);
	temp = temp / 10;
	if (temp < info->bat_temp_fv_comp_cold_th && info->support_diff_temp_comp)
		comp = &info->pmic_fv_compensation_cold;
	else if (temp > info->bat_temp_fv_comp_hot_th && info->support_diff_temp_comp)
		comp = &info->pmic_fv_compensation_hot;
	else
		comp = &info->pmic_fv_compensation;
	v = *comp + effective_result;
	vterm = v;

	if (info->support_pmic_vterm_dynamics_adjust) {
		if (!strategy_class_fg_get_fastcharge()) {
			vterm = effective_result;
		} else {
			int raw;

			strategy_class_fg_ops_get_temperature(&temp);
			raw = temp;
			temp = temp / 10;
			if (raw >= 180 && raw <= 299)
				v = info->pmic_fv_compensation + effective_result;
			if (temp >= 30 && temp <= 39)
				vterm = info->pmic_middle_fv_compensation + effective_result;
			else if (raw >= 400)
				vterm = info->pmic_high_fv_compensation + effective_result;
			else
				vterm = v;
		}
	}

	if (info->base_flip_same) {
		bool fast = strategy_class_fg_get_fastcharge();
		int c;

		strategy_class_fg_get_temp_offset_flag(&offset_flag);
		if (fast) {
			if (temp > 34 || (temp >= 33 && offset_flag))
				c = 45;
			else
				c = 40;
		} else if (temp >= 48 && temp <= 54) {
			c = 30;
		} else {
			c = 0;
		}
		info->pmic_fv_compensation = c;
		vterm = c + effective_result;
	}

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, vterm);
}

static int strategy_buckchg_input_voltage(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->hvdcp_allow_flag = effective_result;
	info->proc_data.voltage = effective_result ? STATEGY_CHARGE_VBUS_9V : STATEGY_CHARGE_VBUS_5V;

	return 0;
}

static int strategy_buckchg_set_5v_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_5v_in = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_5V)
		return 0;

	if (info->buck_5v_in)
		mca_vote(info->input_limit_voter, "volt_limit", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "volt_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_5v_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_5v_ich = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_5V)
		return 0;

	if (info->buck_5v_ich)
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", true, effective_result);
	else
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_9v_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_9v_in = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V)
		return 0;

	if (info->buck_9v_in)
		mca_vote(info->input_limit_voter, "volt_limit", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "volt_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_9v_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_9v_ich = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V)
		return 0;

	if (info->buck_9v_ich)
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", true, effective_result);
	else
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", false, effective_result);

	return 0;
}

/* ===== G13b: functions rebuilt from the OS4 stock disassembly (see STATUS.md for the list) ===== */
static void strategy_buckchg_stop_charging(struct strategy_buckchg_dev *info);
static void strategy_buckchg_start_charging(struct strategy_buckchg_dev *info);
static void strategy_buckchg_process_soc_limit_change_more(int value, struct strategy_buckchg_dev *info);

static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info)
{
	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		mca_vote(info->input_voltage_voter, "real_type", true, 1);
		break;
	case XM_CHARGER_TYPE_PPS:
		if (mca_strategy_is_support_pps_100w()) {
			mca_vote(info->input_suppend_voter, "real_type", true, 0);
			break;
		}
		fallthrough;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (info->proc_data.is_pd_9v)
			mca_vote(info->input_voltage_voter, "real_type", true, 1);
		else
			mca_vote(info->input_voltage_voter, "real_type", true, 0);
		break;
	default:
		mca_vote(info->input_voltage_voter, "real_type", true, 0);
		break;
	}

#line 2074
	mca_log_info("real_type = %d, input_voltage_voter effective_result: %d\n",
		info->proc_data.real_type, mca_get_effective_result(info->input_voltage_voter));
}

static void stategy_buckchg_is_pdo_9v(struct strategy_buckchg_dev *info)
{
	int i;

	if ((unsigned int)(info->proc_data.real_type - XM_CHARGER_TYPE_PD) > 2)
		return;

	if (info->pdo_nums == 1 && info->pwr_cap.nums >= 2)
		queue_delayed_work(system_wq, &info->rerun_handle_pd_auth_work, msecs_to_jiffies(1000));

	info->pdo_nums = info->pwr_cap.nums;
	if (!info->pwr_cap.nums) {
#line 891
		mca_log_info(" pwr_cap.nums is null\n");
		return;
	}

	for (i = 0; i < info->pwr_cap.nums; i++) {
		if (info->pwr_cap.cap[i].max_voltage == STATEGY_CHARGE_VBUS_9V &&
			info->pwr_cap.cap[i].min_voltage == STATEGY_CHARGE_VBUS_9V)
			break;
	}
	if (i < info->pwr_cap.nums) {
		info->proc_data.is_pd_9v = true;
		info->proc_data.curr_pd_pos = i;
#line 900
		mca_log_info("pdo[%d] can support 9v: %d\n", i, true);
	} else {
		info->proc_data.is_pd_9v = false;
		info->proc_data.curr_pd_pos = 0;
	}
}

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info, int target_volt)
{
	if (!target_volt) {
#line 2014
		mca_log_info("target_volt = 0v is invalid\n");
		return;
	}

	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		platform_class_buckchg_ops_set_qc_volt(MAIN_BUCK_CHARGER, target_volt);
		break;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (!info->pwr_cap.nums) {
#line 2027
			mca_log_info("pwr_cap nums is null\n");
			break;
		}
		if (target_volt == STATEGY_CHARGE_VBUS_5V || target_volt == STATEGY_CHARGE_VBUS_12V ||
			target_volt == STATEGY_CHARGE_VBUS_9V) {
			protocol_class_pd_set_fixed_volt(0, target_volt);
		} else {
#line 2041
			mca_log_info("set no fix pdo\n");
			protocol_class_set_adapter_volt_and_curr(ADAPTER_PROTOCOL_PPS, target_volt, 3000);
		}
		break;
	case XM_CHARGER_TYPE_PD:
		if (!info->pwr_cap.nums) {
#line 2035
			mca_log_info("pwr_cap nums is null\n");
			break;
		}
		protocol_class_pd_set_fixed_volt(0, target_volt);
		break;
	default:
		break;
	}
}

static void strategy_buckchg_start_charging(struct strategy_buckchg_dev *info)
{
#line 770
	mca_log_info("start charging\n");
	strategy_buck_update_req_volt(info);
	cancel_delayed_work_sync(&info->monitor_work);
	strategy_class_buckchg_ops_adc_enable(info, true);
	queue_delayed_work(system_wq, &info->monitor_work, 0);
}

static void strategy_buckchg_process_cap_change(struct strategy_buckchg_dev *info)
{
	if (info->proc_data.real_type >= XM_CHARGER_TYPE_PD && info->proc_data.real_type <= XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD, &info->pwr_cap);
		protocol_class_pd_get_suspend_support_status(0, &info->proc_data.pdsuspendsupported);
	}
	stategy_buckchg_is_pdo_9v(info);
	strategy_buck_update_req_volt(info);
	mod_delayed_work(system_wq, &info->monitor_work, 0);
}

static void strategy_buckchg_process_pmic_init_done(struct strategy_buckchg_dev *info)
{
#line 1092
	mca_log_info("deal with pmic_init_done\n");
	mca_rerun_election(info->chg_enable_voter);
	mca_rerun_election(info->input_limit_voter);
	mca_rerun_election(info->charge_limit_voter);
	mca_rerun_election(info->iterm_voter);
	mca_rerun_election(info->vterm_voter);
}

static void strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info)
{
	strategy_buckchg_set_charge_volt(info, info->proc_data.voltage);
	mca_vote(info->input_limit_voter, "wireless_revchg", false, 0);
	info->proc_data.wls_revchg_init_done = false;
	info->rev_icl_for_qc2 = false;
}

static void strategy_buckchg_process_antiburn_change(int value, struct strategy_buckchg_dev *info)
{
#line 956
	mca_log_info("value: %d\n", value);
	strategy_buckchg_set_charge_volt(info, STATEGY_CHARGE_VBUS_5V);
	if (value)
		strategy_buckchg_stop_charging(info);
	else if (info->proc_data.online)
		strategy_buckchg_start_charging(info);
}

static void strategy_buckchg_process_batt_btb_change(int value, struct strategy_buckchg_dev *info)
{
	if (value) {
		mca_vote(info->input_voltage_voter, "batt_miss", true, 0);
		mca_vote(info->input_limit_voter, "batt_miss", true, info->batt_type ? 1000 : 500);
		mca_vote(info->vterm_voter, "batt_miss", true, STAEGY_BATT_MISS_FV);
	} else {
		mca_vote(info->input_voltage_voter, "batt_miss", false, 0);
		mca_vote(info->input_limit_voter, "batt_miss", false, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE);
		mca_vote(info->vterm_voter, "batt_miss", false, STATEGY_VTERM_DEFAULT_VALUE);
	}
	mca_vote_override(info->input_limit_voter, "master_batt_missing", false, 0);
}

static void strategy_buckchg_process_csd_pulse(int value, struct strategy_buckchg_dev *info)
{
	int ffc = strategy_class_fg_get_fastcharge();

#line 1072
	mca_log_info("value: %d, ffc_sts: %d\n", value, ffc);
	if (value >= 1 && ffc == 1)
		strategy_class_fg_set_fastcharge(0);

	if (value == 1 || value == 2) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
		info->csd_flag = true;
		queue_delayed_work(system_wq, &info->csd_pulse_process_work, msecs_to_jiffies(75000));
	} else if (value == 0) {
		cancel_delayed_work_sync(&info->csd_pulse_process_work);
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
		mca_vote(info->chg_enable_voter, "csd_pulse", false, 0);
		info->csd_flag = false;
	} else if (value == 3) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
	}
}

static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct strategy_buckchg_dev *info = container_of(nb, struct strategy_buckchg_dev, thermal_board_nb);

	if (event == MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE)
		info->thermal_board_temp = *(int *)data / 100;
	return 0;
}

static int strategy_buckchg_panel_notifier_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct strategy_buckchg_dev *info = container_of(nb, struct strategy_buckchg_dev, panel_nb);
	int screen;

	if (event == MCA_EVENT_PANEL_SCREEN_STATE_CHANGE) {
		screen = *(int *)data;
#line 3092
		mca_log_info("update screen_state: %d => %d\n", info->screen_status, screen);
		info->screen_status = screen != 0;
	}
	return 0;
}

static void strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, soc_limit_stepper_work.work);

	strategy_buckchg_process_soc_limit_change_more(info->soc_limit, info);
}

static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work)
{
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS, MCA_EVENT_USB_STS_CHANGE, NULL);
#line 875
	mca_log_info("send usb power_supply trigger handle pd auth\n");
}

static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, check_pd_secret_work.work);

	mca_vote(info->input_limit_voter, "icl_limit", false, STATEGY_INPUT_DEFAULT_VALUE);
#line 2973
	mca_log_info("cancel icl_limit vote when using pd charger\n");
}

static int strategy_buckchg_smartchg_set_fcc_callback(void *data, int effective_result)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
	if (effective_result)
		mca_vote(info->charge_limit_voter, "smartchg", true, effective_result);
	else
		mca_vote(info->charge_limit_voter, "smartchg", false, effective_result);
	return 0;
}

static void strategy_buckchg_process_type_change(int value, struct strategy_buckchg_dev *info);
static void strategy_buckchg_cp_revert_handler(int revchg_sts, struct strategy_buckchg_dev *info);
static void strategy_buckchg_process_cap_change(struct strategy_buckchg_dev *info);
/*
 * G13b: process_event rebuilt from the OS4 stock (jump table of 80 entries over enum mca_event_notify_list).  The online change code
 * (events 0/1, strategy_buckchg_process_online_change in the stock logs) is inlined as in the stock.
 * Every case body below was read from the stock disassembly; the voter value arguments (e.g. true/0 and false/1) are literal.
 */
static int strategy_buckchg_process_event(int event, int value, void *data)
{
	struct strategy_buckchg_dev *info = data;
	struct mca_event_notify_data n_data;
	char uevent[MCA_EVENT_NOTIFY_SIZE];
	bool otg_plug = false;
	bool e0 = false, e1 = false;
	bool cp_en = false;
	int soc_low, soc_high;

	if (!data)
		return -1;

#line 1278
	mca_log_info("receive event %d, value %d\n", event, value);

	switch (event) {
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_USB_CONNECT:
		if (info->proc_data.online == value)
			return 0;
		info->proc_data.online = value;
		if (value) {
			if (info->support_base_flip) {
				platform_fg_ops_get_error_state(0, &e0);
				platform_fg_ops_get_error_state(1, &e1);
				if (!e0 || e1)
					platform_class_loadsw_set_lowpower_mode(0, false);
			}
			strategy_buckchg_start_charging(info);
		} else {
			platform_class_loadsw_set_lowpower_mode(0, true);
			info->pps_ptf = 0;
			strategy_buckchg_stop_charging(info);
			info->verify_process_end = 0;
			info->proc_data.charge_done_force_5v = false;
			info->buck_abnormal_cnt = 0;
			if (info->base_flip_same) {
				platform_class_cp_set_busovp(CP_ROLE_MASTER, 13000);
				platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &cp_en);
				mca_log_info("cp_enabled = %d in discharging\n", cp_en);
			}
		}
		return 0;
	case MCA_EVENT_WIRELESS_DISCONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		info->wls_online = value != 0;
		goto otg_plug_check;
	case MCA_EVENT_WIRELESS_REVCHG:
		info->wls_revchg_online = value != 0;
otg_plug_check:
		protocol_class_pd_get_otg_plugin_status(0, &otg_plug);
		if (info->support_reverse_quick_charge && otg_plug && info->start_quick_revchg) {
			cancel_delayed_work_sync(&info->source_status_monitor_work);
			queue_delayed_work(system_wq, &info->source_status_monitor_work, 0);
		}
		return 0;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		strategy_buckchg_process_type_change(value, info);
		break;
	case MCA_EVENT_CHARGE_CAP_CHANGE:
		strategy_buckchg_process_cap_change(info);
		break;
	case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
#line 1358
		mca_log_info("receive pd verify process end event, value %d\n", value);
		info->verify_process_end = value;
		if (value)
			mca_vote(info->input_limit_voter, "icl_limit", false, STATEGY_INPUT_DEFAULT_VALUE);
		break;
	case MCA_EVENT_WIRELESS_USB_REVCHG:
#line 1348
		mca_log_info("wireless revchg event %d\n", value);
		info->wls_revchg_en = value;
		if (value) {
			strategy_wls_revchg_monitor_workfunc(&info->wls_revchg_monitor_work.work);
		} else {
			cancel_delayed_work_sync(&info->wls_revchg_monitor_work);
			strategy_buckchg_exit_wireless_revchg(info);
		}
		break;
	case MCA_EVENT_BATTERY_TOTAL_ITERM:
#line 1440
		mca_log_info("parallel_iterm: %d\n", value);
		info->parallel_iterm = value;
		return 0;
	case MCA_EVENT_CP_CBOOT_FAIL:
		if (value)
			mca_vote(info->input_voltage_voter, "cp_cboot_short", true, 0);
		else
			mca_vote(info->input_voltage_voter, "cp_cboot_short", false, 1);
		break;
	case MCA_EVENT_CHARGE_RESTORE:
		platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &info->proc_data.online);
		if (info->proc_data.online)
			strategy_buckchg_start_charging(info);
		break;
	case MCA_EVENT_CONN_ANTIBURN_CHANGE:
		strategy_buckchg_process_antiburn_change(value, info);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		info->batt_missing = value;
		strategy_buckchg_process_batt_btb_change(value, info);
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
#line 1307
		mca_log_err("receive batt_auth event, value %d", value);
		info->batt_auth = 1;
		mca_vote(info->input_voltage_voter, "batt_auth", false, 0);
		mca_vote(info->charge_limit_voter, "batt_auth", false, info->chg_batt_auth_failed);
		if (info->proc_data.online)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
		if (value)
			mca_vote(info->input_voltage_voter, "lpd", true, 0);
		else
			mca_vote(info->input_voltage_voter, "lpd", false, 0);
		break;
	case MCA_EVENT_PMIC_INIT_DONE:
		strategy_buckchg_process_pmic_init_done(info);
		break;
	case MCA_EVENT_CC_SHORT_VBUS:
		if (value)
			mca_vote(info->input_voltage_voter, "cc_short_vbus", true, 0);
		else
			mca_vote(info->input_voltage_voter, "cc_short_vbus", false, 1);
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value)
			mca_vote(info->chg_enable_voter, "vbat_ovp", true, 0);
		else
			mca_vote(info->chg_enable_voter, "vbat_ovp", false, 1);
		break;
	case MCA_EVENT_QUICK_REVCHG_CHANGE:
		if (info->support_reverse_quick_charge)
			strategy_buckchg_cp_revert_handler(value, info);
		break;
	case MCA_EVENT_BOOST_STS:
		if (!info->support_disable_otg_incold)
			break;
		if (value == 1) {
#line 1453
			mca_log_info("otg inserted, start otg monitor work after 500ms delay\n");
			mca_queue_delayed_work(&info->otg_monitor_work, 125);
		} else {
			mutex_lock(&info->otg_incold_mutex);
			if (info->disable_otg_incold) {
				info->disable_otg_incold = false;
#line 1461
				mca_log_err("otg plug out, report now\n");
				memset(uevent, 0, sizeof(uevent));
				n_data.event = uevent;
				n_data.event_len = snprintf(uevent, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_DISABLE_OTG_INCOLD=%d", info->disable_otg_incold);
				mca_event_report_uevent(&n_data);
			}
			mutex_unlock(&info->otg_incold_mutex);
#line 1471
			mca_log_info("otg removed, cancel otg monitor work\n");
			cancel_delayed_work_sync(&info->otg_monitor_work);
		}
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		strategy_buckchg_stop_charging(info);
		break;
	case MCA_EVENT_CSD_SEND_PULSE:
		strategy_buckchg_process_csd_pulse(value, info);
		break;
	case MCA_EVENT_PPS_PTF:
		info->pps_ptf = value;
#line 1391
		mca_log_info("set PPS_PTF %d\n", value);
		if (info->pps_ptf == USBPD_BUCK_DPM_PORT_PPS_PTF_WARNING)
			mca_vote(info->input_voltage_voter, "ptf", true, 1);
		break;
	case MCA_EVENT_IS_EU_MODEL:
		info->is_eu_model = value != 0;
#line 1397
		mca_log_err("set buck is_eu_model %d\n", value);
		platform_class_buckchg_ops_set_eu_model(MAIN_BUCK_CHARGER, info->is_eu_model);
		break;
	case MCA_EVENT_PLATE_SHOCK:
		if (value)
			mca_vote(info->input_limit_voter, "plate_shock", true, STAEGY_CHARGE_PLATE_SHOCK);
		else
			mca_vote(info->input_limit_voter, "plate_shock", false, 0);
		break;
	case MCA_EVENT_START_QUICK_REVCHG:
		info->start_quick_revchg = true;
		if (info->support_reverse_quick_charge)
			queue_delayed_work(system_wq, &info->source_status_monitor_work, 0);
		break;
	case MCA_EVENT_REVCHG_BCL:
		info->revchg_bcl = value != 0;
		return 0;
	case MCA_EVENT_HANDLE_ALLOW_CHARGE:
#line 1330
		mca_log_info("handle_allow_charge: %d\n", value);
		if (value) {
			mca_vote(info->input_limit_voter, "handle", false, 0);
			mca_vote(info->chg_enable_voter, "handle", false, 0);
		} else {
			mca_vote(info->input_limit_voter, "handle", true, 0);
			mca_vote(info->chg_enable_voter, "handle", true, 0);
		}
		break;
	case MCA_EVENT_BUCKCHG_BATT_OV:
		if (!info->sw_cv_running)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
#line 1427
		mca_log_info("memory_test enable soc_limit: %d\n", value);
		if (value) {
			info->soc_limit_low = 1;
			info->soc_limit_high = 3;
		} else {
			info->soc_limit_low = 0;
			info->soc_limit_high = 0;
		}
		return 0;
	case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
		soc_low = (value >> 8) & 0xff;	/* shift/mask of the high byte: not fully read, GUESS */
		soc_high = value & 0xff;
		info->soc_limit_low = soc_low;
		info->soc_limit_high = soc_high;
#line 1424
		mca_log_info("debug_ctrl set soc_limit: %d %d\n", soc_low, soc_high);
		break;
	default:
		return 0;
	}

	return 0;
}

static void strategy_buckchg_reset_charge_para(struct strategy_buckchg_dev *info)
{
	memset(&info->proc_data, 0, sizeof(info->proc_data));
	memset(&info->pwr_cap, 0, sizeof(info->pwr_cap));
	info->proc_data.voltage = STATEGY_CHARGE_VBUS_5V;
	info->is_non_compliant_qc = false;
	info->non_compliant_run_once = false;
	info->vbat_ov_count = 0;
	mca_vote(info->input_voltage_voter, "non_compliant_qc", false, 0);
	mca_vote(info->input_limit_voter, "non_compliant_qc", false, 0);

	mca_vote(info->chg_enable_voter, "online", true, STATEGY_CHARGE_DISENABLE);
	mca_vote(info->buck_5v_in_voter, "wire_chg_type",
		true, STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_in_voter, "wire_chg_type",
		true, STATEGY_INPUT_DEFAULT_VALUE);
	mca_vote(info->input_limit_voter, "icl_limit", true, STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_in_voter, "pmic_inductor", false, 0);
	mca_vote(info->buck_5v_in_voter, "pmic_inductor", false, 0);

	mca_vote(info->buck_5v_ich_voter, "wire_chg_type",
		true, STATEGY_INPUT_DEFAULT_VALUE);

	mca_vote(info->buck_9v_ich_voter, "wire_chg_type",
		true, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE);
	mca_vote(info->input_voltage_voter, "real_type", true, 0);
	mca_vote(info->input_voltage_voter, "eoc_5v", false, 0);
	mca_vote(info->chg_enable_voter, "vbat_ovp", false, STATEGY_CHARGE_ENABLE);
	mca_vote(info->input_voltage_voter, "lpd", false, 0);
	cancel_delayed_work_sync(&info->csd_pulse_process_work);
	mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
	mca_vote(info->charge_limit_voter, "qc_done", false, 0);
	mca_vote(info->chg_enable_voter, "csd_pulse", false, STATEGY_CHARGE_DISENABLE);
	mca_vote(info->charge_limit_voter, "full_replug", false, 0);
	info->csd_flag = false;

	/*xring system abnormal use default ibus and ibat 500mA */
	if (0 /* G13b: template-only dt flag, not in OS4 stock */) {
		mca_vote(info->chg_enable_voter, "online", true, STATEGY_CHARGE_ENABLE);
		mca_vote(info->buck_5v_in_voter, "wire_chg_type",
				false, STATEGY_INPUT_DEFAULT_VALUE);
		mca_vote(info->buck_5v_ich_voter, "wire_chg_type",
				false, STATEGY_INPUT_DEFAULT_VALUE);
		mca_vote(info->buck_9v_in_voter, "wire_chg_type",
				false, STATEGY_INPUT_DEFAULT_VALUE);
		mca_vote(info->input_limit_voter, "icl_limit",
				false, STATEGY_INPUT_DEFAULT_VALUE);
		mca_vote(info->input_limit_voter, "subpmic_hw",
				true, MCA_WIRE_CHARGE_DEFAULT_IBUS_CURRENT);
		mca_vote(info->charge_limit_voter, "subpmic_hw",
				true, MCA_WIRE_CHARGE_DEFAULT_IBUS_CURRENT);
	}
}

static void strategy_buckchg_stop_charging(struct strategy_buckchg_dev *info)
{
#line 823
	mca_log_info("stop charging\n");
	cancel_delayed_work_sync(&info->monitor_work);
	cancel_delayed_work_sync(&info->wls_revchg_monitor_work);
	cancel_delayed_work_sync(&info->check_pd_secret_work);
	strategy_buckchg_sw_cv_stop(info);
	strategy_buckchg_reset_charge_para(info);
	strategy_class_buckchg_ops_set_input_volt(info, STATEGY_CHARGE_INPUT_VOLT_DEFAULT);
	strategy_class_buckchg_ops_adc_enable(info, false);
	strategy_class_buckchg_ops_set_opt_fws(info, STATEGY_CHARGE_FWS_DEFAULT);
	strategy_class_buckchg_ops_set_usb_aicl_cont_thd(info, STATEGY_CHARGE_AICL_TH_4P4V);

	info->aicl_thd = 0;
	info->pdo_nums = 0;
}

#define PDO_9V_VOLATGE 9000
static void strategy_buckchg_process_type_change(int value, struct strategy_buckchg_dev *info)
{
	struct timespec64 ts;

	if (value == info->proc_data.real_type) {
		if (value == XM_CHARGER_TYPE_UNKNOW)
			strategy_buckchg_check_charger_change(info);
		return;
	}

	info->proc_data.real_type = value;
	strategy_buckchg_check_charger_change(info);

	if (value == XM_CHARGER_TYPE_UNKNOW)
		return;

	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD ||
		info->proc_data.real_type == XM_CHARGER_TYPE_PPS ||
		info->proc_data.real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD, &info->pwr_cap);
		protocol_class_pd_get_suspend_support_status(TYPEC_PORT_0, &info->proc_data.pdsuspendsupported);
	}

	if (info->is_eu_model && info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
			info->proc_data.eu_start_time = ktime_get_boottime();
#line 936
			mca_log_info("is_eu_model for PPS eu_start_time = %lld\n", info->proc_data.eu_start_time);
	}
	stategy_buckchg_is_pdo_9v(info);
	strategy_buck_update_req_volt(info);
	mod_delayed_work(system_wq, &info->monitor_work, 0);

	ktime_get_boottime_ts64(&ts);
#line 943
	mca_log_info("verify_process_end: %d\n", info->verify_process_end);
	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD) {
		if (!info->verify_process_end && (u64)ts.tv_sec > 60) {
			mca_vote(info->input_limit_voter, "icl_limit", true, STATEGY_INPUT_DEFAULT_VALUE);
			schedule_delayed_work(&info->check_pd_secret_work, msecs_to_jiffies(10000));
		}
	} else {
		mca_vote(info->input_limit_voter, "icl_limit", false, STATEGY_INPUT_DEFAULT_VALUE);
	}
}

static void strategy_buckchg_process_soc_limit_change(int value, struct strategy_buckchg_dev *info)
{
	if (value) {
		// use vote to control charge enable/disable
		mca_vote(info->chg_enable_voter, "soc_limit", true, STATEGY_CHARGE_DISENABLE);
		mca_vote(info->input_voltage_voter, "soc_limit", true, 0);
#line 1040
		mca_log_info("SOC limit triggered, stopping charge\n");
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
	} else {
		mca_vote(info->chg_enable_voter, "soc_limit", false, STATEGY_CHARGE_ENABLE);
		mca_vote(info->input_voltage_voter, "soc_limit", false, 0);
#line 1045
		mca_log_info("SOC limit released, starting charge\n");
		if (info->proc_data.online)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
	}
}

static void strategy_buckchg_cp_revert_handler(int auth_pos, struct strategy_buckchg_dev *info)
{
	static int last_otg_present = 0;
	static int last_pos = 1;
	bool otg_present;
	int otg_enable;
	int pos;

	info->reverse_auth_sts = (auth_pos & 0xffff0000) >> 16;
	pos = auth_pos & 0xffff;

	protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &otg_present);
	mca_log_err("otg_present: %d, last_otg_present: %d\n", otg_present, last_otg_present);
	mca_log_err("reverse_auth_sts: %d, pos: %d, last_pos: %d\n", info->reverse_auth_sts, pos, last_pos);

	if (otg_present && last_pos == pos) {
#line 1165
		mca_log_err("same handler, ignore...");
		platform_class_cp_dump_register(CP_ROLE_MASTER);
		return;
	}

	if ((last_otg_present != otg_present) && otg_present)
		schedule_delayed_work(&info->source_status_monitor_work,
			msecs_to_jiffies(SOURCE_STATUS_MONITOR_INTERVAL));
	else if ((last_otg_present != otg_present) && !otg_present)
		cancel_delayed_work_sync(&info->source_status_monitor_work);

	last_otg_present = otg_present;
	last_pos = pos;

	if (otg_present) {
		if (pos == 1) {
#line 1181
			mca_log_err("start external boost");

			// enable external boost
			otg_enable = 1;
			otg_enable = (0 << 16) | (2 << 8) | otg_enable;
			platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, otg_enable);

			// close revert cp boost
			msleep(300);
			platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
			platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, false);
			platform_class_cp_enable_wpcgate(CP_ROLE_MASTER, false);
			platform_class_cp_set_qb(CP_ROLE_MASTER, false);
			platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);
		} else if (pos ==2) {
#line 1197
			mca_log_err("start revert 1_2 cp");

			// enable revert cp boost
			platform_class_cp_enable_acdrv_manual(CP_ROLE_MASTER, true);
			platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_REVERSE_1_2);
			platform_class_cp_enable_wpcgate(CP_ROLE_MASTER, false);
			platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, true);
			platform_class_cp_set_qb(CP_ROLE_MASTER, true);
			platform_class_cp_set_charging_enable(CP_ROLE_MASTER, true);

			// close external boost
			msleep(400);
			otg_enable = 0;
			otg_enable = (0 << 16) | (2 << 8) | otg_enable;
			platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, otg_enable);
		}
	} else {
#line 1227
		mca_log_err("end revert 1_2 cp");

		platform_class_cp_set_charging_enable(CP_ROLE_MASTER, false);
		platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, true);
		platform_class_cp_enable_wpcgate(CP_ROLE_MASTER, false);
		platform_class_cp_set_qb(CP_ROLE_MASTER, false);
		platform_class_cp_set_mode(CP_ROLE_MASTER, CP_MODE_FORWARD_2_1);

		// close otg
		msleep(400);
		otg_enable = 0;
		otg_enable = (0 << 16) | (2 << 8) | otg_enable;
		platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, otg_enable);

		last_otg_present = false;
		last_pos = 1;
		info->source_boost_status = 0;
	}
	platform_class_cp_dump_register(CP_ROLE_MASTER);
}

static int strategy_buckchg_get_status(int status, void *value, void *data)
{
	struct strategy_buckchg_dev *info = (struct strategy_buckchg_dev *)data;
	int *cur_val = (int *)value;

	if (!info || !value)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_ONLINE:
		platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, cur_val);
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*cur_val = info->proc_data.qc_type;
		break;
	case STRATEGY_STATUS_TYPE_ENABLE:
		*cur_val = mca_get_effective_result(info->chg_enable_voter);
		break;
	case STRATEGY_STATUS_TYPE_VBUS:
		*cur_val = info->proc_data.vbus;
		break;
	case STRATEGY_STATUS_TYPE_IBUS:
		*cur_val = info->proc_data.ibus;
		break;
	default:
		return -1;
	}

	return 0;
}

#define USB_ICL_UNENUMERATED 100
static int strategy_buckchg_check_charger_change(struct strategy_buckchg_dev *info)
{
#ifdef CONFIG_FACTORY_BUILD
	return 0;
#endif
	static bool sdp_vote_completed = false;
	int charge_boot_mode = mca_log_get_charge_boot_mode();

	/* Release USB related vote if USB detach has been detected */
	if (info->proc_data.real_type == XM_CHARGER_TYPE_UNKNOW) {
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		mca_vote(info->input_limit_voter, "sdpicl", false, 0);
		sdp_vote_completed = false;
#line 1633
		mca_log_info("charger have been removed reset vote");
		return 0;
	}

	if (charge_boot_mode || !info->is_eu_model) {
#line 1638
		mca_log_info(" charge_boot_mode[%d] cancel sdp enumerated process", charge_boot_mode);
		return 0;
	}

	if (info->proc_data.real_type == XM_CHARGER_TYPE_SDP && !sdp_vote_completed) {
		mca_vote(info->input_limit_voter, "sdpicl", true, USB_ICL_UNENUMERATED);
		sdp_vote_completed = true;
#line 1645
		mca_log_info("sdp charger unenumerated");
	}

	if (info->proc_data.real_type != XM_CHARGER_TYPE_SDP && sdp_vote_completed) {
		mca_vote(info->input_limit_voter, "sdpicl", false, 0);
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		sdp_vote_completed = false;
#line 1652
		mca_log_info("sdp charger error detected reset vote%d\n", info->proc_data.real_type);
	}


	return 0;
}

#define USB_SUSPEND_ICL 0
#define USB_UNSUSPEND_ICL -1
#define USB_SUSPEND_ICL_DEFAULT 2
static int stategy_buckchg_set_usb_icl(int value, struct strategy_buckchg_dev *info)
{
#ifdef CONFIG_FACTORY_BUILD
	return 0;
#endif
	int icl_ma = value / 1000;//ua switch ma;
	static bool usb_suspend = false;
	int charge_boot_mode = mca_log_get_charge_boot_mode();

	if (charge_boot_mode || !info->is_eu_model) {
#line 1672
		mca_log_info("charge_boot_mode[%d] is_eu_model[%d] exit usb_icl",charge_boot_mode, info->is_eu_model);
		return 0;
	}
#line 1675
	mca_log_info("usb type[%d]icl_ma[%d]", info->proc_data.real_type, icl_ma);
	if (info->proc_data.real_type != XM_CHARGER_TYPE_SDP) {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_PD ||
			info->proc_data.real_type == XM_CHARGER_TYPE_PPS) {
			if (!info->proc_data.pdsuspendsupported) {
#line 1680
				mca_log_info("USB suspend is not supported");
				return 0;
			} else {
				if (icl_ma == USB_SUSPEND_ICL_DEFAULT) {
					icl_ma = 0;
					mca_vote(info->input_limit_voter, "usbicl", true, icl_ma);
#line 1686
					mca_log_info("USB suspend for pd");
				} else if (icl_ma == 900 || icl_ma == 500 || icl_ma == 100) {
					mca_vote(info->input_limit_voter, "usbicl", false, 0);
#line 1689
					mca_log_info("USB Unsuspend for pd");
				} else
#line 1691
					mca_log_info("invalid ICL value for pd %d", icl_ma);
				return 0;
			}
		} else {
#line 1695
			mca_log_info("ICL setting is not allowed for usb type[%d]", info->proc_data.real_type);
			return 0;
		}
	}

	if (icl_ma == USB_SUSPEND_ICL_DEFAULT) {
#line 1701
		mca_log_info("USB Input Suspended");
		mca_vote(info->input_limit_voter, "usbicl", true, 0);
		usb_suspend = true;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_USB_SUSPEND, &usb_suspend);
	} else if (icl_ma == USB_UNSUSPEND_ICL) {
#line 1706
		mca_log_info("USB Input UnSuspended");
		mca_vote(info->input_limit_voter, "usbicl", false, 0);
		if (usb_suspend) {
			usb_suspend = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_USB_SUSPEND, &usb_suspend);
		}
	} else {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_SDP &&
			(icl_ma == 900 || icl_ma == 500) ) {
			mca_vote(info->input_limit_voter, "sdpicl", true, icl_ma);
			mca_vote(info->input_limit_voter, "usbicl", true, icl_ma);
#line 1717
			mca_log_info("ICL for SDP set by HLOS is %d mA", icl_ma);
		} else {
#line 1719
			mca_log_info("Invalid ICL for SDP set by HLOS is %d mA", icl_ma);
		}

		if (usb_suspend) {
			usb_suspend = false;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_USB_SUSPEND, &usb_suspend);
		}
	}

	return 0;
}
static int strategy_buckchg_set_config(int config, int value, void *data)
{
	struct strategy_buckchg_dev *info = (struct strategy_buckchg_dev *)data;

	if (!info)
		return -1;

	switch (config) {
	case STRATEGY_CONFIG_INPUT_CURRENT_LIMIT:
		stategy_buckchg_set_usb_icl(value, info);
		break;
	default:
		break;
	}
	return 0;
}

static void strategy_buckchg_update_aicl_cfg(struct strategy_buckchg_dev *info)
{
	int aicl_thd = STATEGY_CHARGE_AICL_TH_4P4V;
	int reg_aicl_thd = 0;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING
		|| info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) {
		aicl_thd = STATEGY_CHARGE_AICL_TH_4P1V;
#line 1756
		mca_log_info("discharging or done, should set aicl vth to 4.1V\n");
		goto update_aicl_cfg;
	}

	if (info->proc_data.real_type >= XM_CHARGER_TYPE_HVDCP3 &&
		info->proc_data.real_type <= XM_CHARGER_TYPE_HVDCP3P5)
		return;

	if (info->proc_data.vbat >= STATEGY_CHARGE_AICL_VBAT_TH)
		aicl_thd = STATEGY_CHARGE_AICL_TH_4P5V;

#ifdef CONFIG_FACTORY_BUILD
	aicl_thd = STATEGY_CHARGE_AICL_TH_4P1V;
#endif

update_aicl_cfg:
	platform_class_buckchg_ops_get_usb_aicl_cont_thd(MAIN_BUCK_CHARGER, &reg_aicl_thd);
	if (aicl_thd != info->aicl_thd || aicl_thd != reg_aicl_thd) {
#line 1782
		mca_log_info("vbat: %d, aicl_thd: %d, reg_aicl_thd: %d\n", info->proc_data.vbat, aicl_thd, reg_aicl_thd);
		info->aicl_thd = aicl_thd;
		strategy_class_buckchg_ops_set_usb_aicl_cont_thd(info, aicl_thd);
	}
}

static void strategy_buckchg_select_charg_para(struct strategy_buckchg_dev *info)
{
	bool dpdm_detect_done = true;
	int ibus_limit = CHARGE_SDP_INPUT_DEFAULT;
	int ibat_limit = CHARGE_SDP_CHARGE_DEFAULT;
	int real_type = info->proc_data.real_type;
	int count = 0;
	int pd_active;
#ifdef CONFIG_FACTORY_BUILD
	bool cc_attach = false;
#endif

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V &&
		real_type >= XM_CHARGER_TYPE_HVDCP2 && real_type <= XM_CHARGER_TYPE_HVDCP3P5)
		real_type = XM_CHARGER_TYPE_DCP;

#ifdef CONFIG_FACTORY_BUILD
	protocol_class_pd_get_cc_status(TYPEC_PORT_0, &cc_attach);
	if (!cc_attach && (real_type == XM_CHARGER_TYPE_SDP || real_type == XM_CHARGER_TYPE_FLOAT))
		real_type = XM_CHARGER_TYPE_DCP;
	mca_log_err("cc_attach %d real_type %d\n", cc_attach, real_type);
#endif

	switch (real_type) {
	case XM_CHARGER_TYPE_UNKNOW:
		ibus_limit = 100;
		ibat_limit = 100;
		break;
	case XM_CHARGER_TYPE_SDP:
#ifdef CONFIG_FACTORY_BUILD
		ibus_limit = 600;
		ibat_limit = 600;
#else
		if (info->is_eu_model) {
				ibus_limit = info->in_sdp - 50;
		} else {
				ibus_limit = info->in_sdp;
		}
		ibat_limit = info->chg_sdp;
#endif
		break;
	case XM_CHARGER_TYPE_CDP:
		ibus_limit = info->in_cdp;
		ibat_limit = info->chg_cdp;
		break;
	case XM_CHARGER_TYPE_FLOAT:
	case XM_CHARGER_TYPE_OCP:
		ibus_limit = info->in_float;
		ibat_limit = info->chg_float;
		break;
	case XM_CHARGER_TYPE_DCP:
		if (info->smartchg_data.pwr_boost_state) {
			ibus_limit = CHARGE_DCP_INPUT_BOOST;
			ibat_limit = CHARGE_DCP_CHARGE_BOOST;
		} else {
			ibus_limit = info->in_dcp;
			ibat_limit = info->chg_dcp;
		}
		break;
	case XM_CHARGER_TYPE_HVDCP2:
		ibus_limit = info->in_hvdcp;
		ibat_limit = info->chg_hvdcp;
		break;
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
		ibus_limit = info->in_hvdcp3;
		ibat_limit = info->chg_hvdcp3;
		break;
	case XM_CHARGER_TYPE_HVDCP3P5:
		ibus_limit = info->in_hvdcp3p5;
		ibat_limit = info->chg_hvdcp3p5;
		break;
	case XM_CHARGER_TYPE_TYPEC:
		ibus_limit = info->in_dcp;
		ibat_limit = info->chg_dcp;
		break;
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (info->pps_ptf == USBPD_BUCK_DPM_PORT_PPS_PTF_OVERTEMP) {
			ibus_limit = CHARGE_PPS_PTF_INPUT_DEFAULT;
			ibat_limit = CHARGE_PPS_PTF_CHARGE_DEFAULT;
		} else {
			ibus_limit = min(info->in_pd, (unsigned int)info->pwr_cap.cap[info->proc_data.curr_pd_pos].max_current);
			ibat_limit = info->chg_pd;
			if (info->pwr_cap.cap[info->proc_data.curr_pd_pos].max_current == 0) {
				ibus_limit = min(info->in_pd, DEFAULT_PD_CURRENT_MA);
#line 1940
				mca_log_info("pdo broadcast abnormal %d \n", info->proc_data.curr_pd_pos);
			}
		}
		break;
	default:
		ibus_limit = info->in_sdp;
		ibat_limit = info->chg_sdp;
		break;
	};

	protocol_class_pd_get_pd_active(TYPEC_PORT_0, &pd_active);
	if (0 /* G13b: hw_bc12 dt flag not in OS4 stock */ && !pd_active) {
		do {
			protocol_class_get_bc12_adapter_detect_done(ADAPTER_PROTOCOL_BC12, &dpdm_detect_done);
			msleep(20);
			count++;
			mca_log_info("get dpdm_detect_done = %d, count  = %d\n", dpdm_detect_done, count);
		} while ((!dpdm_detect_done) && (count < 50));//wait 1s until bc1.2 det done
	} else
		dpdm_detect_done = true;

	if ((ibus_limit != info->proc_data.ibus_limit) && dpdm_detect_done){
		mca_vote(info->buck_5v_in_voter, "wire_chg_type", true, ibus_limit);
		mca_vote(info->buck_9v_in_voter, "wire_chg_type", true, ibus_limit);
		mca_vote(info->input_limit_voter, "subpmic_hw", false, 0); //avoid vote default vaule
		info->proc_data.ibus_limit = ibus_limit;
		mca_log_info("set ibus_limit = %d\n", ibus_limit);
	}

	if ((ibat_limit != info->proc_data.ibat_limit) && dpdm_detect_done) {
		mca_vote(info->buck_5v_ich_voter, "wire_chg_type", true, ibat_limit);
		mca_vote(info->buck_9v_ich_voter, "wire_chg_type", true, ibat_limit);
		mca_vote(info->charge_limit_voter, "subpmic_hw", false, 0); //avoid vote default vaule
		info->proc_data.ibat_limit = ibat_limit;
		mca_log_info("set ibat_limit = %d\n", ibat_limit);
	}

	mca_log_info("ibus_limit = %d, ibat_limit = %d, bc1.2 det process = %d, pd_active = %d\n",
				ibus_limit, ibat_limit, dpdm_detect_done, pd_active);
}

/*static int strategy_buckchg_check_online_msleep(int ms, struct strategy_buckchg_dev *info)
{
	int i, count;
	count = ms / 10;
	for (i = 0; i < count; i++) {
		if (!info->proc_data.online)
			return -1;
		usleep_range(9900, 11000);
	}
	return 0;
}*/

#define VBUS_9V_OVP_VOLTAGE 10000
#define VBUS_5V_OVP_VOLTAGE 7500
#define PMIC_INDUCTOR_SECURE_ICL 1000
static void strategy_buckchg_check_charge_volt(struct strategy_buckchg_dev *info)
{
	int target_volt = info->proc_data.voltage;
	int vbus = 0;

	if (info->wls_revchg_en) {
#line 2122
		mca_log_info("wireless reverse is charging, do not request volt\n");
		return;
	}

	if ((info->proc_data.real_type >= XM_CHARGER_TYPE_PD &&
		info->proc_data.real_type <= XM_CHARGER_TYPE_PD_VERIFY) && !info->pwr_cap.nums) {
#line 2128
		mca_log_info("pd pwr_cap nums is null\n");
		return;
	}

	platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	vbus /= 1000;
	if ((target_volt == STATEGY_CHARGE_VBUS_9V && vbus < CHECK_VBUS_9V_HIGH_TH) ||
		(target_volt == STATEGY_CHARGE_VBUS_5V && vbus > CHECK_VBUS_5V_LOW_TH)) {
#line 2136
		mca_log_info("target_volt: %d, vbus: %d\n", target_volt, vbus);
		strategy_buckchg_set_charge_volt(info, target_volt);
	}

	platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	vbus /= 1000;
	if (target_volt == STATEGY_CHARGE_VBUS_9V && vbus >= VBUS_9V_OVP_VOLTAGE) {
		mca_vote(info->buck_9v_in_voter, "pmic_inductor", true, PMIC_INDUCTOR_SECURE_ICL);
#line 2144
		mca_log_err("hvdcp_9v vbus_ovp decrease input current limit\n");
	} else {
		mca_vote(info->buck_9v_in_voter, "pmic_inductor", false, 0);
	}

	if (target_volt == STATEGY_CHARGE_VBUS_5V && vbus > VBUS_5V_OVP_VOLTAGE) {
		mca_vote(info->buck_5v_in_voter, "pmic_inductor", true, PMIC_INDUCTOR_SECURE_ICL);
#line 2151
		mca_log_err("lvdcp_5v vbus_ovp decrease input current limit\n");
	} else {
		mca_vote(info->buck_5v_in_voter, "pmic_inductor", false, 0);
	}

	if (info->is_non_compliant_qc && !info->non_compliant_run_once) {
#line 2157
		mca_log_err("non_compliant_qc: target_volt: %d\n", target_volt);
		strategy_buckchg_set_charge_volt(info, target_volt);
		mca_vote(info->input_limit_voter, "non_compliant_qc", true, 1500);
		strategy_class_buckchg_ops_set_chg(info, false);
		strategy_class_buckchg_ops_set_chg(info, true);
#line 2162
		mca_log_err("restart aicl and recover buck charging\n");
		platform_class_buckchg_ops_set_rerun_aicl(MAIN_BUCK_CHARGER, true);
		info->non_compliant_run_once = true;
	}
}

#define VBUS_QC_VOL_THRESHOLD_LOW 7200
#define NON_COMPLIANT_QC_IBUS_THR_LOW 200
static void strategy_buckchg_check_non_compliant_qc_charger(struct strategy_buckchg_dev *info)
{
	int target_volt = info->proc_data.voltage;
	int vbus = 0, ibus = 0;
	static int count;

	if (target_volt == STATEGY_CHARGE_VBUS_5V || info->is_non_compliant_qc)
		return;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) {
#line 2180
		mca_log_info("charge done, no need check non_compliant_qc_charger\n");
		return;
	}

	if (info->proc_data.real_type != XM_CHARGER_TYPE_HVDCP2 &&
		info->proc_data.real_type != XM_CHARGER_TYPE_HVDCP3) {
#line 2186
		mca_log_info("only qc2/qc3 need check\n");
		return;
	}

	platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
	vbus /= 1000;
	(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &info->proc_data.ibus);
	ibus = info->proc_data.ibus / 1000;
	if (target_volt == STATEGY_CHARGE_VBUS_9V && vbus < VBUS_QC_VOL_THRESHOLD_LOW
		&& ibus < NON_COMPLIANT_QC_IBUS_THR_LOW) {
#line 2196
		mca_log_info("target_volt: %d, vbus: %d\n", target_volt, vbus);
		count++;
	}
	if (count >= 3 && !info->is_non_compliant_qc) {
		info->is_non_compliant_qc = true;
		count = 0;
#line 2202
		mca_log_err("qc is non-compliant, set request vol to 5V\n");
		mca_vote(info->input_voltage_voter, "non_compliant_qc", true, 0);
	}
}

static int strategy_buckchg_update_charge_status(struct strategy_buckchg_dev *info)
{
	int main_chg_status = 0;
	int aux_chg_status = 0;
	int icl = 0;
	int ichg = 0;
	int suspend = 0;
	int charging_done = 0;
	int adsp_icl = 0;

	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &main_chg_status);
	(void)platform_class_buckchg_ops_get_chg_status(AUX_BUCK_CHARGER, &aux_chg_status);
	(void)platform_class_buckchg_ops_get_bus_curr(MAIN_BUCK_CHARGER, &info->proc_data.ibus);
	(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &info->proc_data.vbus);
	(void)strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	(void)platform_class_buckchg_ops_get_input_curr_lmt(MAIN_BUCK_CHARGER, &adsp_icl);
	icl = mca_get_effective_result(info->input_limit_voter);
	ichg = mca_get_effective_result(info->charge_limit_voter);
	suspend = mca_get_effective_result(info->input_suppend_voter);

	switch (main_chg_status) {
	case MCA_BATT_CHGR_STATUS_INHIBIT:
	case MCA_BATT_CHGR_STATUS_TRICKLE:
	case MCA_BATT_CHGR_STATUS_PRECHARGE:
	case MCA_BATT_CHGR_STATUS_FULLON:
	case MCA_BATT_CHGR_STATUS_TAPER:
	case MCA_BATT_CHGR_STATUS_FAST_LINEAR:
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_CHARGING;
		break;
	case MCA_BATT_CHGR_STATUS_TERMINATION:
		info->proc_data.chg_status =  MCA_BUCK_CHG_STS_CHARGE_DONE;
		break;
	case MCA_BATT_CHGR_STATUS_PAUSE:
	case MCA_BATT_CHGR_STATUS_CHARGING_DISABLED:
		info->proc_data.chg_status = MCA_BUCK_CHG_NO_CHARGING;
		break;
	default:
		info->proc_data.chg_status = MCA_BUCK_CHG_STS_NA;
		break;
	}

	charging_done = strategy_class_fg_ops_get_charging_done();
	if (charging_done)
		info->proc_data.chg_status =  MCA_BUCK_CHG_STS_CHARGE_DONE;

	mca_log_err("pmic_chg_status: %d, chg_status: %d, chg_en: [%d][%s], chg_type: %d, vbat: %d, ibat: %d, vbus: %d, ibus: %d\n",
		main_chg_status, info->proc_data.chg_status,
		mca_get_effective_result(info->chg_enable_voter), mca_get_effective_client(info->chg_enable_voter),
		info->proc_data.real_type, info->proc_data.vbat, info->proc_data.ibat, info->proc_data.vbus, info->proc_data.ibus);
#line 2256
	mca_log_err("ap_icl:[%s][%d], adsp_icl[%d], ichg:[%s][%d], input_suspend:[%s][%d]\n",
		mca_get_effective_client(info->input_limit_voter), icl, adsp_icl,
		mca_get_effective_client(info->charge_limit_voter), ichg,
		mca_get_effective_client(info->input_suppend_voter), suspend);
	return info->proc_data.chg_status;
}

static void strategy_buckchg_enable_fast_charge_mode(struct strategy_buckchg_dev *info, int soc)
{
	int batt_temp, fastcharge_mode, buck_jeita_ffc_size;
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int quick_charge_status = MCA_QUICK_CHG_STS_CHARGE_FAILED;

	if (info->proc_data.real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
		(void)strategy_class_fg_ops_get_temperature(&batt_temp);
		batt_temp /= 10;

		mca_log_info("batt_temp = %d, soc =%d, fcc = %d, iterm = %d, quickchg_sts = %d\n",
			batt_temp, soc, fcc, iterm, quick_charge_status);

		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_JEITA,
			STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM /* G13b: template used a status id that does not exist in OS4; GUESS */, &buck_jeita_ffc_size);
		fastcharge_mode = strategy_class_fg_get_fastcharge();
		if (!fastcharge_mode && soc < ALLOW_START_FFC_BATT_SOC_THR
			&& batt_temp >= info->ffc_temp_low && batt_temp <= info->ffc_temp_high && info->batt_auth
			&& buck_jeita_ffc_size) {
			strategy_class_fg_set_fastcharge(true);
#line 2331
			mca_log_info("buck charger enable fast charge mode\n");
		} else if (batt_temp < info->ffc_temp_low
			|| batt_temp > info->ffc_temp_high) {
			strategy_class_fg_set_fastcharge(false);
#line 2335
			mca_log_info("buck charger disable fast charge mode\n");
		} else if (fastcharge_mode && soc >= ALLOW_START_FFC_BATT_SOC_THR
			&& fcc <= iterm && quick_charge_status != MCA_QUICK_CHG_STS_CHARGING) {
			strategy_class_fg_set_fastcharge(false);
			mca_log_info("buck charger disable fast charge mode by fcc too low\n");
		}
	}
}

static void strategy_buckchg_eoc_force_5v(struct strategy_buckchg_dev *info)
{
	mca_vote(info->input_voltage_voter, "eoc_5v", true, 0);
}

#define COLD_ZONE_LOW -100
#define COLD_ZONE_HIGH 50
#define HOT_ZONE_LOW 480
#define HOT_ZONE_HIGH 550
#define IS_BETWEEN(val, lval, rval)	((val >= lval) ? ((val <= rval) ? true :  false) : \
								((val >= rval) ? true : false))
static int strategy_buckchg_charge_abnormal_cold_or_hot_zone(struct strategy_buckchg_dev *info)
{
	int icl = 0;
	int chg_en = 0;
	int temp = 0;
	int vterm = 0;
	static int count = 0;

	strategy_class_fg_ops_get_temperature(&temp);
	if ( !IS_BETWEEN (temp, COLD_ZONE_LOW, COLD_ZONE_HIGH) &&
		!IS_BETWEEN (temp, HOT_ZONE_LOW, HOT_ZONE_HIGH)) {
#line 2381
		mca_log_info("temp: %d, not in cold or hot zone\n", temp);
		return 0;
	}

	icl = mca_get_effective_result(info->input_limit_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);
	vterm = mca_get_effective_result(info->vterm_voter);
	if (!icl || !chg_en || info->proc_data.vbat >= vterm) {
		mca_log_info("user can stop charging icl: %d, chg_en: %d, vterm: %d, vbat: %d, temp:%d\n",
			icl, chg_en, vterm, info->proc_data.vbat, temp);
		return 0;
	}

	if (info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING) {
		/* temp ranges aren't overlaping, so using the same count variable is safe */
		if (IS_BETWEEN (temp, COLD_ZONE_LOW, COLD_ZONE_HIGH)) {
			count++;
			if (count > 3) {
				count = 0;
				mca_charge_mievent_report(CHARGE_DFX_LOW_TEMP_DISCHARGING, &temp, 1);
			}
		} else if (IS_BETWEEN (temp, HOT_ZONE_LOW, HOT_ZONE_HIGH)) {
			count++;
			if (count > 3) {
				count = 0;
				mca_charge_mievent_report(CHARGE_DFX_HIGH_TEMP_DISCHARGING, &temp, 1);
			}
		} else {
			count = 0;
		}
		mca_log_info("count: %d\n", count);
	} else {
		count = 0;
	}

	return 0;
}

#define STRATEGY_BUCKCHG_ENTER_QUICKCHG_TIME_MS 7000
#define VBAT_DROP_COUNT_TH 3
static void strategy_buckchg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
				struct strategy_buckchg_dev, monitor_work.work);
	int interval = CHARGE_MONITOR_WORK_NORMAL_INTERVAL;
	int quick_charge_status = MCA_QUICK_CHG_STS_CHARGE_FAILED;
	int input_suspned = 0;
	int chg_en = 0, system_soc;
	int vterm = 0;
	int jeita_hot_result = 1;
	ktime_t time_now;
	int verifed = 0;
	bool vbat_drop_exit_flag = false;
	static int vbat_drop_cnt = 0;
	int active_port = protocol_class_pd_get_port_num();

	system_soc = strategy_class_fg_ops_get_soc();
	if (system_soc <= ALLOW_QUICK_CHG_SOC_THR) {
		jeita_hot_result = mca_get_client_vote(info->chg_enable_voter, "jeita-hot");
		mca_log_info("jeita_hot vote value: %d\n", jeita_hot_result);
		switch (info->proc_data.real_type) {
		case XM_CHARGER_TYPE_HVDCP3_B:
		case XM_CHARGER_TYPE_HVDCP3P5:
		case XM_CHARGER_TYPE_PD_VERIFY:
			if (jeita_hot_result)
				mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					MCA_EVENT_CHARGE_ACTION, 0);
			break;
		case XM_CHARGER_TYPE_PPS:
			time_now = ktime_get_boottime();
			protocol_class_pd_get_pd_verifed(active_port, &verifed);
			if (info->is_eu_model) {
#line 2448
				mca_log_info("eu_start_time %lld, time_now %lld  delta = %lld\n",info->proc_data.eu_start_time , time_now,
					ktime_ms_delta(time_now, info->proc_data.eu_start_time));
				if (ktime_ms_delta(time_now, info->proc_data.eu_start_time) > STRATEGY_BUCKCHG_ENTER_QUICKCHG_TIME_MS) {
					mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					MCA_EVENT_CHARGE_ACTION, 0);
					info->proc_data.eu_start_time = time_now;
#line 2454
					mca_log_info("trigger MCA_EVENT_CHARGE_ACTION\n");
				}
			} else {
				if (jeita_hot_result && info->verify_process_end && !verifed) {
					mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
						MCA_EVENT_CHARGE_ACTION, 0);
				}
				mca_log_info("verify_process_end: %d, verifed: %d\n", info->verify_process_end, verifed);
			}
			break;
		default:
			break;
		}

	} else {
		strategy_buckchg_enable_fast_charge_mode(info, system_soc);
	}

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING)
		goto out;

	strategy_buckchg_check_charge_volt(info);
	strategy_buckchg_select_charg_para(info);
	strategy_buckchg_update_charge_status(info);
	strategy_buckchg_check_non_compliant_qc_charger(info);
	strategy_buckchg_update_aicl_cfg(info);
	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGING) {
		if (info->proc_data.charge_done_force_5v) {
			info->proc_data.charge_done_force_5v = false;
			mca_vote(info->input_voltage_voter, "eoc_5v", false, 0);
		}
		// WA: rerun aicl if ibus too low
		if (info->proc_data.ibus < 150000 && mca_get_effective_result(info->input_limit_voter) >= 500) {
			mca_log_info("ibus abnormally low: %d, rerun aicl\n", info->proc_data.ibus);
			platform_class_buckchg_ops_set_rerun_aicl(MAIN_BUCK_CHARGER, true);
		}
	}
	if ((info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGE_DONE) &&
		!info->proc_data.charge_done_force_5v) {
		strategy_buckchg_sw_cv_stop(info);
		strategy_buckchg_eoc_force_5v(info);
		info->proc_data.charge_done_force_5v = true;
	}
	if (!info->proc_data.chg_en) {
		mca_vote(info->chg_enable_voter, "online", true, STATEGY_CHARGE_ENABLE);
		info->proc_data.chg_en = STATEGY_CHARGE_ENABLE;
	}

	// WA: solve pmic abnormal status
	vterm = mca_get_effective_result(info->vterm_voter);
	input_suspned = mca_get_effective_result(info->input_suppend_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);
	if (vterm >= STATEGY_CHARGE_VTERM_LOW_TH /* G13b: template dt flag, not in OS4 stock */ && !input_suspned) {
		if (chg_en && info->proc_data.chg_status == MCA_BUCK_CHG_NO_CHARGING) {
			strategy_class_buckchg_ops_set_chg(info, false);
			strategy_class_buckchg_ops_set_chg(info, true);
#line 2536
			mca_log_err("recover buck charging\n");
		}
	}
	strategy_buckchg_charge_abnormal_cold_or_hot_zone(info);

	if(info->proc_data.vbat <= vterm - (20 * 1 /* G13b: template dt flag, not in OS4 stock */)) {
		vbat_drop_cnt++;
	}
	vbat_drop_exit_flag = vbat_drop_cnt >= VBAT_DROP_COUNT_TH ? true : false;
	vbat_drop_cnt = vbat_drop_exit_flag ? 0 : vbat_drop_cnt;

	if (info->proc_data.chg_status == MCA_BUCK_CHG_STS_CHARGING &&
		!info->sw_cv_running && vterm >= STATEGY_CHARGE_VTERM_LOW_TH /* G13b: template dt flag, not in OS4 stock */ &&
		info->proc_data.vbat >= vterm - CHARGE_SW_CV_VBAT_ALARM_DELTA) {
#line 2545
		mca_log_err("vbat: %d, vterm: %d, start sw_cv_work\n", info->proc_data.vbat, vterm);
		strategy_buckchg_sw_cv_start(info);
	} else if (info->sw_cv_running && (!chg_en || vbat_drop_exit_flag)) {
#line 2548
		mca_log_err("vbat: %d, vterm: %d, chg_en: %d, stop sw_cv_work\n", info->proc_data.vbat, vterm, chg_en);
		strategy_buckchg_sw_cv_stop(info);
	}

	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGE_DONE)
		interval = CHARGE_MONITOR_WORK_FAST_INTERVAL;

out:
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(interval));
}

static void strategy_buckchg_sw_cv_start(struct strategy_buckchg_dev *info)
{
	info->sw_cv_running = true;
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	mca_queue_delayed_work(&info->sw_cv_work, msecs_to_jiffies(CHARGE_SW_CV_WORK_FAST_INTERVAL));
}

static void strategy_buckchg_sw_cv_stop(struct strategy_buckchg_dev *info)
{
#line 2571
	mca_log_err("chg_status: %d, stop sw_cv_work\n", info->proc_data.chg_status);
	cancel_delayed_work_sync(&info->sw_cv_work);
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	info->sw_cv_running = false;
	info->vbat_ov_count = 0;
}

/* G13d: rebuilt from the OS4 stock listing (the XRING template had FCC_STEP/FV_STEP constants, a {1000,5} delta map and no dynamic-adjust block) */
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
		struct strategy_buckchg_dev, sw_cv_work.work);
	/* stock rodata object strategy_buckchg_sw_cv_workfunc.sw_cv_volt_delta_map [16 bytes]: {1000, 4}, {0, 2} */
	static const int sw_cv_volt_delta_map[][2] = {
		{ 1000, 4 },
		{ 0, 2 }
	};
	int interval;
	int vbat, ibat, i;
	int vterm = mca_get_effective_result(info->vterm_voter);
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int pmic_comp = info->pmic_fv_compensation;
	int temp = 0;
	int volt_delta = sw_cv_volt_delta_map[0][1];

	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat);
	vbat = info->proc_data.vbat;
	ibat = -info->proc_data.ibat / 1000;
#line 2605
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, iterm: %d, fcc: %d\n", vbat, ibat, vterm, iterm, fcc);

	for (i = 0; i < ARRAY_SIZE(sw_cv_volt_delta_map); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (vbat >= vterm - volt_delta) {
		int fcc_step = info->sw_cv_fcc_step;

		if (ibat - fcc_step > iterm) {
			int v;

			if (fcc - ibat >= 2 * fcc_step)
				v = ibat / fcc_step * fcc_step;
			else
				v = fcc - fcc_step;
			if (v - iterm < info->sw_cv_fcc_limit_buffer)
				v = info->sw_cv_fcc_limit_buffer + iterm;
			mca_vote(info->charge_limit_voter, "sw_cv", true, v);
		}
		interval = 2000;
	} else {
		interval = 5000;
	}

	if (info->support_pmic_vterm_dynamics_adjust) {
		int fast = strategy_class_fg_get_fastcharge();
		int raw;

		strategy_class_fg_ops_get_temperature(&temp);
		raw = temp;
		temp = temp / 10;
		if (fast && raw >= 180 && raw <= 299)
			pmic_comp = info->pmic_fv_compensation;
		else if (fast && raw >= 300 && raw <= 399)
			pmic_comp = info->pmic_middle_fv_compensation;
		else {
			pmic_comp = 0;
			if (fast && raw >= 400 && raw <= 479)
				pmic_comp = info->pmic_high_fv_compensation;
		}
	}

	if (vbat >= vterm - 1) {
		int old;

#line 2644
		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n", info->vbat_ov_count);
		old = info->vbat_ov_count;
		info->vbat_ov_count = old + 1;
		if (old == 0)
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, pmic_comp + vterm - info->sw_cv_fv_step);
		else if (old + 1 >= 2)
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, pmic_comp + vterm - 2 * info->sw_cv_fv_step);
	}

	mca_queue_delayed_work(&info->sw_cv_work, msecs_to_jiffies(interval));
}

static int strategy_buckchg_wireless_revchg_msleep(int ms, struct strategy_buckchg_dev *info)
{
	int i, count;

	count = ms / 10;

	for (i = 0; i < count; i++) {
		if (!info->proc_data.online || !info->wls_revchg_en)
			return -1;
		usleep_range(9900, 11000);
	}

	return 0;
}

static int strategy_buckchg_process_wireless_revchg(struct strategy_buckchg_dev *info)
{
	int real_type = info->proc_data.real_type;
	int vbus = 0, cnt = 0;
	int rev_req_vadp = 0, req_volt_valid_h = 0, req_volt_valid_l = 0;
	int ret = 0;

	if (real_type < XM_CHARGER_TYPE_DCP || real_type > XM_CHARGER_TYPE_PD_VERIFY)
		return -1;

	if (real_type == XM_CHARGER_TYPE_PD) {
		if (strategy_buckchg_wireless_revchg_msleep(WLS_REVCHG_NORMAL_INTERVAL, info))
			return -1;
	} else if (real_type == XM_CHARGER_TYPE_DCP) {
		if (strategy_buckchg_wireless_revchg_msleep(WLS_REVCHG_SLOW_INTERVAL, info))
			return -1;
	} else if (real_type == XM_CHARGER_TYPE_HVDCP3) {
		if (strategy_buckchg_wireless_revchg_msleep(WLS_REVCHG_FAST_INTERVAL, info))
			return -1;
	}

	real_type = info->proc_data.real_type;
	mca_vote(info->input_limit_voter, "wireless_revchg", true, CHARGE_WLS_REVCHG_INPUT_DEFAULT);
	if (real_type == XM_CHARGER_TYPE_PPS || real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		rev_req_vadp = info->rev_req_vadp[0];
		req_volt_valid_h = info->rev_vadp_valid_h[0] * 1000;
		req_volt_valid_l = info->rev_vadp_valid_l[0] * 1000;
	} else {
		rev_req_vadp = info->rev_req_vadp[1];
		req_volt_valid_h = info->rev_vadp_valid_h[1] * 1000;
		req_volt_valid_l = info->rev_vadp_valid_l[1] * 1000;
	}
	strategy_buckchg_set_charge_volt(info, rev_req_vadp);

	if (strategy_buckchg_wireless_revchg_msleep(300, info))
		return -1;
	(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
#line 2804
	mca_log_info("chg_type: %d, req_vadp: %d, vbus: %d, rev_vadp_valid_h: %d, rev_vadp_valid_l: %d\n",
		real_type, rev_req_vadp, vbus, req_volt_valid_h, req_volt_valid_l);
	while (vbus < req_volt_valid_l || vbus > req_volt_valid_h) {
		if (cnt > 5)
			break;
		if (strategy_buckchg_wireless_revchg_msleep(300, info))
			return -1;
		(void)platform_class_buckchg_ops_get_bus_volt(MAIN_BUCK_CHARGER, &vbus);
		cnt++;
	}
	if (cnt > 5) {
#line 2815
		mca_log_err("request 9V vbus fail\n");
		return -1;
	}

	info->proc_data.wls_revchg_init_done = true;
	ret = mca_wireless_rev_set_wired_chg_ok(true);
	return ret;
}

static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
				struct strategy_buckchg_dev, wls_revchg_monitor_work.work);
	int ret = 0;
	int interval = CHARGE_MONITOR_WORK_NORMAL_INTERVAL;

	if (!info->proc_data.wls_revchg_init_done)
		ret = strategy_buckchg_process_wireless_revchg(info);
	if (ret || !info->wls_revchg_en)
		goto err_out;
	else {
		if (info->proc_data.real_type == XM_CHARGER_TYPE_HVDCP2 && !info->rev_icl_for_qc2) {
			mca_vote(info->input_limit_voter, "wireless_revchg", true, CHARGE_WLS_REVCHG_INPUT_QC2);
			info->rev_icl_for_qc2 = true;
		}
		schedule_delayed_work(&info->wls_revchg_monitor_work,
			msecs_to_jiffies(interval));
		return;
	}

err_out:
	strategy_buckchg_exit_wireless_revchg(info);
}

static void strategy_csd_pulse_process_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
				struct strategy_buckchg_dev, csd_pulse_process_work.work);

	if (info->csd_flag) {
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
		mca_vote(info->chg_enable_voter, "csd_pulse", true, STATEGY_CHARGE_DISENABLE);
		info->csd_flag = false;
		schedule_delayed_work(&info->csd_pulse_process_work, msecs_to_jiffies(75000));
#line 2859
		mca_log_info("vote fcc and discharge logic\n");
	} else {
		mca_vote(info->chg_enable_voter, "csd_pulse", false, STATEGY_CHARGE_DISENABLE);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			MCA_EVENT_CSD_SEND_PULSE, 0);
#line 2864
		mca_log_info("resume charging logic\n");
	}
}

static void strategy_source_status_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
				struct strategy_buckchg_dev, source_status_monitor_work.work);
	int batt_temp = 0;
	int system_soc = 0;
	int status = 0;

	(void)strategy_class_fg_ops_get_temperature(&batt_temp);
	system_soc = strategy_class_fg_ops_get_soc();
	batt_temp /= 10;
	mca_log_info("batt_temp = %d, thermal_board_temp =%d\n", batt_temp, info->thermal_board_temp);

	if (batt_temp < 0 || info->thermal_board_temp > 400 ||
		(info->reverse_auth_sts && system_soc < 30) ||
		(!info->reverse_auth_sts && system_soc < 70))
		status = 1;

	if (status != info->source_boost_status) {
		info->source_boost_status = status;
		protocol_class_pd_set_gear_shift(TYPEC_PORT_0, info->source_boost_status);
	}

	schedule_delayed_work(&info->source_status_monitor_work,
		msecs_to_jiffies(SOURCE_STATUS_MONITOR_INTERVAL));
}

#define NTC_SCALE_BOARDTEMP 100
static int strategy_buckchg_if_set_chg_cur(const char *user,
	char *value, void *data)
{
	int temp_value = 0;
	struct strategy_buckchg_dev *info = data;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

	if (temp_value)
		(void)mca_vote(info->charge_limit_voter, user, true, temp_value);
	else
		(void)mca_vote(info->charge_limit_voter, user, false, 0);

	return 0;
}

static int strategy_buckchg_if_get_chg_cur(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->charge_limit_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->charge_limit_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}


#define MTBF_CLIENT_SUBSTR_UPPER "MTBF\0"
#define MTBF_CLIENT_SUBSTR_LOW "mtbf\0"
#define MTBF_ACTIVE_TEST_CLIENT_SUBSTR "shell\0"
#define MTBF_TEST_MAX_CURRENT_MA 1500
static int strategy_buckchg_if_set_input_cur(const char *user,
	char *value, void *data)
{
	int temp_value = 0;
	struct strategy_buckchg_dev *info = data;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

	if (strstr(user, MTBF_ACTIVE_TEST_CLIENT_SUBSTR) != NULL) {
			if (temp_value > MTBF_TEST_MAX_CURRENT_MA)
				temp_value = MTBF_TEST_MAX_CURRENT_MA;
	}

	if (temp_value) {
		if (strstr(user, MTBF_CLIENT_SUBSTR_UPPER) != NULL ||
			strstr(user, MTBF_CLIENT_SUBSTR_LOW) != NULL ||
			strstr(user, MTBF_ACTIVE_TEST_CLIENT_SUBSTR) != NULL)
			mca_vote_override(info->input_limit_voter, user, true, temp_value);
		else
			(void)mca_vote(info->input_limit_voter, user, true, temp_value);
	} else {
		if (strstr(user, MTBF_CLIENT_SUBSTR_UPPER) != NULL || strstr(user, MTBF_CLIENT_SUBSTR_LOW) != NULL)
			mca_vote_override(info->input_limit_voter, user, false, 0);
		else
			(void)mca_vote(info->input_limit_voter, user, false, 0);
	}

	return 0;
}

static int strategy_buckchg_if_get_input_cur(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->input_limit_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->input_limit_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_buckchg_if_set_chg_en(const char *user,
	unsigned int value, void *data)
{
	struct strategy_buckchg_dev *info = data;

	if (!user || !data)
		return -1;

	if (value)
		(void)mca_vote(info->chg_enable_voter, user,
			true, STATEGY_CHARGE_ENABLE);
	else
		(void)mca_vote(info->chg_enable_voter, user,
			true, STATEGY_CHARGE_DISENABLE);

	return 0;
}

static int strategy_buckchg_if_get_chg_en(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->chg_enable_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->chg_enable_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_buckchg_if_get_input_suspend(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_buckchg_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->input_suppend_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->input_suppend_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_buckchg_if_set_input_suspend(const char *user,
	char *value, void *data)
{
	int temp_value = 0;
	struct strategy_buckchg_dev *info = data;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

#line 3267
	mca_log_err("set_input_suspend: %d\n", temp_value);
	if (temp_value)
		(void)mca_vote(info->input_suppend_voter, user, true, 1);
	else
		(void)mca_vote(info->input_suppend_voter, user, true, 0);

	return 0;
}

static int strategy_buckchg_if_set_ship_mode(const char *user,
	unsigned int val, void *data)
{
	int rc;

	rc = platform_class_buckchg_ops_set_ship_mode(MAIN_BUCK_CHARGER, !!val);
	if (rc < 0)
		return rc;

	return 0;
}

static int strategy_buckchg_if_get_ship_mode(bool *val, void *data)
{
	int rc;

	rc = platform_class_buckchg_ops_get_ship_mode(MAIN_BUCK_CHARGER, val);
	if (rc < 0)
		return rc;

	return 0;
}

static struct mca_charge_if_ops g_strategy_buckchg_if_ops = {
	.type_name = "buck",
	.set_input_suspend = strategy_buckchg_if_set_input_suspend,
	.get_input_suspend = strategy_buckchg_if_get_input_suspend,
	.set_charge_enable = strategy_buckchg_if_set_chg_en,
	.get_charge_enable = strategy_buckchg_if_get_chg_en,
	.set_input_current_limit = strategy_buckchg_if_set_input_cur,
	.get_input_current_limit = strategy_buckchg_if_get_input_cur,
	.set_charge_current_limit = strategy_buckchg_if_set_chg_cur,
	.get_charge_current_limit = strategy_buckchg_if_get_chg_cur,
	.set_ship_mode_en = strategy_buckchg_if_set_ship_mode,
	.get_ship_mode_status = strategy_buckchg_if_get_ship_mode,
};

static int strategy_buckchg_soc_limit_sts_callback(void *data, int effective_result)
{
	struct strategy_buckchg_dev *info = (struct strategy_buckchg_dev *)data;

	if (!data)
		return -1;

	info->soc_limit = effective_result;
#line 3340
	mca_log_info("effective_result: %d\n", effective_result);

	if (info->support_charge_more) {
		if (effective_result && info->proc_data.online) {
			strategy_buckchg_process_soc_limit_change_more(effective_result, info);
		} else {
			cancel_delayed_work_sync(&info->soc_limit_stepper_work);
			strategy_buckchg_process_soc_limit_change_more(0, info);
		}
		return 0;
	}

	if (info->proc_data.online)
		strategy_buckchg_process_soc_limit_change(effective_result, info);
	else
		strategy_buckchg_process_soc_limit_change(0, info);

	return 0;
}

static int mca_charger_buckchg_pwr_boost_sts_callback(void *data, int enable)
{
	struct strategy_buckchg_dev *info = (struct strategy_buckchg_dev *)data;

	if (!data)
		return -1;

	info->smartchg_data.pwr_boost_state = enable;

	return 0;
}

static struct mca_smartchg_if_ops g_buck_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_BUCK,
	.data = NULL,
	.set_fcc = strategy_buckchg_smartchg_set_fcc_callback,
	.set_soc_limit_sts = strategy_buckchg_soc_limit_sts_callback,
	.set_pwr_boost_sts = mca_charger_buckchg_pwr_boost_sts_callback,
};


/*
 * G13d: process_soc_limit_change_more, base_flip_sw_cv_workfunc, otg_monitor_workfunc and check_reverse_quick_charge are rebuilt from the OS4 stock
 * listing (constants, tables and call arguments are the stock ones; see STATUS.md for what is still a guess).
 */
/* stock .rodata object soc_limit_stepper_table [88 bytes]: {charge_limit, input_limit} per step 0..10 (read from the stock .ko) */
struct soc_limit_step {
	int charge_limit;
	int input_limit;
};

static const struct soc_limit_step soc_limit_stepper_table[] = {
	{ 0, 0 },
	{ 1990, 850 },
	{ 1400, 750 },
	{ 1100, 650 },
	{ 900, 550 },
	{ 700, 450 },
	{ 500, 350 },
	{ 300, 350 },
	{ 200, 350 },
	{ 100, 250 },
	{ 0, 250 },
};

static noinline void strategy_buckchg_process_soc_limit_change_more(int value, struct strategy_buckchg_dev *info)
{
	static u8 cur_step;

	if (value) {
#line 1007
		mca_log_info("SOC limit triggered\n");
		if (cur_step < 10)
			cur_step++;
	} else {
		cur_step = 0;
#line 1010
		mca_log_info("SOC limit released\n");
	}

	if (cur_step == 0) {
		mca_vote(info->charge_limit_voter, "soc_limit", false, 0);
		mca_vote(info->input_limit_voter, "soc_limit", false, 0);
		mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
	} else {
		mca_vote(info->charge_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].charge_limit);
		mca_vote(info->input_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].input_limit);
		if (cur_step == 10) {
			mca_vote(info->chg_enable_voter, "soc_limit", true, 0);
		} else {
			mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
			queue_delayed_work(system_wq, &info->soc_limit_stepper_work, 1000);
		}
	}
#line 1031
	mca_log_info("cur_step = %d\n", cur_step);
}

/* stock rodata object strategy_buckchg_base_flip_sw_cv_workfunc.sw_cv_volt_delta_map [16 bytes]: {1000, 7}, {0, 2} */
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, base_flip_sw_cv_work.work);
	static const int sw_cv_volt_delta_map[][2] = {
		{ 1000, 7 },
		{ 0, 2 }
	};
	int interval = 5000;
	int vterm_jeita = mca_get_client_vote(info->vterm_voter, "jeita");
	int iterm = info->parallel_iterm;	/* stock reads dev+2040 here; the name is the BTF member name guess of that slot */
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int first_termination = 0;
	int vbat, ibat, fastcharge, vterm, actual_vterm, volt_delta, vadj, fv_step, i;
	bool same_flag, fast_flag;

	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat);
	fastcharge = strategy_class_fg_get_fastcharge();
	vbat = info->proc_data.vbat;
	ibat = -info->proc_data.ibat / 1000;
	strategy_class_fg_get_first_termination(&first_termination);
#line 2683
	mca_log_info("first_termination_flag:%d", first_termination);
	vterm = vterm_jeita;
	if (first_termination)
		vterm = mca_get_effective_result(info->vterm_voter);
	same_flag = info->base_flip_same == 0;
	fast_flag = fastcharge != 0;
	actual_vterm = info->pmic_fv_compensation + vterm;
	vadj = (fast_flag & same_flag) ? -5 : -3;
	fv_step = (fast_flag & same_flag) ? 10 : 5;
#line 2703
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, actual_vterm: %d, iterm: %d, fcc: %d, fast_charge: %d, fv_step: %d\n",
		vbat, ibat, vterm_jeita, actual_vterm, iterm, fcc, fast_flag, fv_step);

	volt_delta = sw_cv_volt_delta_map[0][1];
	for (i = 0; i < ARRAY_SIZE(sw_cv_volt_delta_map); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (fastcharge && vbat >= vterm_jeita - volt_delta) {
		interval = 2000;
		if (ibat - 50 > iterm) {
			if (fcc - ibat >= 100)
				mca_vote(info->charge_limit_voter, "sw_cv", true, ibat / 50 * 50);
			else if (info->base_flip_same && !first_termination && ibat - iterm >= 300)
				mca_vote(info->charge_limit_voter, "sw_cv", true, fcc - 50);
		}
	}

	if (vbat >= vterm_jeita + vadj) {
#line 2728
		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n", info->vbat_ov_count);
		info->vbat_ov_count++;
		platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, actual_vterm - min(info->vbat_ov_count, 5) * fv_step);
	}

	mca_queue_delayed_work(&info->base_flip_sw_cv_work, msecs_to_jiffies(interval));
}

static void strategy_buckchg_otg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, otg_monitor_work.work);
	int temp = 0, soc, i;
	bool otg_present = false;
	char uevent[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data n_data = { 0 };
	bool need_disable = false;
	bool was_disabled;

	strategy_class_fg_ops_get_temperature(&temp);
	soc = strategy_class_fg_ops_get_soc();
	temp /= 10;
	protocol_class_pd_get_otg_plugin_status(0, &otg_present);
#line 3007
	mca_log_err("otg_present = %d, batt_temp = %d, system_soc = %d\n", otg_present, temp, soc);

	for (i = 0; i < info->otg_threshold_size; i++) {
		if (temp < info->otg_temp_threshold[i] && soc <= info->otg_soc_threshold[i]) {
			need_disable = true;
#line 3013
			mca_log_info("OTG disable condition met: temp<%d && soc<=%d\n",
				info->otg_temp_threshold[i], info->otg_soc_threshold[i]);
			break;
		}
	}

	mutex_lock(&info->otg_incold_mutex);
	was_disabled = info->disable_otg_incold;
#line 3023
	mca_log_err("bat temp = %d, soc = %d, disable_otg_incold = %d, need_disable = %d\n",
		temp, soc, was_disabled, need_disable);
	if (was_disabled) {
		if (info->support_disable_otg_incold && !(otg_present && need_disable)) {
			info->disable_otg_incold = false;
#line 3046
			mca_log_err("temp is above threshold or OTG removed, report now\n");
			goto report;
		}
	} else if (info->support_disable_otg_incold && otg_present && need_disable) {
#line 3029
		mca_log_err("temp is cold, disable otg now\n");
		info->disable_otg_incold = true;
		platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, (0 << 16) | (info->otg_boost_src << 8) | 0);
report:
		n_data.event_len = snprintf(uevent, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_DISABLE_OTG_INCOLD=%d", info->disable_otg_incold);
		n_data.event = uevent;
		mca_event_report_uevent(&n_data);
	}
	mutex_unlock(&info->otg_incold_mutex);

	if (otg_present && info->support_disable_otg_incold)
		mca_queue_delayed_work(&info->otg_monitor_work, 1250);
}

/*
 * stock: standalone (not referenced from .text/.data in the OS4 module) function with the reverse-quick-charge eligibility rule:
 * result = otg_present && !wls_online && !wls_revchg_online && batt_temp >= 0 && thermal_board_temp <= 400 && soc > (reverse_auth_sts ? 29 : 69).
 * The stock stores the result through the 2nd argument as a 32-bit value; the prototype (info, int *result) is inferred from the listing.
 */
static void __used strategy_buckchg_check_reverse_quick_charge(struct strategy_buckchg_dev *info, int *result)
{
	int temp = 0, soc;
	bool otg_present = false;
	int ok = 0;

	strategy_class_fg_ops_get_temperature(&temp);
	soc = strategy_class_fg_ops_get_soc();
	temp /= 10;
	protocol_class_pd_get_otg_plugin_status(0, &otg_present);
#line 1112
	mca_log_info("otg_present = %d, batt_temp = %d, thermal_board_temp =%d\n", otg_present, temp, info->thermal_board_temp);
	if (otg_present && !info->wls_online && !info->wls_revchg_online && temp >= 0 && info->thermal_board_temp <= 400)
		ok = soc > (info->reverse_auth_sts ? 29 : 69);
	*result = ok;
}

/* stock: GLOBAL function with kCFI hash 0x36b1c5a6 (_ZTSFivE = int (void)); the slot in the OS4 .data ops is NULL; G13e: int, not bool; non-static */
int strategy_buckchg_is_support_pps_100w(void)
{
	struct strategy_buckchg_dev *info = g_buckchg_info;
	const struct mca_hwid *hwid;

	if (!info)
		return false;
	if (info->is_eu_model)
		return true;
	if (info->support_pps_100w == MCA_STRATEGY_PPS_100W_ALL)
		return true;
	hwid = mca_get_hwid_info();
	if (!hwid)
		return false;
	if (info->support_pps_100w == MCA_STRATEGY_PPS_100W_NON_CN)
		return hwid->country_version != 0;
	if (info->support_pps_100w == MCA_STRATEGY_PPS_100W_CN)
		return hwid->country_version == 0;
	return false;
}

static int strategy_buckchg_class_probe(struct platform_device *pdev)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct strategy_buckchg_dev *info;
	int online = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	strategy_buckchg_parse_dt(info);
	strategy_buckchg_parse_dt_override(info, hwid);

	/* init voters (inlined in the stock probe, same order and defaults) */
	info->chg_enable_voter = mca_create_votable("chg_enable", MCA_VOTE_AND,
		strategy_buckchg_enable, STATEGY_CHARGE_DISENABLE, info);
	if (IS_ERR(info->chg_enable_voter))
		goto voter_err;
	info->input_suppend_voter = mca_create_votable("input_suspend", MCA_VOTE_OR,
		strategy_buckchg_input_suspend, STATEGY_CHARGE_INPUT_SUSPEND, info);
	if (IS_ERR(info->input_suppend_voter))
		goto voter_err;
	info->input_limit_voter = mca_create_votable("buck_input", MCA_VOTE_MIN,
		strategy_buckchg_input_limit, STATEGY_INPUT_DEFAULT_VALUE, info);
	if (IS_ERR(info->input_limit_voter))
		goto voter_err;
	info->charge_limit_voter = mca_create_votable("buck_charge_curr", MCA_VOTE_MIN,
		strategy_buckchg_charge_limit, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->charge_limit_voter))
		goto voter_err;
	hwid = mca_get_hwid_info();
	info->iterm_voter = mca_create_votable("term_curr",
		(hwid && (hwid->platform_version & ~1U) == 4) ? MCA_VOTE_MIN : MCA_VOTE_MAX,
		strategy_buckchg_set_iterm, STATEGY_ITERM_DEFAULT_VALUE, info);
	if (IS_ERR(info->iterm_voter))
		goto voter_err;
	info->vterm_voter = mca_create_votable("term_volt", MCA_VOTE_MIN,
		strategy_buckchg_set_vterm, STATEGY_VTERM_DEFAULT_VALUE, info);
	if (IS_ERR(info->vterm_voter))
		goto voter_err;
	info->input_voltage_voter = mca_create_votable("input_voltage", MCA_VOTE_AND,
		strategy_buckchg_input_voltage, 1, info);
	if (IS_ERR(info->input_voltage_voter))
		goto voter_err;
	info->buck_5v_in_voter = mca_create_votable("buck_5v_in", MCA_VOTE_MIN,
		strategy_buckchg_set_5v_input, STATEGY_INPUT_5V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_in_voter))
		goto voter_err;
	info->buck_5v_ich_voter = mca_create_votable("buck_5v_ich", MCA_VOTE_MIN,
		strategy_buckchg_set_5v_ichg, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_ich_voter))
		goto voter_err;
	info->buck_9v_in_voter = mca_create_votable("buck_9v_in", MCA_VOTE_MIN,
		strategy_buckchg_set_9v_input, STATEGY_INPUT_9V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_in_voter))
		goto voter_err;
	info->buck_9v_ich_voter = mca_create_votable("buck_9v_ich", MCA_VOTE_MIN,
		strategy_buckchg_set_9v_ichg, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_ich_voter))
		goto voter_err;

	mca_vote(info->input_voltage_voter, "batt_auth", true, 0);
	mca_vote(info->charge_limit_voter, "batt_auth", true, info->chg_batt_auth_failed);
	mca_vote(info->vterm_voter, "batt_miss", true, STAEGY_BATT_MISS_FV);
	info->proc_data.voltage = STATEGY_CHARGE_VBUS_5V;
	info->proc_data.pre_volt = STATEGY_CHARGE_VBUS_5V;
	info->batt_missing = 1;
	info->hvdcp_allow_flag = 0;
	info->vbat_ov_count = 0;
	info->buck_abnormal_cnt = 0;
	info->cp_vendor = -1;
	info->otg_boost_src = -1;
	info->disable_otg_incold = false;
	mutex_init(&info->otg_incold_mutex);

	INIT_DELAYED_WORK(&info->monitor_work, strategy_buckchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->sw_cv_work, strategy_buckchg_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->base_flip_sw_cv_work, strategy_buckchg_base_flip_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->wls_revchg_monitor_work, strategy_wls_revchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->csd_pulse_process_work, strategy_csd_pulse_process_workfunc);
	INIT_DELAYED_WORK(&info->source_status_monitor_work, strategy_source_status_monitor_workfunc);
	INIT_DELAYED_WORK(&info->check_pd_secret_work, strategy_buckchg_check_pdsecret_workfunc);
	INIT_DELAYED_WORK(&info->rerun_handle_pd_auth_work, strategy_rerun_handle_pd_auth_workfunc);
	INIT_DELAYED_WORK(&info->soc_limit_stepper_work, strategy_buckchg_soc_limit_stepper_workfunc);
	INIT_DELAYED_WORK(&info->otg_monitor_work, strategy_buckchg_otg_monitor_workfunc);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
		strategy_buckchg_process_event, strategy_buckchg_get_status, strategy_buckchg_set_config, info);
	g_strategy_buckchg_if_ops.data = info;
	(void)mca_charge_if_ops_register(&g_strategy_buckchg_if_ops);
	g_buck_smartchg_if_ops.data = info;
	(void)mca_smartchg_if_ops_register(&g_buck_smartchg_if_ops);
	info->thermal_board_nb.notifier_call = strategy_buckchg_thermal_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP, &info->thermal_board_nb);
	info->panel_nb.notifier_call = strategy_buckchg_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &info->panel_nb);

	platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (online) {
#line 3446
		mca_log_info("avoid missing first usb connect event\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_USB_CONNECT, NULL);
	}

	g_buckchg_info = info;
#line 3451
	mca_log_err("androidboot.mode=%d\n", mca_log_get_charge_boot_mode());
	info->init_ok = 1;
#line 3454
	mca_log_err("probe success\n");
	return 0;

voter_err:
#line 3398
	mca_log_err("init voter err\n");
	return -1;
}

static int strategy_buckchg_class_remove(struct platform_device *pdev)
{
	return 0;
}

static void strategy_buckchg_class_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,strategy_buckchg"},
	{},
};

static struct platform_driver strategy_buckchg_class_driver = {
	.driver	= {
		.name = "strategy_buckchg_class",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = strategy_buckchg_class_probe,
	.remove = strategy_buckchg_class_remove,
	.shutdown = strategy_buckchg_class_shutdown,
};

static int __init strategy_buckchg_class_init(void)
{
	return platform_driver_register(&strategy_buckchg_class_driver);
}
module_init(strategy_buckchg_class_init);

static void __exit strategy_buckchg_class_exit(void)
{
	platform_driver_unregister(&strategy_buckchg_class_driver);
}
module_exit(strategy_buckchg_class_exit);


MODULE_DESCRIPTION("strategy buckchg class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
