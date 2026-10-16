/* SPDX-License-Identifier: GPL-2.0 */
/* sched-walt / metis API surface imported by miloadtrace (stub providers).  struct rq / walt_task_struct stay opaque. */
#ifndef _MI_EXT_WALT_API_H
#define _MI_EXT_WALT_API_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

struct rq;
struct walt_task_struct;

typedef void (*mi_update_fravg_hooks)(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime, int event);
typedef unsigned int (*walt_window_update_f)(int cpu);
typedef void (*mi_update_group_nr_hooks)(struct task_struct *p, int event, int cpu);
typedef void (*mi_migrate_fload_f)(bool mi_up, struct task_struct *p, int cpu, bool new_task, bool inter_cluster);
typedef void (*mi_migarte_fix_fld_f)(struct rq *rq);
typedef void (*mi_run_frequpdate_f)(struct rq *rq, int flags);
typedef bool (*task_detect_f)(struct task_struct *p);

u64 walt_sched_clock(void);
u64 get_scale_exec_time(u64 delta, int cpu, struct walt_task_struct *wts);
bool is_new_task(struct task_struct *p);
int cpu_is_waiting_on_io(struct rq *rq);
void walt_update_task_ravg(struct task_struct *p, struct rq *rq, int event, u64 wallclock, u64 irqtime);

void register_mi_update_fravg(mi_update_fravg_hooks f);
void unregister_mi_update_fravg(void);
void register_walt_update_hooks(walt_window_update_f f);
void unregister_walt_update_hooks(void);
void register_mi_update_group_nr(mi_update_group_nr_hooks f);
void unregister_mi_update_group_nr(void);
void register_mi_migrate_hook(mi_migrate_fload_f f);
void unregister_mi_migrate_hook(void);
void register_mi_migarte_fix_fld_hooks(mi_migarte_fix_fld_f f);
void unregister_mi_migarte_fix_fld_hooks(void);
void register_mi_run_frequpdate_hooks(mi_run_frequpdate_f f);
void unregister_mi_run_frequpdate_hooks(void);

void register_ui_task_detect(int type, task_detect_f ui_detect, task_detect_f ui_pipeline_detect);
void unregister_ui_task_detect(int type);

#endif
