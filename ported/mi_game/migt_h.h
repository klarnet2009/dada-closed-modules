/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: "freedom" window apps (G1ab, 18.10.2026). Included at the end of params.c (after migt_g.h, before migt_a.h).
 * Threads of the uid written into freedom_window_app / minor_window_app are bound to a cpumask; recovered from the OS4
 * stock disassembly, NOT hardware-tested: DO NOT LOAD.
 */
#include <linux/hashtable.h>
#include <linux/cpumask.h>

extern struct tracepoint __tracepoint_android_rvh_cpuset_fork, __tracepoint_android_vh_sched_setaffinity_early;
extern int tracepoint_probe_register(struct tracepoint *tp, void *probe, void *data);

struct freedom_task_entry {		/* BTF of the stock, 56 bytes */
	struct task_struct *task;	/* +0 (referenced) */
	unsigned long backup;		/* +8 cpus_mask the task had before it was bound */
	u8 is_minor;			/* +16 */
	struct hlist_node node;		/* +24 freedom_task_table */
	struct list_head list;		/* +40 unbind / collect list */
};

static DEFINE_SPINLOCK(freedom_lock);
static struct cpumask freedom_custom_cpumask;
static unsigned int freedom_window_app, minor_window_app;
static DEFINE_MUTEX(freedom_register_lock);
static bool freedom_hooks_registered;
static DEFINE_SPINLOCK(freedom_task_lock);
static DEFINE_HASHTABLE(freedom_task_table, 7);

static int freedom_cpumask_val_set(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;
	unsigned long flags;
	int cpu;

	if (kstrtouint(val, 0, &v))
		return -EINVAL;
	spin_lock_irqsave(&freedom_lock, flags);
	cpumask_clear(&freedom_custom_cpumask);
	for_each_possible_cpu(cpu)
		if ((v >> cpu) & 1)
			cpumask_set_cpu(cpu, &freedom_custom_cpumask);
	spin_unlock_irqrestore(&freedom_lock, flags);
	return 0;
}

static int freedom_cpumask_val_get(char *buf, const struct kernel_param *kp)
{
	unsigned long flags;
	int len;

	spin_lock_irqsave(&freedom_lock, flags);
	len = scnprintf(buf, PAGE_SIZE, "0x%lx\n", *cpumask_bits(&freedom_custom_cpumask));
	spin_unlock_irqrestore(&freedom_lock, flags);
	return len;
}

static const struct kernel_param_ops param_ops_freedom_cpumask = {
	.set = freedom_cpumask_val_set,
	.get = freedom_cpumask_val_get,
};
module_param_cb(freedom_cpumask_val, &param_ops_freedom_cpumask, NULL, 0644);

static __always_inline void freedom_drop(struct freedom_task_entry *e)
{
	put_task_struct(e->task);
	kfree(e);
}

/* bind the task of the entry to the freedom cpumask (everything for the minor window app) and remember it; or give up */
static void freedom_commit_entry(struct freedom_task_entry *e)
{
	struct freedom_task_entry *o;
	struct cpumask mask;
	unsigned long flags;

	if (e->is_minor) {
		cpumask_copy(&mask, cpu_possible_mask);
	} else {
		spin_lock_irqsave(&freedom_lock, flags);
		if (cpumask_empty(&freedom_custom_cpumask)) {
			spin_unlock_irqrestore(&freedom_lock, flags);
			{ freedom_drop(e); return; }
		}
		mask = freedom_custom_cpumask;
		spin_unlock_irqrestore(&freedom_lock, flags);
	}
	spin_lock_irqsave(&freedom_task_lock, flags);
	hash_for_each_possible(freedom_task_table, o, node, (unsigned long)e->task) {
		if (o->task == e->task) {
			spin_unlock_irqrestore(&freedom_task_lock, flags);
			{ freedom_drop(e); return; }
		}
	}
	spin_unlock_irqrestore(&freedom_task_lock, flags);
	if (set_cpus_allowed_ptr(e->task, &mask))
		{ freedom_drop(e); return; }
	spin_lock_irqsave(&freedom_task_lock, flags);
	hash_for_each_possible(freedom_task_table, o, node, (unsigned long)e->task) {
		if (o->task == e->task) {
			spin_unlock_irqrestore(&freedom_task_lock, flags);
			{ freedom_drop(e); return; }
		}
	}
	hash_add(freedom_task_table, &e->node, (unsigned long)e->task);
	spin_unlock_irqrestore(&freedom_task_lock, flags);
}

/* give every bound task of the kind (minor_only) its old affinity back and forget it */
static void freedom_unbind_all(bool minor_only)
{
	struct freedom_task_entry *e, *tmp;
	struct hlist_node *n;
	LIST_HEAD(unbind);
	unsigned long flags;
	int bkt;

	spin_lock_irqsave(&freedom_task_lock, flags);
	hash_for_each_safe(freedom_task_table, bkt, n, e, node) {
		if (e->is_minor == (minor_only & 1)) {
			hlist_del_init(&e->node);
			list_add(&e->list, &unbind);
		}
	}
	spin_unlock_irqrestore(&freedom_task_lock, flags);
	list_for_each_entry_safe(e, tmp, &unbind, list) {
		list_del(&e->list);
		if (!(READ_ONCE(e->task->flags) & PF_EXITING))
			set_cpus_allowed_ptr(e->task, (struct cpumask *)&e->backup);
		put_task_struct(e->task);
		kfree(e);
	}
}

