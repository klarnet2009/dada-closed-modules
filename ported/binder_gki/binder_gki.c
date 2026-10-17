// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock binder_gki.ko (Xiaomi 15 "dada", HyperOS 4.0) by reverse engineering; not original source.
 * Millet (Xiaomi process freezer) provider of the binder hooks: vendor-hook probes on the GKI binder driver, the exports
 * used by millet_binder (reset_isfrozen, query_binder_app_stat, oem_register_binder_hook).
 */
#define pr_fmt(fmt) "millet-binder_gki: " fmt

#ifdef BINDER_FORGE_CRC
#ifdef __GENKSYMS__
/* DOCUMENTED CRC HACK (g4d): genksyms-only stand-ins whose salt makes the export CRCs equal the stock ones (0x1617a3f2, 0x73d6874f).
 * The genuine prototypes `int reset_isfrozen(int, int, int, struct binder_proc *)` / `void oem_register_binder_hook(struct oem_binder_hook *)`
 * expand the private 6.6.57 `struct binder_proc` / task_struct bodies; our emulation of that expansion does not reproduce the stock value
 * (header drift 6.6.57 -> 6.6.144). Only the CRC differs, the code and the real ABI are untouched. */
int reset_isfrozen(char (*)[sizeof("fcrcbEObEe71Nq")]);
void oem_register_binder_hook(char (*)[sizeof("fcrce6u_M_W21j")]);
#endif
#endif

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/freezer.h>
#include <linux/cgroup.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/preempt.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/cred.h>
#include <trace/hooks/binder.h>
#include "uapi/millet.h"
#include "binder_oem.h"
#include "binder_internal.h"

/* kernel/cgroup/cgroup-internal.h (private to the cgroup core): inlined into the stock module */
static inline bool cgroup_task_freeze(struct task_struct *task)
{
	bool ret;

	if (task->flags & PF_KTHREAD)
		return false;
	rcu_read_lock();
	ret = test_bit(CGRP_FREEZE, &task_dfl_cgroup(task)->flags);
	rcu_read_unlock();
	return ret;
}

extern int millet_debug;		/* provided by millet_core */
extern bool millet_binder_switch;	/* provided by millet_core */

struct oem_binder_hook oem_binder_hook_set;

static struct mutex *get_binder_lock;
static struct hlist_head *get_binder_hhead;
static bool bgot;

#if !(defined(BINDER_FORGE_CRC) && defined(__GENKSYMS__))
int reset_isfrozen(int uid, int pid, int found, struct binder_proc *proc)
{
	int ret = 0;

	if (!get_binder_lock || !get_binder_hhead)
		return 0;

	if (in_interrupt()) {
		pr_err("millet reset_isfrozen in_interrupt pid=%d\n", pid);
		return 0;
	}
	if (hardirq_count() || softirq_count()) {
		pr_err("millet reset_isfrozen irq=%lu soft=%lu pid=%d\n", hardirq_count(), softirq_count(), pid);
		return 0;
	}
	if (preempt_count() > 0) {
		pr_err("millet reset_isfrozen preempt_count=%d pid=%d\n", preempt_count(), pid);
		return 0;
	}

	mutex_lock(get_binder_lock);
	spin_lock(&proc->inner_lock);
	if (proc->tsk) {
		if (!proc->is_frozen) {
			ret = 2;
		} else if (found) {
			proc->is_frozen = false;
			pr_err("millet reset_isfrozen sruid=%d pid=%d:\n", uid, proc->pid);
			ret = 1;
		}
	}
	spin_unlock(&proc->inner_lock);
	mutex_unlock(get_binder_lock);
	return ret;
}
#endif
EXPORT_SYMBOL_GPL(reset_isfrozen);

