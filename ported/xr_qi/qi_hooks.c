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
#include <linux/osq_lock.h>
#include <linux/sched/clock.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"
#include "trace_xr_qi.h"

#define RWSEM_FLAG_HANDOFF_Q	4UL
#define RWSEM_WRITER_LOCKED	(1UL << 0)
#define RWSEM_READER_OWNED	(1UL << 0)
#define RWSEM_NONSPINNABLE	(1UL << 1)
#define RWSEM_READER_BIAS	(1UL << 8)
enum owner_state { OWNER_NULL = 1, OWNER_WRITER = 2, OWNER_READER = 4, OWNER_NONSPINNABLE = 8 };
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

extern struct atomic_notifier_head g_qievent_notif_chain __asm__("g_qievent_notif_chain.llvm.6505585393509743323")
	__attribute__((visibility("hidden")));
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

/*
 * G11 (from the stock, 243 insns): the binder driver is about to set the priority of the thread that handles t. caller = the sending task
 * (t->from->task, else current), oneway = t->flags & TF_ONE_WAY (t+0x6c). A qos caller: a qos target only gets QIEVENT_QOS_CORRECT, a normal target
 * inherits (do_binder_qos_inherit + iter_qos_inherit; a oneway transaction clears qi->async_binder_todo); a normal caller on a SYNC transaction
 * records the request in its own vendor area (req = t, f30 = target pid, wait state 0) and the target remembers the caller pid (f4).
 */
static void android_vh_binder_set_priority(void *unused, struct binder_transaction *t, struct task_struct *task)
{
	struct qievent_struct ev = {};
	struct task_struct *caller;
	struct qi_vendor *v, *tv;
	struct qi_struct *qi, *tqi;
	unsigned long flags;
	u8 *buffer;
	u32 tflags;
	void *from;

	if (!is_qi_feat_enabled(1) || !t || !task)
		return;
	buffer = *(u8 **)((u8 *)t + 0x60);
	if (!buffer)
		return;
	if (!(buffer[0x28] & BIT(3)) && !(*((u8 *)t + 0x58) & BIT(0)))
		return;
	from = *(void **)((u8 *)t + 0x28);
	caller = from ? *(struct task_struct **)((u8 *)from + 0x1b8) : current;
	tflags = *(u32 *)((u8 *)t + 0x6c);
	get_task_struct(caller);
	qi = get_task_qi(caller);
	if (unlikely(!qi)) {
		put_task_struct(caller);
		return;
	}
	if (is_qos_task(caller)) {
		if ((tflags & 1) && !qi->async_binder_todo)
			goto put_caller;
		get_task_struct(task);
		if (is_qos_task(task)) {
			tqi = get_task_qi(task);
			spin_lock_irqsave(&tqi->lock, flags);
			ev.to = task;
			ev.from_qos.qos = get_task_qos(task);
			ev.prio = get_task_prio(task);
			atomic_notifier_call_chain(&g_qievent_notif_chain, QIEVENT_QOS_CORRECT, &ev);
			spin_unlock_irqrestore(&tqi->lock, flags);
		} else {
			v = android_task_vendor_data(current);
			if (v) {
				v->iter_a = NULL;
				v->iter_b = NULL;
				v->iter_ptr = t;
				v->iter_depth = 1;
			}
			if (!do_binder_qos_inherit(t, caller, task, tflags & 1))
				iter_qos_inherit(task, t);
			if (v) {
				v->iter_a = NULL;
				v->iter_b = NULL;
				v->iter_ptr = NULL;
				v->iter_depth = 0;
			}
			if ((tflags & 1) && qi->async_binder_todo)
				qi->async_binder_todo = false;
		}
		put_task_struct(task);
		put_task_struct(caller);
		return;
	} else if (!(tflags & 1)) {
		v = android_task_vendor_data(current);
		if (v && v->f1_b4) {
			spin_lock_irqsave(&v->lock, flags);
			v->req = t;
			v->f30 = task->pid;
			v->f1_lo = 0;
			spin_unlock_irqrestore(&v->lock, flags);
			tv = android_task_vendor_data(task);
			if (tv)
				tv->f4 = caller->pid;
		}
	}
put_caller:
	put_task_struct(caller);
}
/* the replying thread restores its priority: the waiter it was waiting for stops waiting, current drops the binder inherit */
static void android_vh_binder_restore_priority(void *unused, struct binder_transaction *t, struct task_struct *task)
{
	struct task_struct *cur, *p;
	struct qi_vendor *v, *pv;
	struct qi_struct *qi;
	unsigned long flags;
	int ret, pid;

	if (!t)
		return;
	cur = current;
	v = android_task_vendor_data(cur);
	if (v) {
		pid = v->f4;
		v->f4 = -1;
		if (pid >= 1) {
			rcu_read_lock();
			p = find_task_by_vpid(pid);
			if (p) {
				pv = android_task_vendor_data(p);
				if (pv && pv->f1_b4) {
					spin_lock_irqsave(&pv->lock, flags);
					pv->f30 = -1;
					pv->req = NULL;
					pv->f1_lo = 7;
					spin_unlock_irqrestore(&pv->lock, flags);
				}
			}
			rcu_read_unlock();
		}
	}
	qi = get_task_qi(cur);
	/* binder_transaction: +0xb8 pid of the replier, +0xa8 lock */
	if (qi && (atomic_read(&qi->flag) & BIT(QOS_BINDER)) && cur && *(int *)((u8 *)t + 0xb8) == cur->pid) {
		spin_lock((spinlock_t *)((u8 *)t + 0xa8));
		ret = undo_qos_inherit(cur, qi, (unsigned long)t, QOS_BINDER);
		if (!ret)
			*(int *)((u8 *)t + 0xb8) = -1;
		trace_qi_binder_restore_priority(ret, (unsigned long)t, cur, atomic_read(&qi->static_qos),
						 atomic_read(&qi->dyn_qos));
		spin_unlock((spinlock_t *)((u8 *)t + 0xa8));
	}
}
/*
 * G11 (from the stock, 216 insns): a qos caller sends a synchronous (need_reply) transaction to "servicemanager": a qos servicemanager only gets a
 * QIEVENT_QOS_CORRECT notification (under its qi lock), a non-qos one inherits the caller's qos (QOS_BINDER, the chain is followed by iter_qos_inherit
 * with the visited-request state of `current` set up like in android_vh_rcu_boost_start); t+0xb8 remembers the pid of the boosted task.
 */
