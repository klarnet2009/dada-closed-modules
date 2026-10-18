// SPDX-License-Identifier: GPL-2.0
/*
 * cameramsger.c - Xiaomi camera messager device (/dev/cam_msger) and camera scheduler helper
 *
 * Reconstructed from the stock cameramsger.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/cred.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <trace/hooks/sched.h>

#define MSGER_MAX_READERS	7
#define MSGER_MSG_NUM		16
#define MSGER_DATA_LEN		2048
#define MSGER_NAME_LEN		60
#define MSGER_DBG_MSGER		0x4cc
#define MSGER_DBG_SCHED		0x4cd

#define CAMMSG_IOC_DBG0		0x40044dcb
#define CAMMSG_IOC_DBG1		0x40044dcc
#define CAMMSG_IOC_DBG2		0x40044dcd
#define CAMMSG_IOC_DBG_OFF	0x40044dce
#define CAMMSG_IOC_DUMP		0x40044dcf
#define CAMMSG_IOC_SCHED	0x40044dd0
#define CAMMSG_IOC_SWITCH	0x40044dd1

struct msger_msghdr {
	int len;
	char from[60];
	char to[60];
};

struct msger_message {
	struct msger_msghdr hdr;
	char data[MSGER_DATA_LEN];
};

struct msger_reader {
	int tid;
	int pid;
	int used;
	int rear;
	int front;
	int pad;
	struct file *fp;
	char name[MSGER_NAME_LEN];
	struct msger_message msgs[MSGER_MSG_NUM];
	wait_queue_head_t wait_queue;
};

struct msger_dev {
	char name[25];
	char version[31];
	struct class *class;
	int major;
	dev_t devt;
	struct device *device;
	struct file_operations fops;
	struct mutex g_lock;
	int reader_count;
	struct msger_reader readers[MSGER_MAX_READERS];
	int debug;
	char buf[MSGER_DATA_LEN];
};

struct cam_sched_data {
	u8 flags[8];
	u8 offset;
	int pids[5];
	struct cpumask ctl_cpus;
	unsigned long reserved[2];
};

/* ThinLTO promoted the static to a hidden global with a hashed name */
struct msger_dev c_msger_dev __asm__("c_msger_dev.llvm.400187243914933332") __attribute__((visibility("hidden")));
unsigned int msger_siwtches = 1;
/*
 * The stock ThinLTO link merges csdata and the two zero-sized lock_class_key objects of cameramsger_init() at one .bss
 * address; the first symbol there is cameramsger_init.__key.  Name csdata like that and hand its address to the lock
 * initialisers as the key (the keys are unused without lockdep).
 */
static struct cam_sched_data __rcu *csdata __asm__("cameramsger_init.__key");
#define MSGER_KEY()							\
({									\
	struct lock_class_key *k__;					\
	asm("adrp %0, cameramsger_init.__key\n\tadd %0, %0, :lo12:cameramsger_init.__key" : "=r"(k__));	\
	k__;								\
})

int debug(int level)
{
	return c_msger_dev.debug == level;
}

static int cameramsger_open(struct inode *inode, struct file *file)
{
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_open, thread:%d file:%p\n", current->pid, file);
	return 0;
}

/* poll_wait() of the stock headers has no smp_mb() */
static inline void msger_poll_wait(struct file *filp, wait_queue_head_t *wait_address, poll_table *p)
{
	if (p && p->_qproc && wait_address)
		p->_qproc(filp, wait_address, p);
}

static __poll_t cameramsger_poll(struct file *file, poll_table *wait)
{
	struct msger_reader *r;
	struct task_struct *cur = NULL;
	int idx;

	if (!(msger_siwtches & 1))
		return 0;
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_poll pid:%d tid:%d file:%p\n", current->tgid, current->pid, file);
	mutex_lock(&c_msger_dev.g_lock);
	for (idx = 0; idx < MSGER_MAX_READERS; idx++) {
		r = &c_msger_dev.readers[idx];
		if (r->used && r->fp == file && (cur = current)->tgid == r->pid)
			break;
	}
	if (idx == MSGER_MAX_READERS) {
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	}
	mutex_unlock(&c_msger_dev.g_lock);
	r = &c_msger_dev.readers[idx];
	if (r->front != r->rear) {
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "cameramsger_poll tid:%d have messages.\n", cur->pid);
		return EPOLLIN | EPOLLRDNORM;
	}
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_poll poll_wait_begin.\n");
	msger_poll_wait(file, &r->wait_queue, wait);
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_poll poll_wait_end.\n");
	if (r->front == r->rear)
		return 0;
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_poll reader pid:%d tid:%d fp:%p have messages.\n", cur->pid, cur->tgid, file);
	return EPOLLIN | EPOLLRDNORM;
}

