// SPDX-License-Identifier: GPL-2.0
/* mi_game: xr task / uid accounting (mi_game_ca.c): only the 32-bit-app uid bitmaps so far. PARTIAL, DO NOT LOAD. */
#include "mi_game.h"
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/user.h>
#include <linux/hardirq.h>
#include <linux/thread_info.h>
#include <linux/cred.h>

#pragma clang diagnostic ignored "-Wmissing-prototypes"

DECLARE_BITMAP(package_uid_32B, 100000);
DECLARE_BITMAP(package_uid_bit, 100000);

bool is_32B_user(int uid)
{
	if (uid > 99999)
		return true;
	return test_bit(uid, package_uid_32B);
}
EXPORT_SYMBOL_GPL(is_32B_user);


/* the non-LTO Kleaf build would call these out of line from xr_game_scheduler_tick (the stock has them inlined) */
static __always_inline void get_task_struct_i(struct task_struct *t)
{
	refcount_t *r = &t->usage;
	int old = atomic_fetch_add_relaxed(1, &r->refs);

	if (unlikely(!old))
		refcount_warn_saturate(r, REFCOUNT_ADD_UAF);
	else if (unlikely(old < 0 || old + 1 < 0))
		refcount_warn_saturate(r, REFCOUNT_ADD_OVF);
}

static __always_inline void put_task_struct_i(struct task_struct *t)
{
	refcount_t *r = &t->usage;
	int old = atomic_fetch_sub_release(1, &r->refs);

	if (old > 0 && old == 1) {
		smp_acquire__after_ctrl_dep();
		__put_task_struct(t);
	} else if (unlikely(old <= 0 || old - 1 < 0)) {
		refcount_warn_saturate(r, REFCOUNT_SUB_UAF);
	}
}

extern struct workqueue_struct *migt_wq;
extern unsigned int migt_debug;
extern struct mi_game_task *get_gt_task(struct task_struct *p);

#define XR_USER_MAGIC	0xdcba1234abcd4321UL

/* xr task accounting accessors: the extension lives right behind the task_struct */
noinline struct plat_task_struct *get_xr_task_struct(struct task_struct *p)
{
	struct plat_task_struct *v;
	unsigned int st;

	if (unlikely(!p || p->exit_state))
		return NULL;
	st = p->__state;
	v = p == &init_task ? (struct plat_task_struct *)vendor_data_pad : (struct plat_task_struct *)(p + 1);
	return st == TASK_DEAD ? NULL : v;
}

struct mi_plat_user_struct *get_xr_user_struct(struct user_struct *user)
{
	u8 *t = (u8 *)user;
	if (*(u64 *)(t + 0x88) == XR_USER_MAGIC)
		return *(void **)(t + 0x90);
	return NULL;
}

bool mi_task_exiting(struct task_struct *p)
{
	if (unlikely(!p || p->exit_state))
		return true;
	return p->__state == TASK_DEAD;
}

void xr_game_task_init(struct mi_game_task *gt, struct task_struct *p)
{
	gt->run_times = gt->fps_exec_est = gt->fps_exec = gt->fps_mexec = 0;
	gt->enqueue_tm = 0;
	INIT_LIST_HEAD(&gt->list);
	INIT_LIST_HEAD(&gt->delay_list);
	gt->touch_jif = 0;
	gt->migt_count = 0;
	gt->f116 = -1;
	gt->wake_render = 0;
	memset(gt->f124, 0, sizeof(gt->f124));
	gt->type = 0;
	gt->in_user_list = false;
	gt->in_work_wq = false;
	gt->task = p;
	gt->magic_num = 0xacbdacbd;
}


static struct kmem_cache *mi_task_struct_cachep, *xr_user_struct_cachep;
static u8 zero_mi_user[48];		/* struct mi_plat_user_struct of the fallback user */
/* 168-byte fake struct user_struct of the stock (.data): refcount 1, mi_user pointer at +144 */
static struct {
	refcount_t count;
	u8 pad0[0x4c];
	u32 uid;		/* +0x50 */
	u8 pad1[0x34];
	unsigned long magic;	/* +0x88 XR_USER_MAGIC */
	void *mi_user;		/* +0x90 */
	u8 pad2[16];
} user_zero = { .count = REFCOUNT_INIT(1), .magic = 0xdcba1234abcd4321UL, .mi_user = &zero_mi_user };

