// SPDX-License-Identifier: GPL-2.0
/*
 * mca_battery_safe.c - battery safety strategies of the Xiaomi MCA fuel gauge strategy (ISC detection, lithium plating,
 * over-voltage dfx).  Reconstructed from the OS4 stock mca_strategy_fg_comp.ko (log tag [mca_battery_safe]).
 */
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/power_supply.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/smart_charge/mca_smart_charge.h>
#include "strategy_fg.h"

#define MCA_LOG_TAG "mca_battery_safe"

void stop_isc_detect_work(struct isc_detect_info *info)
{
	if (!info->isDetecting)
		return;

	info->count = 0;
	info->state = ISC_STATE_WAITING;
	info->isDetecting = false;
	info->need_stop = false;
	info->full_count = 0;
	info->sample_index = 0;
#line 102
	mca_log_info("stop isc detect work");
}

void stop_isc_detect_2_work(struct isc_detect_info *info)
{
	if (!info->isDetecting)
		return;

	info->state = ISC_STATE_WAITING;
	info->isDetecting = false;
	info->need_stop = false;
	info->full_count = 0;
	info->sample_index = 0;
	info->average_point_done_cnt = 0;
	info->count = 0;
#line 189
	mca_log_info("stop isc_detect_2 work");
}

void stop_li_plating_detect_work(struct li_plating_info *info)
{
	if (!info->isDetecting)
		return;

	info->state = LI_PLATING_BEFORE_FULL;
	info->sample_index = 0;
	info->before_full_index = 0;
	info->isDetecting = false;
	memset(info->li_data_1, 0, sizeof(info->li_data_1));
	memset(info->li_data_2, 0, sizeof(info->li_data_2));
#line 402
	mca_log_info("stop li_plating_detect work");
}

void mca_strategy_start_isc_detect_2_work(struct strategy_fg *fg)
{
	if (fg->screen_status) {
#line 169
		mca_log_info("screen_on stop isc_detect_2 work");
		return;
	}

	fg->isc_det_2_info.isDetecting = true;
	fg->isc_det_2_info.count++;
#line 174
	mca_log_info("start isc_detect_2 work");
	schedule_delayed_work(&fg->isc_detect_2_work, msecs_to_jiffies(1800000));
}