static ssize_t cameramsger_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct msger_reader *r;
	int tgid = 0;
	int idx, front, ret;
	size_t cnt, msg_len;

	if (!(msger_siwtches & 1))
		return -1016;
	mutex_lock(&c_msger_dev.g_lock);
	for (idx = 0; idx < MSGER_MAX_READERS; idx++) {
		r = &c_msger_dev.readers[idx];
		if (r->used && r->fp == file && (tgid = current->tgid) == r->pid)
			break;
	}
	if (idx == MSGER_MAX_READERS)
		return -1016;
	r = &c_msger_dev.readers[idx];
	if (r->front == r->rear) {
		ret = -1016;
		goto out;
	}
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_read find message curr-tid:%d c_msger_dev.readers[idx]_info pid:%d file:%p.\n",
		       current->pid, tgid, file);
	cnt = min_t(size_t, count, MSGER_DATA_LEN);
	ret = copy_to_user(buf, r->msgs[r->front].data, cnt);
	front = r->front;
	if (ret) {
		if (ret > 0)
			ret = -1017;
	} else {
		msg_len = strnlen(r->msgs[front].data, cnt);
		ret = msg_len;
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "cameramsger_read message(msg-idx:%d) read finish, mssage:%s, len:%d, ret:%d, message_len:%lu\n",
			       front, r->msgs[front].data, (int)cnt, ret, strnlen(r->msgs[front].data, cnt));
	}
	r->front = (front + 1) % MSGER_MSG_NUM;
out:
	mutex_unlock(&c_msger_dev.g_lock);
	return ret;
}

static int reg_reader(struct file *file, int len);
static int snd_message(int len);
static int find_reader_by_name_or_tgid(const char *name, int tgid);
int cam_sched_w_pids(struct file *file, char *buf, int len, u8 *hit);
int get_tgid_by_cmdline(struct file *file, char *buf, int len, u8 *hit);
void cam_sched_ioctl(struct file *file, unsigned int cmd, unsigned long arg, u8 *hit);

struct cam_submodule {
	void (*init)(void);
	int (*write)(struct file *file, char *buf, int len, u8 *hit);
	void *read;
	void *poll;
	void (*ioctl)(struct file *file, unsigned int cmd, unsigned long arg, u8 *hit);
	void *release;
};

void cam_sched_init(void);

static const struct cam_submodule submodules[] = {
	{ .init = cam_sched_init, .write = cam_sched_w_pids, .ioctl = cam_sched_ioctl },
	{ .write = get_tgid_by_cmdline },
};

static ssize_t cameramsger_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	u8 hit = false;
	int ret = 0, cpy_len, i;

	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s len:%zu", __func__, count);
	if (count < 5)
		return -1013;
	if (!buf)
		return -1009;
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s current thread tid:%d uid:%d\n", __func__, current->pid, __kuid_val(current_uid()));
	if ((__kuid_val(current_uid()) >> 4) > 0x270)
		return -1018;
	mutex_lock(&c_msger_dev.g_lock);
	memset(c_msger_dev.buf, 0, MSGER_DATA_LEN);
	cpy_len = copy_from_user(c_msger_dev.buf, buf, count);
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s: copy after:%s len:%zu cpy_len:%d", __func__, c_msger_dev.buf, count, cpy_len);
	if (!strncmp(c_msger_dev.buf, "swh", 3)) {
		char *p = c_msger_dev.buf;
		int val = 0;

		if (c_msger_dev.buf[3] != '|') {
			ret = -1010;
		} else {
			char *tok = strsep(&p, "|");

			if (tok)
				tok = strsep(&p, "|");
			if (tok && !kstrtoint(tok, 10, &val)) {
				if (debug(MSGER_DBG_MSGER))
					printk(KERN_INFO "%s v_swh:%d msger_siwtches_0x%x\n", "switch_update", val, msger_siwtches);
				msger_siwtches = val;
				if (debug(MSGER_DBG_MSGER))
					printk(KERN_INFO "%s v_swh:%d v_swh_0x%x msger_siwtches_0x%x\n", "switch_update", val, val,
					       msger_siwtches);
			}
			strsep(&p, "|");
			ret = count;
		}
		goto out;
	}
	if (msger_siwtches & 1) {
		if (!strncmp(c_msger_dev.buf, "reg", 3)) {
			ret = reg_reader(file, count);
			goto out;
		}
		if (!strncmp(c_msger_dev.buf, "snd", 3)) {
			ret = snd_message(count);
			goto out;
		}
	}
	for (i = 0; i < ARRAY_SIZE(submodules); i++) {
		if (!submodules[i].write)
			continue;
		ret = submodules[i].write(file, c_msger_dev.buf, count, &hit);
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "%s submodules[%d].write:%p hit:%d retv:%d", __func__, i, submodules[i].write, hit, ret);
		if (hit)
			break;
	}
out:
	mutex_unlock(&c_msger_dev.g_lock);
	return ret;
}

