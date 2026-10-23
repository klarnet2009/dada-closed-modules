/* SPDX-License-Identifier: GPL-2.0 */
/* Types of the mi_schedule provider (snapshot of the mi_schedule reconstruction, drivers/mihw/mi_sched); used by miloadtrace */
#ifndef _MI_SCHED_H
#define _MI_SCHED_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/refcount.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>

#define MI_SCHED_CPUS		8
#define MI_SCHED_CLUSTERS	2

/* ---- group data ---- */
struct group_frame_ravg {
	u64 running_sum_exec_runtime;
	u64 runnable_sum_exec_runtime;
	u64 sleep_sum_exec_runtime;
	u64 io_sum_exec_runtime;
	u64 real_runtime;
};

struct group_frame_status {
	u64 pred_demands_sum_scaled;
	u64 cumulative_runnable_avg_scaled;
};

struct mi_group {
	unsigned int id;
	int local_cid;
	unsigned int type;
	spinlock_t lock;
	spinlock_t cpu_lock[MI_SCHED_CPUS];
	int nr_running;
	int cpu_nr_running[MI_SCHED_CPUS];
	refcount_t refcnt;
	struct list_head list;
	int local_pid;
	unsigned int create_flag;
	u64 create_running_time;
	unsigned int scaling_flag;
	unsigned int fps;
	u64 frame_start;
	u64 mark_start;
	u64 window_period;
	u64 target_period;
	u64 last_update;
	u64 sum_freq_period[MI_SCHED_CLUSTERS];
	u64 avg_freq_period[MI_SCHED_CLUSTERS];
	u64 last_freq_change_time[MI_SCHED_CLUSTERS];
	u64 cur_record_freq[MI_SCHED_CLUSTERS];
	struct list_head freq_head;
	bool in_period_freq_list;
	struct group_frame_ravg cpu_prev_frame[MI_SCHED_CPUS];
	struct group_frame_ravg cpu_curr_frame[MI_SCHED_CPUS];
	struct group_frame_status status;
	struct group_frame_ravg cluster_prev_frame[MI_SCHED_CLUSTERS];
	struct group_frame_ravg cluster_curr_frame[MI_SCHED_CLUSTERS];
	u64 cluster_mark_start[MI_SCHED_CLUSTERS];
	atomic_t cluster_token_pid[2];
	bool wait_wake_flag[2];
	struct group_frame_ravg prev_frame;
	struct group_frame_ravg curr_frame;
	unsigned int rsv;
	u64 pred_target_period;
	u8 up_pred_util[2];
	u8 down_pred_util[2];
	int prefer_cluster;
	struct list_head cluster_head;
	int group_priority;
	struct list_head task_queue;
};

typedef void (*task_group_change_update_f)(void);
typedef void (*mi_update_global_period_f)(unsigned int);

enum MI_TASK_TYPE {
	MI_NORMAL_TASK, MI_MINOR_TASK, MI_REBIND_TASK, MI_LAT_TASK, MI_VVIP_TASK, MI_NEW_VTASK,
	MI_AGING_VTASK, MI_UI_TASK, MI_UI_PIPELINE, MI_REN_TASK, MI_DYNAMIC_VTASK, MI_IN_RTBOOST,
	MI_LOCK_TASK, MI_BINDER_TASK, MI_REQ_ASYNC_BINDER_TASK, MI_ART_MUTEX_TASK, MI_LOW_LT_TASK,
	MI_LINK_BOOST_TASK, MI_CLUSTER_BOOST, MI_FBOOST, MI_EBOOST, MI_SF_TASK, MI_HWC_TASK,
	MI_DIRECT_SET, MI_BAL_IRQ_TASK, MI_MMLOCK_BOOST_TASK, MI_WAKEUP_CRIT_TASK, MI_WAKEUP_TASK,
	MI_WAKEUP_MARK, MI_FREEDOM_TASK, MI_PERSIST_VTASK, MI_GRP_LINK, MI_TASK_TYPES,
};

enum MI_USER_FLAG { MI_NORMAL_USER, MI_32BIT_APP, MI_APP_TYPES };

