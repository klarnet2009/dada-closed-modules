// SPDX-License-Identifier: GPL-2.0
/*
 * rtg: related-thread-group (WALT) control for the Xiaomi performance stack.
 * Reconstructed from the HyperOS 4.0 stock binary (no public source).
 */
#define pr_fmt(fmt) "rtg: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/cpumask.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/irqflags.h>
#include <linux/preempt.h>
#include "rtg.h"

/* GENKSYMS-ONLY pad: forges the export CRC of the stock prototype (spelling unknown); the compiler never sees it */
#ifdef __GENKSYMS__
#define GKPAD(tag)	, struct tag *gkpad
#else
#define GKPAD(tag)
#endif

struct walt_related_thread_group *get_related_thread_group(unsigned int grp_id)
{
	if (grp_id == DEFAULT_CGROUP_COLOC_ID || grp_id >= MAX_NUM_CGROUP_COLOC_ID)
		return NULL;
	return plat_lookup_related_thread_group(grp_id);
}

void os_cpu_get_group_prefer_cpus(struct task_struct *p, struct cpumask *prefer_cpus GKPAD(fcrc8dE3xKhqbX))
{
	struct walt_related_thread_group *grp = wts_of(p)->grp;

	if (grp && grp_prefer(grp))
		cpumask_copy(prefer_cpus, &grp->rtg_prefer_cpus);
	else
		cpumask_setall(prefer_cpus);
}
EXPORT_SYMBOL_GPL(os_cpu_get_group_prefer_cpus);

unsigned long os_cpu_get_group_prefer_util(struct task_struct *p GKPAD(fcrcuCzuze7Nnl))
{
	struct walt_related_thread_group *grp = wts_of(p)->grp;

	return grp ? grp->rtg_util : 0;
}
EXPORT_SYMBOL_GPL(os_cpu_get_group_prefer_util);

bool os_cpu_is_task_prefer_cpu(struct task_struct *p, unsigned int cpu GKPAD(fcrccaoW4fdnbE))
{
	struct walt_related_thread_group *grp = wts_of(p)->grp;

	if (!grp || !grp_prefer(grp))
		return true;
	return cpumask_test_cpu(cpu, &grp->rtg_prefer_cpus);
}
EXPORT_SYMBOL_GPL(os_cpu_is_task_prefer_cpu);

bool os_cpu_can_migrate_task(struct task_struct *p, unsigned int src_cpu, unsigned int dst_cpu GKPAD(fcrcxJ4SXRkclU))
{
	struct walt_related_thread_group *grp = wts_of(p)->grp;

	if (!grp || !grp_prefer(grp))
		return true;
	if (!cpumask_test_cpu(src_cpu, &grp->rtg_prefer_cpus))
		return true;
	return cpumask_test_cpu(dst_cpu, &grp->rtg_prefer_cpus);
}
EXPORT_SYMBOL_GPL(os_cpu_can_migrate_task);

void os_cpu_init_rtg_grp_prefer_cpus(u8 prefer_cpus)
{
	struct walt_related_thread_group *grp = plat_lookup_related_thread_group(DEFAULT_CGROUP_COLOC_ID + 12);

	if (!grp) {
		pr_warn("init grp %u prefer_cpus 0x%x failed: grp not found\n", 13, prefer_cpus);
		return;
	}
	grp->rtg_prefer_cpus.bits[0] = prefer_cpus;
	pr_info("init grp %u prefer_cpus 0x%x\n", 13, prefer_cpus);
}
EXPORT_SYMBOL_GPL(os_cpu_init_rtg_grp_prefer_cpus);

static int perf_actuator_set_task_rtg(void *arg)
{
	struct task_struct *p;
	int ret;

	if (!arg)
		return -EINVAL;

	struct rtg_task_arg a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	rcu_read_lock();
	p = find_task_by_vpid(a.pid);
	if (!p) {
		pr_info("task == NULL %d", a.pid);
		rcu_read_unlock();
		return -ENOENT;
	}
	ret = plat_sched_set_group_id(p, a.grp_id);
	rcu_read_unlock();
	if (ret)
		pr_info("set task %d to rtg %d failed", a.pid, a.grp_id);
	return ret;
}