static int cameramsger_release(struct inode *inode, struct file *file)
{
	struct msger_reader *r;
	struct task_struct *task;
	int idx, i, rpid;
	bool die;

	if (in_interrupt())
		return 0;
	if (!file)
		return -1006;
	if (!(msger_siwtches & 1))
		return 0;
	mutex_lock(&c_msger_dev.g_lock);
	for (idx = 0; idx < MSGER_MAX_READERS; idx++) {
		r = &c_msger_dev.readers[idx];
		if (r->used && r->fp == file)
			break;
	}
	if (idx == MSGER_MAX_READERS || idx == 0) {
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	}
	r = &c_msger_dev.readers[idx];
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "cameramsger_release find reader:pid:%d tid:%d fp:%p name:%s:\n", r->pid, r->tid, file, r->name);
	rpid = r->pid;
	mutex_unlock(&c_msger_dev.g_lock);
	task = get_pid_task(find_vpid(rpid), PIDTYPE_PID);
	if (!task) {
		mutex_lock(&c_msger_dev.g_lock);
		die = true;
	} else {
		bool alive = task->thread_pid != NULL;

		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "[%s] pid_alive:%d", __func__, alive);
		if (!alive || task->__state == TASK_DEAD || task->exit_state == EXIT_DEAD ||
		    task->exit_state == EXIT_ZOMBIE || !task->files)
			die = true;
		else
			die = false;
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "[%s] other state check result: task_die:%d", __func__, die);
		put_task_struct(task);
		mutex_lock(&c_msger_dev.g_lock);
	}
	if (die) {
		for (i = 0; i < MSGER_MAX_READERS; i++) {
			if (i == idx)
				continue;
			if (!c_msger_dev.readers[i].used)
				continue;
			memset(c_msger_dev.buf, 0, MSGER_DATA_LEN);
			snprintf(c_msger_dev.buf, MSGER_DATA_LEN,
				 "snd|%s|%d|{\"id\":5, \"what\":%d, \"time\":%llu, \"replay\":0, \"data\":{\"tid\":%d, \"pid\":%d, \"name\":%s}}",
				 c_msger_dev.readers[i].name, 1015, 1015, ktime_get(), r->tid, r->pid, r->name);
			snd_message(strlen(c_msger_dev.buf));
			if (debug(MSGER_DBG_MSGER))
				printk(KERN_INFO "[%s] notify pid:%d that proc-%d is death.\n", __func__,
				       c_msger_dev.readers[i].pid, r->pid);
		}
	}
	mutex_unlock(&c_msger_dev.g_lock);
	mutex_lock(&c_msger_dev.g_lock);
	r->tid = 0;
	r->pid = 0;
	r->used = 0;
	r->rear = 0;
	r->front = 0;
	memset((char *)r + offsetof(struct msger_reader, fp), 0,
	       sizeof(r->fp) + sizeof(r->name) + sizeof(r->msgs));
	c_msger_dev.reader_count--;
	mutex_unlock(&c_msger_dev.g_lock);
	wake_up_interruptible(&r->wait_queue);
	return 0;
}

static long cameramsger_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	u8 hit = false;
	int i, k;
	struct msger_reader *r;

	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s: %u %lu\n", __func__, cmd, arg);
	switch (cmd) {
	case CAMMSG_IOC_DBG0:
		mutex_lock(&c_msger_dev.g_lock);
		c_msger_dev.debug = 0x4cb;
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	case CAMMSG_IOC_DBG1:
		mutex_lock(&c_msger_dev.g_lock);
		c_msger_dev.debug = MSGER_DBG_MSGER;
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	case CAMMSG_IOC_DBG2:
		mutex_lock(&c_msger_dev.g_lock);
		c_msger_dev.debug = MSGER_DBG_SCHED;
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	case CAMMSG_IOC_DBG_OFF:
		mutex_lock(&c_msger_dev.g_lock);
		c_msger_dev.debug = 0;
		mutex_unlock(&c_msger_dev.g_lock);
		return 0;
	case CAMMSG_IOC_DUMP:
		mutex_lock(&c_msger_dev.g_lock);
		printk(KERN_INFO "Reader count%d debug:%d\n", c_msger_dev.reader_count, c_msger_dev.debug);
		for (i = 0; i < MSGER_MAX_READERS; i++) {
			r = &c_msger_dev.readers[i];
			if (!r->used)
				continue;
			printk(KERN_INFO "Reader %s:\n", r->name);
			printk(KERN_INFO "   used:%d pid:%d tid:%d fp:%p name:%s:\n", r->used, r->pid, r->tid, r->fp, r->name);
			printk(KERN_INFO "    front:%d rear:%d Messages:\n", r->front, r->rear);
			printk(KERN_INFO "Message List:\n");
			for (k = 0; k < MSGER_MSG_NUM; k++)
				printk(KERN_INFO "    %d:from:%s to:%s [%s]\n", k, r->msgs[k].hdr.from, r->msgs[k].hdr.to, r->msgs[k].data);
		}
		break;
	case CAMMSG_IOC_SWITCH:
		printk(KERN_INFO "[%s] msger_siwtches:%x\n", __func__, msger_siwtches);
		return 0;
	default:
		cam_sched_ioctl(file, cmd, arg, &hit);
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "%s: submodules[%d].ioctl:%p hit:%d.\n", __func__, 0, cam_sched_ioctl, hit);
		return hit ? 0 : -ENOTTY;
	}
	mutex_unlock(&c_msger_dev.g_lock);
	return 0;
}

