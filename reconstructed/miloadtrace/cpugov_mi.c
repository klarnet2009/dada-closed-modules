// SPDX-License-Identifier: GPL-2.0
/* migov: schedutil derived cpufreq governor of the group (frame) load tracker */
#line 1 "drivers/mihw/group_load/cpugov_mi.c"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/rcupdate.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/arch_topology.h>

#include "cpugov_mi.h"

extern int get_cluster_type(unsigned int cpu);

/* ThinLTO promoted statics of the stock object (hash cannot be reproduced) */
extern struct cpufreq_governor mi_gov __asm__("mi_gov.llvm.3124179784938975774")
	__attribute__((visibility("hidden")));
unsigned int cal_cpufreq_policy __asm__("cal_cpufreq_policy.llvm.3124179784938975774")
	__attribute__((visibility("hidden")));
const char migov_register_msg[] __asm__(".str.llvm.3124179784938975774")
	__attribute__((visibility("hidden"))) = KERN_INFO "MI-GOV: migt gov register!";
const char migov_unregister_msg[] __asm__(".str.1.llvm.3124179784938975774")
	__attribute__((visibility("hidden"))) = KERN_INFO "MI-GOV: migt gov unregister!";

static unsigned int migov_debug;
static unsigned int in_perf_mod;
static unsigned int freq_mode;
static unsigned int out_capacity_thresh;
static unsigned int big_maxfreq_limit;
unsigned int mi_maxfreq[2];
unsigned int mi_minfreq[2];

module_param_array(mi_maxfreq, uint, NULL, 0644);
module_param_array(mi_minfreq, uint, NULL, 0644);
module_param(cal_cpufreq_policy, uint, 0644);
module_param(migov_debug, uint, 0644);
module_param(in_perf_mod, uint, 0644);
module_param(freq_mode, uint, 0644);
module_param(out_capacity_thresh, uint, 0644);
module_param(big_maxfreq_limit, uint, 0644);

static DEFINE_PER_CPU(struct migov_tunables *, cached_tunables);
static DEFINE_PER_CPU(struct migov_cpu, mi_gov_cpu);
DEFINE_PER_CPU(struct migov_callback *, migov_cb_data);
static DEFINE_MUTEX(min_rate_lock);

static void mi_gov_update_freq(struct migov_callback *hook, u64 time, int flags);

/************************** sysfs interface ************************/

static inline struct migov_tunables *to_migov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct migov_tunables, attr_set);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set, const char *buf,
				      size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);
	struct migov_policy *migov_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(migov_policy, &attr_set->policy_list, tunables_hook) {
		migov_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		mutex_lock(&min_rate_lock);
		migov_policy->min_rate_limit_ns = migov_policy->up_rate_delay_ns < migov_policy->down_rate_delay_ns ?
			migov_policy->up_rate_delay_ns : migov_policy->down_rate_delay_ns;
		mutex_unlock(&min_rate_lock);
	}

	return count;
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);
	struct migov_policy *migov_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(migov_policy, &attr_set->policy_list, tunables_hook) {
		migov_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		mutex_lock(&min_rate_lock);
		migov_policy->min_rate_limit_ns = migov_policy->up_rate_delay_ns < migov_policy->down_rate_delay_ns ?
			migov_policy->up_rate_delay_ns : migov_policy->down_rate_delay_ns;
		mutex_unlock(&min_rate_lock);
	}

	return count;
}

static ssize_t hispeed_load_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_load);
}

static ssize_t hispeed_load_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->hispeed_load))
		return -EINVAL;

	tunables->hispeed_load = min(100U, tunables->hispeed_load);

	return count;
}

static ssize_t hispeed_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_freq);
}

static ssize_t hispeed_freq_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);
	struct migov_policy *migov_policy;
	unsigned long hs_util;
	unsigned long flags;
	unsigned int val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	tunables->hispeed_freq = val;
	list_for_each_entry(migov_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&migov_policy->update_lock, flags);
		hs_util = mult_frac(migov_policy->max, migov_policy->tunables->hispeed_freq,
				    migov_policy->policy->cpuinfo.max_freq);
		if (get_cluster_type(migov_policy->policy->cpu) == 0 &&
		    hs_util >= migov_policy->tunables->target_load_thresh)
			hs_util = mult_frac(hs_util, 94, 100);
		else
			hs_util = mult_frac(hs_util, 80, 100);
		migov_policy->hispeed_util = hs_util;
		raw_spin_unlock_irqrestore(&migov_policy->update_lock, flags);
	}

	return count;
}

