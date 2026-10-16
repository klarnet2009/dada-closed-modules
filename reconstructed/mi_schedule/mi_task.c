// SPDX-License-Identifier: GPL-2.0
/*
 * Reconstructed from the stock mi_schedule.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * Per-task / per-uid package runtime accounting and module entry points.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/user.h>
#include <linux/sched/task.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/cred.h>
#include <linux/bitops.h>
#include <linux/arch_topology.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <trace/hooks/sched.h>
#include "mi_sched.h"

#define MI_TASK_MAGIC		0xabcdabcdacbdadbcUL
#define MI_USER_MAGIC		0xdcba1234abcd4321UL
#define MI_STRUCT_MAGIC		0xacbdacbdUL
#define MI_MAX_UID		99999

int curr_runtime_items __asm__("curr_runtime_items.llvm.2722419413465187494") __attribute__((visibility("hidden")));
int uid_max_value __asm__("uid_max_value.llvm.2722419413465187494") __attribute__((visibility("hidden")));
static unsigned int cpu_cluster_map[NR_CPUS + 1];
unsigned long package_uid_32B[BITS_TO_LONGS(MI_MAX_UID + 1)];
static struct sched_load_update_hooks {
	update_task_load_hook hook;
} SCHED_LOAD_UPDATE_HOOK[MI_SCHED_TYPE];
static bool runtime_enable;
static struct work_struct mi_user_monitor_wk;
static struct kmem_cache *mi_task_struct_cachep;
struct runtime_info package_runtime_info[6] __asm__("package_runtime_info.llvm.2722419413465187494") __attribute__((visibility("hidden")));
static struct mi_user_struct zero_mi_user;
static struct workqueue_struct *sched_wq;
static struct kmem_cache *mi_user_struct_cachep;
static bool mi_task_has_init;
unsigned long package_uid_bit[BITS_TO_LONGS(MI_MAX_UID + 1)];

int runtime_window_size __asm__("runtime_window_size.llvm.2722419413465187494") __attribute__((visibility("hidden"))) = 30;
struct user_struct user_zero __asm__("user_zero.llvm.2722419413465187494") __attribute__((visibility("hidden"))) = {
	.__count = REFCOUNT_INIT(1),
	.android_oem_data1 = { MI_USER_MAGIC, (u64)&zero_mi_user },
};
static unsigned long sys_update_time = 0xffffffff;

DEFINE_PER_CPU(struct mi_rq, mi_runqueues);
EXPORT_PER_CPU_SYMBOL_GPL(mi_runqueues);

int num_sched_clusters __read_mostly = 2;
EXPORT_SYMBOL_GPL(num_sched_clusters);

void *get_curr_runtime_items(void)
{
	return &curr_runtime_items;
}

void *get_uid_max_value(void)
{
	return &uid_max_value;
}

void *get_runtime_window_size(void)
{
	return &runtime_window_size;
}

void *get_user_zero(void)
{
	return &user_zero;
}

#define mi_topo (*(struct cpu_topology (*)[NR_CPUS + 1])cpu_topology)

void mi_cpu_coregroup_mask(int cpu, struct cpumask *mask)
{
	int i;

	if (mi_topo[cpu].cluster_id == -1)
		return;
	for_each_possible_cpu(i)
		if (mi_topo[cpu].cluster_id == mi_topo[i].cluster_id)
			cpumask_set_cpu(i, mask);
}

enum cluster_type get_cluster_type(int cpu)
{
	int i, j, idx = 0;

	if (unlikely(cpu < 0 || cpu >= NR_CPUS))
		return LITTLE_CLUSTER;
	if (!cpu_cluster_map[NR_CPUS]) {
		for_each_online_cpu(i) {
			struct cpumask mask;

			mi_cpu_coregroup_mask(i, &mask);
			if (cpumask_first_and(&mask, cpu_online_mask) == i) {
				for_each_cpu(j, &mask)
					cpu_cluster_map[j] = idx;
				idx++;
			}
		}
		if (!cpu_cluster_map[NR_CPUS])
			cpu_cluster_map[NR_CPUS] = 1;
	}
	return cpu_cluster_map[cpu] < num_sched_clusters ? cpu_cluster_map[cpu] : 0;
}
EXPORT_SYMBOL_GPL(get_cluster_type);

struct mi_user_struct *get_mi_user_struct(struct user_struct *user)
{
	if (user->android_oem_data1[0] == MI_USER_MAGIC)
		return (struct mi_user_struct *)user->android_oem_data1[1];
	return NULL;
}
EXPORT_SYMBOL_GPL(get_mi_user_struct);

bool mi_task_exiting(struct task_struct *p)
{
	if (unlikely(!p || (p->flags & PF_EXITING) || p->__state == TASK_DEAD))
		return true;
	return p->exit_state != 0;
}
EXPORT_SYMBOL_GPL(mi_task_exiting);

struct mi_task_struct *get_mi_task_struct(struct task_struct *p)
{
	if (mi_task_exiting(p))
		return NULL;
	if (p->android_oem_data1[0] == MI_TASK_MAGIC)
		return (struct mi_task_struct *)p->android_oem_data1[1];
	return NULL;
}
EXPORT_SYMBOL_GPL(get_mi_task_struct);

struct mi_task_struct *get_mi_task_struct_unsafe(struct task_struct *p)
{
	if (!p)
		return NULL;
	if (p->android_oem_data1[0] == MI_TASK_MAGIC)
		return (struct mi_task_struct *)p->android_oem_data1[1];
	return NULL;
}
EXPORT_SYMBOL_GPL(get_mi_task_struct_unsafe);

bool is_32B_user(int uid)
{
	if (uid > MI_MAX_UID)
		return true;
	return test_bit(uid, package_uid_32B);
}
EXPORT_SYMBOL_GPL(is_32B_user);

static struct user_struct *task_user(struct task_struct *p);

void update_pkg_load(struct task_struct *p, int cpu, int new_cpu, u64 wallclock, u64 irqtime)
{
	enum cluster_type cluster = get_cluster_type(cpu);
	unsigned int idx = curr_runtime_items % 4;
	struct user_struct *user;
	struct mi_user_struct *mi_user;
	struct mi_task_struct *mi_task;
	int uid;

	if (runtime_enable == 1 && pause_mode == 0 && !(p->flags & PF_IDLE)) {
		user = task_user(p);
		if (!user)
			user = &user_zero;
		uid = user->uid.val;
		if (test_ti_thread_flag(task_thread_info(p), TIF_32BIT) && uid <= MI_MAX_UID)
			set_bit(uid, package_uid_32B);
		mi_user = get_mi_user_struct(user);
		if (mi_user) {
			mi_task = get_mi_task_struct(p);
			if (mi_task) {
				if (cluster == BIG_CLUSTER) {
					mi_task->pkg.sup_cluster_runtime.v[4] += irqtime;
					mi_user->pkg.sup_cluster_runtime.v[4] += irqtime;
				} else if (cluster == LITTLE_CLUSTER) {
					mi_task->pkg.little_cluster_runtime.v[4] += irqtime;
					mi_user->pkg.little_cluster_runtime.v[4] += irqtime;
				}
				u64 s = mi_task->pkg.sup_cluster_runtime.v[4];
				u64 b2 = mi_task->pkg.big_cluster_2_runtime.v[4];
				u64 b1 = mi_task->pkg.big_cluster_1_runtime.v[4];
				u64 l = mi_task->pkg.little_cluster_runtime.v[4];

				mi_task->pkg.sup_cluster_runtime.v[idx] = s;
				mi_task->pkg.big_cluster_2_runtime.v[idx] = b2;
				mi_task->pkg.big_cluster_1_runtime.v[idx] = b1;
				mi_task->pkg.little_cluster_runtime.v[idx] = l;
			}
		} else {
			uid = min(uid, MI_MAX_UID);
			if (!test_and_set_bit(uid, package_uid_bit)) {
				get_uid(user);
				if (uid > uid_max_value)
					uid_max_value = uid;
			}
		}
		if (user != &user_zero)
			free_uid(user);
	}
	if (SCHED_LOAD_UPDATE_HOOK[0].hook)
		SCHED_LOAD_UPDATE_HOOK[0].hook(p, cpu, new_cpu, wallclock, irqtime);
	if (SCHED_LOAD_UPDATE_HOOK[1].hook)
		SCHED_LOAD_UPDATE_HOOK[1].hook(p, cpu, new_cpu, wallclock, irqtime);
	if (SCHED_LOAD_UPDATE_HOOK[2].hook)
		SCHED_LOAD_UPDATE_HOOK[2].hook(p, cpu, new_cpu, wallclock, irqtime);
}
EXPORT_SYMBOL_GPL(update_pkg_load);

void register_sched_load_update_hook(int mod, update_task_load_hook f)
{
	if (mod >= MI_SCHED_TYPE) {
		pr_err("mi-sched: invalid mod register\n");
		return;
	}
	pr_info("mi-sched: mod %d %s now\n", mod, __func__);
	SCHED_LOAD_UPDATE_HOOK[mod].hook = f;
}
EXPORT_SYMBOL(register_sched_load_update_hook);

void unregister_sched_load_update_hook(int mod)
{
	if (mod >= MI_SCHED_TYPE) {
		pr_err("mi-sched: invalid mod unregister\n");
		return;
	}
	pr_info("mi-sched: mod %d %s now\n", mod, __func__);
	SCHED_LOAD_UPDATE_HOOK[mod].hook = NULL;
}
EXPORT_SYMBOL(unregister_sched_load_update_hook);

bool pkg_enable(void)
{
	return pause_mode == 0;
}
EXPORT_SYMBOL_GPL(pkg_enable);

static void task_add_to_userlist(struct mi_task_struct *mi_task, struct mi_user_struct *mi_user, int uid,
				 enum userlist_cmd cmd);

void mi_sched_tick(void *nouse, struct rq *rq)
{
	struct task_struct *curr;
	struct user_struct *user;
	struct mi_user_struct *mi_user;
	struct mi_task_struct *mi_task;
	u64 now;
	int uid;

	if (!(runtime_enable == 1 && pause_mode == 0))
		return;
	curr = *(struct task_struct **)((char *)rq + 0xaa8);
	now = jiffies_64;
	if (!(curr->flags & PF_IDLE)) {
		user = task_user(curr);
		if (likely(user)) {
			mi_user = get_mi_user_struct(user);
			if (mi_user) {
				uid = user->uid.val;
				if (likely(refcount_read(&curr->usage))) {
					get_task_struct(curr);
					mi_task = get_mi_task_struct(curr);
					if (unlikely(!mi_task)) {
						put_task_struct(curr);
					} else {
						if (unlikely(mi_task->in_user_list && mi_task->rsv0 != uid && !mi_task->in_work_wq))
							task_add_to_userlist(mi_task, mi_user, uid, MOVE_TASK);
						if (unlikely(!mi_task->in_user_list && !mi_task->in_work_wq))
							task_add_to_userlist(mi_task, mi_user, uid, ADD_TASK);
						put_task_struct(curr);
					}
				}
			}
			if (likely(user != &user_zero))
				free_uid(user);
		}
	}
	if (now - sys_update_time >= runtime_window_size * HZ)
		queue_work_on(0, system_long_wq, &mi_user_monitor_wk);
}
EXPORT_SYMBOL(mi_sched_tick);

static struct mi_task_struct *__get_mi_task_struct(struct task_struct *p, gfp_t gfp);

void mi_task_fork(void *nouse, struct task_struct *p)
{
	if (!p)
		return;
	p->android_oem_data1[1] = 0;
	p->android_oem_data1[0] = 0;
	if (!__get_mi_task_struct(p, GFP_KERNEL))
		pr_err("mi-sched: tsk %s %d alloc mi_task failed\n", p->comm, p->pid);
}
EXPORT_SYMBOL(mi_task_fork);

static struct mi_task_struct *__get_mi_task_struct(struct task_struct *p, gfp_t gfp)
{
	struct mi_task_struct *mi;

	if (p->android_oem_data1[0] == MI_TASK_MAGIC)
		return (struct mi_task_struct *)p->android_oem_data1[1];
	if (unlikely(in_interrupt()))
		return NULL;
	mi = kmem_cache_alloc(mi_task_struct_cachep, gfp);
	if (!mi)
		return NULL;
	mi->in_user_list = false;
	mi->in_work_wq = false;
	mi->task = p;
	mi->mi_user = NULL;
	mi->magic_num = MI_STRUCT_MAGIC;
	mi->rsv0 = -1;
	mi->flags = 0;
	mi->double_cyc_sum = 0;
	mi->cpumask = p->cpus_mask;
	memset(&mi->pkg, 0, sizeof(mi->pkg));
	init_ravg_info(mi);
	mi->enqueue_after_migration = false;
	mi->idle_flag = !!(p->flags & PF_IDLE);
	mi->enqueue_from_wakeup = false;
	mi->migt.wake_render = 0;
	mi->migt.rsv2[0] = 0;
	spin_lock_init(&mi->lock);
	mi->old_cid = -1;
	mi->waker_type = 0;
	mi->migt.boost_end = 0;
	RB_CLEAR_NODE(&mi->pkg_node);
	mi->migt.lock_start_time = 0;
	mi->migt.rsv2[2] = 0;
	mi->touch_jif = 0;
	mi->migt.rsv2[4] = 0;
	mi->migt.wake_ts = 0;
	mi->enqueue_tm = 0;
	mi->migt.prev_sum = 0;
	mi->migt.fps_mexec = 0;
	mi->migt.max_exec = 0;
	mi->boost_end_jif = 0;
	mi->migt.fps_exec = 0;
	mi->migt.rsv0 = 0;
	mi->migt.cur_window = 0;
	mi->link_depth = 0;
	mi->migt.run_times = 0;
	mi->migt.rsv1 = 0;
	mi->cs_or_spin_time = 0;
	mi->link_owner_pid = 0;
	INIT_LIST_HEAD(&mi->lt_list);
	mi->on_pkg_tree = false;
	mi->migt.flags = 0;
	mi->migt.rsv2[1] = 0;
	mi->migt.rsv2[3] = 0;
	mi->migt.migt_count = 0;
	mi->rsv2 = 0;
	mi->rsv3 = 0;
	mi->state = 0;
	p->android_oem_data1[1] = (u64)mi;
	p->android_oem_data1[0] = MI_TASK_MAGIC;
	return mi;
}

void free_mi_task_struct(void *nouse, struct task_struct *p)
{
	struct mi_task_struct *mi;
	struct mi_user_struct *mi_user;
	struct mi_group *grp;
	struct mi_rq *rq;
	unsigned long flags;

	if (p->android_oem_data1[0] != MI_TASK_MAGIC)
		return;
	mi = (struct mi_task_struct *)p->android_oem_data1[1];
	p->android_oem_data1[0] = 0;
	if (mi->magic_num != MI_STRUCT_MAGIC)
		return;
	mi->magic_num = 0;
	if (mi->in_user_list && mi->mi_user) {
		mi_user = mi->mi_user;
		write_lock_irq(&mi_user->lock);
		mi->mi_user = NULL;
		if (mi->in_user_list)
			list_del(&mi->list);
		write_unlock_irq(&mi_user->lock);
	}
	if (mi->on_pkg_tree) {
		rq = per_cpu_ptr(&mi_runqueues, task_cpu(p));
		mi->on_pkg_tree = false;
		rb_erase_cached(&mi->pkg_node, &rq->vip_tasks);
	}
	if (!list_empty(&mi->lt_list)) {
		rq = per_cpu_ptr(&mi_runqueues, task_cpu(p));
		raw_spin_lock_irqsave(&rq->low_lt_lock, flags);
		list_del_init(&mi->lt_list);
		raw_spin_unlock_irqrestore(&rq->low_lt_lock, flags);
	}
	if (mi->ls_grp) {
		if (mi->ls_grp->create_pid == p->pid)
			destory_ls_group(mi->ls_grp);
		else
			mi_ls_group_dec(mi->ls_grp);
	}
	grp = mi->mi_grp;
	if (grp) {
		atomic_cmpxchg(&grp->cluster_token_pid[0], p->pid, 0);
		atomic_cmpxchg(&grp->cluster_token_pid[1], p->pid, 0);
		mi_group_ref_dec(grp);
	}
	kmem_cache_free(mi_task_struct_cachep, mi);
}
EXPORT_SYMBOL(free_mi_task_struct);

struct runtime_info *get_package_runtime_info(void)
{
	return package_runtime_info;
}

void oem_update_num_sched_clusters(int clusters)
{
	num_sched_clusters = clusters;
}
EXPORT_SYMBOL_GPL(oem_update_num_sched_clusters);

static struct user_struct *task_user(struct task_struct *p)
{
	struct user_struct *user;

	rcu_read_lock();
	user = task_cred_xxx(p, user);
	if (unlikely(!user))
		user = &user_zero;
	else
		get_uid(user);
	rcu_read_unlock();
	return user;
}

static void task_add_work(struct work_struct *work);

static void task_add_to_userlist(struct mi_task_struct *mi_task, struct mi_user_struct *mi_user, int uid,
				 enum userlist_cmd cmd)
{
	struct mi_sched_work *w;

	if (!mi_task->task)
		return;
	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (!w)
		return;
	mi_task->in_work_wq = true;
	get_task_struct(mi_task->task);
	get_uid(mi_user->user);
	w->cmd = cmd;
	w->mi_task = mi_task;
	w->mi_user = mi_user;
	w->uid = uid;
	INIT_WORK(&w->work, task_add_work);
	queue_work(sched_wq, &w->work);
}

static void task_add_work(struct work_struct *work)
{
	struct mi_sched_work *w = container_of(work, struct mi_sched_work, work);
	struct mi_task_struct *mi_task = w->mi_task;
	struct mi_user_struct *mi_user = w->mi_user;
	struct mi_user_struct *old_user;
	int uid = w->uid;

	switch (w->cmd) {
	case ADD_TASK:
		write_lock_irq(&mi_user->lock);
		if (mi_task->in_work_wq && !mi_task->in_user_list) {
			list_add(&mi_task->list, &mi_user->list);
			mi_task->in_user_list = true;
			mi_task->in_work_wq = false;
		}
		mi_task->mi_user = mi_user;
		mi_task->rsv0 = uid;
		write_unlock_irq(&mi_user->lock);
		if (unlikely(!mi_task->mi_grp)) {
			if (!mi_user)
				pr_info("mi-sched: ADD TASK mi-user is NULL!!!");
			mi_task->mi_grp = find_add_mi_group_by_user(mi_user);
		}
		break;
	case MOVE_TASK:
		old_user = mi_task->mi_user;
		if (old_user) {
			write_lock_irq(&old_user->lock);
			if (likely(mi_task->in_user_list)) {
				list_del(&mi_task->list);
				mi_task->in_user_list = false;
			}
			mi_task->mi_user = NULL;
			mi_task->rsv0 = -1;
			write_unlock_irq(&old_user->lock);
		}
		write_lock_irq(&mi_user->lock);
		if (mi_task->in_work_wq && !mi_task->in_user_list) {
			list_add(&mi_task->list, &mi_user->list);
			mi_task->in_user_list = true;
			mi_task->in_work_wq = false;
		}
		mi_task->mi_user = mi_user;
		mi_task->rsv0 = uid;
		write_unlock_irq(&mi_user->lock);
		if (!mi_task->mi_grp) {
			if (!mi_user)
				pr_info("mi-sched: MOVE TASK2 mi-user is NULL!!!");
		} else if (!(mi_task->mi_grp->type & (1 << LATENCY_SENSITIVE))) {
			atomic_cmpxchg(&mi_task->mi_grp->cluster_token_pid[0], mi_task->task->pid, 0);
			atomic_cmpxchg(&mi_task->mi_grp->cluster_token_pid[1], mi_task->task->pid, 0);
			mi_group_ref_dec(mi_task->mi_grp);
			if (unlikely(!mi_user))
				pr_info("mi-sched: MOVE TASK1 mi-user is NULL!!!");
		} else {
			break;
		}
		mi_task->mi_grp = find_add_mi_group_by_user(mi_user);
		break;
	case DEL_TASK:
		old_user = mi_task->mi_user;
		if (old_user) {
			write_lock_irq(&old_user->lock);
			if (likely(mi_task->in_work_wq && mi_task->in_user_list)) {
				list_del(&mi_task->list);
				mi_task->in_user_list = false;
				mi_task->in_work_wq = false;
			}
			mi_task->mi_user = NULL;
			mi_task->rsv0 = -1;
			write_unlock_irq(&old_user->lock);
		}
		mi_task->mi_user = NULL;
		mi_task->rsv0 = -1;
		if (mi_task->mi_grp && !(mi_task->mi_grp->type & (1 << LATENCY_SENSITIVE))) {
			atomic_cmpxchg(&mi_task->mi_grp->cluster_token_pid[0], mi_task->task->pid, 0);
			atomic_cmpxchg(&mi_task->mi_grp->cluster_token_pid[1], mi_task->task->pid, 0);
			mi_group_ref_dec(mi_task->mi_grp);
			mi_task->mi_grp = NULL;
		}
		break;
	default:
		break;
	}
	mi_task->in_work_wq = false;
	put_task_struct(mi_task->task);
	free_uid(mi_user->user);
	kfree(w);
}

static void roll_runtime(struct package_runtime_info *pkg, unsigned int idx)
{
	u64 s = pkg->sup_cluster_runtime.v[4];
	u64 b2 = pkg->big_cluster_2_runtime.v[4];
	u64 b1 = pkg->big_cluster_1_runtime.v[4];
	u64 l = pkg->little_cluster_runtime.v[4];

	pkg->sup_cluster_runtime.v[idx] = s;
	pkg->big_cluster_2_runtime.v[idx] = b2;
	pkg->big_cluster_1_runtime.v[idx] = b1;
	pkg->little_cluster_runtime.v[idx] = l;
}

static noinline int mi_task_world_init(int *count);

static void mi_user_roll_wk(struct work_struct *work)
{
	int uid, suc = 0, fail;
	s64 t0, t1;
	struct user_struct *user;
	struct mi_user_struct *mi_user, *zmi;
	struct mi_task_struct *mi_task, *tmp;
	unsigned int idx;

	if (!mi_task_has_init) {
		mi_task_has_init = true;
		t0 = ktime_to_us(ktime_get());
		fail = mi_task_world_init(&suc);
		t1 = ktime_to_us(ktime_get());
		pr_info("mi-sched: Mi-task world init suc %d, fai %d tcost %llu\n", suc, fail, t1 - t0);
	}
	for_each_set_bit(uid, package_uid_bit, min(uid_max_value + 1, MI_MAX_UID)) {
		user = find_user(KUIDT_INIT(uid));
		if (!user) {
			idx = curr_runtime_items;
			zmi = get_mi_user_struct(&user_zero);
			roll_runtime(&zmi->pkg, idx);
			continue;
		}
		if (user->android_oem_data1[0] == MI_USER_MAGIC) {
			mi_user = (struct mi_user_struct *)user->android_oem_data1[1];
		} else {
			mi_user = NULL;
			if (!in_interrupt()) {
				mi_user = kmem_cache_alloc(mi_user_struct_cachep, GFP_KERNEL);
				if (mi_user) {
					mi_user->magic_num = MI_STRUCT_MAGIC;
					rwlock_init(&mi_user->lock);
					INIT_LIST_HEAD(&mi_user->list);
					mi_user->flags = 1;
					mi_user->user = user;
					get_uid(user);
					memset(&mi_user->pkg, 0, sizeof(mi_user->pkg));
					mi_user->default_group = create_mi_default_group(user);
					user->android_oem_data1[1] = (u64)mi_user;
					user->android_oem_data1[0] = MI_USER_MAGIC;
				}
			}
		}
		if (mi_user) {
			idx = curr_runtime_items;
			roll_runtime(&mi_user->pkg, idx);
		}
		free_uid(user);
		if (refcount_read(&user->__count) == 1) {
			clear_bit(user->uid.val, package_uid_bit);
			if (user->android_oem_data1[0] == MI_USER_MAGIC) {
				mi_user = (struct mi_user_struct *)user->android_oem_data1[1];
				user->android_oem_data1[0] = 0;
				if (mi_user->magic_num != MI_STRUCT_MAGIC)
					pr_err("mi-sched: Mi user struct has been destoryed\n");
				free_mi_default_group(mi_user->default_group);
				mi_user->magic_num = 0;
				write_lock_irq(&mi_user->lock);
				if (!list_empty(&mi_user->list)) {
					list_for_each_entry_safe(mi_task, tmp, &mi_user->list, list) {
						mi_task->mi_user = NULL;
						list_del(&mi_task->list);
						mi_task->in_user_list = false;
					}
				}
				write_unlock_irq(&mi_user->lock);
				kmem_cache_free(mi_user_struct_cachep, mi_user);
				free_uid(user);
			}
		}
	}
	idx = curr_runtime_items;
	package_runtime_info[idx].start = sys_update_time;
	sys_update_time = jiffies;
	package_runtime_info[4].start = sys_update_time;
	curr_runtime_items = idx >= 3 ? idx - 3 : idx + 1;
}

static noinline int mi_task_world_init(int *count)
{
	struct task_struct *p, *t;
	int fail = 0, suc = 0;

	rcu_read_lock();
	for_each_process_thread(p, t) {
		get_task_struct(t);
		if (test_ti_thread_flag(task_thread_info(t), TIF_32BIT)) {
			if ((u32)task_uid(t).val <= MI_MAX_UID)
				set_bit(task_uid(t).val, package_uid_32B);
		}
		if (__get_mi_task_struct(t, GFP_ATOMIC)) {
			suc++;
		} else {
			pr_err("mi-sched: tsk %s %d alloc mi_task failed\n", t->comm, t->pid);
			fail++;
		}
		put_task_struct(t);
	}
	rcu_read_unlock();
	if (count)
		*count = suc;
	return fail;
}

static int __init mi_sched_init(void)
{
	struct mi_user_struct *zmi;
	unsigned long now;

	pr_err("in %s\n", __func__);
	mi_task_struct_cachep = kmem_cache_create("mi_task_struct", sizeof(struct mi_task_struct), 0,
						  SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	mi_user_struct_cachep = kmem_cache_create("mi_user_struct", sizeof(struct mi_user_struct), 0,
						  SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	sched_wq = alloc_workqueue("mi_sched", WQ_UNBOUND, 0);
	if (!sched_wq) {
		kmem_cache_destroy(mi_task_struct_cachep);
		kmem_cache_destroy(mi_user_struct_cachep);
		return 0;
	}
	create_runtime_proc();
	memset(package_runtime_info, 0, sizeof(package_runtime_info));
	zmi = get_mi_user_struct(&user_zero);
	if (zmi) {
		zmi->magic_num = MI_STRUCT_MAGIC;
		rwlock_init(&zmi->lock);
		INIT_LIST_HEAD(&zmi->list);
		zmi->flags = 1;
		zmi->user = &user_zero;
		get_uid(&user_zero);
		memset(&zmi->pkg, 0, sizeof(zmi->pkg));
		zmi->default_group = create_mi_default_group(&user_zero);
		user_zero.uid = GLOBAL_ROOT_UID;
		set_bit(0, package_uid_bit);
		if (uid_max_value < 0)
			uid_max_value = 0;
		get_uid(&user_zero);
	}
	INIT_WORK(&mi_user_monitor_wk, mi_user_roll_wk);
	now = jiffies;
	sys_update_time = now;
	package_runtime_info[4].start = now;
	package_runtime_info[5].start = now;
	register_trace_android_vh_free_task(free_mi_task_struct, NULL);
	register_trace_android_vh_scheduler_tick(mi_sched_tick, NULL);
	init_mi_group();
	runtime_enable = true;
	return 0;
}

static void __exit mi_sched_exit(void)
{
	pr_err("in %s\n", __func__);
	exit_mi_group();
	delete_runtimeproc();
	kmem_cache_destroy(mi_task_struct_cachep);
	kmem_cache_destroy(mi_user_struct_cachep);
}

module_init(mi_sched_init);
module_exit(mi_sched_exit);
MODULE_DESCRIPTION("pkg runtime info calc by David");
MODULE_LICENSE("GPL");
