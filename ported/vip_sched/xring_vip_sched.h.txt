/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024-2024, X-Ring technologies Inc., All rights reserved.
 */

#ifndef _VIP_SCHED_H
#define _VIP_SCHED_H

#include <soc/xring/sched.h>

#define	VIP_BALANCE_KICK	(1)

enum vip_preempt_type {
	VIP_PREEMPT_NONE = 0,
	/* Normal task was preempted by vip task */
	VIP_PREEMPT_OTHER,
	/* Low prio vip task was preempted by
	 * high prio vip task, or vip task was
	 * preempted by rt/dl task
	 */
	VIP_PREEMPT_VIP,
};

struct vb_env {
	int src_cpu, dst_cpu;
	struct rq *src_rq, *dst_rq;
	struct vip_rq *src_vrq, *dst_vrq;
	struct task_struct *task;
};

struct vip_rq {
	int cpu;
	int h_prio; /* highest prio */
	int nr_running;
	bool sync_waiting;
	enum vip_preempt_type vip_preempt;
	struct list_head vip_list;
	/* vip balance */
	bool doing_balance;
	call_single_data_t csd;
	atomic_t kick_flags;
};

extern int vip_sched_init(void);
extern void vip_init_task(struct task_struct *p);
extern void vip_init_rq(int cpu);
extern void vip_sched_enqueue_task_fair(struct rq *rq,
		struct task_struct *p, int flags);
extern void vip_sched_dequeue_task_fair(struct rq *rq,
		struct task_struct *p, int flags);
extern void vip_sched_select_task_rq_fair(struct task_struct *p,
		int prev_cpu, int sd_flag, int wake_flags, int *target_cpu);
extern void vip_sched_replace_next_task_fair(struct rq *rq,
		struct task_struct **p, struct sched_entity **se,
		bool *repick, bool simple, struct task_struct *prev);
extern void vip_sched_check_preempt_wakeup(struct rq *rq,
		struct task_struct *p, bool *preempt, bool *nopreempt,
		int wake_flags, struct sched_entity *se, struct sched_entity *pse,
		int next_buddy_marked);
extern void vip_sched_schedule(struct task_struct *prev,
		struct task_struct *next, struct rq *rq);
extern void vip_sched_scheduler_tick(struct rq *rq);
extern int vip_sched_newidle_balance(struct rq *rq);
extern bool is_vip_task(struct task_struct *p);
extern int set_vip_prio(struct task_struct *p, int prio, bool set_orig);
extern int set_vip_prio_locked(struct task_struct *p, int prio, bool set_orig);
extern void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se);

#if IS_ENABLED(CONFIG_XRING_SCHED_VIP)
static inline struct task_struct *vip_to_task(struct vip_task *vip)
{
	struct xr_task_struct *tmp = container_of(vip, struct xr_task_struct, vip);
	struct task_struct *p = xrts_to_ts(tmp);

	return p;
}

static inline struct vip_task *task_to_vip(struct task_struct *p)
{
	struct xr_task_struct *tmp = (struct xr_task_struct *)p->android_vendor_data1;
	struct vip_task *vip = &tmp->vip;

	return vip;
}

static inline struct task_struct *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_vpid(pid) : current;
}
#endif

#endif /* _VIP_SCHED_H */