static ssize_t hispeed_cond_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_cond_freq);
}

static ssize_t hispeed_cond_freq_store(struct gov_attr_set *attr_set, const char *buf,
				       size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);
	struct migov_policy *migov_policy;
	unsigned long flags;

	if (kstrtouint(buf, 10, &tunables->hispeed_cond_freq))
		return -EINVAL;

	list_for_each_entry(migov_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&migov_policy->update_lock, flags);
		migov_policy->hispeed_cond_util = mult_frac(migov_policy->max,
							    tunables->hispeed_cond_freq,
							    migov_policy->policy->cpuinfo.max_freq);
		raw_spin_unlock_irqrestore(&migov_policy->update_lock, flags);
	}

	return count;
}

static ssize_t show_target_load_thresh(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->target_load_thresh);
}

static ssize_t store_target_load_thresh(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->target_load_thresh))
		return -EINVAL;

	return count;
}

static ssize_t show_adaptive_level_1(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->adaptive_level_1);
}

static ssize_t store_adaptive_level_1(struct gov_attr_set *attr_set, const char *buf,
				      size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->adaptive_level_1))
		return -EINVAL;

	return count;
}

static ssize_t show_adaptive_low_freq(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->adaptive_low_freq);
}

static ssize_t store_adaptive_low_freq(struct gov_attr_set *attr_set, const char *buf,
				       size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->adaptive_low_freq))
		return -EINVAL;

	return count;
}

static ssize_t show_adaptive_high_freq(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->adaptive_high_freq);
}

static ssize_t store_adaptive_high_freq(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->adaptive_high_freq))
		return -EINVAL;

	return count;
}

static ssize_t show_freq_shift(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->freq_shift);
}

static ssize_t store_freq_shift(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->freq_shift))
		return -EINVAL;

	return count;
}

static ssize_t avg_load_show(struct gov_attr_set *attr_set, char *buf)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->avg_load);
}

static ssize_t avg_load_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct migov_tunables *tunables = to_migov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->avg_load))
		return -EINVAL;

	tunables->avg_load = min(100U, tunables->avg_load);

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);
static struct governor_attr hispeed_load = __ATTR_RW(hispeed_load);
static struct governor_attr hispeed_freq = __ATTR_RW(hispeed_freq);
static struct governor_attr hispeed_cond_freq = __ATTR_RW(hispeed_cond_freq);
static struct governor_attr target_load_thresh =
	__ATTR(target_load_thresh, 0644, show_target_load_thresh, store_target_load_thresh);
static struct governor_attr adaptive_level_1 =
	__ATTR(adaptive_level_1, 0644, show_adaptive_level_1, store_adaptive_level_1);
static struct governor_attr adaptive_low_freq =
	__ATTR(adaptive_low_freq, 0644, show_adaptive_low_freq, store_adaptive_low_freq);
static struct governor_attr adaptive_high_freq =
	__ATTR(adaptive_high_freq, 0644, show_adaptive_high_freq, store_adaptive_high_freq);
static struct governor_attr freq_shift =
	__ATTR(freq_shift, 0644, show_freq_shift, store_freq_shift);
static struct governor_attr avg_load = __ATTR_RW(avg_load);

static struct attribute *migov_attrs[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&hispeed_load.attr,
	&hispeed_freq.attr,
	&hispeed_cond_freq.attr,
	&target_load_thresh.attr,
	&adaptive_level_1.attr,
	&adaptive_low_freq.attr,
	&adaptive_high_freq.attr,
	&freq_shift.attr,
	&avg_load.attr,
	NULL
};
ATTRIBUTE_GROUPS(migov);

