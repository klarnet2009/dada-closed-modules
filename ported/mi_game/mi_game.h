/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: Xiaomi game scheduler (MIGT / game-load / frame-load tracking), HyperOS 4.0 (dada).
 * Reconstructed from the OS4 stock disassembly (no public source). PARTIAL. NOT hardware-tested: DO NOT LOAD.
 * Layout offsets below are those of the stock objects (derived from load/store offsets).
 */
#ifndef _MI_GAME_H
#define _MI_GAME_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/irq_work.h>
#include <linux/ratelimit.h>
#include <linux/sysctl.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/cpufreq.h>

/* vendor enum (stock BTF): its tag name is part of the kCFI type of set/clean/reset/update_render_* */
enum MI_RENDER_TYPE { MI_RENDER_QUEUE_THREAD = 0, MI_RENDER_DEQUEUE_THREAD = 1, MI_RENDER_TYPES = 2 };

/* ---- imports from sched-walt / mist / plat_common (stub providers carry the stock CRCs) ---- */
extern unsigned int num_sched_clusters;
extern unsigned int sched_ravg_window;	/* defined (read_mostly bss) in tiny.c: the stock defines both itself */
extern int debugfs_walt_features;
enum mi_cluster_type { MI_LITTLE_CLUSTER = 0, MI_MID_CLUSTER_1 = 1, MI_MID_CLUSTER_2 = 1, MI_BIG_CLUSTER = 1, MI_CLUSTER_TYPES = 2 };
extern enum mi_cluster_type get_cluster_type(int cpu);
extern unsigned int glk_disable;
extern int game_task(struct task_struct *p);
extern int game_vip_task(struct task_struct *p);
extern int game_super_task(struct task_struct *p);
extern int is_render_thread(struct task_struct *p);
extern void mi_cpu_coregroup_mask(int cpu, struct cpumask *mask);
extern u64 walt_sched_clock(void);
struct frame_sf {	/* BTF of the stock, 32 bytes */
	s64 predSfVsync, vsyncPeriod, target_runtime;
	int buffer_count, current_uid;
};
extern int migt_enable(void);
/* imports of the sibling closed modules (stub providers carry the stock CRCs) */
extern int register_plat_func(int id, void *fn);
extern void unregister_plat_func(int id);
extern struct list_head cluster_head;	/* sched-walt: list of struct walt_sched_cluster (list node at +8, cpus at +0x18) */
extern void walt_update_task_ravg(struct task_struct *p, void *rq, int event, u64 wallclock, u64 irqtime);
struct walt_sched_cluster {
	raw_spinlock_t load_lock;
	struct list_head list;
	struct cpumask cpus;
};
struct rq {
	raw_spinlock_t __lock;	/* only the leading lock is declared; kernel/sched/sched.h is private */
};
DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);
#define RQ_CURR(rq)	(*(struct task_struct **)((u8 *)(rq) + 0xaa8))
#define RQ_CAPACITY(rq)	(*(unsigned long *)((u8 *)(rq) + 0xb60))


/* ---- flt state (frame-load tracking) ---- */
struct capacity_state {
	u64 cpu_util;
	unsigned long freq;
	unsigned long max_freq;
};
struct frame_load_items {	/* BTF of the stock: 120 bytes */
	u64 frame_start, frame_period;
	struct capacity_state cap_states[2];
	u64 last_frame_period;
	u64 real_util[2];
	u32 real_freq[2];
	u32 avg_freq[2];
	u32 sum_avg_freq[2];
	u32 need_target_delta;
};
/*
 * global_frame_info (2696 bytes): 0x60-byte header at +0 (+4/+5/+6 flags, +0x1c current slot, +0x20 enable,
 * +0x28 debug, +0x50 window), 21 frame_load_items at +0x60 (the stock indexes [22] in dump_his_load_info: see flt_view22),
 * then at +0xa58 the irq_work of flt_irq_work followed by gpu time (+0x20) and the fence-pending flag (+0x28).
 */