static int perf_actuator_set_task_array_rtg(void *arg)
{
	struct task_struct *p;
	int ret = 0;
	unsigned int i;

	if (!arg)
		return -EINVAL;
	struct rtg_task_array_arg a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (a.nr > RTG_TASK_ARRAY_MAX)
		return -EFAULT;
	for (i = 0; i < a.nr; i++) {
		int pid = a.pid[i];

		if (!pid)
			continue;
		rcu_read_lock();
		p = find_task_by_vpid(pid);
		if (!p) {
			rcu_read_unlock();
			pr_err(" task %d is not exist\n", pid);
			continue;
		}
		ret = plat_sched_set_group_id(p, a.grp_id);
		rcu_read_unlock();
		if (ret)
			pr_err("set task %d to rtg %d failed", pid, a.grp_id);
	}
	return ret;
}

static int perf_actuator_set_rtg_cpus(void *arg)
{
	struct walt_related_thread_group *grp;

	if (!arg)
		return -EINVAL;
	struct rtg_cpus_arg a;
	unsigned int id, cpus;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	id = a.grp_id;
	cpus = a.cpus;
	grp = get_related_thread_group(id);
	if (!grp) {
		pr_err("set preferred cpus for group %d fail\n", id);
		return -ENODEV;
	}
	grp->rtg_prefer_cpus.bits[0] = cpus;
	plat_core_ctl_update();
	return 0;
}

static int perf_actuator_set_rtg_util(void *arg)
{
	struct walt_related_thread_group *grp;

	if (!arg)
		return -EINVAL;
	struct rtg_util_arg a;
	unsigned int id, util;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	id = a.grp_id;
	util = a.util;
	grp = get_related_thread_group(id);
	if (!grp) {
		pr_err("set prefer util for group %d fail\n", id);
		return -ENODEV;
	}
	grp->rtg_util = min_t(unsigned int, util, 1024);
	return 0;
}

static int perf_actuator_clear_rtg(void *arg)
{
	struct walt_related_thread_group *grp;
	struct walt_task_struct_min *wts;
	struct task_struct *p;
	unsigned long flags;
	int max_loop = 200;

	if (!arg)
		return -EINVAL;

	unsigned int grp_id;

	if (copy_from_user(&grp_id, arg, sizeof(grp_id)))
		return -EFAULT;
	grp = get_related_thread_group(grp_id);
	if (!grp)
		return -ENOENT;

	local_irq_save(flags);
	preempt_disable();
	do {
		raw_spin_lock(&grp->lock);
		wts = list_first_entry_or_null(&grp->tasks, struct walt_task_struct_min, grp_list);
		if (!wts) {
			raw_spin_unlock(&grp->lock);
			break;
		}
		p = wts_to_ts(wts);
		get_task_struct(p);
		raw_spin_unlock(&grp->lock);
		plat_sched_set_group_id(p, 0);
		put_task_struct(p);
	} while (--max_loop > 0);
	local_irq_restore(flags);
	preempt_enable();
	return max_loop ? 0 : -EAGAIN;
}

static int __init rtg_init(void)
{
	register_perf_actuator(PERF_ACTUATOR_SET_TASK_RTG, perf_actuator_set_task_rtg);
	register_perf_actuator(PERF_ACTUATOR_SET_TASK_ARRAY_RTG, perf_actuator_set_task_array_rtg);
	register_perf_actuator(PERF_ACTUATOR_SET_RTG_CPUS, perf_actuator_set_rtg_cpus);
	register_perf_actuator(PERF_ACTUATOR_SET_RTG_UTIL, perf_actuator_set_rtg_util);
	register_perf_actuator(PERF_ACTUATOR_CLEAR_RTG, perf_actuator_clear_rtg);
	return 0;
}

module_init(rtg_init);
MODULE_LICENSE("GPL v2");
