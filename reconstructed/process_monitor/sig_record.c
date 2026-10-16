// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock process_monitor.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
/*
 * The stock module was built against 6.6.57, whose generic __access_ok() lacks the later "size <= limit" check;
 * provide that older form so that the inlined _copy_from_user() is identical.
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <trace/hooks/signal.h>
#include "process_monitor.h"

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

#define SR_ERR(fmt, ...)  printk(KERN_ERR "process_monitor-sig_record: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define SR_INFO(fmt, ...) printk(KERN_INFO "process_monitor-sig_record: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

#define SIG_HISTORY_MAX	50
#define SIG_NUM		32

struct sig_driver {
	unsigned long long sig_bitmap;
	spinlock_t lock;
	unsigned long reserved;
};

struct signal_record {
	unsigned long sig;
	long long time;
	int killer_pid;
	int killer_tgid;
	int killer_uid;
	char killer_comm[TASK_COMM_LEN];
	int victim_pid;
	int victim_tgid;
	int victim_uid;
	char victim_comm[TASK_COMM_LEN];
};

struct signal_history {
	char name[8];
	unsigned int size;
	unsigned int max;
	unsigned int cur_pos;
	unsigned int total;
	char reserved[16];
	struct signal_record rec[SIG_HISTORY_MAX];
};

unsigned char sig_monitor_defualt[2] = { SIGKILL, SIGSTOP };

char *sig_tab[SIG_NUM] = {
	"NULL", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS",
	"SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM",
	"SIGSTKFLT", "SIGCHLD", "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG",
	"SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO", "SIGPWR", "SIGSYS",
};

struct proc_dir_entry *g_sig_dir_entry;
struct sig_driver g_sig_driver;
struct signal_history *g_psig_history;

void write_record_to_buffer(struct signal_record *rec, char *buf, int *pos, int idx)
{
	struct tm tm;
	char timestr[16] = { 0 };

	time64_to_tm(rec->time, 0, &tm);
	snprintf(timestr, sizeof(timestr), "%02d:%02d:%02d %02d-%02d", tm.tm_hour, tm.tm_min, tm.tm_sec,
		 tm.tm_mon + 1, tm.tm_mday);
	*pos += snprintf(buf + *pos, 120,
			 "#%-3d  %-9s from [%-15s(%-5u,%-5u,%-5u)] to [%-15s(%-5u,%-5u,%-5u)] at %s\n",
			 idx, sig_tab[rec->sig], rec->killer_comm, rec->killer_pid, rec->killer_tgid, rec->killer_uid,
			 rec->victim_comm, rec->victim_pid, rec->victim_tgid, rec->victim_uid, timestr);
}

ssize_t sig_history_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	struct signal_record rec = { 0 };
	unsigned long flags;
	int len;
	unsigned int i, max, cur_pos, total;
	char *buf;
	ssize_t ret;

	buf = kmalloc(6120, GFP_KERNEL);
	if (!buf) {
#line 67
		SR_ERR("read buffer alloc failed ");
		return 0;
	}
	spin_lock_irqsave(&g_sig_driver.lock, flags);
	max = g_psig_history->max;
	cur_pos = g_psig_history->cur_pos;
	total = g_psig_history->total;
	spin_unlock_irqrestore(&g_sig_driver.lock, flags);
#line 76
	SR_INFO("history: cur_pos=%d, total=%d \n", cur_pos, total);
	len = snprintf(buf, 120, "#%-3s  %-9s       %-15s %-5s %-5s %-5s       %-15s %-5s %-5s %-5s      %s\n",
		       "NUM", "SIGNAL", "TASK_NAME", "PID", "TID", "UID", "TASK_NAME", "PID", "TID", "UID", "TIME");
	if (total > max) {
		for (i = 0; i < max; i++) {
			int idx = (cur_pos + 1 + i) % max;

			spin_lock_irqsave(&g_sig_driver.lock, flags);
			rec = g_psig_history->rec[idx];
			spin_unlock_irqrestore(&g_sig_driver.lock, flags);
			write_record_to_buffer(&rec, buf, &len, i);
		}
	} else {
		for (i = 0; i < total; i++) {
			spin_lock_irqsave(&g_sig_driver.lock, flags);
			rec = g_psig_history->rec[i];
			spin_unlock_irqrestore(&g_sig_driver.lock, flags);
			write_record_to_buffer(&rec, buf, &len, i);
		}
	}
	buf[len] = '\0';
	ret = simple_read_from_buffer(ubuf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

ssize_t monitor_list_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[512] = { 0 };
	int len = 0;
	int i;

	for (i = 1; i < SIG_NUM; i++) {
		if (g_sig_driver.sig_bitmap & (1 << i))
			len += snprintf(buf + len, 10, "%s,", sig_tab[i]);
	}
	if (len > 0)
		buf[len - 1] = '\n';
	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

ssize_t monitor_list_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[512] = { 0 };
	unsigned long long sig_map = 0;
	int pos = 0;
	int n = 0;
	int i;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
#line 165
	SR_INFO("signal strings : %s\n", buf);
	while (pos < count) {
		char name[16] = { 0 };

		sscanf(buf + pos, "%15[^,]%n", name, &n);
		if (name[n - 1] == '\n')
			name[n - 1] = '\0';
		for (i = 0; i < SIG_NUM; i++) {
			if (!strcmp(sig_tab[i], name)) {
				sig_map |= 1 << i;
				break;
			}
		}
		pos += n + 1;
	}
#line 185
	SR_INFO("show new bitmap 0x%llx\n", sig_map);
	if (sig_map)
		g_sig_driver.sig_bitmap = sig_map;
	return count;
}

static const struct proc_ops proc_sig_history_ops = {
	.proc_read = sig_history_read,
	.proc_lseek = default_llseek,
};

static const struct proc_ops proc_sig_monitor_list_ops = {
	.proc_read = monitor_list_read,
	.proc_write = monitor_list_write,
	.proc_lseek = default_llseek,
};

bool is_sig_need_record(int sig)
{
	if (sig > SIG_NUM - 1)
		return false;
	return g_sig_driver.sig_bitmap & (1 << sig);
}

void sig_bitmap_init(struct sig_driver *drv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sig_monitor_defualt); i++) {
		if (sig_monitor_defualt[i] < SIG_NUM)
			drv->sig_bitmap |= 1 << sig_monitor_defualt[i];
	}
#line 234
	SR_INFO("show sig bitmap 0x%llx\n", drv->sig_bitmap);
}

void sig_driver_init(struct sig_driver *drv)
{
	memset(drv, 0, sizeof(*drv));
	sig_bitmap_init(drv);
}

void sig_history_init(struct signal_history *hist)
{
	strncpy(hist->name, "signal", sizeof(hist->name));
	hist->size = 256;
	hist->max = SIG_HISTORY_MAX;
	hist->cur_pos = -1;
	hist->total = 0;
}

static void signal_record_hook_handle(void *data, int sig, struct task_struct *killer, struct task_struct *dst)
{
	struct signal_history *h;
	struct signal_record *rec;
	unsigned long flags;
	const struct cred *killer_cred, *dst_cred;
	int killer_uid, dst_uid;

	if (!is_sig_need_record(sig))
		return;
	struct timespec64 ts = { 0 };

#line 268
	SR_INFO("hook cat sig = %d", sig);
	rcu_read_lock();
	killer_cred = rcu_dereference(killer->real_cred);
	dst_cred = rcu_dereference(dst->real_cred);
	if (!killer_cred || !dst_cred) {
		rcu_read_unlock();
		return;
	}
	killer_uid = killer_cred->uid.val;
	dst_uid = dst_cred->uid.val;
	rcu_read_unlock();
#line 281
	SR_INFO("from %s %d:%d:%d to %s %d:%d:%d \n", killer->comm, killer->pid, killer->tgid, killer_uid, dst->comm, dst->pid, killer->tgid, dst_uid);
	if ((unsigned int)killer->pid < 2)
		return;
	ktime_get_coarse_real_ts64(&ts);
	spin_lock_irqsave(&g_sig_driver.lock, flags);
	h = g_psig_history;
	h->cur_pos++;
	h->total++;
	if (h->cur_pos >= h->max)
		h->cur_pos = 0;
	rec = &h->rec[h->cur_pos];
	memset(rec, 0, sizeof(*rec));
	rec->sig = sig;
	rec->killer_pid = killer->pid;
	rec->killer_tgid = killer->tgid;
	rec->killer_uid = killer_uid;
	rec->victim_pid = dst->pid;
	rec->victim_tgid = dst->tgid;
	rec->victim_uid = dst_uid;
	rec->time = ts.tv_sec + 28800;
	strncpy(rec->killer_comm, killer->comm, TASK_COMM_LEN);
	strncpy(rec->victim_comm, dst->comm, TASK_COMM_LEN);
	spin_unlock_irqrestore(&g_sig_driver.lock, flags);
#line 310
	SR_INFO("history: cur_pos=%d, total=%d, time = %lld \n", g_psig_history->cur_pos, g_psig_history->total, rec->time);
}

int init_signal_record(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *p1, *p2;

	g_psig_history = kzalloc(sizeof(*g_psig_history), GFP_KERNEL);
	if (!g_psig_history) {
#line 323
		SR_ERR("kzalloc failed!\n");
		return -ENOMEM;
	}
	g_sig_dir_entry = proc_mkdir("signal_record", parent);
	if (!g_sig_dir_entry) {
#line 329
		SR_ERR("%s: can't create signal_record proc entry\n", __func__);
		goto err;
	}
	p1 = proc_create("history", 0664, g_sig_dir_entry, &proc_sig_history_ops);
	p2 = proc_create("monitor_list", 0664, g_sig_dir_entry, &proc_sig_monitor_list_ops);
	if (!p1 || !p2) {
#line 338
		SR_ERR("proc_create failed!\n");
		goto err;
	}
	sig_driver_init(&g_sig_driver);
	sig_history_init(g_psig_history);
	register_trace_android_vh_do_send_sig_info(signal_record_hook_handle, NULL);
	return 0;
err:
	kfree(g_psig_history);
	g_psig_history = NULL;
	return -ENOMEM;
}

void exit_signal_record(struct proc_dir_entry *parent)
{
	kfree(g_psig_history);
	g_psig_history = NULL;
	unregister_trace_android_vh_do_send_sig_info(signal_record_hook_handle, NULL);
	remove_proc_entry("history", g_sig_dir_entry);
	remove_proc_entry("monitor_list", g_sig_dir_entry);
	remove_proc_entry("signal_record", parent);
}
