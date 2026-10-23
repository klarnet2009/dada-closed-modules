// SPDX-License-Identifier: GPL-2.0
/*
 * mca_battery_safe.c - battery safety strategies of the Xiaomi MCA fuel gauge strategy (ISC detection, lithium plating,
 * over-voltage dfx).  Reconstructed from the OS4 stock mca_strategy_fg_comp.ko (log tag [mca_battery_safe]).
 */
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/strategy/strategy_class.h>
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

void mca_strategy_start_isc_detect_2_work(void *data)
{
	struct strategy_fg *fg = data;

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

#define BUF_SZ 500

static void report_isc_data(struct isc_detect_info *info)
{
	int i, j;

	for (i = 0; i < 2; i++) {
		struct mca_event_notify_data event_data = { 0 };
		char buf[BUF_SZ];
		unsigned int len;

		memset(buf, 0, sizeof(buf));
		len = snprintf(buf, BUF_SZ, "%s:%d-%d:", "BATTERY_SAFE_REPORT=ISC_det", i, 1);
		for (j = 0; j < 5; j++) {
			struct batterydata *d = &info->isc_data[i][j];

			len += snprintf(buf + len, BUF_SZ - len, "p=%d,t=%d,v=%d,i=%d,", j, d->temp, d->volt, d->curr);
		}
		buf[len - 1] = '\0';
		event_data.event = buf;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
#line 89
		mca_log_info("%s", buf);
	}
}

static void report_isc_data_2(struct isc_detect_info *info)
{
	struct mca_event_notify_data event_data = { 0 };
	char buf[BUF_SZ];
	int qmax = 0;
	unsigned int len;
	int i, j, soc0, soc1;
	time64_t t0, t1;

	memset(buf, 0, sizeof(buf));
	platform_fg_ops_get_batt_qmax(0, &qmax);
	t1 = info->isc_data[1][0].time; t0 = info->isc_data[0][0].time; soc0 = info->isc_data[0][4].soc; soc1 = info->isc_data[1][4].soc;	/* G52a: stock loads the four fields into registers before the first snprintf */
	len = snprintf(buf, BUF_SZ, "%s:%d-%d:", "BATTERY_SAFE_REPORT=ISC_det_2", 0, 2);
	len += snprintf(buf + len, BUF_SZ - len, "qmax=%d,d_t=%d,", qmax, (int)(t1 - t0));
	len += snprintf(buf + len, BUF_SZ - len, "p=%d,cc=%d,", 0, soc0);
	len += snprintf(buf + len, BUF_SZ - len, "p=%d,cc=%d,", 1, soc1);
	event_data.event = buf;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
#line 280
	mca_log_info("%s", buf);

	for (i = 0; i < 2; i++) {
		struct mca_event_notify_data ev = { 0 };

		memset(buf, 0, sizeof(buf));
		len = snprintf(buf, BUF_SZ, "%s:%d-%d:", "BATTERY_SAFE_REPORT=ISC_det_2", i + 1, 2);
		for (j = 0; j < 5; j++) {
			struct batterydata *d = &info->isc_data[i][j];

			len += snprintf(buf + len, BUF_SZ - len, "p=%d,t=%d,v=%d,i=%d,", j, d->temp, d->volt, d->curr);
		}
		buf[len - 1] = '\0';
		ev.event = buf;
		ev.event_len = len;
		mca_event_report_uevent(&ev);
#line 294
		mca_log_info("%s", buf);
	}
}

static void isc_do_point_sample_2(struct strategy_fg *fg)
{
	int curr = 0, volt = 0, temp = 0;
	int ret, idx, group, pos, i, j;
	int avg_curr[2], avg_temp[2], delta_i, delta_t;
	time64_t now;

	ret = platform_fg_ops_get_curr(0, &curr);
	ret |= platform_fg_ops_get_volt(0, &volt);
	ret |= platform_fg_ops_get_temp(0, &temp);
	if (ret) {
#line 205
		mca_log_err("get curr/volt/temp failed\n");
		curr = 0;
		temp = 0;
		volt = 0;
	}

	idx = fg->isc_det_2_info.sample_index;
	group = idx / 5;
	pos = idx - group * 5;
	now = ktime_get_coarse_boottime() / NSEC_PER_SEC;
	/* G25c: direct member accesses: array-bounds trap `pos < 5` (stock `cmp #5`), not `pos <= 5` as for `d = &isc_data[group][pos]` */
	fg->isc_det_2_info.isc_data[group][pos].time = now;
	fg->isc_det_2_info.isc_data[group][pos].temp = temp / 10;
	fg->isc_det_2_info.isc_data[group][pos].volt = volt;
	fg->isc_det_2_info.isc_data[group][pos].curr = curr / 1000;
	fg->isc_det_2_info.sample_index++;

	if (pos == 4) {
		platform_fg_ops_get_cc_coulomb_integral(0, &fg->isc_det_2_info.isc_data[group][pos].soc);
		if (idx >= 5) {
			for (i = 0; i < 2; i++) {
				int sum_i = 0, sum_t = 0;

				for (j = 0; j < 5; j++) {
					sum_t += fg->isc_det_2_info.isc_data[i][j].temp;	/* G25c: stock loads temp first (CODEGEN) */
					sum_i += fg->isc_det_2_info.isc_data[i][j].curr;
				}
				avg_curr[i] = sum_i / 5;
				avg_temp[i] = sum_t / 5;
#line 230
				mca_log_info("avg_curr[%d]=%d, avg_temp[%d]=%d", i, avg_curr[i], i, avg_temp[i]);
			}
			delta_i = abs(avg_curr[1] - avg_curr[0]);
			delta_t = abs(avg_temp[1] - avg_temp[0]);
			if (delta_i > 29 || delta_t >= 10) {
#line 236
				mca_log_info("average_point_done_cnt %d", fg->isc_det_2_info.average_point_done_cnt);
				if (fg->isc_det_2_info.average_point_done_cnt >= 2) {
					fg->isc_det_2_info.need_stop = true;
#line 239
					mca_log_info("sample not satisfy condition, exit");
				}
				fg->isc_det_2_info.average_point_done_cnt++;
#line 242
				mca_log_info("ave point 2 need resample, average_point_done_cnt:%d, delta_i:%d delta_t:%d",
					     fg->isc_det_2_info.average_point_done_cnt, delta_i, delta_t);
				fg->isc_det_2_info.sample_index = 5;
				memcpy(fg->isc_det_2_info.isc_data[0], fg->isc_det_2_info.isc_data[1],
				       sizeof(fg->isc_det_2_info.isc_data[0]));
				memset(fg->isc_det_2_info.isc_data[1], 0, sizeof(fg->isc_det_2_info.isc_data[1]));
				return;
			}
		}
	}

	if (fg->isc_det_2_info.sample_index % 5 == 0)
		fg->isc_det_2_info.average_point_done_cnt++;
#line 252
	mca_log_info("average_point_done_cnt %d, sample_index %d", fg->isc_det_2_info.average_point_done_cnt,
		     fg->isc_det_2_info.sample_index);
#line 256
	mca_log_info("sample [%d][%d] collected: time=%d, temp=%d, volt=%d, curr=%d", group, pos, (int)fg->isc_det_2_info.isc_data[group][pos].time, fg->isc_det_2_info.isc_data[group][pos].temp, fg->isc_det_2_info.isc_data[group][pos].volt, fg->isc_det_2_info.isc_data[group][pos].curr);
}

void strategy_isc_detect_2_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, isc_detect_2_work.work);
	struct isc_detect_info *info = &fg->isc_det_2_info;
	static bool interval_ms;

	if (fg->usb_online || fg->wls_online) {
		info->need_stop = true;
#line 306
		mca_log_info("power present, stop work");
	} else if (info->need_stop) {
#line 310
		mca_log_info("need_stop set, stop work");
	} else {
#line 313
		mca_log_info("count:%d, state:%d", info->count, info->state);
		switch (info->state) {
		case ISC_STATE_WAITING:
			if (info->count >= (info->average_point_done_cnt ? 4 : 1)) {
				info->count = 0;
				info->state = ISC_STATE_SAMPLING;
				interval_ms = true;
#line 324
				mca_log_info("start to sample");
			} else {
				info->count++;
			}
			break;
		case ISC_STATE_SAMPLING:
			if (info->count > 4 || info->count == 0) {
				info->count = 0;
				isc_do_point_sample_2(fg);
				if (info->sample_index > 0 && info->sample_index % 5 == 0 && !info->need_stop) {
					if (info->sample_index > 9 || info->average_point_done_cnt >= 3) {
						info->need_stop = true;
						report_isc_data_2(info);
#line 338
						mca_log_info("all samples collected. Stopping. ave point cnt:%d",
							     info->average_point_done_cnt);
					} else {
						info->state = ISC_STATE_WAITING;
						interval_ms = false;
#line 343
						mca_log_info("new average point [%d] start", info->sample_index / 5);
					}
				}
			}
			info->count++;
			break;
		default:
#line 350
			mca_log_info("invalid state %d", info->state);
			info->need_stop = true;
			break;
		}
	}

	if (info->need_stop) {
		stop_isc_detect_2_work(info);
		return;
	}
	schedule_delayed_work(&fg->isc_detect_2_work, msecs_to_jiffies(interval_ms ? 1000 : 1800000));	/* G52a: stock loads system_wq after msecs_to_jiffies (schedule_delayed_work) */
}

