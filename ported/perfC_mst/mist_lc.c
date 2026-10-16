// SPDX-License-Identifier: GPL-2.0
/* mist-lc: load calibration glue between the gpu/ddr frame trackers and mist */
#define pr_fmt(fmt) "mist-lc: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>

#include "mist.h"

static unsigned int lc_debug;
static unsigned int gflt_wait_threshold = 10;

module_param(lc_debug, uint, 0644);
module_param(gflt_wait_threshold, uint, 0644);

u64 gpu_load_calibration(void)
{
	struct gflt_load_info info;

	if (!mi_get_gflt_enable())
		return 0;
	info = get_adreno_gflt_load();
	if (unlikely(lc_debug))
		trace_printk("adreno total = %llu, busy = %llu, ram_time = %llu, ram_wait = %llu\n",
			     info.total_time, info.busy_time, info.ram_time, info.ram_wait);
	if (info.total_time) {
		u32 wait_pct = (u32)(info.ram_wait * 100) / (u32)info.ram_time;
		u64 diff = info.busy_time - info.ram_wait;

		if (wait_pct > gflt_wait_threshold && info.busy_time > info.ram_wait)
			return diff;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(gpu_load_calibration);

u64 gpubw_load_calibration(void)
{
	struct gflt_load_info info;

	if (!mi_get_gflt_enable())
		return 0;
	info = get_gpubw_gflt_load();
	if (unlikely(lc_debug))
		trace_printk("gpubw total = %llu, busy = %llu, ram_time = %llu, ram_wait = %llu\n",
			     info.total_time, info.busy_time, info.ram_time, info.ram_wait);
	if (info.total_time) {
		u32 wait_pct = (u32)(info.ram_wait * 100) / (u32)info.ram_time;
		u64 diff = info.busy_time - info.ram_wait;

		if (wait_pct > gflt_wait_threshold && info.busy_time > info.ram_wait)
			return diff;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(gpubw_load_calibration);

u64 cpu_load_calibration(void)
{
	return 0;
}

u64 ddr_load_calibration(void)
{
	return 0;
}

void trigger_mist_load_tracking(struct workqueue_struct *migt_wq)
{
	if (mist_perf_api_enable() && !migt_wq) {
		trigger_mist_dflt();
		if (lc_debug)
			pr_info("%s walt enter ", __func__);
	} else if (!mist_perf_api_enable() && migt_wq) {
		trigger_mist_dflt();
		trigger_mist_gflt(migt_wq);
		if (lc_debug)
			pr_info("%s migt enter ", __func__);
	}
}
EXPORT_SYMBOL_GPL(trigger_mist_load_tracking);
