/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xr_qs (Xiaomi QoS sched driver, OS4): shared definitions of qs.c / sl.c.
 * Reconstructed from the stock binary (XRING qos_sched.h / qos_inherit.h as vocabulary).
 */
#ifndef _XR_QS_H
#define _XR_QS_H

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/types.h>

/* provided by xr_qi */
extern int register_qievent_notifier(struct notifier_block *nb);
extern int unregister_qievent_notifier(struct notifier_block *nb);
extern void update_static_prio(struct task_struct *task, int prio, int vip_prio);
extern u32 get_static_qos_lvl(struct task_struct *task);
extern int get_task_prio(struct task_struct *task);
extern bool is_qos_task(struct task_struct *task);
extern int get_proc_static_qos_lvl(struct task_struct *task);
/* provided by vip_sched */
extern int set_vip_prio(struct task_struct *p, int prio, bool set_orig);
extern int get_vip_prio(struct task_struct *p);
extern void restore_vip_prio(struct task_struct *p);
/* provided by plat_common */
extern int plat_sched_set_group_id(struct task_struct *p, unsigned int grp_id);
extern unsigned int plat_sched_get_group_id(struct task_struct *p);
extern bool plat_game_vip_check(struct task_struct *p);

enum qievent_notif_type {
	QIEVENT_QOS_SET = 1,
	QIEVENT_QOS_UNSET,
	QIEVENT_QOS_RESTORE,
	QIEVENT_QOS_INHERIT,
	QIEVENT_QOS_UNINHERIT,
	QIEVENT_QOS_CORRECT,
	QIEVENT_TASK_EXIT,
};

union qos_union {
	u32 qos;
	struct {
		u32 qos_lvl : 4;
		u32 sched_opt : 1;
		u32 vip_sched_qos : 3;
		u32 reserved : 24;
	} bits;
};

struct qievent_struct {
	struct task_struct *from;
	struct task_struct *to;
	int prio;
	int vip_prio;
	union qos_union from_qos;
	union qos_union to_qos;
};

struct qs_struct {
	u32 rt_sched : 1;
	u32 rt_sched_reason : 2;
	u32 vip_sched : 1;
	u32 vip_sched_reason : 2;
	u32 policy_backup : 8;
	u32 speedlock_flag : 1;
	u32 rtg_set_flag : 1;
	u32 prio_backup : 8;
	u32 reserved : 8;
};

struct qs_vendor {
	u8 pad[0x40];	/* other vendor users of the dynamic task_struct tail */
	struct qs_struct qs;
};

static inline struct qs_struct *task_to_qs(struct task_struct *p)
{
	struct qs_vendor *v = android_task_vendor_data(p);
	struct qs_struct *qs;

	if (!v)
		return NULL;
	qs = &v->qs;
	if (!qs)
		return NULL;
	return qs;
}

#endif /* _XR_QS_H */
