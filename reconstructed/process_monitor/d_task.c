// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock process_monitor.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
/*
 * The stock module was built against 6.6.57, whose generic __access_ok() lacks the later "size <= limit" check;
 * provide that older form so that the inlined _copy_from_user() is identical.
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/stacktrace.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <linux/nmi.h>
#include <net/sock.h>
#include <net/net_namespace.h>
#include <trace/hooks/hung_task.h>
#include "process_monitor.h"

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

#define DTASK_ERR(fmt, ...)  printk(KERN_ERR "process_monitor-d_task: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DTASK_INFO(fmt, ...) printk(KERN_INFO "process_monitor-d_task: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define DTASK_MAX_ITEMS		20
#define DTASK_MAX_LINE		100
#define NETLINK_DTASK		23

enum detect_list_op {
	LIST_OP_SET,
	LIST_OP_ADD,
	LIST_OP_CLEAN,
};

enum detect_result {
	NO_TIME_OUT,
	HALF_TIME_OUT,
	FULL_TIME_OUT,
};

/* one line of /proc/process_monitor/D_task/detect_list: "<group leader comm>,<task comm>,<timeout in seconds>" */
typedef struct {
	char task_name[16];
	char node_name[16];
	int timeout;
	int state;		/* 0 = ok, 1 = half timeout, 2 = full timeout */
	bool flag;
} task_node_t;

struct Dtask {
	int enable;
	unsigned int num;
	int ignore_num;
	task_node_t list[DTASK_MAX_ITEMS];
	task_node_t ignore_list[DTASK_MAX_ITEMS];
	char *log_buf;
	int log_size;
	int log_pos;
	bool log_full;
	struct sock *nl_sk;
	int user_pid;
	struct task_struct *thread;
	bool debug;
};

struct Dtask *g_pDtask;
struct proc_dir_entry *d_task_entry;
struct mutex lock_d;

void trim(char *str)
{
	size_t len = strlen(str);
	char *start = str;
	char *end = str + len - 1;
	int n;

	while (*start && isspace(*start))
		start++;
	while (end > start && isspace(*end))
		end--;
	n = end - start + 1;
	memmove(str, start, n);
	str[n] = '\0';
}

int get_firstline_flag(char *buf, enum detect_list_op *flag)
{
	char *nl = strchr(buf, '\n');
	int len = nl ? nl - buf : strlen(buf);

	if (!strncmp(buf, "addition", len)) {
		*flag = LIST_OP_ADD;
		return len + 1;
	}
	if (!strncmp(buf, "clean", len)) {
		*flag = LIST_OP_CLEAN;
		return len + 1;
	}
	*flag = LIST_OP_SET;
	return 0;
}

ssize_t detect_list_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[512] = { 0 };
	char tmp[4] = { 0 };
	char *p = buf;
	int len;
	int i;
	int total;

	for (i = 0; i < g_pDtask->num; i++) {
		len = strlen(g_pDtask->list[i].task_name);
		memcpy(p, g_pDtask->list[i].task_name, len);
		p[len] = ',';
		p += len + 1;
		len = strlen(g_pDtask->list[i].node_name);
		memcpy(p, g_pDtask->list[i].node_name, len);
		p[len] = ',';
		p += len + 1;
		sprintf(tmp, "%d", g_pDtask->list[i].timeout);
		len = strlen(tmp);
		memcpy(p, tmp, len);
		p[len] = '\n';
		p += len + 1;
	}
	total = p - buf;
	return simple_read_from_buffer(ubuf, count, ppos, buf, total);
}