static void isc_do_point_sample(struct isc_detect_info *info)
{
	int curr = 0, volt = 0, temp = 0;
	int ret, idx;

	ret = platform_fg_ops_get_curr(0, &curr);
	ret |= platform_fg_ops_get_volt(0, &volt);
	ret |= platform_fg_ops_get_temp(0, &temp);
	if (ret) {
		curr = 0;
		temp = 0;
		volt = 0;
#line 56
		mca_log_err("get curr/volt/temp failed");
	}

	idx = info->sample_index;
	info->isc_data[idx / 5][idx % 5].temp = temp / 10;
	info->isc_data[idx / 5][idx % 5].volt = volt;
	info->isc_data[idx / 5][idx % 5].curr = curr / 1000;
	info->sample_index++;
#line 65
	mca_log_info("sample [%d][%d] collected: temp=%d, volt=%d, curr=%d", idx / 5, idx % 5, temp / 10, volt, curr / 1000);
}

void strategy_isc_detect_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, isc_detect_work.work);
	struct isc_detect_info *info = &fg->isc_det_info;
	static bool interval_ms;

	if (!fg->usb_online && !fg->wls_online) {
		info->need_stop = true;
#line 111
		mca_log_info("adapter not present, stop work");
	} else if (info->need_stop) {
#line 115
		mca_log_info("need_stop flag set, stop work");
	} else {
#line 118
		mca_log_info("count:%d, state:%d", info->count, info->state);
		switch (info->state) {
		case ISC_STATE_WAITING:
			if (info->count >= 20) {
				info->count = 0;
				info->state = ISC_STATE_SAMPLING;
				interval_ms = true;
#line 125
				mca_log_info("start to sample");
			} else {
				info->count++;
			}
			break;
		case ISC_STATE_SAMPLING:
			if (info->count > 4 || info->count == 0) {
				info->count = 0;
				isc_do_point_sample(info);
				if (info->sample_index > 0 && info->sample_index % 5 == 0) {
					if (info->sample_index >= 10) {
						info->need_stop = true;
						info->collect_done = 1;
						report_isc_data(info);
#line 141
						mca_log_info("all samples collected. Stopping.");
					} else {
						info->state = ISC_STATE_WAITING;
						interval_ms = false;
#line 145
						mca_log_info("new average point [%d] started", info->sample_index / 5);
					}
				}
			}
			info->count++;
			break;
		default:
#line 152
			mca_log_info("invalid state %d", info->state);
			info->need_stop = true;
			break;
		}
	}

	if (info->need_stop) {
		stop_isc_detect_work(info);
		return;
	}
	schedule_delayed_work(&fg->isc_detect_work, msecs_to_jiffies(interval_ms ? 1000 : 60000));	/* G52a: as isc_detect_2_work */
}

