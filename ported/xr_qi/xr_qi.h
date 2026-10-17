/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xr_qi (Xiaomi QoS inherit driver, OS4): shared definitions. Reconstructed from the stock binary
 * (XRING soc/xring/qos_inherit.h as vocabulary; struct layouts from the stock BTF/disassembly).
 */
#ifndef _XR_QI_H
#define _XR_QI_H

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* GENKSYMS-ONLY pad: forges the export CRC of the stock prototype (g1c_tools/crcpad.py); compiled out */
#ifdef __GENKSYMS__
#define GKPAD(tag)	, struct tag *gkpad
#else
#define GKPAD(tag)
#endif

enum {
	QOS_BINDER,
	QOS_MUTEX,
	QOS_RWSEM,
	QOS_PERCPU_RWSEM,
	QOS_FUTEX,
	QOS_FUSE,
	QOS_RCU,
	QOS_TYPE_NUM,
	QOS_TYPE_FIRST = QOS_BINDER,
};

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

struct inherit_list {
	struct list_head list;
};

/* per-task qos state, allocated on demand (176 bytes) */
struct qi_struct {
	spinlock_t lock;
	atomic_t flag;
	atomic_t static_qos;
	atomic_t dyn_qos;
	atomic_t proc_static_qos;
	pid_t futex_owner;
	unsigned long timeout;
	struct inherit_list inherit[QOS_TYPE_NUM];
	u64 opt_spin_start_time;
	int static_prio;
	int static_vip_prio;
	int dyn_prio;
	int dyn_vip_prio;
	bool blocked_on_gpu;
	bool async_binder_todo;
};

/* part of the xiaomi vendor area of task_struct (android_task_vendor_data()) used by xr_qi */
struct qi_vendor {
	u8 f0;
	u8 f1 : 5;
	u8 iter_active : 1;
	u8 iter_x : 1;
	u8 f1_top : 1;
	u16 f2;
	int f4;
	u8 pad8[0x10];
	void *iter_ptr;
	void *iter_a;
	void *iter_b;
	u8 pad30[4];
	int f34;
	struct qi_struct *qi;
};

struct gpu_blocked_info {
	pid_t pid;
	u64 blocked_ns;
};

struct gpu_blocked_node {
	struct list_head list;
	pid_t pid;
	u64 start;
};

/* provided by perf_actuator */
typedef int (*XR_FBT_FUNC)(void *);
extern int register_xr_fbt(unsigned int cmd, XR_FBT_FUNC func);
extern int unregister_xr_fbt(unsigned int cmd);
/* provided by vip_sched */
extern int get_vip_prio(struct task_struct *p);
/* provided by plat_common */
extern void plat_free_mi_task_struct(struct task_struct *p);

bool is_qi_feat_enabled(u8 feat);
struct qi_struct *get_task_qi(struct task_struct *p);
int register_qievent_notifier(struct notifier_block *nb);
int unregister_qievent_notifier(struct notifier_block *nb);
void update_static_prio(struct task_struct *task, int prio, int vip_prio GKPAD(fcrc0sDJLyNXnI));
u32 get_static_qos_lvl(struct task_struct *task GKPAD(fcrc9MB0_bGrcl));
int get_task_prio(struct task_struct *task GKPAD(fcrc5mMF35Dihv));
u32 get_qos_lvl(struct task_struct *task GKPAD(fcrcqPOppdmcYd));
u32 get_task_qos_lvl(struct task_struct *task GKPAD(fcrcY_7sUMcsGm));
bool is_qos_task(struct task_struct *task GKPAD(fcrcUZoL8g8VvT));
int get_proc_static_qos_lvl(struct task_struct *task GKPAD(fcrch5GlIPlGdv));
bool qi_is_gpu_blocked_task(struct task_struct *task GKPAD(fcrckHqAV4SjCz));
pid_t qi_gpu_blocked_longest(void);
int kern_qi_set_qos(struct task_struct *task, u32 qos GKPAD(fcrcM8qDHkAXHp));
struct mutex;
struct mutex_waiter;
void os_cpu_android_vh_alter_mutex_list_add(void *unused, struct mutex *lock, struct mutex_waiter *waiter,
					    struct list_head *list, bool *already_on_list GKPAD(fcrcpSKVvx3Mb5));
int qi_acquire_gpu_blockeds(struct gpu_blocked_info *info, u32 max);

#endif /* _XR_QI_H */
