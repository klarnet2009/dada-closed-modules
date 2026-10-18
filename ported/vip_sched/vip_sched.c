// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024, X-Ring technologies Inc., All rights reserved.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/trace.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>

#include <trace/hooks/sched.h>

#include "vip_sched.h"

#define CREATE_TRACE_POINTS
#include "trace_vip_sched.h"

#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

DEFINE_PER_CPU(struct vip_rq, vip_rq);

DEFINE_MUTEX(vip_update_lock);
static u64 g_wait_time_thres = 50000000; /* stock .data initial value (0x2faf080) */
static u64 g_vip_wait_time_thres = 3000000; /* stock .data initial value (0x2dc6c0) */

struct vip_stats {
	atomic_t nr_running;
};
static struct vip_stats sys_vip_stats;

/***********************************************
 * VIP Sched helper methods
 */

static inline bool is_fair_task(struct task_struct *p)
{
	return p->prio >= MAX_RT_PRIO && !is_idle_task(p);
}

static inline bool is_dl_rt_task(struct task_struct *p)
{
	return p->prio < MAX_RT_PRIO;
}

bool is_vip_task(struct task_struct *p)
{
	struct vip_task *vip = task_to_vip(p);

	if (!vip)
		return false;
	return !!(vip->prio);
}
EXPORT_SYMBOL_GPL(is_vip_task);

static inline int has_vip_running(void)
{
	return atomic_read(&sys_vip_stats.nr_running);
}

int get_vip_prio(struct task_struct *p)
{
	struct vip_task *vip;

	if (!p)
		return 0;
	vip = task_to_vip(p);
	if (!vip)
		return 0;
	return vip->prio;
}
EXPORT_SYMBOL_GPL(get_vip_prio);

bool is_vip_cpu(int cpu)
{
	return per_cpu(vip_rq, cpu).nr_running > 0;
}
EXPORT_SYMBOL_GPL(is_vip_cpu);

int vip_cpu_hprio(int cpu)
{
	return per_cpu(vip_rq, cpu).h_prio;
}
EXPORT_SYMBOL_GPL(vip_cpu_hprio);

int find_vip_preempt_cpu(const cpumask_t *mask)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		if (per_cpu(vip_rq, cpu).vip_preempt)
			return cpu;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(find_vip_preempt_cpu);

static int vip_rq_highest_prio(struct vip_rq *vrq)
{
	struct vip_task *vip;

	/* The highest prio in an empty vip_list is 0 */
	if (list_empty(&vrq->vip_list))
		return 0;

	/* Get the fist entry in rq's vip_list */
	vip = list_first_entry(&vrq->vip_list, struct vip_task, entry);

	return vip->prio;
}

static bool optimistic_wait(struct task_struct *next)
{
	/* If the preemptor is a small task (util < 25%), no need to migrate
	 * the preempted vip task.
	 */
	if (plat_task_util(next) < (SCHED_CAPACITY_SCALE >> 2))
		return true;

	return false;
}

static enum vip_preempt_type vip_preempt_classify(struct task_struct *prev,
		struct task_struct *next, struct rq *rq)
{
	struct vip_task *p_vip = task_to_vip(prev);
	struct vip_task *n_vip = task_to_vip(next);
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));

	if (prev == next || !vrq->nr_running)
		return VIP_PREEMPT_NONE;

	if (!n_vip->prio && !is_dl_rt_task(next))
		return VIP_PREEMPT_NONE;

	if (prev->on_rq && p_vip->prio &&
		!optimistic_wait(next))
		return VIP_PREEMPT_VIP;

	if (prev->on_rq)
		return VIP_PREEMPT_OTHER;

	return vrq->vip_preempt;
}

/***********************************************
 * VIP Sched task list manipulation methods
 */

static void vip_detach_task(struct task_struct *p, struct rq *src_rq,
							struct rq *dst_rq)
{
	deactivate_task(src_rq, p, 0);
	set_task_cpu(p, cpu_of(dst_rq));
}

