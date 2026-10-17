// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS inherit driver (OS4): vendor-hook handlers and the per-feature (un)registration
 * functions. Reconstructed from the stock binary (round 5, ).
 * PLACEHOLDERS (compile-only bodies, marked STUB): the handlers and helpers whose logic is not
 * recovered yet; they only exist so that the register/unregister functions are real.
 */

#define pr_fmt(fmt) "qi: " fmt

#include <linux/futex.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fuse.h>
#include <trace/hooks/rcu.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"
#include "trace_xr_qi.h"

#define RWSEM_FLAG_HANDOFF_Q	4UL
#define XR_FBT_QOS_EVENT	0x400c780aU

#define RCU_INFO_MAX	10
static struct {
	struct task_struct *tasks[RCU_INFO_MAX];
	int head;
	int count;
	spinlock_t lock;
	u32 pad;
} g_rcu_info;

/* private kernel/locking/mutex.h layout */
struct mutex_waiter {
	struct list_head list;
	struct task_struct *task;
	struct ww_acquire_ctx *ww_ctx;
};

static inline void *qi_vendor_req(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	return v ? v->req : NULL;
}

/* ---- STUB helpers (logic not recovered) ---- */
/* STUBS of the per-feature iterative inherit engines (called from iter_qos_inherit) */
/* a qos task jumps the queue: its waiter goes in front of the first normal waiter of the mutex */
static noinline void do_mutex_jump(struct mutex *lock, struct mutex_waiter *waiter, bool *already_on_list)
{
	struct mutex_waiter *w, *tmp;

	if (!is_qi_feat_enabled(2))
		return;
	list_for_each_entry_safe(w, tmp, &lock->wait_list, list) {
		get_task_struct(w->task);
		if (!is_qos_rt_task(w->task)) {
			list_add_tail(&waiter->list, &w->list);
			put_task_struct(w->task);
			*already_on_list = true;
			trace_qi_mutex_jump(waiter->task, (unsigned long)lock);
			return;
		}
		put_task_struct(w->task);
	}
}
static noinline void undo_mutex_qos_inherit(struct mutex *lock)
{
	struct task_struct *cur = current;
	struct qi_struct *qi = get_task_qi(cur);
	int ret;

	if (qi && (atomic_read(&qi->flag) & BIT(QOS_MUTEX))) {
		ret = undo_qos_inherit(cur, qi, (unsigned long)lock, QOS_MUTEX);
		trace_qi_mutex_uninherit(ret, cur, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
					 (unsigned long)lock);
	}
}
static noinline void undo_rwsem_qos_inherit(struct rw_semaphore *sem, struct task_struct *p)
{
	struct qi_struct *qi;
	int ret;

	get_task_struct(p);
	qi = get_task_qi(p);
	if (qi && (atomic_read(&qi->flag) & BIT(QOS_RWSEM))) {
		ret = undo_qos_inherit(p, qi, (unsigned long)sem, QOS_RWSEM);
		trace_qi_rwsem_uninherit(ret, p, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
					 (unsigned long)sem);
	}
	put_task_struct(p);
}
static noinline int qos_event_handler(void *arg) { return 0; }

/* ---- binder ---- */
static __used noinline int do_binder_qos_inherit(struct binder_transaction *t, struct task_struct *from, struct task_struct *to, bool async)
{
	struct qi_struct *qi = get_alloc_task_qi(to);
	int ret;

	if (!qi)
		return -ENOMEM;
	ret = do_qos_inherit(from, to, (unsigned long)t, QOS_BINDER);
	trace_qi_binder_set_priority(ret, from, to, async, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	if (!ret)
		*(int *)((u8 *)t + 0xb8) = to->pid;
	return ret;
}

static DEFINE_SPINLOCK(g_binder_lock);

/* the binder thread that waits for p inherits p's qos along the chain of transactions */
void iter_binder_qos_inherit(struct task_struct *p, void *req)
{
	struct qi_vendor *v = android_task_vendor_data(p);
	struct qi_vendor *cv;
	struct task_struct *t;
	struct qi_struct *qi;
	unsigned long flags;
	int pid, ret, st;

	spinlock_t *lock = v ? &v->lock : NULL;

	if (!lock)
		return;
	spin_lock_irqsave(lock, flags);
	pid = v->f30;
	v->f30 = -1;
	spin_unlock_irqrestore(lock, flags);
	if (pid < 1)
		return;
	rcu_read_lock();
	t = find_task_by_vpid(pid);
	if (!t) {
		rcu_read_unlock();
		return;
	}
	get_task_struct(t);
	rcu_read_unlock();
	if (t == p || t->exit_state || (t->__state & (TASK_INTERRUPTIBLE | TASK_FREEZABLE)))
		goto out;
	spin_lock(&g_binder_lock);
	if (v->req != req || !*(void **)((u8 *)req + 0x28) || *(int *)((u8 *)req + 0x34) != p->pid ||
	    get_task_qi(p)) {
		spin_unlock(&g_binder_lock);
		goto out;
	}
	cv = android_task_vendor_data(current);
	if (cv) {
		st = cv->iter_depth;
		if (st != 3) {
			cv->iter[st] = req;
			cv->iter_depth = st + 1;
		}
	}
	spin_lock((spinlock_t *)((u8 *)req + 0xa8));
	ret = do_binder_qos_inherit(req, p, t, false);
	spin_unlock((spinlock_t *)((u8 *)req + 0xa8));
	spin_unlock(&g_binder_lock);
	if (ret)
		goto out;
	trace_qi_iter_binder_inherit(p, t);
	if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && v->req == req) {
		iter_qos_inherit(t, req);
	} else {
		qi = get_task_qi(t);
		ret = undo_qos_inherit(t, qi, (unsigned long)req, QOS_BINDER);
		trace_qi_binder_restore_priority(ret, (unsigned long)req, t, atomic_read(&qi->static_qos),
						 atomic_read(&qi->dyn_qos));
	}
out:
	put_task_struct(t);
	return;
}

