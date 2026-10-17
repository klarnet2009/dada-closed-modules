// SPDX-License-Identifier: GPL-2.0
/*
 * dump_binder: Xiaomi binder async buffer monitor.
 * Reconstructed from the HyperOS 4.0 stock module (no public source).
 *
 * Hooks android_vh_binder_proc_transaction_finish; when system_server's async
 * (oneway) buffer space runs low it snapshots the allocated async buffers of the
 * offending sender and exposes the result through /proc/mi_bindermeminfo
 * (poll() wakes up when a new snapshot is ready).
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/ktime.h>
#include <linux/rtc.h>
#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <trace/hooks/binder.h>

#include "binder_internal.h"

#define MAX_BUFFER_COUNT	512
#define MASK_SPAM 1	/* large async allocation of one sender */
#define MASK_LOW 2	/* free_async_space is low */
#define MASK_EVENT 4	/* new snapshot not yet read */

struct binder_buf_node {
	size_t size;
	int pid;
	struct list_head list;
	u64 ptr;
	u64 cookie;
	u32 code;
	ktime_t start_time;
};

struct binder_info_s {
	int pid;
	char process_name[16];
	int oneway_spam_suspect_pid;
	struct timespec64 ts;
	size_t buffer_size;
	size_t free_async_space;
	size_t total_alloc_size;
	int valid_buffer_size;
	struct list_head list;
};

static int is_monitoring;
static spinlock_t binder_info_lock;
static atomic_t low_buffer_status_mask;
static struct binder_info_s binder_info;
static wait_queue_head_t epoll_event_wait;

static size_t binder_alloc_buffer_size(struct binder_alloc *alloc,
				       struct binder_buffer *buffer)
{
	if (list_is_last(&buffer->entry, &alloc->buffers))
		return alloc->buffer + alloc->buffer_size - buffer->user_data;
	return list_next_entry(buffer, entry)->user_data - buffer->user_data;
}

/* codegen hint: the stock inlined list_add_tail() here (callsite is 'cold' by static heuristics in our CFG) */
static __always_inline void list_add_tail_inl(struct list_head *new, struct list_head *head)
{
	struct list_head *prev = head->prev;

	if (!__list_add_valid(new, prev, head))
		return;
	head->prev = new;
	new->next = head;
	new->prev = prev;
	WRITE_ONCE(prev->next, new);
}

static void clear_binder_info_locked(void)
{
	struct list_head *pos, *tmp;
	struct binder_buf_node *node;

	list_for_each_safe(pos, tmp, &binder_info.list) {
		node = list_entry(pos, struct binder_buf_node, list);
		if (node) {
			list_del(&node->list);
			kfree(node);
		}
	}
	memset(&binder_info, 0, sizeof(binder_info));
	INIT_LIST_HEAD(&binder_info.list);
}

