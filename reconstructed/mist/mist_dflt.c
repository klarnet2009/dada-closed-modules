// SPDX-License-Identifier: GPL-2.0
/* mist-dflt: DDR/LLCC/L3 latency (memlat) frame tuner */
#define pr_fmt(fmt) "mist-dflt: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/cpufreq.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/arch_topology.h>
#include <linux/interrupt.h>
#include <soc/qcom/pmu_lib.h>
#include <soc/qcom/dcvs.h>
#include <soc/qcom/mist_ext.h>

#include "mist.h"
#include "mist_dflt.h"

#define MIST_UNUSED_HW	3

static unsigned int dflt_ftrace_debug;
static unsigned int dflt_debug;
static unsigned int need_boost_in_frame;
static unsigned int dflt_fpercent_other = 2;
static unsigned int max_ddr_ib = 4224000;
static unsigned int spm_thres = 200;
static unsigned int dflt_ddr_ib_scale = 100;
static unsigned int dflt_ddr_boost;
static unsigned long dflt_cpu_mask_debug;
static unsigned int dflt_spm_low_threshold;
static unsigned int dflt_ddr_ib_scale_low = 100;
static unsigned int dflt_ddr_ib_scale_high = 100;
static unsigned int dflt_spm_high_threshold = 10000;
static unsigned int dflt_memlat_vote_type;
static unsigned int mist_memlat_vote_enable;
unsigned int dflt_stallratio_thresh[4];
unsigned int dflt_spm_thresh[4] = { 150, 150, 150, 150 };
unsigned int dflt_ips_thresh[4];
static char *mist_version = MIST_VERSION;
static unsigned int be_procent = 100;
static unsigned int mist_soc_min_stall = 200;
static unsigned int mist_set_uid;
unsigned int mist_soc_freq_thresh[4] = { 1440000, 1958400, 1843200, 2342400 };

module_param(dflt_ftrace_debug, uint, 0644);
module_param(dflt_debug, uint, 0644);
module_param(need_boost_in_frame, uint, 0644);
module_param(dflt_fpercent_other, uint, 0644);
module_param(max_ddr_ib, uint, 0644);
module_param(spm_thres, uint, 0644);
module_param(dflt_ddr_ib_scale, uint, 0644);
module_param(dflt_ddr_boost, uint, 0644);
module_param(dflt_cpu_mask_debug, ulong, 0644);
module_param(dflt_spm_low_threshold, uint, 0644);
module_param(dflt_ddr_ib_scale_low, uint, 0644);
module_param(dflt_ddr_ib_scale_high, uint, 0644);
module_param(dflt_spm_high_threshold, uint, 0644);
module_param(dflt_memlat_vote_type, uint, 0644);
module_param(mist_memlat_vote_enable, uint, 0644);
module_param_array(dflt_stallratio_thresh, uint, NULL, 0644);
module_param_array(dflt_spm_thresh, uint, NULL, 0644);
module_param_array(dflt_ips_thresh, uint, NULL, 0644);
module_param(mist_version, charp, 0444);
module_param(be_procent, uint, 0644);
module_param(mist_soc_min_stall, uint, 0644);
module_param(mist_set_uid, uint, 0644);
module_param_array(mist_soc_freq_thresh, uint, NULL, 0644);

MODULE_LICENSE("GPL");
MODULE_VERSION(MIST_VERSION);
MODULE_DESCRIPTION("soc performance tuner developed by David");

static unsigned int mist_soc_api_enable;
static struct mist_map_entry *mist_map[MIST_MAX_HW][MIST_MAX_CLUSTER];
static unsigned long mist_cpus[MIST_MAX_HW][MIST_MAX_CLUSTER];
struct mist_cluster_cpus cpu_list_map[4];
static unsigned int dflt_latency_enable;
static u8 mist_memlat_vote_start;
static unsigned int mist_commit_mask;
static struct frame_load frame_stats_record[MIST_FRAME_HIST];
static int cur_record;
static u64 frame_index;
static unsigned int mist_cluster_mask;
static u64 mc_cc;
unsigned int mist_next_freq[4];
mi_dflt_bw_update_cb dflt_bw_update_cb;
mi_dflt_lat_update_cb dflt_lat_update_cb;
mi_dflt_lat_cal_cb dflt_lat_cal_cb;
ktime_t frame_start_time;
ktime_t last_frame_start_time;
struct hrtimer dflt_vote_timer;
bool dflt_memlat_sample_start;
static int dflt_latency_cur_vote_counter;
bool dflt_latency_vote_start;
static int dflt_latency_enable_from_user;
static int dflt_bwmon_enable;
u8 dflt_bwmon_sample_start[2];
static int dflt_bwmon_counter_enable;
static int dflt_bwmon_counter;
u32 cur_dflt_ib[2];
static int mem_sample_counter;
struct hrtimer dflt_timer;
struct hrtimer memlat_vote_timer;
u32 dflt_ev_ids[3];
u32 sampled_max_spm[20];
u32 sampled_spm_idx;
u32 sampled_max_cpu_freq[20];
u32 spm_window_size;
u32 spm_vote_inc_steps;
u32 disable_spm_value;
u64 prev_max_miss;
u32 spm_freq_map[4];
u32 last_ddr_latency_ib;
struct hrtimer *dflt_lat_vote_timer;
struct {
	u32 a, b, c, d;
	u64 e;
} dflt_ev_data[3];
u32 cur_ddr_clk;
u32 spm_max_freq;
bool inited;
u32 last_dflt_ib;
u32 last_ddr_freq;
static struct workqueue_struct *dflt_wq;
static struct work_struct fast_vote_work;
static DEFINE_SPINLOCK(dflt_commit_lock);
static struct proc_dir_entry *frame_load_dir;
static struct proc_dir_entry *history_load_entry;
static u32 last_vote_freq[3];
static u32 cur_vote_freq[3];
static const int cluster[8] = { 0, 0, 0, 0, 0, 0, 1, 1 };

static DEFINE_PER_CPU(struct dflt_latency_cpu_stats *, sampling_stats);

int mist_ddr_vote_enable(void)
{
	return mist_memlat_vote_enable;
}
EXPORT_SYMBOL_GPL(mist_ddr_vote_enable);