static void android_vh_binder_transaction_init(void *unused, struct binder_transaction *t)
{
	if (is_qi_feat_enabled(1))
		*(int *)((u8 *)t + 0xb8) = -1;
}

static void android_vh_binder_set_priority(void *unused, struct binder_transaction *t, struct task_struct *task) { /* STUB */ }
static void android_vh_binder_restore_priority(void *unused, struct binder_transaction *t, struct task_struct *task) { /* STUB */ }
static void android_vh_binder_proc_transaction(void *unused, struct task_struct *caller_task,
					       struct task_struct *binder_proc_task,
					       struct task_struct *binder_th_task, int node_debug_id,
					       struct binder_transaction *t, bool pending_async) { /* STUB */ }
static void android_vh_free_binder_transaction(void *unused, struct binder_transaction *t) { /* STUB */ }
static void android_vh_binder_buffer_release(void *unused, struct binder_proc *proc,
					     struct binder_thread *thread, struct binder_buffer *buffer,
					     bool has_transaction)
{
	struct task_struct *cur;
	struct qi_struct *qi;
	int ret;

	/* binder_buffer bitfield byte at +0x28, bit 3 = async_transaction */
	if (!buffer || !(*((u8 *)buffer + 0x28) & BIT(3)))
		return;
	cur = current;
	qi = get_task_qi(cur);
	if (!qi || !(atomic_read(&qi->flag) & 1))
		return;
	ret = undo_qos_inherit(cur, qi, 0, 0);
	trace_qi_binder_buffer_release(ret, cur, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
}

int qi_vh_binder_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_binder_transaction_init(android_vh_binder_transaction_init, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_binder_set_priority(android_vh_binder_set_priority, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_binder_restore_priority(android_vh_binder_restore_priority, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_binder_proc_transaction(android_vh_binder_proc_transaction, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_free_oem_binder_struct(android_vh_free_binder_transaction, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_binder_buffer_release(android_vh_binder_buffer_release, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_binder: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_binder_unregister(void)
{
	unregister_trace_android_vh_binder_buffer_release(android_vh_binder_buffer_release, NULL);
	unregister_trace_android_vh_free_oem_binder_struct(android_vh_free_binder_transaction, NULL);
	unregister_trace_android_vh_binder_proc_transaction(android_vh_binder_proc_transaction, NULL);
	unregister_trace_android_vh_binder_restore_priority(android_vh_binder_restore_priority, NULL);
	unregister_trace_android_vh_binder_set_priority(android_vh_binder_set_priority, NULL);
	unregister_trace_android_vh_binder_transaction_init(android_vh_binder_transaction_init, NULL);
}

/* ---- fuse ---- */
static noinline int do_fuse_qos_inherit(struct fuse_req *req, struct task_struct *from, struct task_struct *to)
{
	struct qi_struct *qi;
	int ret;

	get_task_struct(to);
	qi = get_alloc_task_qi(to);
	if (!qi) {
		put_task_struct(to);
		return -ENOMEM;
	}
	ret = do_qos_inherit(from, to, (unsigned long)req, QOS_FUSE);
	trace_qi_fuse_inherit(ret, from, to, (unsigned long)req, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	put_task_struct(to);
	return ret;
}

static noinline void undo_fuse_qos_inherit(struct task_struct *p, struct fuse_req *req);

/* p waits for a fuse request served by the task recorded in its vendor area */
void iter_fuse_qos_inherit(struct task_struct *p, void *req)
{
	struct qi_vendor *v, *cv;
	struct task_struct *t;
	spinlock_t *lock;
	unsigned long flags;
	int pid, ret, st;

	if (!req)
		return;
	v = android_task_vendor_data(p);
	lock = v ? &v->lock : NULL;
	if (!lock)
		return;
	spin_lock_irqsave(lock, flags);
	pid = v->f30;
	v->f30 = -1;
	spin_unlock_irqrestore(lock, flags);
	if (pid < 1)
		return;
	rcu_read_lock();
	t = find_task_by_vpid(pid);
	if (!t) {
		rcu_read_unlock();
		return;
	}
	get_task_struct(t);
	rcu_read_unlock();
	if (unlikely(t->exit_state))
		goto out;
	cv = android_task_vendor_data(current);
	if (cv) {
		st = cv->iter_depth;
		if (st != 3) {
			cv->iter[st] = req;
			cv->iter_depth = st + 1;
		}
	}
	ret = do_fuse_qos_inherit(req, p, t);
	if (!ret) {
		trace_qi_iter_fuse_inherit(p, t, (unsigned long)req);
		if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && v->req == req)
			iter_qos_inherit(t, req);
		else
			undo_fuse_qos_inherit(t, req);
	}
out:
	put_task_struct(t);
}

static void android_vh_fuse_request_fetch(void *unused, struct fuse_req *req, struct task_struct *self)
{
	spinlock_t *lock = (spinlock_t *)((char *)req + 0x70);
	struct list_head *head = (struct list_head *)((char *)req + 0x78);
	struct task_struct *task;
	struct qi_vendor *v;
	unsigned long flags;

	if (!is_qi_feat_enabled(9))
		return;
	spin_lock_irqsave(lock, flags);
	if (head->next == head)
		goto out_unlock;
	task = *(struct task_struct **)((char *)head->next - 0x10);
	if (IS_ERR_OR_NULL(task))
		goto out_unlock;
	get_task_struct(task);
	spin_unlock_irqrestore(lock, flags);
	if (is_qos_task(task)) {
		v = android_task_vendor_data(current);
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = req;
			v->iter_depth = 1;
		}
		if (!do_fuse_qos_inherit(req, task, self))
			iter_qos_inherit(self, req);
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = NULL;
			v->iter_depth = 0;
		}
	} else {
		v = android_task_vendor_data(task);
		if (v && v->f1_b4) {
			spin_lock_irqsave(&v->lock, flags);
			v->req = req;
			v->f30 = self->pid;
			v->f1_lo = 5;
			spin_unlock_irqrestore(&v->lock, flags);
		}
	}
	put_task_struct(task);
	return;
out_unlock:
	spin_unlock_irqrestore(lock, flags);
}
static noinline void undo_fuse_qos_inherit(struct task_struct *p, struct fuse_req *req)
{
	struct qi_struct *qi = get_task_qi(p);
	int ret;

	if (qi && (atomic_read(&qi->flag) & BIT(QOS_FUSE))) {
		ret = undo_qos_inherit(p, qi, (unsigned long)req, QOS_FUSE);
		trace_qi_fuse_uninherit(ret, p, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
					(unsigned long)req);
	}
}

static void android_vh_fuse_request_end(void *unused, struct fuse_req *req, struct task_struct *self)
{
	struct qi_vendor *v = android_task_vendor_data(self);
	unsigned long flags;

	if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->f30 = -1;
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	if (is_qos_task(self))
		undo_fuse_qos_inherit(current, req);
}

int qi_vh_fuse_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_fuse_request_fetch(android_vh_fuse_request_fetch, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_fuse_request_end_ext(android_vh_fuse_request_end, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_fuse: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_fuse_unregister(void)
{
	unregister_trace_android_vh_fuse_request_end_ext(android_vh_fuse_request_end, NULL);
	unregister_trace_android_vh_fuse_request_fetch(android_vh_fuse_request_fetch, NULL);
}

/* ---- futex ---- */
static __used noinline int do_futex_qos_inherit(struct task_struct *from, unsigned long lock, struct task_struct *to)
{
	struct qi_struct *qi = get_alloc_task_qi(to);
	int ret;

	if (!qi)
		return -EPERM;
	ret = do_qos_inherit(from, to, lock, QOS_FUTEX);
	trace_qi_futex_inherit(ret, from, to, lock, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	return ret;
}

/* req = futex word seen by android_vh_do_futex: owner tid in the upper 16 bits, low 16 bits are the lock key */
void iter_futex_qos_inherit(struct task_struct *p, void *req_)
{
	unsigned long req = (unsigned long)req_;
	struct qi_struct *qi = get_task_qi(p), *tqi;
	struct task_struct *t;
	struct qi_vendor *cv;
	unsigned long lock;
	pid_t pid;
	int ret, st;

	if (!qi)
		return;
	pid = req >> 16;
	if (!pid)
		return;
	if (current->pid == pid || p->pid == pid)
		return;
	rcu_read_lock();
	t = find_task_by_vpid(pid);
	if (!t) {
		rcu_read_unlock();
		return;
	}
	lock = req & 0xffff;
	get_task_struct(t);
	rcu_read_unlock();
	cv = android_task_vendor_data(current);
	if (cv) {
		st = cv->iter_depth;
		if (st != 3) {
			cv->iter[st] = (void *)req;
			cv->iter_depth = st + 1;
		}
	}
	ret = do_futex_qos_inherit(p, lock, t);
	if (!ret) {
		trace_qi_iter_futex_inherit(p, pid, lock);
		if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && qi_vendor_req(p) == (void *)req) {
			qi->futex_owner = req;
			iter_qos_inherit(t, (void *)req);
		} else {
			tqi = get_task_qi(t);
			ret = undo_qos_inherit(t, tqi, lock, QOS_FUTEX);
			trace_qi_futex_uninherit(ret, t, lock, atomic_read(&tqi->static_qos), atomic_read(&tqi->dyn_qos));
		}
	}
	put_task_struct(t);
}

static void android_vh_do_futex(void *unused, int cmd, unsigned int *flags, u32 __user *uaddr2)
{
	struct task_struct *cur;
	struct qi_struct *qi;
	struct qi_vendor *v;
	u32 val;
	int err;

	if (!is_qi_feat_enabled(8) || !uaddr2)
		return;
	if (cmd != FUTEX_WAIT && cmd != FUTEX_WAIT_BITSET)
		return;
	cur = current;
	pagefault_disable();
	err = get_user(val, uaddr2);
	pagefault_enable();
	if (err)
		return;
	if (val < 0x10000)
		return;
	qi = get_task_qi(cur);
	if (qi && is_qos_task(cur)) {
		qi->futex_owner = val;
		return;
	}
	v = android_task_vendor_data(cur);
	if (v)
		v->futex_owner = val;
}
static void android_vh_futex_wait_queue_start(void *unused, u32 __user *uaddr, unsigned int flags, u32 bitset) { /* STUB */ }
static void android_vh_futex_wait_end(void *unused, unsigned int flags, u32 bitset) { /* STUB */ }
static void android_vh_futex_wake_traverse_plist(void *unused, struct plist_head *chain, int *target_nr,
						 union futex_key key, u32 bitset)
{
	struct task_struct *cur = current;
	struct qi_struct *qi;

	get_task_struct(cur);
	qi = get_task_qi(cur);
	if (qi && (atomic_read(&qi->flag) & BIT(4)))
		*target_nr = (((u32)key.both.word + key.both.offset) >> 2) & 0xffff;
	else
		*target_nr = -1;
	put_task_struct(cur);
}
static void android_vh_futex_wake_up_q(void *unused, int nr_wake, int target_nr)
{
	struct task_struct *cur;
	struct qi_struct *qi;
	int ret;

	if (target_nr == -1)
		return;
	cur = current;
	get_task_struct(cur);
	qi = get_task_qi(cur);
	if (qi && (atomic_read(&qi->flag) & BIT(4))) {
		ret = undo_qos_inherit(cur, qi, target_nr, 4);
		trace_qi_futex_uninherit(ret, cur, target_nr, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	}
	put_task_struct(cur);
}

int qi_vh_futex_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_do_futex(android_vh_do_futex, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_futex_wait_queue_start(android_vh_futex_wait_queue_start, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_futex_wait_end(android_vh_futex_wait_end, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_futex_wake_traverse_plist(android_vh_futex_wake_traverse_plist, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_futex_wake_up_q_finish(android_vh_futex_wake_up_q, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_futex: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_futex_unregister(void)
{
	unregister_trace_android_vh_futex_wake_up_q_finish(android_vh_futex_wake_up_q, NULL);
	unregister_trace_android_vh_futex_wake_traverse_plist(android_vh_futex_wake_traverse_plist, NULL);
	unregister_trace_android_vh_futex_wait_end(android_vh_futex_wait_end, NULL);
	unregister_trace_android_vh_futex_wait_queue_start(android_vh_futex_wait_queue_start, NULL);
	unregister_trace_android_vh_do_futex(android_vh_do_futex, NULL);
}

/* ---- mutex ---- */
static void android_vh_mutex_lock_released(void *unused, struct mutex *lock, unsigned long settime_jiffies)
{
	if (settime_jiffies)
		return;
	if (is_qos_task(current))
		undo_mutex_qos_inherit(lock);
}

static void android_vh_mutex_unlock_slowpath(void *unused, struct mutex *lock)
{
	if (is_qos_task(current))
		undo_mutex_qos_inherit(lock);
}

void os_cpu_android_vh_alter_mutex_list_add(void *unused, struct mutex *lock, struct mutex_waiter *waiter,
					    struct list_head *list, bool *already_on_list GKPAD(fcrcpSKVvx3Mb5))
{
	if (!is_qi_feat_enabled(3))
		return;
	if (IS_ERR_OR_NULL(lock) || IS_ERR_OR_NULL(waiter) || IS_ERR_OR_NULL(list) || IS_ERR_OR_NULL(already_on_list))
		return;
	get_task_struct(waiter->task);
	if (is_qos_task(waiter->task))
		do_mutex_jump(lock, waiter, already_on_list);
	put_task_struct(waiter->task);
}
EXPORT_SYMBOL_GPL(os_cpu_android_vh_alter_mutex_list_add);

static __used noinline int do_mutex_qos_inherit(struct mutex *lock, struct task_struct *from, struct task_struct *to)
{
	struct qi_struct *qi = get_alloc_task_qi(to);
	int ret;

	if (!qi)
		return -ENOMEM;
	ret = do_qos_inherit(from, to, (unsigned long)lock, QOS_MUTEX);
	trace_qi_mutex_inherit(ret, from, to, (unsigned long)lock, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	return ret;
}

/* p waits for a mutex: lend its qos to the owner, or (no owner yet) queue p's waiter in front of the first normal waiter */
void iter_mutex_qos_inherit(struct task_struct *p, void *req)
{
	struct mutex *lock = req;
	struct mutex_waiter *w, *first = NULL;
	struct task_struct *owner;
	struct qi_vendor *cv;
	struct qi_struct *qi;
	unsigned long flags;
	int ret, st;
	bool rt;

	if (!(p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)))
		return;
	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	owner = (struct task_struct *)(atomic_long_read(&lock->owner) & ~7UL);
	if (owner == p)
		goto unlock;
	if (owner) {
		get_task_struct(owner);
		cv = android_task_vendor_data(current);
		if (cv) {
			st = cv->iter_depth;
			if (st != 3) {
				cv->iter[st] = lock;
				cv->iter_depth = st + 1;
			}
		}
		ret = do_mutex_qos_inherit(lock, p, owner);
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		if (!ret) {
			trace_qi_iter_mutex_inherit(p, owner, (unsigned long)lock);
			if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && qi_vendor_req(p) == lock) {
				iter_qos_inherit(owner, lock);
			} else {
				qi = get_task_qi(owner);
				ret = undo_qos_inherit(owner, qi, (unsigned long)lock, QOS_MUTEX);
				trace_qi_mutex_uninherit(ret, owner, atomic_read(&qi->static_qos),
							 atomic_read(&qi->dyn_qos), (unsigned long)lock);
			}
		}
		put_task_struct(owner);
		return;
	}
	list_for_each_entry(w, &lock->wait_list, list) {
		if (w->task == p) {
			if (first)
				list_move_tail(&w->list, &first->list);
			break;
		}
		rt = is_qos_rt_task(w->task);
		first = (rt || first) ? first : w;
	}
unlock:
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
}

static void android_vh_mutex_wait_start(void *unused, struct mutex *lock)
{
	struct task_struct *cur, *owner;
	struct qi_vendor *v;
	unsigned long flags;
	bool qos;

	if (!is_qi_feat_enabled(3))
		return;
	if (is_qi_feat_enabled(13)) {
		cur = current;
		if (is_qos_task(cur)) {
			v = android_task_vendor_data(cur);
			if (v)
				v->f0 |= 1;
			atomic64_inc((atomic64_t *)&lock->android_oem_data1[0]);
			trace_qos_mutex_wait(true, cur->pid, atomic64_read((atomic64_t *)&lock->android_oem_data1[0]));
		}
	}
	if (IS_ERR_OR_NULL(lock))
		return;
	owner = (struct task_struct *)(atomic_long_read(&lock->owner) & ~7UL);
	if (!owner)
		return;
	cur = current;
	qos = is_qos_task(cur);
	v = android_task_vendor_data(cur);
	if (qos) {
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = lock;
			v->iter_depth = 1;
		}
		if (!do_mutex_qos_inherit(lock, cur, owner))
			iter_qos_inherit(owner, lock);
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = NULL;
			v->iter_depth = 0;
		}
	} else if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = lock;
		v->f1_lo = 1;
		spin_unlock_irqrestore(&v->lock, flags);
	}
}
static void android_vh_mutex_wait_finish(void *unused, struct mutex *lock)
{
	struct task_struct *cur = current;
	struct qi_vendor *v = android_task_vendor_data(cur);
	unsigned long flags;

	if (!v)
		return;
	if (v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	if (!(v->f0 & BIT(0)))
		return;
	atomic64_dec_if_positive((atomic64_t *)&lock->android_oem_data1[0]);
	v->f0 &= ~BIT(0);
	trace_qos_mutex_wait(false, cur->pid, atomic64_read((atomic64_t *)&lock->android_oem_data1[0]));
}
static void android_vh_mutex_opt_spin_start(void *unused, struct mutex *lock, bool *time_out, int *cnt)
{
	struct task_struct *cur;
	struct qi_struct *qi;
	u64 delta;
	bool crit;

	if (!is_qi_feat_enabled(13))
		return;
	cur = current;
	qi = get_task_qi(cur);
	if (!qi)
		return;
	if (!qi->opt_spin_start_time) {
		qi->opt_spin_start_time = sched_clock();
		return;
	}
	(*cnt)++;
	if (*cnt & 0xf)
		return;
	delta = sched_clock() - qi->opt_spin_start_time;
	crit = is_qos_task(cur);
	if ((!crit && delta > sysctl_mutex_normal_opt_spin_time_ns) ||
	    (crit && delta > sysctl_mutex_qos_opt_spin_time_ns)) {
		*time_out = true;
		trace_qos_mutex_opt_spin(cur->pid, crit, qi->opt_spin_start_time, delta);
	}
}

static void android_vh_mutex_opt_spin_finish(void *unused, struct mutex *lock, bool taken)
{
	struct qi_struct *qi;

	if (!is_qi_feat_enabled(13))
		return;
	qi = get_task_qi(current);
	if (qi && qi->opt_spin_start_time)
		qi->opt_spin_start_time = 0;
}

static void android_vh_mutex_can_spin_on_owner(void *unused, struct mutex *lock, int *retval)
{
	struct task_struct *cur;
	u64 v;

	if (!is_qi_feat_enabled(13))
		return;
	cur = current;
	if (is_qos_task(cur))
		return;
	if (cur->prio < 100)
		return;
	v = READ_ONCE(lock->android_oem_data1[0]);
	if (v > 0 && v < 1000)
		*retval = 0;
}

int qi_vh_mutex_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_record_mutex_lock_starttime(android_vh_mutex_lock_released, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_unlock_slowpath(android_vh_mutex_unlock_slowpath, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_wait_start(android_vh_mutex_wait_start, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_wait_finish(android_vh_mutex_wait_finish, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_opt_spin_start(android_vh_mutex_opt_spin_start, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_opt_spin_finish(android_vh_mutex_opt_spin_finish, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_can_spin_on_owner(android_vh_mutex_can_spin_on_owner, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_mutex: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_mutex_unregister(void)
{
	unregister_trace_android_vh_mutex_wait_start(android_vh_mutex_wait_start, NULL);
	unregister_trace_android_vh_mutex_wait_finish(android_vh_mutex_wait_finish, NULL);
	unregister_trace_android_vh_mutex_unlock_slowpath(android_vh_mutex_unlock_slowpath, NULL);
	unregister_trace_android_vh_record_mutex_lock_starttime(android_vh_mutex_lock_released, NULL);
	unregister_trace_android_vh_mutex_opt_spin_start(android_vh_mutex_opt_spin_start, NULL);
	unregister_trace_android_vh_mutex_opt_spin_finish(android_vh_mutex_opt_spin_finish, NULL);
	unregister_trace_android_vh_mutex_can_spin_on_owner(android_vh_mutex_can_spin_on_owner, NULL);
}

/* ---- percpu rwsem ---- */
static __used noinline int do_percpu_rwsem_qos_inherit(struct percpu_rw_semaphore *sem, struct task_struct *from, struct task_struct *to)
{
	struct qi_struct *qi = get_alloc_task_qi(to);
	int ret;

	if (!qi)
		return -ENOMEM;
	ret = do_qos_inherit(from, to, (unsigned long)sem, QOS_PERCPU_RWSEM);
	trace_qi_percpu_rwsem_inherit(ret, from, to, (unsigned long)sem, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	return ret;
}

/* p waits for a percpu rwsem: the writer, or (no writer yet) the task recorded by record_pcpu_rwsem_starttime, inherits p's qos */
void iter_percpu_rwsem_qos_inherit(struct task_struct *p, void *req)
{
	struct percpu_rw_semaphore *sem = req;
	struct task_struct *writer, *owner;
	struct qi_vendor *cv;
	struct qi_struct *qi;
	unsigned long flags, handle = 0;
	pid_t pid;
	int ret, st;

	if (!(p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)))
		return;
	spin_lock_irqsave(&sem->waiters.lock, flags);
	rcu_read_lock();
	smp_mb();
	writer = rcu_dereference(sem->writer.task);
	if (writer) {
		get_task_struct(writer);
		owner = writer;
	} else {
		rcu_read_unlock();
		if (!atomic_read(&sem->block))
			goto unlock;
		handle = sem->android_oem_data1;
		if ((u32)(handle ^ (unsigned long)sem))
			goto unlock;
		pid = handle >> 32;
		if (!pid)
			goto unlock;
		rcu_read_lock();
		owner = find_task_by_vpid(pid);
		if (!owner) {
			rcu_read_unlock();
			goto unlock;
		}
		get_task_struct(owner);
	}
	rcu_read_unlock();
	if (owner == p || owner == current) {
		spin_unlock_irqrestore(&sem->waiters.lock, flags);
		goto put;
	}
	cv = android_task_vendor_data(current);
	if (cv) {
		st = cv->iter_depth;
		if (st != 3) {
			cv->iter[st] = sem;
			cv->iter_depth = st + 1;
		}
	}
	ret = do_percpu_rwsem_qos_inherit(sem, p, owner);
	spin_unlock_irqrestore(&sem->waiters.lock, flags);
	if (ret)
		goto put;
	trace_qi_iter_percpu_rwsem_inherit(p, owner, (unsigned long)sem);
	if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && qi_vendor_req(p) == sem &&
	    (writer || (atomic_read(&sem->block) && handle == sem->android_oem_data1))) {
		iter_qos_inherit(owner, sem);
	} else {
		qi = get_task_qi(owner);
		ret = undo_qos_inherit(owner, qi, (unsigned long)sem, QOS_PERCPU_RWSEM);
		trace_qi_percpu_rwsem_uninherit(ret, owner, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
						(unsigned long)sem);
	}
put:
	put_task_struct(owner);
	return;
unlock:
	spin_unlock_irqrestore(&sem->waiters.lock, flags);
}

static void android_vh_percpu_rwsem_init(void *unused, struct percpu_rw_semaphore *sem)
{
	sem->android_oem_data1 = 0;
}

static void android_vh_record_pcpu_rwsem_starttime(void *unused, struct percpu_rw_semaphore *sem,
						   unsigned long settime_jiffies)
{
	struct task_struct *cur;
	struct qi_vendor *v;
	struct qi_struct *qi;
	unsigned long flags;
	int ret;

	if (settime_jiffies) {
		if (atomic_read(&sem->block))
			sem->android_oem_data1 = ((u64)current->pid << 32) | ((u64)sem & 0xffffffff);
		return;
	}
	cur = current;
	v = android_task_vendor_data(cur);
	if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	sem->android_oem_data1 = 0;
	qi = get_task_qi(cur);
	if (qi && (atomic_read(&qi->flag) & BIT(3))) {
		ret = undo_qos_inherit(cur, qi, (unsigned long)sem, 3);
		trace_qi_percpu_rwsem_uninherit(ret, cur, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
						(unsigned long)sem);
	}
}
static void android_vh_percpu_rwsem_wq_add(void *unused, struct percpu_rw_semaphore *sem, bool reader) { /* STUB */ }

int qi_vh_percpu_rwsem_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_percpu_rwsem_init(android_vh_percpu_rwsem_init, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_record_pcpu_rwsem_starttime(android_vh_record_pcpu_rwsem_starttime, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_percpu_rwsem_wq_add(android_vh_percpu_rwsem_wq_add, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_percpu_rwsem: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_percpu_rwsem_unregister(void)
{
	unregister_trace_android_vh_percpu_rwsem_init(android_vh_percpu_rwsem_init, NULL);
	unregister_trace_android_vh_percpu_rwsem_wq_add(android_vh_percpu_rwsem_wq_add, NULL);
	unregister_trace_android_vh_record_pcpu_rwsem_starttime(android_vh_record_pcpu_rwsem_starttime, NULL);
}

/* ---- rcu ---- */
static void android_vh_sync_rcu_wait_start(void *unused, struct task_struct *self)
{
	unsigned long flags;
	int idx;

	if (!is_qi_feat_enabled(10))
		return;
	if (!is_qos_task(self))
		return;
	spin_lock_irqsave(&g_rcu_info.lock, flags);
	idx = g_rcu_info.head;
	g_rcu_info.tasks[idx] = self;
	g_rcu_info.head = idx == RCU_INFO_MAX - 1 ? 0 : idx + 1;
	if (g_rcu_info.count < RCU_INFO_MAX)
		g_rcu_info.count++;
	spin_unlock_irqrestore(&g_rcu_info.lock, flags);
}
static void android_vh_sync_rcu_wait_end(void *unused, struct task_struct *self)
{
	unsigned long flags;
	int i, idx, last;

	spin_lock_irqsave(&g_rcu_info.lock, flags);
	if (!g_rcu_info.count)
		goto out;
	last = g_rcu_info.head - 1;
	idx = g_rcu_info.head > 0 ? last : RCU_INFO_MAX - 1;
	for (i = 0; i < g_rcu_info.count; i++) {
		if (g_rcu_info.tasks[idx] == self) {
			if (idx == last)
				g_rcu_info.head = last;
			g_rcu_info.tasks[idx] = NULL;
			g_rcu_info.count--;
			break;
		}
		idx = idx == 0 ? RCU_INFO_MAX - 1 : idx - 1;
	}
out:
	spin_unlock_irqrestore(&g_rcu_info.lock, flags);
}
static void android_vh_rcu_boost_start(void *unused, struct rt_mutex *mtx, struct task_struct *t) { /* STUB */ }
static void android_vh_rcu_boost_end(void *unused, struct rt_mutex *mtx, struct task_struct *t) { /* STUB */ }

int qi_vh_rcu_register(void)
{
	int ret = 0, err;

	spin_lock_init(&g_rcu_info.lock);
	err = register_trace_android_vh_sync_rcu_wait_start(android_vh_sync_rcu_wait_start, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_sync_rcu_wait_end(android_vh_sync_rcu_wait_end, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_rcu_boost_start(android_vh_rcu_boost_start, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_rcu_boost_end(android_vh_rcu_boost_end, NULL);
	if (err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_rcu: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_rcu_unregister(void)
{
	unregister_trace_android_vh_sync_rcu_wait_start(android_vh_sync_rcu_wait_start, NULL);
	unregister_trace_android_vh_sync_rcu_wait_end(android_vh_sync_rcu_wait_end, NULL);
	unregister_trace_android_vh_rcu_boost_start(android_vh_rcu_boost_start, NULL);
	unregister_trace_android_vh_rcu_boost_end(android_vh_rcu_boost_end, NULL);
}

/* ---- rwsem ---- */
static noinline int do_rwsem_qos_inherit(struct rw_semaphore *sem, struct task_struct *from, struct task_struct *to)
{
	struct qi_struct *qi;
	int ret;

	get_task_struct(to);
	qi = get_alloc_task_qi(to);
	if (!qi) {
		put_task_struct(to);
		return -ENOMEM;
	}
	ret = do_qos_inherit(from, to, (unsigned long)sem, QOS_RWSEM);
	trace_qi_rwsem_inherit(ret, from, to, (unsigned long)sem, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
	put_task_struct(to);
	return ret;
}

/* per-mmap_lock reader bookkeeping hung off sem->android_oem_data1[0] */
struct rwsem_rd_info {
	spinlock_t lock;
	int cnt;
	u64 owner;
	u64 slot[10];
};

/* p waits for a rwsem: a writer owner inherits p's qos */
void iter_rwsem_qos_inherit(struct task_struct *p, void *req)
{
	struct rw_semaphore *sem = req;
	struct task_struct *owner;
	struct qi_vendor *cv;
	struct qi_struct *qi;
	unsigned long flags, val;
	int ret, st;

	if (!(p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)))
		return;
	if (!raw_spin_trylock_irqsave(&sem->wait_lock, flags))
		return;
	val = atomic_long_read(&sem->owner);
	if (val & 1)
		goto unlock;
	owner = (struct task_struct *)(val & ~3UL);
	if (!owner || owner == p || current == owner)
		goto unlock;
	get_task_struct(owner);
	cv = android_task_vendor_data(current);
	if (cv) {
		st = cv->iter_depth;
		if (st != 3) {
			cv->iter[st] = sem;
			cv->iter_depth = st + 1;
		}
	}
	ret = do_rwsem_qos_inherit(sem, p, owner);
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	if (likely(!ret)) {
		trace_qi_iter_rwsem_inherit(p, owner, (unsigned long)sem);
		if ((p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) && qi_vendor_req(p) == sem) {
			iter_qos_inherit(owner, sem);
		} else {
			qi = get_task_qi(owner);
			ret = undo_qos_inherit(owner, qi, (unsigned long)sem, QOS_RWSEM);
			trace_qi_rwsem_uninherit(ret, owner, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
						 (unsigned long)sem);
		}
	}
	put_task_struct(owner);
	return;
unlock:
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
}

static void android_vh_alter_rwsem_list_add(void *unused, struct rwsem_waiter *waiter,
					    struct rw_semaphore *sem, bool *already_on_list) { /* STUB */ }

static void android_vh_clear_rwsem_writer_owned(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *cur;

	if (!sem)
		return;
	cur = current;
	if (is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);
}

static void android_vh_clear_rwsem_reader_owned(void *unused, struct rw_semaphore *sem) { /* STUB */ }

static void android_vh_rwsem_direct_rsteal(void *unused, struct rw_semaphore *sem, bool *steal)
{
	if (is_qi_feat_enabled(13) && osq_is_locked(&sem->osq))
		*steal = false;
}

static void android_vh_rwsem_opt_rspin(void *unused, struct rw_semaphore *sem, long *adjustment, bool *rspin) { /* STUB */ }
static void android_vh_rwsem_write_wait_finish(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *cur = current;
	struct qi_vendor *v = android_task_vendor_data(cur);
	struct rwsem_rd_info *info;
	struct task_struct *owner;
	unsigned long flags, val;
	int i;

	if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	val = atomic_long_read(&sem->owner);
	owner = (val & 1) ? NULL : (struct task_struct *)(val & ~3UL);
	if (owner != cur && is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);

	if (sem->android_vendor_data1 != (u64)sem)
		return;
	info = (struct rwsem_rd_info *)sem->android_oem_data1[0];
	if (!info)
		return;
	spin_lock_irqsave(&info->lock, flags);
	info->owner = 0;
	if (info->cnt) {
		for (i = 0; i < 10; i++)
			if (info->slot[i])
				info->slot[i] = 0;
		info->cnt = 0;
	}
	spin_unlock_irqrestore(&info->lock, flags);
}
static void android_vh_rwsem_read_wait_finish(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *cur = current;
	struct qi_vendor *v = android_task_vendor_data(cur);
	unsigned long flags;
	long val;

	if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	if (is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);
	if (!v || !(v->f0 & BIT(1)))
		return;
	v->f0 &= ~BIT(1);
	val = atomic_long_read(&sem->count);
	while (val & RWSEM_FLAG_HANDOFF_Q) {
		if (atomic_long_try_cmpxchg_acquire(&sem->count, &val, val & ~RWSEM_FLAG_HANDOFF_Q)) {
			trace_qos_rwsem_wait(false, cur->pid, atomic_long_read(&sem->count));
			break;
		}
	}
}
static void android_vh_rwsem_read_trylock_failed(void *unused, struct rw_semaphore *sem, long *cntp, int *ret) { /* STUB */ }
static void android_vh_record_rwsem_lock_starttime(void *unused, struct rw_semaphore *sem,
						   unsigned long settime_jiffies)
{
	struct rwsem_rd_info *info;
	unsigned long flags, val;
	u64 *slot;
	int i;

	if (!is_qi_feat_enabled(5) || !settime_jiffies)
		return;
	if (sem->android_vendor_data1 != (u64)sem)
		return;
	val = atomic_long_read(&sem->count);
	if (val & 1) {
		if (sem->android_vendor_data1 != (u64)sem)
			return;
		info = (struct rwsem_rd_info *)sem->android_oem_data1[0];
		if (!info)
			return;
		spin_lock_irqsave(&info->lock, flags);
		info->owner = 0;
		if (info->cnt) {
			for (i = 0; i < 10; i++)
				if (info->slot[i])
					info->slot[i] = 0;
			info->cnt = 0;
		}
	} else {
		info = (struct rwsem_rd_info *)sem->android_oem_data1[0];
		if (!info)
			return;
		spin_lock_irqsave(&info->lock, flags);
		if (info->cnt != 10) {
			for (i = 0; i < 10; i++) {
				slot = &info->slot[i];
				if (likely(*slot))
					continue;
				if (unlikely(info->owner == (u64)current))
					continue;
				*slot = (u64)current;
				info->cnt++;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

static void android_vh_mmap_lock_init(void *unused, struct rw_semaphore *sem)
{
	if (sem)
		sem->android_vendor_data1 = (u64)sem;
}

static void android_vh_mmap_lock_free(void *unused, struct rw_semaphore *sem)
{
	void *p;

	if (!sem)
		return;
	p = (void *)sem->android_oem_data1[0];
	sem->android_vendor_data1 = 0;
	sem->android_oem_data1[0] = 0;
	if (p)
		kfree(p);
}

static void android_vh_rwsem_init(void *unused, struct rw_semaphore *sem)
{
	if (sem) {
		sem->android_vendor_data1 = 0;
		sem->android_oem_data1[0] = 0;
	}
}

int qi_vh_rwsem_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_alter_rwsem_list_add(android_vh_alter_rwsem_list_add, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_clear_rwsem_writer_owned(android_vh_clear_rwsem_writer_owned, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_clear_rwsem_reader_owned(android_vh_clear_rwsem_reader_owned, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_direct_rsteal(android_vh_rwsem_direct_rsteal, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_optimistic_rspin(android_vh_rwsem_opt_rspin, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_write_wait_finish(android_vh_rwsem_write_wait_finish, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_read_wait_finish(android_vh_rwsem_read_wait_finish, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_read_trylock_failed(android_vh_rwsem_read_trylock_failed, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_record_rwsem_lock_starttime(android_vh_record_rwsem_lock_starttime, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mmap_lock_init(android_vh_mmap_lock_init, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_mmap_lock_free(android_vh_mmap_lock_free, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_rwsem_init(android_vh_rwsem_init, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_rwsem: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_rwsem_unregister(void)
{
	unregister_trace_android_vh_alter_rwsem_list_add(android_vh_alter_rwsem_list_add, NULL);
	unregister_trace_android_vh_clear_rwsem_writer_owned(android_vh_clear_rwsem_writer_owned, NULL);
	unregister_trace_android_vh_clear_rwsem_reader_owned(android_vh_clear_rwsem_reader_owned, NULL);
	unregister_trace_android_vh_rwsem_direct_rsteal(android_vh_rwsem_direct_rsteal, NULL);
	unregister_trace_android_vh_rwsem_optimistic_rspin(android_vh_rwsem_opt_rspin, NULL);
	unregister_trace_android_vh_rwsem_read_wait_finish(android_vh_rwsem_read_wait_finish, NULL);
	unregister_trace_android_vh_rwsem_write_wait_finish(android_vh_rwsem_write_wait_finish, NULL);
	unregister_trace_android_vh_record_rwsem_lock_starttime(android_vh_record_rwsem_lock_starttime, NULL);
	unregister_trace_android_vh_rwsem_read_trylock_failed(android_vh_rwsem_read_trylock_failed, NULL);
	unregister_trace_android_vh_mmap_lock_init(android_vh_mmap_lock_init, NULL);
	unregister_trace_android_vh_mmap_lock_free(android_vh_mmap_lock_free, NULL);
	unregister_trace_android_vh_rwsem_init(android_vh_rwsem_init, NULL);
}

/* ---- xr_fbt ioctl 0x400c780a ---- */
int qi_stats_register(void)
{
	return register_xr_fbt(XR_FBT_QOS_EVENT, qos_event_handler);
}

void qi_stats_unregister(void)
{
	unregister_xr_fbt(XR_FBT_QOS_EVENT);
}