static void vip_attach_task(struct task_struct *p, struct rq *rq)
{
	WARN_ON(task_rq(p) != rq);
	activate_task(rq, p, 0);
	check_preempt_curr(rq, p, 0);
}

static void find_relaxed_target(struct cpumask *target,
		struct task_struct *p)
{
	struct cpumask tmp;
	struct vip_rq *vrq;
	struct vip_task *vip;
	int i, l_prio = INT_MAX;

	if (cpumask_empty(target))
		return;

	cpumask_clear(&tmp);

	vip = task_to_vip(p);
	if (!vip)
		return;
	for_each_cpu(i, target) {
		vrq = &per_cpu(vip_rq, i);
		if (vrq->h_prio > vip->prio)
			continue;

		if (cpu_rq(i)->rt.rt_nr_running)
			continue;

		if (vrq->h_prio < l_prio) {
			l_prio = vrq->h_prio;
			cpumask_clear(&tmp);
		}

		if (vrq->h_prio == l_prio)
			cpumask_set_cpu(i, &tmp);
	}

	cpumask_copy(target, &tmp);
}

static void find_fits_target(struct cpumask *target,
		struct task_struct *p)
{
	struct cpumask tmp;
	int i;

	if (cpumask_empty(target))
		return;

	cpumask_clear(&tmp);

	for_each_cpu(i, target) {
		if (arch_scale_cpu_capacity(i) <
			plat_get_group_prefer_util(p))
			continue;

		cpumask_set_cpu(i, &tmp);
	}

	cpumask_copy(target, &tmp);
}

static int find_idle_cpu(const struct cpumask *mask)
{
	int i;

	if (cpumask_empty(mask))
		return -1;

	for_each_cpu(i, mask) {
		if (available_idle_cpu(i))
			return i;
	}

	return -1;
}

static int find_vip_cpu(struct task_struct *p)
{
	int target_cpu = -1;
	struct cpumask target_mask, rtg_mask, trace_mask;
	bool repick = false;

	if (!p) {
		WARN_ONCE(1, "task is NULL\n");
		return -1;
	}

again:
	/* The candidate should be task's allowed cpu */
	cpumask_and(&target_mask, p->cpus_ptr, cpu_online_mask);

	/* The candidate should be unhalted cpus */
	cpumask_andnot(&target_mask, &target_mask, plat_cpu_halt_mask());

	if (repick) {
		/* The candidate should be task fits cpu */
		find_fits_target(&target_mask, p);
		goto pick_idle;
	}

	/* RTG prefer cpus would be a better choice */
	plat_get_group_prefer_cpus(p, &rtg_mask);
	if (trace_find_vip_cpu_enabled())
		cpumask_copy(&trace_mask, &rtg_mask);

	cpumask_and(&rtg_mask, &target_mask, &rtg_mask);
	if (cpumask_empty(&rtg_mask)) {
		repick = true;
		goto again;
	}
	cpumask_copy(&target_mask, &rtg_mask);

pick_idle:
	/* Idle cpu would be a prefer choice */
	target_cpu = find_idle_cpu(&target_mask);
	if (target_cpu >= 0)
		goto done;

	/* The candidate rq should have a lower vip prio */
	find_relaxed_target(&target_mask, p);
	if (cpumask_empty(&target_mask) && !repick) {
		repick = true;
		goto again;
	}

done:
	trace_find_vip_cpu(p->pid, &target_mask, target_cpu, p->cpus_ptr,
			(struct cpumask *)plat_cpu_halt_mask(), &trace_mask, repick);

	return target_cpu;
}

