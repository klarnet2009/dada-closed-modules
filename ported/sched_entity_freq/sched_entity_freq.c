// SPDX-License-Identifier: GPL-2.0
/*
 * sched_entity_freq: Sched Entity Frequency Control (Xiaomi).
 * Reconstructed from the HyperOS 4.0 stock module (no public source).
 *
 * STATE (G1h): the whole module is reconstructed from the disassembly (sysfs interface,
 * config, exports, cluster/cpu state, tick hook, work handler, DDR voter, init/exit).
 * 33 of 36 functions are byte-identical to the stock; sched_tick_hook,
 * cluster_freq_work_handler and init_module differ in register allocation /
 * scheduling only. Not tested on hardware: do not load before review.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/kernfs.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/arch_topology.h>
#include <linux/bitops.h>
#include <linux/ktime.h>
#include <trace/hooks/sched.h>
#define CONFIG_QCOM_DCVS 1
#include <soc/qcom/dcvs.h>

#define SEF "sched_entity_freq: "
#define MAX_CLUSTERS	8
#define MAX_CPUS	32

struct sef_config {
	u32 idle_threshold;		/* 0x00 */
	u32 active_threshold;		/* 0x04 */
	u32 cluster_idle_threshold;	/* 0x08 */
	u32 cluster_active_threshold;	/* 0x0c */
	u32 min_freq;			/* 0x10 */
	u32 mid_freq;			/* 0x14 */
	u32 normal_freq;		/* 0x18 */
	u32 fast_switch_delay;		/* 0x1c */
	bool ddr_freq_sync;		/* 0x20 */
	u32 ddr_min_freq;		/* 0x24 */
	bool enabled;			/* 0x28 */
};

enum cpu_power_state {
	CPU_STATE_ACTIVE = 0,
	CPU_STATE_POWER_SAVE = 1,
};

struct cpu_state {			/* 40 bytes */
	u32 cpu;			/* +0x00 */
	u32 cluster_id;			/* +0x04 */
	u32 runq_count;			/* +0x08 */
	bool flag0c;			/* +0x0c */
	bool skip;			/* +0x0d: CPUs of tiny clusters are not tracked */
	u64 last_jiffies;		/* +0x10 */
	u64 low_since;			/* +0x18 */
	enum cpu_power_state state;	/* +0x20 */
	u32 unchanged_ticks;		/* +0x24 */
};

struct cluster_state {			/* 128 bytes */
	u32 cluster_id;			/* +0x00 */
	unsigned long cpu_mask;		/* +0x08 */
	u32 active_cpus;		/* +0x10 */
	bool power_save;		/* +0x14 */
	u32 min_freq;			/* +0x18 */
	u32 max_freq;			/* +0x1c */
	u32 cur_freq;			/* +0x20 */
	bool freq_pending;		/* +0x24 */
	u32 target_freq;		/* +0x28 */
	u32 num_cpus;			/* +0x2c */
	u32 mid_cpus;			/* +0x30 */
	struct freq_qos_request qos_req; /* +0x38 */
	bool qos_init;			/* +0x78 */
	u32 saved_max;			/* +0x7c */
};

static DEFINE_SPINLOCK(state_lock);
static struct cpu_state cpu_states[MAX_CPUS];
static u32 num_clusters;
static struct cluster_state cluster_states[MAX_CLUSTERS];
static struct workqueue_struct *freq_workqueue;
static atomic_t module_ref_count;
static struct kobject *dcvs_arbi_kobj;
static struct kobject *sched_entity_freq_kobj;

struct sef_config config = {
	.idle_threshold = 1,
	.active_threshold = 2,
	.min_freq = 600000,
	.mid_freq = 1017600,
	.normal_freq = 2500000,
	.fast_switch_delay = 5,
	.ddr_freq_sync = true,
	.ddr_min_freq = 100000,
};
DEFINE_MUTEX(config_mutex);
bool cluster_idle_threshold_override;
bool cluster_active_threshold_override;

