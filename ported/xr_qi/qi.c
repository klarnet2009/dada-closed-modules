// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS inherit driver (OS4): core, per-task qos state, gpu-blocked list, sysfs/sysctl.
 * Reconstructed from the stock binary; qi.c in the XRING tree is an empty stub.
 */

#define pr_fmt(fmt) "qi: " fmt

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"

#define CREATE_TRACE_POINTS
#include "trace_xr_qi.h"

#define SYS_MODE	0640

static volatile unsigned long g_qi_enable;
static DEFINE_SPINLOCK(g_binder_lock);
static DEFINE_SPINLOCK(g_qi_lock);
static DEFINE_SPINLOCK(g_stats_lock);
static struct ctl_table_header *xr_qos_cth;

/* ThinLTO promoted this object (used from several source files of the stock module) */
struct atomic_notifier_head g_qievent_notif_chain __asm__("g_qievent_notif_chain.llvm.6505585393509743323")
	__attribute__((visibility("hidden"))) = ATOMIC_NOTIFIER_INIT(g_qievent_notif_chain);

unsigned int sysctl_mutex_qos_opt_spin_time_ns = 100000;
unsigned int sysctl_mutex_normal_opt_spin_time_ns = 60000;

static struct list_head g_gpu_blocked_list = LIST_HEAD_INIT(g_gpu_blocked_list);

int register_qievent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&g_qievent_notif_chain, nb);
}
EXPORT_SYMBOL_GPL(register_qievent_notifier);

int unregister_qievent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&g_qievent_notif_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_qievent_notifier);

int call_qievent_notifiers(unsigned long action, void *data)
{
	return atomic_notifier_call_chain(&g_qievent_notif_chain, action, data);
}

noinline bool is_qi_feat_enabled(u8 feat)
{
	return g_qi_enable & 1;
}

static inline int fair_policy(int policy)
{
	return policy == SCHED_NORMAL || policy == SCHED_BATCH;
}

noinline struct qi_struct *get_task_qi(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return NULL;
	return smp_load_acquire(&v->qi);
}

struct qi_struct *get_alloc_task_qi(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);
	struct qi_struct *qi;
	unsigned long flags;
	int i;

	if (v) {
		qi = smp_load_acquire(&v->qi);
		if (qi)
			return qi;
	}
	spin_lock_irqsave(&g_qi_lock, flags);
	if (v) {
		qi = smp_load_acquire(&v->qi);
		if (qi)
			goto out;
	}
	qi = kzalloc(sizeof(*qi), GFP_ATOMIC);
	if (!qi)
		goto out;
	spin_lock_init(&qi->lock);
	for (i = 0; i < QOS_TYPE_NUM; i++)
		INIT_LIST_HEAD(&qi->inherit[i].list);
	qi->static_prio = 120;
	qi->dyn_prio = 120;
	smp_wmb();
	if (v)
		v->qi = qi;
out:
	spin_unlock_irqrestore(&g_qi_lock, flags);
	return qi;
}

void update_static_prio(struct task_struct *task, int prio, int vip_prio GKPAD(fcrc0sDJLyNXnI))
{
	struct qi_struct *qi;

	if (!task)
		return;
	qi = get_task_qi(task);
	if (!qi)
		return;
	if (prio >= 0)
		qi->static_prio = prio;
	if (vip_prio >= 0)
		qi->static_vip_prio = vip_prio;
}
EXPORT_SYMBOL_GPL(update_static_prio);

u32 get_static_qos_lvl(struct task_struct *task GKPAD(fcrc9MB0_bGrcl))
{
	struct qi_struct *qi = get_task_qi(task);

	if (!qi)
		return 0;
	return atomic_read(&qi->static_qos) & 0xf;
}
EXPORT_SYMBOL_GPL(get_static_qos_lvl);

u32 get_dyn_qos_lvl(struct task_struct *task)
{
	struct qi_struct *qi = get_task_qi(task);

	if (!qi)
		return 0;
	if (!atomic_read(&qi->flag))
		return 0;
	smp_rmb();
	return atomic_read(&qi->dyn_qos) & 0xf;
}

