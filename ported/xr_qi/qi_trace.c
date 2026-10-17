// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS inherit driver (OS4): the translation unit that defines the tracepoints (CREATE_TRACE_POINTS)
 * and the per-task qos state accessors. Separate file because the stock calls __traceiter_* out of line
 * from the ioctl/getter file (ThinLTO import limit), reconstructed from the call structure.
 */

#define pr_fmt(fmt) "qi: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/sched.h>

#include "xr_qi.h"

#define CREATE_TRACE_POINTS
#include "trace_xr_qi.h"

static DEFINE_SPINLOCK(g_qi_lock);

struct qi_struct *get_task_qi(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);

	if (unlikely(!v))
		return NULL;
	return smp_load_acquire(&v->qi);
}

struct qi_struct *get_alloc_task_qi(struct task_struct *p)
{
	struct qi_vendor *v = android_task_vendor_data(p);
	struct qi_struct *qi;
	unsigned long flags;
	int i;

	if (v) {
		qi = smp_load_acquire(&v->qi);
		if (likely(qi))
			return qi;
	}
	spin_lock_irqsave(&g_qi_lock, flags);
	if (v) {
		qi = smp_load_acquire(&v->qi);
		if (qi)
			goto out;
	}
	qi = kzalloc(sizeof(*qi), GFP_ATOMIC);
	if (!qi)
		goto out;
	spin_lock_init(&qi->lock);
	for (i = 0; i < QOS_TYPE_NUM; i++)
		INIT_LIST_HEAD(&qi->inherit[i].list);
	qi->static_prio = 120;
	qi->dyn_prio = 120;
	smp_wmb();
	if (v)
		v->qi = qi;
out:
	spin_unlock_irqrestore(&g_qi_lock, flags);
	return qi;
}


static noinline void update_dyn_vip_prio(struct task_struct *p)
{
	struct qi_struct *qi = get_task_qi(p);
	struct inherit_node *node;
	struct list_head *head;
	int prio = 0, i;

	if (!qi)
		return;
	for (i = 0; i < QOS_TYPE_NUM; i++) {
		head = &qi->inherit[i].list;
		if (list_empty(head))
			continue;
		list_for_each_entry(node, head, list) {
			if ((node->qos & 0x30) == 0x20 && node->prio > prio)
				prio = node->prio;
		}
	}
	qi->dyn_vip_prio = prio;
}

int call_qievent_notifiers(unsigned long action, void *data);

/* ---- task lifetime (comm) ---- */
static void android_vh_dup_task_struct(void *unused, struct task_struct *tsk, struct task_struct *orig)
{
	struct qi_vendor *v = android_task_vendor_data(tsk);

	if (!v)
		return;
	spin_lock_init(&v->lock);
	v->f1_lo = 7;
	v->f1_b4 = 1;
	v->f4 = -1;
}

static void android_vh_task_exit(void *unused, struct task_struct *p)
{
	struct qievent_struct ev = {};
	struct qi_struct *qi;
	unsigned long flags;

	if (!p)
		return;
	qi = get_task_qi(p);
	if (!qi)
		return;
	spin_lock_irqsave(&qi->lock, flags);
	ev.to = p;
	call_qievent_notifiers(QIEVENT_TASK_EXIT, &ev);
	trace_qi_task_exit(p);
	spin_unlock_irqrestore(&qi->lock, flags);
}
static void android_vh_free_task(void *unused, struct task_struct *p)
{
	struct qi_vendor *v;
	struct qi_struct *qi;
	struct inherit_node *node, *tmp;
	unsigned long flags;
	int i;

	if (!p)
		return;
	v = android_task_vendor_data(p);
	qi = v ? smp_load_acquire(&v->qi) : NULL;
	if (qi) {
		if (qi->timeout && time_after(jiffies, qi->timeout))
			printk(KERN_ERR "qi_utils: %s pid:%d ends with QI timeout: flags:0x%x, timeout:[%u]ms, static_qos:%d, dyn_qos:%d\n",
			       p->comm, p->pid, atomic_read(&qi->flag), jiffies_to_msecs(jiffies - qi->timeout),
			       atomic_read(&qi->static_qos), atomic_read(&qi->dyn_qos));
		spin_lock_irqsave(&qi->lock, flags);
		for (i = 0; i < QOS_TYPE_NUM; i++) {
			list_for_each_entry_safe(node, tmp, &qi->inherit[i].list, list) {
				list_del(&node->list);
				kfree(node);
			}
		}
		v->qi = NULL;
		spin_unlock_irqrestore(&qi->lock, flags);
		kfree(qi);
		trace_qi_free_task(p);
	}
	plat_free_mi_task_struct(p);
}

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



