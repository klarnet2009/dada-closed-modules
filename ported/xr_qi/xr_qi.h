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
	u64 reserved;
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
	u8 f1_lo : 4;
	u8 f1_b4 : 1;
	u8 iter_depth : 2;	/* number of visited requests of the current inherit chain */
	u8 f1_top : 1;
	u16 f2;
	int f4;
	u32 futex_owner;	/* +8 */
	u8 pad_c[4];
	void *req;		/* +0x10 */
	union {
		void *iter[3];	/* +0x18: requests already visited in this chain */
		struct {
			void *iter_ptr;
			void *iter_a;
			void *iter_b;
		};
	};
	int f30;
	spinlock_t lock;	/* +0x34 */
	struct qi_struct *qi;
};

/* one entry of qi_struct.inherit[type] */
struct inherit_node {
	struct list_head list;
	void *owner;
	int prio;
	u32 qos;
};

struct gpu_blocked_info {
	pid_t pid;
	u64 blocked_ns;
};

struct gpu_blocked_node {
	struct list_head list;
	pid_t pid;
	u64 start;
	u32 type;	/* +0x20: third word of the user event (kzalloc size 40 in the stock) */
};

/* argument of the XR_FBT_QOS_EVENT callback (12 bytes, copied from user space) */
struct qi_gpu_event {
	pid_t pid;
	u32 unblocked;	/* 0: the task blocks on the gpu, 1: it is released */
	u32 type;
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
void iter_qos_inherit(struct task_struct *p, void *ptr);
int do_qos_inherit(struct task_struct *from, struct task_struct *to, void *ptr, u8 type);
#ifndef QI_NO_UNDO_MACRO
/* G11b: the stock prototype is int (struct task_struct *, struct task_struct *, void *, u8) (kCFI 0x12ff2bc8, found with Itanium substitutions S0_) */
#define do_qos_inherit(f, t, ptr, type) do_qos_inherit(f, t, (void *)(unsigned long)(ptr), type)
#endif
int undo_qos_inherit(struct task_struct *p, struct qi_struct *qi, void *ptr, u8 type);
#ifndef QI_NO_UNDO_MACRO
/* G11: the stock prototype takes `void *` (kCFI 0x86fb9f9a); the callers of this file pass the key as an integer */
#define undo_qos_inherit(p, qi, ptr, type) undo_qos_inherit(p, qi, (void *)(unsigned long)(ptr), type)
#endif
u32 get_task_qos(struct task_struct *task);
u32 get_dyn_qos_lvl(struct task_struct *task);
extern unsigned int sysctl_mutex_qos_opt_spin_time_ns;
extern unsigned int sysctl_mutex_normal_opt_spin_time_ns;
struct qi_struct *get_task_qi(struct task_struct *p);
struct qi_struct *get_alloc_task_qi(struct task_struct *p);
int register_qievent_notifier(struct notifier_block *nb);
int unregister_qievent_notifier(struct notifier_block *nb);
void update_static_prio(struct task_struct *task, int prio, int vip_prio GKPAD(fcrc5KDk031J1g));
u32 get_static_qos_lvl(struct task_struct *task GKPAD(fcrc63JozGiuwN));
int get_task_prio(struct task_struct *task GKPAD(fcrcr2lXe04u7v));
u32 get_qos_lvl(struct task_struct *task GKPAD(fcrcG4u98hqq6J));
u32 get_task_qos_lvl(struct task_struct *task GKPAD(fcrcaIh0tGelF4));
bool is_qos_task(struct task_struct *task GKPAD(fcrcnTDbsZh2d8));
bool is_qos_rt_task(struct task_struct *task);
int get_proc_static_qos_lvl(struct task_struct *task GKPAD(fcrcowoRoWNTBR));
bool qi_is_gpu_blocked_task(struct task_struct *task GKPAD(fcrcXgucAeK9kc));
pid_t qi_gpu_blocked_longest(void);
int kern_qi_set_qos(struct task_struct *task, u32 qos GKPAD(fcrcaNQafAojQ5));
struct mutex;
struct mutex_waiter;
void os_cpu_android_vh_alter_mutex_list_add(void *unused, struct mutex *lock, struct mutex_waiter *waiter,
					    struct list_head *list, bool *already_on_list GKPAD(fcrcpSKVvx3Mb5));
int qi_acquire_gpu_blockeds(struct gpu_blocked_info *info, u32 max);
extern spinlock_t g_stats_lock;
extern struct list_head g_gpu_blocked_list;

#endif /* _XR_QI_H */
