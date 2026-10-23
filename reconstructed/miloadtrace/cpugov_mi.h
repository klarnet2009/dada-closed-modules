/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drivers/mihw/group_load/cpugov_mi.h -- "migov": a schedutil derived cpufreq governor
 * driven by the group (frame) load tracker.  Reconstructed from the stock module (perfC).
 */
#ifndef _CPUGOV_MI_H
#define _CPUGOV_MI_H

#include <linux/cpufreq.h>
#include <linux/percpu.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "walt_ext.h"

struct migov_tunables {
	struct gov_attr_set attr_set;
	unsigned int up_rate_limit_us;
	unsigned int down_rate_limit_us;
	unsigned int freq_shift;
	unsigned int hispeed_load;
	unsigned int hispeed_freq;
	unsigned int hispeed_cond_freq;
	unsigned int avg_load;
	unsigned int target_load_thresh;
	unsigned int adaptive_level_1;
	unsigned int adaptive_low_freq;
	unsigned int adaptive_high_freq;
	unsigned int adaptive_level_1_kernel;
	unsigned int adaptive_low_freq_kernel;
	unsigned int adaptive_high_freq_kernel;
};

struct migov_policy {
	struct cpufreq_policy *policy;
	struct migov_tunables *tunables;
	struct list_head tunables_hook;
	unsigned int avg_freq;
	u32 avg_cap;
	unsigned long max;
	raw_spinlock_t update_lock;
	s64 min_rate_limit_ns;
	s64 up_rate_delay_ns;
	s64 down_rate_delay_ns;
	unsigned long hispeed_util;
	unsigned long hispeed_cond_util;
	u64 last_freq_update_time;
	unsigned int next_freq;
	unsigned int cached_raw_freq;
	unsigned int driving_cpu;
	bool limits_changed;
	bool need_freq_update;
};

struct migov_cpu {
	struct migov_callback update_util;
	struct migov_policy *migov_policy;
	unsigned long util;
	unsigned long max;
	unsigned int cpu;
	unsigned int flags;
	unsigned int reason;
};

/* group_load/frame_weight.c */
u64 get_group_util(struct cpufreq_policy *policy, unsigned int *avg_freq, unsigned int flags,
		   u64 *target_delta, u64 *real_delta, bool *fore_timeout, bool *group_util_valid);

int migtgov_register(void);
void migtgov_unregister(void);

/* sched-walt / mi_schedule providers */
extern struct cpumask __cpu_partial_halt_mask;
extern struct cpumask __cpu_halt_mask;
extern struct cpumask part_haltable_cpus;
extern unsigned int perf_min_freq[2];

#endif /* _CPUGOV_MI_H */
