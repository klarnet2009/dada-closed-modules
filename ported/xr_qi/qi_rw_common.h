#ifndef QI_RW_COMMON_H
#define QI_RW_COMMON_H
#include "qi_hooks_common.h"

/* per-mmap_lock reader bookkeeping hung off sem->android_oem_data1[0] */
struct rwsem_rd_info {
	spinlock_t lock;
	int cnt;
	u64 owner;
	u64 slot[10];
};


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

#endif
