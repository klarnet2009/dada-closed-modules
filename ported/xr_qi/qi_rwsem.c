#include "qi_hooks_common.h"
#include "qi_rw_common.h"

static noinline int do_rwsem_qos_inherit(struct rw_semaphore *sem, struct task_struct *from, struct task_struct *to);
static noinline struct task_struct *find_next_reader_owner(struct rwsem_rd_info *info, u64 new_owner);

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

static void android_vh_clear_rwsem_writer_owned(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *cur;

	if (!sem)
		return;
	cur = current;
	if (is_qos_task(cur))
		undo_rwsem_qos_inherit(sem, cur);
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