static int reg_reader(struct file *file, int len)
{
	int idx, k;
	struct msger_reader *r;
	bool dbg;
	char *name = &c_msger_dev.buf[4];

	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s\n", __func__);
	if (len < 5)
		return -1013;
	if (c_msger_dev.buf[3] != '|')
		return -1010;
	if (strlen(name) == 0)
		return -1010;
	if (strlen(name) > 59)
		return -1004;
	dbg = false;
	if (debug(MSGER_DBG_MSGER)) {
		printk(KERN_INFO "%s data:%s reader_name:%s\n", __func__, c_msger_dev.buf, name);
		dbg = c_msger_dev.debug == MSGER_DBG_MSGER;
	}
	idx = find_reader_by_name_or_tgid(name, current->tgid);
	if (dbg)
		printk(KERN_INFO "%s reader_already_registed reader_idx:%d\n", __func__, idx);
	if (idx != MSGER_MAX_READERS && idx != 0)
		return -1002;
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "find_new_reader_idx \n");
	for (k = 0; k < MSGER_MAX_READERS; k++)
		if (!c_msger_dev.readers[k].used)
			break;
	if (k == MSGER_MAX_READERS)
		return -1003;
	r = &c_msger_dev.readers[k];
	r->fp = file;
	r->used = 1;
	r->tid = current->pid;
	r->pid = current->tgid;
	c_msger_dev.reader_count++;
	snprintf(r->name, MSGER_NAME_LEN, "%s", name);
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "reader reg success! tid:%d, pid:%d, name:%s, fp:%p \n", r->tid, r->pid, r->name, r->fp);
	return len;
}

static int snd_message(int len)
{
	int pos[3] = { -1, -1, -1 };
	struct msger_msghdr hdr = { 0 };
	char data[MSGER_DATA_LEN] = { 0 };
	char second[MSGER_NAME_LEN] = { 0 };
	char first[MSGER_NAME_LEN] = { 0 };
	struct msger_reader *r;
	int i, n = 0, idx, rear;
	unsigned long j;
	char *bp = c_msger_dev.buf;

	if (len < 8)	/* G1ak: stock tests this before scanning (cmp w0,#8; b.lt -> -1010 without the debug print) */
		return -1010;
	for (i = 0; i < len; i++) {
		if (bp[i] != '|')
			continue;
		pos[n] = i;
		if (n == 2)
			break;
		n++;
	}
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s at position:%d - %d - %d", __func__, pos[0], pos[1], pos[2]);
	if (pos[1] - pos[0] < 2 || pos[2] - pos[1] < 2)
		return -1010;
	for (i = pos[0] + 1, j = 0; i < pos[1]; i++, j++)
		first[j] = bp[i];
	for (i = pos[1] + 1, j = 0; i < pos[2]; i++, j++)
		second[j] = bp[i];
	memcpy(data, &c_msger_dev.buf[pos[1] + 1], len - pos[1] - 1);
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s sender:%s recever:%s msg:%s\n", __func__, second, first, &c_msger_dev.buf[pos[1] + 1]);
	idx = find_reader_by_name_or_tgid(first, -1);
	if (idx == MSGER_MAX_READERS)
		return -1011;
	memcpy(hdr.from, second, sizeof(second));
	memcpy(hdr.to, first, sizeof(first));
	r = &c_msger_dev.readers[idx];
	rear = (r->rear + 1) % MSGER_MSG_NUM;
	if (rear == r->front)
		r->front = (rear + 1) % MSGER_MSG_NUM;
	r->msgs[r->rear].hdr = hdr;
	memcpy(r->msgs[r->rear].data, data, sizeof(data));
	r->rear = rear;
	wake_up_interruptible(&r->wait_queue);
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "%s add message to %d success\n", __func__, r->tid);
	return len;
}

static int find_reader_by_name_or_tgid(const char *name, int tgid)
{
	int i;

	for (i = 0; i < MSGER_MAX_READERS; i++) {
		struct msger_reader *r = &c_msger_dev.readers[i];

		if (!r->used)
			continue;
		if (name && !strcmp(name, r->name))
			return i;
		if (tgid > 0 && r->pid == tgid)
			return i;
	}
	return MSGER_MAX_READERS;
}

