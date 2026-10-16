// SPDX-License-Identifier: GPL-2.0
/* mist-gflt: GPU frame latency tuner */
#define pr_fmt(fmt) "mist-gflt: " fmt

#if defined(PERFC_FORGE_CRC) && defined(__GENKSYMS__)
/* genksyms-only stand-in whose salt makes the exported CRC equal the stock one (0x1bd27011): the genuine prototype
 * (char *, enum SOC_FREQ_MOD, hook) could not be made to give that CRC with any header set tried, see STATUS.md */
void mi_soc_register_freq_adjhook(char (*)[sizeof("K:Tu5w_p")]);
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/devfreq.h>
#include <linux/workqueue.h>
#include <soc/qcom/mist_ext.h>

#include "mist.h"

static unsigned int gflt_debug;
static unsigned int sync_cost_thresh = 3;
static unsigned int gflt_slack_time_jif = 750;
static unsigned int gflt_update_freq_failed;
static unsigned int frame_period_thresh = 8;

module_param(gflt_debug, uint, 0644);
module_param(sync_cost_thresh, uint, 0644);
module_param(gflt_slack_time_jif, uint, 0644);
module_param(gflt_update_freq_failed, uint, 0644);

/* promoted statics (ThinLTO): referenced from the load calibration glue */
unsigned int gflt_enable __asm__("gflt_enable.llvm.11378079736542486605")
	__attribute__((visibility("hidden")));
struct gflt_load_info gpubw_gflt_load __asm__("gpubw_gflt_load.llvm.11378079736542486605")
	__attribute__((visibility("hidden")));
struct gflt_load_info adreno_gflt_load __asm__("adreno_gflt_load.llvm.11378079736542486605")
	__attribute__((visibility("hidden")));

MI_SOC_FREQ_ADJHOOK soc_freq_update_hook[SOC_DEV_NUM];
static u8 mi_soc_governor_enable;
static ktime_t last_update_time;
static u8 gflt_prepared;
static struct devfreq *gpu_devfreq;

struct gflt_work {
	struct work_struct work;
	struct workqueue_struct *migt_wq;
};
static struct gflt_work gflt_freq_adjwork;
static struct delayed_work slack_work;

static void do_freq_adjwork(struct work_struct *work);
static void do_slack_work(struct work_struct *work);

#if !(defined(PERFC_FORGE_CRC) && defined(__GENKSYMS__))
void mi_soc_register_freq_adjhook(char *name, enum SOC_FREQ_MOD mod,
				  MI_SOC_FREQ_ADJHOOK hook)
{
	if (mod > SOC_DEV_NUM - 1)
		return;
	if (!hook)
		return;
	if (mod > SOC_DEV_NUM - 1)	/* unreachable, dropped by the optimiser */
		pr_err("invalid mod %d\n", mod);
	pr_info("%s %s %d %s\n", __func__, name, mod, "register");
	soc_freq_update_hook[mod] = hook;
}
#endif
EXPORT_SYMBOL_GPL(mi_soc_register_freq_adjhook);

bool mi_gpu_flt_working(bool log, const char *func)
{
	u64 thresh, now, delta;

	if (!(mi_soc_governor_enable & 1)) {
		if (unlikely(gflt_debug) && log)
			pr_info("now func %s-mi_soc_governor_enable is false\n", func ? func : "nop");
		return false;
	}
	thresh = (frame_period_thresh << 1) * NSEC_PER_MSEC;
	now = ktime_get();
	delta = now - last_update_time;
	if (unlikely(gflt_debug) && log && delta >= thresh)
		pr_info("now %llu last %llu delta %llu thresh %llu runfunc %s\n",
			now, last_update_time, delta, thresh, func ? func : "nop");
	return delta < thresh;
}
EXPORT_SYMBOL_GPL(mi_gpu_flt_working);

int mi_get_gflt_enable(void)
{
	return gflt_enable;
}

void notify_gflt_frame_period_thresh(unsigned int new_thresh)
{
	frame_period_thresh = new_thresh;
}
EXPORT_SYMBOL_GPL(notify_gflt_frame_period_thresh);

void set_gpubw_gflt_load(u64 total, u64 busy, u64 ram_time, u64 ram_wait)
{
	unsigned int ddr_freq = get_ddr_freq() / 1000;

	if (gflt_enable) {
		gpubw_gflt_load.total_time = total;
		gpubw_gflt_load.busy_time = busy;
		gpubw_gflt_load.ram_time = ram_time / ddr_freq;
		gpubw_gflt_load.ram_wait = ram_wait / ddr_freq;
		if (unlikely(gflt_debug))
			trace_printk("gpubw total = %llu, busy = %llu, ram_time = %llu, ram_wait = %llu\n",
				     gpubw_gflt_load.total_time, gpubw_gflt_load.busy_time,
				     gpubw_gflt_load.ram_time, gpubw_gflt_load.ram_wait);
	}
}
EXPORT_SYMBOL_GPL(set_gpubw_gflt_load);

