// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of the sched-walt symbols imported by miloadtrace. */
#ifdef __GENKSYMS__
void get_scale_exec_time(char (*)[sizeof("s7--eRb;")]); /* CRC forged: see crcforge.py */
void is_new_task(char (*)[sizeof("#a>|2i<1")]); /* CRC forged: see crcforge.py */
void cpu_is_waiting_on_io(char (*)[sizeof("&&bAXKQ,")]); /* CRC forged: see crcforge.py */
void walt_update_task_ravg(char (*)[sizeof("/P,Yy;.8")]); /* CRC forged: see crcforge.py */
void register_mi_update_fravg(char (*)[sizeof("Ar[k3S7.")]); /* CRC forged: see crcforge.py */
void register_mi_update_group_nr(char (*)[sizeof("ULxp`dPT")]); /* CRC forged: see crcforge.py */
void register_mi_migrate_hook(char (*)[sizeof("YkA|SaX'")]); /* CRC forged: see crcforge.py */
void register_mi_migarte_fix_fld_hooks(char (*)[sizeof("kOxjm}A4")]); /* CRC forged: see crcforge.py */
void register_mi_run_frequpdate_hooks(char (*)[sizeof("h7LPhAk#")]); /* CRC forged: see crcforge.py */
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <mi_ext/walt_api.h>

#ifdef __GENKSYMS__
extern char walt_rq[sizeof(":%m6dYwW")]; /* CRC forged: see crcforge.py */
#else
struct walt_sched_cluster;
struct walt_rq {
	struct task_struct *push_task;
	struct walt_sched_cluster *cluster;
};

DEFINE_PER_CPU(struct walt_rq, walt_rq);
#endif
EXPORT_PER_CPU_SYMBOL(walt_rq);

struct list_head cluster_head = LIST_HEAD_INIT(cluster_head);
EXPORT_SYMBOL(cluster_head);

struct cpumask __cpu_partial_halt_mask;
EXPORT_SYMBOL(__cpu_partial_halt_mask);

#ifdef __GENKSYMS__
extern char part_haltable_cpus[sizeof("{#^;3*>A")]; /* CRC forged: see crcforge.py */
#else
struct cpumask part_haltable_cpus;
#endif
EXPORT_SYMBOL(part_haltable_cpus);

struct cpumask __cpu_halt_mask;
EXPORT_SYMBOL(__cpu_halt_mask);

u64 walt_sched_clock(void)
{
	return 0;
}
EXPORT_SYMBOL(walt_sched_clock);

#ifndef __GENKSYMS__
u64 get_scale_exec_time(u64 delta, int cpu, struct walt_task_struct *wts)
{
	return delta;
}
#endif
EXPORT_SYMBOL(get_scale_exec_time);

#ifndef __GENKSYMS__
bool is_new_task(struct task_struct *p)
{
	return false;
}
#endif
EXPORT_SYMBOL(is_new_task);

#ifndef __GENKSYMS__
int cpu_is_waiting_on_io(struct rq *rq)
{
	return 0;
}
#endif
EXPORT_SYMBOL(cpu_is_waiting_on_io);

#ifndef __GENKSYMS__
void walt_update_task_ravg(struct task_struct *p, struct rq *rq, int event, u64 wallclock, u64 irqtime)
{
}
#endif
EXPORT_SYMBOL(walt_update_task_ravg);

#ifndef __GENKSYMS__
void register_mi_update_fravg(mi_update_fravg_hooks f)
{
}
#endif
EXPORT_SYMBOL(register_mi_update_fravg);

void unregister_mi_update_fravg(void)
{
}
EXPORT_SYMBOL(unregister_mi_update_fravg);

void register_walt_update_hooks(walt_window_update_f f)
{
}
EXPORT_SYMBOL(register_walt_update_hooks);

void unregister_walt_update_hooks(void)
{
}
EXPORT_SYMBOL(unregister_walt_update_hooks);

#ifndef __GENKSYMS__
void register_mi_update_group_nr(mi_update_group_nr_hooks f)
{
}
#endif
EXPORT_SYMBOL(register_mi_update_group_nr);

void unregister_mi_update_group_nr(void)
{
}
EXPORT_SYMBOL(unregister_mi_update_group_nr);

#ifndef __GENKSYMS__
void register_mi_migrate_hook(mi_migrate_fload_f f)
{
}
#endif
EXPORT_SYMBOL(register_mi_migrate_hook);

void unregister_mi_migrate_hook(void)
{
}
EXPORT_SYMBOL(unregister_mi_migrate_hook);

#ifndef __GENKSYMS__
void register_mi_migarte_fix_fld_hooks(mi_migarte_fix_fld_f f)
{
}
#endif
EXPORT_SYMBOL(register_mi_migarte_fix_fld_hooks);

void unregister_mi_migarte_fix_fld_hooks(void)
{
}
EXPORT_SYMBOL(unregister_mi_migarte_fix_fld_hooks);

#ifndef __GENKSYMS__
void register_mi_run_frequpdate_hooks(mi_run_frequpdate_f f)
{
}
#endif
EXPORT_SYMBOL(register_mi_run_frequpdate_hooks);

void unregister_mi_run_frequpdate_hooks(void)
{
}
EXPORT_SYMBOL(unregister_mi_run_frequpdate_hooks);

MODULE_LICENSE("GPL");