/* tear down the xr task / user accounting: plat funcs 18, 21, 19 and the two slab caches */
void xr_sched_exit(void)
{
	unregister_plat_func(18);
	unregister_plat_func(21);
	unregister_plat_func(19);
	kmem_cache_destroy(mi_task_struct_cachep);
	kmem_cache_destroy(xr_user_struct_cachep);
}

/* referenced user_struct of the task credentials, or the static fallback */
static struct user_struct *task_user(struct task_struct *p)
{
	struct user_struct *u;

	rcu_read_lock();
	u = task_cred_xxx(p, user);
	if (!u)
		u = (struct user_struct *)&user_zero;
	else
		refcount_inc(&u->__count);
	rcu_read_unlock();
	return u;
}

/* task exit: drop the game record (leave the per-user list) and free it */
void free_mi_task_struct(struct task_struct *p)
{
	struct plat_task_struct *xr = p == &init_task ? (struct plat_task_struct *)vendor_data_pad : (struct plat_task_struct *)(p + 1);
	struct mi_game_task *gt;

	if (!xr) {
		printk(KERN_ERR "free mi task struct failed\n");
		return;
	}
	if (xr->game_magic_num != XR_GAME_MAGIC)
		return;
	gt = xr->game;
	xr->game_magic_num = 0;
	if (gt->magic_num != 0xacbdacbd)
		return;
	gt->magic_num = 0;
	if (gt->in_user_list && xr->mi_user) {
		rwlock_t *lock = (rwlock_t *)((u8 *)xr->mi_user + 0x10);
		unsigned long flags;

		write_lock_irqsave(lock, flags);	/* G14c: was write_lock_irq in the CN stock */
		xr->mi_user = NULL;
		if (gt->in_user_list) {
			list_del_init(&gt->list);
			list_del_init(&gt->delay_list);
			gt->in_user_list = false;
		}
		write_unlock_irqrestore(lock, flags);
	}
	kmem_cache_free(mi_task_struct_cachep, xr->game);
	xr->game = NULL;
}

extern unsigned long package_uid_32B[];
extern unsigned long package_uid_bit[];

#define XR_USER_MAGIC_V		0xdcba1234abcd4321UL
#define USER_XR_MAGIC(u)	(*(unsigned long *)((u8 *)(u) + 0x88))
#define USER_XR_MI(u)		(*(struct mi_plat_user_struct **)((u8 *)(u) + 0x90))
#define UID_LIM			0x1869e

enum userlist_cmd { ADD_TASK = 0, MOVE_TASK = 1, DEL_TASK = 2, CMD_NUMS = 3 };

static u8 mi_task_has_init;
static u8 runtime_enable;
static int uid_max_value;
static unsigned long sys_update_time = 0xffffffff;
static struct work_struct xr_user_monitor_wk;

/* the stock inlines get_xr_task_struct() in this block */
static __always_inline struct plat_task_struct *xr_inl(struct task_struct *p)
{
	if (unlikely(!p || p->exit_state))
		return NULL;
	if (p->__state == TASK_DEAD)
		return NULL;
	if (p == &init_task)
		return (struct plat_task_struct *)vendor_data_pad;
	return (struct plat_task_struct *)(p + 1);
}

/* G56a: task_add_to_userlist() of the stock dereferences p without a NULL test (p is nonnull there) */
static __always_inline struct plat_task_struct *xr_nn_inl(struct task_struct *p)
{
	if (p->exit_state)
		return NULL;
	if (p->__state == TASK_DEAD)
		return NULL;
	if (p == &init_task)
		return (struct plat_task_struct *)vendor_data_pad;
	return (struct plat_task_struct *)(p + 1);
}

static __always_inline void gt_init_inl(struct mi_game_task *gt, struct task_struct *p)
{
	gt->run_times = gt->fps_exec_est = gt->fps_exec = gt->fps_mexec = 0;
	gt->enqueue_tm = 0;
	INIT_LIST_HEAD(&gt->list);
	INIT_LIST_HEAD(&gt->delay_list);
	gt->touch_jif = 0;
	gt->migt_count = 0;
	gt->f116 = -1;
	gt->wake_render = 0;
	memset(gt->f124, 0, sizeof(gt->f124));
	gt->type = 0;
	gt->in_user_list = false;
	gt->in_work_wq = false;
	gt->task = p;
	gt->magic_num = 0xacbdacbd;
}