u32 get_task_qos(struct task_struct *task)
{
	struct qi_struct *qi = get_task_qi(task);
	u32 s, d;

	if (!qi)
		return 0;
	s = atomic_read(&qi->static_qos);
	if (!atomic_read(&qi->flag))
		return s;
	smp_rmb();
	d = atomic_read(&qi->dyn_qos);
	return (s & 0xf) < (d & 0xf) ? (d | (s & 0xf0)) : (s | (d & 0xf0));
}

int get_task_prio(struct task_struct *task GKPAD(fcrc5mMF35Dihv))
{
	struct qi_struct *qi = get_task_qi(task);

	if (!qi)
		return 120;
	return min_t(int, qi->static_prio, qi->dyn_prio);
}
EXPORT_SYMBOL_GPL(get_task_prio);

u32 get_qos_lvl(struct task_struct *task GKPAD(fcrcqPOppdmcYd))
{
	return max(get_dyn_qos_lvl(task), get_static_qos_lvl(task));
}
EXPORT_SYMBOL_GPL(get_qos_lvl);

bool is_qos_task(struct task_struct *task GKPAD(fcrcUZoL8g8VvT))
{
	if (!task)
		return false;
	return get_qos_lvl(task) > 5;
}
EXPORT_SYMBOL_GPL(is_qos_task);

bool is_qos_rt_task(struct task_struct *task)
{
	if (!fair_policy(task->policy))
		return true;
	return is_qos_task(task);
}

u32 get_task_qos_lvl(struct task_struct *task GKPAD(fcrcY_7sUMcsGm))
{
	u32 lvl;

	if (!task)
		return 0;
	lvl = get_qos_lvl(task);
	return lvl > 6 ? 1 : (lvl == 6) << 1;
}
EXPORT_SYMBOL_GPL(get_task_qos_lvl);

int get_proc_static_qos_lvl(struct task_struct *task GKPAD(fcrch5GlIPlGdv))
{
	struct qi_struct *qi;

	if (!task) {
		pr_err("invalid task pointer\n");
		return -EINVAL;
	}
	qi = get_task_qi(task->group_leader);
	if (!qi)
		return 0;
	return atomic_read(&qi->proc_static_qos) & 0xf;
}
EXPORT_SYMBOL_GPL(get_proc_static_qos_lvl);

static ssize_t inherit_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s:\t%d\n", "QI_ENABLE", (int)(g_qi_enable & 1));
}

static ssize_t inherit_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long long val = 0;

	if (kstrtoull(buf, 0, &val)) {
		pr_err("inherit enable store: parse enable failed\n");
		return -EINVAL;
	}
	if (val >= 0x4000) {
		pr_err("inherit enable store: invalid value\n");
		return -EINVAL;
	}
	g_qi_enable = val;
	return count;
}

static struct kobj_attribute inherit_enable_attr = __ATTR(inherit_enable, SYS_MODE, inherit_enable_show, inherit_enable_store);

static struct attribute *qi_attrs[] = {
	&inherit_enable_attr.attr,
	NULL,
};

static struct attribute_group qi_attr_group = {
	.attrs = qi_attrs,
};

static struct ctl_table xr_qos_sysctls[] = {
	{
		.procname	= "mutex_qos_opt_spin_time_ns",
		.data		= &sysctl_mutex_qos_opt_spin_time_ns,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname	= "mutex_normal_opt_spin_time_ns",
		.data		= &sysctl_mutex_normal_opt_spin_time_ns,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
};

#undef pr_fmt
#define pr_fmt(fmt) fmt

int qi_sysctl_register(void)
{
	xr_qos_cth = register_sysctl_sz("kernel/xr_qos", xr_qos_sysctls, ARRAY_SIZE(xr_qos_sysctls));
	if (!xr_qos_cth)
		pr_err("xr_qos sysctl init failed");
	return 0;
}

void qi_sysctl_unregister(void)
{
	unregister_sysctl_table(xr_qos_cth);
}

#undef pr_fmt
#define pr_fmt(fmt) "qi: " fmt

static void iter_ptrs_start(struct task_struct *p, void *ptr)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return;
	v->iter_ptr = ptr;
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_x = 0;
	v->iter_active = 1;
}

