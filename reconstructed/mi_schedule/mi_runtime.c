// SPDX-License-Identifier: GPL-2.0
/*
 * Reconstructed from the stock mi_schedule.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * Package (uid) runtime statistics proc interface.
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/user.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/rcupdate.h>
#include <linux/bitops.h>
#include "mi_sched.h"

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

#define MI_BITMAP_UIDS	99999

static struct user_struct *user_zero;
static struct proc_dir_entry *package_rootdir;
static struct proc_dir_entry *package_dir;
static struct proc_dir_entry *package_showall_entry;
static struct proc_dir_entry *package_top_bcore_entry;
static struct proc_dir_entry *package_top_lcore_entry;
static struct proc_dir_entry *package_top_entry;
static struct proc_dir_entry *package_trace_entry;
static struct proc_dir_entry *package_compact_entry;
static struct proc_dir_entry *stat_dir;
static struct proc_dir_entry *package_tracestat_entry;
static struct proc_dir_entry *package_trace_window;
static struct proc_dir_entry *package_window_size_entry;
static struct proc_dir_entry *package_develop_mode_entry;
static struct proc_dir_entry *package_resettraced_window;
static struct proc_dir_entry *package_pause_mode_entry;
static struct runtime_info *package_runtime_info;
static int top_package_uid[PACKAGE_TYPES];
int pause_mode;

static int develop_mode = 1;
static int runtime_traced_uid = 1000;
static int compact_traced_uid = 1000;
static unsigned int traced_window = 3;

static const char * const app_com_type_str[] = {
	[UNKNOW] = "unknow",
	[APP_32BIT] = "32",
	[APP_64BIT] = "64",
};

static int runtime_show_all(struct seq_file *m, void *v);
static int package_runtime_show(struct seq_file *m, void *v);

static int runtime_open_all(struct inode *inode, struct file *file)
{
	return single_open(file, runtime_show_all, NULL);
}

static int runtime_show_all(struct seq_file *m, void *v)
{
	int uid, nbits;
	unsigned int usec;
	u64 total_ns;
	u64 sum_s = 0, sum_b2 = 0, sum_b1 = 0, sum_l = 0;
	int cum_s = 0, cum_b2 = 0, cum_b1 = 0, cum_l = 0;
	int top_g = 0, top_s = 0, top_b2 = 0, top_b1 = 0, top_l = 0;
	struct user_struct *user;
	struct mi_user_struct *mi;

	nbits = min(*(int *)get_uid_max_value() + 1, MI_BITMAP_UIDS);
	usec = jiffies_to_usecs(jiffies - package_runtime_info[5].start);
	package_runtime_info[4].start = jiffies;
	if (develop_mode) {
		package_runtime_info[4].start = jiffies;
		usec = jiffies_to_usecs(jiffies - package_runtime_info[5].start);
	}
	if (!usec)
		return 0;
	seq_printf(m, "time info: from [%llu]s to [%llu]s ago\n",
		   (jiffies - package_runtime_info[4].start) / HZ, (jiffies - package_runtime_info[5].start) / HZ);
	total_ns = (u64)usec * 1000;
	for_each_set_bit(uid, package_uid_bit, nbits) {
		u64 d_s, d_b2, d_b1, d_l, total;
		int p_s, p_b2, p_b1, p_l, p_t;
		int bias_s, bias_b2, bias_b1;

		user = find_user(KUIDT_INIT(uid));
		if (!user)
			continue;
		mi = get_mi_user_struct(user);
		if (!mi) {
			printk("mi user struct is null\n");
			continue;
		}
		if (mi->pkg.little_cluster_runtime.v[4] < mi->pkg.little_cluster_runtime.v[5])
			continue;
		if (mi->pkg.big_cluster_1_runtime.v[4] < mi->pkg.big_cluster_1_runtime.v[5])
			continue;
		if (mi->pkg.big_cluster_2_runtime.v[4] < mi->pkg.big_cluster_2_runtime.v[5])
			continue;
		if (mi->pkg.sup_cluster_runtime.v[4] < mi->pkg.sup_cluster_runtime.v[5])
			continue;
		if (mi->pkg.sup_cluster_runtime.v[4] + mi->pkg.big_cluster_2_runtime.v[4] +
		    mi->pkg.big_cluster_1_runtime.v[4] + mi->pkg.little_cluster_runtime.v[4] ==
		    mi->pkg.sup_cluster_runtime.v[5] + mi->pkg.big_cluster_2_runtime.v[5] +
		    mi->pkg.big_cluster_1_runtime.v[5] + mi->pkg.little_cluster_runtime.v[5])
			continue;
		d_s = mi->pkg.sup_cluster_runtime.v[4] - mi->pkg.sup_cluster_runtime.v[5];
		d_b2 = mi->pkg.big_cluster_2_runtime.v[4] - mi->pkg.big_cluster_2_runtime.v[5];
		d_b1 = mi->pkg.big_cluster_1_runtime.v[4] - mi->pkg.big_cluster_1_runtime.v[5];
		d_l = mi->pkg.little_cluster_runtime.v[4] - mi->pkg.little_cluster_runtime.v[5];
		total = d_s + d_b2 + d_b1 + d_l;
		p_s = d_s * 10000 / total_ns;
		if (top_s <= p_s) {
			top_s = p_s;
			top_package_uid[TOP_ON_SCORE] = uid;
		}
		p_b2 = d_b2 * 10000 / total_ns;
		if (top_b2 <= p_b2) {
			top_b2 = p_b2;
			top_package_uid[TOP_ON_B2CORE] = uid;
		}
		p_b1 = d_b1 * 10000 / total_ns;
		if (top_b1 <= p_b1) {
			top_b1 = p_b1;
			top_package_uid[TOP_ON_B1CORE] = uid;
		}
		p_l = d_l * 10000 / total_ns;
		if (top_l <= p_l) {
			top_l = p_l;
			top_package_uid[TOP_ON_LCORE] = uid;
		}
		p_t = p_b2 + p_s + p_b1 + p_l;
		if (p_t > top_g) {
			top_g = p_t;
			top_package_uid[TOP_ALL] = uid;
		}
		cum_s += max(p_s, 0);
		cum_b2 += max(p_b2, 0);
		cum_b1 += max(p_b1, 0);
		cum_l += max(p_l, 0);
		sum_s += d_s;
		sum_b2 += d_b2;
		sum_b1 += d_b1;
		sum_l += d_l;
		bias_b1 = max((int)(d_b1 * 10000 / total), 0);
		bias_b2 = max((int)(d_b2 * 10000 / total), 0);
		bias_s = max((int)(d_s * 10000 / total), 0);
		seq_printf(m, "%5d: <s>%16llu [%3d.%02d] <b2>%16llu [%3d.%02d] <b1>%16llu [%3d.%02d] <l>: %16llu ",
			   uid, d_s, p_s / 100, p_s % 100, d_b2, p_b2 / 100, p_b2 % 100,
			   d_b1, p_b1 / 100, p_b1 % 100, d_l);
		seq_printf(m, " [%3d.%02d]--bias: s-[%3d.%02d] b2-[%3d.%02d] b1-[%3d.%02d] %s\n",
			   bias_s / 100, bias_s % 100, bias_b2 / 100, bias_b2 % 100,
			   bias_b1 / 100, bias_b1 % 100, p_l / 100, p_l % 100,
			   is_32B_user(uid) ? "32BIT" : "64BIT");
		free_uid(user);
	}
	seq_printf(m, "\nusage---sup cluster:[%d.%d] big cluster 2:[%d.%d] big cluster 1:[%d.%d] little cluster :[%d.%d]\n",
		   cum_s / 100, cum_s % 100, cum_b2 / 100, cum_b2 % 100, cum_b1 / 100, cum_b1 % 100,
		   cum_l / 100, cum_l % 100);
	seq_printf(m, "total_usage---sup cluster:[%llu] big cluster 2:[%llu] big cluster 1:[%llu] little cluster:[%llu] \n",
		   sum_s, sum_b2, sum_b1, sum_l);
	seq_printf(m, "top load package info--- 0 for s/b2/b1/l cluster\n");
	seq_printf(m, "%s: %d %3d.%02d\n", "g:     top_package", top_package_uid[TOP_ALL], top_g / 100, top_g % 100);
	seq_printf(m, "%s: %d %3d.%02d\n", "score: top_package", top_package_uid[TOP_ON_SCORE], top_s / 100, top_s % 100);
	seq_printf(m, "%s: %d %3d.%02d\n", "b2core: top_package", top_package_uid[TOP_ON_B2CORE], top_b2 / 100, top_b2 % 100);
	seq_printf(m, "%s: %d %3d.%02d\n", "b1core: top_package", top_package_uid[TOP_ON_B1CORE], top_b1 / 100, top_b1 % 100);
	seq_printf(m, "%s: %d %3d.%02d\n", "lcore: top_package", top_package_uid[TOP_ON_LCORE], top_l / 100, top_l % 100);
	return 0;
}

static const struct proc_ops package_show_all_fops = {
	.proc_open = runtime_open_all,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int top_package_on_b2core_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_runtime_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &top_package_uid[TOP_ON_B2CORE];
	return ret;
}

static int package_runtime_show(struct seq_file *m, void *v)
{
	int uid = *(int *)m->private;
	unsigned int usec;
	u64 window_ns;
	struct task_struct *p, *t;
	struct mi_task_struct *mi;
	u64 sum_s = 0, sum_b2 = 0, sum_b1 = 0, sum_l = 0;
	int cum_s = 0, cum_big = 0, cum_l = 0;
	bool is32 = false;

	usec = jiffies_to_usecs(jiffies - package_runtime_info[5].start);
	package_runtime_info[4].start = jiffies;
	if (develop_mode) {
		package_runtime_info[4].start = jiffies;
		usec = jiffies_to_usecs(jiffies - package_runtime_info[5].start);
	}
	window_ns = (u64)usec * 1000;
	seq_printf(m, "traced uid %d time delta is %llu\n", uid, window_ns);
	seq_printf(m, "time info: from [%llu]s to [%llu]s ago\n",
		   (jiffies - package_runtime_info[4].start) / HZ, (jiffies - package_runtime_info[5].start) / HZ);
	rcu_read_lock();
	for_each_process(p) {
		if (task_uid(p).val != uid)
			continue;
		for_each_thread(p, t) {
			u64 d_s, d_b2, d_b1, d_l, total;
			int p_s, p_b2, p_b1, p_l;
			int bias_s, bias_b2, bias_b1;

			mi = get_mi_task_struct(t);
			if (!mi)
				continue;
			is32 |= test_ti_thread_flag(task_thread_info(t), TIF_32BIT);
			if (mi->pkg.big_cluster_1_runtime.v[4] < mi->pkg.big_cluster_1_runtime.v[5])
				continue;
			if (mi->pkg.big_cluster_2_runtime.v[4] < mi->pkg.big_cluster_2_runtime.v[5])
				continue;
			if (mi->pkg.sup_cluster_runtime.v[4] < mi->pkg.sup_cluster_runtime.v[5])
				continue;
			d_s = mi->pkg.sup_cluster_runtime.v[4] - mi->pkg.sup_cluster_runtime.v[5];
			d_b2 = mi->pkg.big_cluster_2_runtime.v[4] - mi->pkg.big_cluster_2_runtime.v[5];
			d_b1 = mi->pkg.big_cluster_1_runtime.v[4] - mi->pkg.big_cluster_1_runtime.v[5];
			if (!(d_b2 + d_s + d_b1))
				continue;
			if (mi->pkg.little_cluster_runtime.v[4] < mi->pkg.little_cluster_runtime.v[5])
				continue;
			d_l = mi->pkg.little_cluster_runtime.v[4] - mi->pkg.little_cluster_runtime.v[5];
			total = d_l + d_b2 + d_s + d_b1;
			sum_b2 += d_b2;
			sum_s += d_s;
			sum_b1 += d_b1;
			sum_l += d_l;
			p_s = max((int)(d_s * 10000 / window_ns), 0);
			cum_s += p_s;
			p_b2 = max((int)(d_b2 * 10000 / window_ns), 0);
			p_b1 = max((int)(d_b1 * 10000 / window_ns), 0);
			p_l = max((int)(d_l * 10000 / window_ns), 0);
			cum_l += p_l;
			cum_big += p_b2 + p_b1;
			bias_b2 = max((int)(d_b2 * 10000 / total), 0);
			bias_s = max((int)(d_s * 10000 / total), 0);
			bias_b1 = max((int)(d_b1 * 10000 / total), 0);
			seq_printf(m, "%5d,%16s, s:%16llu [%3d.%02d], b2:%16llu [%3d.%02d], b1:%16llu [%3d.%02d]",
				   t->pid, t->comm, d_s, p_s / 100, p_s % 100, d_b2, p_b2 / 100, p_b2 % 100,
				   d_b1, p_b1 / 100, p_b1 % 100);
			seq_printf(m, "l:%16llu [%3d.%02d],| s:[%3d.%02d], b2:[%3d.%02d], b1:[%3d.%02d] %s\n",
				   d_l, p_l / 100, p_l % 100, bias_s / 100, bias_s % 100,
				   bias_b2 / 100, bias_b2 % 100, bias_b1 / 100, bias_b1 % 100,
				   test_ti_thread_flag(task_thread_info(t), TIF_32BIT) ? "32B" : "64B");
		}
	}
	seq_printf(m, "[%s]usage total sup:[%d.%d] big2:[%d.%d] big1:[%d.%d] little:[%d.%d] \n",
		   is32 ? "32B" : "64B", cum_s / 100, cum_s % 100, cum_big / 100, cum_big % 100, 0, 0,
		   cum_l / 100, cum_l % 100);
	seq_printf(m, "\ntotal_usage---sup cluster:[%llu] big cluster 2:[%llu] big cluster 1:[%llu] little cluster:[%llu] \n",
		   sum_s, sum_b2, sum_b1, sum_l);
	rcu_read_unlock();
	return 0;
}

static const struct proc_ops package_top_b2core_fops = {
	.proc_open = top_package_on_b2core_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int top_package_on_b1core_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_runtime_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &top_package_uid[TOP_ON_B1CORE];
	return ret;
}

static const struct proc_ops package_top_b1core_fops = {
	.proc_open = top_package_on_b1core_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int top_package_on_lcore_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_runtime_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &top_package_uid[TOP_ON_LCORE];
	return ret;
}

static const struct proc_ops package_top_lcore_fops = {
	.proc_open = top_package_on_lcore_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int top_package_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_runtime_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &top_package_uid[TOP_ALL];
	return ret;
}

static const struct proc_ops package_top_fops = {
	.proc_open = top_package_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int package_runtime_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_runtime_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &runtime_traced_uid;
	return ret;
}

static ssize_t trace_package_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = { 0 };
	size_t len = min(count, sizeof(buf));
	int uid;

	if (copy_from_user(buf, ubuf, len))
		return count;
	uid = simple_strtol(buf, NULL, 10);
	printk(KERN_ERR "got user info: %d....\n", uid);
	*(int *)((struct seq_file *)file->private_data)->private = uid;
	return count;
}

static const struct proc_ops package_trace_fops = {
	.proc_open = package_runtime_open,
	.proc_read = seq_read,
	.proc_write = trace_package_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int package_compact_show(struct seq_file *m, void *v);

static int package_compact_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, package_compact_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &compact_traced_uid;
	return ret;
}

static int package_compact_show(struct seq_file *m, void *v)
{
	int uid = *(int *)m->private;

	if (uid)
		seq_printf(m, "%d:%sBit\n", uid, app_com_type_str[is_32B_user(uid) ? APP_32BIT : APP_64BIT]);
	return 0;
}

static const struct proc_ops package_compact_fops = {
	.proc_open = package_compact_open,
	.proc_read = seq_read,
	.proc_write = trace_package_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int package_trace_stat(struct seq_file *m, void *v);

static int package_tracestat_open(struct inode *inode, struct file *file)
{
	return single_open(file, package_trace_stat, NULL);
}

static int package_trace_stat(struct seq_file *m, void *v)
{
	seq_printf(m, "traced uid  is %d\n", runtime_traced_uid);
	seq_printf(m, "traced max uid is %d\n", *(int *)get_uid_max_value());
	return 0;
}

static const struct proc_ops package_tracestat_fops = {
	.proc_open = package_tracestat_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int traced_window_show(struct seq_file *m, void *v);

static int package_open_traced_window(struct inode *inode, struct file *file)
{
	return single_open(file, traced_window_show, NULL);
}

static ssize_t traced_window_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = { 0 };
	size_t len = min(count, sizeof(buf));

	if (copy_from_user(buf, ubuf, len))
		return count;
	traced_window = simple_strtol(buf, NULL, 10);
	printk(KERN_ERR "traced window %d\n", traced_window);
	return count;
}

static int traced_window_show(struct seq_file *m, void *v)
{
	seq_printf(m, "traced window is last %d\n", traced_window);
	return 0;
}

static const struct proc_ops package_tracedwindow_fops = {
	.proc_open = package_open_traced_window,
	.proc_read = seq_read,
	.proc_write = traced_window_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int window_size_show(struct seq_file *m, void *v);

static int package_open_window_size(struct inode *inode, struct file *file)
{
	return single_open(file, window_size_show, NULL);
}

static int window_size_show(struct seq_file *m, void *v)
{
	seq_printf(m, "window size is %d\n", *(int *)get_runtime_window_size());
	return 0;
}

static const struct proc_ops package_windowsize_fops = {
	.proc_open = package_open_window_size,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int develop_mode_show(struct seq_file *m, void *v);

static int develop_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, develop_mode_show, NULL);
}

static ssize_t develop_mode_set(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = { 0 };
	size_t len = min(count, sizeof(buf));

	if (!copy_from_user(buf, ubuf, len))
		develop_mode = simple_strtol(buf, NULL, 10);
	return count;
}

static int develop_mode_show(struct seq_file *m, void *v)
{
	seq_printf(m, "develop mode %s\n", develop_mode ? "open" : "close");
	return 0;
}

static const struct proc_ops develop_mode_fops = {
	.proc_open = develop_mode_open,
	.proc_read = seq_read,
	.proc_write = develop_mode_set,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int reset_traced_window_show(struct seq_file *m, void *v);

static int package_reset_traced_window(struct inode *inode, struct file *file)
{
	return single_open(file, reset_traced_window_show, NULL);
}

static int reset_traced_window_show(struct seq_file *m, void *v)
{
	int idx, src, uid, nbits;
	struct user_struct *user;
	struct mi_user_struct *mi;
	struct task_struct *p, *t;
	struct mi_task_struct *mt;

	idx = traced_window > 3 ? *(int *)get_curr_runtime_items() + 2 :
	      *(int *)get_curr_runtime_items() - traced_window + 4;
	src = develop_mode == 0 ? idx % 4 : 4;
	nbits = min(*(int *)get_uid_max_value() + 1, MI_BITMAP_UIDS);
	for_each_set_bit(uid, package_uid_bit, nbits) {
		user = find_user(KUIDT_INIT(uid));
		if (user) {
			mi = get_mi_user_struct(user);
			if (!mi) {
				printk("mi user struct is null\n");
				continue;
			}
			mi->pkg.sup_cluster_runtime.v[5] = mi->pkg.sup_cluster_runtime.v[src];
			mi->pkg.big_cluster_2_runtime.v[5] = mi->pkg.big_cluster_2_runtime.v[src];
			mi->pkg.big_cluster_1_runtime.v[5] = mi->pkg.big_cluster_1_runtime.v[src];
			mi->pkg.little_cluster_runtime.v[5] = mi->pkg.little_cluster_runtime.v[src];
			free_uid(user);
		} else {
			mi = get_mi_user_struct(user_zero);
			mi->pkg.sup_cluster_runtime.v[5] = mi->pkg.sup_cluster_runtime.v[src];
			mi->pkg.big_cluster_2_runtime.v[5] = mi->pkg.big_cluster_2_runtime.v[src];
			mi->pkg.big_cluster_1_runtime.v[5] = mi->pkg.big_cluster_1_runtime.v[src];
			mi->pkg.little_cluster_runtime.v[5] = mi->pkg.little_cluster_runtime.v[src];
		}
		if (refcount_read(&user->__count) == 1) {
			clear_bit(user->uid.val, package_uid_bit);
			free_uid(user);
		}
	}
	package_runtime_info[5].start = jiffies;
	rcu_read_lock();
	for_each_process(p) {
		for_each_thread(p, t) {
			mt = get_mi_task_struct(t);
			if (!mt)
				continue;
			mt->pkg.sup_cluster_runtime.v[5] = mt->pkg.sup_cluster_runtime.v[src];
			mt->pkg.big_cluster_2_runtime.v[5] = mt->pkg.big_cluster_2_runtime.v[src];
			mt->pkg.big_cluster_1_runtime.v[5] = mt->pkg.big_cluster_1_runtime.v[src];
			mt->pkg.little_cluster_runtime.v[5] = mt->pkg.little_cluster_runtime.v[src];
		}
	}
	rcu_read_unlock();
	seq_printf(m, "reset traced window show success!\n");
	return 0;
}

static const struct proc_ops package_reset_tracedwindow_fops = {
	.proc_open = package_reset_traced_window,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int pause_mode_show(struct seq_file *m, void *v);

static int pause_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, pause_mode_show, NULL);
}

static ssize_t pause_mode_set(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = { 0 };
	size_t len = min(count, sizeof(buf));

	if (!copy_from_user(buf, ubuf, len))
		pause_mode = simple_strtol(buf, NULL, 10);
	return count;
}

static int pause_mode_show(struct seq_file *m, void *v)
{
	seq_printf(m, "pause mode %s\n", pause_mode ? "yes" : "no");
	return 0;
}

static const struct proc_ops pause_mode_fops = {
	.proc_open = pause_mode_open,
	.proc_read = seq_read,
	.proc_write = pause_mode_set,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

void create_runtime_proc(void)
{
	pause_mode = 0;
	user_zero = get_user_zero();
	package_rootdir = proc_mkdir("package", NULL);
	if (!package_rootdir)
		return;
	package_dir = proc_mkdir("pkg", package_rootdir);
	if (!package_dir)
		return;
	package_showall_entry = proc_create("show_all", 0664, package_dir, &package_show_all_fops);
	package_top_bcore_entry = proc_create("top_package_on_b2core", 0664, package_dir, &package_top_b2core_fops);
	package_top_bcore_entry = proc_create("top_package_on_b1core", 0664, package_dir, &package_top_b1core_fops);
	package_top_lcore_entry = proc_create("top_package_on_lcore", 0664, package_dir, &package_top_lcore_fops);
	package_top_entry = proc_create("top_package", 0664, package_dir, &package_top_fops);
	package_trace_entry = proc_create("show_traced_package", 0666, package_dir, &package_trace_fops);
	package_compact_entry = proc_create("show_32B_compact", 0666, package_dir, &package_compact_fops);
	stat_dir = proc_mkdir("stat", package_rootdir);
	if (!stat_dir)
		return;
	package_tracestat_entry = proc_create("show_tracestat", 0664, stat_dir, &package_tracestat_fops);
	package_trace_window = proc_create("show_traced_window", 0664, stat_dir, &package_tracedwindow_fops);
	package_window_size_entry = proc_create("show_windowsize", 0664, stat_dir, &package_windowsize_fops);
	package_develop_mode_entry = proc_create("develop_mode", 0666, stat_dir, &develop_mode_fops);
	package_resettraced_window = proc_create("reset_traced_window", 0664, stat_dir, &package_reset_tracedwindow_fops);
	package_pause_mode_entry = proc_create("pause_mode", 0666, stat_dir, &pause_mode_fops);
	package_runtime_info = get_package_runtime_info();
}

void delete_runtimeproc(void)
{
	if (!package_rootdir)
		return;
	if (!package_dir) {
		proc_remove(package_rootdir);
		return;
	}
	if (package_showall_entry)
		proc_remove(package_showall_entry);
	if (package_tracestat_entry)
		proc_remove(package_tracestat_entry);
	if (package_top_bcore_entry)
		proc_remove(package_top_bcore_entry);
	if (package_top_lcore_entry)
		proc_remove(package_top_lcore_entry);
	if (package_top_entry)
		proc_remove(package_top_entry);
	if (package_compact_entry)
		proc_remove(package_compact_entry);
	proc_remove(package_dir);
	if (!stat_dir)
		return;
	if (package_trace_entry)
		proc_remove(package_trace_entry);
	if (package_trace_window)
		proc_remove(package_trace_window);
	if (package_window_size_entry)
		proc_remove(package_window_size_entry);
	if (package_develop_mode_entry)
		proc_remove(package_develop_mode_entry);
	if (package_pause_mode_entry)
		proc_remove(package_pause_mode_entry);
	if (package_resettraced_window)
		proc_remove(package_resettraced_window);
	proc_remove(stat_dir);
	proc_remove(package_rootdir);
}
