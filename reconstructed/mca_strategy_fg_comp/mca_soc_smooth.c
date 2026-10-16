// SPDX-License-Identifier: GPL-2.0
/*
 * mca_soc_smooth.c - UI soc smoothing of the Xiaomi MCA fuel gauge strategy (Xiaomi 15 "dada").
 * Reconstructed from the stock mca_strategy_fg_comp.ko, XRING mca_soc_smooth.c as template.
 */
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/protocol/protocol_class.h>
#include <mca/smart_charge/mca_smart_charge.h>
#include "strategy_fg.h"

#define MCA_LOG_TAG "mca_soc_smooth"

#ifndef abs
#define abs(x) ((x)  > 0 ? (x) : -(x))
#endif

#define EU_SOC_PROPORTION_C 100
#define EU_SOC_PROPORTION 101

#define FG_HOLD_UISOC_100_GAP		120

#line 41
int strategy_soc_smooth_parse_dt(struct strategy_fg *fg){
	struct device_node *node = fg->dev->of_node;
	int ret, len;

	ret = mca_parse_dts_u32(node, "suppot_RSOC_0_smooth",
		&(fg->smooth.suppot_RSOC_0_smooth), 0);
	if (fg->smooth.suppot_RSOC_0_smooth == 0)
		return 0;

	ret = mca_parse_dts_u32_array(node, "smooth_curr_unit", fg->smooth.smooth_curr_unit, CURR_SMOOTH_LEN);
	if (ret < 0)
		return -1;

	ret = mca_parse_dts_u32_array(node, "smooth_vol_offset", fg->smooth.smooth_vol_offset, VOL_SMOOTH_LEN);
	if (ret < 0)
		return -1;

	ret = mca_parse_dts_u32_array(node, "smooth_temp_unit", fg->smooth.smooth_temp_unit, TEMP_SMOOTH_LEN);
	if (ret < 0)
		return -1;

	len = mca_parse_dts_u32_count(node, "dischg_smooth_low_temp_unit", VOL_SMOOTH_LEN, CURR_SMOOTH_LEN);
	if (len < 0) {
#line 64
		mca_log_err("parse dischg_smooth_low_temp_unit failed\n");
		return -1;
	}
	ret = mca_parse_dts_u32_array(node, "dischg_smooth_low_temp_unit", &fg->smooth.dischg_smooth_low_temp_unit[0][0], len);
	if (ret < 0)
		return -1;

	len = mca_parse_dts_u32_count(node, "dischg_smooth_high_temp_unit", VOL_SMOOTH_LEN, CURR_SMOOTH_LEN);
	if (len < 0) {
#line 73
		mca_log_err("parse dischg_smooth_high_temp_unit failed\n");
		return -1;
	}
	ret = mca_parse_dts_u32_array(node, "dischg_smooth_high_temp_unit", &fg->smooth.dischg_smooth_high_temp_unit[0][0], len);
	if (ret < 0)
		return -1;
	return 0;
}

#line 83
static void get_unit_time_low_soc(struct strategy_fg *fg, int *soc_keep_time)
{
	int (*dischg_smooth_temp_unit)[CURR_SMOOTH_LEN] = NULL;
	int unit_time = 0;

	if (fg->batt_temperature < fg->smooth.smooth_temp_unit[0]) //0
		dischg_smooth_temp_unit = fg->smooth.dischg_smooth_low_temp_unit;
	else if (fg->batt_temperature < fg->smooth.smooth_temp_unit[1]) //15
		dischg_smooth_temp_unit = fg->smooth.dischg_smooth_low_temp_unit;
	else
		dischg_smooth_temp_unit = fg->smooth.dischg_smooth_high_temp_unit;

	if (dischg_smooth_temp_unit == NULL)
		goto out;

	for (int i = VOL_SMOOTH_LEN - 1; i >= 0; i--) {
		if (fg->batt_voltage_mean > fg->vcutoff_shutdown_delay + fg->smooth.smooth_vol_offset[i]) {
			for (int j = CURR_SMOOTH_LEN - 1; j >= 0; j--) {
				if (fg->batt_current_mean > fg->smooth.smooth_curr_unit[j]) {
					unit_time = dischg_smooth_temp_unit[i][j];
					break;
				}
			}
		}
		if (unit_time)
			break;
	}
out:
	if (unit_time >= 10 && unit_time <= 500)
		*soc_keep_time = unit_time;
	else
#line 113
		mca_log_info("data error unit_time:%d\n", unit_time);
}