#define LI_PLATING_REPORT_TAG "BATTERY_SAFE_REPORT=Li_plating"

static void report_li_plating_data(struct li_plating_info *info)
{
	struct mca_event_notify_data event_data = { 0 };
	char buf[BUF_SZ];
	struct batterydata *d;
	int len = 0, group = 1, i;

	memset(buf, 0, sizeof(buf));
	/* G25c: direct member accesses (not `d = &li_data_1[idx]`): clang's array-bounds trap is `idx < 10` for an access, `idx <= 10` for an address-of: stock = `cmp #0xa` */
	event_data.event_len = scnprintf(buf, BUF_SZ, "%s:%d-%d:t=%d,v=%d,i=%d", LI_PLATING_REPORT_TAG, 0, 3,
					 info->li_data_1[info->before_full_index].temp,
					 info->li_data_1[info->before_full_index].volt,
					 info->li_data_1[info->before_full_index].curr);
	event_data.event = buf;
	mca_event_report_uevent(&event_data);
#line 419
	mca_log_info("%s", buf);
	memset(buf, 0, sizeof(buf));
	msleep(100);

	for (i = 0; i < LI_DATA2_NUM; i++) {
		if (i % 10 == 0)
			len = scnprintf(buf, BUF_SZ, "%s:%d-%d:", LI_PLATING_REPORT_TAG, group++, 3);
		/* stock +0x378: tested on every iteration (after the optional group header), not only inside the header block */
		if (len > 498)
			break;
		d = &info->li_data_2[i];
		len += scnprintf(buf + len, BUF_SZ - len, "p=%d,t=%d,v=%d,i=%d,", i, d->temp, d->volt, d->curr);
		if ((i + 1) % 10 == 0 || i == LI_DATA2_NUM - 1) {
			if (len > 0 && len <= 499 && buf[len - 1] == ',')
				buf[--len] = '\0';
			event_data.event = buf;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
#line 443
			mca_log_info("%s", buf);
			msleep(100);
		}
	}
}

