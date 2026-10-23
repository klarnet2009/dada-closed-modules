// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock cameralog.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define CAMLOG_MSG_LEN		1024
#define CAMLOG_FIFO_SIZE	(6 * sizeof(struct camlog_message))
#define CAMLOG_IOC_TOGGLE_DEBUG	0x40044d01
#define CAMLOG_IOC_DUMP		0x40044d02
#define CAMLOG_IOC_SET_DEBUG	0x40044d03
#define CAMLOG_IOC_GET_FIFO_LEN	0x80044d04
#define CAMLOG_IOC_GET_LAST_MSG	0x80084d05
#define CAMLOG_IOC_DEBUG_ON	0x40044d06
#define CAMLOG_IOC_DEBUG_OFF	0x40044d07

struct camlog_message {
	unsigned int uid;
	unsigned int pid;
	char msg[CAMLOG_MSG_LEN];
};

static struct kfifo message_kfifo;
static int camlog_major;
static struct class *camlog_class;
static dev_t devt;
static struct mutex camlog_message_lock;
static wait_queue_head_t camlog_is_not_empty;
static int debugLevel;
static struct camlog_message s_read_buf;
static unsigned int s_last_reader_uid;
static unsigned int s_last_reader_pid;
static struct camlog_message s_write_buf;
static unsigned int s_last_caller_uid;
static unsigned int s_last_caller_pid;
static char s_last_message[CAMLOG_MSG_LEN];

#define camlog_dbg(fmt, ...) \
	do { \
		if (debugLevel > 0) \
			printk(KERN_DEBUG "cameralog: " fmt, ##__VA_ARGS__); \
	} while (0)

void camlog_send_message(void)
{
	printk(KERN_DEBUG "cameralog:  camlog_send_message ss  \n");
}
EXPORT_SYMBOL(camlog_send_message);

static ssize_t cameralog_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;
	int len;
	int rc;
	char *msg = s_read_buf.msg;
	struct task_struct *task;

	camlog_dbg("cameralog_read .");
	mutex_lock(&camlog_message_lock);
	memset(&s_read_buf, 0, sizeof(s_read_buf));
	camlog_dbg("cameralog_read  kfifo len:%lu", kfifo_len(&message_kfifo) / sizeof(s_read_buf));
	ret = kfifo_out(&message_kfifo, &s_read_buf, sizeof(s_read_buf));
	if (ret != sizeof(s_read_buf)) {
		camlog_dbg("cameralog_read: not read anything from kfifo %d  expect:%lu\n", (int)ret,
			   sizeof(s_read_buf));
		ret = 0;
		goto out;
	}
	len = strlen(msg);
	if (!len) {
		ret = 0;
		goto out;
	}
	task = current;
	msg[len - 1] = '\n';
	camlog_dbg("cameralog_read message to user:%s   len:%d", s_read_buf.msg, (int)len);
	rc = copy_to_user(buf, s_read_buf.msg, len);
	ret = rc ? 0 : len;
	camlog_dbg("cameralog_read return len:%zd", ret);
	s_last_reader_uid = task->cred->uid.val;
	s_last_reader_pid = task->pid;
out:
	mutex_unlock(&camlog_message_lock);
	return ret;
}

static ssize_t cameralog_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	size_t valid_len, len;
	unsigned int ret;
	int rc;

	camlog_dbg("cameralog_write .");
	mutex_lock(&camlog_message_lock);
	memset(&s_write_buf, 0, sizeof(s_write_buf));
	valid_len = min_t(size_t, count, CAMLOG_MSG_LEN - 1);
	rc = copy_from_user(s_write_buf.msg, buf, valid_len);
	if (rc)
		len = 0;
	else
		len = strlen(s_write_buf.msg);
	camlog_dbg("cameralog_write  valid_len:%d  strlen:%zd.", (int)valid_len, len);
	if (!len) {
		mutex_unlock(&camlog_message_lock);
		return 0;
	}
	camlog_dbg("cameralog_write  input  msg : %s", s_write_buf.msg);
	while (kfifo_avail(&message_kfifo) < sizeof(s_write_buf)) {
		if (kfifo_out(&message_kfifo, &s_read_buf, sizeof(s_read_buf)) != sizeof(s_read_buf))
			message_kfifo.kfifo.out++;
	}
	ret = kfifo_in(&message_kfifo, &s_write_buf, sizeof(s_write_buf));
	if (ret != sizeof(s_write_buf)) {
		camlog_dbg("cameralog: Failed to write to kfifo %d -  %lu\n", (int)ret, sizeof(s_write_buf));
		mutex_unlock(&camlog_message_lock);
		return 0;
	}
	s_last_caller_uid = current_uid().val;
	s_last_caller_pid = current->pid;
	strncpy(s_last_message, s_write_buf.msg, len);
	camlog_dbg("cameralog message kfifo   len:%d", kfifo_len(&message_kfifo));
	mutex_unlock(&camlog_message_lock);
	wake_up_interruptible(&camlog_is_not_empty);
	return len;
}