/* the scheduler's struct rq is private: only nr_running (+4) and cpu (+0xbb8) are used */
struct sef_rq {
	u32 pad;
	unsigned int nr_running;
	char pad2[0xbb8 - 8];
	int cpu;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct sef_rq, runqueues);

struct ddr_voter_s {
	bool registered;		/* +0x00 */
	char name[35];			/* +0x01 */
	struct dcvs_freq low;		/* +0x24: min vote (all clusters in power save) */
	struct dcvs_freq high;		/* +0x30: max vote */
	struct dcvs_freq cur;		/* +0x3c: vote sent last */
	bool power_save;		/* +0x48 */
};
static struct ddr_voter_s ddr_voter;
static struct work_struct cluster_freq_work;

static void read_runq_on_cpu(void *info)
{
	u32 *counts = info;
	int cpu = smp_processor_id();

	counts[cpu] = this_cpu_ptr(&runqueues)->nr_running;
}

static noinline void cleanup_ddr_voter(void)
{
	if (!ddr_voter.registered)
		return;
	qcom_dcvs_unregister_voter(ddr_voter.name, 0, 1);
	ddr_voter.registered = false;
	pr_info(SEF "DDR voter unregistered\n");
}

static void init_active_cpus_from_runqueues(void)
{
	static int runq_counts[MAX_CPUS];
	unsigned long flags;
	int cpu;

	on_each_cpu(read_runq_on_cpu, runq_counts, 1);
	spin_lock_irqsave(&state_lock, flags);
	for_each_possible_cpu(cpu) {
		struct cpu_state *cs = &cpu_states[cpu];

		if (cs->cluster_id >= num_clusters)
			continue;
		cs->runq_count = runq_counts[cpu];
		if (runq_counts[cpu] > 0) {
			cs->state = CPU_STATE_ACTIVE;
			cluster_states[cs->cluster_id].active_cpus++;
		} else {
			cs->state = CPU_STATE_POWER_SAVE;
		}
	}
	spin_unlock_irqrestore(&state_lock, flags);
}

static void cpu_set_power_state_locked(int cpu, enum cpu_power_state state)
{
	struct cpu_state *cs = &cpu_states[cpu];
	struct cluster_state *cl = &cluster_states[cs->cluster_id];

	if (cs->state == state)
		return;
	cs->unchanged_ticks = 0;
	if (state == CPU_STATE_POWER_SAVE) {
		if (cl->active_cpus)
			cl->active_cpus--;
	} else {
		cl->active_cpus++;
	}
	cs->state = state;
}

static void fast_switch_cluster_freq(int cluster_id, unsigned int freq_khz)
{
	struct cpufreq_policy *policy;
	int cpu;

	for_each_set_bit(cpu, &cluster_states[cluster_id].cpu_mask, 32) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (policy->fast_switch_possible) {
			preempt_disable();
			cpufreq_driver_fast_switch(policy, freq_khz);
			preempt_enable();
		}
		cpufreq_cpu_put(policy);
		return;
	}
}