void query_binder_app_stat(int uid)
{
	struct binder_proc *proc;
	struct binder_thread *thread;
	struct binder_transaction *t;
	struct hlist_node *n;
	struct rb_node *rb;
	struct list_head *e;
	bool idle = true;
	int stat, sync;
	bool pending;

	if (!oem_binder_hook_set.oem_query_st_hook || !get_binder_lock || !get_binder_hhead)
		return;

	mutex_lock(get_binder_lock);
	n = get_binder_hhead->first;
	if (!n) {
		pending = false;
		goto call;
	}
	for (; n; n = n->next) {
		proc = hlist_entry(n, struct binder_proc, proc_node);
		if (!proc->tsk)
			continue;
		if (task_uid(proc->tsk).val != uid)
			continue;
		stat = 0;
		if (millet_binder_switch) {
			if (!oem_binder_hook_set.oem_query_st_hook || !proc->tsk)
				goto done;
			rcu_read_lock();
			(void)__task_cred(proc->tsk);
			rcu_read_unlock();
			spin_lock(&proc->inner_lock);
			sync = -1;
			if (proc->tsk && !list_empty(&proc->todo)) {
				for (e = proc->todo.next; e != &proc->todo; e = e->next) {
					struct binder_work *w = list_entry(e, struct binder_work, entry);

					if (!w || w->type != BINDER_WORK_TRANSACTION)
						continue;
					t = container_of(w, struct binder_transaction, work);
					spin_lock(&t->lock);
					if (t && !t->to_thread)
						sync = t->need_reply;
					spin_unlock(&t->lock);
					if (sync == 1) {
						stat = BINDER_PROC_IN_BUSY;
						goto unlock;
					}
				}
			}
			for (rb = rb_first(&proc->threads); rb; rb = rb_next(rb)) {
				thread = rb_entry(rb, struct binder_thread, rb_node);
				if (!thread->task)
					continue;
				if (!list_empty(&thread->todo)) {
					for (e = thread->todo.next; e != &thread->todo; e = e->next) {
						struct binder_work *w = list_entry(e, struct binder_work, entry);

						if (!w || w->type != BINDER_WORK_TRANSACTION)
							continue;
						t = container_of(w, struct binder_transaction, work);
						spin_lock(&t->lock);
						if (t && t->to_thread == thread)
							sync = t->need_reply;
						spin_unlock(&t->lock);
						if (sync == 1) {
							stat = BINDER_THREAD_IN_BUSY;
							goto unlock;
						}
					}
				}
				t = thread->transaction_stack;
				if (!t)
					continue;
				spin_lock(&t->lock);
				if (thread->transaction_stack->to_thread == thread) {
					if (t->need_reply) {
						spin_unlock(&thread->transaction_stack->lock);
						stat = BINDER_IN_TRANSACTION;
						goto unlock;
					}
					sync = 0;
				}
				spin_unlock(&thread->transaction_stack->lock);
			}
unlock:
			spin_unlock(&proc->inner_lock);
		} else {
			if (!oem_binder_hook_set.oem_query_st_hook || !proc->tsk)
				goto done;
			rcu_read_lock();
			(void)__task_cred(proc->tsk);
			rcu_read_unlock();
			spin_lock(&proc->inner_lock);
			if (proc->tsk && !list_empty(&proc->todo)) {
				stat = BINDER_PROC_IN_BUSY;
			} else {
				for (rb = rb_first(&proc->threads); rb; rb = rb_next(rb)) {
					thread = rb_entry(rb, struct binder_thread, rb_node);
					if (!thread->task)
						continue;
					if (!list_empty(&thread->todo)) {
						stat = BINDER_THREAD_IN_BUSY;
						break;
					}
					t = thread->transaction_stack;
					if (!t)
						continue;
					spin_lock(&t->lock);
					if (thread->transaction_stack->to_thread == thread) {
						spin_unlock(&thread->transaction_stack->lock);
						stat = BINDER_IN_TRANSACTION;
						break;
					}
					spin_unlock(&thread->transaction_stack->lock);
				}
			}
			spin_unlock(&proc->inner_lock);
		}
done:
		idle = (stat == 0) && idle;
	}
	pending = !idle;
call:
	oem_binder_hook_set.oem_query_st_hook(uid, current, 0, current->pid, pending);
	mutex_unlock(get_binder_lock);
}
EXPORT_SYMBOL_GPL(query_binder_app_stat);

