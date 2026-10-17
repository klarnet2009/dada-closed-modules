/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RTG_H
#define _RTG_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/rcupdate.h>
#include <linux/cpumask.h>
#include <linux/sched.h>

#define DEFAULT_CGROUP_COLOC_ID		1
#define MAX_NUM_CGROUP_COLOC_ID		20

/* WALT group as extended in OS4 (stock BTF: 104 bytes, rtg_prefer_cpus at +88, rtg_util at +96) */
struct walt_related_thread_group {
	int			id;
	raw_spinlock_t		lock;
	struct list_head	tasks;
	struct list_head	list;
	bool			skip_min;
	struct rcu_head		rcu;
	u64			last_update;
	u64			downmigrate_ts;
	u64			start_ktime_ts;
	cpumask_t		rtg_prefer_cpus;
	u64			rtg_util;
};

/* walt_task_struct (task->android_vendor_data1): only the group members are used here (grp at +0xf0) */
struct walt_task_struct_min {
	u8			pad[0xf0];
	struct walt_related_thread_group __rcu *grp;
	struct list_head	grp_list;
};

#define grp_prefer(g)	((u32)(g)->rtg_prefer_cpus.bits[0])
#define wts_of(p)	((struct walt_task_struct_min *)(p)->android_vendor_data1)
#define wts_to_ts(wts)	((struct task_struct *)((void *)(wts) - offsetof(struct task_struct, android_vendor_data1)))

/* ioctl structures of the perf_actuator rtg commands */
struct rtg_task_arg { int pid; int grp_id; };
#define RTG_TASK_ARRAY_MAX	15
struct rtg_task_array_arg { int grp_id; unsigned int nr; int pid[RTG_TASK_ARRAY_MAX]; };
struct rtg_cpus_arg { int grp_id; unsigned int cpus; };
struct rtg_util_arg { int grp_id; unsigned int util; };

#define PERF_ACTUATOR_SET_TASK_RTG		_IOW('x', 2, struct rtg_task_arg)
#define PERF_ACTUATOR_SET_TASK_ARRAY_RTG	_IOW('x', 0x25, struct rtg_task_array_arg)
#define PERF_ACTUATOR_SET_RTG_CPUS		_IOW('x', 3, struct rtg_cpus_arg)
#define PERF_ACTUATOR_SET_RTG_UTIL		_IOW('x', 4, struct rtg_util_arg)
#define PERF_ACTUATOR_CLEAR_RTG			_IOW('x', 7, unsigned int)

/* provided by plat_common */
extern struct walt_related_thread_group *plat_lookup_related_thread_group(unsigned int grp_id);
extern int plat_sched_set_group_id(struct task_struct *p, unsigned int grp_id);
extern void plat_core_ctl_update(void);
/* provided by perf_actuator */
typedef int (*PERF_ACTUATOR_FUNC)(void *);
extern int register_perf_actuator(unsigned int cmd, PERF_ACTUATOR_FUNC func);

struct walt_related_thread_group *get_related_thread_group(unsigned int grp_id);

#endif