static void cluster_freq_work_handler(struct work_struct *work)
{
	unsigned long flags;
	bool all_ps;
	u32 i;

	for (i = 0; i < num_clusters; i++) {
		struct cluster_state *cl = &cluster_states[i];
		struct cpufreq_policy *policy;
		u32 min_f, mid_f, target, freq, max, saved;
		bool pending;
		int ret;

		spin_lock_irqsave(&state_lock, flags);
		min_f = config.min_freq;
		mid_f = config.mid_freq;
		pending = cl->freq_pending;
		target = cl->target_freq;
		cl->freq_pending = false;
		spin_unlock_irqrestore(&state_lock, flags);
		if (!pending)
			continue;
		policy = cpufreq_cpu_get(find_first_bit(&cl->cpu_mask, 32));
		if (target == min_f || target == mid_f) {
			if (policy) {
				max = policy->max;
				cpufreq_cpu_put(policy);
				if (max) {
					spin_lock_irqsave(&state_lock, flags);
					cl->saved_max = max;
					spin_unlock_irqrestore(&state_lock, flags);
				}
			}
			freq = target;
		} else {
			max = 0;
			if (policy) {
				max = policy->max;
				cpufreq_cpu_put(policy);
			}
			spin_lock_irqsave(&state_lock, flags);
			if (max == config.min_freq && (saved = cl->saved_max)) {
				cl->saved_max = 0;
				spin_unlock_irqrestore(&state_lock, flags);
				freq = saved;
			} else {
				cl->saved_max = 0;
				spin_unlock_irqrestore(&state_lock, flags);
				if (!cl->qos_init) {
					pr_warn(SEF "Cluster %u QoS not initialized\n", i);
					continue;
				}
				ret = freq_qos_update_request(&cl->qos_req, INT_MAX);
				if (ret < 0) {
					pr_err(SEF "Failed to update freq_qos for cluster %u: %d\n", i, ret);
					continue;
				}
				cl->cur_freq = INT_MAX;
				continue;
			}
		}
		if (!cl->qos_init) {
			pr_warn(SEF "Cluster %u QoS not initialized\n", i);
			continue;
		}
		ret = freq_qos_update_request(&cl->qos_req, freq);
		if (ret < 0) {
			pr_err(SEF "Failed to update freq_qos for cluster %u: %d\n", i, ret);
			continue;
		}
		cl->cur_freq = freq;
	}

	spin_lock_irqsave(&state_lock, flags);
	all_ps = true;
	for (i = 0; i < num_clusters; i++) {
		if (!cluster_states[i].power_save) {
			all_ps = false;
			break;
		}
	}
	if (ddr_voter.power_save == all_ps) {
		spin_unlock_irqrestore(&state_lock, flags);
		return;
	}
	ddr_voter.power_save = all_ps;
	spin_unlock_irqrestore(&state_lock, flags);
	if (config.ddr_freq_sync && ddr_voter.registered) {
		if (all_ps)
			ddr_voter.cur = ddr_voter.low;
		else
			ddr_voter.cur = ddr_voter.high;
		qcom_dcvs_update_votes(ddr_voter.name, &ddr_voter.cur, 1, DCVS_FAST_PATH);
	}
}

static void sched_tick_hook(void *data, struct rq *rq)
{
	struct sef_rq *r = (struct sef_rq *)rq;
	int cpu = r->cpu;
	struct cluster_state *cl;
	unsigned long flags;
	u32 nr, cluster, thr, freq, cid, active;

	if (!atomic_read(&module_ref_count))
		return;
	if (cpu >= MAX_CPUS)
		return;
	if (!READ_ONCE(config.enabled))
		return;
	if (cpu_states[cpu].skip)
		return;
	nr = r->nr_running;
	if (READ_ONCE(cpu_states[cpu].runq_count) == nr) {
		if (!READ_ONCE(cpu_states[cpu].low_since)) {
			if (cpu_states[cpu].unchanged_ticks < 255)
				cpu_states[cpu].unchanged_ticks++;
			return;
		}
	}
	cpu_states[cpu].unchanged_ticks = 0;
	spin_lock_irqsave(&state_lock, flags);
	cluster = cpu_states[cpu].cluster_id;
	cl = &cluster_states[cluster];
	cpu_states[cpu].runq_count = nr;
	if (nr == 1 && cpu_states[cpu].state == CPU_STATE_POWER_SAVE) {
		if (!cpu_states[cpu].low_since) {
			cpu_states[cpu].low_since = ktime_get();
			goto out;
		}
		if (ktime_get() - cpu_states[cpu].low_since <= 6000000)
			goto out;
		cpu_set_power_state_locked(cpu, CPU_STATE_ACTIVE);
		cpu_states[cpu].low_since = 0;
		goto cluster_active;
	}
	cpu_states[cpu].low_since = 0;
	if (nr >= config.active_threshold && cpu_states[cpu].state == CPU_STATE_POWER_SAVE) {
		cpu_set_power_state_locked(cpu, CPU_STATE_ACTIVE);
cluster_active:
		if (!cl->power_save)
			goto out;
		cid = cpu_states[cpu].cluster_id;
		active = cl->active_cpus;
		if (cluster_active_threshold_override && config.cluster_active_threshold)
			thr = config.cluster_active_threshold;
		else
			thr = cluster_states[cid].num_cpus / 2 + 1;
		if (active < thr)
			goto out;
		cl->power_save = false;
		cl->freq_pending = true;
		cl->target_freq = INT_MAX;
		spin_unlock_irqrestore(&state_lock, flags);
		queue_work(freq_workqueue, &cluster_freq_work);
		return;
	} else if (nr <= config.idle_threshold && cpu_states[cpu].state == CPU_STATE_ACTIVE) {
		cpu_set_power_state_locked(cpu, CPU_STATE_POWER_SAVE);
		if (cl->power_save)
			goto out;
		cid = cpu_states[cpu].cluster_id;
		active = cl->active_cpus;
		if (cluster_idle_threshold_override && config.cluster_idle_threshold)
			thr = config.cluster_idle_threshold;
		else
			thr = cluster_states[cid].num_cpus / 2;
		if (active <= thr) {
			cl->power_save = true;
			cl->freq_pending = true;
			freq = config.min_freq;
		} else if (active <= cl->mid_cpus) {
			cl->freq_pending = true;
			freq = config.mid_freq;
		} else {
			goto out;
		}
		cl->target_freq = freq;
		spin_unlock_irqrestore(&state_lock, flags);
		fast_switch_cluster_freq(cid, freq);
		queue_work(freq_workqueue, &cluster_freq_work);
		return;
	}
out:
	spin_unlock_irqrestore(&state_lock, flags);
}