unsigned int cpufreq_to_memfreq(int cid, unsigned int cpufreq_mhz, int hw_type)
{
	struct dflt_latency_cpu_stats *stats;
	struct mist_map_entry *map, **slot __maybe_unused;
	int cpu, cpus_id = 0, i = 0, idx;
	unsigned int ret;

	for_each_possible_cpu(cpu) {
		if (cpu_topology[cpu].cluster_id != cid)
			continue;
		for (i = 0; i < MIST_MAX_CLUSTER; i++) {
			if (mist_map[hw_type][i] &&
			    (mist_cpus[hw_type][i] & (1UL << cpu))) {
				cpus_id = i;
				goto found;
			}
		}
	}
found:
	slot = &mist_map[hw_type][cpus_id];
	map = *slot;
	if (!map)
		return 0;

	if (unlikely(dflt_debug))
		pr_err(" %s cid: %d, cpus_id: %d ,mist_cpus[%d][%d] :%0lx \n",
		       __func__, cid, cpus_id, hw_type, i, mist_cpus[hw_type][i]);

	map = *slot;

	while (map->cpufreq_mhz && map->cpufreq_mhz < cpufreq_mhz) {
		if (unlikely(dflt_debug))
			pr_err(" %s map cpufreq_mhz :%d, memfreq_khz: %d!\n",
			       __func__, map->cpufreq_mhz, map->memfreq_khz);
		map++;
	}

	for (i = 0; i < cpu_list_map[cid].cpu_num; i++) {
		cpu = cpu_list_map[cid].cpus[i];
		stats = per_cpu(sampling_stats, cpu);
		if (!map->cpufreq_mhz)
			break;
		if (unlikely(dflt_debug))
			pr_err(" %s cur_spm :%d!, ips: %d \n", __func__,
			       stats->spm[0], stats->ips);
		if (stats->spm[0] > dflt_spm_thresh[cid]) {
			if (unlikely(dflt_debug))
				pr_err(" %s cur spm :%d!\n", __func__, stats->spm[0]);
			map++;
			break;
		}
		if (stats->ips < dflt_ips_thresh[cid]) {
			if (unlikely(dflt_debug))
				pr_err(" %s cur ips:%d!\n", __func__, stats->ips);
			map++;
			break;
		}
	}

	idx = map->cpufreq_mhz == 0 ? -1 : 0;
	ret = map[idx].memfreq_khz;
	if (unlikely(dflt_debug))
		pr_err(" %s next freq %d ,cpufreq_mhz %d mem_khz:%d!\n", __func__,
		       cpufreq_mhz, map[idx].cpufreq_mhz, ret);
	return ret;
}

int mi_dflt_get_latency_enable(void)
{
	return READ_ONCE(dflt_latency_enable);
}
EXPORT_SYMBOL_GPL(mi_dflt_get_latency_enable);

int mist_update_ddr_freq(int cid, int target_freq)
{
	unsigned int min_stall = mist_soc_min_stall;
	unsigned int tmp;
	struct frame_load *rec;
	int new_freq;
	int i;

	if (!READ_ONCE(dflt_latency_enable))
		return 0;
	if (mist_memlat_vote_start != 1)
		return 0;

	mist_next_freq[cid] = target_freq;
	mist_commit_mask |= BIT(cid);
	rec = &frame_stats_record[cur_record];

	if (min_stall >= 1 && min_stall < 1024) {
		for (i = 0; i < cpu_list_map[cid].cpu_num; i++) {
			unsigned int stall = rec->stall_ratio[cpu_list_map[cid].cpus[i]];

			if (stall < min_stall)
				min_stall = stall;
		}
	}

	tmp = (1024 - min_stall) * target_freq;
	new_freq = tmp >> 10;
	if (unlikely(dflt_debug))
		pr_info("%s enter cid: %d, target_freq:%d, frame_index %llu, new freq:%d, min_stall = %u, mist_commit_mask = %d mist_cluster_mask=%d \n",
			__func__, cid, target_freq, frame_index, new_freq, min_stall,
			mist_commit_mask, mist_cluster_mask);
	if (tmp >= 1024)
		mist_next_freq[cid] = new_freq;

	if (mist_commit_mask == mist_cluster_mask) {
		mist_commit_mask = 0;
		mist_memlat_vote_start = 0;
		if (unlikely(dflt_ftrace_debug)) {
			clk_debug_mc_cc_get(NULL, &mc_cc);
			trace_printk("frame_index = %llu, l_freq = %d, m_freq = %d, m2_freq = %d, b_freq = %d, ddr_freq = %llu \n",
				     frame_index, mist_next_freq[0], mist_next_freq[1],
				     mist_next_freq[2], mist_next_freq[3], mc_cc);
		}
		if (unlikely(dflt_debug))
			pr_info("%s ,frame_index = %llu, l_freq = %d, m_freq = %d, m2_freq = %d, b_freq = %d \n",
				__func__, frame_index, mist_next_freq[0], mist_next_freq[1],
				mist_next_freq[2], mist_next_freq[3]);
		if (!mist_memlat_vote_enable) {
			if (!hrtimer_active(&dflt_vote_timer))
				hrtimer_start(&dflt_vote_timer, ms_to_ktime(1), HRTIMER_MODE_REL);
		}
	}
	return new_freq;
}
EXPORT_SYMBOL_GPL(mist_update_ddr_freq);

bool mi_dflt_get_latency_sample_start(void)
{
	return dflt_memlat_sample_start;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_latency_sample_start);

void mi_dflt_set_latency_sample_start(bool new_dflt_latency_sample_start)
{
	dflt_memlat_sample_start = new_dflt_latency_sample_start;
	if (!new_dflt_latency_sample_start) {
		dflt_latency_cur_vote_counter++;
		if (unlikely(dflt_debug))
			pr_info("%s dflt_latency_cur_vote_counter: %d", __func__,
				dflt_latency_cur_vote_counter);
	}
}
EXPORT_SYMBOL_GPL(mi_dflt_set_latency_sample_start);

bool mi_dflt_get_latency_vote_start(void)
{
	return dflt_latency_vote_start;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_latency_vote_start);

void mi_dflt_set_latency_vote_start(bool new_dflt_latency_vote_start)
{
	if (dflt_debug && !new_dflt_latency_vote_start)
		pr_info("%s enter", __func__);
	if (dflt_latency_cur_vote_counter >= 2 && new_dflt_latency_vote_start)
		return;
	dflt_latency_vote_start = new_dflt_latency_vote_start;
}
EXPORT_SYMBOL_GPL(mi_dflt_set_latency_vote_start);

int mi_dflt_get_latency_enable_from_user(void)
{
	return dflt_latency_enable_from_user;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_latency_enable_from_user);

