#include "qi_hooks_common.h"
#include "qi_rw_common.h"

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
