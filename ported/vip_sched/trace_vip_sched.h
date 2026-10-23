/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024-2024, X-Ring technologies Inc., All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vip_sched

#if !defined(_TRACE_VIP_SCHED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VIP_SCHED_H

#include <linux/tracepoint.h>

#include "vip_sched.h"

TRACE_EVENT(find_emerg_cpu,

	TP_PROTO(int cpu, int vip_preempt, int nr_running),

	TP_ARGS(cpu, vip_preempt, nr_running),

	TP_STRUCT__entry(
		__field(int,	cpu)
		__field(int,	vip_preempt)
		__field(int,	nr_running)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->vip_preempt	= vip_preempt;
		__entry->nr_running		= nr_running;
	),

	TP_printk("emerg=%d preempt=%d nr=%d", __entry->cpu,
		__entry->vip_preempt, __entry->nr_running)
);

TRACE_EVENT(vip_balance,

	TP_PROTO(struct vb_env *env),

	TP_ARGS(env),

	TP_STRUCT__entry(
		__field(int,	src_cpu)
		__field(int,	dst_cpu)
		__field(pid_t,	pid)
	),

	TP_fast_assign(
		__entry->src_cpu	= env->src_cpu;
		__entry->dst_cpu	= env->dst_cpu;
		__entry->pid		= env->task ? env->task->pid : -1;
	),

	TP_printk("src_cpu=%d dst_cpu=%d task=%d", __entry->src_cpu,
		__entry->dst_cpu, __entry->pid)
);

TRACE_EVENT(kick_vip_balance,

	TP_PROTO(int src_cpu, int dst_cpu, pid_t pid, bool kick_idle),

	TP_ARGS(src_cpu, dst_cpu, pid, kick_idle),

	TP_STRUCT__entry(
		__field(int,	src_cpu)
		__field(int,	dst_cpu)
		__field(pid_t,	pid)
		__field(bool,	kick_idle)
	),

	TP_fast_assign(
		__entry->src_cpu		= src_cpu;
		__entry->dst_cpu		= dst_cpu;
		__entry->pid			= pid;
		__entry->kick_idle		= kick_idle;
	),

	TP_printk("src_cpu=%d dst_cpu=%d task=%d%s", __entry->src_cpu,
		__entry->dst_cpu, __entry->pid, __entry->kick_idle ? " idle" : "")
);

TRACE_EVENT(find_vip_cpu,

	TP_PROTO(pid_t pid, struct cpumask *mask, int cpu,
		const struct cpumask *cpus_ptr, struct cpumask *halt_mask,
		struct cpumask *rtg_mask, bool repick),

	TP_ARGS(pid, mask, cpu, cpus_ptr, halt_mask, rtg_mask, repick),

	TP_STRUCT__entry(
		__field(pid_t,			pid)
		__field(int,			cpu)
		__field(bool,			repick)
		__field(unsigned long,  target_mask)
		__field(unsigned long,  allowed_mask)
		__field(unsigned long,  halt_mask)
		__field(unsigned long,  rtg_mask)
	),

	TP_fast_assign(
		__entry->pid		= pid;
		__entry->cpu		= cpu;
		__entry->repick		= repick;
		__entry->target_mask = cpumask_bits(mask)[0];
		__entry->allowed_mask = cpumask_bits(cpus_ptr)[0];
		__entry->halt_mask = cpumask_bits(halt_mask)[0];
		__entry->rtg_mask = cpumask_bits(rtg_mask)[0];
	),

	TP_printk("pid=%d mask=0x%lx target=%d repick=%d allowed_mask=0x%lx halt_mask=0x%lx rtg_mask=0x%lx",
			__entry->pid, __entry->target_mask, __entry->cpu, __entry->repick, __entry->allowed_mask,
			__entry->halt_mask, __entry->rtg_mask)
);

TRACE_EVENT(queue_vip_task,

	TP_PROTO(bool enqueue, int cpu, struct vip_rq *vrq, pid_t pid),

	TP_ARGS(enqueue, cpu, vrq, pid),

	TP_STRUCT__entry(
		__field(bool,	enqueue)
		__field(int,	cpu)
		__field(int,	h_prio)
		__field(int,	nr_running)
		__field(pid_t,	pid)
	),

	TP_fast_assign(
		__entry->enqueue			= enqueue;
		__entry->cpu	= cpu;
		__entry->h_prio		= vrq->h_prio;
		__entry->nr_running		= vrq->nr_running;
		__entry->pid		= pid;
	),

	TP_printk("%s: cpu=%d h_prio=%d nr_runing=%d pid=%d",
			__entry->enqueue ? "enq" : "deq", __entry->cpu,
			__entry->h_prio, __entry->nr_running, __entry->pid)
);