static struct kobj_type mi_gov_tunables_ktype = {
	.default_groups = migov_groups,
	.sysfs_ops = &governor_sysfs_ops,
};

/********************** cpufreq governor interface *********************/

void migov_run_callback(struct rq *rq, int flags) __asm__("migov_run_callback.llvm.3124179784938975774")
	__attribute__((visibility("hidden")));
void migov_run_callback(struct rq *rq, int flags)
{
	struct migov_callback *data;
	unsigned int cpu = rq->cpu;

	data = rcu_dereference_sched(per_cpu(migov_cb_data, cpu));
	if (data)
		data->func(data, walt_sched_clock(), flags);
}

static struct migov_policy *migov_policy_alloc(struct cpufreq_policy *policy)
{
	struct migov_policy *migov_policy;

	migov_policy = kzalloc(sizeof(*migov_policy), GFP_KERNEL);
	if (!migov_policy)
		return NULL;

	migov_policy->policy = policy;
	raw_spin_lock_init(&migov_policy->update_lock);
	return migov_policy;
}

static int cpu_gov_init(struct cpufreq_policy *policy)
{
	struct migov_policy *migov_policy;
	struct migov_tunables *tunables;
	struct migov_tunables *cached;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	tunables = NULL;

	cpufreq_enable_fast_switch(policy);
	if (policy->fast_switch_possible && !policy->fast_switch_enabled)
		BUG_ON(1);

	migov_policy = migov_policy_alloc(policy);
	if (!migov_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (!tunables) {
		ret = -ENOMEM;
		goto free_migov_policy;
	}
	gov_attr_set_init(&tunables->attr_set, &migov_policy->tunables_hook);

	policy->governor_data = migov_policy;
	migov_policy->tunables = tunables;

	tunables->freq_shift = 2;
	tunables->hispeed_load = 90;
	tunables->avg_load = 100;
	tunables->target_load_thresh = 1024;

	cached = per_cpu(cached_tunables, policy->cpu);
	if (cached) {
		tunables->hispeed_load = cached->hispeed_load;
		tunables->hispeed_freq = cached->hispeed_freq;
		tunables->hispeed_cond_freq = cached->hispeed_cond_freq;
		tunables->up_rate_limit_us = cached->up_rate_limit_us;
		tunables->down_rate_limit_us = cached->down_rate_limit_us;
		tunables->adaptive_level_1 = cached->adaptive_level_1;
		tunables->adaptive_low_freq = cached->adaptive_low_freq;
		tunables->adaptive_high_freq = cached->adaptive_high_freq;
		tunables->adaptive_level_1_kernel = cached->adaptive_level_1_kernel;
		tunables->adaptive_low_freq_kernel = cached->adaptive_low_freq_kernel;
		tunables->adaptive_high_freq_kernel = cached->adaptive_high_freq_kernel;
		tunables->target_load_thresh = cached->target_load_thresh;
	}

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &mi_gov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s", mi_gov.name);
	if (ret)
		goto fail;

	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	kfree(&tunables);

free_migov_policy:
	kfree(migov_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("MI-GOV: initialization failed (error %d)\n", ret);
	return ret;
}

static void cpu_gov_exit(struct cpufreq_policy *policy)
{
	struct migov_policy *migov_policy = policy->governor_data;
	struct migov_tunables *tunables = migov_policy->tunables;
	unsigned int count;

	count = gov_attr_set_put(&tunables->attr_set, &migov_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count) {
		struct migov_tunables *cached;
		int cpu;

		cached = per_cpu(cached_tunables, policy->cpu);
		if (!cached) {
			cached = kzalloc(sizeof(*tunables), GFP_KERNEL);
			if (cached) {
				for_each_cpu(cpu, policy->related_cpus)
					per_cpu(cached_tunables, cpu) = cached;
			}
		}
		if (cached) {
			cached->hispeed_load = tunables->hispeed_load;
			cached->hispeed_freq = tunables->hispeed_freq;
			cached->hispeed_cond_freq = tunables->hispeed_cond_freq;
			cached->up_rate_limit_us = tunables->up_rate_limit_us;
			cached->down_rate_limit_us = tunables->down_rate_limit_us;
			cached->adaptive_level_1 = tunables->adaptive_level_1;
			cached->adaptive_low_freq = tunables->adaptive_low_freq;
			cached->adaptive_high_freq = tunables->adaptive_high_freq;
			cached->adaptive_level_1_kernel = tunables->adaptive_level_1_kernel;
			cached->adaptive_low_freq_kernel = tunables->adaptive_low_freq_kernel;
			cached->adaptive_high_freq_kernel = tunables->adaptive_high_freq_kernel;
			cached->target_load_thresh = tunables->target_load_thresh;
		}
		kfree(tunables);
	}

	kfree(migov_policy);
	cpufreq_disable_fast_switch(policy);
}

static void migov_add_hook(int cpu, struct migov_callback *data,
			   void (*func)(struct migov_callback *data, u64 time, int flags))
{
	if (WARN_ON(!data || !func))
		return;

	if (WARN_ON(per_cpu(migov_cb_data, cpu)))
		return;

	data->func = func;
	rcu_assign_pointer(per_cpu(migov_cb_data, cpu), data);
}

static int cpu_gov_start(struct cpufreq_policy *policy)
{
	struct migov_policy *migov_policy = policy->governor_data;
	unsigned int cpu;

	skip_walt_result[get_cluster_type(policy->cpu)] = 1;

	mutex_lock(&min_rate_lock);
	migov_policy->min_rate_limit_ns = migov_policy->up_rate_delay_ns < migov_policy->down_rate_delay_ns ?
		migov_policy->up_rate_delay_ns : migov_policy->down_rate_delay_ns;
	mutex_unlock(&min_rate_lock);

	migov_policy->last_freq_update_time = 0;
	migov_policy->next_freq = 0;
	migov_policy->cached_raw_freq = 0;
	migov_policy->limits_changed = false;
	migov_policy->need_freq_update = false;

	for_each_cpu(cpu, policy->cpus) {
		struct migov_cpu *mi_cpu = &per_cpu(mi_gov_cpu, cpu);

		memset(mi_cpu, 0, sizeof(*mi_cpu));
		mi_cpu->cpu = cpu;
		mi_cpu->migov_policy = migov_policy;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct migov_cpu *mi_cpu = &per_cpu(mi_gov_cpu, cpu);

		migov_add_hook(cpu, &mi_cpu->update_util, mi_gov_update_freq);
	}
	return 0;
}

static void cpu_gov_stop(struct cpufreq_policy *policy)
{
	unsigned int cpu;

	skip_walt_result[get_cluster_type(policy->cpu)] = 0;

	for_each_cpu(cpu, policy->cpus)
		rcu_assign_pointer(per_cpu(migov_cb_data, cpu), NULL);

	synchronize_rcu();
}

static void cpu_gov_limits(struct cpufreq_policy *policy)
{
	struct migov_policy *migov_policy = policy->governor_data;
	unsigned long flags;
	unsigned int freq;
	u64 now;

	if (!policy->fast_switch_enabled)
		return;

	raw_spin_lock_irqsave(&migov_policy->update_lock, flags);
	freq = policy->cur;
	now = walt_sched_clock();
	freq = cpufreq_driver_resolve_freq(policy, freq);
	if (migov_policy->next_freq != freq) {
		migov_policy->next_freq = freq;
		migov_policy->cached_raw_freq = freq;
		migov_policy->last_freq_update_time = now;
		cpufreq_driver_fast_switch(migov_policy->policy, freq);
	}
	raw_spin_unlock_irqrestore(&migov_policy->update_lock, flags);

	migov_policy->limits_changed = true;
}

static void mi_gov_update_freq(struct migov_callback *hook, u64 time, int flags)
{
	struct migov_cpu *mi_cpu = container_of(hook, struct migov_cpu, update_util);
	struct migov_policy *migov_policy = mi_cpu->migov_policy;
	u64 now = walt_sched_clock();
	unsigned long max;
	bool should_update;
	s64 delta_ns;

	raw_spin_lock(&migov_policy->update_lock);

	if (unlikely(migov_policy->limits_changed)) {
		migov_policy->limits_changed = false;
		migov_policy->need_freq_update = true;
		should_update = true;
	} else {
		delta_ns = now - migov_policy->last_freq_update_time;
		should_update = delta_ns >= migov_policy->min_rate_limit_ns;
	}
	if ((flags & 2) || !should_update)
		goto out;

	{
		unsigned int max_limit, min_limit, cid;
		unsigned int util, freq, next_f;
		unsigned int hispeed_thresh, cflags;
		bool hispeed;

		max = per_cpu(cpu_scale, mi_cpu->cpu);
		struct cpufreq_policy *policy = migov_policy->policy;
		u64 target_delta = 0, real_delta = 0;
		bool fore_timeout = false, group_util_valid = true;

		mi_cpu->max = (unsigned int)max;
		mi_cpu->flags = flags;
		util = get_group_util(policy, &migov_policy->avg_freq, flags, &target_delta, &real_delta,
				      &fore_timeout, &group_util_valid);
		if (!group_util_valid)
			goto out;

		if (migov_policy->max != mi_cpu->max) {
			unsigned long pct = 80;
			unsigned long hu;

			migov_policy->max = mi_cpu->max;
			hu = mult_frac(migov_policy->max, migov_policy->tunables->hispeed_freq,
				       migov_policy->policy->cpuinfo.max_freq);
			if (get_cluster_type(migov_policy->policy->cpu) == 0 &&
			    unlikely(hu >= migov_policy->tunables->target_load_thresh))
				pct = 94;
			migov_policy->hispeed_util = mult_frac(hu, pct, 100);
		}

		cflags = mi_cpu->flags;
		migov_policy->avg_cap = mult_frac(migov_policy->max, migov_policy->avg_freq,
						  migov_policy->policy->cpuinfo.max_freq);
		hispeed_thresh = mult_frac(migov_policy->avg_cap, migov_policy->tunables->hispeed_load, 100);
		hispeed = hispeed_thresh <= util;
		if (cpumask_test_cpu(mi_cpu->cpu, &__cpu_partial_halt_mask)) {
			if (cpumask_weight(&part_haltable_cpus)) {
				struct cpumask tmp, mask;

				cpumask_or(&mask, &__cpu_halt_mask, &__cpu_partial_halt_mask);
				cpumask_andnot(&tmp, &part_haltable_cpus, &mask);
				hispeed = !cpumask_empty(&tmp) && hispeed_thresh <= util;
			}
		}

		if (migov_policy->avg_cap >= migov_policy->hispeed_cond_util && hispeed &&
		    !(cflags & 4) && migov_policy->hispeed_util && util <= migov_policy->hispeed_util) {
			util = migov_policy->hispeed_util;
			mi_cpu->reason = 0x10;
		}

		if (util <= migov_policy->avg_cap && migov_policy->avg_cap &&
		    util >= mult_frac(migov_policy->avg_cap, migov_policy->tunables->avg_load, 100)) {
			util = migov_policy->avg_cap;
			mi_cpu->reason = 0x20;
		}

		if (migov_debug)
			printk(KERN_INFO "MI-GOV: avg_cap%d hispeed_util%lu cur_util%d cpu-max%u policy-max%lu",
			       migov_policy->avg_cap, migov_policy->hispeed_util, util, (unsigned int)max,
			       migov_policy->max);

		cid = get_cluster_type(migov_policy->policy->cpu);
		if (cal_cpufreq_policy)
			freq = (migov_policy->policy->cpuinfo.max_freq +
				(migov_policy->tunables->freq_shift ?
				 migov_policy->policy->cpuinfo.max_freq >> migov_policy->tunables->freq_shift : 0)) *
			       (u64)util / (unsigned int)max;
		else {
			unsigned int f = DIV_ROUND_UP((u64)(migov_policy->policy->cpuinfo.max_freq * util) +
						      (unsigned int)max - 1, (unsigned int)max);

			f = DIV_ROUND_UP((u64)f << 10, target_delta);
			freq = f + (migov_policy->tunables->freq_shift ? f >> migov_policy->tunables->freq_shift : 0);
		}

		if (big_maxfreq_limit) {
			unsigned long cap = cpu_rq(migov_policy->policy->cpu)->cpu_capacity;

			if (util > cap * (out_capacity_thresh + 100) / 100) {
				if (migov_debug)
					printk(KERN_INFO "MI-GOV: out_capacity-freq%d soft-max%d cur_capacity%lu util%u\n",
					       freq, migov_policy->policy->max, cap, util);
			}
		}

		max_limit = mi_maxfreq[cid] ? mi_maxfreq[cid] : migov_policy->policy->max;
		min_limit = mi_minfreq[cid] ? mi_minfreq[cid] : migov_policy->policy->min;
		if (in_perf_mod)
			min_limit = max(min_limit, perf_min_freq[cid]);
		if (real_delta > target_delta * 2 && in_perf_mod && freq > max_limit)
			max_limit = migov_policy->policy->max;

		freq = min(max_limit, max(freq, min_limit));
		if (migov_debug)
			printk(KERN_INFO "MI-GOV: %d-freq%d soft-max%d soft-min%d", migov_policy->policy->cpu, freq,
			       max_limit, min_limit);

		if (!fore_timeout && freq_mode == 1)
			freq = min(freq, migov_policy->avg_freq);

		if (migov_policy->tunables->adaptive_high_freq) {
			unsigned int reason_bit = 0;
			unsigned int lvl1 = max(migov_policy->tunables->adaptive_level_1,
						migov_policy->tunables->adaptive_level_1_kernel);
			unsigned int low = max(migov_policy->tunables->adaptive_low_freq,
					       migov_policy->tunables->adaptive_low_freq_kernel);
			unsigned int high = max(migov_policy->tunables->adaptive_high_freq,
						migov_policy->tunables->adaptive_high_freq_kernel);

			if (freq < lvl1) {
				freq = lvl1;
				reason_bit = 0x20000;
			} else if (freq < low) {
				freq = low;
				reason_bit = 0x400;
			} else if (freq <= high) {
				freq = high;
				reason_bit = 0x800;
			}
			if (reason_bit)
				mi_cpu->reason |= reason_bit;
		}

		if (migov_debug)
			printk(KERN_INFO "MI-GOV: %d-freq%d", policy->cpu, freq);

		if (migov_policy->cached_raw_freq && freq == migov_policy->cached_raw_freq &&
		    !migov_policy->need_freq_update)
			goto out;

		migov_policy->need_freq_update = false;
		next_f = cpufreq_driver_resolve_freq(policy, freq);
		if (migov_policy->next_freq == next_f)
			goto out;

		delta_ns = now - migov_policy->last_freq_update_time;
		if (migov_policy->next_freq < next_f && delta_ns < migov_policy->up_rate_delay_ns) {
			migov_policy->cached_raw_freq = 0;
			goto out;
		}
		if (migov_policy->next_freq > next_f && delta_ns < migov_policy->down_rate_delay_ns) {
			migov_policy->cached_raw_freq = 0;
			goto out;
		}

		migov_policy->next_freq = next_f;
		migov_policy->cached_raw_freq = freq;
		migov_policy->last_freq_update_time = now;
		if (next_f && migov_policy->policy->fast_switch_enabled)
			cpufreq_driver_fast_switch(migov_policy->policy, next_f);
	}
out:
	raw_spin_unlock(&migov_policy->update_lock);
}

struct cpufreq_governor mi_gov = {
	.name = "migov",
	.owner = THIS_MODULE,
	.init = cpu_gov_init,
	.exit = cpu_gov_exit,
	.start = cpu_gov_start,
	.stop = cpu_gov_stop,
	.limits = cpu_gov_limits,
};

int migtgov_register(void)
{
	int ret;

	ret = cpufreq_register_governor(&mi_gov);
	cal_cpufreq_policy = 1;
	register_mi_run_frequpdate_hooks(migov_run_callback);
	printk(migov_register_msg);
	return ret;
}

void migtgov_unregister(void)
{
	unregister_mi_run_frequpdate_hooks();
	cpufreq_unregister_governor(&mi_gov);
	printk(migov_unregister_msg);
}