static void extend_android_vh_binder_proc_transaction_finish(void *data,
		struct binder_proc *proc, struct binder_transaction *t,
		struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	struct binder_alloc *alloc;
	struct rb_node *n;
	size_t total = 0;
	bool large = false;
	int status;
	int count = 0;
	size_t len;

	if (!proc || sync || !proc->tsk || !t || !t->buffer)
		return;
	{ struct timespec64 ts = {};
	if (unlikely(!strcmp("system_server", proc->tsk->comm))) {

	alloc = &proc->alloc;
	if (alloc->free_async_space > alloc->buffer_size / 10) {
		if (!spin_trylock(&binder_info_lock))
			return;
		if (atomic_fetch_and(~(MASK_SPAM | MASK_LOW), &low_buffer_status_mask) &
		    (MASK_SPAM | MASK_LOW))
			clear_binder_info_locked();
		else {
			spin_unlock(&binder_info_lock);
			return;
		}
		spin_unlock(&binder_info_lock);
		atomic_or(MASK_EVENT, &low_buffer_status_mask);
		wake_up_interruptible(&epoll_event_wait);
		return;
	}

	if (!spin_trylock(&alloc->lock))
		return;

	if (alloc->oneway_spam_detected) {
		if (!t->buffer->oneway_spam_suspect)
			{ spin_unlock(&alloc->lock); return; }
		for (n = rb_first(&alloc->allocated_buffers); n; n = rb_next(n)) {
			struct binder_buffer *b = rb_entry(n, struct binder_buffer, rb_node);

			if (b->pid == t->buffer->pid && b->async_transaction)
				total += binder_alloc_buffer_size(alloc, b);
		}
		large = total >= alloc->buffer_size / 4;
		status = large ? MASK_SPAM : MASK_LOW;
	} else {
		status = MASK_LOW;
	}

	if (atomic_read(&low_buffer_status_mask) & status)
		{ spin_unlock(&alloc->lock); return; }
	atomic_or(status, &low_buffer_status_mask);

	if (!spin_trylock(&binder_info_lock))
		{ spin_unlock(&alloc->lock); return; }
	clear_binder_info_locked();
	ktime_get_real_ts64(&ts);
	binder_info.ts = ts;
	binder_info.pid = proc->pid;
	len = strlen(proc->tsk->comm);
	memcpy(binder_info.process_name, proc->tsk->comm, len);
	binder_info.process_name[len] = '\0';
	if (large) {
		binder_info.total_alloc_size = total;
		binder_info.oneway_spam_suspect_pid = t->buffer->pid;
	}
	binder_info.buffer_size = alloc->buffer_size;
	binder_info.free_async_space = alloc->free_async_space;
	for (n = rb_first(&alloc->allocated_buffers); n; n = rb_next(n)) {
		struct binder_buffer *b = rb_entry(n, struct binder_buffer, rb_node);
		struct binder_node *tnode;
		struct binder_transaction *ttrans;
		struct binder_buf_node *node;

		if (!binder_info.oneway_spam_suspect_pid ||
		    b->pid == binder_info.oneway_spam_suspect_pid) {
			tnode = b->target_node;
			ttrans = b->transaction;
		} else {
			continue;
		}
		if (tnode && ttrans && b->async_transaction) {
			node = kzalloc(sizeof(*node), GFP_ATOMIC);
			if (!node) {
				pr_err("%s: kzalloc fail\n", __func__);
				spin_unlock(&binder_info_lock);
				goto out_unlock;
			}
			node->size = binder_alloc_buffer_size(alloc, b);
			node->pid = b->pid;
			node->ptr = tnode->ptr;
			node->cookie = tnode->cookie;
			node->code = ttrans->code;
			node->start_time = b->transaction->start_time;
			list_add_tail_inl(&node->list, &binder_info.list);
			if (++count >= MAX_BUFFER_COUNT) {
				pr_warn("%s: binder buffer too large, count:%d\n", __func__, count);
				break;
			}
		}
	}
	binder_info.valid_buffer_size = count;
	spin_unlock(&binder_info_lock);
	spin_unlock(&alloc->lock);
	pr_err("%s: extend_android_vh_binder_proc_transaction_finish start \n", __func__);
	atomic_or(MASK_EVENT, &low_buffer_status_mask);
	wake_up_interruptible(&epoll_event_wait);
	return;

out_unlock:
	spin_unlock(&alloc->lock);
	}
	}
}

int start_monitor(void)
{
	int ret = register_trace_android_vh_binder_proc_transaction_finish(
			extend_android_vh_binder_proc_transaction_finish, NULL);

	if (!ret)
		is_monitoring = 1;
	pr_info("%s: start_monitor ret:%d\n", __func__, ret);
	return ret;
}

int stop_monitor(void)
{
	int ret = unregister_trace_android_vh_binder_proc_transaction_finish(
			extend_android_vh_binder_proc_transaction_finish, NULL);

	spin_lock(&binder_info_lock);
	clear_binder_info_locked();
	spin_unlock(&binder_info_lock);
	if (!ret)
		is_monitoring = 0;
	pr_info("%s: stop_monitor ret:%d\n", __func__, ret);
	return ret;
}