#line 117
static int fg_update_mapped_soc(struct strategy_fg *fg)
{
	int raw_soc = fg->batt_raw_soc;
	int mapped_soc = 0;
	int mapped_soc_decimal = 0;

	if (fg->is_eu_model) {
		if (fg->charging_done && fg->batt_rsoc == 100) {
			mapped_soc = 100;
			mapped_soc_decimal = 0;
		} else if (fg->charging_done && fg->en_smooth_full) {
			mapped_soc = 100;
			mapped_soc_decimal = 0;
			if (fg->batt_ui_soc == 100)
				fg->en_smooth_full = false;
		} else if (fg->lossless_recharge && fg->batt_rsoc == 100) {
			mapped_soc = 100;
			mapped_soc_decimal = 0;
		} else if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
			mapped_soc = (raw_soc + EU_SOC_PROPORTION_C) / EU_SOC_PROPORTION;
			mapped_soc_decimal = (raw_soc + EU_SOC_PROPORTION_C) % EU_SOC_PROPORTION;
			if (mapped_soc > 99)
				mapped_soc = 99;
		} else {
			mapped_soc = fg->batt_rsoc;
			mapped_soc_decimal = (raw_soc + EU_SOC_PROPORTION_C) % EU_SOC_PROPORTION;
		}
	} else {
		if (raw_soc >= fg->cfg.report_full_raw_soc) {
			mapped_soc = 100;
			mapped_soc_decimal = 0;
		} else {
			mapped_soc = (raw_soc + fg->cfg.soc_proportion_c) / fg->cfg.soc_proportion;
			mapped_soc_decimal = (raw_soc + fg->cfg.soc_proportion_c) % fg->cfg.soc_proportion;

			if (mapped_soc > 99)
				mapped_soc = 99;
		}
	}

	fg->batt_mapped_soc = mapped_soc;
	fg->batt_mapped_soc_decimal = mapped_soc_decimal;

#line 160
	mca_log_info("is_eu_model[%d], en_smooth_full[%d], mapped_soc[%d]\n", fg->is_eu_model, fg->en_smooth_full, mapped_soc);
	return 0;
}

#line 164
static int fg_ui_soc_process_special_conditions(struct strategy_fg *fg, int last_ui_soc, int ui_soc)
{
	time64_t time_now = ktime_get_boottime_seconds();
	int down_update_duration = 30;
	int soc_limit = 0;

	soc_limit = mca_smartchg_get_limit_soc();
	if (ui_soc > last_ui_soc &&
		(!fg->power_present ||
		fg->chg_status != POWER_SUPPLY_STATUS_CHARGING ||
		(soc_limit > 0 && last_ui_soc >= soc_limit))) {
#line 176
		mca_log_info("WARNING: keep ui_soc constant by: power_present=%d, chg_status=%d, soc_limit=%d\n",
			fg->power_present, fg->chg_status, soc_limit);
		ui_soc = last_ui_soc;
	}

	if (ui_soc < last_ui_soc && fg->batt_current_mean < 0) {
#line 182
		mca_log_info("WARNING: keep ui_soc constant by: batt_current_mean=%d\n", fg->batt_current_mean);
		ui_soc = last_ui_soc;
	}

	if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (fg->real_type == XM_CHARGER_TYPE_SDP)
			down_update_duration = 70;
		else
			down_update_duration = 30;
		if ((ui_soc > last_ui_soc && time_now - fg->plugin_time < 15) ||
			(ui_soc < last_ui_soc && time_now - fg->plugin_time < down_update_duration)) {
#line 193
			mca_log_info("WARNING: keep ui_soc constant by: plugin_time [down_update_duration][%d]\n", down_update_duration);
			ui_soc = last_ui_soc;
		}
	}

	if (fg->keep_full_flag && last_ui_soc == 100 &&
		fg->batt_raw_soc > fg->cfg.report_full_raw_soc - FG_HOLD_UISOC_100_GAP) {
#line 200
		mca_log_info("WARNING: set ui_soc to 100 by: keep_full_flag\n");
		ui_soc = 100;
	}

	if (fg->is_eu_model && last_ui_soc == 100 && fg->batt_raw_soc >= 9881 &&
		fg->chg_status == POWER_SUPPLY_STATUS_FULL) {
#line 206
		mca_log_info("WARNING: set ui_soc to 100 by: POWER_SUPPLY_STATUS_FULL\n");
		ui_soc = 100;
	}

	if (fg->vbatt_empty) {
#line 212
		mca_log_info("WARNING: set ui_soc to 0 by: vbatt_empty\n");
		ui_soc = 0;
	} else {
		mca_battery_shutdown_check_shutdown_delay(fg, &ui_soc);
		if (ui_soc > 100)
			ui_soc = 100;
		else if (ui_soc < 0)
			ui_soc = 0;
	}

	return ui_soc;
}