static int find_emerg_cpu(void)
{
	struct vip_rq *vrq;
	int cpu, emerg = -1;
	int trace_preempt, trace_nr_running;
	unsigned long flags;

	for_each_online_cpu(cpu) {
		raw_spin_lock_irqsave(&cpu_rq(cpu)->__lock, flags);

		vrq = &per_cpu(vip_rq, cpu);

		/* We should help the preempted vip task firstly */
		if (vrq->vip_preempt == VIP_PREEMPT_VIP) {
			trace_find_emerg_cpu(cpu, vrq->vip_preempt, vrq->nr_running);
			raw_spin_unlock_irqrestore(&cpu_rq(cpu)->__lock, flags);
			return cpu;
		}

		if (vrq->nr_running <= 1)
			goto next;

		emerg = cpu;
		if (trace_find_emerg_cpu_enabled()) {
			trace_preempt = vrq->vip_preempt;
			trace_nr_running = vrq->nr_running;
		}

next:
		raw_spin_unlock_irqrestore(&cpu_rq(cpu)->__lock, flags);
	}

	trace_find_emerg_cpu(emerg, trace_preempt, trace_nr_running);
	return emerg;
}

enum {
	MIGRATE_OK,
	MIGRATE_NOT_ALLOW,
	MIGRATE_RUNNING,
	MIGRATE_H_PRIO,
	MIGRATE_L_CAP,
	MIGRATE_RTG,
};

static bool can_migrate_vip(struct task_struct *p, struct vip_task *vip,
							struct vb_env *env)
{
	int cpu = -1;
	int migrate = true;
	int reason = MIGRATE_OK;
	struct cpumask rtg_mask;

	if (!vip || !env)
		return 0;

	cpu = env->dst_cpu;
	/* Can't migrate task to a not allowed cpu */
	if (!cpumask_test_cpu(cpu, p->cpus_ptr)) {
		reason = MIGRATE_NOT_ALLOW;
		goto done;
	}

	/* Can't migrate a running task */
	if (p->on_cpu == 1) {
		reason = MIGRATE_RUNNING;
		goto done;
	}

	/* No need to migrate a lower prio vip to this cpu */
	if (vip->prio <= env->dst_vrq->h_prio) {
		reason = MIGRATE_H_PRIO;
		goto done;
	}

	if (plat_capacity_orig_of(cpu) < plat_task_util(p)) {
		reason = MIGRATE_L_CAP;
		goto done;
	}

	plat_get_group_prefer_cpus(p, &rtg_mask);
	if (!cpumask_test_cpu(cpu, &rtg_mask) &&
			env->src_vrq->vip_preempt != VIP_PREEMPT_VIP) {
		reason = MIGRATE_RTG;
		goto done;
	}

done:
	if (reason != MIGRATE_OK)
		migrate = false;

	trace_can_migrate_vip(cpu, p->pid, vip->prio, migrate, reason);
	return migrate;
}

static int vip_balance(int this_cpu, struct rq *this_rq)
{
	struct task_struct *p;
	struct vip_task *tmp;
	unsigned long flags;

	struct vb_env env = {
		.dst_rq = this_rq,
		.dst_cpu = this_cpu,
		.dst_vrq = &per_cpu(vip_rq, this_cpu),
		.task = NULL,
	};

	env.src_cpu = find_emerg_cpu();
	if (env.src_cpu < 0 || env.src_cpu == this_cpu)
		return 0;

	env.src_rq = cpu_rq(env.src_cpu);

	raw_spin_lock_irqsave(&env.src_rq->__lock, flags);

	env.src_vrq = &per_cpu(vip_rq, env.src_cpu);

	/* Migate vip task from low to high priority */
	list_for_each_entry_reverse(tmp, &env.src_vrq->vip_list,
						entry) {
		p = vip_to_task(tmp);
		if (can_migrate_vip(p, tmp, &env)) {
			env.task = p;
			break;
		}
	}

	if (env.task)
		vip_detach_task(env.task, env.src_rq, env.dst_rq);

	raw_spin_unlock_irqrestore(&env.src_rq->__lock, flags);

	raw_spin_lock_irqsave(&env.dst_rq->__lock, flags);
	if (env.task)
		vip_attach_task(env.task, env.dst_rq);

	raw_spin_unlock_irqrestore(&env.dst_rq->__lock, flags);

	trace_vip_balance(&env);

	return env.task ? 1 : 0;
}

