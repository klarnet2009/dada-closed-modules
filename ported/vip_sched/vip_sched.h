/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * vip_sched (Xiaomi QOS sched driver), OS4 Qualcomm port of the XRING vip_sched.
 * Reconstructed from the stock binary; base template: XRING walt/vip_sched.{c,h}.
 */
#ifndef _VIP_SCHED_H
#define _VIP_SCHED_H

#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/sched/task.h>
#include <linux/sched/rt.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include "ksched/sched.h"

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

struct vip_task {
	int prio;
	int orig_prio;
	struct list_head entry;
};

struct vip_vendor {
	u8 pad[0x48];	/* other vendor users of the dynamic task_struct tail */
	struct vip_task vip;
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
	u64 last_grace_ts;
};

/* provided by plat_common */
extern unsigned long plat_task_util(struct task_struct *p);
extern bool plat_cpu_halted(int cpu);
extern const struct cpumask *plat_cpu_halt_mask(void);
extern unsigned long plat_capacity_orig_of(int cpu);
extern int plat_find_idle_cpu_in_freq_domain(int cpu);
extern void plat_get_group_prefer_cpus(struct task_struct *p, struct cpumask *prefer_cpus);
extern unsigned long plat_get_group_prefer_util(struct task_struct *p);
extern int plat_migt_enable(void);
/* provided by perf_actuator */
typedef int (*PERF_ACTUATOR_FUNC)(void *);
extern int register_perf_actuator(unsigned int cmd, PERF_ACTUATOR_FUNC func);

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

static inline struct task_struct *vip_to_task(struct vip_task *vip)
{
	struct vip_vendor *v = container_of(vip, struct vip_vendor, vip);

	if (!v)
		return NULL;
	if ((void *)v == (void *)&vendor_data_pad[0])
		return &init_task;
	return (struct task_struct *)((void *)v) - 1;
}

static inline struct vip_task *task_to_vip(struct task_struct *p)
{
	struct vip_vendor *v = android_task_vendor_data(p);
	struct vip_task *vip;

	if (!v)
		return NULL;
	vip = &v->vip;
	if (!vip)
		return NULL;
	return vip;
}

static inline struct task_struct *find_process_by_pid(pid_t pid)
{
	return pid ? find_task_by_vpid(pid) : current;
}

#endif /* _VIP_SCHED_H */