static void fg_ui_soc_update_decimal(struct strategy_fg *fg, int last_ui_soc, int ui_soc)
{
	if (ui_soc < fg->batt_mapped_soc && ui_soc == last_ui_soc)
		fg->batt_ui_soc_decimal = 99;
	else if (ui_soc > fg->batt_mapped_soc && ui_soc == last_ui_soc)
		fg->batt_ui_soc_decimal = 0;
	else
		fg->batt_ui_soc_decimal = fg->batt_mapped_soc_decimal;
}

#line 222
int fg_ui_soc_smooth(struct strategy_fg *fg, bool prohibit_jump)
{
	int ui_soc = 0;
	static int last_ui_soc = -1;
	int soc_delta = 0, delta_time = 0;
	int soc_keep_time = 10;
	int soc_changed = 0;
	time64_t time_now = ktime_get_boottime_seconds();
	static time64_t time_last;

	fg_update_mapped_soc(fg);
	ui_soc = fg->batt_mapped_soc;

	// Get the initial value for the first time
	if (last_ui_soc == -1) {
		time_last = time_now;
		if (ui_soc > 0)
			last_ui_soc = ui_soc;
		else
			last_ui_soc = fg->batt_rsoc;
		return last_ui_soc;
	}

	// If the soc jump, will smooth one cap every 10S
	if (fg->en_smooth_full) {
		soc_keep_time = 30;
#line 262
		mca_log_info("start en_smooth_full soc increase per 30s \n");
	}
	soc_delta = abs(ui_soc - last_ui_soc);
	if (soc_delta >= 1 && prohibit_jump) {
		if (fg->smooth.suppot_RSOC_0_smooth && fg->batt_raw_soc == 0 && fg->batt_current > 0)
			get_unit_time_low_soc(fg, &soc_keep_time);
		delta_time = (time_now - time_last) / soc_keep_time;
		if (delta_time < 0)
			delta_time = 0;

		soc_changed = min(1, delta_time);
		if (ui_soc > last_ui_soc)
			ui_soc = last_ui_soc + soc_changed;
		else if (ui_soc < last_ui_soc)
			ui_soc = last_ui_soc - soc_changed;
		else
			ui_soc = last_ui_soc;

#line 280
		mca_log_info("ui_soc: %d, last_ui_soc: %d, soc_delta: %d, soc_changed: %d, batt_mapped_soc:%d, is_eu_model=%d, soc_keep_time:%d\n",
			ui_soc, last_ui_soc, soc_delta, soc_changed, fg->batt_mapped_soc, fg->is_eu_model, soc_keep_time);
	}

	if (prohibit_jump) {
		ui_soc = fg_ui_soc_process_special_conditions(fg, last_ui_soc, ui_soc);
		fg_ui_soc_update_decimal(fg, last_ui_soc, ui_soc);
	}

#line 289
	mca_log_info("ui_soc: %d => %d, time_now: %lld prohibit_jump:%d\n", last_ui_soc, ui_soc, time_now, prohibit_jump);
	if (ui_soc != last_ui_soc) {
		if (last_ui_soc == 100)
			fg->keep_full_flag = false;
		time_last = time_now;
		last_ui_soc = ui_soc;
	}

	return ui_soc;
}

MODULE_DESCRIPTION("Strategy Fuel Gauge Smooth");
MODULE_AUTHOR("liweiwei9@xiaomi.com");
MODULE_LICENSE("GPL v2");