/* deferred add / move / delete of a game task on the per-uid list (the request lives in a kmalloc'ed work item) */
struct userlist_work {
	struct work_struct work;	/* 48 bytes here (vendor data) */
	int cmd;			/* +0x30 */
	struct plat_task_struct *xr;	/* +0x38 */
	struct mi_plat_user_struct *mi_user;	/* +0x40 */
	struct task_struct *p;		/* +0x48 */
	struct task_struct *gt_task;	/* +0x50 */
	struct user_struct *user;	/* +0x58 */
	int uid;			/* +0x60 */
};
_Static_assert(sizeof(struct userlist_work) == 0x68, "userlist_work");

static void task_add_work(struct work_struct *work);

static __used void __attribute__((nonnull(4)))
task_add_to_userlist(struct mi_game_task *gt, struct mi_plat_user_struct *mi_user, int uid,
		     struct task_struct *p, enum userlist_cmd cmd)
{
	struct userlist_work *n;
	struct plat_task_struct *xr;

	if (!gt->task)
		return;
	n = kmalloc(sizeof(*n), GFP_ATOMIC);
	if (!n)
		return;
	xr = xr_nn_inl(p);
	if (!xr) {
		kfree(n);
		return;
	}
	gt->in_work_wq = true;
	get_task_struct(gt->task);
	get_uid(mi_user->user);
	n->cmd = cmd;
	n->p = p;
	n->gt_task = gt->task;
	n->xr = xr;
	n->mi_user = mi_user;
	n->user = mi_user->user;
	n->uid = uid;
	INIT_WORK(&n->work, task_add_work);
	queue_work_on(WORK_CPU_UNBOUND, migt_wq, &n->work);
}

static __always_inline void ul_add(struct mi_game_task *gt, struct mi_plat_user_struct *mu,
				   struct plat_task_struct *xr, int uid)
{
	write_lock_irq(&mu->lock);
	if (gt->in_work_wq && !gt->in_user_list) {
		list_add(&gt->list, &mu->list);
		gt->in_user_list = true;
		gt->in_work_wq = false;
	}
	xr->mi_user = mu;
	gt->f116 = uid;
	write_unlock_irq(&mu->lock);
}

static void task_add_work(struct work_struct *work)
{
	struct userlist_work *n = (struct userlist_work *)work;
	struct task_struct *p = n->p;
	struct plat_task_struct *xr = n->xr;
	struct mi_plat_user_struct *mu = n->mi_user, *old;
	int uid = n->uid;
	struct mi_game_task *gt;

	gt = get_gt_task(p);
	if (!gt) {
		put_task_struct(p);
		goto out;
	}
	if (xr->game_magic_num == XR_GAME_MAGIC) {
		switch (n->cmd) {
		case ADD_TASK:
			ul_add(gt, mu, xr, uid);
			break;
		case MOVE_TASK:
			old = xr->mi_user;
			if (old) {
				write_lock_irq(&old->lock);
				if (gt->in_user_list) {
					list_del_init(&gt->list);
					gt->in_user_list = false;
				}
				xr->mi_user = NULL;
				gt->f116 = -1;
				write_unlock_irq(&old->lock);
			}
			ul_add(gt, mu, xr, uid);
			break;
		case DEL_TASK:
			old = xr->mi_user;
			if (old) {
				write_lock_irq(&old->lock);
				if (gt->in_work_wq && gt->in_user_list) {
					list_del_init(&gt->list);
					gt->in_user_list = false;
					gt->in_work_wq = false;
				}
				xr->mi_user = NULL;
				gt->f116 = -1;
				write_unlock_irq(&old->lock);
			}
			xr->mi_user = NULL;
			gt->f116 = -1;
			break;
		}
		gt->in_work_wq = false;
	}
	put_task_struct(n->gt_task);
out:
	free_uid(n->user);
	kfree(n);
}

