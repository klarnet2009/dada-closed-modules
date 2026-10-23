/* SPDX-License-Identifier: GPL-2.0 */
/* sched-walt / metis providers as seen by miloadtrace (shared by cpugov_mi.c and frame_weight.c) */
#ifndef _WALT_EXT_H
#define _WALT_EXT_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

/* the scheduler's runqueue: only the members miloadtrace touches (layout of the 6.6.57 GKI build) */
struct rq {
	raw_spinlock_t __lock;
	char __pad0[0xaa8 - sizeof(raw_spinlock_t)];
	struct task_struct *curr;
	struct task_struct *idle;
	char __pad1[0xb58 - 0xab0 - sizeof(void *)];
	unsigned long cpu_capacity;
	unsigned long cpu_capacity_orig;
	char __pad2[0xbb8 - 0xb60 - sizeof(unsigned long)];
	int cpu;
};

/* struct walt_task_struct (sched-walt), only what miloadtrace touches */
struct walt_task_struct {
	u32 flags;
	u64 mark_start;
	u64 window_start;
	char __pad[0x180 - 0x18];
	u8 enqueue_after_migration;
};

struct walt_sched_cluster {
	raw_spinlock_t load_lock;
	struct list_head list;
	struct cpumask cpus;
	int id;
};

struct walt_rq {
	struct task_struct *push_task;
	struct walt_sched_cluster *cluster;
};

extern struct list_head cluster_head;
DECLARE_PER_CPU(struct rq, runqueues);
#define cpu_rq(cpu)	(&per_cpu(runqueues, (cpu)))
DECLARE_PER_CPU(struct walt_rq, walt_rq);

struct migov_callback {
	void (*func)(struct migov_callback *cb, u64 time, int flags);
};

DECLARE_PER_CPU(struct migov_callback *, migov_cb_data);

extern int skip_walt_result[2];

u64 walt_sched_clock(void);
u64 get_scale_exec_time(u64 delta, int cpu, struct walt_task_struct *wts);
bool is_new_task(struct task_struct *p);
bool cpu_is_waiting_on_io(struct rq *rq);
void walt_update_task_ravg(struct task_struct *p, struct rq *rq, int event, u64 wallclock, u64 irqtime);

typedef void (*mi_update_fravg_hooks)(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime, int event);
typedef unsigned int (*walt_window_update_f)(int cpu);
typedef void (*mi_update_group_nr_hooks)(struct task_struct *p, int event, int cpu);
typedef void (*mi_migrate_fload_f)(bool mi_up, struct task_struct *p, int cpu, bool new_task, bool inter_cluster);
typedef void (*mi_run_frequpdate_f)(struct rq *rq, int flags);

void register_mi_update_fravg(mi_update_fravg_hooks f);
void unregister_mi_update_fravg(void);
void register_walt_update_hooks(walt_window_update_f f);
void unregister_walt_update_hooks(void);
void register_mi_update_group_nr(mi_update_group_nr_hooks f);
void unregister_mi_update_group_nr(void);
void register_mi_migrate_hook(mi_migrate_fload_f f);
void unregister_mi_migrate_hook(void);
typedef void (*mi_migarte_fix_fld_f)(struct rq *rq);
void register_mi_migarte_fix_fld_hooks(mi_migarte_fix_fld_f f);
void unregister_mi_migarte_fix_fld_hooks(void);

/* metis */
typedef bool (*task_detect_f)(struct task_struct *p);
void register_ui_task_detect(int type, task_detect_f ui_detect, task_detect_f ui_pipeline_detect);
void unregister_ui_task_detect(int type);

void register_mi_run_frequpdate_hooks(mi_run_frequpdate_f f);
void unregister_mi_run_frequpdate_hooks(void);

#endif /* _WALT_EXT_H */