struct flt_global {
	u8 f0[4];
	u8 force_maxfreq_break, maxfreq_break, minfreq_break;
	u8 f7[0x1c - 7];
	int cur_idx;			/* +0x1c */
	u32 enable;			/* +0x20 */
	u32 f24;
	u32 debug;			/* +0x28 */
	u32 hispeed_load[4];		/* +0x2c */
	u32 lowspeed_load[4];		/* +0x3c */
	u8 f4c[4];
	u64 window;			/* +0x50 */
	u64 frame_period;		/* +0x58 */
	struct frame_load_items items[21];	/* +0x60 */
	struct ctl_table_header *header;	/* +0xa38 */
	struct proc_dir_entry *dir, *history, *cpu;	/* +0xa40 / 0xa48 / 0xa50 */
	struct irq_work irq;		/* +0xa58 */
	u64 gpu_time;
	u8 fence_pending;
	u8 pad2[7];
};
_Static_assert(sizeof(struct flt_global) == 2696, "global_frame_info size");
struct mi_rq_load { u64 exec; u64 rsv[4]; };	/* 40 bytes: only the first u64 is used by the stock code seen so far */
struct mi_rq {		/* BTF of the stock: 184 bytes */
	struct mi_rq_load prev_frame_load, cur_frame_load, nt_prev_frame_load, nt_cur_frame_load;
	u64 frame_start;
	u64 vip_tasks[2];
};
DECLARE_PER_CPU(struct mi_rq, mi_runqueues);
/* G1aj: stock bounds-checks the slot index with cmp #0x15 / b.hi (22 entries); a [22] view was tried: adjust 4->7, flt_cal_freq 117->121, update_frame_load 72->65: no net gain, kept 21 */
#define FLT_ITEM(i)	(global_frame_info.items[(i)])
#define GFI_U8(off)	(*(u8 *)((u8 *)&global_frame_info + (off)))
#define GFI_U32(off)	(*(u32 *)((u8 *)&global_frame_info + (off)))
#define GFI_U64(off)	(*(u64 *)((u8 *)&global_frame_info + (off)))

extern unsigned int flt_target_fps, flt_fence_enable, flt_gpu_enable, glk_fbreak_enable;
extern unsigned int glk_freq_limit_walt, glk_freq_limit_start;
extern unsigned int glk_maxfreq[4], glk_minfreq[4];


/* ---- vendor area behind every task_struct (BTF of the stock: struct plat_task_struct / mi_game_task) ---- */
#define XR_GAME_MAGIC	0x012345678abcdef1UL
struct mi_game_task {
	u64 run_times, fps_exec_est, fps_exec, fps_mexec;
	u64 prev_frame_load, cur_frame_load, nt_prev_frame_load, nt_cur_frame_load;
	u64 enqueue_tm;
	struct list_head list;		/* +72 */
	struct list_head delay_list;	/* +88 */
	unsigned long touch_jif;	/* +104 */
	u32 migt_count;			/* +112 */
	int f116;
	u32 wake_render;		/* +120 */
	u8 f124[40];
	int type;			/* +164 enum MI_GAME_TASK_TYPE (bit flags) */
	bool in_user_list;		/* +168 */
	bool in_work_wq;		/* +169 */
	struct task_struct *task;	/* +176 */
	unsigned long magic_num;	/* +184 */
};
struct mi_plat_user_struct {	/* BTF of the stock, 48 bytes; hangs off the vendor area of struct user_struct (+0x88 magic, +0x90 this) */
	u32 flag;
	unsigned long magic_num;	/* +8 = 0xacbdacbd */
	rwlock_t lock;			/* +16 */
	struct list_head list;		/* +24: struct mi_game_task::list of the tasks of this uid */
	struct user_struct *user;	/* +40 */
};
struct plat_task_struct {
	u8 raw[0x60];
	struct mi_game_task *game;	/* +96 */
	struct mi_plat_user_struct *mi_user;	/* +104 */
	unsigned long game_magic_num;	/* +112 */
};
#define XR_GAME_VALID(t)	((t) && (t)->game_magic_num == 0x012345678abcdef1UL)
extern struct plat_task_struct *get_xr_task_struct(struct task_struct *p);

/* xr accounting block (mi_game_ca.c) */
extern void xr_game_task_fork(struct task_struct *p);
extern void update_task_runtime_info(struct task_struct *p, u64 delta, int cpu);
extern void xr_game_scheduler_tick(struct rq *rq);
extern int xr_sched_init(void);
extern void xr_sched_exit(void);
extern void free_mi_task_struct(struct task_struct *p);
extern void xr_game_walt_get_indicies(struct task_struct *p, int *order_index, int *end_index, int num_clusters,
				      bool *skip_walt_indicies, bool *energy_eval_needed);

#endif