/* adopt every existing thread: 32-bit uid bitmap + a game record for each; returns the failures, *ok the successes */
static __used int mi_task_world_init(int *ok_out)
{
	struct task_struct *g, *t;
	int ok = 0, fail = 0;

	rcu_read_lock();
	for_each_process_thread(g, t) {
		struct plat_task_struct *xr;
		struct mi_game_task *gt;

		get_task_struct(t);
		if (test_ti_thread_flag(task_thread_info(t), TIF_32BIT) && task_cred_xxx(t, uid).val < 100000)
			set_bit(task_cred_xxx(t, uid).val, package_uid_32B);
		xr = xr_inl(t);
		if (xr) {
			if (unlikely(get_gt_task(t))) {
				ok++;
				goto put;
			}
			if (!in_interrupt()) {
				gt = kmem_cache_alloc(mi_task_struct_cachep, GFP_ATOMIC);
				if (gt) {
					gt_init_inl(gt, t);
					xr->game = gt;
					xr->game_magic_num = XR_GAME_MAGIC;
					ok++;
					goto put;
				}
			}
		}
		pr_err("tsk %s %d alloc mi_task failed\n", t->comm, t->pid);
		fail++;
put:
		put_task_struct(t);
	}
	rcu_read_unlock();
	if (ok_out)
		*ok_out = ok;
	return fail;
}

/* plat func 18: a new task is forked: give it a game record */
void xr_game_task_fork(struct task_struct *p)
{
	struct plat_task_struct *xr;
	struct mi_game_task *gt;

	if (!p)
		return;
	xr = xr_inl(p);
	if (xr) {
		if (unlikely(get_gt_task(p)))
			return;
		if (!in_interrupt()) {
			gt = kmem_cache_alloc(mi_task_struct_cachep, GFP_KERNEL);
			if (gt) {
				gt_init_inl(gt, p);
				xr->game = gt;
				xr->game_magic_num = XR_GAME_MAGIC;
				return;
			}
		}
	}
	pr_err("tsk %s %d alloc mi_task failed\n", p->comm, p->pid);
}

/* periodic worker: create the per-uid records of the known uids and retire the ones nobody uses any more */
static __used void xr_user_roll_wk(struct work_struct *work)
{
	int ok = 0, fail, uid;
	struct user_struct *user;
	struct mi_plat_user_struct *mu;

	if (!mi_task_has_init) {
		s64 t0;

		mi_task_has_init = true;
		t0 = ktime_to_us(ktime_get());
		fail = mi_task_world_init(&ok);
		pr_info("Mi-task world init suc %d, fai %d tcost %llu\n", ok, fail, ktime_to_us(ktime_get()) - t0);
	}
	for (uid = find_next_bit(package_uid_bit, min(uid_max_value, UID_LIM) + 1, 0);
	     uid <= min(uid_max_value, UID_LIM);
	     uid = find_next_bit(package_uid_bit, min(uid_max_value, UID_LIM) + 1, uid + 1)) {
		struct mi_game_task *gt, *tmp;

		user = find_user(KUIDT_INIT(uid));
		if (!user)
			continue;
		if (USER_XR_MAGIC(user) != XR_USER_MAGIC_V && !in_interrupt()) {
			mu = kmem_cache_alloc(xr_user_struct_cachep, GFP_KERNEL);
			if (mu) {
				mu->magic_num = 0xacbdacbd;
				rwlock_init(&mu->lock);
				INIT_LIST_HEAD(&mu->list);
				mu->flag = 1;
				mu->user = user;
				get_uid(user);
				USER_XR_MAGIC(user) = XR_USER_MAGIC_V;
				USER_XR_MI(user) = mu;
			}
		}
		free_uid(user);
		if (refcount_read(&user->__count) != 1)
			continue;
		clear_bit(user->uid.val, package_uid_bit);
		if (unlikely(migt_debug))
			pr_info("User %d free mem\n", user->uid.val);
		if (USER_XR_MAGIC(user) == XR_USER_MAGIC_V) {
			mu = USER_XR_MI(user);
			USER_XR_MAGIC(user) = 0;
			if (unlikely(mu->magic_num != 0xacbdacbd))
				pr_err("Mi user struct has been destoryed\n");
			mu->magic_num = 0;
			write_lock_irq(&mu->lock);
			/* g62a: the stock tests list_empty() (READ_ONCE = ldar, S+1f4) before the walk */
			if (!list_empty(&mu->list))
			list_for_each_entry_safe(gt, tmp, &mu->list, list) {
				struct plat_task_struct *xr;

				list_del(&gt->list);
				xr = xr_inl(gt->task);
				if (xr)
					xr->mi_user = NULL;
				gt->in_user_list = false;
			}
			write_unlock_irq(&mu->lock);
			kmem_cache_free(xr_user_struct_cachep, mu);
		}
		free_uid(user);
	}
	sys_update_time = jiffies;
}