/* QoS word of a task: bits 0-3 level, bit 4 priority boost, bits 4-5 == 0x20 vip boost */
static inline u32 qos_merge(u32 a, u32 b)
{
	return (b & 0xf) <= (a & 0xf) ? (a | (b & 0xf0)) : ((a & 0xf0) | b);
}

int do_qos_inherit(struct task_struct *from, struct task_struct *to, unsigned long ptr, u8 type)
{
	struct qi_struct *from_qi = get_task_qi(from);
	struct qievent_struct event = {};
	struct qi_struct *to_qi;
	struct inherit_node *node;
	struct list_head *head;
	unsigned long flags;
	u32 qos, old_qos, dyn;
	int prio, old_prio, ret;

	if (unlikely(from == to))
		return -EINVAL;
	if (unlikely(from->__state == TASK_DEAD || !from_qi))
		return -EINVAL;
	spin_lock_irqsave(&from_qi->lock, flags);
	qos = get_task_qos(from);
	if ((qos & 0x30) == 0x20)
		prio = get_vip_prio(from);
	else
		prio = get_task_prio(from);
	spin_unlock_irqrestore(&from_qi->lock, flags);

	to_qi = get_task_qi(to);
	if (!to_qi)
		return -EINVAL;
	spin_lock_irqsave(&to_qi->lock, flags);
	head = &to_qi->inherit[type].list;
	list_for_each_entry(node, head, list) {
		if (node->owner == (void *)ptr)
			goto found;
	}
	node = kzalloc(sizeof(*node), GFP_ATOMIC);
	if (!node) {
		ret = -ENOMEM;
		goto out;
	}
	INIT_LIST_HEAD(&node->list);
	node->owner = (void *)ptr;
	node->prio = prio;
	node->qos = qos;
	list_add(&node->list, head);
	if (qos & 0x10) {
		if (to_qi->dyn_prio > prio)
			to_qi->dyn_prio = prio;
	} else {
		if (to_qi->dyn_vip_prio < prio)
			to_qi->dyn_vip_prio = prio;
	}
	dyn = atomic_read(&to_qi->dyn_qos);
	atomic_set(&to_qi->dyn_qos, get_dyn_qos_lvl(to) >= (qos & 0xf) ? (dyn | (qos & 0xf0)) : ((dyn & 0xf0) | qos));
	if (to_qi->timeout == 0)
		to_qi->timeout = jiffies + 500;
	smp_wmb();
	if (!(atomic_read(&to_qi->flag) & (1U << type)))
		atomic_or(1U << type, &to_qi->flag);
	goto notify;

found:
	old_qos = node->qos;
	if (qos == old_qos && prio == node->prio) {
		ret = -EEXIST;
		goto out;
	}
	node->qos = qos_merge(old_qos, qos);
	dyn = atomic_read(&to_qi->dyn_qos);
	atomic_set(&to_qi->dyn_qos, get_dyn_qos_lvl(to) < (node->qos & 0xf) ? (node->qos | (dyn & 0xf0)) :
		   ((node->qos & 0xf0) | dyn));
	if ((qos & 0x30) == 0x20 && (old_qos & 0x30) == 0x20) {
		if (node->prio < prio) {
			node->prio = prio;
			if (to_qi->dyn_vip_prio < prio)
				to_qi->dyn_vip_prio = prio;
		}
	} else if ((qos & 0x10) && (old_qos & 0x30) == 0x20) {
		old_prio = node->prio;
		node->prio = prio;
		if (to_qi->dyn_prio > prio)
			to_qi->dyn_prio = prio;
		if (old_prio == to_qi->dyn_vip_prio)
			update_dyn_vip_prio(to);
	} else if (old_qos & (qos & 0x10)) {
		if (node->prio > prio) {
			node->prio = prio;
			if (to_qi->dyn_prio > prio)
				to_qi->dyn_prio = prio;
		}
	}
notify:
	event.from = from;
	event.to = to;
	event.from_qos.qos = qos;
	event.to_qos.qos = get_task_qos(to);
	event.prio = get_task_prio(to);
	event.vip_prio = to_qi->dyn_vip_prio > to_qi->static_vip_prio ? to_qi->dyn_vip_prio : to_qi->static_vip_prio;
	call_qievent_notifiers(QIEVENT_QOS_INHERIT, &event);
	ret = 0;
out:
	spin_unlock_irqrestore(&to_qi->lock, flags);
	return ret;
}