enum MIGT_TASK_TYPE {
	MIGT_NORMAL_TASK, GAME_NORMAL_TASK, GAME_IP_TASK, GAME_IP_TASK_FUSER, GAME_VIP_TASK,
	GAME_VIP_TASK_FUSER, GAME_QRENDER_TASK, GAME_DQRENDER_TASK, GAME_SUPER_TASK, MI_VIP_TASK,
	GAME_TASK_LEVELS,
};

enum MI_TASK_STATE {
	MI_TASK_RUNNING, BLOCKED_ON_FUTEX, BOOST_FUTEX, TASK_IN_CS, TASK_IN_PRECS, TASK_IN_D,
	BLOCKED_ON_RWSEM, BLOCKED_ON_PRWSEM, BLOCKED_ON_MUTEX, BLOCKED_ON_RTMUTEX, MI_TASK_STATES,
};

enum MI_APP_TYPE {
	SYSTEM, BACKGROUND, FORE_POINT, FORE_NOPOINT, LATENCY_SENSITIVE, NEED_DECEL, NON_DECEL,
	DISPLAY, KERRNEL, APP_TYPE_NUMS,
};

enum GROUP_CMD {
	APP_CHANGE_STATUS, CREATE_LATENCY_GROUP, DESTORY_LATENCY_GROUP, CHANGE_LS_GROUP,
	APP_CHANGE_FPS, TASK_CHANGE_FPS, CHANGE_GRP_PREFER_CID, CHANGE_DISPLAY, GROUP_CMD_TYPES,
};

struct mi_default_group {
	unsigned int id;
	unsigned int flags;
	struct mi_group *mi_grp;
};

struct ls_group {
	unsigned int id;
	refcount_t refcnt;
	int create_pid;
	unsigned int flags;
	struct mi_group *mi_grp;
	struct list_head list;
};

/* ---- package runtime ---- */
struct runtime_item {
	u64 v[6];
};

struct package_runtime_info {
	struct runtime_item sup_cluster_runtime;
	struct runtime_item big_cluster_2_runtime;
	struct runtime_item big_cluster_1_runtime;
	struct runtime_item little_cluster_runtime;
};

struct mi_user_struct {
	enum MI_USER_FLAG flags;
	unsigned long magic_num;
	rwlock_t lock;
	struct list_head list;
	struct package_runtime_info pkg;
	struct user_struct *user;
	struct mi_default_group *default_group;
};

struct mi_task_bandwidth_data_wrapper {
	unsigned long magic_num;
	struct list_head throttled_list;
	int list_flag;
	u64 data[17];
	struct task_struct *task;
};

struct gt_task {
	unsigned int migt_count;
	enum MIGT_TASK_TYPE flags;
	unsigned int wake_render;
	unsigned long boost_end;
	u64 run_times;
	u64 prev_sum;
	unsigned int max_exec;
	u64 fps_exec_est;
	u64 fps_exec;
	u64 fps_mexec;
	u64 cur_window;
	u64 rsv0;
	u64 wake_ts;
	u64 lock_start_time;
	unsigned int rsv1;
	u64 prev_frame_load;
	u64 cur_frame_load;
	u64 nt_prev_frame_load;
	u64 nt_cur_frame_load;
	u64 rsv2[5];
};

struct mi_task_ravg {
	u64 running_exec;
	u64 runnable_exec;
	u64 sleep_exec;
	u64 io_exec;
	u64 memstall_exec;
	u32 demand;
	u32 demand_scaled;
	u32 coloc_demand;
	u64 running_hist[5];
	u64 runnable_hist[5];
	u64 sum_hist[5];
	u64 sum_hist_util[5];
	u32 rsv;
};