/* parse one "<task>,<node>,<timeout>" line and add it to the detect list */
static void set_config(char *line)
{
	task_node_t item;
	char tmp[4];
	char field[DTASK_MAX_LINE];
	char *p, *q;
	int len;
	size_t n;

	memset(&item, 0, sizeof(item));


	p = strchr(line, ',');
	if (!p) {
#line 128
		DTASK_ERR("set detect fail, string format err ");
		return;
	}
	len = min_t(size_t, p - line, DTASK_MAX_LINE - 1);
	strncpy(field, line, len);
	field[len] = '\0';
	trim(field);
	n = strlen(field) > sizeof(item.task_name) - 1 ? sizeof(item.task_name) - 1 : strlen(field);
	strncpy(item.task_name, field, n);
	p++;
	q = strchr(p, ',');
	if (!q) {
#line 145
		DTASK_ERR("set detect fail, string format err ");
		return;
	}
	len = min_t(size_t, q - p, DTASK_MAX_LINE - 1);
	strncpy(field, p, len);
	field[len] = '\0';
	trim(field);
	n = strlen(field) > sizeof(item.node_name) - 1 ? sizeof(item.node_name) - 1 : strlen(field);
	strncpy(item.node_name, field, n);
	q++;
	len = strlen(q) > DTASK_MAX_LINE - 1 ? DTASK_MAX_LINE - 1 : strlen(q);
	strncpy(field, q, len);
	field[len] = '\0';
	trim(field);
	if (strlen(field) >= sizeof(tmp)) {
#line 159
		DTASK_ERR("set detect fail, timeout string format err");
		return;
	}
	strncpy(tmp, field, strlen(field));
	if (kstrtoint(tmp, 10, &item.timeout)) {
#line 165
		DTASK_ERR("set detect fail, timeout string format err");
		return;
	}
	if (item.timeout > 0 && item.timeout <= 120) {
		g_pDtask->list[g_pDtask->num] = item;
		g_pDtask->num++;
#line 171
		DTASK_INFO("set detect sucess : group_leader comm = %s, task comm = %s , timeout = %d \n", item.task_name, item.node_name, item.timeout);
	} else {
#line 174
		DTASK_ERR("set detect fail, timeout val err");
	}
}

ssize_t detect_list_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char *kbuf;
	char line[DTASK_MAX_LINE];
	enum detect_list_op flag = LIST_OP_SET;
	int pos;

	if (count >= 512) {
#line 218
		DTASK_ERR("detect_list : set string len = %d > max len = %d,return \n", (int)count, 512);
		return -EINVAL;
	}
	kbuf = kzalloc(512, count);
	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;
#line 228
	DTASK_INFO("detect_list : %s\n", kbuf);
	pos = get_firstline_flag(kbuf, &flag);
	if (flag == LIST_OP_SET) {
		memset(g_pDtask->list, 0, sizeof(g_pDtask->list));
		g_pDtask->num = 0;
	}
	if (flag == LIST_OP_CLEAN) {
		memset(g_pDtask->list, 0, sizeof(g_pDtask->list));
		g_pDtask->num = 0;
		goto out;
	}
	while (pos < count) {
		char *nl = strchr(kbuf + pos, '\n');
		int len;

		if (nl)
			len = nl - (kbuf + pos);
		else
			len = strlen(kbuf + pos);
		memcpy(line, kbuf + pos, len > DTASK_MAX_LINE - 1 ? DTASK_MAX_LINE - 1 : len);
		line[len > DTASK_MAX_LINE - 1 ? DTASK_MAX_LINE - 1 : len] = '\0';
#line 245
		DTASK_INFO("detect_list oneline: %s\n", line);
		set_config(line);
		pos += len + 1;
	}
out:
	g_pDtask->enable = g_pDtask->num != 0;
	kfree(kbuf);
	return count;
}

ssize_t ignore_list_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[100] = { 0 };

	return simple_read_from_buffer(ubuf, count, ppos, buf, 0);
}

ssize_t ignore_list_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[100] = { 0 };

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	return count;
}

ssize_t get_log_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	int len = g_pDtask->log_full ? g_pDtask->log_size : g_pDtask->log_pos;

	return simple_read_from_buffer(ubuf, count, ppos, g_pDtask->log_buf, len);
}

ssize_t debug_into_D_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[20];
	int len;

	if (g_pDtask->debug) {
		wake_up_process(g_pDtask->thread);
		msleep(1000);
		mutex_lock(&lock_d);
		mutex_unlock(&lock_d);
		strcpy(buf, "30s D state end");
		len = sizeof("30s D state end");
	} else {
		strcpy(buf, "not enable debug");
		len = sizeof("not enable debug");
	}
	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

ssize_t debug_into_D_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[4] = { 0 };

	if (count >= sizeof(buf)) {
#line 314
		DTASK_ERR("string is too long, len = %d ,return \n", (int)count);
		return -EINVAL;
	}
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	if (count >= 2 && buf[1] == '\n')
		buf[1] = '\0';
	if (strlen(buf) != 1 || (buf[0] != '0' && buf[0] != '1')) {
#line 326
		DTASK_ERR(" only set 0 or 1 to debug_into_D,return \n");
		return -EINVAL;
	}
	g_pDtask->debug = buf[0] != '0';
	return count;
}