#define VIP_KICK_IDLE  0
static void kick_vip_balance(struct vip_rq *vrq, struct task_struct *p)
{
	struct vip_rq *t_vrq;
	int cpu;
	unsigned int flags;

	if (!vrq || !is_vip_task(p) || !cpu_active(vrq->cpu))
		return;

#if VIP_KICK_IDLE
	/* Find a idle cpu */
	for_each_online_cpu(cpu) {
		if (available_idle_cpu(cpu) && !plat_cpu_halted(cpu) &&
			cpu != vrq->cpu) {
			t_vrq = &per_cpu(vip_rq, cpu);
			flags = atomic_fetch_or(VIP_BALANCE_KICK, &t_vrq->kick_flags);
			if (flags & VIP_BALANCE_KICK)
				return;

			trace_kick_vip_balance(vrq->cpu, cpu, p->pid, true);
			smp_call_function_single_async(cpu, &t_vrq->csd);
			return;
		}
	}
#else
	cpu = find_vip_cpu(p);
	if (cpu >= 0 && cpu != vrq->cpu) {
		t_vrq = &per_cpu(vip_rq, cpu);
		flags = atomic_fetch_or(VIP_BALANCE_KICK, &t_vrq->kick_flags);
		if (flags & VIP_BALANCE_KICK)
			return;

		trace_kick_vip_balance(vrq->cpu, cpu, p->pid, false);
		smp_call_function_single_async(cpu, &t_vrq->csd);
	}
#endif
}

static int vip_balance_commit(struct vip_rq *vrq)
{
	int ret = 0;

	if (!has_vip_running() || vrq->doing_balance)
		return ret;

	vrq->doing_balance = true;
	ret = vip_balance(vrq->cpu, cpu_rq(vrq->cpu));
	vrq->doing_balance = false;

	return ret;
}

static void vip_csd_func(void *info)
{
	struct vip_rq *vrq = info;
	unsigned int flags;

	flags = atomic_fetch_andnot(VIP_BALANCE_KICK, &vrq->kick_flags);
	WARN_ON(!(flags & VIP_BALANCE_KICK));
	vip_balance_commit(vrq);
}

static void enqueue_vip_task(struct rq *rq, struct task_struct *p)
{
	struct vip_rq *vrq;
	struct vip_task *tmp, *vip;

	if (!rq || !p)
		return;

	vip = task_to_vip(p);
	if (!vip || !vip->prio || !list_empty(&vip->entry))
		return;

	vrq = &per_cpu(vip_rq, cpu_of(rq));

	list_for_each_entry(tmp, &vrq->vip_list, entry) {
		if (likely(tmp) && vip->prio > tmp->prio)
			break;
	}

	list_add(&vip->entry, tmp->entry.prev);

	vrq->h_prio = vip_rq_highest_prio(vrq);
	vrq->nr_running++;

	atomic_inc(&sys_vip_stats.nr_running);

	trace_queue_vip_task(true, cpu_of(rq), vrq, p->pid);
}

static void dequeue_vip_task(struct rq *rq, struct task_struct *p)
{
	struct vip_rq *vrq;
	struct vip_task *vip;

	if (!rq || !p)
		return;

	vip = task_to_vip(p);
	if (!vip || !vip->prio || list_empty(&vip->entry))
		return;

	vrq = &per_cpu(vip_rq, cpu_of(rq));

	list_del_init(&vip->entry);

	vrq->h_prio = vip_rq_highest_prio(vrq);
	vrq->nr_running--;

	atomic_dec(&sys_vip_stats.nr_running);

	trace_queue_vip_task(false, cpu_of(rq), vrq, p->pid);
}

int set_vip_prio_locked(struct task_struct *p, int prio, bool set_orig)
{
	struct rq *rq = task_rq(p);
	struct vip_task *vip;
	bool queued;

	/* rq lock must been held here */
	lockdep_assert_rq_held(rq);

	queued = is_fair_task(p) && task_on_rq_queued(p);
	if (queued) {
		update_rq_clock(rq);
		dequeue_vip_task(rq, p);
	}

	vip = task_to_vip(p);
	if (!vip)
		return -EINVAL;
	vip->prio = prio;

	if (set_orig)
		vip->orig_prio = prio;

	if (queued)
		enqueue_vip_task(rq, p);

	trace_set_vip_prio(p->pid, vip->prio, vip->orig_prio);

	return 0;
}
EXPORT_SYMBOL_GPL(set_vip_prio_locked);