struct mi_task_struct {
	unsigned long magic_num;
	struct mi_task_bandwidth_data_wrapper bd_data;
	bool in_user_list;
	bool in_work_wq;
	bool enqueue_from_wakeup;
	int rsv0;
	enum MI_TASK_TYPE flags;
	cpumask_t cpumask;
	cpumask_t rebind_cpumask;
	struct task_struct *task;
	struct mi_user_struct *mi_user;
	struct list_head list;
	struct list_head delay_list;
	struct package_runtime_info pkg;
	struct gt_task migt;
	struct rb_node pkg_node;
	bool on_pkg_tree;
	unsigned long touch_jif;
	u64 enqueue_tm;
	u64 double_cyc_sum;
	spinlock_t lock;
	unsigned long boost_end_jif;
	unsigned int link_depth;
	int old_cid;
	struct list_head vtask_node;
	int link_owner_pid;
	int waker_type;
	enum MI_TASK_STATE state;
	u64 cs_or_spin_time;
	struct list_head lt_list;
	u64 rsv2;
	u64 rsv3;
	struct mi_task_ravg ravg;
	struct ls_group *ls_grp;
	struct mi_group *mi_grp;
	struct list_head grp_node;
	unsigned int group_flag;
	u64 grp_prev_window_cpus[MI_SCHED_CPUS];
	u64 grp_curr_window_cpus[MI_SCHED_CPUS];
	u64 grp_curr_window_cpu;
	u64 grp_prev_window_cpu;
	u64 prev_window_cpus[MI_SCHED_CPUS];
	u64 curr_window_cpus[MI_SCHED_CPUS];
	u64 curr_window_cpu;
	u64 prev_window_cpu;
	bool idle_flag;
	u64 rsv4;
	bool enqueue_after_migration;
};

struct runtime_info {
	u64 start;
};

enum cluster_type { LITTLE_CLUSTER, MID_CLUSTER_1 = 1, MID_CLUSTER_2 = 1, BIG_CLUSTER = 1, CLUSTER_TYPES = 2 };

enum MI_SCHED_MOD { GRP_FRAME, GT_TASK, METIS_TASK, MI_SCHED_TYPE };

enum userlist_cmd { ADD_TASK, MOVE_TASK, DEL_TASK, CMD_NUMS };

enum package_type { TOP_ALL, TOP_ON_SCORE, TOP_ON_B2CORE, TOP_ON_B1CORE, TOP_ON_LCORE, PACKAGE_TYPES };

enum APP_COM_TYPE { UNKNOW, APP_32BIT, APP_64BIT };

typedef void (*update_task_load_hook)(struct task_struct *p, int cpu, int new_cpu, u64 wallclock, u64 irqtime);

struct load_subs {
	u64 ws;
	u64 subs;
	u64 nt_subs;
};

struct cpu_layered_task {
	int vip_nr_running;
	int sys_nr_running;
	u64 rt_cumulative_runnable_avg_scaled;
	u64 rt_pred_demands_sum_scaled;
	u64 vip_cumulative_runnable_avg_scaled;
	u64 vip_pred_demands_sum_scaled;
	u64 sys_cumulative_runnable_avg_scaled;
	u64 sys_pred_demands_sum_scaled;
	u64 raw_cumulative_runnable_avg_scaled;
	u64 raw_pred_demands_sum_scaled;
};

struct mi_sched_work {
	struct work_struct work;
	enum userlist_cmd cmd;
	struct mi_task_struct *mi_task;
	struct mi_user_struct *mi_user;
	int uid;
};

struct mi_rq {
	u64 prev_frame_load[5];
	u64 cur_frame_load[5];
	u64 nt_prev_frame_load[5];
	u64 nt_cur_frame_load[5];
	u64 frame_start_group;
	u64 frame_start;
	u64 frame_load_last_update;
	u64 prev_frame_exec;
	u64 cur_frame_exec;
	u64 nt_prev_frame_exec;
	u64 nt_cur_frame_exec;
	u64 grp_prev_frame_exec;
	u64 grp_cur_frame_exec;
	struct load_subs subs[2];
	struct rb_root_cached vip_tasks;
	struct cpu_layered_task layered;
	struct list_head vip_tasks_list;
	raw_spinlock_t low_lt_lock;
	struct list_head low_lt_list;
};

