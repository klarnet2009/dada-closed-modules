// SPDX-License-Identifier: GPL-2.0
/*
 * mi_wq_dynamic_priority: block-layer workqueue boost ("mikblockd") for dada.
 * Reconstructed from the HyperOS 4.0 stock binary (no public source).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/workqueue.h>
#include <linux/blk-mq.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/preempt.h>
#include <trace/hooks/blk.h>
#include <trace/hooks/wqlockup.h>

/* private workqueue flag of the stock kernel (checked in alloc_and_link_pwqs) */
#define MI_WQ_BOOST_FLAG	(1 << 14)

/* struct workqueue_struct is private to kernel/workqueue.c; ->flags sits at +0x100 */
#define WQ_FLAGS(wq)	(*(const unsigned int *)((const char *)(wq) + 0x100))

extern int get_task_qos_lvl(struct task_struct *p);

static struct workqueue_struct *mi_kblockd_workqueue;
static bool mikblockd_enabled = true;

static void sfi_delay_run_hw_queue(void *data, int cpu, struct blk_mq_hw_ctx *hctx,
				   unsigned long jif_delay, bool *skip)
{
	bool enabled = READ_ONCE(mikblockd_enabled);

	if (!jif_delay && likely(enabled) && likely(mi_kblockd_workqueue) && in_task()) {
		int lvl = get_task_qos_lvl(current);

		if (lvl == 1 || lvl == 2 || rt_task(current)) {
			*skip = queue_work_on(WORK_CPU_UNBOUND, mi_kblockd_workqueue, &hctx->run_work.work);
			return;
		}
	}
	*skip = false;
}

static void sfi_kick_requeue_list(void *data, struct request_queue *q,
				  unsigned long jif_delay, bool *skip)
{
	bool enabled = READ_ONCE(mikblockd_enabled);

	if (!jif_delay && likely(enabled) && likely(mi_kblockd_workqueue) && in_task()) {
		int lvl = get_task_qos_lvl(current);

		if (lvl == 1 || lvl == 2 || rt_task(current)) {
			*skip = queue_work_on(WORK_CPU_UNBOUND, mi_kblockd_workqueue, &q->requeue_work.work);
			return;
		}
	}
	*skip = false;
}

static void sfi_create_worker(void *data, struct task_struct *p, struct workqueue_attrs *attrs)
{
	if (attrs->nice == -1000) {
		sched_set_fifo_low(p);
		set_user_nice(p, -20);
	}
}

static void sfi_alloc_and_link_pwqs(void *data, struct workqueue_struct *wq, int *ret, bool *skip)
{
	struct workqueue_attrs *attrs;

	if (wq && (WQ_FLAGS(wq) & MI_WQ_BOOST_FLAG)) {
		attrs = alloc_workqueue_attrs();
		if (!attrs) {
			*skip = false;
			pr_err("sfi_v3: alloc_workqueue_attrs failed.\n");
			return;
		}
		attrs->nice = -1000;
		cpus_read_lock();
		*ret = apply_workqueue_attrs(wq, attrs);
		cpus_read_unlock();
		*skip = true;
		free_workqueue_attrs(attrs);
	} else {
		*skip = false;
	}
}

static int mikblockd_enable_show(struct seq_file *m, void *v)
{
	if (!m) {
		pr_err("seq_file is Null.\n");
		return -EFAULT;
	}
	seq_printf(m, "%d\n", READ_ONCE(mikblockd_enabled));
	return 0;
}

static int mikblockd_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, mikblockd_enable_show, pde_data(inode));
}

static ssize_t mikblockd_enable_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	unsigned long long val = 0;
	char kbuf[64];
	size_t len = min_t(size_t, count, sizeof(kbuf) - 1);
	int ret;

	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';
	ret = kstrtoull(kbuf, 10, &val);
	if (ret)
		return ret;
	if (val == 1) {
		mikblockd_enabled = true;
		pr_err("mikblockd_enabled is  true.\n");
	} else {
		mikblockd_enabled = false;
		pr_err("mikblockd_enabled is false.\n");
	}
	return len;
}