void strategy_li_plating_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, li_plating_detect_work.work);
	struct li_plating_info *info = &fg->li_plating_det_info;
	int curr = 0, volt = 0, temp = 0;	/* G25c: stock stack slots: curr 0x24, volt 0x20, temp 0x1c (CODEGEN) */
	bool need_stop = false;
	int ret, idx;

	if (!fg->usb_online && !fg->wls_online) {
#line 458
		mca_log_info("no power present, stop work");
		stop_li_plating_detect_work(info);
		return;
	}

#line 461
	mca_log_info("full_cnt:%d, state:%d, before_full_index:%d", info->full_count, info->state, info->before_full_index);
	if (info->full_count == 1 && info->state == LI_PLATING_BEFORE_FULL) {
		info->sample_index = 0;
		info->state = LI_PLATING_AFTER_FULL;
	}

	ret = platform_fg_ops_get_curr(0, &curr);
	ret |= platform_fg_ops_get_volt(0, &volt);
	ret |= platform_fg_ops_get_temp(0, &temp);
	if (ret) {
		temp = 0;
		volt = 0;
		curr = 0;
#line 471
		mca_log_err("get curr/volt/temp failed");
	}

	switch (info->state) {
	case LI_PLATING_BEFORE_FULL:
		idx = info->sample_index % 10;
		/* G23c (CODEGEN try): stock does the li_data_1[idx] bounds check before the store of before_full_index */
		info->li_data_1[idx].temp = temp / 10;
		info->li_data_1[idx].volt = volt;
		info->li_data_1[idx].curr = curr / 1000;
		info->before_full_index = idx;
#line 481
		mca_log_info("p1=%d, t=%d, v=%d, i=%d", idx, temp / 10, volt, curr / 1000);
		break;
	case LI_PLATING_AFTER_FULL:
		idx = info->sample_index;
		info->li_data_2[idx].temp = temp / 10;
		info->li_data_2[idx].volt = volt;
		info->li_data_2[idx].curr = curr / 1000;
#line 488
		mca_log_info("p2=%d, t=%d, v=%d, i=%d", idx, temp / 10, volt, curr / 1000);
		break;
	default:
		break;
	}

	info->sample_index++;
	if (info->state == LI_PLATING_AFTER_FULL && info->sample_index >= LI_DATA2_NUM) {
		report_li_plating_data(info);
#line 497
		mca_log_info("data collected done, stop work");
		need_stop = true;
	}
	if (info->state == LI_PLATING_AFTER_FULL && curr > 100999) {
#line 501
		mca_log_info("curr high, stop and clear data");
		need_stop = true;
	}

	if (need_stop) {
		stop_li_plating_detect_work(info);
		return;
	}
	queue_delayed_work(system_wq, &fg->li_plating_detect_work, msecs_to_jiffies(1000));
}

int mca_strategy_check_li_plating(void *data)
{
	struct strategy_fg *fg = data;
	struct li_plating_info *info = &fg->li_plating_det_info;
	int charging = 0, online = 0;
	int temp = fg->batt_temperature;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, STRATEGY_STATUS_TYPE_ONLINE, &online);
	mca_strategy_func_get_status(online ? STRATEGY_FUNC_TYPE_QUICK_WIRELESS : STRATEGY_FUNC_TYPE_QUICK_CHARGE,
				     2, &charging);

	if (info->full_count)
		return 0;
	if (info->isDetecting || temp < 150 || fg->fast_charge || charging == 1)
		return 0;

#line 378
	mca_log_info("cc:%d, detecting:%d, full_cnt:%d, temp:%d, rsoc:%d", fg->batt_cyclecount, info->isDetecting,
		     info->full_count, temp / 10, fg->batt_rsoc);
	if (!info->isDetecting && !info->full_count && temp >= 160 && fg->batt_rsoc >= 96) {
		info->state = LI_PLATING_BEFORE_FULL;
		info->sample_index = 0;
		info->isDetecting = true;
		memset(info->li_data_1, 0, sizeof(info->li_data_1));
		memset(info->li_data_2, 0, sizeof(info->li_data_2));
		queue_delayed_work(system_wq, &fg->li_plating_detect_work, msecs_to_jiffies(1000));
#line 388
		mca_log_info("li_plating_det start detect work");
	}
	return 0;
}