struct rq;
struct rq_flags;
static void xmi_rvh_is_cpu_allowed(void *unused, struct task_struct *p, int cpu, bool *allowed);
static void xmi_can_migrate_task(void *unused, struct task_struct *p, int dst_cpu, int *can_migrate);
static void xmi_rvh_select_task_rq_fair(void *unused, struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags,
					int *target_cpu);
static void xmi_sched_newidle_balance(void *unused, struct rq *this_rq, struct rq_flags *rf, int *pulled_task,
				      int *done);

void cam_sched_ioctl(struct file *file, unsigned int cmd, unsigned long arg, u8 *hit)
{
	struct cam_sched_data *cs;

	if (cmd != CAMMSG_IOC_SCHED)
		return;
	printk(KERN_INFO "struct cam_sched_data[csdata]\n");
	if (!csdata) {
		*hit = true;
		return;
	}
	rcu_read_lock();
	cs = rcu_dereference(csdata);
	printk(KERN_INFO "offset:%d\n", cs->offset);
	printk(KERN_INFO "ctl_cpus:%*pbl\n", nr_cpu_ids, cs->ctl_cpus.bits);
	printk(KERN_INFO "CPU-flags:\n");
	printk(KERN_INFO "cpu:%d flag:%d\n", 0, cs->flags[0]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 1, cs->flags[1]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 2, cs->flags[2]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 3, cs->flags[3]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 4, cs->flags[4]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 5, cs->flags[5]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 6, cs->flags[6]);
	printk(KERN_INFO "cpu:%d flag:%d\n", 7, cs->flags[7]);
	printk(KERN_INFO "The PID of the managed thread:\n");
	if (cs->pids[0] >= 1)
		printk(KERN_INFO "i:%d %d\n", 0, cs->pids[0]);
	if (cs->pids[1] >= 1)
		printk(KERN_INFO "i:%d %d\n", 1, cs->pids[1]);
	if (cs->pids[2] >= 1)
		printk(KERN_INFO "i:%d %d\n", 2, cs->pids[2]);
	if (cs->pids[3] >= 1)
		printk(KERN_INFO "i:%d %d\n", 3, cs->pids[3]);
	if (cs->pids[4] >= 1)
		printk(KERN_INFO "i:%d %d\n", 4, cs->pids[4]);
	rcu_read_unlock();
	*hit = true;
}

static void clear_cam_sched_data_entity(struct cam_sched_data *cs);
int find_pid_cmdline(char *cmdline);

int cam_sched_w_pids(struct file *file, char *buf, int len, u8 *hit)
{
	int cpu_val = 0;
	char *p = buf;
	int pids[5] = { 0 };
	char *tok;
	int nr = 0, flag = 4, pid;
	unsigned int npids = 0;
	int i;
	long j;
	struct cam_sched_data *ns, *old;

	if (!buf)
		return -2000;
	struct cpumask mask = { 0 };
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s] buff:%s len:%d \n", __func__, buf, len);
	*hit = false;
	if (!(msger_siwtches & 2) || strncmp(buf, "ctl", 3))
		return -1;
	*hit = true;
	if (len < 5)
		return -2001;
	if (len > 0x1ff || !strchr(buf, '|'))
		return -2002;
	tok = strsep(&p, "|");
	while (tok) {
		if (debug(MSGER_DBG_SCHED))
			printk(KERN_INFO "[%s] idx:%d token:%s sizeof_token:%lu rest:%s \n", __func__, nr, tok, strnlen(tok, 60), p);
		if (npids > 4)
			break;
		if (nr == 1) {
			if (kstrtoint(tok, 10, &cpu_val))
				return -2003;
			if (debug(MSGER_DBG_SCHED))
				printk(KERN_INFO "[%s] cpumask:%d \n", __func__, cpu_val);
			if (cpu_val > 255)
				return -2003;
			if (!cpu_val)
				break;
		} else if (nr == 2) {
			if (debug(MSGER_DBG_SCHED))
				printk(KERN_INFO "[%s] token_len:%lu, %lu, %lu \n", __func__, strlen(tok), 5UL, 10UL);
			if (strnlen(tok, 10) >= 5 && !strncmp(tok, "allow", 5)) {
				flag = 2;
			} else if (strnlen(tok, 10) >= 10 && !strncmp(tok, "neverallow", 10)) {
				flag = 4;
			} else {
				if (debug(MSGER_DBG_SCHED))
					printk(KERN_INFO "[%s] tisot_flag:%X\n", __func__, flag);
				break;
			}
		} else if (nr >= 3) {
			if ((int)strnlen(tok, 60) - 1 >= 1) {
				pid = find_pid_cmdline(tok);
				if (pid < 0) {
					if (debug(MSGER_DBG_SCHED))
						printk(KERN_INFO "[%s] token:%s pid:%d \n", __func__, tok, pid);
				} else {
					if (debug(MSGER_DBG_SCHED))
						printk(KERN_INFO "[%s] token:%s pid:%d \n", __func__, tok, pid);
					pids[npids++] = pid;
				}
			}
		}
		nr++;
		tok = strsep(&p, "|");
	}
	if (npids) {
		for (i = 0; i < npids - 1; i++) {
			for (j = i + 1; j < npids; j++) {
				if (pids[i] < pids[j]) {
					pids[i] ^= pids[j];
					pids[j] ^= pids[i];
					pids[i] ^= pids[j];
				}
			}
		}
	}
	ns = kmalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return -2004;
	clear_cam_sched_data_entity(ns);
	if (cpu_val) {
		for_each_possible_cpu(i) {
			if ((cpu_val >> i) & 1) {
				cpumask_set_cpu(i, &mask);
				ns->flags[i] = flag;
			}
		}
	}
	ns->offset = npids;
	memcpy(ns->pids, pids, sizeof(pids));
	ns->ctl_cpus = mask;
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s] ctl_cpus:%*pbl \n", __func__, nr_cpu_ids, mask.bits);
	old = rcu_dereference_protected(csdata, 1);
	rcu_assign_pointer(csdata, ns);
	synchronize_rcu();
	kfree(old);
	return len;
}