static const struct proc_ops mikblockd_enable_fops = {
	.proc_open	= mikblockd_enable_open,
	.proc_read	= seq_read,
	.proc_write	= mikblockd_enable_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int handler_alloc_workqueue_pre(struct kprobe *p, struct pt_regs *regs)
{
	const char *name = (const char *)regs->regs[0];
	unsigned int flags;

	if (!name)
		return 0;
	flags = regs->regs[1];
	if (!strncmp(name, "kverityd", 8)) {
		if (flags == 0x401a) {
			if (!strncmp(name, "loop", 4))
				regs->regs[1] = 0x4006;
			return 0;
		}
		regs->regs[1] = 0x401a;
		return 0;
	}
	if (!strncmp(name, "loop", 4)) {
		if (flags != 0x4006)
			regs->regs[1] = 0x4006;
	}
	return 0;
}

static struct kprobe mi_alloc_workqueue_kp = {
	.symbol_name	= "alloc_workqueue",
	.pre_handler	= handler_alloc_workqueue_pre,
};

static void unregister_mi_wq_dynprio_vendor_hooks(void)
{
	unregister_trace_android_vh_blk_mq_delay_run_hw_queue(sfi_delay_run_hw_queue, NULL);
	unregister_trace_android_vh_blk_mq_kick_requeue_list(sfi_kick_requeue_list, NULL);
}

static int register_mi_wq_dynprio_vendor_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_blk_mq_delay_run_hw_queue(sfi_delay_run_hw_queue, NULL);
	if (ret) {
		pr_err("register_trace_android_vh_blk_mq_delay_run_hw_queue failed! ret=%d\n", ret);
		return ret;
	}
	ret = register_trace_android_vh_blk_mq_kick_requeue_list(sfi_kick_requeue_list, NULL);
	if (ret) {
		pr_err("register_trace_android_vh_blk_mq_kick_requeue_list failed! ret=%d\n", ret);
		unregister_trace_android_vh_blk_mq_delay_run_hw_queue(sfi_delay_run_hw_queue, NULL);
		return ret;
	}
	ret = register_trace_android_rvh_create_worker(sfi_create_worker, NULL);
	if (ret) {
		pr_err("register_trace_android_rvh_create_worker failed! ret=%d\n", ret);
		unregister_mi_wq_dynprio_vendor_hooks();
		return ret;
	}
	ret = register_trace_android_rvh_alloc_and_link_pwqs(sfi_alloc_and_link_pwqs, NULL);
	if (ret) {
		pr_err("register_trace_android_rvh_alloc_and_link_pwqs failed! ret=%d\n", ret);
		unregister_mi_wq_dynprio_vendor_hooks();
		return ret;
	}
	return 0;
}

static int __init mi_wq_dynprio_init(void)
{
	int ret;

	ret = register_mi_wq_dynprio_vendor_hooks();
	if (ret) {
		mikblockd_enabled = false;
		pr_err("register_mi_wq_dynprio_vendor_hooks failed.\n");
		return 0;
	}
	if (!proc_create("mikblockd_enable", 0664, NULL, &mikblockd_enable_fops)) {
		mikblockd_enabled = false;
		pr_err("fail to create file node\n");
		unregister_mi_wq_dynprio_vendor_hooks();
		return 0;
	}
	mi_kblockd_workqueue = alloc_workqueue("mikblockd", 0x401a, 0);
	if (!mi_kblockd_workqueue) {
		mikblockd_enabled = false;
		pr_err("alloc  mi_kblockd_workqueue fail!\n");
		remove_proc_entry("mikblockd_enable", NULL);
		unregister_mi_wq_dynprio_vendor_hooks();
		return 0;
	}
	ret = register_kprobe(&mi_alloc_workqueue_kp);
	if (ret < 0) {
		mikblockd_enabled = false;
		pr_err("register_kprobe alloc_workqueue failed, returned %d\n", ret);
		remove_proc_entry("mikblockd_enable", NULL);
		unregister_mi_wq_dynprio_vendor_hooks();
		destroy_workqueue(mi_kblockd_workqueue);
		return 0;
	}
	try_module_get(THIS_MODULE);
	return 0;
}

static void __exit mi_wq_dynprio_exit(void)
{
	mikblockd_enabled = false;
	unregister_mi_wq_dynprio_vendor_hooks();
	unregister_kprobe(&mi_alloc_workqueue_kp);
	synchronize_rcu();
	remove_proc_entry("mikblockd_enable", NULL);
	if (mi_kblockd_workqueue)
		destroy_workqueue(mi_kblockd_workqueue);
}

module_init(mi_wq_dynprio_init);
module_exit(mi_wq_dynprio_exit);
MODULE_LICENSE("GPL");
