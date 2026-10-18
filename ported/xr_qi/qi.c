// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS inherit driver (OS4): core, per-task qos state, gpu-blocked list, sysfs/sysctl.
 * Reconstructed from the stock binary; qi.c in the XRING tree is an empty stub.
 */

#define pr_fmt(fmt) "qi: " fmt

#include <linux/cred.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"

#include "trace_xr_qi.h"

#define SYS_MODE	0640

static volatile unsigned long g_qi_enable;
DEFINE_SPINLOCK(g_stats_lock);
static struct ctl_table_header *xr_qos_cth;

/* ThinLTO promoted this object (used from several source files of the stock module) */
struct atomic_notifier_head g_qievent_notif_chain __asm__("g_qievent_notif_chain.llvm.6505585393509743323")
	__attribute__((visibility("hidden"))) = ATOMIC_NOTIFIER_INIT(g_qievent_notif_chain);

unsigned int sysctl_mutex_qos_opt_spin_time_ns = 100000;
unsigned int sysctl_mutex_normal_opt_spin_time_ns = 60000;

struct list_head g_gpu_blocked_list = LIST_HEAD_INIT(g_gpu_blocked_list);

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

void update_static_prio(struct task_struct *task, int prio, int vip_prio GKPAD(fcrc5KDk031J1g))
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

u32 get_static_qos_lvl(struct task_struct *task GKPAD(fcrc63JozGiuwN))
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

int get_task_prio(struct task_struct *task GKPAD(fcrcr2lXe04u7v))
{
	struct qi_struct *qi = get_task_qi(task);

	if (!qi)
		return 120;
	return min_t(int, qi->static_prio, qi->dyn_prio);
}
EXPORT_SYMBOL_GPL(get_task_prio);

u32 get_qos_lvl(struct task_struct *task GKPAD(fcrcG4u98hqq6J))
{
	return max(get_dyn_qos_lvl(task), get_static_qos_lvl(task));
}
EXPORT_SYMBOL_GPL(get_qos_lvl);

bool is_qos_task(struct task_struct *task GKPAD(fcrcnTDbsZh2d8))
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

u32 get_task_qos_lvl(struct task_struct *task GKPAD(fcrcaIh0tGelF4))
{
	u32 lvl;

	if (!task)
		return 0;
	lvl = get_qos_lvl(task);
	return lvl > 6 ? 1 : (lvl == 6) << 1;
}
EXPORT_SYMBOL_GPL(get_task_qos_lvl);

int get_proc_static_qos_lvl(struct task_struct *task GKPAD(fcrcowoRoWNTBR))
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
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_ptr = ptr;
	v->iter_depth = 1;
}

static void iter_ptrs_end(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return;
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_ptr = NULL;
	v->iter_depth = 0;
}

static void android_vh_dup_task_struct(void *unused, struct task_struct *tsk, struct task_struct *orig)
{
	struct qi_vendor *v = android_task_vendor_data(tsk);

	if (!v)
		return;
	spin_lock_init(&v->lock);
	v->f1_lo = 7;
	v->f1_b4 = 1;
	v->f4 = -1;
}

bool qi_is_gpu_blocked_task(struct task_struct *task GKPAD(fcrcXgucAeK9kc))
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
	int ret = -EINVAL;

	if (!info || !max)
		return ret;
	spin_lock(&g_stats_lock);
	ret = 0;
	list_for_each_entry(n, &g_gpu_blocked_list, list) {
		info[ret].pid = n->pid;
		info[ret].blocked_ns = ktime_get() - n->start;
		if (ret++ == max - 1)
			break;
	}
	spin_unlock(&g_stats_lock);
	return ret;
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

/* ---- qos setters/getters (ioctl side of the stock source file) ---- */
static bool cred_valid(struct task_struct *p)
{
	kuid_t uid = current_uid();

	if (uid_eq(uid, GLOBAL_ROOT_UID) || __kuid_val(uid) == 1000)
		return true;
	if (!uid_valid(uid)) {
		pr_err("invalid uid\n");
		return false;
	}
	return uid_eq(uid, task_uid(p));
}