static int bindermeminfo_show(struct seq_file *m, void *v)
{
	struct binder_buf_node *node;
	struct rtc_time tm = {};
	ktime_t now;

	if (!READ_ONCE(is_monitoring)) {
		seq_printf(m, "monitor not start\n");
		return 0;
	}
	atomic_and(~MASK_EVENT, &low_buffer_status_mask);
	if (!(atomic_read(&low_buffer_status_mask) & (MASK_SPAM | MASK_LOW))) {
		seq_printf(m, "binder free_async_space is sufficient\n");
		return 0;
	}
	spin_lock(&binder_info_lock);
	rtc_time64_to_tm(binder_info.ts.tv_sec, &tm);
	seq_printf(m, "binder mem info collect time(utc0):%d-%02d-%02d %02d:%02d:%02d\n",
		   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	seq_printf(m, "proc info pid: %d, process_name: %s, oneway_spam_suspect_pid:%d\n",
		   binder_info.pid, binder_info.process_name, binder_info.oneway_spam_suspect_pid);
	seq_printf(m, "alloc info buffer_size: %zu, free_async_space: %zu, total_alloc_size: %zu, valid_buffer_size:%d\n",
		   binder_info.buffer_size, binder_info.free_async_space,
		   binder_info.total_alloc_size, binder_info.valid_buffer_size);
	now = ktime_get();
	list_for_each_entry(node, &binder_info.list, list)
		seq_printf(m, "buffer_info buffer_size: %zu, pid: %d, node_info.ptr: %llx, node_info.cookie: %llx, code:%u, elapsed %lldms\n",
			   node->size, node->pid, node->ptr, node->cookie, node->code,
			   ktime_to_ms(ktime_sub(now, node->start_time)));
	spin_unlock(&binder_info_lock);
	return 0;
}

static int bindermeminfo_open(struct inode *inode, struct file *file)
{
	return single_open(file, bindermeminfo_show, NULL);
}

static ssize_t bindermeminfo_write(struct file *file, const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	static atomic_t bindermeminfo_write_busy;
	char buf[128] = {};
	unsigned long long val = 0;
	ssize_t ret = count;

	if (atomic_xchg(&bindermeminfo_write_busy, 1))
		return -EBUSY;
	if (count == 0 || count >= sizeof(buf) - 1) {
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(buf, ubuf, count)) {
		ret = -EFAULT;
		goto out;
	}
	buf[count] = '\0';
	if (kstrtoull(buf, 10, &val)) {
		ret = -EINVAL;
		goto out;
	}
	if (val == 0 && READ_ONCE(is_monitoring) == 1)
		stop_monitor();
	else if (val == 1 && READ_ONCE(is_monitoring) == 0)
		start_monitor();
	pr_info("%s: callser:%s, in_data:%lu\n", __func__, current->comm, (unsigned long)val);
out:
	atomic_set(&bindermeminfo_write_busy, 0);
	return ret;
}

/* local copy: the 6.6.144 poll_wait() adds a barrier the stock build does not have */
static __poll_t binder_mem_event_poll(struct file *file, poll_table *wait)
{
	if (wait && wait->_qproc)
		wait->_qproc(file, &epoll_event_wait, wait);
	return (atomic_read(&low_buffer_status_mask) & MASK_EVENT) ? EPOLLIN : 0;
}

static const struct proc_ops bindermeminfo_ops = {
	.proc_open	= bindermeminfo_open,
	.proc_read	= seq_read,
	.proc_write	= bindermeminfo_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_poll	= binder_mem_event_poll,
};

int init_bindermeminfo_proc(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("mi_bindermeminfo", 0600, NULL, &bindermeminfo_ops);
	if (!entry) {
		pr_err("%s: create bindermeminfo node failed\n", __func__);
		return -EIO;
	}
	proc_set_user(entry, KUIDT_INIT(1000), KGIDT_INIT(1000));
	return 0;
}

static int __init dump_binder_init(void)
{
	INIT_LIST_HEAD(&binder_info.list);
	init_waitqueue_head(&epoll_event_wait);
	return init_bindermeminfo_proc();
}

static void __exit dump_binder_exit(void)
{
	remove_proc_entry("mi_bindermeminfo", NULL);
	if (READ_ONCE(is_monitoring) == 1)
		stop_monitor();
}

module_init(dump_binder_init);
module_exit(dump_binder_exit);
MODULE_DESCRIPTION("Xiaomi binder async buffer monitor");
MODULE_LICENSE("GPL v2");