void mi_dflt_set_latency_enable(int new_dflt_latency_enable)
{
	dflt_latency_enable = new_dflt_latency_enable;
}
EXPORT_SYMBOL_GPL(mi_dflt_set_latency_enable);

int mi_dflt_get_bwmon_enable(void)
{
	return dflt_bwmon_enable;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_bwmon_enable);

void mi_dflt_set_bwmon_enable(int new_dflt_bwmon_enable)
{
	dflt_bwmon_enable = new_dflt_bwmon_enable;
}
EXPORT_SYMBOL_GPL(mi_dflt_set_bwmon_enable);

bool mi_dflt_get_bwmon_sample_start(int hw_type)
{
	if (hw_type >= 0 && hw_type <= 1)
		return dflt_bwmon_sample_start[hw_type];
	return false;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_bwmon_sample_start);

void mi_dflt_set_bwmon_sample_start(int hw_type, bool new_dflt_bwmon_sample_start)
{
	if (hw_type < 0 || hw_type > 1)
		return;
	if (unlikely(dflt_debug))
		pr_info("%s enter new_dflt_bwmon_sample_start: %d, cur:%d", __func__,
			new_dflt_bwmon_sample_start, dflt_bwmon_sample_start[hw_type]);
	if (unlikely(dflt_bwmon_counter_enable)) {
		if (hw_type == 0)
			dflt_bwmon_counter++;
	}
	dflt_bwmon_sample_start[hw_type] = new_dflt_bwmon_sample_start;
}
EXPORT_SYMBOL_GPL(mi_dflt_set_bwmon_sample_start);

u32 *mi_dflt_get_ddr_frame_load(void)
{
	return cur_dflt_ib;
}
EXPORT_SYMBOL_GPL(mi_dflt_get_ddr_frame_load);

int mist_perf_api_enable(void)
{
	return READ_ONCE(mist_soc_api_enable);
}
EXPORT_SYMBOL_GPL(mist_perf_api_enable);

static inline unsigned long map_coeff_freq(unsigned int cpu, unsigned long util,
					  unsigned long cap, unsigned int max_freq,
					  u64 min_stall)
{
	unsigned long freq = 0;

	if (min_stall < 1024) {
		freq = mult_frac((unsigned long)max_freq * util, 1024 - min_stall, cap << 10);
		freq += dflt_fpercent_other ? freq >> dflt_fpercent_other : 0;
	}
	if (dflt_debug)
		pr_info("%s [%u]: target_freq[%lu], util = %lu, max = %lu, min_stall %llu",
			__func__, cpu, freq, util, cap, min_stall);
	return freq;
}

void mist_set_next_freq_hook(unsigned long util, unsigned long freq,
			     unsigned long cap, unsigned long *next_freq,
			     struct cpufreq_policy *policy, bool *need_freq_update)
{
	unsigned int cpu = policy->cpu;
	unsigned int c = cpu;
	int cid = cluster[cpu];
	unsigned int max_freq = policy->cpuinfo.max_freq;
	unsigned int soc_thresh = mist_soc_freq_thresh[cid];
	unsigned int util_thresh = cap >> 3;
	unsigned long old_freq = *next_freq;
	struct frame_load *rec;
	u64 min_stall;
	unsigned int target;
	unsigned long t;
	int i;

	if ((util <= util_thresh) | (old_freq <= soc_thresh))
		return;

	rec = &frame_stats_record[cur_record];
	min_stall = mist_soc_min_stall;
	for (i = 0; i < cpu_list_map[cid].cpu_num; i++) {
		unsigned int stall;

		c = cpu_list_map[cid].cpus[i];
		stall = rec->stall_ratio[c];
		if (stall < min_stall)
			min_stall = stall;
	}

	t = map_coeff_freq(c, util, cap, max_freq, min_stall);
	if (!t || t >= max_freq)
		t = max_freq;
	target = t;
	*next_freq = target;
	if (!target)
		return;
	*next_freq = min(old_freq, (unsigned long)target);
	if (dflt_debug)
		pr_info("%s [%u]: *next_freq[%lu], old_freq = %lu, freq = %lu",
			__func__, cpu, *next_freq, old_freq, freq);
}
EXPORT_SYMBOL_GPL(mist_set_next_freq_hook);

void mi_dflt_bwmon_update_cur_freq_hook(u32 new_dflt_ib, int hw_type)
{
	if (unlikely(dflt_debug))
		pr_info("%s new ib:%d, cur ib:%d, hw_type:%d", __func__,
			new_dflt_ib, cur_dflt_ib[hw_type], hw_type);
	cur_dflt_ib[hw_type] = new_dflt_ib;
	if (need_boost_in_frame)
		pr_info("%s ", __func__);
}
EXPORT_SYMBOL_GPL(mi_dflt_bwmon_update_cur_freq_hook);

void trigger_mist_dflt(void)
{
	struct frame_load *rec = &frame_stats_record[cur_record];
	int cpu;

	if (!READ_ONCE(dflt_latency_enable) && dflt_bwmon_enable < 1)
		return;

	if (unlikely(dflt_debug))
		pr_info("%s enter ", __func__);
	dflt_latency_vote_start = false;
	mi_dflt_set_bwmon_sample_start(0, true);
	mi_dflt_set_bwmon_sample_start(1, true);
	dflt_latency_cur_vote_counter = 0;
	dflt_memlat_sample_start = true;
	mist_memlat_vote_start = 1;
	mist_commit_mask = 0;
	WRITE_ONCE(mem_sample_counter, 0);
	for_each_possible_cpu(cpu)
		per_cpu(sampling_stats, cpu)->dflt_latency_sample_start = true;

	last_frame_start_time = frame_start_time;
	frame_start_time = ktime_get();
	frame_index++;
	if (last_frame_start_time) {
		rec->q2q = ktime_to_us(frame_start_time - last_frame_start_time);
		if (unlikely(dflt_ftrace_debug))
			trace_printk("frame_index = %llu, q2q_time = %llu \n", frame_index, rec->q2q);
		cur_record = (cur_record + 1) % MIST_FRAME_HIST;
	}

	if (!hrtimer_active(&dflt_timer))
		hrtimer_start(&dflt_timer, 0, HRTIMER_MODE_REL);
	if (READ_ONCE(dflt_latency_enable) && mist_memlat_vote_enable) {
		if (!hrtimer_active(&memlat_vote_timer))
			hrtimer_start(&memlat_vote_timer, ms_to_ktime(1), HRTIMER_MODE_REL);
	}
}
EXPORT_SYMBOL_GPL(trigger_mist_dflt);