static int __qi_set_task_qos(struct task_struct *task, u32 qos, u32 qos_lvl)
{
	struct qievent_struct event = {};
	struct qi_struct *qi, *gl_qi;
	struct task_struct *t;
	unsigned long flags;
	u32 old, max_lvl;
	unsigned long act;
	bool new_on, old_on;

	if (!cred_valid(task)) {
		pr_err("qi_set_task_qos: FAILED - task=%s[%d] cred_valid failed\n", task->comm, task->pid);
		return -EINVAL;
	}
	if (task->__state == TASK_DEAD) {
		pr_err("qi_set_task_qos: FAILED - task=%s[%d] is DEAD\n", task->comm, task->pid);
		return -EINVAL;
	}
	qi = get_alloc_task_qi(task);
	if (!qi) {
		pr_err("qi_set_task_qos: FAILED - task=%s[%d] get_alloc_task_qi\n", task->comm, task->pid);
		return -ENOMEM;
	}
	old = atomic_read(&qi->static_qos);
	if (old == qos)
		return 0;

	new_on = qos_lvl != 0;
	old_on = (old & 0xf) != 0;
	act = (new_on && !old_on) ? QIEVENT_QOS_SET : QIEVENT_QOS_RESTORE;
	if (new_on && old_on)
		act = QIEVENT_QOS_UNSET;

	spin_lock_irqsave(&qi->lock, flags);
	atomic_set(&qi->static_qos, qos);

	max_lvl = 0;
	gl_qi = get_task_qi(task->group_leader);
	if (gl_qi && (atomic_read(&gl_qi->proc_static_qos) & 0xf) > qos_lvl) {
		for_each_thread(task, t) {
			u32 lvl = get_static_qos_lvl(t);

			if (lvl > max_lvl)
				max_lvl = lvl;
		}
	}
	gl_qi = get_alloc_task_qi(task->group_leader);
	if (!gl_qi)
		pr_err("alloc task qi failed\n");
	else
		atomic_set(&gl_qi->proc_static_qos, max(max_lvl, qos_lvl));

	event.to = task;
	event.from_qos.qos = qos;
	event.to_qos.qos = get_task_qos(task);
	event.prio = get_task_prio(task);
	event.vip_prio = qi->dyn_vip_prio;
	call_qievent_notifiers(act, &event);
	trace_qi_set_qos(task, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	spin_unlock_irqrestore(&qi->lock, flags);
	return 0;
}

int kern_qi_set_qos(struct task_struct *task, u32 qos GKPAD(fcrcaNQafAojQ5))
{
	u32 qos_lvl = qos & 0xf;
	int ret;

	if (!task) {
		pr_err("qi_set_task_qos: FAILED - task is NULL\n");
		return -EINVAL;
	}
	if (qos_lvl >= 11) {
		pr_err("qi_set_task_qos: FAILED - task=%s[%d] invalid qos_lvl=%u\n", task->comm, task->pid, qos_lvl);
		return -EINVAL;
	}
	get_task_struct(task);
	ret = __qi_set_task_qos(task, qos, qos_lvl);
	put_task_struct(task);
	return ret;
}
EXPORT_SYMBOL_GPL(kern_qi_set_qos);

struct qi_ioctl_arg {
	u32 val;
	pid_t pid;
};

static int qi_set_qos(void *arg)
{
	struct qi_ioctl_arg a = {};
	struct task_struct *task;
	u32 qos_lvl;
	int ret;

	if (!(g_qi_enable & 1))
		return 0;
	if (!arg) {
		pr_err("set qos: invalid arg from user space\n");
		return -EINVAL;
	}
	if (copy_from_user(&a, (void __user *)arg, sizeof(a))) {
		pr_err("set qos: failed to copy from user\n");
		return -EFAULT;
	}
	qos_lvl = a.val & 0xf;
	if (unlikely(a.pid < 1 || qos_lvl >= 11)) {
		pr_err("set qos: invalid pid[%u] or qos level[%u]\n", a.pid, qos_lvl);
		return -EINVAL;
	}
	rcu_read_lock();
	task = find_task_by_vpid(a.pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("set qos: find task failed\n");
		return -EINVAL;
	}
	get_task_struct(task);
	rcu_read_unlock();
	ret = __qi_set_task_qos(task, a.val, qos_lvl);
	put_task_struct(task);
	return ret;
}

static int qi_get_qos_by_pid(void *arg)
{
	struct qi_ioctl_arg a = {};
	struct task_struct *task, *t;
	u32 qos;
	int ret;

	if (!arg) {
		pr_err("get qos by pid: invalid arg from user space\n");
		return -EINVAL;
	}
	if (copy_from_user(&a, (void __user *)arg, sizeof(a))) {
		pr_err("get qos by pid: failed to copy from user\n");
		return -EFAULT;
	}
	rcu_read_lock();
	task = find_task_by_vpid(a.pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("get qos by pid: find task failed\n");
		return -EINVAL;
	}
	if (!cred_valid(task)) {
		rcu_read_unlock();
		pr_err("set qos: cred check failed\n");
		return -EINVAL;
	}
	if (unlikely(!thread_group_leader(task) || task->__state == TASK_DEAD)) {
		rcu_read_unlock();
		pr_err("get qos by pid: not a group leader or dead task, pid=%d, tgid=%d\n", task->pid, task->tgid);
		return -EINVAL;
	}
	qos = 0;
	for_each_thread(task, t)
		qos = max(get_qos_lvl(t), qos);
	rcu_read_unlock();
	a.val = qos;
	if (copy_to_user((void __user *)arg, &a, sizeof(a))) {
		pr_err("get qos by pid: failed to copy to user\n");
		ret = -EFAULT;
	} else {
		ret = 0;
	}
	trace_qi_get_qos_by_pid(a.pid, a.val, ret);
	return ret;
}

static int qi_get_qos_by_tid(void *arg)
{
	struct qi_ioctl_arg a = {};
	struct task_struct *task;
	int ret;

	if (!arg) {
		pr_err("get qos by tid: invalid arg from user space\n");
		return -EINVAL;
	}
	if (copy_from_user(&a, (void __user *)arg, sizeof(a))) {
		pr_err("get qos by tid: failed to copy from user\n");
		return -EFAULT;
	}
	rcu_read_lock();
	task = find_task_by_vpid(a.pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("get qos by tid: find task failed\n");
		return -EINVAL;
	}
	if (!cred_valid(task)) {
		rcu_read_unlock();
		pr_err("set qos: cred check failed\n");
		return -EINVAL;
	}
	if (task->__state == TASK_DEAD) {
		rcu_read_unlock();
		pr_err("get qos: task is dead\n");
		return -EINVAL;
	}
	a.val = get_qos_lvl(task);
	rcu_read_unlock();
	if (copy_to_user((void __user *)arg, &a, sizeof(a))) {
		pr_err("get qos by tid: failed to copy to user\n");
		ret = -EFAULT;
	} else {
		ret = 0;
	}
	trace_qi_get_qos_by_tid(a.pid, a.val, ret);
	return ret;
}

static int qi_set_async_binder(void *arg)
{
	struct qi_ioctl_arg a = {};
	struct task_struct *task;
	struct qi_struct *qi;
	unsigned long flags;
	bool async;
	int ret;

	if (!(g_qi_enable & 1))
		return 0;
	if (!arg) {
		pr_err("set async binder: invalid arg from user space\n");
		return -EINVAL;
	}
	if (copy_from_user(&a, (void __user *)arg, sizeof(a))) {
		pr_err("set async binder: failed to copy from user\n");
		return -EFAULT;
	}
	if (a.pid <= 0) {
		pr_err("set async binder: invalid pid[%u]\n", a.pid);
		return -EINVAL;
	}
	async = a.val;
	rcu_read_lock();
	task = find_task_by_vpid(a.pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("set async binder: find task failed\n");
		return -EINVAL;
	}
	get_task_struct(task);
	rcu_read_unlock();
	if (!cred_valid(task)) {
		pr_err("set async binder: cred check failed\n");
		ret = -EINVAL;
		goto out;
	}
	if (task->__state == TASK_DEAD) {
		pr_err("set async binder: task is dead\n");
		ret = 0;
		goto out;
	}
	qi = get_alloc_task_qi(task);
	if (!qi) {
		ret = -ENOMEM;
		goto out;
	}
	spin_lock_irqsave(&qi->lock, flags);
	qi->async_binder_todo = a.val;
	trace_qi_set_async_binder(task, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos), async);
	spin_unlock_irqrestore(&qi->lock, flags);
	ret = 0;
out:
	put_task_struct(task);
	return ret;
}

