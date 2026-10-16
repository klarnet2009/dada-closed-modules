// SPDX-License-Identifier: GPL-2.0
/*
 * Reconstructed from the stock mi_schedule.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * Original path: drivers/mihw/mi_sched/mi_group.c
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sched/user.h>
#include <linux/sched/signal.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include "mi_sched.h"

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

#undef pr_fmt
#define pr_fmt(fmt) "mi_group: " fmt

#define GRP_LOG(lvl, fmt, ...) \
	do { if (unlikely(group_debug == (lvl))) pr_info(fmt, ##__VA_ARGS__); } while (0)

DEFINE_SPINLOCK(period_freq_list_lock);
EXPORT_SYMBOL_GPL(period_freq_list_lock);

static unsigned int group_debug;
unsigned int target_makeup_ns;

task_group_change_update_f task_group_change_update_hook;
mi_update_global_period_f mi_update_global_period;

static struct mi_group *mi_grp_system;
static struct mi_group *mi_grp_background;
static struct mi_group *mi_grp_kernel;
static DEFINE_RWLOCK(active_group_lock);
static struct mi_group *mi_grp_display;
static unsigned int mi_id;
static unsigned int ls_id;
static DEFINE_RWLOCK(active_ls_group_lock);
static unsigned int deafult_id;
static DEFINE_SPINLOCK(fore_uid_list_head_lock);

static int ls_grp_prefer_cid = -1;
static LIST_HEAD(active_mi_groups);
static LIST_HEAD(active_ls_groups);
static LIST_HEAD(fore_uid_list_head);

struct fore_uid_entry {
	struct list_head list;
	int uid;
	int type;
};

void register_task_group_change_update_f(task_group_change_update_f f)
{
	task_group_change_update_hook = f;
}
EXPORT_SYMBOL_GPL(register_task_group_change_update_f);

void unregister_task_group_change_update_f(void)
{
	task_group_change_update_hook = NULL;
}
EXPORT_SYMBOL_GPL(unregister_task_group_change_update_f);

void register_mi_update_global_period_f(mi_update_global_period_f f)
{
	mi_update_global_period = f;
}
EXPORT_SYMBOL_GPL(register_mi_update_global_period_f);

void unregister_mi_update_global_period_f(void)
{
	mi_update_global_period = NULL;
}
EXPORT_SYMBOL_GPL(unregister_mi_update_global_period_f);

struct mi_group *get_mi_system_group(void)
{
	return mi_grp_system;
}
EXPORT_SYMBOL_GPL(get_mi_system_group);

struct mi_group *get_mi_back_group(void)
{
	return mi_grp_background;
}
EXPORT_SYMBOL_GPL(get_mi_back_group);

struct mi_group *get_mi_kern_group(void)
{
	return mi_grp_kernel;
}
EXPORT_SYMBOL_GPL(get_mi_kern_group);

struct list_head *get_active_mi_groups(void)
{
	return &active_mi_groups;
}
EXPORT_SYMBOL_GPL(get_active_mi_groups);

u64 get_pred_target_period(unsigned int fps)
{
	u64 period = 1000 / fps * 1000000;

	return period - target_makeup_ns;
}
EXPORT_SYMBOL_GPL(get_pred_target_period);

void init_ravg_info(struct mi_task_struct *mi_tsk)
{
	mi_tsk->mi_grp = NULL;
	mi_tsk->ls_grp = NULL;
	mi_tsk->curr_window_cpu = 0;
	mi_tsk->prev_window_cpu = 0;
	memset(mi_tsk->curr_window_cpus, 0, sizeof(mi_tsk->curr_window_cpus));
	mi_tsk->grp_curr_window_cpu = 0;
	mi_tsk->grp_prev_window_cpu = 0;
	memset(mi_tsk->prev_window_cpus, 0, sizeof(mi_tsk->prev_window_cpus));
	memset(mi_tsk->grp_prev_window_cpus, 0, sizeof(mi_tsk->grp_prev_window_cpus));
	memset(mi_tsk->grp_curr_window_cpus, 0, sizeof(mi_tsk->grp_curr_window_cpus));
	mi_tsk->ravg.running_exec = 0;
	mi_tsk->ravg.runnable_exec = 0;
	mi_tsk->ravg.sleep_exec = 0;
	mi_tsk->ravg.io_exec = 0;
	mi_tsk->ravg.memstall_exec = 0;
	mi_tsk->ravg.demand = 0;
	mi_tsk->ravg.demand_scaled = 0;
	mi_tsk->ravg.coloc_demand = 0;
	memset((char *)&mi_tsk->ravg + offsetof(struct mi_task_ravg, running_hist), 0,
	       sizeof(mi_tsk->ravg.running_hist) * 4);
}

void free_mi_group(struct mi_group *grp)
{
	unsigned long flags;

	if (!grp) {
		pr_info("group has been free\n");
		return;
	}
	grp->in_period_freq_list = true;
	if (grp->type & (1 << NON_DECEL))
		return;
	GRP_LOG(5, "free mi group %d", grp->id);
	write_lock(&active_group_lock);
	if (!list_empty(&grp->list)) {
		list_del_init(&grp->list);
		refcount_dec(&grp->refcnt);
	}
	write_unlock(&active_group_lock);
	spin_lock_irqsave(&period_freq_list_lock, flags);
	if (!list_empty(&grp->freq_head))
		list_del_init(&grp->freq_head);
	spin_unlock_irqrestore(&period_freq_list_lock, flags);
	if (refcount_dec_and_test(&grp->refcnt))
		kfree(grp);
}

void mi_group_ref_dec(struct mi_group *grp)
{
	if (!grp)
		return;
	refcount_dec(&grp->refcnt);
	if (refcount_read(&grp->refcnt) == 2)
		free_mi_group(grp);
}
EXPORT_SYMBOL_GPL(mi_group_ref_dec);

static struct mi_group *alloc_add_mi_group(enum MI_APP_TYPE type);

struct mi_group *find_add_mi_group_id(int id, enum MI_APP_TYPE type)
{
	struct mi_group *grp = NULL;
	struct list_head *pos, *n;

	switch (id) {
	case 0:
		grp = mi_grp_system;
		break;
	case 1:
		grp = mi_grp_background;
		break;
	case 2:
		grp = mi_grp_display;
		break;
	case 3:
		grp = mi_grp_kernel;
		break;
	case 4:
		grp = alloc_add_mi_group(type);
		break;
	default:
		if (mi_id > id) {
			list_for_each_safe(pos, n, &active_mi_groups) {
				grp = list_entry(pos, struct mi_group, list);
				if (grp && grp->id == id)
					break;
			}
		} else {
			grp = alloc_add_mi_group(type);
		}
		break;
	}
	if (grp)
		refcount_inc(&grp->refcnt);
	GRP_LOG(5, "find_add_mi_group_id-type%d", type);
	return grp;
}

static struct mi_group *alloc_add_mi_group(enum MI_APP_TYPE type)
{
	struct mi_group *grp;
	int i;

	grp = kzalloc(sizeof(*grp), GFP_NOWAIT);
	if (!grp)
		return NULL;
	INIT_LIST_HEAD(&grp->freq_head);
	grp->in_period_freq_list = false;
	spin_lock_init(&grp->lock);
	grp->nr_running = 0;
	grp->id = mi_id++;
	grp->type |= 1 << type;
	grp->frame_start = 0;
	grp->mark_start = 0;
	grp->window_period = 0;
	grp->target_period = 0;
	grp->local_cid = -1;
	grp->prefer_cluster = -1;
	grp->scaling_flag = 0;
	for (i = 0; i < MI_SCHED_CPUS; i++) {
		grp->cpu_prev_frame[i].running_sum_exec_runtime = 0;
		grp->cpu_prev_frame[i].runnable_sum_exec_runtime = 0;
		grp->cpu_prev_frame[i].sleep_sum_exec_runtime = 0;
		grp->cpu_prev_frame[i].io_sum_exec_runtime = 0;
		grp->cpu_curr_frame[i].running_sum_exec_runtime = 0;
		grp->cpu_curr_frame[i].runnable_sum_exec_runtime = 0;
		grp->cpu_curr_frame[i].sleep_sum_exec_runtime = 0;
		grp->cpu_curr_frame[i].io_sum_exec_runtime = 0;
		grp->cpu_nr_running[i] = 0;
		spin_lock_init(&grp->cpu_lock[i]);
	}
	for (i = 0; i < MI_SCHED_CLUSTERS; i++) {
		grp->wait_wake_flag[i] = false;
		grp->up_pred_util[i] = 0;
		grp->down_pred_util[i] = 0;
		grp->cluster_prev_frame[i].running_sum_exec_runtime = 0;
		grp->cluster_prev_frame[i].runnable_sum_exec_runtime = 0;
		grp->cluster_prev_frame[i].sleep_sum_exec_runtime = 0;
		grp->cluster_prev_frame[i].io_sum_exec_runtime = 0;
		grp->cluster_curr_frame[i].running_sum_exec_runtime = 0;
		grp->cluster_curr_frame[i].runnable_sum_exec_runtime = 0;
		grp->cluster_curr_frame[i].sleep_sum_exec_runtime = 0;
		grp->cluster_curr_frame[i].io_sum_exec_runtime = 0;
		grp->cluster_mark_start[i] = 0;
		atomic_set(&grp->cluster_token_pid[i], 0);
	}
	grp->prev_frame.running_sum_exec_runtime = 0;
	grp->prev_frame.runnable_sum_exec_runtime = 0;
	grp->prev_frame.sleep_sum_exec_runtime = 0;
	grp->prev_frame.io_sum_exec_runtime = 0;
	grp->curr_frame.running_sum_exec_runtime = 0;
	grp->curr_frame.runnable_sum_exec_runtime = 0;
	grp->curr_frame.sleep_sum_exec_runtime = 0;
	grp->curr_frame.io_sum_exec_runtime = 0;
	refcount_set(&grp->refcnt, 1);
	INIT_LIST_HEAD(&grp->list);
	write_lock(&active_group_lock);
	list_add(&grp->list, &active_mi_groups);
	refcount_inc(&grp->refcnt);
	write_unlock(&active_group_lock);
	GRP_LOG(3, "alloc mi group id%d[type%d]", grp->id, type);
	return grp;
}

static void mi_group_ref_add(struct mi_group *grp)
{
	refcount_inc(&grp->refcnt);
}

struct mi_group *find_mi_group_by_user(struct mi_user_struct *user)
{
	if (!user) {
		GRP_LOG(2, "err:user is NULL in %s!!", __func__);
		return NULL;
	}
	if (!user->default_group) {
		GRP_LOG(2, "err:user->group is NULL in %s!!", __func__);
		return NULL;
	}
	return user->default_group->mi_grp;
}
EXPORT_SYMBOL_GPL(find_mi_group_by_user);

struct mi_group *find_add_mi_group_by_user(struct mi_user_struct *user)
{
	struct mi_group *grp;

	if (!user) {
		GRP_LOG(2, "err:mi-user is NULL in %s!!", __func__);
		return NULL;
	}
	grp = find_mi_group_by_user(user);
	if (grp)
		refcount_inc(&grp->refcnt);
	GRP_LOG(2, "find by user grp%d", grp ? grp->id : -1);
	return grp;
}

struct mi_group *find_mi_group_by_task(struct task_struct *p, struct mi_task_struct *mi_tsk)
{
	struct mi_group *grp = NULL, *user_grp;

	if (unlikely(!mi_tsk))
		goto out;
	if (unlikely(p->flags & PF_IDLE))
		goto out;

	grp = mi_tsk->mi_grp;
	if (grp) {
		if (grp->type & (1 << NEED_DECEL)) {
			refcount_dec(&grp->refcnt);
			if (likely(refcount_read(&grp->refcnt) == 2))
				free_mi_group(grp);
			mi_tsk->mi_grp = NULL;
			if (unlikely(group_debug == 2 && !mi_tsk->mi_user))
				pr_info("2-pid%d uid%d mi_user is NULL", p->pid, task_uid(p).val);
			grp = find_add_mi_group_by_user(mi_tsk->mi_user);
			mi_tsk->mi_grp = grp;
			if (grp)
				goto out;
			if (mi_tsk->rsv0 == 1000 || mi_tsk->rsv0 == 0) {
				grp = find_add_mi_group_id(0, SYSTEM);
				mi_tsk->mi_grp = grp;
				goto out;
			}
			grp = NULL;
			goto out;
		}
		if (grp->type & ((1 << SYSTEM) | (1 << LATENCY_SENSITIVE) | (1 << DISPLAY) | (1 << KERRNEL)))
			goto out;
		user_grp = find_mi_group_by_user(mi_tsk->mi_user);
		if (!user_grp) {
			GRP_LOG(2, "err:uid%dpid%d user_grp is NULL in %s!!", mi_tsk->rsv0, p->pid, __func__);
		} else {
			if (likely(grp == user_grp))
				goto out;
			atomic_cmpxchg(&grp->cluster_token_pid[0], p->pid, 0);
			atomic_cmpxchg(&grp->cluster_token_pid[1], p->pid, 0);
			mi_group_ref_dec(grp);
			mi_tsk->mi_grp = user_grp;
			mi_group_ref_add(user_grp);
		}
		grp = mi_tsk->mi_grp;
		goto out;
	}
	if (unlikely(group_debug == 2 && !mi_tsk->mi_user))
		pr_info("1-pid%d uid%d mi_user is NULL", p->pid, task_uid(p).val);
	if (p->flags & PF_KTHREAD) {
		grp = find_add_mi_group_id(3, KERRNEL);
		mi_tsk->mi_grp = grp;
		goto out;
	}
	grp = find_add_mi_group_by_user(mi_tsk->mi_user);
	mi_tsk->mi_grp = grp;
	if (grp)
		goto out;
	if (mi_tsk->rsv0 == 1000 || mi_tsk->rsv0 == 0) {
		grp = find_add_mi_group_id(0, SYSTEM);
		mi_tsk->mi_grp = grp;
		goto out;
	}
	grp = NULL;
out:
	return grp;
}
EXPORT_SYMBOL_GPL(find_mi_group_by_task);

void mi_ls_group_dec(struct ls_group *ls)
{
	if (!ls)
		return;
	refcount_dec(&ls->refcnt);
	GRP_LOG(3, "dec ls grp id%d[%d]", ls->id, refcount_read(&ls->refcnt));
	if (refcount_read(&ls->refcnt) == 2) {
		GRP_LOG(5, "free ls grp id%d", ls->id);
		write_lock(&active_ls_group_lock);
		if (!list_empty(&ls->list))
			list_del_init(&ls->list);
		write_unlock(&active_ls_group_lock);
		if (ls->mi_grp) {
			ls->mi_grp->type |= 1 << NEED_DECEL;
			mi_group_ref_dec(ls->mi_grp);
			ls->mi_grp = NULL;
		}
		kfree(ls);
	}
}

void destory_ls_group(struct ls_group *ls)
{
	if (!ls)
		return;
	ls->flags |= 1 << NEED_DECEL;
	ls->mi_grp->type |= 1 << NEED_DECEL;
	GRP_LOG(3, "destort ls grp id%d[%d]", ls->id, refcount_read(&ls->refcnt));
	mi_ls_group_dec(ls);
}

void mi_ls_group_add(struct ls_group *ls)
{
	if (!ls)
		return;
	refcount_inc(&ls->refcnt);
}

struct ls_group *alloc_ls_group(enum MI_APP_TYPE type, unsigned int fps, unsigned int ls_type)
{
	struct ls_group *ls;
	struct mi_group *grp;

	ls = kzalloc(sizeof(*ls), GFP_NOWAIT);
	if (!ls) {
		pr_err("BUG alloc ls group failed!!!");
		return NULL;
	}
	refcount_set(&ls->refcnt, 1);
	ls->id = ls_id;
	ls_id = (ls_id + 1) & 7;
	ls->flags |= 1 << type;
	grp = find_add_mi_group_id(4, LATENCY_SENSITIVE);
	ls->mi_grp = grp;
	grp->target_period = 1000000000 / fps;
	INIT_LIST_HEAD(&ls->list);
	write_lock(&active_ls_group_lock);
	list_add(&ls->list, &active_ls_groups);
	write_unlock(&active_ls_group_lock);
	refcount_inc(&ls->refcnt);
	return ls;
}

struct ls_group *create_ls_group(int pid, enum MI_APP_TYPE type, unsigned int fps, unsigned int ls_type)
{
	struct task_struct *p;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;
	unsigned long flags;

	if (pid < 1)
		return NULL;
	p = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (unlikely(!p))
		return NULL;
	mi_tsk = get_mi_task_struct(p);
	if (unlikely(!mi_tsk))
		return NULL;
	mi_tsk->ls_grp = alloc_ls_group(type, fps, ls_type);
	if (!mi_tsk->ls_grp)
		return NULL;
	mi_ls_group_add(mi_tsk->ls_grp);
	mi_tsk->ls_grp->create_pid = pid;
	mi_tsk->group_flag = (mi_tsk->group_flag & ~7u) | 1;
	grp = find_mi_group_by_task(p, mi_tsk);
	if (grp) {
		atomic_cmpxchg(&grp->cluster_token_pid[0], p->pid, 0);
		atomic_cmpxchg(&grp->cluster_token_pid[1], p->pid, 0);
		mi_group_ref_dec(grp);
		mi_tsk->mi_grp = NULL;
	}
	mi_tsk->mi_grp = mi_tsk->ls_grp->mi_grp;
	spin_lock_irqsave(&mi_tsk->mi_grp->lock, flags);
	if (mi_tsk->mi_grp) {
		mi_group_ref_add(mi_tsk->mi_grp);
		if (p->on_cpu)
			mi_tsk->mi_grp->nr_running++;
	}
	spin_unlock_irqrestore(&mi_tsk->mi_grp->lock, flags);
	GRP_LOG(3, "%d create ls grp %d[usage%d] nr-running%d", pid, mi_tsk->ls_grp->id,
		refcount_read(&mi_tsk->ls_grp->refcnt), mi_tsk->mi_grp->nr_running);
	return mi_tsk->ls_grp;
}
EXPORT_SYMBOL_GPL(create_ls_group);

struct ls_group *find_ls_grp_by_task(struct mi_task_struct *mi_tsk)
{
	if (!mi_tsk)
		return NULL;
	if (mi_tsk->ls_grp && (mi_tsk->ls_grp->flags & (1 << NEED_DECEL))) {
		mi_ls_group_dec(mi_tsk->ls_grp);
		mi_tsk->ls_grp = NULL;
	}
	return mi_tsk->ls_grp;
}

void APP_destory_ls_group(int pid)
{
	struct task_struct *p;
	struct mi_task_struct *mi_tsk;
	struct ls_group *ls;

	if (pid < 1)
		return;
	p = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!p)
		return;
	mi_tsk = get_mi_task_struct_unsafe(p);
	if (!mi_tsk)
		return;
	ls = mi_tsk->ls_grp;
	if (ls) {
		if (!(ls->flags & (1 << NEED_DECEL))) {
			ls->flags |= 1 << NEED_DECEL;
			ls->mi_grp->type |= 1 << NEED_DECEL;
			GRP_LOG(3, "destort ls grp id%d[%d]", ls->id, refcount_read(&ls->refcnt));
		}
		mi_ls_group_dec(ls);
	}
	mi_tsk->ls_grp = NULL;
	GRP_LOG(3, "APP pid%d destory ls group", pid);
}

static int change_lsgroup(struct task_struct *p, struct ls_group *ls, unsigned int type);

int thread_change_ls_group_auto(struct task_struct *p)
{
	struct mi_task_struct *mi_tsk;
	struct ls_group *ls;

	GRP_LOG(3, "tsk auto%d", p->pid);
	if (!p)
		return -1;
	if (current == p) {
		ls = NULL;
		GRP_LOG(3, "tsk%d auto grp%d", p->pid, -1);
	} else {
		mi_tsk = get_mi_task_struct(current);
		if (!mi_tsk)
			return -1;
		ls = mi_tsk->ls_grp;
		if (!ls)
			return -1;
		if (ls->flags & (1 << NEED_DECEL)) {
			mi_ls_group_dec(ls);
			mi_tsk->ls_grp = NULL;
			return -1;
		}
		GRP_LOG(3, "tsk%d auto grp%d", p->pid, ls->id);
	}
	return change_lsgroup(p, ls, 4);
}
EXPORT_SYMBOL_GPL(thread_change_ls_group_auto);

static int change_lsgroup(struct task_struct *p, struct ls_group *ls, unsigned int type)
{
	struct mi_task_struct *mi_tsk;
	struct mi_group *old;
	unsigned long flags;
	bool was_dead;

	mi_tsk = get_mi_task_struct_unsafe(p);
	if (!mi_tsk)
		return -1;
	if (!ls && !(mi_tsk->group_flag & 7))
		return -1;
	if (mi_tsk->ls_grp) {
		if (mi_tsk->ls_grp->flags & (1 << NEED_DECEL)) {
			mi_ls_group_dec(mi_tsk->ls_grp);
			mi_tsk->ls_grp = NULL;
			if (!ls)
				return -1;
			was_dead = true;
		} else {
			if (mi_tsk->ls_grp == ls) {
				pr_info("err in change_lsgroup[ls_grp == grp]");
				return -1;
			}
			mi_ls_group_dec(mi_tsk->ls_grp);
			mi_tsk->ls_grp = NULL;
			if (!ls)
				goto to_default;
			was_dead = false;
		}
	} else {
		if (!ls)
			return -1;
		was_dead = true;
	}
	mi_tsk->ls_grp = ls;
	mi_ls_group_add(ls);
	mi_tsk->group_flag = (mi_tsk->group_flag & ~7u) | type;
	old = find_mi_group_by_task(p, mi_tsk);
	if (old) {
		spin_lock_irqsave(&old->lock, flags);
		if (!was_dead && p->on_cpu)
			old->nr_running--;
#line 810
		if (WARN_ON(old->nr_running < 0)) {
			pr_info("grp->nr_running < 0!!");
			old->nr_running = 0;
		}
		spin_unlock_irqrestore(&old->lock, flags);
		atomic_cmpxchg(&old->cluster_token_pid[0], p->pid, 0);
		atomic_cmpxchg(&old->cluster_token_pid[1], p->pid, 0);
		mi_group_ref_dec(old);
		mi_tsk->mi_grp = NULL;
	}
	mi_tsk->mi_grp = ls->mi_grp;
	if (mi_tsk->mi_grp) {
		mi_group_ref_add(mi_tsk->mi_grp);
		spin_lock_irqsave(&mi_tsk->mi_grp->lock, flags);
		if (p->on_cpu)
			mi_tsk->mi_grp->nr_running++;
		spin_unlock_irqrestore(&mi_tsk->mi_grp->lock, flags);
	}
	GRP_LOG(3, "task%d[%d] change to ls grp%d[%d], nr-running%d", p->pid, mi_tsk->group_flag,
		ls->id, refcount_read(&ls->refcnt), mi_tsk->mi_grp->nr_running);
	return 1;

to_default:
	if (!(mi_tsk->group_flag & type))
		return -1;
	old = find_mi_group_by_task(p, mi_tsk);
	if (old) {
		spin_lock_irqsave(&old->lock, flags);
		if (p->on_cpu)
			old->nr_running--;
#line 769
		if (WARN_ON(old->nr_running < 0)) {
			pr_info("grp->nr_running < 0!!");
			old->nr_running = 0;
		}
		spin_unlock_irqrestore(&old->lock, flags);
		atomic_cmpxchg(&old->cluster_token_pid[0], p->pid, 0);
		atomic_cmpxchg(&old->cluster_token_pid[1], p->pid, 0);
		mi_group_ref_dec(old);
		mi_tsk->mi_grp = NULL;
	}
	mi_tsk->group_flag &= ~7u;
	mi_tsk->mi_grp = find_add_mi_group_by_user(mi_tsk->mi_user);
	if (group_debug == 2)
		pr_info("3-pid%d uid%d", p->pid, task_uid(p).val);
	if (group_debug)
		pr_info("task%d[%d==%d] back2default", p->pid, mi_tsk->group_flag, type);
	return 0;
}

void change_lsgroup_status(struct task_struct *p, enum MI_APP_TYPE type)
{
	struct mi_task_struct *mi_tsk;

	mi_tsk = get_mi_task_struct_unsafe(p);
	if (!mi_tsk || !mi_tsk->ls_grp)
		return;
	if (mi_tsk->ls_grp->flags & (1 << NEED_DECEL)) {
		mi_ls_group_dec(mi_tsk->ls_grp);
		mi_tsk->ls_grp = NULL;
		return;
	}
	mi_tsk->ls_grp->flags = 1 << type;
}

bool mi_group_ls_group(struct task_struct *p, u64 unused)
{
	get_mi_task_struct(p);
	return false;
}
EXPORT_SYMBOL_GPL(mi_group_ls_group);

struct mi_default_group *alloc_mi_default_group(int id, enum MI_APP_TYPE type)
{
	struct mi_default_group *d;

	d = kzalloc(sizeof(*d), GFP_NOWAIT);
	if (!d) {
		pr_err("BUG alloc default group failed!!!");
		return NULL;
	}
	d->id = deafult_id++;
	d->flags |= 1 << type;
	d->mi_grp = find_add_mi_group_id(id, type);
	GRP_LOG(3, "alloc grp_id%d type%d", deafult_id, type);
	return d;
}

struct mi_default_group *create_mi_default_group(struct user_struct *user)
{
	struct fore_uid_entry *e;
	struct mi_default_group *d;
	int uid = user->uid.val;
	int dbg_type;

	if (uid == 0 || uid == 1000) {
		d = alloc_mi_default_group(0, SYSTEM);
		dbg_type = 1;
		goto out;
	}
	list_for_each_entry(e, &fore_uid_list_head, list) {
		if (!e)
			break;
		if (e->uid == uid) {
			d = alloc_mi_default_group(4, e->type);
			if (d) {
				dbg_type = 2;
				goto out;
			}
			break;
		}
	}
	d = alloc_mi_default_group(1, BACKGROUND);
	dbg_type = 5;
out:
	GRP_LOG(5, "uid:%d, debug_type:%d in %s", uid, dbg_type, __func__);
	return d;
}

void free_mi_default_group(struct mi_default_group *d)
{
	if (!d) {
		pr_info("default group has been free\n");
		return;
	}
	if (!(d->mi_grp->type & (1 << NON_DECEL)))
		d->mi_grp->type |= 1 << NEED_DECEL;
	mi_group_ref_dec(d->mi_grp);
	d->mi_grp = NULL;
	GRP_LOG(5, "free_mi_default_group!!!!");
	kfree(d);
}

static void set_group_fps(struct mi_group *grp, unsigned int fps)
{
	grp->fps = fps;
	grp->target_period = 1000000000 / fps;
	grp->pred_target_period = get_pred_target_period(fps);
}

void change_group_fps(int id, unsigned int fps, enum GROUP_CMD cmd)
{
	struct task_struct *p;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;
	struct user_struct *user;
	struct mi_user_struct *mi_user;

	if (cmd == APP_CHANGE_FPS && id != 0 && id != 1000) {
		user = find_user(KUIDT_INIT(id));
		if (!user)
			return;
		mi_user = get_mi_user_struct(user);
		if (mi_user) {
			grp = find_mi_group_by_user(mi_user);
			if (grp) {
				set_group_fps(grp, fps);
				GRP_LOG(4, "APP_CHANGE_FPS:uid%d, fps%d", id, fps);
			}
		}
		free_uid(user);
	} else if (cmd == TASK_CHANGE_FPS) {
		p = get_pid_task(find_vpid(id), PIDTYPE_PID);
		if (!p)
			return;
		mi_tsk = get_mi_task_struct(p);
		if (!mi_tsk)
			return;
		grp = find_mi_group_by_task(p, mi_tsk);
		if (!grp)
			return;
		set_group_fps(grp, fps);
		GRP_LOG(4, "TASK_CHANGE_FPS:pid%d, fps%d", id, fps);
	}
}

void change_system_to_display(int pid)
{
	struct task_struct *p, *t;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;

	p = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!p)
		return;
	for_each_thread(p, t) {
		mi_tsk = get_mi_task_struct(t);
		grp = find_mi_group_by_task(t, mi_tsk);
		if (grp) {
			if (grp->type & (1 << BACKGROUND))
				continue;
			atomic_cmpxchg(&grp->cluster_token_pid[0], p->pid, 0);
			atomic_cmpxchg(&grp->cluster_token_pid[1], p->pid, 0);
			mi_group_ref_dec(grp);
			mi_tsk->mi_grp = NULL;
		}
		grp = find_add_mi_group_id(2, DISPLAY);
		mi_tsk->mi_grp = grp;
		GRP_LOG(6, "p%d add in grp%d[%d]", t->pid, grp->id, grp->type);
	}
}

int thread_change_ls_group_explicit(struct ls_group *ls, int pid, unsigned int type)
{
	struct task_struct *p;

	if (pid < 1)
		return -1;
	p = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!p)
		return -1;
	if (ls)
		return change_lsgroup(p, ls, type);
	return change_lsgroup(p, NULL, type);
}
EXPORT_SYMBOL_GPL(thread_change_ls_group_explicit);

struct miscdevice group_misc __asm__("group_misc.llvm.5288858629294896160") __attribute__((visibility("hidden")));

void init_mi_group(void)
{
	mi_grp_system = alloc_add_mi_group(SYSTEM);
	mi_grp_system->type |= 1 << NON_DECEL;
	mi_grp_system->in_period_freq_list = true;
	mi_grp_background = alloc_add_mi_group(BACKGROUND);
	mi_grp_background->type |= 1 << NON_DECEL;
	mi_grp_background->in_period_freq_list = true;
	mi_grp_display = alloc_add_mi_group(DISPLAY);
	mi_grp_display->type |= 1 << NON_DECEL;
	mi_grp_kernel = alloc_add_mi_group(KERRNEL);
	mi_grp_kernel->type |= 1 << NON_DECEL;
	mi_grp_kernel->in_period_freq_list = true;
	mi_id++;
	misc_register(&group_misc);
	pr_info("mi group init !!!!!!");
}

void exit_mi_group(void)
{
	misc_deregister(&group_misc);
}

static int set_target_makeup_ns(const char *val, const struct kernel_param *kp)
{
	struct mi_group *grp;
	unsigned int max_fps = 0;

	if (sscanf(val, "%u", &target_makeup_ns) != 1)
		return -EINVAL;
	list_for_each_entry(grp, &active_mi_groups, list) {
		if (!grp || (grp->type & (1 << NON_DECEL)))
			continue;
		max_fps = max(max_fps, grp->fps);
		grp->pred_target_period = get_pred_target_period(grp->fps);
	}
	if (mi_update_global_period)
		mi_update_global_period(max_fps);
	return 0;
}

static int get_target_makeup_ns(char *buf, const struct kernel_param *kp)
{
	return sprintf(buf, "target_makeup_ns:%d\n", target_makeup_ns);
}

static const struct kernel_param_ops param_ops_target_makeup_ns = {
	.set = set_target_makeup_ns,
	.get = get_target_makeup_ns,
};

struct mi_group_data {
	enum MI_APP_TYPE type;
	int id;
	int fps;
	int ls_type;
};

static void change_APP_status(const struct mi_group_data *data)
{
	int uid = data->id;
	enum MI_APP_TYPE type = data->type;
	unsigned int fps;
	struct fore_uid_entry *e;
	struct user_struct *user;
	struct mi_user_struct *mi_user;
	struct mi_default_group *dg;
	struct mi_group *grp;
	unsigned int old_flags = 0;

	switch (type) {
	case FORE_POINT:
	case FORE_NOPOINT:
		list_for_each_entry(e, &fore_uid_list_head, list) {
			if (!e)
				break;
			if (e->uid == uid) {
				e->type = type;
				goto done_list;
			}
		}
		e = kzalloc(sizeof(*e), GFP_NOWAIT);
		if (e) {
			e->uid = uid;
			e->type = type;
			INIT_LIST_HEAD(&e->list);
			spin_lock(&fore_uid_list_head_lock);
			list_add(&e->list, &fore_uid_list_head);
			spin_unlock(&fore_uid_list_head_lock);
		}
		break;
	case BACKGROUND:
		list_for_each_entry(e, &fore_uid_list_head, list) {
			if (!e)
				break;
			if (e->uid == uid) {
				spin_lock(&fore_uid_list_head_lock);
				list_del_init(&e->list);
				spin_unlock(&fore_uid_list_head_lock);
				kfree(e);
				break;
			}
		}
		break;
	default:
		break;
	}
done_list:
	if (uid == 1000 || uid == 0) {
		GRP_LOG(5, "user%d in [user_id == 0 || user_id == 1000]", uid);
		goto out;
	}
	fps = data->fps;
	user = find_user(KUIDT_INIT(uid));
	if (!user) {
		pr_info("!user%d in %s", uid, __func__);
		goto out;
	}
	mi_user = get_mi_user_struct(user);
	if (!mi_user) {
		free_uid(user);
		pr_info("%d-!mi_user in %s", uid, __func__);
		goto out;
	}
	switch (type) {
	case FORE_POINT:
	case FORE_NOPOINT:
		dg = mi_user->default_group;
		if (!dg) {
			dg = alloc_mi_default_group(4, type);
			mi_user->default_group = dg;
			old_flags = 0;
		} else {
			old_flags = dg->flags;
			if (old_flags & (1 << BACKGROUND)) {
				if (dg->mi_grp) {
					mi_group_ref_dec(dg->mi_grp);
					mi_user->default_group->mi_grp = NULL;
				}
				grp = find_add_mi_group_id(4, type);
				mi_user->default_group->mi_grp = grp;
				if (task_group_change_update_hook)
					task_group_change_update_hook();
				goto update_flags;
			} else if (!(old_flags & ((1 << SYSTEM) | (1 << DISPLAY) | (1 << KERRNEL)))) {
update_flags:
				mi_user->default_group->flags &= ~old_flags;
				mi_user->default_group->flags |= 1 << type;
				GRP_LOG(5, "uid%d-id%d old%d->fore%d-id%d", uid, mi_user->default_group->id,
					old_flags, type, mi_user->default_group->mi_grp->id);
			} else {
				GRP_LOG(5, "FORE old_flags is %d", old_flags);
			}
		}
		break;
	case BACKGROUND:
		dg = mi_user->default_group;
		if (!dg) {
			dg = alloc_mi_default_group(1, BACKGROUND);
			mi_user->default_group = dg;
			GRP_LOG(5, "BACK !d_grp!!!!");
			old_flags = 0;
		} else {
			old_flags = dg->flags;
			if (old_flags & ((1 << SYSTEM) | (1 << BACKGROUND) | (1 << DISPLAY) | (1 << KERRNEL))) {
				GRP_LOG(5, "BACK old_flags is %d", old_flags);
			} else {
				if (dg->mi_grp) {
					dg->mi_grp->type |= 1 << NEED_DECEL;
					mi_group_ref_dec(mi_user->default_group->mi_grp);
					mi_user->default_group->mi_grp = NULL;
				}
				grp = find_add_mi_group_id(1, BACKGROUND);
				mi_user->default_group->mi_grp = grp;
				mi_user->default_group->flags &= ~old_flags;
				mi_user->default_group->flags |= 1 << BACKGROUND;
				if (task_group_change_update_hook)
					task_group_change_update_hook();
				GRP_LOG(5, "uid%d-id%d old%d->back%d-id%d", uid, mi_user->default_group->id,
					old_flags, mi_user->default_group->flags, mi_user->default_group->mi_grp->id);
			}
		}
		break;
	default:
		pr_info("change APP status error in %d", type);
		break;
	}
	if (mi_user->default_group && mi_user->default_group->mi_grp)
		mi_user->default_group->mi_grp->target_period = fps ? 1000000000 / fps : 0;
	free_uid(user);
	GRP_LOG(5, "user%d old%d->new%d.", uid, old_flags, type);
out:
	GRP_LOG(5, "change_APP_status, uid%d, type%d, fps%d", uid, type, data->fps);
}

static long mi_group_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mi_group_data data = {};
	unsigned int ret = 0;
	unsigned int size = _IOC_SIZE(cmd);
	unsigned int nr = _IOC_NR(cmd);
	enum MI_APP_TYPE type;
	int id, cid;
	struct task_struct *p;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;

	if (unlikely(group_debug))
		pr_info("ioctl-in-cmd:%d", nr);
	if (unlikely(size != sizeof(data))) {
		pr_info("err:size%d != sizeof(struct mi_group_data", size);
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		pr_info("err:copy_from_user(&data, ubuf, sizeof(data))");
		ret = -EFAULT;
		goto out;
	}
	type = data.type;
	id = data.id;
	if (unlikely(group_debug))
		pr_info("cmd:%d, id:%d, type:%d, fps:%d, lstype%d", nr, id, type, data.fps, data.ls_type);

	switch (_IOC_NR(cmd)) {
	case APP_CHANGE_STATUS:
		change_APP_status(&data);
		break;
	case APP_CHANGE_FPS:
		change_group_fps(id, data.fps, APP_CHANGE_FPS);
		break;
	case TASK_CHANGE_FPS:
		change_group_fps(id, data.fps, TASK_CHANGE_FPS);
		break;
	case CHANGE_GRP_PREFER_CID:
		if (id < 1)
			break;
		cid = data.fps;
		p = get_pid_task(find_vpid(id), PIDTYPE_PID);
		if (!p)
			break;
		mi_tsk = get_mi_task_struct(p);
		if (!mi_tsk)
			break;
		grp = find_mi_group_by_task(p, mi_tsk);
		if (!grp)
			break;
		grp->prefer_cluster = cid;
		GRP_LOG(3, "change_group_cid pid%d id%d 2cid%d", id, grp->id, cid);
		break;
	case CHANGE_DISPLAY:
		change_system_to_display(id);
		break;
	default:
		break;
	}
out:
	return ret;
}

static int mi_group_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static int mi_group_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mi_group_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations group_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mi_group_ioctl,
	.compat_ioctl = mi_group_ioctl,
	.mmap = mi_group_mmap,
	.open = mi_group_open,
	.release = mi_group_release,
};

struct miscdevice group_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mi_group",
	.fops = &group_fops,
};

module_param(ls_grp_prefer_cid, int, 0644);
module_param(group_debug, uint, 0644);
module_param_cb(target_makeup_ns, &param_ops_target_makeup_ns, &target_makeup_ns, 0644);