static void mi_game_freedom_cpuset_fork(void *nouse, struct task_struct *p, bool *forbidden);
static void mi_game_freedom_setaffinity(void *nouse, struct task_struct *p, const struct cpumask *in_mask,
					bool *forbidden);

static int freedom_register_hooks(void)
{
	int ret = 0;

	mutex_lock(&freedom_register_lock);
	if (!freedom_hooks_registered) {
		ret = android_rvh_probe_register(&__tracepoint_android_rvh_cpuset_fork, mi_game_freedom_cpuset_fork, NULL);
		if (ret) {
			printk(KERN_ERR "migt-gtc: mi_game freedom: cpuset_fork hook register failed: %d\n", ret);
		} else {
			ret = tracepoint_probe_register(&__tracepoint_android_vh_sched_setaffinity_early,
							mi_game_freedom_setaffinity, NULL);
			if (ret)
				printk(KERN_ERR "migt-gtc: mi_game freedom: sched_setaffinity_early hook register failed: %d\n",
				       ret);
			freedom_hooks_registered = true;	/* sic: set even when the second hook failed (stock) */
		}
	}
	mutex_unlock(&freedom_register_lock);
	return ret;
}

static __always_inline struct freedom_task_entry *freedom_new_entry(struct task_struct *t, gfp_t gfp)
{
	struct freedom_task_entry *e = kmalloc(sizeof(*e), gfp);

	if (!e)
		return NULL;
	get_task_struct(t);
	e->task = t;
	e->backup = *cpumask_bits(&t->cpus_mask);
	e->is_minor = 0;
	INIT_LIST_HEAD(&e->list);
	return e;
}

/* a task is forked: if its uid is a window app, bind it right away */
static void mi_game_freedom_cpuset_fork(void *nouse, struct task_struct *p, bool *forbidden)
{
	struct freedom_task_entry *e;
	unsigned int uid, fw, mw;

	if (!p)
		return;
	uid = task_cred_xxx(p, uid).val;
	fw = smp_load_acquire(&freedom_window_app);
	if (fw && uid == fw) {
		if (test_ti_thread_flag(task_thread_info(p), TIF_32BIT) || (p->flags & PF_EXITING))
			return;
		e = freedom_new_entry(p, GFP_KERNEL);
		if (!e)
			return;
		freedom_commit_entry(e);
	} else if ((mw = smp_load_acquire(&minor_window_app)) && uid == mw) {
		if (test_ti_thread_flag(task_thread_info(p), TIF_32BIT) || (p->flags & PF_EXITING))
			return;
		e = freedom_new_entry(p, GFP_KERNEL);
		if (!e)
			return;
		e->is_minor = 1;
		freedom_commit_entry(e);
	}
}

/* sched_setaffinity of a bound task is ignored */
static void mi_game_freedom_setaffinity(void *nouse, struct task_struct *p, const struct cpumask *in_mask,
					bool *forbidden)
{
	struct freedom_task_entry *e;
	unsigned long flags;

	if (!p || !forbidden)
		return;
	spin_lock_irqsave(&freedom_task_lock, flags);
	hash_for_each_possible(freedom_task_table, e, node, (unsigned long)p) {
		if (e->task == p) {
			*forbidden = true;
			break;
		}
	}
	spin_unlock_irqrestore(&freedom_task_lock, flags);
}

/* bind every thread of the uid now written into the parameter (and release the ones of the old uid) */
static __always_inline int freedom_window_set(const char *val, unsigned int *app, bool minor)
{
	struct task_struct *g, *t;
	struct freedom_task_entry *e, *tmp;
	unsigned int v = 0, old;
	struct list_head todo;
	int ret;

	ret = kstrtouint(val, 0, &v);
	if (ret)
		return ret;
	ret = freedom_register_hooks();
	if (ret)
		return ret;
	old = smp_load_acquire(app);
	*app = v;
	if (old)
		freedom_unbind_all(minor);
	if (!v)
		return 0;
	if (!minor && cpumask_empty(&freedom_custom_cpumask))
		return 0;
	INIT_LIST_HEAD(&todo);
	rcu_read_lock();
	for_each_process_thread(g, t) {
		if (task_cred_xxx(t, uid).val != v)
			continue;
		if (test_ti_thread_flag(task_thread_info(t), TIF_32BIT) || (t->flags & PF_EXITING))
			continue;
		e = freedom_new_entry(t, GFP_ATOMIC);
		if (!e)
			continue;
		e->is_minor = minor;
		list_add(&e->list, &todo);
	}
	rcu_read_unlock();
	list_for_each_entry_safe(e, tmp, &todo, list) {
		list_del(&e->list);
		freedom_commit_entry(e);
	}
	return 0;
}

static int freedom_window_app_set(const char *val, const struct kernel_param *kp)
{
	return freedom_window_set(val, &freedom_window_app, false);
}

static int minor_window_app_set(const char *val, const struct kernel_param *kp)
{
	return freedom_window_set(val, &minor_window_app, true);
}

static const struct kernel_param_ops freedom_window_app_ops = { .set = freedom_window_app_set, .get = param_get_uint };
static const struct kernel_param_ops minor_window_app_ops = { .set = minor_window_app_set, .get = param_get_uint };
module_param_cb(freedom_window_app, &freedom_window_app_ops, &freedom_window_app, 0644);
module_param_cb(minor_window_app, &minor_window_app_ops, &minor_window_app, 0644);