static void clear_cam_sched_data_entity(struct cam_sched_data *cs)
{
	cs->offset = 0;
	cpumask_clear(&cs->ctl_cpus);
	memset(cs->pids, 0, sizeof(cs->pids));
	memset(cs->flags, 0, sizeof(cs->flags));
}

void cam_sched_init(void)
{
	struct cam_sched_data *cs;

	cs = kmalloc(sizeof(*cs), GFP_KERNEL);
	csdata = cs;
	if (cs) {
		clear_cam_sched_data_entity(cs);
		register_trace_android_rvh_is_cpu_allowed(xmi_rvh_is_cpu_allowed, NULL);
		register_trace_android_rvh_can_migrate_task(xmi_can_migrate_task, NULL);
		register_trace_android_rvh_select_task_rq_fair(xmi_rvh_select_task_rq_fair, NULL);
		register_trace_android_rvh_sched_newidle_balance(xmi_sched_newidle_balance, NULL);
		if (debug(MSGER_DBG_SCHED))
			printk(KERN_INFO " %s finish. \n", __func__);
	} else if (debug(MSGER_DBG_SCHED)) {
		printk(KERN_INFO "[%s] struct xmi_cam_sched init failed.\n", __func__);
	}
}

static bool effect_cpu_action(struct task_struct *p, int cpu, u8 *flag, bool *hit, int *offset);

static void xmi_rvh_is_cpu_allowed(void *unused, struct task_struct *p, int cpu, bool *allowed)
{
	u8 flag = 0;
	int offset = 0;
	bool hit = false;
	bool result;

	if (!(msger_siwtches & 2))
		return;
	if (!effect_cpu_action(p, cpu, &flag, &hit, &offset))
		return;
	if (!offset) {
		*allowed = false;
		return;
	}
	result = true;
	*allowed = true;
	if (flag == 2 && !hit)
		{
		result = false;
		*allowed = false;
	}
	else if (flag == 4 && hit)
		{
		result = false;
		*allowed = false;
	}
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s] %s-%d cpu:%d allowed:%d\n", __func__, p->comm, p->pid, cpu, result);
}

static void xmi_can_migrate_task(void *unused, struct task_struct *p, int dst_cpu, int *can_migrate)
{
	u8 flag = 0;
	int offset = 0;
	bool hit = false;
	int result;

	if (!(msger_siwtches & 2))
		return;
	if (!effect_cpu_action(p, dst_cpu, &flag, &hit, &offset))
		return;
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s-begin] %s-%d dest_cpu:%d cam_migrate:%d ctl_proc:%d flag:%d\n", __func__, p->comm, p->pid,
		       dst_cpu, *can_migrate, hit, flag);
	if (!offset) {
		result = 0;
		*can_migrate = 0;
	} else {
		result = 1;
		*can_migrate = 1;
		if (flag == 2 && !hit)
			{
			result = 0;
			*can_migrate = 0;
		}
		else if (flag == 4 && hit)
			{
			result = 0;
			*can_migrate = 0;
		}
	}
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s-finish] %s-%d dest_cpu:%d cam_migrate:%d\n", __func__, p->comm, p->pid, dst_cpu, result);
}