/* ---- exports ---- */

void sched_entity_freq_enable(bool enable)
{
	unsigned long flags;
	int cpu;
	u32 i;

	mutex_lock(&config_mutex);
	config.enabled = enable;
	mutex_unlock(&config_mutex);

	spin_lock_irqsave(&state_lock, flags);
	if (enable) {
		spin_unlock_irqrestore(&state_lock, flags);
		init_active_cpus_from_runqueues();
	} else {
		for_each_possible_cpu(cpu) {
			cpu_states[cpu].runq_count = 0;
			cpu_states[cpu].low_since = 0;
			cpu_states[cpu].state = CPU_STATE_ACTIVE;
			cpu_states[cpu].unchanged_ticks = 0;
		}
		for (i = 0; i < num_clusters; i++) {
			cluster_states[i].power_save = false;
			cluster_states[i].freq_pending = true;
			cluster_states[i].target_freq = INT_MAX;
			cluster_states[i].active_cpus = 0;
			cluster_states[i].saved_max = 0;
		}
		spin_unlock_irqrestore(&state_lock, flags);
		if (freq_workqueue)
			flush_workqueue(freq_workqueue);
		for (i = 0; i < num_clusters; i++) {
			if (cluster_states[i].qos_init)
				freq_qos_update_request(&cluster_states[i].qos_req, INT_MAX);
		}
		spin_lock_irqsave(&state_lock, flags);
		for (i = 0; i < num_clusters; i++)
			cluster_states[i].freq_pending = false;
		spin_unlock_irqrestore(&state_lock, flags);
	}
	pr_info(SEF "Feature %s\n", enable ? "enabled" : "disabled");
}
EXPORT_SYMBOL(sched_entity_freq_enable);

unsigned int sched_entity_freq_get_runq_count(int cpu)
{
	unsigned long flags;
	unsigned int count = 0;

	if ((unsigned int)cpu < MAX_CPUS) {
		spin_lock_irqsave(&state_lock, flags);
		count = cpu_states[cpu].runq_count;
		spin_unlock_irqrestore(&state_lock, flags);
	}
	return count;
}
EXPORT_SYMBOL(sched_entity_freq_get_runq_count);

