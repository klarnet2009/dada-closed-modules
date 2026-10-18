#ifndef QI_HOOKS_COMMON_H
#define QI_HOOKS_COMMON_H
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


#endif