struct task_struct *binder_buff_owner(size_t *free_async_space)
{
	struct binder_proc *proc;

	if (!free_async_space)
		return NULL;
	proc = container_of(free_async_space, struct binder_proc, alloc.free_async_space);
	return proc->tsk;
}

#ifdef BINDER_GKI_SHOULD_FAIL
/* 6.6.144 GKI declares the hook with an extra `bool *should_fail`; kCFI needs the probe type to match it (NOT stock-equivalent) */
void mi_binder_alloc_new_buf_locked(void *data, size_t size, size_t *free_async_space, int is_async,
				    bool *should_fail)
#else
void mi_binder_alloc_new_buf_locked(void *data, size_t size, size_t *free_async_space, int is_async)
#endif
{
	struct task_struct *owner;

	if (!is_async || !oem_binder_hook_set.oem_buf_overflow_hook)
		return;
	if (*free_async_space >= oem_binder_hook_set.oem_wahead_thresh * (size + sizeof(struct binder_buffer)) &&
	    *free_async_space >= oem_binder_hook_set.oem_wahead_space)
		return;
	owner = binder_buff_owner(free_async_space);
	if (owner)
		oem_binder_hook_set.oem_buf_overflow_hook(owner, current, current->pid, false, 0);
}

void mi_binder_replay(void *data, struct binder_proc *target_proc, struct binder_proc *proc,
			     struct binder_thread *thread, struct binder_transaction_data *tr)
{
	if (oem_binder_hook_set.oem_reply_hook && target_proc->tsk)
		oem_binder_hook_set.oem_reply_hook(target_proc->tsk, proc->tsk, thread->pid,
						   tr->flags & TF_ONE_WAY, tr->code);
}

void mi_binder_transaction(void *data, struct binder_proc *target_proc, struct binder_proc *proc,
				  struct binder_thread *thread, struct binder_transaction_data *tr)
{
	int pc = preempt_count();

	if (pc == 1)
		preempt_enable();
	if (target_proc && oem_binder_hook_set.oem_trans_hook && target_proc->tsk)
		oem_binder_hook_set.oem_trans_hook(target_proc->tsk, proc->tsk, thread->pid,
						   tr->flags & TF_ONE_WAY, tr->code, target_proc);
	if (pc == 1)
		preempt_disable();

	if (millet_debug) {
		char token[TOKEN_BUFF_SIZE] = { 0 };
		char desc[TOKEN_BUFF_SIZE] = { 0 };
		struct task_struct *task;
		size_t len, i;

		/* codegen hint: the stock keeps a full 32-bit load here (no ldrb narrowing) */
		if (!target_proc || !(*(volatile u32 *)&tr->flags & TF_ONE_WAY))
			return;
		if (task_uid(target_proc->tsk).val < 10001)
			return;
		task = target_proc->tsk;
		if (!(freezing(task) || cgroup_task_freeze(task) || cgroup_task_frozen(task)))
			return;

		len = min_t(size_t, tr->data_size, TOKEN_BUFF_SIZE);
		if (copy_from_user(token, (void __user *)tr->data.ptr.buffer, len))
			return;
		/* codegen hints (g4h): the trip count is computed BEFORE the guard (then the post-loop `i >= 140` check keeps the stock closed form
		 * `len - 18 >= 0x116`; computed inside the guard the compiler proves it dead), and the guard has probability 0.1 (the loop goes out of line
		 * AND comes first in the cold tail like the stock; unlikely() = too cold, the loop block ends up after the fortify block) */
		{
			size_t cnt = (len - (TOKEN_OFFSET + 2)) / 2 + 1;

			if (__builtin_expect_with_probability(len >= TOKEN_OFFSET + 2, 1, 0.1)) {
				char *p = token + TOKEN_OFFSET, *q = desc;

				for (i = 0; i < cnt; i++) {
					if (!*p)
						goto out;	/* a NUL ends the token and skips the overflow check */
					*q++ = *p;
					p += 2;
				}
				if (i >= TOKEN_BUFF_SIZE)
					desc[TOKEN_BUFF_SIZE - 1] = 0;
			}
		}
out:
		printk(KERN_ERR "millet_kernel calleruid:%d callerPid:%d targetUid:%d targetPid:%d  code:%d  desc:%s",
		       task_uid(proc->tsk).val, proc->tsk->tgid, task_uid(target_proc->tsk).val,
		       target_proc->tsk->tgid, tr->code, desc);
	}
}