static void android_vh_binder_proc_transaction(void *unused, struct task_struct *caller_task,
					       struct task_struct *binder_proc_task,
					       struct task_struct *binder_th_task, int node_debug_id,
					       struct binder_transaction *t, bool pending_async)
{
	struct qievent_struct ev = {};
	struct qi_vendor *v;
	struct qi_struct *qi;
	unsigned long flags;
	int ret;

	if (!is_qi_feat_enabled(1) || !caller_task || !binder_proc_task || !t)
		return;
	if (strcmp(binder_proc_task->comm, "servicemanager"))
		return;
	if (!(*((u8 *)t + 0x58) & BIT(0)))
		return;
	get_task_struct(caller_task);
	get_task_struct(binder_proc_task);
	qi = get_alloc_task_qi(binder_proc_task);
	if (is_qos_task(caller_task) && qi) {
		if (is_qos_task(binder_proc_task)) {
			spin_lock_irqsave(&qi->lock, flags);
			ev.to = binder_proc_task;
			atomic_notifier_call_chain(&g_qievent_notif_chain, QIEVENT_QOS_CORRECT, &ev);
			spin_unlock_irqrestore(&qi->lock, flags);
		} else {
			v = android_task_vendor_data(current);
			if (v) {
				v->iter_a = NULL;
				v->iter_b = NULL;
				v->iter_ptr = t;
				v->iter_depth = 1;
			}
			ret = do_qos_inherit(caller_task, binder_proc_task, (unsigned long)t, QOS_BINDER);
			trace_qi_binder_proc_transaction(ret, caller_task, binder_proc_task, atomic_read(&qi->static_qos),
							 atomic_read(&qi->dyn_qos));
			if (ret >= 0) {
				*(int *)((u8 *)t + 0xb8) = binder_proc_task->pid;
				iter_qos_inherit(binder_proc_task, t);
			}
			if (v) {
				v->iter_a = NULL;
				v->iter_b = NULL;
				v->iter_ptr = NULL;
				v->iter_depth = 0;
			}
		}
	}
	put_task_struct(binder_proc_task);
	put_task_struct(caller_task);
}
/* a transaction is freed: the task that waited for its reply (from-thread, else looked up by from_pid, else current) stops waiting */
static void android_vh_free_binder_transaction(void *unused, struct binder_transaction *t)
{
	struct task_struct *task = NULL;
	struct qi_vendor *v;
	unsigned long flags;
	bool ref = false;
	void *from;

	/* binder_transaction bitfield byte at +0x58, bit 0 = need_reply; +0x28 from; +0x34 from_pid */
	if (!(*((u8 *)t + 0x58) & BIT(0)))
		return;
	from = *(void **)((u8 *)t + 0x28);
	if (from) {
		task = *(struct task_struct **)((u8 *)from + 0x1b8);
	} else {
		if (*(int *)((u8 *)t + 0x34) >= 1) {
			rcu_read_lock();
			task = find_task_by_vpid(*(int *)((u8 *)t + 0x34));
			ref = task != NULL;
			if (task)
				get_task_struct(task);
			rcu_read_unlock();
		}
	}
	if (!task)
		task = current;
	v = android_task_vendor_data(task);
	if (likely(v) && v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	spin_lock(&g_binder_lock);
	if (v)
		v->f30 = -1;
	spin_unlock(&g_binder_lock);
	if (ref)
		put_task_struct(task);
}
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
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_binder_restore_priority(android_vh_binder_restore_priority, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_binder_proc_transaction(android_vh_binder_proc_transaction, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_free_oem_binder_struct(android_vh_free_binder_transaction, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_binder_buffer_release(android_vh_binder_buffer_release, NULL);
	if ((err < 0) & !ret)
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
	if ((err < 0) & !ret)
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
/* a futex waiter: a qos waiter lends its qos to the lock owner recorded by android_vh_do_futex, a normal one only records the request */
static void android_vh_futex_wait_queue_start(void *unused, u32 __user *uaddr, unsigned int flags, u32 bitset)
{
	struct task_struct *cur, *t;
	struct qi_vendor *v, *cv;
	struct qi_struct *qi;
	unsigned long irqflags, key;
	u32 val;
	int pid;
	int ret;

	if (!is_qi_feat_enabled(8) || !uaddr)
		return;
	cur = current;
	if (is_qos_task(cur)) {
		qi = get_task_qi(cur);
		if (!qi)
			return;
		val = qi->futex_owner;
		if (!val)
			return;
		key = ((unsigned long)uaddr >> 2) & 0xffff;
		if (key != (val & 0xffff))
			return;
		pid = (int)val >> 16;
		rcu_read_lock();
		t = find_task_by_vpid(pid);
		if (!t) {
			rcu_read_unlock();
			return;
		}
		get_task_struct(t);
		rcu_read_unlock();
		cv = android_task_vendor_data(cur);
		if (cv) {
			cv->iter_a = NULL;
			cv->iter_b = NULL;
			cv->iter_ptr = (void *)(long)qi->futex_owner;
			cv->iter_depth = 1;
		}
		ret = do_futex_qos_inherit(cur, key, t);
		if (!ret)
			iter_qos_inherit(t, (void *)(long)qi->futex_owner);
		if (cv) {
			cv->iter_a = NULL;
			cv->iter_b = NULL;
			cv->iter_ptr = NULL;
			cv->iter_depth = 0;
		}
		put_task_struct(t);
		return;
	}
	v = android_task_vendor_data(cur);
	if (!v)
		return;
	val = v->futex_owner;
	v->futex_owner = 0;
	if (!val)
		return;
	if ((((unsigned long)uaddr >> 2) & 0xffff) != (val & 0xffff))
		return;
	if (v->f1_b4) {
		spin_lock_irqsave(&v->lock, irqflags);
		v->req = (void *)(unsigned long)val;
		v->f1_lo = 4;
		spin_unlock_irqrestore(&v->lock, irqflags);
	}
}
/* the futex wait ends: current stops waiting, the lock owner it lent its qos to is restored */
static void android_vh_futex_wait_end(void *unused, unsigned int flags, u32 bitset)
{
	struct task_struct *cur, *t;
	struct qi_vendor *v;
	struct qi_struct *qi, *tqi;
	unsigned long irqflags, val;
	int ret;

	cur = current;
	v = android_task_vendor_data(cur);
	if (v && v->f1_b4) {
		spin_lock_irqsave(&v->lock, irqflags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, irqflags);
	}
	qi = get_task_qi(cur);
	if (!qi || !qi->futex_owner)
		return;
	rcu_read_lock();
	val = qi->futex_owner;
	t = find_task_by_vpid((int)val >> 16);
	if (!t) {
		rcu_read_unlock();
		return;
	}
	get_task_struct(t);
	rcu_read_unlock();
	tqi = get_task_qi(t);
	if (tqi && (atomic_read(&tqi->flag) & BIT(QOS_FUTEX))) {
		val &= 0xffff;
		ret = undo_qos_inherit(t, tqi, val, QOS_FUTEX);
		trace_qi_futex_uninherit(ret, t, val, atomic_read(&tqi->static_qos), atomic_read(&tqi->dyn_qos));
		if (!ret)
			qi->futex_owner = 0;
	}
	put_task_struct(t);
}
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
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_futex_wait_end(android_vh_futex_wait_end, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_futex_wake_traverse_plist(android_vh_futex_wake_traverse_plist, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_futex_wake_up_q_finish(android_vh_futex_wake_up_q, NULL);
	if ((err < 0) & !ret)
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

	/* the stock ignores the result of the first hook */
	register_trace_android_vh_record_mutex_lock_starttime(android_vh_mutex_lock_released, NULL);
	err = register_trace_android_vh_mutex_unlock_slowpath(android_vh_mutex_unlock_slowpath, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_mutex_wait_start(android_vh_mutex_wait_start, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mutex_wait_finish(android_vh_mutex_wait_finish, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mutex_opt_spin_start(android_vh_mutex_opt_spin_start, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mutex_opt_spin_finish(android_vh_mutex_opt_spin_finish, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mutex_can_spin_on_owner(android_vh_mutex_can_spin_on_owner, NULL);
	if ((err < 0) & !ret)
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
static void android_vh_percpu_rwsem_wq_add(void *unused, struct percpu_rw_semaphore *sem, bool reader);

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
	if ((err < 0) & !ret)
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
static void android_vh_rcu_boost_start(void *unused, struct rt_mutex *mtx, struct task_struct *t)
{
	struct task_struct *waiter;
	struct qi_vendor *v;
	struct qi_struct *qi;
	unsigned long flags;
	int idx, ret;

	if (!is_qi_feat_enabled(10))
		return;
	spin_lock_irqsave(&g_rcu_info.lock, flags);
	if (!g_rcu_info.count)
		goto out;
	idx = g_rcu_info.head > 0 ? g_rcu_info.head - 1 : RCU_INFO_MAX - 1;
	waiter = g_rcu_info.tasks[idx];
	if (!is_qos_task(waiter)) {
		g_rcu_info.tasks[idx] = NULL;
		g_rcu_info.head = idx;
		g_rcu_info.count--;
		goto out;
	}
	v = android_task_vendor_data(current);
	if (v) {
		v->iter_a = NULL;
		v->iter_b = NULL;
		v->iter_ptr = mtx;
		v->iter_depth = 1;
	}
	get_task_struct(t);
	qi = get_alloc_task_qi(t);
	if (!qi) {
		put_task_struct(t);
		goto end_iter;
	}
	get_task_struct(waiter);
	ret = do_qos_inherit(waiter, t, (unsigned long)mtx, QOS_RCU);
	trace_qi_rcu_inherit(ret, waiter, t, (unsigned long)mtx, atomic_read(&qi->static_qos),
			     atomic_read(&qi->dyn_qos));
	put_task_struct(waiter);
	if (!ret)
		iter_qos_inherit(t, mtx);
	put_task_struct(t);
end_iter:
	if (v) {
		v->iter_a = NULL;
		v->iter_b = NULL;
		v->iter_ptr = NULL;
		v->iter_depth = 0;
	}
out:
	spin_unlock_irqrestore(&g_rcu_info.lock, flags);
}
static void android_vh_rcu_boost_end(void *unused, struct rt_mutex *mtx, struct task_struct *t)
{
	struct qi_struct *qi;
	int ret;

	if (!is_qos_task(t))
		return;
	get_task_struct(t);
	qi = get_task_qi(t);
	if (qi && (atomic_read(&qi->flag) & BIT(QOS_RCU))) {
		ret = undo_qos_inherit(t, qi, (unsigned long)mtx, QOS_RCU);
		trace_qi_rcu_uninherit(ret, t, atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos),
				       (unsigned long)mtx);
	}
	put_task_struct(t);
}

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

/* take the first recorded reader out of the table and make `new_owner` the table owner; returns the reader */
static noinline struct task_struct *find_next_reader_owner(struct rwsem_rd_info *info, u64 new_owner)
{
	struct task_struct *reader = NULL;
	int i;

	if (info->cnt) {
		for (i = 0; i < 10; i++) {
			reader = (struct task_struct *)info->slot[i];
			if (reader) {
				info->slot[i] = 0;
				info->cnt--;
				break;
			}
		}
	}
	info->owner = new_owner;
	return reader;
}

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

static noinline void iter_ptrs_start(struct task_struct *p, void *ptr);
static noinline void iter_ptrs_end(struct task_struct *p);

/* callsite-inlined list_add_tail() / put_task_struct(): the stock inlines them in the cold queue-jump block (ThinLTO cold threshold) */
static __always_inline void qi_list_add_tail(struct list_head *new, struct list_head *head)
{
	[[clang::always_inline]] __list_add(new, head->prev, head);
}

static __always_inline void qi_put_task_struct(struct task_struct *t)
{
	bool zero;

	[[clang::always_inline]] zero = __refcount_sub_and_test(1, &t->usage, NULL);
	if (zero)
		__put_task_struct(t);
}

/*
 * (G11b, from the stock disassembly) a task queues on a percpu rwsem. Feature 7: a qos task lends its qos to the
 * writer (rcuwait task) or, without writer, to the task recorded by record_pcpu_rwsem_starttime; a normal task only
 * records the request. Feature 6: the waiter just queued (list tail) is a reader (WQ_FLAG_CUSTOM) -> it lends its
 * qos to the writers in front of it; a writer -> it jumps in front of the first reader / non-qos-rt waiter.
 */
static void android_vh_percpu_rwsem_wq_add(void *unused, struct percpu_rw_semaphore *sem, bool reader)
{
	struct task_struct *cur, *writer, *owner, *task;
	struct list_head *head, *own, *pos, *next;
	struct wait_queue_entry *w;
	struct qi_vendor *v;
	struct qi_struct *qi;
	unsigned long flags, handle;
	unsigned int wflags;
	bool first;
	pid_t pid;
	int ret;

	if (!is_qi_feat_enabled(7))
		return;
	if (IS_ERR_OR_NULL(sem))
		return;
	cur = current;
	if (!is_qos_task(cur)) {
		v = android_task_vendor_data(cur);
		if (!v || !v->f1_b4)
			return;
		spin_lock_irqsave(&v->lock, flags);
		v->req = sem;
		v->f1_lo = 3;
		spin_unlock_irqrestore(&v->lock, flags);
		return;
	}
	rcu_read_lock();
	smp_mb();
	writer = rcu_dereference(sem->writer.task);
	if (writer) {
		[[clang::always_inline]] get_task_struct(writer);
		rcu_read_unlock();
		v = android_task_vendor_data(cur);
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = sem;
			v->iter_depth = 1;
		}
		ret = do_percpu_rwsem_qos_inherit(sem, cur, writer);
		if (!ret)
			iter_qos_inherit(writer, sem);
		if (v) {
			v->iter_a = NULL;
			v->iter_b = NULL;
			v->iter_ptr = NULL;
			v->iter_depth = 0;
		}
		qi_put_task_struct(writer);
	} else {
		rcu_read_unlock();
		if (READ_ONCE(sem->block.counter)) {
			handle = sem->android_oem_data1;
			pid = handle >> 32;
			if (!(u32)(handle ^ (unsigned long)sem) && pid) {
				rcu_read_lock();
				owner = find_task_by_vpid(pid);
				if (!owner) {
					rcu_read_unlock();
				} else {
					get_task_struct(owner);
					rcu_read_unlock();
					iter_ptrs_start(cur, sem);
					ret = do_percpu_rwsem_qos_inherit(sem, cur, owner);
					if (!ret) {
						if (READ_ONCE(sem->block.counter) && handle == sem->android_oem_data1) {
							iter_qos_inherit(owner, sem);
						} else {
							qi = get_task_qi(owner);
							ret = undo_qos_inherit(owner, qi, sem, 3);
							[[clang::__noinline__]] trace_qi_percpu_rwsem_uninherit(ret, cur,
								READ_ONCE(qi->static_qos.counter), READ_ONCE(qi->dyn_qos.counter),
								(unsigned long)sem);
						}
					}
					iter_ptrs_end(cur);
					put_task_struct(owner);
				}
			}
		}
	}

	if (!is_qi_feat_enabled(6))
		return;
	own = sem->waiters.head.prev;
	w = list_entry(own, struct wait_queue_entry, entry);
	if (unlikely(w->private != cur))
		return;
	wflags = w->flags;
	pos = sem->waiters.head.next;
	head = &sem->waiters.head;
	if (wflags & WQ_FLAG_CUSTOM) {
		if (pos == head)
			return;
		v = android_task_vendor_data(cur);
		first = true;
		while (pos != own) {
			w = list_entry(pos, struct wait_queue_entry, entry);
			if (w->flags & WQ_FLAG_CUSTOM)
				return;
			task = w->private;
			pos = pos->next;
			[[clang::always_inline]] get_task_struct(task);
			if (first) {
				if (v) {
					v->iter_a = NULL;
					v->iter_b = NULL;
					v->iter_ptr = sem;
					v->iter_depth = 1;
				}
				ret = do_percpu_rwsem_qos_inherit(sem, cur, task);
				if (!ret)
					iter_qos_inherit(task, sem);
				if (v) {
					v->iter_a = NULL;
					v->iter_b = NULL;
					v->iter_ptr = NULL;
					v->iter_depth = 0;
				}
			} else {
				do_percpu_rwsem_qos_inherit(sem, cur, task);
			}
			qi_put_task_struct(task);
			first = false;
			if (pos == head)
				return;
		}
		return;
	}
	if (pos == head)
		return;
	while (pos != own) {
		w = list_entry(pos, struct wait_queue_entry, entry);
		task = w->private;
		next = pos->next;
		[[clang::always_inline]] get_task_struct(task);
		if ((w->flags & WQ_FLAG_CUSTOM) || !is_qos_rt_task(task)) {
			[[clang::always_inline]] __list_del_entry(own);
			qi_list_add_tail(own, pos);
			qi_put_task_struct(task);
			[[clang::always_inline]] trace_qi_percpu_rwsem_jump(cur, (unsigned long)sem);
			return;
		}
		trace_qi_percpu_rwsem_skip(task, !!(w->flags & WQ_FLAG_CUSTOM), is_qos_rt_task(task));
		qi_put_task_struct(task);
		pos = next;
		if (pos == head)
			return;
	}
}

/* private kernel/locking/rwsem.c layout */
struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum { RWSEM_WAITING_FOR_WRITE, RWSEM_WAITING_FOR_READ } type;
	unsigned long timeout;
	bool handoff_set;
};

/*
 * a waiter queues on a rwsem: a qos reader sets the handoff bit; a qos waiter lends its qos to the (writer or
 * recorded reader) owner and to the first waiting writers; a qos writer jumps in front of the first normal waiter
 */
static void android_vh_alter_rwsem_list_add(void *unused, struct rwsem_waiter *waiter,
					    struct rw_semaphore *sem, bool *already_on_list)
{
	struct task_struct *task, *cur, *owner, *reader;
	struct rwsem_waiter *first, *w, *next;
	struct rwsem_rd_info *info;
	struct qi_vendor *v;
	unsigned long flags, val;
	long count;
	u64 vd;
	int ret;

	if (!is_qi_feat_enabled(5))
		return;
	task = waiter->task;
	if (is_qi_feat_enabled(13) && is_qos_task(task) && waiter->type == RWSEM_WAITING_FOR_READ) {
		count = atomic_long_read(&sem->count);
		while (!(count & RWSEM_FLAG_HANDOFF_Q)) {
			if (!atomic_long_try_cmpxchg_acquire(&sem->count, &count, count | RWSEM_FLAG_HANDOFF_Q))
				continue;
			if (list_empty(&sem->wait_list)) {
				waiter->handoff_set = true;
				v = android_task_vendor_data(task);
				if (v)
					v->f0 |= BIT(1);
			} else {
				first = list_first_entry(&sem->wait_list, struct rwsem_waiter, list);
				if (first->type == RWSEM_WAITING_FOR_READ) {
					first->handoff_set = true;
					v = android_task_vendor_data(first->task);
					if (v)
						v->f0 |= BIT(1);
				}
			}
			trace_qos_rwsem_wait(true, task->pid, atomic_long_read(&sem->count));
			break;
		}
	}
	if (IS_ERR_OR_NULL(sem) || IS_ERR_OR_NULL(waiter) || IS_ERR_OR_NULL(already_on_list))
		return;
	if (!is_qos_task(waiter->task)) {
		v = android_task_vendor_data(waiter->task);
		if (v && v->f1_b4) {
			spin_lock_irqsave(&v->lock, flags);
			v->req = sem;
			v->f1_lo = 2;
			spin_unlock_irqrestore(&v->lock, flags);
		}
		return;
	}
	val = atomic_long_read(&sem->owner);
	if (!(val & RWSEM_READER_OWNED)) {
		owner = (struct task_struct *)(val & ~3UL);
		if (owner) {
			cur = current;
			iter_ptrs_start(cur, sem);
			ret = do_rwsem_qos_inherit(sem, waiter->task, owner);
			if (!ret) {
				val = atomic_long_read(&sem->owner);
				if (!(val & RWSEM_READER_OWNED) && owner == (struct task_struct *)(val & ~3UL))
					iter_qos_inherit(owner, sem);
				else
					undo_rwsem_qos_inherit(sem, owner);
			}
			iter_ptrs_end(cur);
		}
	} else {
		vd = sem->android_vendor_data1;
		if (vd == (u64)sem && waiter->type == RWSEM_WAITING_FOR_WRITE) {
			info = (struct rwsem_rd_info *)sem->android_oem_data1[0];
			if (!info) {
				info = kzalloc(sizeof(*info), GFP_ATOMIC);
				if (info) {
					spin_lock_init(&info->lock);
					info->owner = (u64)waiter->task;
					smp_wmb();
					sem->android_oem_data1[0] = (u64)info;
				}
			} else {
				spin_lock_irqsave(&info->lock, flags);
				reader = find_next_reader_owner(info, (u64)waiter->task);
				if (!reader) {
					spin_unlock_irqrestore(&info->lock, flags);
				} else {
					cur = current;
					iter_ptrs_start(cur, (void *)vd);
					ret = do_rwsem_qos_inherit(sem, waiter->task, reader);
					spin_unlock_irqrestore(&info->lock, flags);
					if (!ret) {
						trace_qi_reader_owner_rwsem_inherit(waiter->task, reader, vd);
						iter_qos_inherit(reader, sem);
					}
					iter_ptrs_end(cur);
				}
			}
		}
	}
	if (!is_qi_feat_enabled(4))
		return;
	if (list_empty(&sem->wait_list))
		return;
	first = (struct rwsem_waiter *)sem->wait_list.next;
	if (waiter->type == RWSEM_WAITING_FOR_WRITE) {
		for (w = first; &w->list != &sem->wait_list; w = next) {
			next = (struct rwsem_waiter *)w->list.next;
			get_task_struct(w->task);
			if (w->type == RWSEM_WAITING_FOR_READ || !is_qos_rt_task(w->task)) {
				qi_list_add_tail(&waiter->list, &w->list);
				if (w == first && w->handoff_set) {
					w->handoff_set = false;
					waiter->handoff_set = true;
				}
				qi_put_task_struct(w->task);
				*already_on_list = true;
				[[clang::always_inline]] trace_qi_rwsem_jump(waiter->task, (unsigned long)sem);
				return;
			}
			trace_qi_rwsem_skip(w->task, w->type == RWSEM_WAITING_FOR_READ, is_qos_rt_task(w->task));
			put_task_struct(w->task);
		}
		return;
	}
	if (&first->list == &sem->wait_list || first->type == RWSEM_WAITING_FOR_READ)
		return;
	next = (struct rwsem_waiter *)first->list.next;
	v = android_task_vendor_data(current);
	if (v) {
		v->iter_a = NULL;
		v->iter_b = NULL;
		v->iter_ptr = sem;
		v->iter_depth = 1;
	}
	ret = do_rwsem_qos_inherit(sem, waiter->task, first->task);
	if (!ret)
		iter_qos_inherit(first->task, sem);
	if (v) {
		v->iter_a = NULL;
		v->iter_b = NULL;
		v->iter_ptr = NULL;
		v->iter_depth = 0;
	}
	for (w = next; &w->list != &sem->wait_list; w = next) {
		if (w->type == RWSEM_WAITING_FOR_READ)
			return;
		next = (struct rwsem_waiter *)w->list.next;
		do_rwsem_qos_inherit(sem, waiter->task, w->task);
	}
}

static void android_vh_clear_rwsem_writer_owned(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *cur;

	if (!sem)
		return;
	cur = current;
	if (is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);
}

static noinline void iter_ptrs_start(struct task_struct *p, void *ptr)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return;
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_ptr = ptr;
	v->iter_depth = 1;
}

static noinline void iter_ptrs_end(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (!v)
		return;
	v->iter_a = NULL;
	v->iter_b = NULL;
	v->iter_ptr = NULL;
	v->iter_depth = 0;
}

/* the current task stops holding a read lock: a reader-owner of the mmap_lock inherits the qos of the first recorded reader */
static void android_vh_clear_rwsem_reader_owned(void *unused, struct rw_semaphore *sem)
{
	struct rwsem_rd_info *info;
	struct task_struct *cur, *reader;
	unsigned long flags;
	u64 vd;
	u64 *slot;
	int i, ret;

	if (!sem)
		return;
	vd = sem->android_vendor_data1;
	if (vd != (u64)sem) {
		cur = current;
		if (is_qos_task(cur))
			undo_rwsem_qos_inherit(sem, cur);
		return;
	}
	info = (struct rwsem_rd_info *)sem->android_oem_data1[0];
	if (!info) {
		cur = current;
		if (is_qos_task(cur))
			undo_rwsem_qos_inherit(sem, cur);
		return;
	}
	spin_lock_irqsave(&info->lock, flags);
	cur = current;
	if (is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);
	if (!info->cnt) {
		info->owner = 0;
		goto unlock;
	}
	for (i = 0; i < 10; i++) {
		if (info->slot[i] == (u64)cur) {
			info->slot[i] = 0;
			if (!--info->cnt)
				info->owner = 0;
			break;
		}
	}
	if (!is_qi_feat_enabled(5) || !info->owner)
		goto unlock;
	get_task_struct((struct task_struct *)info->owner);
	if (((struct task_struct *)info->owner)->exit_state ||
	    !(((struct task_struct *)info->owner)->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) ||
	    !is_qos_task((struct task_struct *)info->owner)) {
		for (i = 0; i < 10; i++)
			if (info->slot[i])
				info->slot[i] = 0;
		info->cnt = 0;
		put_task_struct((struct task_struct *)info->owner);
		info->owner = 0;
		goto unlock;
	}
	put_task_struct((struct task_struct *)info->owner);
	for (i = 0; i < 10; i++) {
		slot = &info->slot[i];
		reader = (struct task_struct *)*slot;
		if (reader)
			break;
	}
	if (!reader)
		goto unlock;
	{
		struct task_struct *owner = (struct task_struct *)info->owner;

		get_task_struct(owner);
		iter_ptrs_start(cur, (void *)vd);
		ret = do_rwsem_qos_inherit(sem, owner, reader);
		*slot = 0;
		if (!--info->cnt)
			info->owner = 0;
		spin_unlock_irqrestore(&info->lock, flags);
		if (!ret) {
			trace_qi_reader_owner_rwsem_inherit(owner, reader, vd);
			iter_qos_inherit(reader, sem);
		}
		iter_ptrs_end(cur);
		put_task_struct(owner);
	}
	return;
unlock:
	spin_unlock_irqrestore(&info->lock, flags);
}

static void android_vh_rwsem_direct_rsteal(void *unused, struct rw_semaphore *sem, bool *steal)
{
	if (is_qi_feat_enabled(13) && osq_is_locked(&sem->osq))
		*steal = false;
}

/* rwsem_spin_on_owner() of the reader path: no hooks, owner state computed without RWSEM_NONSPINNABLE */
static inline struct task_struct *rspin_owner_flags(struct rw_semaphore *sem, unsigned long *pflags)
{
	unsigned long owner = atomic_long_read(&sem->owner);

	*pflags = owner & 3UL;
	return (struct task_struct *)(owner & ~3UL);
}

static inline int rspin_owner_state(struct task_struct *owner, unsigned long flags)
{
	if (flags & RWSEM_READER_OWNED)
		return OWNER_READER;
	return owner ? OWNER_WRITER : OWNER_NULL;
}

static noinline int rwsem_rspin_on_owner(struct rw_semaphore *sem)
{
	struct task_struct *owner, *new;
	unsigned long flags, new_flags;
	int state;

	owner = rspin_owner_flags(sem, &flags);
	state = rspin_owner_state(owner, flags);
	if (state != OWNER_WRITER)
		return state;
	for (;;) {
		new = rspin_owner_flags(sem, &new_flags);
		if (new != owner || new_flags != flags) {
			state = rspin_owner_state(new, new_flags);
			break;
		}
		barrier();
		if (need_resched() || !owner_on_cpu(owner)) {
			state = OWNER_NONSPINNABLE;
			break;
		}
		cpu_relax();
	}
	return state;
}

static void android_vh_rwsem_opt_rspin(void *unused, struct rw_semaphore *sem, long *adjustment, bool *rspin)
{
	struct task_struct *cur, *owner;
	unsigned long val;
	bool taken = false;
	u64 start = 0;
	int state;

	if (!is_qi_feat_enabled(13))
		return;
	*rspin = false;
	cur = current;
	if (!is_qos_task(cur) || need_resched())
		return;
	val = atomic_long_read(&sem->owner);
	owner = (struct task_struct *)(val & ~3UL);
	if (owner && !(val & RWSEM_READER_OWNED) && !owner_on_cpu(owner))
		return;
	atomic_long_add(-(long)RWSEM_READER_BIAS, &sem->count);
	if (likely(osq_lock(&sem->osq))) {
		if (trace_qos_rwsem_opt_rspin_enabled())
			start = sched_clock();
		state = rwsem_rspin_on_owner(sem);
		while (state & (OWNER_NULL | OWNER_WRITER | OWNER_READER)) {
			if (!(atomic_long_read(&sem->count) & RWSEM_WRITER_LOCKED)) {
				if (!(atomic_long_fetch_add_acquire(RWSEM_READER_BIAS, &sem->count) & RWSEM_WRITER_LOCKED)) {
					atomic_long_set(&sem->owner, (long)cur | RWSEM_READER_OWNED |
						(atomic_long_read(&sem->owner) & RWSEM_NONSPINNABLE));
					taken = true;
				} else {
					atomic_long_add(-(long)RWSEM_READER_BIAS, &sem->count);
				}
			}
			if (taken)
				break;
			if (state != OWNER_WRITER && need_resched())
				break;
			cpu_relax();
			state = rwsem_rspin_on_owner(sem);
		}
		osq_unlock(&sem->osq);
		trace_qos_rwsem_opt_rspin(cur->pid, start, state, taken);
		if (taken) {
			*adjustment = 0;
			*rspin = true;
			return;
		}
	}
	atomic_long_add(RWSEM_READER_BIAS, &sem->count);
}
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
/*
 * a high-qos reader of its own mmap_lock failed the read trylock because of waiters: it may barge in
 * (retry the trylock) unless one of the first waiting writers has a higher priority / qos, or the first waiter timed out
 */
#define RWSEM_READFAIL_MASK	(RWSEM_WRITER_LOCKED | (1UL << (BITS_PER_LONG - 1)))
static void android_vh_rwsem_read_trylock_failed(void *unused, struct rw_semaphore *sem, long *cntp, int *ret)
{
	struct task_struct *cur, *task;
	struct rwsem_waiter *first, *w;
	long count;
	u32 qos, tq;
	unsigned int cnt;
	int tprio, cprio;

	if (!is_qi_feat_enabled(13))
		return;
	cur = current;
	if (!cur->mm || &cur->mm->mmap_lock != sem)
		return;
	if (get_qos_lvl(cur) < 7)
		return;
	if (atomic_long_read(&sem->count) & RWSEM_READFAIL_MASK)
		return;
	qos = get_qos_lvl(cur);
	raw_spin_lock_irq(&sem->wait_lock);
	if (list_empty(&sem->wait_list)) {
		[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, cur, qos, "wait list empty");
		goto retry;
	}
	first = (struct rwsem_waiter *)sem->wait_list.next;
	if (time_after(jiffies, first->timeout + 4)) {
		task = first->task;
		[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, task, get_qos_lvl(task), "waiting timeout");
		raw_spin_unlock_irq(&sem->wait_lock);
		return;
	}
	cnt = 0;
	for (w = first; &w->list != &sem->wait_list; w = (struct rwsem_waiter *)w->list.next) {
		task = w->task;
		if (!task)
			continue;
		if (++cnt > 3)
			break;
		if (w->type != RWSEM_WAITING_FOR_WRITE)
			continue;
		tprio = task->prio;
		cprio = cur->prio;
		tq = get_qos_lvl(task);
		if (tprio <= cprio) {
			[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, task, tq, "higher prio");
			raw_spin_unlock_irq(&sem->wait_lock);
			return;
		}
		if (tq < qos)
			continue;
		[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, task, get_qos_lvl(task), "higher qos");
		raw_spin_unlock_irq(&sem->wait_lock);
		return;
	}
	[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, cur, qos, "retry lock");
retry:
	raw_spin_unlock_irq(&sem->wait_lock);
	if (!cntp) {
		int ntry;

		/* the stock retries the cmpxchg at most 3 times (fully unrolled, the 3rd failure just leaves) */
		count = atomic_long_read(&sem->count);
		for (ntry = 0; ntry < 3; ntry++) {
			if (count & RWSEM_READFAIL_MASK)
				break;
			if (atomic_long_try_cmpxchg_acquire(&sem->count, &count, count + RWSEM_READER_BIAS)) {
				*ret = 1;
				break;
			}
		}
	} else if (!(*cntp & RWSEM_READFAIL_MASK)) {
		*ret = 1;
	}
	[[clang::always_inline]] trace_qi_rwsem_reader_retry_lock(sem, cur, get_qos_lvl(cur), *ret ? "retry success" : "retry failed");
}
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
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_clear_rwsem_reader_owned(android_vh_clear_rwsem_reader_owned, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_direct_rsteal(android_vh_rwsem_direct_rsteal, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_optimistic_rspin(android_vh_rwsem_opt_rspin, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_write_wait_finish(android_vh_rwsem_write_wait_finish, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_read_wait_finish(android_vh_rwsem_read_wait_finish, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_read_trylock_failed(android_vh_rwsem_read_trylock_failed, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_record_rwsem_lock_starttime(android_vh_record_rwsem_lock_starttime, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mmap_lock_init(android_vh_mmap_lock_init, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_mmap_lock_free(android_vh_mmap_lock_free, NULL);
	if ((err < 0) & !ret)
		ret = err;
	err = register_trace_android_vh_rwsem_init(android_vh_rwsem_init, NULL);
	if ((err < 0) & !ret)
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
#undef pr_fmt
#define pr_fmt(fmt) "qi_stats: " fmt

/*
 * XR_FBT_QOS_EVENT callback (G11b, from the stock disassembly): a gpu driver reports that a task blocks on / is
 * released from the gpu. blocked: the task gets a node in g_gpu_blocked_list and qi->blocked_on_gpu; released: the
 * node is removed; both notify the qievent chain (type 8) and fire a tracepoint.
 */
static int qos_event_handler(void *arg)
{
	struct qi_gpu_event ev = {};
	struct qievent_struct event = {};
	struct gpu_blocked_node *node, *tmp;
	struct task_struct *task;
	struct qi_struct *qi;
	unsigned long left;
	u32 type;
	int ret = 0;

	if (!is_qi_feat_enabled(12))
		return 0;
	if (!arg) {
		pr_err("set qos: invalid arg from user space\n");
		return -EINVAL;
	}
	[[clang::always_inline]] left = copy_from_user(&ev, (void __user *)arg, sizeof(ev));
	if (left) {
		pr_err("handle qos event: failed to copy from user\n");
		return -EFAULT;
	}
	rcu_read_lock();
	task = find_task_by_vpid(ev.pid);
	if (!task) {
		rcu_read_unlock();
		pr_err("handle qos event: find task failed\n");
		return -EINVAL;
	}
	get_task_struct(task);
	rcu_read_unlock();
	if (task->__state == TASK_DEAD) {
		pr_err("handle qos event: task is dead\n");
		ret = -EINVAL;
		goto put;
	}
	if (!is_qos_task(task))
		goto put;
	if (ev.unblocked > 1) {
		ret = -EINVAL;
		goto put;
	}
	qi = get_task_qi(task);
	if (!qi) {
		ret = -EINVAL;
		goto put;
	}
	spin_lock(&qi->lock);
	if (!ev.unblocked) {
		if (qi->blocked_on_gpu) {
			ret = -EEXIST;
			goto unlock;
		}
		type = ev.type;
		qi->blocked_on_gpu = true;
		node = kzalloc(sizeof(*node), GFP_ATOMIC);
		if (node) {
			INIT_LIST_HEAD(&node->list);
			node->pid = task->pid;
			node->type = type;
			node->start = ktime_get();
			spin_lock(&g_stats_lock);
			[[clang::always_inline]] __list_add(&node->list, &g_gpu_blocked_list, g_gpu_blocked_list.next);
			spin_unlock(&g_stats_lock);
			[[clang::always_inline]] trace_qi_gpu_blocked(task);
		}
	} else {
		if (!qi->blocked_on_gpu) {
			ret = -EINVAL;
			goto unlock;
		}
		qi->blocked_on_gpu = false;
		spin_lock(&g_stats_lock);
		list_for_each_entry_safe(node, tmp, &g_gpu_blocked_list, list) {
			if (node->pid == task->pid) {
				[[clang::always_inline]] __list_del_entry(&node->list);
				node->list.next = LIST_POISON1;
				node->list.prev = LIST_POISON2;
				kfree(node);
				break;
			}
		}
		spin_unlock(&g_stats_lock);
		[[clang::always_inline]] trace_qi_gpu_unblocked(task);
	}
	spin_unlock(&qi->lock);
	event.to = task;
	*(u32 *)&event.reserved = ev.unblocked;
	atomic_notifier_call_chain(&g_qievent_notif_chain, 8, &event);
	ret = 0;
	goto put;
unlock:
	spin_unlock(&qi->lock);
put:
	put_task_struct(task);
	return ret;
}

#undef pr_fmt
#define pr_fmt(fmt) "qi: " fmt

int qi_stats_register(void)
{
	return register_xr_fbt(XR_FBT_QOS_EVENT, qos_event_handler);
}

void qi_stats_unregister(void)
{
	unregister_xr_fbt(XR_FBT_QOS_EVENT);
}