static __poll_t cameralog_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	/* stock has no smp_mb() here: open-coded poll_wait() */
	if (wait && wait->_qproc)
		wait->_qproc(file, &camlog_is_not_empty, wait);
	if (!kfifo_is_empty(&message_kfifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static long cameralog_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	printk(KERN_DEBUG "cameralog:  cameralog_ioctl! %u  %lu\n", cmd, arg);
	switch (cmd) {
	case CAMLOG_IOC_TOGGLE_DEBUG:
		if (debugLevel > 0)
			goto debug_off;
		goto debug_on;
	case CAMLOG_IOC_GET_LAST_MSG: {
		size_t l = strlen(s_last_message) + 1;

		if (copy_to_user((void __user *)arg, s_last_message, l))
			goto fault;
		goto ok;
	}
	case CAMLOG_IOC_SET_DEBUG: {
		int val = 0;

		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			goto fault;
		debugLevel = val;
		goto ok;
	}
	case CAMLOG_IOC_DEBUG_ON:
debug_on:
		debugLevel = 1;
		goto ok;
	case CAMLOG_IOC_DEBUG_OFF:
debug_off:
		debugLevel = 0;
		goto ok;
	case CAMLOG_IOC_GET_FIFO_LEN: {
		int val = kfifo_len(&message_kfifo);

		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			goto fault;
		goto ok;
	}
	case CAMLOG_IOC_DUMP: {
		struct camlog_message tmp;
		int n, i;

		memset(&tmp, 0, sizeof(tmp));
		printk(KERN_DEBUG "cameralog: ----------------- DUMP CAMERALOG START------------------------");
		printk(KERN_DEBUG "cameralog: cameralog version %s", "2.0");
		printk(KERN_DEBUG "cameralog:  last writer uid:%u  pid:%u", s_last_caller_uid, s_last_caller_pid);
		printk(KERN_DEBUG "cameralog:  last reader uid:%u  pid:%u", s_last_reader_uid, s_last_reader_pid);
		printk(KERN_DEBUG "cameralog:  last message:%s ", s_last_message);
		n = kfifo_len(&message_kfifo);
		printk(KERN_DEBUG "cameralog:  kfifo len:%d ", n);
		if (n > 0) {
			printk(KERN_DEBUG "cameralog: -------fifo messages-------");
			printk(KERN_DEBUG "cameralog: UID\tPID\tMESSAGE");
			for (i = 0; i < n; i++) {
				if (kfifo_out(&message_kfifo, &tmp, sizeof(tmp)) != sizeof(tmp))
					break;
				printk(KERN_DEBUG "cameralog:   %u\t%u\t %s\n", tmp.uid, tmp.pid, tmp.msg);
			}
			printk(KERN_DEBUG "cameralog: -------------------------------");
		}
		printk(KERN_DEBUG "cameralog: ----------------- DUMP CAMERALOG END   ------------------------");
		goto ok;
	}
	default:
		return -ENOTTY;
	}
ok:
	return 0;
fault:
	return -EFAULT;
}

static int cameralog_open(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "cameralog:  cameralog_open!\n");
	return 0;
}

static int cameralog_release(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "cameralog:  cameralog_release!\n");
	wake_up_interruptible(&camlog_is_not_empty);
	return 0;
}

static const struct file_operations cameralog_fops = {
	.owner = THIS_MODULE,
	.read = cameralog_read,
	.write = cameralog_write,
	.poll = cameralog_poll,
	.unlocked_ioctl = cameralog_ioctl,
	.open = cameralog_open,
	.release = cameralog_release,
};

/*
 * The stock LTO link merges/zero-sizes the lock_class_key objects: the mutex key sits at its own address, the
 * waitqueue key coincides with camlog_is_not_empty itself.  Mimic that (the key is unused without lockdep).
 */
#define CAMLOG_KEY_SECTION	__attribute__((section(".bss.camlog_lockkeys")))
#undef mutex_init
#define mutex_init(mutex)						do {										static struct lock_class_key __key CAMLOG_KEY_SECTION;			__mutex_init((mutex), #mutex, &__key);				} while (0)

static int __init cameralog_init(void)
{
	int ret;
	struct device *dev;
	struct lock_class_key *wq_key;

	ret = kfifo_alloc(&message_kfifo, CAMLOG_FIFO_SIZE, GFP_KERNEL);
	if (ret) {
		printk(KERN_ERR "cameralog message kfifo: Failed to allocate kfifo\n");
		return ret;
	}
	camlog_major = register_chrdev(0, "camlog", &cameralog_fops);
	camlog_class = class_create("camlog");
	if (IS_ERR(camlog_class)) {
		unregister_chrdev(camlog_major, "camlog");
		printk(KERN_WARNING "cameralog: Failed to create class.\n");
		return PTR_ERR(camlog_class);
	}
	devt = MKDEV(camlog_major, 0);
	dev = device_create(camlog_class, NULL, devt, NULL, "camlog");
	if (IS_ERR(dev)) {
		printk(KERN_ERR "cameralog: error while trying to create %s\n", "camlog");
		return -EINVAL;
	}
	mutex_init(&camlog_message_lock);
	/* hide that both addresses are equal so the compiler keeps a separate adrp/add like the stock code */
	asm("adrp %0, camlog_is_not_empty\n\tadd %0, %0, :lo12:camlog_is_not_empty" : "=r"(wq_key));
	__init_waitqueue_head(&camlog_is_not_empty, "&camlog_is_not_empty", wq_key);
	return 0;
}

static void __exit cameralog_exit(void)
{
	device_destroy(camlog_class, devt);
	class_destroy(camlog_class);
	unregister_chrdev(camlog_major, "camlog");
	kfifo_free(&message_kfifo);
}

module_init(cameralog_init);
module_exit(cameralog_exit);

MODULE_DESCRIPTION("camera log device driver");
MODULE_LICENSE("GPL v2");