bool sched_entity_freq_is_power_save_mode(unsigned int cluster_id)
{
	unsigned long flags;
	bool ps = false;

	if (cluster_id < num_clusters) {
		spin_lock_irqsave(&state_lock, flags);
		ps = cluster_states[cluster_id].power_save;
		spin_unlock_irqrestore(&state_lock, flags);
	}
	return ps;
}
EXPORT_SYMBOL(sched_entity_freq_is_power_save_mode);

/* ---- sysfs ---- */

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	bool v;

	mutex_lock(&config_mutex);
	v = config.enabled;
	mutex_unlock(&config_mutex);
	return scnprintf(buf, PAGE_SIZE, "%u\n", v);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	bool val = false;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;
	sched_entity_freq_enable(val);
	return count;
}

#define SEF_SHOW(name)							\
static ssize_t name##_show(struct kobject *kobj,			\
			   struct kobj_attribute *attr, char *buf)	\
{									\
	return scnprintf(buf, PAGE_SIZE, "%u\n", config.name);		\
}

SEF_SHOW(idle_threshold)
SEF_SHOW(active_threshold)
SEF_SHOW(cluster_idle_threshold)
SEF_SHOW(cluster_active_threshold)
SEF_SHOW(min_freq)
SEF_SHOW(mid_freq)
SEF_SHOW(normal_freq)
SEF_SHOW(fast_switch_delay)
SEF_SHOW(ddr_freq_sync)
SEF_SHOW(ddr_min_freq)

static ssize_t idle_threshold_store(struct kobject *kobj, struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (val > config.active_threshold) {
		pr_err(SEF "idle_threshold %u cannot be greater than active_threshold %u\n",
		       val, config.active_threshold);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.idle_threshold = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "idle_threshold set to %u\n", val);
	return count;
}

static ssize_t active_threshold_store(struct kobject *kobj, struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (val < config.idle_threshold) {
		pr_err(SEF "active_threshold %u cannot be less than idle_threshold %u\n",
		       val, config.idle_threshold);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.active_threshold = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "active_threshold set to %u\n", val);
	return count;
}

static ssize_t cluster_idle_threshold_store(struct kobject *kobj, struct kobj_attribute *attr,
					    const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (cluster_active_threshold_override && val > config.cluster_active_threshold) {
		pr_err(SEF "cluster_idle_threshold %u cannot be greater than cluster_active_threshold %u\n",
		       val, config.cluster_active_threshold);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.cluster_idle_threshold = val;
	cluster_idle_threshold_override = true;
	mutex_unlock(&config_mutex);
	pr_info(SEF "cluster_idle_threshold set to %u (override)\n", val);
	return count;
}

static ssize_t cluster_active_threshold_store(struct kobject *kobj, struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (cluster_idle_threshold_override && val < config.cluster_idle_threshold) {
		pr_err(SEF "cluster_active_threshold %u cannot be less than cluster_idle_threshold %u\n",
		       val, config.cluster_idle_threshold);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.cluster_active_threshold = val;
	cluster_active_threshold_override = true;
	mutex_unlock(&config_mutex);
	pr_info(SEF "cluster_active_threshold set to %u (override)\n", val);
	return count;
}

static ssize_t min_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (!val) {
		pr_err(SEF "min_freq cannot be 0\n");
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	if (val > config.normal_freq) {
		pr_err(SEF "min_freq %u cannot be greater than normal_freq %u\n",
		       val, config.normal_freq);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.min_freq = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "min_freq set to %u kHz\n", val);
	return count;
}

static ssize_t mid_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (!val) {
		pr_err(SEF "mid_freq cannot be 0\n");
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.mid_freq = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "mid_freq set to %u kHz\n", val);
	return count;
}

static ssize_t normal_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (!val) {
		pr_err(SEF "normal_freq cannot be 0\n");
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	if (val < config.min_freq) {
		pr_err(SEF "normal_freq %u cannot be less than min_freq %u\n",
		       val, config.min_freq);
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.normal_freq = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "normal_freq set to %u kHz\n", val);
	return count;
}

static ssize_t fast_switch_delay_store(struct kobject *kobj, struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	config.fast_switch_delay = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "fast_switch_delay set to %u ms\n", val);
	return count;
}