#define MI_OFF(t, f, o) _Static_assert(offsetof(struct t, f) == (o), #t "." #f)
MI_OFF(mi_group, type, 8); MI_OFF(mi_group, lock, 12); MI_OFF(mi_group, cpu_lock, 16); MI_OFF(mi_group, nr_running, 48);
MI_OFF(mi_group, cpu_nr_running, 52); MI_OFF(mi_group, refcnt, 84); MI_OFF(mi_group, list, 88); MI_OFF(mi_group, local_pid, 104);
MI_OFF(mi_group, scaling_flag, 120); MI_OFF(mi_group, fps, 124); MI_OFF(mi_group, frame_start, 128); MI_OFF(mi_group, last_update, 160);
MI_OFF(mi_group, sum_freq_period, 168); MI_OFF(mi_group, freq_head, 232); MI_OFF(mi_group, in_period_freq_list, 248);
MI_OFF(mi_group, cpu_prev_frame, 256); MI_OFF(mi_group, cpu_curr_frame, 576); MI_OFF(mi_group, status, 896);
MI_OFF(mi_group, cluster_prev_frame, 912); MI_OFF(mi_group, cluster_curr_frame, 992); MI_OFF(mi_group, cluster_mark_start, 1072);
MI_OFF(mi_group, cluster_token_pid, 1088); MI_OFF(mi_group, prev_frame, 1104); MI_OFF(mi_group, curr_frame, 1144);
MI_OFF(mi_group, pred_target_period, 1192); MI_OFF(mi_group, up_pred_util, 1200); MI_OFF(mi_group, prefer_cluster, 1204);
MI_OFF(mi_group, cluster_head, 1208); MI_OFF(mi_group, group_priority, 1224); MI_OFF(mi_group, task_queue, 1232);
_Static_assert(sizeof(struct mi_group) == 1248, "mi_group");
MI_OFF(ls_group, mi_grp, 16); MI_OFF(ls_group, list, 24); _Static_assert(sizeof(struct ls_group) == 40, "ls_group");
_Static_assert(sizeof(struct mi_default_group) == 16, "mdg");
_Static_assert(sizeof(struct package_runtime_info) == 192, "pri");
MI_OFF(mi_user_struct, list, 24); MI_OFF(mi_user_struct, pkg, 40); MI_OFF(mi_user_struct, user, 232); _Static_assert(sizeof(struct mi_user_struct) == 248, "mus");
_Static_assert(sizeof(struct mi_task_bandwidth_data_wrapper) == 176, "bdw");
_Static_assert(sizeof(struct gt_task) == 184, "gt");
_Static_assert(sizeof(struct mi_task_ravg) == 224, "ravg");
MI_OFF(mi_task_struct, bd_data, 8); MI_OFF(mi_task_struct, in_user_list, 184); MI_OFF(mi_task_struct, flags, 192); MI_OFF(mi_task_struct, cpumask, 200);
MI_OFF(mi_task_struct, task, 216); MI_OFF(mi_task_struct, mi_user, 224); MI_OFF(mi_task_struct, list, 232); MI_OFF(mi_task_struct, delay_list, 248);
MI_OFF(mi_task_struct, pkg, 264); MI_OFF(mi_task_struct, migt, 456); MI_OFF(mi_task_struct, pkg_node, 640); MI_OFF(mi_task_struct, on_pkg_tree, 664);
MI_OFF(mi_task_struct, touch_jif, 672); MI_OFF(mi_task_struct, lock, 696); MI_OFF(mi_task_struct, boost_end_jif, 704);
MI_OFF(mi_task_struct, vtask_node, 720); MI_OFF(mi_task_struct, link_owner_pid, 736); MI_OFF(mi_task_struct, state, 744);
MI_OFF(mi_task_struct, lt_list, 760); MI_OFF(mi_task_struct, rsv2, 776); MI_OFF(mi_task_struct, ravg, 792); MI_OFF(mi_task_struct, ls_grp, 1016);
MI_OFF(mi_task_struct, mi_grp, 1024); MI_OFF(mi_task_struct, grp_node, 1032); MI_OFF(mi_task_struct, group_flag, 1048);
MI_OFF(mi_task_struct, grp_prev_window_cpus, 1056); MI_OFF(mi_task_struct, grp_curr_window_cpus, 1120); MI_OFF(mi_task_struct, grp_curr_window_cpu, 1184);
MI_OFF(mi_task_struct, prev_window_cpus, 1200); MI_OFF(mi_task_struct, curr_window_cpus, 1264); MI_OFF(mi_task_struct, curr_window_cpu, 1328);
MI_OFF(mi_task_struct, idle_flag, 1344); MI_OFF(mi_task_struct, enqueue_after_migration, 1360);
_Static_assert(sizeof(struct mi_task_struct) == 1368, "mts");
MI_OFF(mi_rq, frame_start_group, 160); MI_OFF(mi_rq, subs, 232); MI_OFF(mi_rq, vip_tasks, 280); MI_OFF(mi_rq, layered, 296);
MI_OFF(mi_rq, vip_tasks_list, 368); MI_OFF(mi_rq, low_lt_lock, 384); MI_OFF(mi_rq, low_lt_list, 392);
_Static_assert(sizeof(struct mi_rq) == 408, "mi_rq");