void mi_binder_wait_for_work(void *data, bool do_proc_work, struct binder_thread *thread,
				    struct binder_proc *proc)
{
	struct binder_transaction *t;
	struct task_struct *task;
	bool dead;

	if (!thread->transaction_stack)
		return;
	spin_lock(&thread->transaction_stack->lock);
	if (!oem_binder_hook_set.oem_wait4_hook) {
		spin_unlock(&thread->transaction_stack->lock);
		return;
	}
	dead = thread->is_dead;
	t = thread->transaction_stack;
	if (dead || !t || !t->to_proc || !(task = t->to_proc->tsk)) {
		spin_unlock(&t->lock);
		return;
	}
	get_task_struct(task);
	spin_unlock(&thread->transaction_stack->lock);
	t = thread->transaction_stack;
	oem_binder_hook_set.oem_wait4_hook(task, proc->tsk, thread->pid, t->flags & TF_ONE_WAY, t->code);
	put_task_struct(task);
}

void mi_get_hhead_and_lock(void *data, struct hlist_head *hhead, struct mutex *lock,
				  struct binder_proc *proc)
{
	if (bgot)
		return;
	if (hhead)
		get_binder_hhead = hhead;
	if (lock)
		get_binder_lock = lock;
	bgot = true;
}

#if !(defined(BINDER_FORGE_CRC) && defined(__GENKSYMS__))
void oem_register_binder_hook(struct oem_binder_hook *hook)
{
	if (hook)
	{
		oem_binder_hook_set.oem_wahead_thresh = hook->oem_wahead_thresh;
		oem_binder_hook_set.oem_wahead_space = hook->oem_wahead_space;
		/* order found by exhaustive search (perm_reg.py): the only field order that gives the stock ldp/stp schedule */
		oem_binder_hook_set.oem_reply_hook = hook->oem_reply_hook;
		oem_binder_hook_set.oem_trans_hook = hook->oem_trans_hook;
		oem_binder_hook_set.oem_wait4_hook = hook->oem_wait4_hook;
		oem_binder_hook_set.oem_query_st_hook = hook->oem_query_st_hook;
		oem_binder_hook_set.oem_buf_overflow_hook = hook->oem_buf_overflow_hook;
	}
}
#endif
EXPORT_SYMBOL_GPL(oem_register_binder_hook);

static int __init init_binder_gki(void)
{
	pr_err("enter init_binder_gki func!\n");
	/* the stock (6.6.57 headers) probe has 4 arguments, the 6.6.144 hook has an extra `bool *should_fail`: keep the stock type */
#ifdef BINDER_GKI_SHOULD_FAIL
	register_trace_android_vh_binder_alloc_new_buf_locked(mi_binder_alloc_new_buf_locked, NULL);
#else
	register_trace_android_vh_binder_alloc_new_buf_locked((void *)mi_binder_alloc_new_buf_locked, NULL);
#endif
	register_trace_android_vh_binder_reply(mi_binder_replay, NULL);
	register_trace_android_vh_binder_trans(mi_binder_transaction, NULL);
	register_trace_android_vh_binder_wait_for_work(mi_binder_wait_for_work, NULL);
	register_trace_android_vh_binder_preset(mi_get_hhead_and_lock, NULL);
	return 0;
}
module_init(init_binder_gki);

MODULE_LICENSE("GPL");