static void xmi_rvh_select_task_rq_fair(void *unused, struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags,
					int *target_cpu)
{
	u8 flag = 0;
	int offset = 0;
	bool hit = false;
	unsigned long ctl;
	unsigned long cpu = (unsigned int)prev_cpu;

	if (!(msger_siwtches & 2))
		return;
	if (!effect_cpu_action(p, *target_cpu, &flag, &hit, &offset))
		return;
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s-begin] %s-%d prev_cpu:%d new_cpu:%d\n", __func__, p->comm, p->pid, prev_cpu, *target_cpu);
	if (p) {
		bool h = hit;
		u8 fl = flag;
		int o = offset;

		if (debug(MSGER_DBG_SCHED))
			printk(KERN_INFO "[%s] %s-%d cpu:%d ctl_proc:%d flag:%d offset:%d \n", "filter_cpu", p->comm, p->pid,
			       *target_cpu, h, fl, o);
		struct cpumask mask;
		unsigned long *bits;

		rcu_read_lock();
		bits = cpumask_bits(&rcu_dereference(csdata)->ctl_cpus);
		ctl = bits[BIT_WORD(cpu)];
		cpumask_andnot(&mask, p->cpus_ptr, &rcu_dereference(csdata)->ctl_cpus);
		rcu_read_unlock();
		if (debug(MSGER_DBG_SCHED))
			printk(KERN_INFO "[%s] %s-%d cpu:%d allowed_cpus:%*pbl\n", __func__, p->comm, p->pid, *target_cpu,
			       nr_cpu_ids, cpumask_bits(&mask));
		if (!cpumask_empty(&mask)) {
			if ((fl == 4 && h) || !o || (fl == 2 && !h)) {
				if (ctl & BIT_MASK(cpu))
					*target_cpu = cpumask_first(&mask);
				else
					*target_cpu = prev_cpu;
			}
		}
	}
	if (*target_cpu == -1)
		*target_cpu = prev_cpu;
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s-finish] %s-%d prev_cpu:%d new_cpu:%d\n", __func__, p->comm, p->pid, prev_cpu, *target_cpu);
}

static void xmi_sched_newidle_balance(void *unused, struct rq *this_rq, struct rq_flags *rf, int *pulled_task,
				      int *done)
{
	int cpu = get_cpu();
	struct cam_sched_data *cs;

	put_cpu();
	if (!(msger_siwtches & 2) || *pulled_task == 1)
		return;
	rcu_read_lock();
	cs = rcu_dereference(csdata);
	if (cs->flags[cpu]) {
		*done = 1;
		rcu_read_unlock();
		if (debug(MSGER_DBG_SCHED))
			printk(KERN_INFO "[%s] this-cpu:%d done:%d pulled_task:%d \n", __func__, cpu, *done, *pulled_task);
	} else {
		rcu_read_unlock();
	}
}

static bool effect_cpu_action(struct task_struct *p, int cpu, u8 *flag, bool *hit, int *offset)
{
	int pids[5];
	int n, i;
	bool in_list __maybe_unused;
	bool ret = false;

	if (!p || !p->mm)
		return false;
	if (!cpumask_test_cpu(cpu, cpu_active_mask))
		return false;
	if (p->nr_cpus_allowed < 3)
		return false;
	rcu_read_lock();
	if (!rcu_dereference(csdata)->flags[cpu]) {
		rcu_read_unlock();
		return false;
	}
	*flag = rcu_dereference(csdata)->flags[cpu];
	*offset = rcu_dereference(csdata)->offset;
	memcpy(pids, rcu_dereference(csdata)->pids, sizeof(pids));
	rcu_read_unlock();
	n = *offset;
	if (n == 0) {
		*hit = true;
		ret = true;
		goto out;
	}
	if (*flag & 4) {
		if (p->tgid > pids[0] || p->tgid < pids[n - 1])
			goto out;
		for (i = 0; i < n; i++) {
			if (p->tgid == pids[i]) {
				*hit = true;
				ret = true;
				goto out;
			}
		}
	}
	if (*flag & 2) {
		ret = true;
		if (p->tgid > pids[0] || p->tgid < pids[n - 1])
			goto out;
		for (i = 0; i < n; i++) {
			if (p->tgid == pids[i]) {
				*hit = true;
				ret = false;
				goto out;
			}
		}
	}
out:
	if (debug(MSGER_DBG_SCHED))
		printk(KERN_INFO "[%s] %s:%d cpu:%d ret:%d \n", __func__, p->comm, p->pid, cpu, ret);
	return ret;
}

int get_tgid_by_cmdline(struct file *file, char *buf, int len, u8 *hit)
{
	char *p = buf;
	char *tok;
	int pid;

	if (!buf || len < 5)
		return -3001;
	*hit = false;
	if (!(msger_siwtches & 4) || strncmp(buf, "ctp", 3))
		return -1;
	*hit = true;
	if (!strchr(buf, '|'))
		return -3002;
	if (!strsep(&p, "|"))
		return -3003;
	tok = strsep(&p, "|");
	if (!tok)
		return -3003;
	if ((int)strnlen(tok, 60) - 1 < 1)
		return -3001;
	pid = find_pid_cmdline(tok);
	if (pid < 0)
		return -3005;
	strsep(&p, "|");
	return pid;
}