int set_vip_prio(struct task_struct *p, int prio, bool set_orig)
{
	struct rq_flags rf;
	struct rq *rq = NULL;

	rq = task_rq_lock(p, &rf);

	set_vip_prio_locked(p, prio, set_orig);

	task_rq_unlock(rq, p, &rf);

	return 0;
}
EXPORT_SYMBOL_GPL(set_vip_prio);

void inherit_vip_prio(struct task_struct *src_p,
		struct task_struct *dst_p)
{
	struct vip_task *tmp;

	if (!src_p || !dst_p)
		return;

	tmp = task_to_vip(src_p);
	if (!tmp || !tmp->prio)
		return;

	set_vip_prio(dst_p, tmp->prio, false);
}
EXPORT_SYMBOL_GPL(inherit_vip_prio);

void restore_vip_prio(struct task_struct *p)
{
	struct vip_task *tmp;

	if (!p)
		return;

	tmp = task_to_vip(p);
	if (!tmp)
		return;
	set_vip_prio(p, tmp->orig_prio, false);
}
EXPORT_SYMBOL_GPL(restore_vip_prio);

/***********************************************
 * VIP Sched vendor hooks
 */

void vip_sched_enqueue_task_fair(struct rq *rq, struct task_struct *p,
								int flags)
{
	enqueue_vip_task(rq, p);
}
EXPORT_SYMBOL_GPL(vip_sched_enqueue_task_fair);

void vip_sched_dequeue_task_fair(struct rq *rq, struct task_struct *p,
								int flags)
{
	dequeue_vip_task(rq, p);
}
EXPORT_SYMBOL_GPL(vip_sched_dequeue_task_fair);

void vip_sched_select_task_rq_fair(struct task_struct *p, int prev_cpu,
									int sd_flag, int wake_flags,
									int *target_cpu)
{
	struct vip_task *vip;

	if (likely(!plat_migt_enable())) {
		vip = task_to_vip(p);
		if (vip && vip->prio)
			*target_cpu = find_vip_cpu(p);
	}
}
EXPORT_SYMBOL_GPL(vip_sched_select_task_rq_fair);

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
			for (; se; se = se->parent)
#else   /* !CONFIG_FAIR_GROUP_SCHED */
#define for_each_sched_entity(se) \
			for (; se; se = NULL)
#endif

/* time a task has been waiting (stock: derived from two walt task timestamps at task+0xc90/0xca0) */
#define VIP_TS_A(p)	(*(u64 *)((char *)(p) + 0xca0))
#define VIP_TS_B(p)	(*(u64 *)((char *)(p) + 0xc90))
#define VIP_GRACE_NS	1000000ULL

static u64 vip_wait_time(struct rq *rq, struct task_struct *p, u64 now)
{
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));
	u64 ts;

	if (rq->curr == p && now - vrq->last_grace_ts > VIP_GRACE_NS)
		return 0;
	ts = max(VIP_TS_A(p), VIP_TS_B(p));
	return now < ts ? 0 : now - ts;
}