void savelog_timeout(struct task_struct *t, unsigned int timeout, enum detect_result type)
{
	static int index;
	struct Dtask *d = g_pDtask;
	char buf[1024] = { 0 };
	unsigned long entries[32] = { 0 };
	unsigned int nr, i;
	unsigned int len;

	if (!d->log_buf) {
#line 373
		DTASK_ERR("log buffer is null ,can't save\n");
		return;
	}
	len = snprintf(buf, 1024, " ######## Index %d task:%s PID:%d event:%s(%ds) jiffies:%lu ########\nKernel calltrace: \n",
		       index++, t->comm, t->pid, type == HALF_TIME_OUT ? "half-timeout" : "full-timeout",
		       type == HALF_TIME_OUT ? timeout / 2 : timeout, jiffies);
	nr = stack_trace_save_tsk(t, entries, 32, 0);
	for (i = 0; i < nr; i++)
		len += snprintf(&buf[len], 1024 - len, "[<%d>] %pB\n", i, (void *)entries[i]);
	if (d->log_pos + len > d->log_size) {
		d->log_pos = 0;
		d->log_full = true;
	}
	memcpy(d->log_buf + d->log_pos, buf, len);
	d->log_pos += len;
}

void show_state_filter_single(unsigned long state_filter)
{
	struct task_struct *g, *p;

	printk(KERN_INFO "  task \t\t\t\t\t   PC stack   pid father\n");
	rcu_read_lock();
	for_each_process_thread(g, p) {
		touch_softlockup_watchdog();
		if (p->__state == state_filter)
			sched_show_task(p);
	}
	rcu_read_unlock();
}

void crash_now(void)
{
	struct Dtask *d = g_pDtask;
	int i;

	console_loglevel = CONSOLE_LOGLEVEL_MOTORMOUTH;
	if (d->log_buf[0]) {
		printk(KERN_INFO "------ log buffer ------\n");
		for (i = 0; i < d->log_pos; i += 1024)
			printk(KERN_INFO "%s", d->log_buf + i);
	}
	printk(KERN_INFO "\n------ collect all D-state processes ------\n");
	show_state_filter_single(TASK_UNINTERRUPTIBLE);
	printk(KERN_INFO "------ end collecting, trigger panic now  ------\n");
#line 439
	BUG();
}

bool timeout_crash_enable(void)
{
	return true;
}

int send_log_by_netlink(char *log, int len)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int size, ret;

	if (!log) {
#line 455
		DTASK_ERR("dtask log is null\n");
		return -EINVAL;
	}
	if (!g_pDtask->user_pid) {
#line 460
		DTASK_ERR("user pid is null\n");
		return -EINVAL;
	}
	size = NLMSG_SPACE(len);
	skb = nlmsg_new(size, GFP_ATOMIC);
	if (!skb) {
#line 466
		DTASK_ERR("allocate skb failed\n");
		return -ENOMEM;
	}
	nlh = nlmsg_put(skb, 0, 0, 23, size - NLMSG_HDRLEN, 0);
	if (!nlh) {
#line 472
		DTASK_ERR("nlmsg_put failed\n");
		kfree_skb(skb);
		return -EMSGSIZE;
	}
	memcpy(nlmsg_data(nlh), log, len);
	NETLINK_CB(skb).dst_group = 0;
	ret = netlink_unicast(g_pDtask->nl_sk, skb, g_pDtask->user_pid, MSG_DONTWAIT);
	if (ret < 0) {
#line 481
		DTASK_ERR("sent data failed,retval = %d\n", ret);
	}
	return ret;
}

void send_log_to_user(void)
{
	struct Dtask *d = g_pDtask;
	int ret;

	if (d->log_buf[0]) {
		ret = send_log_by_netlink(d->log_buf, d->log_pos);
		if (ret < 0) {
#line 497
			DTASK_ERR("send netlink failed , %d\n", ret);
			return;
		}
		memset(d->log_buf, 0, d->log_size);
		d->log_pos = 0;
		d->log_full = false;
	}
}

int get_task_cmdline(struct task_struct *task, char *buf)
{
	struct mm_struct *mm;
	unsigned long arg_start, arg_end;
	int len, i, j;

	mm = get_task_mm(task);
	if (!mm || !mm->env_end)
		return 0;
	spin_lock(&mm->arg_lock);
	arg_start = mm->arg_start;
	arg_end = mm->arg_end;
	spin_unlock(&mm->arg_lock);
	if (arg_end <= arg_start)
		return 0;
	len = min_t(unsigned long, arg_end - arg_start, 63);
	len = access_process_vm(task, mm->arg_start, buf, len, 0);
	if (len > 0) {
		j = 0;
		for (i = 0; i < len; i++) {
			if (buf[i] && isprint(buf[i])) {
				buf[j] = buf[i];
				j++;
			}
		}
		buf[j] = '\0';
	}
	mmput(mm);
	return len;
}