int qi_vh_comm_register(void);
void qi_vh_comm_unregister(void);
int qi_vh_binder_register(void);
void qi_vh_binder_unregister(void);
int qi_vh_mutex_register(void);
void qi_vh_mutex_unregister(void);
int qi_vh_rwsem_register(void);
void qi_vh_rwsem_unregister(void);
int qi_vh_percpu_rwsem_register(void);
void qi_vh_percpu_rwsem_unregister(void);
int qi_vh_futex_register(void);
void qi_vh_futex_unregister(void);
int qi_vh_fuse_register(void);
void qi_vh_fuse_unregister(void);
int qi_vh_rcu_register(void);
void qi_vh_rcu_unregister(void);
int qi_stats_register(void);
void qi_stats_unregister(void);
int qi_sysctl_register(void);
void qi_sysctl_unregister(void);

static int (*const g_qi_regs[])(void) = {
	qi_vh_comm_register, qi_vh_binder_register, qi_vh_mutex_register, qi_vh_rwsem_register,
	qi_vh_percpu_rwsem_register, qi_vh_futex_register, qi_vh_fuse_register, qi_vh_rcu_register,
	qi_stats_register, qi_sysctl_register,
};

static void (*const g_qi_unregs[])(void) = {
	qi_sysctl_unregister, qi_stats_unregister, qi_vh_rcu_unregister, qi_vh_fuse_unregister,
	qi_vh_futex_unregister, qi_vh_percpu_rwsem_unregister, qi_vh_rwsem_unregister,
	qi_vh_mutex_unregister, qi_vh_binder_unregister, qi_vh_comm_unregister,
};