void mist_update_rel_freq(struct cpufreq_policy *policy, unsigned long freq)
{
	int cpu = policy->cpu;
	unsigned int idx = cur_record ? cur_record - 1 : MIST_FRAME_HIST - 1;
	struct frame_load *rec = &frame_stats_record[idx];

	rec->freq[cluster[cpu]] = freq;
}
EXPORT_SYMBOL_GPL(mist_update_rel_freq);

void mi_dflt_process_raw_ctrs_hook(int dflt_cpu, u64 cycle_data, u64 stall_data)
{
	struct dflt_latency_cpu_stats *stats = per_cpu(sampling_stats, dflt_cpu);
	struct frame_load *rec = &frame_stats_record[cur_record];
	u64 delta_cycles, delta_stall;
	u32 ratio;

	if (dflt_debug == 5)
		pr_info("%s cpu: %d cycle_data: %llu, stall_data: %llu", __func__,
			dflt_cpu, cycle_data, stall_data);
	stats->curr_ctrs[1] = stall_data;
	stats->curr_ctrs[2] = cycle_data;
	delta_cycles = cycle_data - stats->prev_ctrs[2];
	if (!delta_cycles)
		return;
	delta_stall = stall_data - stats->prev_ctrs[1];
	ratio = mult_frac(1024, delta_stall, delta_cycles);
	ratio = mult_frac(ratio, be_procent, 100);
	rec->stall_ratio[dflt_cpu] = ratio;
	if (unlikely(dflt_debug))
		pr_info("%s cpu: %d stall_ratio: %u", __func__, dflt_cpu, ratio);
}
EXPORT_SYMBOL_GPL(mi_dflt_process_raw_ctrs_hook);

#define DFLT_DELTA(n)								\
	if (dflt_ev_ids[n]) {							\
		stats->delta_ctrs[n] = stats->curr_ctrs[n] - stats->prev_ctrs[n]; \
		if (unlikely(dflt_debug == 5))						\
			pr_info(" %s, id =  %d,curr_ctrs:%llu, del: %llu, prev:%llu!\n", \
				__func__, n, stats->curr_ctrs[n],		\
				stats->delta_ctrs[n], stats->prev_ctrs[n]);	\
	}

void mi_dflt_calculate_dflt_lantancy_hook(int hw_type, int cpu, s64 delta_us,
					  u64 instructions, u64 miss, u64 cycles)
{
	struct dflt_latency_cpu_stats *stats = per_cpu(sampling_stats, cpu);
	unsigned int idx = cur_record ? cur_record - 1 : MIST_FRAME_HIST - 1;
	struct frame_load *rec = &frame_stats_record[idx];
	u64 ips, stall;

	if (dflt_debug == 5)
		pr_info(" %s  cpu:%d, delta_us:%lld!, instructions:%llu, miss:%llu, cycles:%llu\n",
			__func__, cpu, delta_us, instructions, miss, cycles);
	if (!stats) {
		pr_info(" %s  stat\n", __func__);
		return;
	}
	if (hw_type < 0 || hw_type >= 3) {
		if (dflt_debug == 5)
			pr_info(" %s  invalid hw type: %d\n", __func__, hw_type);
		return;
	}

	if (hw_type == 0) {
		DFLT_DELTA(0)
		DFLT_DELTA(1)
		DFLT_DELTA(2)
		memcpy(stats->prev_ctrs, stats->curr_ctrs, sizeof(stats->prev_ctrs));
	}

	stats->freq_mhz = stats->delta_ctrs[2] / delta_us;
	stats->spm[hw_type] = stats->delta_ctrs[1];
	if (dflt_debug)
		pr_info(" %s, orgin spm:%u, freq_mhz: %d, cycle %llu!\n", __func__,
			stats->spm[hw_type], stats->freq_mhz, stats->delta_ctrs[2]);
	stats->ll_miss = miss;
	if (miss)
		stats->spm[hw_type] /= miss;

	stall = stats->delta_ctrs[1];
	ips = mult_frac(100, instructions, cycles);
	stats->ips = ips;
	if (dflt_debug == 5)
		pr_info(" %s, orgin cycles :%llu, stall: %llu,valid_cycles: %llu, instructions: %llu,ips: %u!\n",
			__func__, cycles, stall, cycles - stall,
			instructions, (u32)ips);

	rec->ipc[cpu] = ips;
	rec->miss[cpu] = miss;
	rec->stall_ratio[cpu] = mult_frac(1024, stats->delta_ctrs[1], cycles);
	rec->instructions[cpu] = instructions;
	if (hw_type == 0 && unlikely(dflt_ftrace_debug))
		trace_printk("frame_index = %llu, cpu = %d, ipc = %llu, miss = %llu, stall_ratio= %u, instruction = %llu\n",
			     frame_index, cpu, rec->ipc[cpu], rec->miss[cpu],
			     rec->stall_ratio[cpu], instructions);
	if (unlikely(dflt_debug))
		pr_info(" %s cpu:%d, ips:%u, spm:%u!\n", __func__, cpu, stats->ips,
			stats->spm[hw_type]);
}
EXPORT_SYMBOL_GPL(mi_dflt_calculate_dflt_lantancy_hook);