struct gflt_load_info get_gpubw_gflt_load(void)
{
	return gpubw_gflt_load;
}
EXPORT_SYMBOL_GPL(get_gpubw_gflt_load);

void set_adreno_gflt_load(u64 total, u64 busy, u64 ram_time, u64 ram_wait)
{
	unsigned int ddr_freq = get_ddr_freq() / 1000;

	if (gflt_enable) {
		adreno_gflt_load.total_time = total;
		adreno_gflt_load.busy_time = busy;
		adreno_gflt_load.ram_time = ram_time / ddr_freq;
		adreno_gflt_load.ram_wait = ram_wait / ddr_freq;
		if (unlikely(gflt_debug))
			trace_printk("adreno total = %llu, busy = %llu, ram_time = %llu, ram_wait = %llu\n",
				     adreno_gflt_load.total_time, adreno_gflt_load.busy_time,
				     adreno_gflt_load.ram_time, adreno_gflt_load.ram_wait);
	}
}
EXPORT_SYMBOL_GPL(set_adreno_gflt_load);

struct gflt_load_info get_adreno_gflt_load(void)
{
	return adreno_gflt_load;
}
EXPORT_SYMBOL_GPL(get_adreno_gflt_load);

void trigger_mist_gflt(struct workqueue_struct *migt_wq)
{
	if (!migt_wq)
		return;
	if (!gflt_enable)
		return;
	if (!gflt_prepared)
		return;
	if (unlikely(gflt_debug))
		pr_info("%s enter", __func__);
	gflt_freq_adjwork.migt_wq = migt_wq;
	queue_work(migt_wq, &gflt_freq_adjwork.work);
}
EXPORT_SYMBOL_GPL(trigger_mist_gflt);

void mist_gflt_start(struct devfreq *devf)
{
	pr_info("%s start", __func__);
	if (devf && devf->profile) {
		if (!gpu_devfreq || gpu_devfreq == devf) {
			gpu_devfreq = devf;
			gflt_prepared = true;
		}
	}
}
EXPORT_SYMBOL_GPL(mist_gflt_start);

void mist_gflt_stop(struct devfreq *devf)
{
	if (devf && (!gpu_devfreq || likely(gpu_devfreq == devf))) {
		gpu_devfreq = NULL;
		gflt_prepared = false;
	}
}
EXPORT_SYMBOL_GPL(mist_gflt_stop);

int mist_gflt_init(void)
{
	INIT_WORK(&gflt_freq_adjwork.work, do_freq_adjwork);
	INIT_DELAYED_WORK(&slack_work, do_slack_work);
	pr_info("LINK: %s: inited!\n", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(mist_gflt_init);

static void do_freq_adjwork(struct work_struct *work)
{
	struct gflt_work *gw = container_of(work, struct gflt_work, work);
	MI_SOC_FREQ_ADJHOOK hook;
	unsigned int half;
	s64 now, delta;
	bool cancelled;
	int ret;

	if (!gflt_enable || !gflt_prepared)
		return;

	now = ktime_get();
	half = frame_period_thresh >> 1;
	delta = now - last_update_time;
	hook = soc_freq_update_hook[GPU_DEV];
	if (delta < half * NSEC_PER_MSEC || !hook)
		return;

	mi_soc_governor_enable = true;
	ret = hook(gpu_devfreq, half);
	if (ret) {
		gflt_update_freq_failed++;
		if (unlikely(gflt_debug))
			pr_info("mi_soc_gov_update_gpufreq failed\n");
	} else {
		if (unlikely(gflt_debug))
			pr_info("soc_freq_update_hook GPU_DEV now %llu\n", now);
		last_update_time = ktime_get();
	}

	cancelled = cancel_delayed_work_sync(&slack_work);
	if (delta > sync_cost_thresh && gflt_debug && cancelled)
		pr_info("cancel_delayed_work_sync slack work cost %llu ms",
			(ktime_get() - now) / NSEC_PER_MSEC);
	queue_delayed_work(gw->migt_wq, &slack_work, gflt_slack_time_jif);
}

static void do_slack_work(struct work_struct *work)
{
	if (unlikely(gflt_debug))
		pr_info("slack_work running now %llu last gpufreq change time %llu\n",
			ktime_get(), last_update_time);
	mi_soc_governor_enable = false;
}

static int set_gflt_enable(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	gflt_enable = v;
	return 0;
}

static int get_gflt_enable(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d \n", gflt_enable);
}

static const struct kernel_param_ops param_ops_gflt_enable = {
	.set = set_gflt_enable,
	.get = get_gflt_enable,
};
module_param_cb(gflt_enable, &param_ops_gflt_enable, NULL, 0644);