static int __init qi_init(void)
{
	int ret, err, i;
	struct kobject *qi_kobj;

	qi_kobj = kobject_create_and_add("qos_inherit", kernel_kobj);
	if (!qi_kobj)
		return -ENOMEM;
	ret = sysfs_create_group(qi_kobj, &qi_attr_group);
	if (ret) {
		pr_err("qi init: failed to create sys node\n");
		goto err_put;
	}
	for (i = 0; i < ARRAY_SIZE(g_qi_regs); i++) {
		err = g_qi_regs[i]();
		if ((err < 0) & !ret)
			ret = err;
	}
	if (ret < 0) {
		for (i = 0; i < ARRAY_SIZE(g_qi_unregs); i++)
			g_qi_unregs[i]();
		sysfs_remove_group(qi_kobj, &qi_attr_group);
		goto err_put;
	}
	register_xr_fbt(0x40087807, qi_set_qos);
	register_xr_fbt(0xc0087808, qi_get_qos_by_pid);
	register_xr_fbt(0xc0087809, qi_get_qos_by_tid);
	register_xr_fbt(0x4008780b, qi_set_async_binder);
	return ret;
err_put:
	kobject_put(qi_kobj);
	return ret;
}

module_init(qi_init);

MODULE_AUTHOR("Shaohua Fan <fanshaohua@xiaomi.com>");
MODULE_DESCRIPTION("MI QOS inherit Driver");
MODULE_LICENSE("GPL v2");