static void iter_ptrs_end(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return;
	v->iter_ptr = NULL;
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_x = 0;
	v->iter_active = 0;
}

static void android_vh_dup_task_struct(void *unused, struct task_struct *tsk, struct task_struct *orig)
{
	struct qi_vendor *v = android_task_vendor_data(tsk);

	if (!v)
		return;
	v->f34 = 0;
	v->f1 = 0x17;
	v->f4 = -1;
}

bool qi_is_gpu_blocked_task(struct task_struct *task GKPAD(fcrckHqAV4SjCz))
{
	struct qi_struct *qi = get_task_qi(task);

	if (!qi)
		return false;
	return qi->blocked_on_gpu;
}
EXPORT_SYMBOL_GPL(qi_is_gpu_blocked_task);

pid_t qi_gpu_blocked_longest(void)
{
	struct list_head *first, *last;
	struct gpu_blocked_node *n;

	spin_lock(&g_stats_lock);
	first = READ_ONCE(g_gpu_blocked_list.next);
	last = g_gpu_blocked_list.prev;
	spin_unlock(&g_stats_lock);
	if (first == &g_gpu_blocked_list || !last)
		return -1;
	n = list_entry(last, struct gpu_blocked_node, list);
	return n->pid;
}
EXPORT_SYMBOL_GPL(qi_gpu_blocked_longest);

int qi_acquire_gpu_blockeds(struct gpu_blocked_info *info, u32 max)
{
	struct gpu_blocked_node *n;
	int cnt = 0;

	if (!info || !max)
		return -EINVAL;
	spin_lock(&g_stats_lock);
	list_for_each_entry(n, &g_gpu_blocked_list, list) {
		info[cnt].pid = n->pid;
		info[cnt].blocked_ns = ktime_get() - n->start;
		if (++cnt >= max)
			break;
	}
	spin_unlock(&g_stats_lock);
	return cnt;
}
EXPORT_SYMBOL_GPL(qi_acquire_gpu_blockeds);

static void android_vh_mmap_lock_init(void *unused, struct rw_semaphore *sem)
{
	if (sem)
		*(void **)((char *)sem + 0x28) = sem;
}

static void android_vh_rwsem_init(void *unused, struct rw_semaphore *sem)
{
	if (sem) {
		*(void **)((char *)sem + 0x28) = NULL;
		*(void **)((char *)sem + 0x30) = NULL;
	}
}

static void android_vh_mutex_opt_spin_finish(void *unused, struct mutex *lock, bool taken)
{
	struct qi_struct *qi;

	if (!is_qi_feat_enabled(13))
		return;
	qi = get_task_qi(current);
	if (qi && qi->opt_spin_start_time)
		qi->opt_spin_start_time = 0;
}

/*
 * PARTIAL: the stock init_module additionally runs the ten qi_vh_*_register()/qi_stats_register()/qi_sysctl_register()
 * hooks of the other source files and registers four xr_fbt ioctl handlers; those parts are not reconstructed yet.
 */
static int __init qi_init(void)
{
	int ret;
	struct kobject *qi_kobj;

	qi_kobj = kobject_create_and_add("qos_inherit", kernel_kobj);
	if (!qi_kobj)
		return -ENOMEM;
	ret = sysfs_create_group(qi_kobj, &qi_attr_group);
	if (ret) {
		kobject_put(qi_kobj);
		pr_err("qi init: failed to create sys node\n");
	}
	return ret;
}

module_init(qi_init);

MODULE_AUTHOR("Shaohua Fan <fanshaohua@xiaomi.com>");
MODULE_DESCRIPTION("MI QOS inherit Driver");
MODULE_LICENSE("GPL v2");