int undo_qos_inherit(struct task_struct *p, struct qi_struct *qi, unsigned long ptr, u8 type)
{
	struct qievent_struct event = {};
	struct inherit_node *node, *tmp, *n;
	struct list_head *head;
	struct qi_struct *q;
	unsigned long flags;
	u32 qos, fl = 0, merged = 0;
	int max_vip = 0, min_prio = 120;
	bool any;
	int i, ret;

	spin_lock_irqsave(&qi->lock, flags);
	head = &qi->inherit[type].list;
	any = !type && !ptr;
	list_for_each_entry_safe(node, tmp, head, list) {
		if (any || node->owner == (void *)ptr)
			goto found;
	}
	ret = -1;
	goto out;

found:
	qos = node->qos;
	list_del(&node->list);
	kfree(node);
	if (list_empty(head)) {
		atomic_andnot(1U << type, &qi->flag);
		smp_wmb();
		if (!atomic_read(&qi->flag))
			qi->timeout = 0;
	}
	q = get_task_qi(p);
	if (q) {
		for (i = 0; i < QOS_TYPE_NUM; i++) {
			head = &q->inherit[i].list;
			if (list_empty(head))
				continue;
			list_for_each_entry(n, head, list) {
				if ((n->qos & 0xf) > (merged & 0xf))
					merged = n->qos;
				if ((n->qos & 0x30) == 0x20 && n->prio > max_vip) {
					max_vip = n->prio;
					fl |= 2;
				}
				if ((n->qos & 0x10) && n->prio < min_prio) {
					min_prio = n->prio;
					fl |= 1;
				}
			}
		}
		q->dyn_prio = min_prio;
		q->dyn_vip_prio = max_vip;
		atomic_set(&q->dyn_qos, merged | (fl << 4));
	}
	event.to = p;
	event.from_qos.qos = qos;
	event.to_qos.qos = get_task_qos(p);
	event.prio = get_task_prio(p);
	event.vip_prio = qi->dyn_vip_prio > qi->static_vip_prio ? qi->dyn_vip_prio : qi->static_vip_prio;
	call_qievent_notifiers(QIEVENT_QOS_UNINHERIT, &event);
	ret = 0;
out:
	spin_unlock_irqrestore(&qi->lock, flags);
	return ret;
}

void iter_binder_qos_inherit(struct task_struct *p, void *req);
void iter_mutex_qos_inherit(struct task_struct *p, void *req);
void iter_rwsem_qos_inherit(struct task_struct *p, void *req);
void iter_percpu_rwsem_qos_inherit(struct task_struct *p, void *req);
void iter_futex_qos_inherit(struct task_struct *p, void *req);
void iter_fuse_qos_inherit(struct task_struct *p, void *req);

void iter_qos_inherit(struct task_struct *p, void *ptr)
{
	struct qi_vendor *v, *cv;
	unsigned long flags;
	void *req;
	u8 f1;
	u32 type, st;

	if (!is_qi_feat_enabled(11))
		return;
	if (p->exit_state)
		return;
	get_task_struct(p);
	v = android_task_vendor_data(p);
	if (!v || !v->f1_b4) {
		put_task_struct(p);
		return;
	}
	if (p->__state & (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)) {
		spin_lock_irqsave(&v->lock, flags);
		type = v->f1_lo;
		req = v->req;
		spin_unlock_irqrestore(&v->lock, flags);
		if (req && type < QOS_TYPE_NUM) {
			cv = android_task_vendor_data(current);
			if (cv) {
				st = cv->iter_depth;
				if (st) {
					if (st == 3)
						goto done;
					if (cv->iter[0] == req)
						goto done;
					if (st != 1 && cv->iter[1] == req)
						goto done;
				}
			}
			if (type == QOS_BINDER)
				iter_binder_qos_inherit(p, req);
			else if (type == QOS_FUTEX)
				iter_futex_qos_inherit(p, req);
			else if (type == QOS_MUTEX)
				iter_mutex_qos_inherit(p, req);
			else if (type == QOS_RWSEM)
				iter_rwsem_qos_inherit(p, req);
			else if (type == QOS_PERCPU_RWSEM)
				iter_percpu_rwsem_qos_inherit(p, req);
			else if (type == QOS_FUSE)
				iter_fuse_qos_inherit(p, req);
		}
	}
done:
	if (v->f1_b4) {
		spin_lock_irqsave(&v->lock, flags);
		v->req = NULL;
		v->f1_lo = 7;
		spin_unlock_irqrestore(&v->lock, flags);
	}
	put_task_struct(p);
}
