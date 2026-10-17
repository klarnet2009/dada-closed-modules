// SPDX-License-Identifier: GPL-2.0
/*
 * sched_entity_freq: Sched Entity Frequency Control (Xiaomi).
 * Reconstructed from the HyperOS 4.0 stock module (no public source).
 *
 * STATE: the sysfs interface (/sys/devices/system/cpu/dcvs_arbi/sched_entity_freq),
 * the config object, the three exports' simple parts and the helpers are reconstructed
 * and checked against the stock; the frequency engine (sched_entity_freq_enable,
 * init_active_cpus_from_runqueues, cluster_freq_work_handler, sched_tick_hook,
 * cpu_set_power_state_locked, fast_switch_cluster_freq, module init/exit) is NOT yet
 * reconstructed: those functions are placeholders marked TODO(G1d) below.
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

struct cpu_state {			/* 40 bytes */
	u64 pad0;
	u32 runq_count;			/* +0x08 */
	u32 pad1[7];
};

struct cluster_state {			/* 128 bytes */
	u8 pad0[0x14];
	u8 power_save;			/* +0x14 */
	u8 pad1[0x80 - 0x15];
};

static DEFINE_SPINLOCK(state_lock);
static struct cpu_state cpu_states[MAX_CPUS];
static u32 num_clusters;
static struct cluster_state cluster_states[MAX_CLUSTERS];
static struct workqueue_struct *freq_workqueue __maybe_unused;
static u32 module_ref_count __maybe_unused;
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

struct sef_rq {
	u32 pad;
	unsigned int nr_running;
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct sef_rq, runqueues);

struct ddr_voter_s {
	bool registered;
	char name[75];
};
static struct ddr_voter_s ddr_voter;

extern int qcom_dcvs_unregister_voter(const char *name, int hw_type, u32 path_type);

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

/* ---- exports ---- */

void sched_entity_freq_enable(bool enable)
{
	/* TODO(G1d): not reconstructed (263 insns in the stock: ddr voter, per-cluster freq_qos, tick hook) */
	config.enabled = enable;
}
EXPORT_SYMBOL(sched_entity_freq_enable);

unsigned int sched_entity_freq_get_runq_count(int cpu)
{
	unsigned long flags;
	unsigned int count;

	if ((unsigned int)cpu >= MAX_CPUS)
		return 0;
	spin_lock_irqsave(&state_lock, flags);
	count = cpu_states[cpu].runq_count;
	spin_unlock_irqrestore(&state_lock, flags);
	return count;
}
EXPORT_SYMBOL(sched_entity_freq_get_runq_count);

bool sched_entity_freq_is_power_save_mode(unsigned int cluster_id)
{
	unsigned long flags;
	bool ps;

	if (cluster_id >= num_clusters)
		return false;
	spin_lock_irqsave(&state_lock, flags);
	ps = cluster_states[cluster_id].power_save;
	spin_unlock_irqrestore(&state_lock, flags);
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

static noinline int sched_entity_freq_sysfs_init(void)
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

static noinline void sched_entity_freq_sysfs_exit(void)
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
	/* TODO(G1d): not reconstructed (388 insns: cluster detection, freq_qos, DDR voter, tick hook) */
	static u32 runq_counts[MAX_CPUS];

	on_each_cpu(read_runq_on_cpu, runq_counts, 1);
	cpu_states[0].runq_count = runq_counts[0];
	num_clusters = num_possible_cpus() > 4 ? 3 : 1;
	cluster_states[0].power_save = runq_counts[1] != 0;
	return sched_entity_freq_sysfs_init();
}

static void __exit sched_entity_freq_exit(void)
{
	cleanup_ddr_voter();
	sched_entity_freq_sysfs_exit();
}

module_init(sched_entity_freq_init);
module_exit(sched_entity_freq_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Xiaomi Inc.");
MODULE_DESCRIPTION("Sched Entity Frequency Control");