static ssize_t ddr_freq_sync_store(struct kobject *kobj, struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	bool val = false;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	config.ddr_freq_sync = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "ddr_freq_sync set to %s\n", val ? "true" : "false");
	return count;
}

static ssize_t ddr_min_freq_store(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	u32 val = 0;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;
	mutex_lock(&config_mutex);
	if (!val) {
		pr_err(SEF "ddr_min_freq cannot be 0\n");
		mutex_unlock(&config_mutex);
		return -EINVAL;
	}
	config.ddr_min_freq = val;
	mutex_unlock(&config_mutex);
	pr_info(SEF "ddr_min_freq set to %u kHz\n", val);
	return count;
}

static struct kobj_attribute enable_attr = __ATTR(enable, 0644, enable_show, enable_store);
static struct kobj_attribute idle_threshold_attr = __ATTR(idle_threshold, 0644, idle_threshold_show, idle_threshold_store);
static struct kobj_attribute active_threshold_attr = __ATTR(active_threshold, 0644, active_threshold_show, active_threshold_store);
static struct kobj_attribute cluster_idle_threshold_attr = __ATTR(cluster_idle_threshold, 0644, cluster_idle_threshold_show, cluster_idle_threshold_store);
static struct kobj_attribute cluster_active_threshold_attr = __ATTR(cluster_active_threshold, 0644, cluster_active_threshold_show, cluster_active_threshold_store);
static struct kobj_attribute min_freq_attr = __ATTR(min_freq, 0644, min_freq_show, min_freq_store);
static struct kobj_attribute mid_freq_attr = __ATTR(mid_freq, 0644, mid_freq_show, mid_freq_store);
static struct kobj_attribute normal_freq_attr = __ATTR(normal_freq, 0644, normal_freq_show, normal_freq_store);
static struct kobj_attribute fast_switch_delay_attr = __ATTR(fast_switch_delay, 0644, fast_switch_delay_show, fast_switch_delay_store);
static struct kobj_attribute ddr_freq_sync_attr = __ATTR(ddr_freq_sync, 0644, ddr_freq_sync_show, ddr_freq_sync_store);
static struct kobj_attribute ddr_min_freq_attr = __ATTR(ddr_min_freq, 0644, ddr_min_freq_show, ddr_min_freq_store);

static struct attribute *sched_entity_freq_attrs[] = {
	&enable_attr.attr,
	&idle_threshold_attr.attr,
	&active_threshold_attr.attr,
	&cluster_idle_threshold_attr.attr,
	&cluster_active_threshold_attr.attr,
	&min_freq_attr.attr,
	&mid_freq_attr.attr,
	&normal_freq_attr.attr,
	&fast_switch_delay_attr.attr,
	&ddr_freq_sync_attr.attr,
	&ddr_min_freq_attr.attr,
	NULL,
};

static struct attribute_group sched_entity_freq_attr_group = {
	.attrs = sched_entity_freq_attrs,
};

/* global in the stock (separate TU there) */
noinline int sched_entity_freq_sysfs_init(void)
{
	struct device *cpu_dev;
	struct kernfs_node *kn;
	struct kobject *k;
	int ret;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err(SEF "Failed to get cpu device\n");
		return -ENODEV;
	}
	kn = NULL;
	if (cpu_dev->kobj.parent)
		kn = kernfs_find_and_get_ns(cpu_dev->kobj.parent->sd, "dcvs_arbi", NULL);
	if (!kn) {
		dcvs_arbi_kobj = NULL;
		pr_err(SEF "dcvs_arbi kobject not found\n");
		return -ENODEV;
	}
	k = kn->priv;
	kobject_get(k);
	kernfs_put(kn);
	dcvs_arbi_kobj = k;
	if (!k) {
		pr_err(SEF "dcvs_arbi kobject not found\n");
		return -ENODEV;
	}
	sched_entity_freq_kobj = kobject_create_and_add("sched_entity_freq", k);
	if (!sched_entity_freq_kobj) {
		pr_err(SEF "Failed to create kobject\n");
		kobject_put(dcvs_arbi_kobj);
		dcvs_arbi_kobj = NULL;
		return -ENOMEM;
	}
	ret = sysfs_create_group(sched_entity_freq_kobj, &sched_entity_freq_attr_group);
	if (!ret) {
		pr_info(SEF "Sysfs interface initialized under /sys/devices/system/cpu/dcvs_arbi/sched_entity_freq\n");
		return 0;
	}
	pr_err(SEF "Failed to create sysfs group: %d\n", ret);
	kobject_put(sched_entity_freq_kobj);
	kobject_put(dcvs_arbi_kobj);
	sched_entity_freq_kobj = NULL;
	dcvs_arbi_kobj = NULL;
	return ret;
}