/* mi_group.c */
extern spinlock_t period_freq_list_lock;
extern unsigned int target_makeup_ns;
extern task_group_change_update_f task_group_change_update_hook;
extern mi_update_global_period_f mi_update_global_period;
void register_task_group_change_update_f(task_group_change_update_f f);
void unregister_task_group_change_update_f(void);
void register_mi_update_global_period_f(mi_update_global_period_f f);
void unregister_mi_update_global_period_f(void);
struct mi_group *get_mi_system_group(void);
struct mi_group *get_mi_back_group(void);
struct mi_group *get_mi_kern_group(void);
struct list_head *get_active_mi_groups(void);
u64 get_pred_target_period(unsigned int fps);
void init_ravg_info(struct mi_task_struct *mi_tsk);
void free_mi_group(struct mi_group *grp);
void mi_group_ref_dec(struct mi_group *grp);
struct mi_group *find_add_mi_group_id(int id, enum MI_APP_TYPE type);
struct mi_group *find_mi_group_by_user(struct mi_user_struct *user);
struct mi_group *find_add_mi_group_by_user(struct mi_user_struct *user);
struct mi_group *find_mi_group_by_task(struct task_struct *p, struct mi_task_struct *mi_tsk);
void mi_ls_group_dec(struct ls_group *ls);
void destory_ls_group(struct ls_group *ls);
void mi_ls_group_add(struct ls_group *ls);
struct ls_group *alloc_ls_group(enum MI_APP_TYPE type, unsigned int fps, unsigned int ls_type);
struct ls_group *create_ls_group(int pid, enum MI_APP_TYPE type, unsigned int fps, unsigned int ls_type);
struct ls_group *find_ls_grp_by_task(struct mi_task_struct *mi_tsk);
void APP_destory_ls_group(int pid);
int thread_change_ls_group_auto(struct task_struct *p);
void change_lsgroup_status(struct task_struct *p, enum MI_APP_TYPE type);
bool mi_group_ls_group(struct task_struct *p, u64 unused);
struct mi_default_group *alloc_mi_default_group(int id, enum MI_APP_TYPE type);
struct mi_default_group *create_mi_default_group(struct user_struct *user);
void free_mi_default_group(struct mi_default_group *d);
void change_group_fps(int id, unsigned int fps, enum GROUP_CMD cmd);
void change_system_to_display(int pid);
int thread_change_ls_group_explicit(struct ls_group *ls, int pid, unsigned int type);
void init_mi_group(void);
void exit_mi_group(void);

/* mi_task.c */
struct rq;
struct user_struct;
struct mi_task_struct *get_mi_task_struct(struct task_struct *p);
struct mi_task_struct *get_mi_task_struct_unsafe(struct task_struct *p);
struct mi_user_struct *get_mi_user_struct(struct user_struct *user);
extern unsigned long package_uid_32B[];
extern unsigned long package_uid_bit[];
void *get_curr_runtime_items(void);
void *get_uid_max_value(void);
void *get_runtime_window_size(void);
void *get_user_zero(void);
void mi_cpu_coregroup_mask(int cpu, struct cpumask *mask);
enum cluster_type get_cluster_type(int cpu);
bool mi_task_exiting(struct task_struct *p);
bool is_32B_user(int uid);
void update_pkg_load(struct task_struct *p, int cpu, int new_cpu, u64 wallclock, u64 irqtime);
void register_sched_load_update_hook(int mod, update_task_load_hook f);
void unregister_sched_load_update_hook(int mod);
bool pkg_enable(void);
void mi_sched_tick(void *nouse, struct rq *rq);
void mi_task_fork(void *nouse, struct task_struct *p);
void free_mi_task_struct(void *nouse, struct task_struct *p);
struct runtime_info *get_package_runtime_info(void);
void oem_update_num_sched_clusters(int clusters);

/* mi_runtime.c */
extern int pause_mode;
void create_runtime_proc(void);
void delete_runtimeproc(void);

#endif