void timeout_check(struct task_struct *t, task_node_t *item)
{
	unsigned long switch_count = t->nvcsw + t->nivcsw;
	unsigned long timeout;

#line 561
	DTASK_ERR("Task Info: switch_count=%lu, t->last_switch_count=%lu \n", switch_count, t->last_switch_count);
	if (switch_count != t->last_switch_count) {
		t->last_switch_count = switch_count;
		t->last_switch_time = jiffies;
		return;
	}
	timeout = msecs_to_jiffies(item->timeout * 1000);
#line 570
	DTASK_ERR("Time Info: last_j=%lu timeout_j=%lu cur_j=%lu \n", t->last_switch_time, timeout, jiffies);
	if (time_after(jiffies, t->last_switch_time + timeout)) {
#line 573
		DTASK_ERR("set_reult:  FULL_TIME_OUT \n");
		item->state = 2;
		savelog_timeout(t, item->timeout, FULL_TIME_OUT);
	} else if (time_after(jiffies, t->last_switch_time + timeout / 2)) {
#line 579
		DTASK_ERR("set_reult:  HALF_TIME_OUT \n");
		item->state = 1;
		savelog_timeout(t, item->timeout, HALF_TIME_OUT);
	} else {
		item->state = 0;
	}
	item->flag = true;
}

void check_task(void *unused, struct task_struct *t, unsigned long timeout, bool *need_check)
{
	struct task_struct *leader;
	task_node_t *item;
	unsigned int num, i;
	unsigned int state;

	*need_check = false;
	if (!g_pDtask->enable)
		return;
	state = READ_ONCE(t->__state);
	if ((state & (TASK_UNINTERRUPTIBLE | TASK_WAKEKILL | TASK_NOLOAD | TASK_FROZEN)) != TASK_UNINTERRUPTIBLE)
		return;
	if (READ_ONCE(t->flags) & PF_KTHREAD)
		return;
	num = g_pDtask->num;
	for (i = 0; i < num; i++) {
		item = &g_pDtask->list[i];
		if (item->task_name[0]) {
			leader = t->group_leader;
			if (!strncmp(item->task_name, leader->comm, strlen(item->task_name))) {
#line 617
				DTASK_ERR(" MATCH, task:[%s][pid:%d][tgid:%d], node:[%s][%s], timeout:[%d] \n", t->comm, t->pid, leader->pid, item->task_name, item->node_name, item->timeout);
				timeout_check(t, item);
				return;
			}
		}
		if (item->node_name[0]) {
			if (!strncmp(item->node_name, t->comm, strlen(item->node_name))) {
#line 623
				DTASK_ERR(" MATCH, task:[%s][pid:%d][tgid:%d], node:[%s][%s], timeout:[%d] \n", t->comm, t->pid, t->group_leader->pid, item->task_name, item->node_name, item->timeout);
				timeout_check(t, item);
				return;
			}
		}
	}
}

void check_task_total(void *unused, void *unused2)
{
	int i;

	if (!g_pDtask->enable)
		return;
#line 635
	DTASK_INFO("come into Dtask detect total\n");
	for (i = 0; i < g_pDtask->num; i++) {
		if (!g_pDtask->list[i].flag)
			g_pDtask->list[i].state = 0;
		else if (g_pDtask->list[i].state == 2)
			crash_now();
		else
			g_pDtask->list[i].flag = false;
	}
}

static int debug_into_D_thread(void *data)
{
	mutex_init(&lock_d);
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(MAX_SCHEDULE_TIMEOUT);
		if (g_pDtask->debug) {
			mutex_lock(&lock_d);
			msleep(30000);
			mutex_unlock(&lock_d);
			msleep(2000);
		}
	}
	return 0;
}

void debug_into_D_init(void)
{
	g_pDtask->thread = kthread_run(debug_into_D_thread, NULL, "kthread_debug_intoD");
}

void Dtask_struct_init(void)
{
	g_pDtask->log_buf = kzalloc(4096, GFP_KERNEL);
	if (g_pDtask->log_buf)
		g_pDtask->log_size = 4096;
}