/* per-task bookkeeping: remember the uid of a running task (32-bit apps, known uids) */
void update_task_runtime_info(struct task_struct *p, u64 delta, int cpu)
{
	struct user_struct *user;
	int uid;

	if (runtime_enable != 1)
		return;
	if (p->flags & PF_IDLE)
		return;
	user = task_user(p);
	if (!user)
		user = (struct user_struct *)&user_zero;
	uid = user->uid.val;
	if (test_ti_thread_flag(task_thread_info(p), TIF_32BIT) && uid <= 99999)
		set_bit(uid, package_uid_32B);
	if (USER_XR_MAGIC(user) != XR_USER_MAGIC_V || !USER_XR_MI(user)) {
		int c = min(uid, 99999);

		if (!test_and_set_bit(c, package_uid_bit)) {
			get_uid(user);
			if (c > uid_max_value)
				uid_max_value = c;
		}
	}
	if (user != (struct user_struct *)&user_zero)
		free_uid(user);
}

/* plat func 19 (scheduler tick): keep the running task on the list of its uid, kick the roll worker now and then */
void xr_game_scheduler_tick(struct rq *rq)
{
	struct task_struct *curr;
	struct user_struct *user;
	unsigned long now;

	if (runtime_enable != 1)
		return;
	curr = RQ_CURR(rq);
	now = jiffies_64;
	if (curr && !(curr->flags & PF_IDLE)) {
		user = task_user(curr);
		if (user) {
			struct mi_plat_user_struct *mu;

			if (USER_XR_MAGIC(user) == XR_USER_MAGIC_V && (mu = USER_XR_MI(user))) {
				int uid = user->uid.val;

				if (refcount_read(&curr->usage)) {
					struct mi_game_task *gt;

					get_task_struct_i(curr);
					gt = get_gt_task(curr);
					if (gt) {
						if (gt->in_user_list) {
							if (gt->f116 == uid || gt->in_work_wq)
								goto done;
							task_add_to_userlist(gt, mu, uid, curr, MOVE_TASK);
							if (gt->in_user_list)
								goto done;
						}
						if (!gt->in_work_wq)
							task_add_to_userlist(gt, mu, uid, curr, ADD_TASK);
					}
done:
					put_task_struct_i(curr);
				}
			}
			if (user != (struct user_struct *)&user_zero)
				free_uid(user);
		}
	}
	if (((now - sys_update_time) >> 2) >= 0x753)
		queue_work_on(0, system_long_wq, &xr_user_monitor_wk);
}

/* module init of the xr accounting: slab caches, the fallback user, the monitor worker, plat funcs 18 / 21 / 19 */
int xr_sched_init(void)
{
	xr_user_struct_cachep = kmem_cache_create("mi_plat_user_struct", 48, 0,
						  SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	mi_task_struct_cachep = kmem_cache_create("mi_task_struct", 192, 0,
						  SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);
	if (user_zero.magic == XR_USER_MAGIC_V && user_zero.mi_user) {
		struct mi_plat_user_struct *mu = user_zero.mi_user;

		mu->magic_num = 0xacbdacbd;
		rwlock_init(&mu->lock);
		INIT_LIST_HEAD(&mu->list);
		mu->flag = 1;
		mu->user = (struct user_struct *)&user_zero;
		refcount_inc(&user_zero.count);
		user_zero.uid = 0;
		set_bit(0, package_uid_bit);
		refcount_inc(&user_zero.count);
	}
	INIT_WORK(&xr_user_monitor_wk, xr_user_roll_wk);
	sys_update_time = jiffies;
	register_plat_func(18, xr_game_task_fork);
	register_plat_func(21, free_mi_task_struct);
	register_plat_func(19, xr_game_scheduler_tick);
	runtime_enable = 1;
	return 0;
}
