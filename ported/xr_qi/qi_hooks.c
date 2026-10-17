// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS inherit driver (OS4): vendor-hook handlers and the per-feature (un)registration
 * functions. Reconstructed from the stock binary (round 5, ).
 * PLACEHOLDERS (compile-only bodies, marked STUB): the handlers and helpers whose logic is not
 * recovered yet; they only exist so that the register/unregister functions are real.
 */

#define pr_fmt(fmt) "qi: " fmt

#include <linux/futex.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fuse.h>
#include <trace/hooks/rcu.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"

#define XR_FBT_QOS_EVENT	0x400c780aU

static struct {
	u8 pad[88];
	u32 enable;
	u32 pad2;
} g_rcu_info;

/* private kernel/locking/mutex.h layout */
struct mutex_waiter {
	struct list_head list;
	struct task_struct *task;
	struct ww_acquire_ctx *ww_ctx;
};

/* ---- STUB helpers (logic not recovered) ---- */
static noinline int __qi_set_task_qos(struct task_struct *task, u32 qos, u32 qos_lvl)
{
	barrier_data(task);
	return qos + qos_lvl;
}
static noinline void do_mutex_jump(struct mutex *lock, struct mutex_waiter *waiter, bool *already_on_list)
{
	barrier_data(lock);
	barrier_data(waiter);
	barrier_data(already_on_list);
}
static noinline void undo_mutex_qos_inherit(struct mutex *lock) { barrier_data(lock); }
static noinline void undo_rwsem_qos_inherit(struct rw_semaphore *sem, struct task_struct *p) { barrier_data(sem); barrier_data(p); }
static noinline int qos_event_handler(void *arg) { return 0; }

int kern_qi_set_qos(struct task_struct *task, u32 qos GKPAD(fcrcM8qDHkAXHp))
{
	u32 qos_lvl = qos & 0xf;
	int ret;

	if (!task) {
		pr_err("qi_set_task_qos: FAILED - task is NULL\n");
		return -EINVAL;
	}
	if (qos_lvl >= 11) {
		pr_err("qi_set_task_qos: FAILED - task=%s[%d] invalid qos_lvl=%u\n", task->comm, task->pid, qos_lvl);
		return -EINVAL;
	}
	get_task_struct(task);
	ret = __qi_set_task_qos(task, qos, qos_lvl);
	put_task_struct(task);
	return ret;
}
EXPORT_SYMBOL_GPL(kern_qi_set_qos);

/* ---- binder ---- */
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
					     bool has_transaction) { /* STUB */ }

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
static void android_vh_fuse_request_fetch(void *unused, struct fuse_req *req, struct task_struct *self) { /* STUB */ }
static void android_vh_fuse_request_end(void *unused, struct fuse_req *req, struct task_struct *self) { /* STUB */ }

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
static void android_vh_do_futex(void *unused, int cmd, unsigned int *flags, u32 __user *uaddr2) { /* STUB */ }
static void android_vh_futex_wait_queue_start(void *unused, u32 __user *uaddr, unsigned int flags, u32 bitset) { /* STUB */ }
static void android_vh_futex_wait_end(void *unused, unsigned int flags, u32 bitset) { /* STUB */ }
static void android_vh_futex_wake_traverse_plist(void *unused, struct plist_head *chain, int *target_nr,
						 union futex_key key, u32 bitset) { /* STUB */ }
static void android_vh_futex_wake_up_q(void *unused, int nr_wake, int target_nr) { /* STUB */ }

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

static void android_vh_mutex_wait_start(void *unused, struct mutex *lock) { /* STUB */ }
static void android_vh_mutex_wait_finish(void *unused, struct mutex *lock) { /* STUB */ }
static void android_vh_mutex_opt_spin_start(void *unused, struct mutex *lock, bool *time_out, int *cnt) { /* STUB */ }

static void android_vh_mutex_opt_spin_finish(void *unused, struct mutex *lock, bool taken)
{
	struct qi_struct *qi;

	if (!is_qi_feat_enabled(13))
		return;
	qi = get_task_qi(current);
	if (qi && qi->opt_spin_start_time)
		qi->opt_spin_start_time = 0;
}

static void android_vh_mutex_can_spin_on_owner(void *unused, struct mutex *lock, int *retval) { /* STUB */ }

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
static void android_vh_percpu_rwsem_init(void *unused, struct percpu_rw_semaphore *sem)
{
	sem->android_oem_data1 = 0;
}

static void android_vh_record_pcpu_rwsem_starttime(void *unused, struct percpu_rw_semaphore *sem,
						   unsigned long settime_jiffies) { /* STUB */ }
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
static void android_vh_sync_rcu_wait_start(void *unused, struct task_struct *self) { /* STUB */ }
static void android_vh_sync_rcu_wait_end(void *unused, struct task_struct *self) { /* STUB */ }
static void android_vh_rcu_boost_start(void *unused, struct rt_mutex *mtx, struct task_struct *t) { /* STUB */ }
static void android_vh_rcu_boost_end(void *unused, struct rt_mutex *mtx, struct task_struct *t) { /* STUB */ }

int qi_vh_rcu_register(void)
{
	int ret = 0, err;

	g_rcu_info.enable = 0;
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
static void android_vh_rwsem_write_wait_finish(void *unused, struct rw_semaphore *sem) { /* STUB */ }
static void android_vh_rwsem_read_wait_finish(void *unused, struct rw_semaphore *sem) { /* STUB */ }
static void android_vh_rwsem_read_trylock_failed(void *unused, struct rw_semaphore *sem, long *cntp, int *ret) { /* STUB */ }
static void android_vh_record_rwsem_lock_starttime(void *unused, struct rw_semaphore *sem,
						   unsigned long settime_jiffies) { /* STUB */ }

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

/* ---- task lifetime (comm) ---- */
static void android_vh_dup_task_struct(void *unused, struct task_struct *tsk, struct task_struct *orig)
{
	struct qi_vendor *v = android_task_vendor_data(tsk);

	if (!v)
		return;
	v->f34 = 0;
	v->f1 = 0x17;
	v->f4 = -1;
}

static void android_vh_task_exit(void *unused, struct task_struct *p) { /* STUB */ }
static void android_vh_free_task(void *unused, struct task_struct *p) { /* STUB */ }

int qi_vh_comm_register(void)
{
	int ret = 0, err;

	err = register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct, NULL);
	if (err < 0)
		ret = err;
	err = register_trace_android_vh_exit_check(android_vh_task_exit, NULL);
	if (!ret && err < 0)
		ret = err;
	err = register_trace_android_vh_free_task(android_vh_free_task, NULL);
	if (!ret && err < 0)
		ret = err;
	if (ret < 0)
		printk(KERN_ERR "qi_utils: register hooks failed, ret %d\n", ret);
	return ret;
}

void qi_vh_comm_unregister(void)
{
	unregister_trace_android_vh_free_task(android_vh_free_task, NULL);
	unregister_trace_android_vh_exit_check(android_vh_task_exit, NULL);
	unregister_trace_android_vh_dup_task_struct(android_vh_dup_task_struct, NULL);
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