void vip_sched_replace_next_task_fair(struct rq *rq, struct task_struct **p,
		struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev)
{
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));
	struct vip_task *vip;
	struct task_struct *task, *picked;
	struct cfs_rq *cfs_rq;
	u64 now, wait_picked = 0, wait_vip;

	if (list_empty(&vrq->vip_list))
		return;

	/*
	 * If the task picked by CFS scheduler is cpu binding, keep it
	 * to prevent starving issue.
	 */
	picked = *p;
	if (picked && picked->nr_cpus_allowed == 1) {
		trace_vip_replace_next_task(picked, 0, picked->pid, 0);
		return;
	}

	vip = list_first_entry(&vrq->vip_list, struct vip_task, entry);
	task = vip_to_task(vip);

	if (task && task != prev && (task->on_cpu == 1 || task->on_rq == 0 ||
		task->on_rq == TASK_ON_RQ_MIGRATING ||
		task_cpu(task) != cpu_of(rq))) {
		panic("replace task error, pid=%d, on_cpu=%d, on_rq=%d, task_cpu=%d, cpu=%d\n",
				task->pid, task->on_cpu, task->on_rq, task_cpu(task), cpu_of(rq));
	}

	now = rq_clock(rq);
	if (picked)
		wait_picked = vip_wait_time(rq, picked, now);
	wait_vip = vip_wait_time(rq, task, now);

	/* grace period: the cfs pick has waited long enough while the vip task has not */
	if (wait_picked > g_wait_time_thres && wait_vip < g_vip_wait_time_thres) {
		if (rq->curr != picked)
			vrq->last_grace_ts = now;
		trace_vip_replace_next_task(picked, 0, picked->pid, wait_picked);
		return;
	}

	*p = task;
	*se = &task->se;
	*repick = true;

	if (simple) {
		for_each_sched_entity((*se)) {
			cfs_rq = cfs_rq_of(*se);
			set_next_entity(cfs_rq, *se);
		}
	}

	trace_vip_replace_next_task(task, vip->prio, picked ? picked->pid : -1, wait_picked);
}
EXPORT_SYMBOL_GPL(vip_sched_replace_next_task_fair);

void vip_sched_check_preempt_wakeup(struct rq *rq, struct task_struct *p,
		bool *preempt, bool *nopreempt, int wake_flags, struct sched_entity *se,
		struct sched_entity *pse, int next_buddy_marked)
{
	struct vip_task *vip = task_to_vip(p);
	struct vip_task *c_vip = task_to_vip(rq->curr);
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));

	if (vrq->h_prio)
		*nopreempt = true;

	if (!vip || !c_vip)
		return;

	if (vip->prio > c_vip->prio) {
		*preempt = true;
		trace_vip_preempt_curr(rq->curr->pid, p->pid, vip->prio);
	}
}
EXPORT_SYMBOL_GPL(vip_sched_check_preempt_wakeup);

void vip_sched_schedule(struct task_struct *prev, struct task_struct *next,
						struct rq *rq)
{
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));
	struct vip_task *p_vip = task_to_vip(prev);
	struct vip_task *n_vip = task_to_vip(next);
	struct vip_task *last;

	if (!p_vip || !n_vip)
		return;
	vrq->vip_preempt = vip_preempt_classify(prev, next, rq);
	trace_vip_schedule(prev->pid, p_vip->prio, next->pid, n_vip->prio,
			vrq->vip_preempt, plat_task_util(next));

	if (vrq->vip_preempt == VIP_PREEMPT_VIP) {
		if (unlikely(list_empty(&vrq->vip_list)))
			return;

		/* Get the last entry in rq's vip_list */
		last = list_last_entry(&vrq->vip_list, struct vip_task, entry);
		kick_vip_balance(vrq, vip_to_task(last));
	}
}
EXPORT_SYMBOL_GPL(vip_sched_schedule);

void vip_sched_scheduler_tick(struct rq *rq)
{
	int cpu = cpu_of(rq);
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu);
	struct vip_task *last;

	if (!vrq->nr_running) {
		vrq->vip_preempt = VIP_PREEMPT_NONE;
		if (!plat_cpu_halted(cpu))
			vip_balance_commit(vrq);
	}

	raw_spin_lock(&rq->__lock);
	if (vrq->nr_running && !is_vip_task(rq->curr) &&
			!is_idle_task(rq->curr))
		vrq->vip_preempt = VIP_PREEMPT_VIP;

	if (vrq->nr_running >= 2) {
		/* No available idle sibling, let non-prefer cpu to help */
		if (plat_find_idle_cpu_in_freq_domain(cpu) < 0)
			vrq->vip_preempt = VIP_PREEMPT_VIP;

		if (!list_empty(&vrq->vip_list)) {
			/* Get the last entry in rq's vip_list */
			last = list_last_entry(&vrq->vip_list, struct vip_task, entry);
			kick_vip_balance(vrq, vip_to_task(last));
		}
	}
	raw_spin_unlock(&rq->__lock);
}
EXPORT_SYMBOL_GPL(vip_sched_scheduler_tick);