TRACE_EVENT(vip_replace_next_task,

	TP_PROTO(struct task_struct *p, int vip_prio, pid_t orig_pid, u64 wait_time),

	TP_ARGS(p, vip_prio, orig_pid, wait_time),

	TP_STRUCT__entry(
		__field(pid_t,			pid)
		__field(int,			vip_prio)
		__field(unsigned long,  mask)
		__field(pid_t,			orig_pid)
		__field(u64,			wait_time)
	),

	TP_fast_assign(
		__entry->pid		= p->pid;
		__entry->vip_prio	= vip_prio;
		__entry->mask = cpumask_bits(p->cpus_ptr)[0];
		__entry->orig_pid	= orig_pid;
		__entry->wait_time	= wait_time;
	),

	TP_printk("pid=%d vip_prio=%d mask=0x%lx orig_pid=%d wait_time=%llu", __entry->pid,
		__entry->vip_prio, __entry->mask, __entry->orig_pid, __entry->wait_time)
);

TRACE_EVENT(vip_preempt_curr,

	TP_PROTO(pid_t curr_pid, pid_t pid, int vip_prio),

	TP_ARGS(curr_pid, pid, vip_prio),

	TP_STRUCT__entry(
		__field(pid_t,	curr_pid)
		__field(pid_t,	pid)
		__field(int,	vip_prio)
	),

	TP_fast_assign(
		__entry->curr_pid	= curr_pid;
		__entry->pid		= pid;
		__entry->vip_prio	= vip_prio;
	),

	TP_printk("curr=%d vip=%d prio=%d", __entry->curr_pid,
		__entry->pid, __entry->vip_prio)
);

TRACE_EVENT(vip_schedule,

	TP_PROTO(pid_t p_pid, int p_prio, pid_t n_pid, int n_prio,
		int vip_preempt, int n_util),

	TP_ARGS(p_pid, p_prio, n_pid, n_prio, vip_preempt, n_util),

	TP_STRUCT__entry(
		__field(pid_t,	p_pid)
		__field(int,	p_prio)
		__field(pid_t,	n_pid)
		__field(pid_t,	n_prio)
		__field(int,	vip_preempt)
		__field(int,	n_util)
	),

	TP_fast_assign(
		__entry->p_pid			= p_pid;
		__entry->p_prio			= p_prio;
		__entry->n_pid			= n_pid;
		__entry->n_prio			= n_prio;
		__entry->vip_preempt	= vip_preempt;
		__entry->n_util			= n_util;
	),

	TP_printk("prev=%d p_prio=%d next=%d n_prio=%d preempt=%d n_util=%d",
			__entry->p_pid, __entry->p_prio, __entry->n_pid, __entry->n_prio,
			__entry->vip_preempt, __entry->n_util)
);

TRACE_EVENT(set_vip_prio,

	TP_PROTO(pid_t pid, int prio, int orig_prio),

	TP_ARGS(pid, prio, orig_prio),

	TP_STRUCT__entry(
		__field(pid_t,	pid)
		__field(int,	prio)
		__field(int,	orig_prio)
	),

	TP_fast_assign(
		__entry->pid			= pid;
		__entry->prio			= prio;
		__entry->orig_prio		= orig_prio;
	),

	TP_printk("pid=%d prio=%d orig_prio=%d",
			__entry->pid, __entry->prio, __entry->orig_prio)
);

TRACE_EVENT(can_migrate_vip,

	TP_PROTO(int cpu, pid_t pid, int prio, bool migrate, int reason),

	TP_ARGS(cpu, pid, prio, migrate, reason),

	TP_STRUCT__entry(
		__field(int,	cpu)
		__field(pid_t,	pid)
		__field(int,	prio)
		__field(bool,	migrate)
		__field(int,	reason)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->pid			= pid;
		__entry->prio			= prio;
		__entry->migrate		= migrate;
		__entry->reason			= reason;
	),

	TP_printk("cpu=%d pid=%d prio=%d migrate=%d reason=%d",
			__entry->cpu, __entry->pid, __entry->prio, __entry->migrate,
			__entry->reason)
);

#endif /* _TRACE_VIP_SCHED_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_vip_sched

#include <trace/define_trace.h>