void mi_dflt_calculate_mon_sampling_freq_hook(cpumask_t cpus, int hw_type)
{
	struct dflt_latency_cpu_stats *stats;
	int cpu, i;
	unsigned int max_cpufreq = 0, max_spm = 0, sum_spm = 0, avg_spm;
	unsigned int ib;
	u64 max_miss = 0;

	if (hw_type != 0) {
		if (unlikely(dflt_debug))
			pr_info(" %s  invalid hw type: %d\n", __func__, hw_type);
		return;
	}

	if (dflt_cpu_mask_debug) {
		for_each_cpu(cpu, to_cpumask(&dflt_cpu_mask_debug)) {
			stats = per_cpu(sampling_stats, cpu);
			if (unlikely(dflt_debug))
				pr_info(" %s  stat, cpu: %d\n", __func__, cpu);
			if (!stats)
				return;
			max_cpufreq = max(max_cpufreq, stats->freq_mhz);
			if (stats->ll_miss > max_miss)
				max_miss = stats->ll_miss;
			if (stats->spm[0] > max_spm)
				max_spm = stats->spm[0];
		}
	} else {
		for_each_cpu(cpu, &cpus) {
			stats = per_cpu(sampling_stats, cpu);
			if (!stats) {
				pr_info(" %s  stat\n", __func__);
				return;
			}
			max_cpufreq = max(max_cpufreq, stats->freq_mhz);
			if (stats->ll_miss > max_miss)
				max_miss = stats->ll_miss;
			if (stats->spm[0] > max_spm)
				max_spm = stats->spm[0];
		}
	}

	if (unlikely(dflt_debug))
		pr_info(" %s, max_spm:%d, max_spm_cpufreq: %d!\n", __func__, max_spm, max_cpufreq);
	if (max_spm < dflt_spm_low_threshold)
		dflt_ddr_ib_scale = dflt_ddr_ib_scale_low;
	else if (max_spm > dflt_spm_high_threshold)
		dflt_ddr_ib_scale = dflt_ddr_ib_scale_high;

	sampled_max_spm[sampled_spm_idx] = max_spm;
	sampled_max_cpu_freq[sampled_spm_idx] = max_cpufreq;
	sampled_spm_idx = (sampled_spm_idx + 1) % spm_window_size;

	for (i = 0; i < spm_window_size; i++) {
		sum_spm += sampled_max_spm[i];
		max_cpufreq = max(max_cpufreq, sampled_max_cpu_freq[i]);
	}
	avg_spm = sum_spm / spm_window_size;
	if (unlikely(dflt_debug))
		pr_info(" %s, avg_spm:%d!\n", __func__, avg_spm);

	if (avg_spm >= spm_thres && max_cpufreq >= 2000)
		spm_vote_inc_steps++;
	else if (avg_spm < disable_spm_value && spm_vote_inc_steps)
		spm_vote_inc_steps--;

	if (spm_vote_inc_steps && prev_max_miss > max_miss) {
		u32 pct = mult_frac(100, (u32)(prev_max_miss - max_miss), prev_max_miss);

		if (pct >= 30)
			spm_vote_inc_steps = 0;
	}
	prev_max_miss = max_miss;

	if (max_cpufreq < 200) {
		spm_vote_inc_steps = 0;
		ib = 0;
	} else if (!spm_vote_inc_steps) {
		ib = 0;
	} else if (max_cpufreq < spm_freq_map[0]) {
		ib = spm_freq_map[1];
	} else if (max_cpufreq < spm_freq_map[2]) {
		ib = spm_freq_map[3];
	} else {
		ib = max_ddr_ib;
	}
	last_ddr_latency_ib = ib;
	if (unlikely(dflt_debug))
		pr_info(" %s  spm_max_vote_khz:%d! spm_vote_inc_steps : %d, prev_max_miss : %llu\n",
			__func__, ib, spm_vote_inc_steps, prev_max_miss);
}
EXPORT_SYMBOL_GPL(mi_dflt_calculate_mon_sampling_freq_hook);

void mi_dflt_update_memlat_fp_vote_hook(struct dcvs_freq *voted_freqs, int grp)
{
	u32 ib;

	if (grp < 0 || (int)dflt_memlat_vote_type < grp)
		return;

	ib = min(voted_freqs[grp].ib, max_ddr_ib);
	voted_freqs[grp].ib = ib;
	ib *= dflt_ddr_ib_scale;
	voted_freqs[grp].ib = ib / 100;
	if (dflt_ddr_boost) {
		if (ib < 54700000)
			voted_freqs[grp].ib = 547000;
		else if (ib < 76800000)
			voted_freqs[grp].ib = 768000;
		else if (ib < 155500000)
			voted_freqs[grp].ib = 1555000;
		else if (ib < 209200000)
			voted_freqs[grp].ib = 2092000;
		else if (ib < 318700000)
			voted_freqs[grp].ib = 3187000;
		else if (ib < 368600000)
			voted_freqs[grp].ib = 3686000;
		else if (ib < 422400000)
			voted_freqs[grp].ib = 4224000;
	}
	if (unlikely(dflt_debug))
		pr_info("%s  ib:%d, grp:%d!\n", __func__, voted_freqs[grp].ib, grp);
}
EXPORT_SYMBOL_GPL(mi_dflt_update_memlat_fp_vote_hook);

void register_mi_dflt_bw_update_cb(mi_dflt_bw_update_cb cb)
{
	dflt_bw_update_cb = cb;
}
EXPORT_SYMBOL_GPL(register_mi_dflt_bw_update_cb);

void unregister_mi_dflt_bw_update_cb(void)
{
	dflt_bw_update_cb = NULL;
}
EXPORT_SYMBOL_GPL(unregister_mi_dflt_bw_update_cb);

void register_mi_dflt_lat_update_cb(mi_dflt_lat_update_cb cb, struct hrtimer *timer,
				    mi_dflt_lat_cal_cb cal_f)
{
	dflt_lat_update_cb = cb;
	dflt_lat_vote_timer = timer;
	dflt_lat_cal_cb = cal_f;
}
EXPORT_SYMBOL_GPL(register_mi_dflt_lat_update_cb);

void unregister_mi_dflt_lat_update_cb(void)
{
	dflt_lat_update_cb = NULL;
	dflt_lat_vote_timer = NULL;
	dflt_lat_cal_cb = NULL;
}
EXPORT_SYMBOL_GPL(unregister_mi_dflt_lat_update_cb);

void dlft_get_latency(void)
{
	int cpu = smp_processor_id();
	struct dflt_latency_cpu_stats *stats = per_cpu(sampling_stats, cpu);
	ktime_t now = ktime_get();
	int ret, i;

	if (!dflt_memlat_sample_start)
		return;
	if (unlikely(dflt_debug))
		pr_info(" %s enter, cur_cpu: %d\n", __func__, cpu);
	if (!dflt_lat_update_cb) {
		pr_info(" %s dflt_lat_update_cb is null \n", __func__);
		return;
	}

	mi_dflt_set_latency_sample_start(false);
	stats->raw_ctrs.num_evs = 0;
	ret = qcom_pmu_read_all_local(&stats->raw_ctrs);
	if (ret < 0 || !stats->raw_ctrs.num_evs) {
		pr_err("error reading pmu counters on cpu%d: %d\n", cpu, ret);
	} else {
		dflt_lat_update_cb(cpu, now, &stats->raw_ctrs);
		mi_dflt_process_raw_ctrs_hook(cpu, stats->raw_ctrs.ev_data[1],
					      stats->raw_ctrs.ev_data[3]);
	}

	for_each_possible_cpu(i) {
		if (i == cpu)
			continue;
		stats = per_cpu(sampling_stats, i);
		stats->raw_ctrs.num_evs = 0;
		ret = qcom_pmu_read_all(i, &stats->raw_ctrs);
		if (ret < 0 || !stats->raw_ctrs.num_evs) {
			pr_err("error reading pmu counters on cpu%d: %d\n", i, ret);
			continue;
		}
		dflt_lat_update_cb(i, now, &stats->raw_ctrs);
		mi_dflt_process_raw_ctrs_hook(i, stats->raw_ctrs.ev_data[1],
					      stats->raw_ctrs.ev_data[3]);
	}
	mi_dflt_set_latency_vote_start(true);
}
EXPORT_SYMBOL_GPL(dlft_get_latency);

