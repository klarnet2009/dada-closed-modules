// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of the mi_schedule symbols imported by miloadtrace. */
#ifdef __GENKSYMS__
void get_cluster_type(char (*)[sizeof("|:c8w77%")]); /* CRC forged: see crcforge.py */
void mi_task_exiting(char (*)[sizeof("Dg:+jWx^")]); /* CRC forged: see crcforge.py */
void get_mi_task_struct(char (*)[sizeof("VSk7q{5T")]); /* CRC forged: see crcforge.py */
void get_mi_task_struct_unsafe(char (*)[sizeof("n&039#6E")]); /* CRC forged: see crcforge.py */
void find_mi_group_by_task(char (*)[sizeof("T;~Xs+yg")]); /* CRC forged: see crcforge.py */
void mi_group_ref_dec(char (*)[sizeof("-EzrTMfu")]); /* CRC forged: see crcforge.py */
void get_mi_system_group(char (*)[sizeof("Z`IY<?Ix")]); /* CRC forged: see crcforge.py */
void get_mi_back_group(char (*)[sizeof("rxc28lOF")]); /* CRC forged: see crcforge.py */
void get_mi_kern_group(char (*)[sizeof("jwHeOxPR")]); /* CRC forged: see crcforge.py */
void find_mi_group_by_user(char (*)[sizeof("<!@r>np$")]); /* CRC forged: see crcforge.py */
void get_mi_user_struct(char (*)[sizeof(".}.GwOlZ")]); /* CRC forged: see crcforge.py */
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <mi_ext/mi_sched.h>

#ifdef __GENKSYMS__
extern char mi_runqueues[sizeof("sWQf4p5n")]; /* CRC forged: see crcforge.py */
#else
DEFINE_PER_CPU(struct mi_rq, mi_runqueues);
#endif
EXPORT_PER_CPU_SYMBOL(mi_runqueues);

spinlock_t period_freq_list_lock;
EXPORT_SYMBOL(period_freq_list_lock);

#ifndef __GENKSYMS__
enum cluster_type get_cluster_type(int cpu)
{
	return LITTLE_CLUSTER;
}
#endif
EXPORT_SYMBOL(get_cluster_type);

u64 get_pred_target_period(unsigned int fps)
{
	return 0;
}
EXPORT_SYMBOL(get_pred_target_period);

#ifndef __GENKSYMS__
bool mi_task_exiting(struct task_struct *p)
{
	return false;
}
#endif
EXPORT_SYMBOL(mi_task_exiting);

#ifndef __GENKSYMS__
struct mi_task_struct *get_mi_task_struct(struct task_struct *p)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_task_struct);

#ifndef __GENKSYMS__
struct mi_task_struct *get_mi_task_struct_unsafe(struct task_struct *p)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_task_struct_unsafe);

#ifndef __GENKSYMS__
struct mi_group *find_mi_group_by_task(struct task_struct *p, struct mi_task_struct *mi_tsk)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(find_mi_group_by_task);

#ifndef __GENKSYMS__
void mi_group_ref_dec(struct mi_group *grp)
{
}
#endif
EXPORT_SYMBOL(mi_group_ref_dec);

struct list_head *get_active_mi_groups(void)
{
	return NULL;
}
EXPORT_SYMBOL(get_active_mi_groups);

#ifndef __GENKSYMS__
struct mi_group *get_mi_system_group(void)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_system_group);

#ifndef __GENKSYMS__
struct mi_group *get_mi_back_group(void)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_back_group);

#ifndef __GENKSYMS__
struct mi_group *get_mi_kern_group(void)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_kern_group);

#ifndef __GENKSYMS__
struct mi_group *find_mi_group_by_user(struct mi_user_struct *user)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(find_mi_group_by_user);

#ifndef __GENKSYMS__
struct mi_user_struct *get_mi_user_struct(struct user_struct *user)
{
	return NULL;
}
#endif
EXPORT_SYMBOL(get_mi_user_struct);

void register_task_group_change_update_f(task_group_change_update_f f)
{
}
EXPORT_SYMBOL(register_task_group_change_update_f);

void unregister_task_group_change_update_f(void)
{
}
EXPORT_SYMBOL(unregister_task_group_change_update_f);

void register_mi_update_global_period_f(mi_update_global_period_f f)
{
}
EXPORT_SYMBOL(register_mi_update_global_period_f);

void unregister_mi_update_global_period_f(void)
{
}
EXPORT_SYMBOL(unregister_mi_update_global_period_f);

MODULE_LICENSE("GPL");