static void dtask_recv_nl(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;

	if (!skb) {
#line 695
		DTASK_ERR("skb is NULL\n");
		return;
	}
	nlh = nlmsg_hdr(skb);
	if (nlh->nlmsg_flags == 0x4449) {
		g_pDtask->user_pid = nlh->nlmsg_pid;
#line 704
		DTASK_INFO("netlink recv message:%s,get pid[%u]\n", (char *)nlmsg_data(nlh), nlh->nlmsg_pid);
	} else {
#line 707
		DTASK_ERR("nlmsg_flags is error\n");
	}
}

void sendmsg_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = dtask_recv_nl,
	};

	g_pDtask->nl_sk = netlink_kernel_create(&init_net, NETLINK_DTASK, &cfg);
	if (!g_pDtask->nl_sk) {
#line 720
		DTASK_ERR(" create netlink socket error !!!\n");
	} else {
#line 723
		DTASK_ERR(" create netlink socket success\n");
	}
}

static const struct proc_ops proc_detect_list_ops = {
	.proc_read = detect_list_read,
	.proc_write = detect_list_write,
	.proc_lseek = default_llseek,
};

static const struct proc_ops proc_ignore_list_ops = {
	.proc_read = ignore_list_read,
	.proc_write = ignore_list_write,
	.proc_lseek = default_llseek,
};

static const struct proc_ops proc_get_log_ops = {
	.proc_read = get_log_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops proc_debug_into_D_ops = {
	.proc_read = debug_into_D_read,
	.proc_write = debug_into_D_write,
	.proc_lseek = default_llseek,
};

int init_D_task(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *p1, *p2, *p3, *p4;
	int ret;

	g_pDtask = kzalloc(sizeof(*g_pDtask), GFP_KERNEL);
	if (!g_pDtask) {
#line 740
		DTASK_ERR("kzalloc failed!\n");
		return -ENOMEM;
	}
	d_task_entry = proc_mkdir("D_task", parent);
	if (!d_task_entry) {
#line 746
		DTASK_ERR("%s: can't create D_task proc entry\n", __func__);
		ret = -ENOMEM;
		goto err;
	}
	p1 = proc_create("detect_list", 0664, d_task_entry, &proc_detect_list_ops);
	p2 = proc_create("ignore_list", 0664, d_task_entry, &proc_ignore_list_ops);
	p3 = proc_create("get_log", 0664, d_task_entry, &proc_get_log_ops);
	p4 = proc_create("debug_into_D", 0664, d_task_entry, &proc_debug_into_D_ops);
	if (!p1 || !p2 || !p3 || !p4) {
#line 758
		DTASK_ERR("dtask sub proc create failed!\n");
		ret = -ENOMEM;
		goto err;
	}
	ret = register_trace_android_vh_check_uninterruptible_tasks(check_task, NULL);
	if (!ret) {
		ret = register_trace_android_vh_check_uninterruptible_tasks_dn(check_task_total, NULL);
		if (ret)
			unregister_trace_android_vh_check_uninterruptible_tasks(check_task, NULL);
	}
	if (ret) {
#line 781
		DTASK_ERR("hook register failed!\n");
		goto err;
	}
	Dtask_struct_init();
	debug_into_D_init();
	sendmsg_netlink_init();
	return 0;
err:
	kfree(g_pDtask);
	g_pDtask = NULL;
	return ret;
}

void Dtask_struct_free(void)
{
	if (g_pDtask->log_buf)
		kfree(g_pDtask->log_buf);
	kfree(g_pDtask);
	g_pDtask = NULL;
}

void sendmsg_netlink_free(void)
{
	if (g_pDtask->nl_sk)
		netlink_kernel_release(g_pDtask->nl_sk);
}

void exit_D_task(struct proc_dir_entry *parent)
{
	kthread_stop(g_pDtask->thread);
	sendmsg_netlink_free();
	Dtask_struct_free();
	remove_proc_entry("detect_list", d_task_entry);
	remove_proc_entry("ignore_list", d_task_entry);
	remove_proc_entry("get_log", d_task_entry);
	remove_proc_entry("debug_into_D", d_task_entry);
	remove_proc_entry("D_task", parent);
	unregister_trace_android_vh_check_uninterruptible_tasks(check_task, NULL);
	unregister_trace_android_vh_check_uninterruptible_tasks_dn(check_task_total, NULL);
}