void mi_dflt_schedule_hook(void)
{
	int cpu = smp_processor_id();
	struct dflt_latency_cpu_stats *stats = per_cpu(sampling_stats, cpu);

	if (!stats->dflt_latency_sample_start)
		return;
	stats->dflt_latency_sample_start = false;
	if (unlikely(dflt_debug))
		pr_info(" %s enter, cur_cpu: %d\n", __func__, cpu);
	if (!dflt_lat_update_cb) {
		pr_info(" %s dflt_lat_update_cb is null \n", __func__);
		return;
	}
	dflt_lat_update_cb(cpu, frame_start_time, &stats->raw_ctrs);
}
EXPORT_SYMBOL_GPL(mi_dflt_schedule_hook);

static enum hrtimer_restart queue_dflt_sample_start(struct hrtimer *timer);
static enum hrtimer_restart queue_dflt_vote_start(struct hrtimer *timer);
static enum hrtimer_restart queue_memlat_vote_start(struct hrtimer *timer);
static int parse_cluster_node_map(struct device_node *np, int hw_type);
static void commit_fast_vote(struct work_struct *work);
static int history_load_open(struct inode *inode, struct file *file);
static const struct proc_ops history_load_fops;

int mist_dflt_init(void)
{
	struct device_node *node, *ddr, *llcc, *l3;
	struct dflt_latency_cpu_stats *stats;
	int max_cid = 0;
	int cpu, i, j;
	int ret = 0;

	hrtimer_init(&dflt_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dflt_timer.function = queue_dflt_sample_start;
	frame_index = 0;
	hrtimer_init(&dflt_vote_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	dflt_vote_timer.function = queue_dflt_vote_start;
	hrtimer_init(&memlat_vote_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	memlat_vote_timer.function = queue_memlat_vote_start;

	for_each_possible_cpu(cpu) {
		stats = kzalloc(sizeof(*stats), GFP_KERNEL);
		if (!stats) {
			ret = -ENOMEM;
			goto free_stats;
		}
		per_cpu(sampling_stats, cpu) = stats;
		spin_lock_init(&stats->ctrs_lock);
	}

	for_each_possible_cpu(cpu)
		max_cid = max(topology_cluster_id(cpu), max_cid);
	mist_cluster_mask = ~(~1U << max_cid);

	node = of_find_node_by_name(NULL, "qcom,memlat");
	if (!node) {
		pr_err(" Failed to find node 'qcom,memlat'\n");
		return -ENODEV;
	}
	ddr = of_get_child_by_name(node, "ddr");
	llcc = of_get_child_by_name(node, "llcc");
	l3 = of_get_child_by_name(node, "l3");
	if (parse_cluster_node_map(ddr, 0) == -1 ||
	    parse_cluster_node_map(llcc, 1) == -1 ||
	    parse_cluster_node_map(l3, 2) == -1)
		goto free_map;

	dflt_ev_ids[0] = 0x23;
	dflt_ev_data[0].a = 0x23;
	dflt_ev_data[0].b = 0xff;
	dflt_ev_data[0].c = 0xff;
	dflt_ev_data[0].e = 0xff;
	dflt_ev_ids[1] = 0x24;
	dflt_ev_data[1].a = 0x24;
	dflt_ev_data[1].b = 3;
	dflt_ev_data[1].c = 3;
	dflt_ev_data[1].e = 0xff;
	dflt_ev_ids[2] = 0x11;
	dflt_ev_data[2].a = 0x11;
	dflt_ev_data[2].b = 1;
	dflt_ev_data[2].c = 0;
	dflt_ev_data[2].e = 0xff;
	spm_thres = 200;
	spm_window_size = 10;
	spm_freq_map[0] = 1200;
	spm_freq_map[1] = 1708000;
	spm_freq_map[2] = 2100;
	spm_freq_map[3] = 2092000;
	cpu_list_map[0].cpu_num = 6;
	cpu_list_map[0].weight = 80;
	cpu_list_map[0].cpus[0] = 0;
	cpu_list_map[0].cpus[1] = 1;
	cpu_list_map[0].cpus[2] = 2;
	cpu_list_map[0].cpus[3] = 3;
	cpu_list_map[0].cpus[4] = 4;
	cpu_list_map[0].cpus[5] = 5;
	cpu_list_map[1].cpu_num = 2;
	cpu_list_map[1].weight = 60;
	cpu_list_map[1].cpus[0] = 6;
	cpu_list_map[1].cpus[1] = 7;
	cpu_list_map[2].cpu_num = 2;
	cpu_list_map[2].weight = 40;
	cpu_list_map[2].cpus[0] = 5;
	cpu_list_map[2].cpus[1] = 6;
	cpu_list_map[3].cpu_num = 1;
	cpu_list_map[3].weight = 40;
	cpu_list_map[3].cpus[0] = 7;

	dflt_wq = alloc_workqueue("dflt_wq", WQ_HIGHPRI, 0);
	if (!dflt_wq) {
		ret = -EFAULT;
		goto free_map;
	}
	INIT_WORK(&fast_vote_work, commit_fast_vote);
	pr_info("LINK: %s: inited!\n", __func__);
	spin_lock_init(&dflt_commit_lock);
	dflt_lat_vote_timer = NULL;

	for (i = 0; i < MIST_FRAME_HIST; i++) {
		struct frame_load *rec = &frame_stats_record[i];

		rec->freq[0] = 0;
		rec->freq[1] = 0;
		rec->freq[2] = 0;
		rec->ddr_freq = 0;
		rec->q2q = 0;
		for (j = 0; j < MIST_MAX_CPUS; j++)
			rec->ipc[j] = 0;
		for (j = 0; j < MIST_MAX_CPUS; j++)
			rec->miss[j] = 0;
		for (j = 0; j < MIST_MAX_CPUS; j++)
			rec->stall_ratio[j] = 0;
		for (j = 0; j < MIST_MAX_CPUS; j++)
			rec->instructions[j] = 0;
	}

	frame_load_dir = proc_mkdir("mist", NULL);
	history_load_entry = proc_create("frame_history_load", 0664, frame_load_dir,
					 &history_load_fops);
	/* ret is always 0 here: debug strings of the stock build that the optimiser drops */
	if (ret > 0) {
		pr_err(" dflt dbg1 %d\n", ret);
		pr_err(" dflt dbg2 %d\n", ret);
		pr_err(" dflt dbg3 %d\n", ret);
		pr_err(" dflt dbg4 %d\n", ret);
		pr_err(" dflt dbg5 %d\n", ret);
	}
	return 0;

free_map:
	for (i = 0; i < MIST_MAX_HW; i++)
		for (j = 0; j < MIST_MAX_CLUSTER; j++)
			if (mist_map[i][j])
				kfree(mist_map[i][j]);
free_stats:
	for_each_possible_cpu(cpu)
		if (per_cpu(sampling_stats, cpu))
			kfree(per_cpu(sampling_stats, cpu));
	return ret;
}
EXPORT_SYMBOL_GPL(mist_dflt_init);

static inline int mist_nr_clusters(void)
{
	int cpu, max_cid = 0;

	for_each_possible_cpu(cpu)
		if (topology_cluster_id(cpu) > max_cid)
			max_cid = topology_cluster_id(cpu);
	return max_cid + 1;
}

static enum hrtimer_restart queue_dflt_sample_start(struct hrtimer *timer)
{
	if (unlikely(dflt_debug))
		pr_err(" start...%s %llu", __func__, ktime_to_us(ktime_get()));
	if (dflt_bwmon_enable && dflt_bw_update_cb)
		dflt_bw_update_cb();
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart queue_dflt_vote_start(struct hrtimer *timer)
{
	unsigned int max_memfreq;
	int hw, i;

	if (unlikely(dflt_debug))
		pr_err(" start...%s %llu", __func__, ktime_to_us(ktime_get()));
	if (!dflt_lat_cal_cb) {
		pr_info(" %s dflt_lat_cal_cb is null \n", __func__);
		return HRTIMER_NORESTART;
	}
	dflt_lat_cal_cb();

	for (hw = 0; hw < MIST_MAX_HW; hw++) {
		max_memfreq = 0;
		for (i = 0; i < mist_nr_clusters(); i++)
			max_memfreq = max(max_memfreq,
					  cpufreq_to_memfreq(i, mist_next_freq[i], hw));
		if (last_vote_freq[hw] == max_memfreq) {
			cur_vote_freq[hw] = 0;
		} else {
			cur_vote_freq[hw] = max_memfreq;
			if (unlikely(dflt_debug))
				pr_err(" %s type: %d, max_memfreq: %d, cur_vote_freq: %d, last_vote_freq: %d!\n",
				       __func__, hw, max_memfreq, cur_vote_freq[hw],
				       last_vote_freq[hw]);
		}
	}

	if (!work_pending(&fast_vote_work))
		queue_work(dflt_wq, &fast_vote_work);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart queue_memlat_vote_start(struct hrtimer *timer)
{
	if (unlikely(dflt_debug))
		pr_err(" start...%s %llu", __func__, ktime_to_us(ktime_get()));
	if (READ_ONCE(dflt_latency_enable)) {
		if (dflt_latency_cur_vote_counter <= 1)
			dflt_latency_vote_start = true;
	}
	return HRTIMER_NORESTART;
}

static int parse_cluster_node_map(struct device_node *np, int hw_type)
{
	struct device_node *child = NULL;
	int cid = 0, cpus_total = 0;
	int i, num_entries;
	int j;
	unsigned long *cpus_slot;
	unsigned long cpus __uninitialized;

	while ((child = of_get_next_available_child(np, child))) {
		int len;
		u32 val;

		if (cid >= MIST_MAX_CLUSTER) {
			pr_err(" %s Invalid index: hw_type=%d, num_map=%d\n",
			       "parse_cluster_freq_map", hw_type, cid);
			continue;
		}
		if (!of_get_property(child, "qcom,cpulist", &len)) {
			pr_err("  Failed to get qcom,cpulist len %d\n", len);
			continue;
		}
		cpus = 0;
		for (i = 0; i < len / sizeof(u32); i++)
			set_bit(cpus_total + i, &cpus);
		cpus_total += len >> 2;
		cpus_slot = &mist_cpus[hw_type][cid];
		*cpus_slot = cpus;

		if (!of_find_property(child, "qcom,cpufreq-memfreq-tbl", &len))
			continue;
		len >>= 2;
		if (!len || (len & 1))
			continue;
		num_entries = len / 2;
		mist_map[hw_type][cid] = kzalloc((num_entries + 1) * sizeof(struct mist_map_entry), GFP_KERNEL);
		if (!mist_map[hw_type][cid])
			return -1;
		for (i = 0, j = 0; i < num_entries; i++) {
			if (of_property_read_u32_index(child, "qcom,cpufreq-memfreq-tbl",
						       j++, &val) < 0)
				return -1;
			mist_map[hw_type][cid][i].cpufreq_mhz = val;
			if (of_property_read_u32_index(child, "qcom,cpufreq-memfreq-tbl",
						       j++, &val) < 0)
				return -1;
			mist_map[hw_type][cid][i].memfreq_khz = val;
			if (unlikely(dflt_debug))
				pr_err("%s mist_map[%d][%d][%d].memfreq_khz: %d,mist_map[%d][%d][%d].cpufreq_mhz: %d\n",
				       "parse_cluster_freq_map", hw_type, cid, i,
				       mist_map[hw_type][cid][i].memfreq_khz,
				       hw_type, cid, i,
				       mist_map[hw_type][cid][i].cpufreq_mhz);
		}
		if (cpus_total > 7)
			return 1;
		cid++;
	}
	return 1;
}

static void commit_fast_vote(struct work_struct *work)
{
	struct dcvs_freq votes[2] = {};
	struct dcvs_freq l3 = {};
	unsigned long flags;
	u32 commit_mask = 0;
	int ret, hw;

	local_irq_save(flags);
	spin_lock(&dflt_commit_lock);
	for (hw = 0; hw < 2; hw++) {
		if (!cur_vote_freq[hw])
			continue;
		commit_mask |= 1 << hw;
		votes[hw].ib = cur_vote_freq[hw];
		votes[hw].ab = 0;
		votes[hw].hw_type = hw;
		if (unlikely(dflt_debug))
			pr_info("%s enter hw_type: %d, cur_vote_freq:%u, commit_mask %d",
				__func__, hw, cur_vote_freq[hw], commit_mask);
		if (unlikely(dflt_ftrace_debug))
			trace_printk("frame_id = %llu, hw_type: %d, cur_vote_freq = %u \n",
				     frame_index, hw, cur_vote_freq[hw]);
	}

	ret = qcom_dcvs_update_votes("memlat_fp", votes, commit_mask, DCVS_FAST_PATH);
	if (ret < 0) {
		spin_unlock(&dflt_commit_lock);
		local_irq_restore(flags);
		return;
	}
	if (unlikely(dflt_debug))
		pr_info("%s qcom_dcvs_update_votes ret %d", __func__, ret);
	if (!ret) {
		for (hw = 0; hw < 2; hw++)
			if (commit_mask & BIT(hw))
				last_vote_freq[hw] = cur_vote_freq[hw];
	}
	spin_unlock(&dflt_commit_lock);
	local_irq_restore(flags);

	if (cur_vote_freq[2]) {
		l3.ib = cur_vote_freq[2];
		l3.ab = 0;
		l3.hw_type = DCVS_L3;
		ret = qcom_dcvs_update_votes("soc:qcom,memlat:l3", &l3, 1, DCVS_SLOW_PATH);
		if (ret < 0) {
			pr_err("%s qcom dcvs  l3 vote err: %d\n", __func__, ret);
			return;
		}
		last_vote_freq[2] = cur_vote_freq[2];
		if (unlikely(dflt_ftrace_debug))
			trace_printk("frame_id = %llu, hw_type: %d, cur_vote_freq = %u \n",
				     frame_index, 2, cur_vote_freq[2]);
	}
}

static int set_dflt_bm_counter_enable(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	if (v)
		dflt_bwmon_counter = 0;
	dflt_bwmon_counter_enable = v;
	return 0;
}

static int get_dflt_bm_counter_enable(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d:%u \n", dflt_bwmon_counter_enable,
			dflt_bwmon_counter);
}

static const struct kernel_param_ops param_ops_dflt_bm_counter_enable = {
	.set = set_dflt_bm_counter_enable,
	.get = get_dflt_bm_counter_enable,
};
module_param_cb(dflt_bm_counter_enable, &param_ops_dflt_bm_counter_enable, NULL, 0644);

static int set_dflt_bwmon_enable(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	dflt_bwmon_enable = v;
	return 0;
}

static int get_dflt_bwmon_enable(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d \n", dflt_bwmon_enable);
}

static const struct kernel_param_ops param_ops_dflt_bwmon = {
	.set = set_dflt_bwmon_enable,
	.get = get_dflt_bwmon_enable,
};
module_param_cb(dflt_bw_enable, &param_ops_dflt_bwmon, NULL, 0644);

static int set_dflt_latency_enable(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	dflt_latency_enable_from_user = v;
	if (!v)
		frame_index = 0;
	return 0;
}

static int get_dflt_latency_enable(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d \n", dflt_latency_enable_from_user);
}

static const struct kernel_param_ops param_ops_dflt_latency = {
	.set = set_dflt_latency_enable,
	.get = get_dflt_latency_enable,
};
module_param_cb(dflt_lat_enable, &param_ops_dflt_latency, NULL, 0644);

static int set_soc_api_enable(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (kstrtouint(val, 0, &v))
		return -EINVAL;
	mist_soc_api_enable = v;
	return 0;
}

static int get_soc_api_enable(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d \n", READ_ONCE(mist_soc_api_enable));
}

static const struct kernel_param_ops param_soc_api_enable = {
	.set = set_soc_api_enable,
	.get = get_soc_api_enable,
};
module_param_cb(mist_soc_api_enable, &param_soc_api_enable, NULL, 0644);

static int dump_his_load_info(int idx, struct seq_file *m, void *v)
{
	struct frame_load *rec = &frame_stats_record[idx % MIST_FRAME_HIST];
	int cpu;

	seq_printf(m, "{q2q = %llu us\n", rec->q2q);
	seq_printf(m, "ddr_freq = %u \n", rec->ddr_freq);
	seq_printf(m, "[%c]: ", 'l');
	seq_printf(m, "%9llu  ", rec->freq[0]);
	seq_printf(m, "[%c]: ", 'm');
	seq_printf(m, "%9llu  ", rec->freq[1]);
	seq_printf(m, "[%c]: ", 'b');
	seq_printf(m, "%9llu  ", rec->freq[2]);
	seq_printf(m, "\n");
	for (cpu = 0; cpu < MIST_MAX_CPUS; cpu++) {
		seq_printf(m, "[cpu %d]: ", cpu);
		seq_printf(m, "ins %llu ", rec->instructions[cpu]);
		seq_printf(m, "ipc %llu ", rec->ipc[cpu]);
		seq_printf(m, "miss %llu ", rec->miss[cpu]);
		seq_printf(m, "stall_ratio %u.\n", rec->stall_ratio[cpu]);
	}
	seq_printf(m, "}\n\n");
	return 0;
}

static int history_load_show(struct seq_file *m, void *v)
{
	int cur = cur_record;
	int i;

	for (i = cur + 1; i <= MIST_FRAME_HIST - 1; i++)
		dump_his_load_info(i, m, v);
	for (i = 0; i <= cur; i++)
		dump_his_load_info(i, m, v);
	return 0;
}

static int history_load_open(struct inode *inode, struct file *file)
{
	return single_open(file, history_load_show, NULL);
}

static const struct proc_ops history_load_fops = {
	.proc_open	= history_load_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init mist_init(void)
{
	mist_dflt_init();
	mist_gflt_init();
	return 0;
}

static void __exit mist_exit(void)
{
	int cpu, i, j;

	for (i = 0; i < MIST_MAX_HW; i++)
		for (j = 0; j < MIST_MAX_CLUSTER; j++)
			if (mist_map[i][j])
				kfree(mist_map[i][j]);
	for_each_possible_cpu(cpu)
		if (per_cpu(sampling_stats, cpu))
			kfree(per_cpu(sampling_stats, cpu));
	if (frame_load_dir) {
		if (history_load_entry)
			proc_remove(history_load_entry);
		proc_remove(frame_load_dir);
	}
}

module_init(mist_init);
module_exit(mist_exit);