int find_pid_cmdline(char *cmdline)
{
	int pids[10] = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct task_struct *task, *t;
	int n = 0, i, len, ret = -1;
	char *name;

	if (!cmdline)
		return -1;
	len = strnlen(cmdline, 60) - 1;
	if (len < 1)
		return -1;
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "[%s] cmdline:%s cmdline_len:%d\n", __func__, cmdline, len);
	if (list_empty(&init_task.tasks))
		return -1;
	rcu_read_lock();
	for (task = list_entry_rcu(init_task.tasks.next, struct task_struct, tasks);
	     task != &init_task && task;
	     task = list_entry_rcu(task->tasks.next, struct task_struct, tasks)) {
		if (!task->tasks.next || list_empty(&task->tasks))
			break;
		if (task->pid != task->tgid)
			continue;
		if (len <= 15 && strnlen(task->comm, 16) > len)
			continue;
		if (n > 9)
			break;
		if (strstr(cmdline, task->comm)) {
			pids[n++] = task->pid;
			if (debug(MSGER_DBG_MSGER))
				printk(KERN_INFO "[%s] cmdline:%s idx:%d pid:%d\n", __func__, task->comm, n - 1, task->pid);
		}
	}
	rcu_read_unlock();
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "[%s] cmdline_len:%d idx:%d\n", __func__, len, n);
	for (i = 0; i < 10; i++) {
		if (pids[i] < 1)
			continue;
		t = get_pid_task(find_vpid(pids[i]), PIDTYPE_PID);
		if (!t)
			continue;
		name = kstrdup_quotable_cmdline(t, GFP_KERNEL);
		if (debug(MSGER_DBG_MSGER))
			printk(KERN_INFO "[%s] tm_cmdline:%s cmdline:%s camdline_len:%d strncmp:%d\n", __func__, name, cmdline,
			       len, strncmp(name, cmdline, len));
		if (!strncmp(name, cmdline, len)) {
			ret = t->tgid;
			kfree(name);
			put_task_struct(t);
			return ret;
		}
		kfree(name);
		put_task_struct(t);
	}
	return -1;
}

static int __init cameramsger_init(void)
{
	int i, idx;
	struct device *dev;

	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "mode init cameramsger_init begin\n");
	strcpy(c_msger_dev.name, "cam_msger");
	strcpy(c_msger_dev.version, "2.0");
	c_msger_dev.fops.owner = THIS_MODULE;
	c_msger_dev.fops.open = cameramsger_open;
	c_msger_dev.fops.read = cameramsger_read;
	c_msger_dev.fops.write = cameramsger_write;
	c_msger_dev.fops.release = cameramsger_release;
	c_msger_dev.fops.poll = cameramsger_poll;
	c_msger_dev.fops.unlocked_ioctl = cameramsger_ioctl;
	c_msger_dev.major = register_chrdev(0, c_msger_dev.name, &c_msger_dev.fops);
	c_msger_dev.class = class_create(c_msger_dev.name);
	if (IS_ERR(c_msger_dev.class)) {
		unregister_chrdev(c_msger_dev.major, c_msger_dev.name);
		printk(KERN_WARNING "Failed to create class.\n");
		return PTR_ERR(c_msger_dev.class);
	}
	c_msger_dev.devt = MKDEV(c_msger_dev.major, 0);
	dev = device_create(c_msger_dev.class, NULL, c_msger_dev.devt, NULL, c_msger_dev.name);
	c_msger_dev.device = dev;
	c_msger_dev.reader_count = 0;
	memset((char *)&c_msger_dev + offsetof(struct msger_dev, debug), 0, sizeof(c_msger_dev.debug) + sizeof(c_msger_dev.buf));
	__mutex_init(&c_msger_dev.g_lock, "&c_msger_dev.g_lock", MSGER_KEY());
	for (idx = 0; idx < MSGER_MAX_READERS; idx++) {
		c_msger_dev.readers[idx].tid = 0;
		c_msger_dev.readers[idx].pid = 0;
		c_msger_dev.readers[idx].used = 0;
		c_msger_dev.readers[idx].rear = 0;
		c_msger_dev.readers[idx].front = 0;
		memset((char *)&c_msger_dev.readers[idx] + offsetof(struct msger_reader, name), 0,
		       sizeof(c_msger_dev.readers[idx].name) + sizeof(c_msger_dev.readers[idx].msgs));
		__init_waitqueue_head(&(c_msger_dev.readers[idx].wait_queue), "&(c_msger_dev.readers[idx].wait_queue)", MSGER_KEY());
	}
	if (IS_ERR(c_msger_dev.device)) {
		printk(KERN_ERR "error while trying to create %s\n", c_msger_dev.name);
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(submodules); i++)
		if (submodules[i].init)
			submodules[i].init();
	if (debug(MSGER_DBG_MSGER))
		printk(KERN_INFO "mode init sucesss\n");
	return 0;
}

module_init(cameramsger_init);

MODULE_DESCRIPTION("camera messager device driver");
MODULE_LICENSE("GPL v2");