noinline void sched_entity_freq_sysfs_exit(void)
{
	if (sched_entity_freq_kobj) {
		sysfs_remove_group(sched_entity_freq_kobj, &sched_entity_freq_attr_group);
		kobject_put(sched_entity_freq_kobj);
		sched_entity_freq_kobj = NULL;
	}
	if (dcvs_arbi_kobj) {
		kobject_put(dcvs_arbi_kobj);
		dcvs_arbi_kobj = NULL;
	}
	pr_info(SEF "Sysfs interface cleaned up\n");
}

static int __init sched_entity_freq_init(void)
{
	struct cpufreq_policy *policy;
	int cpu, ret;
	u32 i;

	pr_info(SEF "Initializing sched entity frequency control\n");
	for_each_possible_cpu(cpu) {
		u32 cluster = cpu_topology[cpu].cluster_id;

		if (cluster == -1)
			cluster = cpu_topology[cpu].package_id;
		if (cluster >= MAX_CLUSTERS) {
			pr_warn(SEF "Cluster ID %u exceeds maximum supported clusters\n", cluster);
			continue;
		}
		if (cluster >= num_clusters)
			num_clusters = cluster + 1;
		set_bit(cpu, &cluster_states[cluster].cpu_mask);
		cluster_states[cluster].cluster_id = cluster;
		cpu_states[cpu].cpu = cpu;
		cpu_states[cpu].cluster_id = cluster;
		cpu_states[cpu].runq_count = 0;
		cpu_states[cpu].flag0c = false;
		cpu_states[cpu].last_jiffies = jiffies;
		cpu_states[cpu].low_since = 0;
		cpu_states[cpu].state = CPU_STATE_ACTIVE;
		cpu_states[cpu].unchanged_ticks = 0;
	}
	for (i = 0; i < num_clusters; i++) {
		struct cluster_state *cl = &cluster_states[i];

		cl->num_cpus = bitmap_weight(&cl->cpu_mask, 32);
		if (cl->num_cpus <= 2) {
			for_each_set_bit(cpu, &cl->cpu_mask, 32)
				cpu_states[cpu].skip = true;
			pr_info(SEF "Cluster %u (%u CPUs): skipped\n", i, cl->num_cpus);
		}
	}
	pr_info(SEF "Detected %u clusters with %d CPUs\n", num_clusters, num_possible_cpus());
	for (i = 0; i < num_clusters; i++) {
		struct cluster_state *cl = &cluster_states[i];

		if (cpumask_first((const struct cpumask *)&cl->cpu_mask) >= nr_cpu_ids)
			continue;
		cl->min_freq = config.min_freq;
		cl->max_freq = config.normal_freq;
		cl->cur_freq = config.normal_freq;
		cl->active_cpus = 0;
		cl->power_save = false;
		cl->freq_pending = false;
		cl->target_freq = config.normal_freq;
		cl->qos_init = false;
		cl->saved_max = 0;
		cl->mid_cpus = cl->num_cpus * 3 / 4;
	}
	for (i = 0; i < num_clusters; i++) {
		cpu = find_first_bit(&cluster_states[i].cpu_mask, 32);
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			ret = freq_qos_add_request(&policy->constraints, &cluster_states[i].qos_req,
						   FREQ_QOS_MAX, INT_MAX);
			cpufreq_cpu_put(policy);
			if (ret >= 0) {
				cluster_states[i].qos_init = true;
				continue;
			}
			pr_err(SEF "Failed to add freq_qos request for cluster %u: %d\n", i, ret);
		} else {
			pr_err(SEF "Failed to get cpufreq policy for cluster %u cpu %d\n", i, cpu);
		}
		pr_warn(SEF "Failed to initialize QoS for cluster %u\n", i);
	}
	init_active_cpus_from_runqueues();
	if (config.ddr_freq_sync) {
		u32 ddr_min = 0, ddr_max = 0;

		ret = qcom_dcvs_hw_minmax_get(DCVS_DDR, &ddr_min, &ddr_max);
		if (ret) {
			pr_warn(SEF "Failed to get DDR frequency range, using defaults\n");
			ddr_min = config.ddr_min_freq;
			ddr_max = ddr_min * 10;
		}
		strcpy(ddr_voter.name, "sched_entity_freq");
		ddr_voter.low.hw_type = DCVS_DDR;
		ddr_voter.low.ib = ddr_min;
		ddr_voter.low.ab = ddr_min;
		ddr_voter.high.hw_type = DCVS_DDR;
		ddr_voter.high.ib = ddr_max;
		ddr_voter.high.ab = ddr_max;
		ddr_voter.cur.hw_type = DCVS_DDR;
		ret = qcom_dcvs_register_voter(ddr_voter.name, DCVS_DDR, DCVS_FAST_PATH);
		if (ret) {
			pr_err(SEF "Failed to register DDR voter: %d\n", ret);
			pr_warn(SEF "Failed to initialize DDR voter: %d\n", ret);
		} else {
			ddr_voter.registered = true;
			ddr_voter.power_save = false;
			pr_info(SEF "DDR voter registered: min=%u, max=%u\n", ddr_min, ddr_max);
		}
	}
	freq_workqueue = alloc_workqueue("sef_freq_wq", WQ_HIGHPRI, 0);
	if (!freq_workqueue) {
		ret = -ENOMEM;
		goto fail;
	}
	INIT_WORK(&cluster_freq_work, cluster_freq_work_handler);
	ret = sched_entity_freq_sysfs_init();
	if (ret)
		pr_warn(SEF "Failed to initialize sysfs interface: %d\n", ret);
	ret = register_trace_android_vh_scheduler_tick(sched_tick_hook, NULL);
	if (ret) {
		pr_err(SEF "Failed to register scheduler tick hook: %d\n", ret);
		if (freq_workqueue) {
			destroy_workqueue(freq_workqueue);
			freq_workqueue = NULL;
		}
		goto fail;
	}
	atomic_inc(&module_ref_count);
	pr_info(SEF "Sched entity frequency control initialized successfully\n");
	return 0;
fail:
	cleanup_ddr_voter();
	sched_entity_freq_sysfs_exit();
	return ret;
}

static void __exit sched_entity_freq_exit(void)
{
	u32 i;

	pr_info(SEF "Exiting sched entity frequency control\n");
	atomic_dec(&module_ref_count);
	unregister_trace_android_vh_scheduler_tick(sched_tick_hook, NULL);
	tracepoint_synchronize_unregister();
	for (i = 0; i < num_clusters; i++) {
		if (cluster_states[i].qos_init) {
			freq_qos_remove_request(&cluster_states[i].qos_req);
			cluster_states[i].qos_init = false;
		}
	}
	cleanup_ddr_voter();
	if (freq_workqueue) {
		flush_workqueue(freq_workqueue);
		destroy_workqueue(freq_workqueue);
		freq_workqueue = NULL;
	}
	sched_entity_freq_sysfs_exit();
}

module_init(sched_entity_freq_init);
module_exit(sched_entity_freq_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xiaomi Inc.");
MODULE_DESCRIPTION("Sched Entity Frequency Control");