int vip_sched_newidle_balance(struct rq *rq)
{
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu_of(rq));

	return vip_balance_commit(vrq);
}
EXPORT_SYMBOL_GPL(vip_sched_newidle_balance);

/***********************************************
 * VIP Sched ioctl method
 */

struct vip_io_cfg {
	pid_t pid;
	unsigned int prio;
};

struct vip_wait_cfg {
	u64 wait_time_thres;
	u64 vip_wait_time_thres;
};


#define CMD_SET_VIP_PRIO	_IOW('x', 0x1e, struct vip_io_cfg)
#define CMD_SET_VIP_WAIT_THRES	_IOW('x', 0x24, struct vip_wait_cfg)

static int perf_actuator_set_vip_prio(void *uarg)
{
	struct vip_io_cfg cfg;
	struct task_struct *p = NULL;
	int ret;

	if (copy_from_user(&cfg, uarg, sizeof(struct vip_io_cfg)))
		return -EFAULT;

	rcu_read_lock();

	p = find_process_by_pid(cfg.pid);
	if (!p) {
		rcu_read_unlock();
		return -ESRCH;
	}

	get_task_struct(p);
	rcu_read_unlock();

	ret = set_vip_prio(p, cfg.prio, true);
	if (ret)
		pr_err("set vip prio failed\n");

	put_task_struct(p);

	return ret;
}

static int perf_actuator_set_vip_wait_thres(void *uarg)
{
	struct vip_wait_cfg cfg;

	if (copy_from_user(&cfg, uarg, sizeof(cfg)))
		return -EFAULT;

	mutex_lock(&vip_update_lock);
	g_wait_time_thres = cfg.wait_time_thres;
	g_vip_wait_time_thres = cfg.vip_wait_time_thres;
	mutex_unlock(&vip_update_lock);

	return 0;
}

/***********************************************
 * VIP Sched init methods
 */

void vip_init_rq(int cpu)
{
	struct vip_rq *vrq = &per_cpu(vip_rq, cpu);

	vrq->cpu = cpu;
	vrq->h_prio = 0;
	vrq->nr_running = 0;
	vrq->vip_preempt = VIP_PREEMPT_NONE;
	vrq->sync_waiting = false;
	vrq->last_grace_ts = 0;

	INIT_LIST_HEAD(&vrq->vip_list);

	/* vip balance */
	vrq->doing_balance = false;
	INIT_CSD(&vrq->csd, vip_csd_func, vrq);
	atomic_set(&vrq->kick_flags, 0);
}
EXPORT_SYMBOL_GPL(vip_init_rq);

void vip_init_task(struct task_struct *p)
{
	struct vip_task *vip = task_to_vip(p);

	if (!vip)
		return;
	vip->prio = 0;
	vip->orig_prio = 0;
	INIT_LIST_HEAD(&vip->entry);
}
EXPORT_SYMBOL_GPL(vip_init_task);

static void init_sys_vip_stats(void)
{
	atomic_set(&sys_vip_stats.nr_running, 0);
}

static int __init vip_sched_init(void)
{
	init_sys_vip_stats();
	register_perf_actuator(CMD_SET_VIP_PRIO, perf_actuator_set_vip_prio);
	register_perf_actuator(CMD_SET_VIP_WAIT_THRES, perf_actuator_set_vip_wait_thres);
	return 0;
}

module_init(vip_sched_init);
MODULE_AUTHOR("Shaohua Fan <fanshaohua@xiaomi.com>");
MODULE_DESCRIPTION("MI QOS sched Driver");
MODULE_LICENSE("GPL v2");
