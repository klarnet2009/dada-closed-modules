// SPDX-License-Identifier: GPL-2.0
/*
 * fault_report: reports f2fs fault events (tracepoint f2fs_fault_report) with a stack trace to miev.
 * Reconstructed from the HyperOS 4.0 stock binary (no public source).
 */
#define pr_fmt(fmt) "fault_report: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stacktrace.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include "mievent.h"

#define RATELIMIT_ENTRIES	16
#define RATELIMIT_KEY_LEN	74
#define RATELIMIT_JIFFIES	10800000UL	/* 3h at HZ=1000 */
#define STACK_DEPTH		10

struct ratelimit_entry {
	char key[RATELIMIT_KEY_LEN];
	unsigned long ts;
};

struct fault_event {
	unsigned long stack[STACK_DEPTH];
	struct work_struct work;
	char func[64];
	unsigned int event_id;
	unsigned int nr_entries;
	unsigned int line;
};

static struct tracepoint *tp_f2fs_fault_report;
static struct proc_dir_entry *proc_entry;
static struct workqueue_struct *fault_report_wq;
static struct ratelimit_entry ratelimit_entries[RATELIMIT_ENTRIES];
static u8 fault_report_enabled;
static DEFINE_SPINLOCK(ratelimit_lock);

static const u32 err_code_to_miev[3] = { 0x363dd18a, 0x363dd18b, 0x363dd18c };

static void find_f2fs_fault_tp(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, "f2fs_fault_report"))
		tp_f2fs_fault_report = tp;
}

static void report_work_handler(struct work_struct *work)
{
	struct fault_event *ev = container_of(work, struct fault_event, work);
	struct misight_mievent *event;
	char *buf;
	int pos = 0;
	unsigned int i;

	buf = kmalloc(1024, GFP_KERNEL);
	if (!buf)
		goto out;
	for (i = 0; i < ev->nr_entries && pos < 960; i++)
		pos += snprintf(buf + pos, 1024 - pos, "%pB\n", (void *)ev->stack[i]);
	if (!ev->event_id)
		goto out_free;
	event = cdev_tevent_alloc(ev->event_id);
	if (!event)
		goto out_free;
	if (cdev_tevent_add_str(event, "func", ev->func) == -1 ||
	    cdev_tevent_add_int(event, "line", ev->line) == -1 ||
	    cdev_tevent_add_str(event, "stack", buf) == -1)
		goto out_destroy;
	cdev_tevent_write(event);
out_destroy:
	cdev_tevent_destroy(event);
out_free:
	kfree(buf);
out:
	kfree(ev);
}

static void get_stack_info(void *ignore, struct super_block *sb, unsigned int err,
			   const char *func, unsigned int line)
{
	char key[RATELIMIT_KEY_LEN];
	unsigned long entries[STACK_DEPTH];
	struct fault_event *ev;
	unsigned long flags, now;
	int i, slot = -1, old_idx = -1;
	unsigned int nr;

	if (fault_report_enabled != 1)
		return;
	now = jiffies;
	snprintf(key, sizeof(key), "%s%u", func, line);
	spin_lock_irqsave(&ratelimit_lock, flags);
	for (i = 0; i < RATELIMIT_ENTRIES; i++) {
		struct ratelimit_entry *e = &ratelimit_entries[i];

		if (!e->key[0]) {
			if (slot == -1)
				slot = i;
			continue;
		}
		if (!strcmp(e->key, key)) {
			slot = i;
			goto chosen;
		}
		if (!time_after_eq(e->ts + RATELIMIT_JIFFIES, now)) {
			if (old_idx == -1 || time_before(e->ts, ratelimit_entries[old_idx].ts))
				old_idx = i;
		}
	}
	if (slot == -1)
		slot = old_idx;
chosen:
	i = slot;
	if (i < 0)
		goto unlock;
	if (!strcmp(ratelimit_entries[i].key, key)) {
		if (time_after_eq(ratelimit_entries[i].ts + RATELIMIT_JIFFIES, now))
			goto unlock;
	} else {
		strscpy(ratelimit_entries[i].key, key, RATELIMIT_KEY_LEN);
	}
	ratelimit_entries[i].ts = now;
	spin_unlock_irqrestore(&ratelimit_lock, flags);

	nr = stack_trace_save(entries, STACK_DEPTH, 3);
	if (!nr)
		return;
	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
	ev->event_id = err < ARRAY_SIZE(err_code_to_miev) ? err_code_to_miev[err] : 0;
	strscpy(ev->func, func, sizeof(ev->func));
	ev->nr_entries = min_t(unsigned int, nr, STACK_DEPTH);
	ev->line = line;
	memcpy(ev->stack, entries, ev->nr_entries * sizeof(unsigned long));
	INIT_WORK(&ev->work, report_work_handler);
	if (!queue_work(fault_report_wq, &ev->work))
		kfree(ev);
	return;
unlock:
	spin_unlock_irqrestore(&ratelimit_lock, flags);
}

static int fault_report_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", fault_report_enabled);
	return 0;
}

static int fault_report_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fault_report_proc_show, NULL);
}

static ssize_t fault_report_proc_write(struct file *file, const char __user *buf,
				       size_t count, loff_t *ppos)
{
	unsigned int val = 0;
	int ret = kstrtouint_from_user(buf, count, 10, &val);

	if (ret < 0)
		return ret;
	if (val <= 1) {
		fault_report_enabled = val == 1;
		return count;
	}
	return -EINVAL;
}

static const struct proc_ops fault_report_proc_ops = {
	.proc_open	= fault_report_proc_open,
	.proc_read	= seq_read,
	.proc_write	= fault_report_proc_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int __init fault_report_init(void)
{
	int ret, i;

	for_each_kernel_tracepoint(find_f2fs_fault_tp, NULL);
	if (!tp_f2fs_fault_report) {
		pr_warn("tracepoint f2fs_fault_report not found\n");
		return 0;
	}
	ret = tracepoint_probe_register(tp_f2fs_fault_report, get_stack_info, NULL);
	if (ret) {
		pr_err("failed to register probe, ret=%d\n", ret);
		return ret;
	}
	proc_entry = proc_create("fault_report", 0660, NULL, &fault_report_proc_ops);
	if (!proc_entry) {
		tracepoint_probe_unregister(tp_f2fs_fault_report, get_stack_info, NULL);
		pr_err("failed to create proc entry\n");
		return -ENOMEM;
	}
	fault_report_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM | 0xe0000, 1, "fault_report_wq");
	if (!fault_report_wq) {
		tracepoint_probe_unregister(tp_f2fs_fault_report, get_stack_info, NULL);
		proc_remove(proc_entry);
		proc_entry = NULL;
		pr_err("failed to create workqueue\n");
		return -ENOMEM;
	}
	for (i = 0; i < RATELIMIT_ENTRIES; i++) {
		ratelimit_entries[i].key[0] = '\0';
		ratelimit_entries[i].ts = 0;
	}
	pr_info("module loaded\n");
	return 0;
}

static void __exit fault_report_exit(void)
{
	if (tp_f2fs_fault_report)
		tracepoint_probe_unregister(tp_f2fs_fault_report, get_stack_info, NULL);
	tracepoint_synchronize_unregister();
	if (fault_report_wq) {
		flush_workqueue(fault_report_wq);
		destroy_workqueue(fault_report_wq);
		fault_report_wq = NULL;
	}
	if (proc_entry) {
		proc_remove(proc_entry);
		proc_entry = NULL;
	}
}

module_init(fault_report_init);
module_exit(fault_report_exit);
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: miev");
