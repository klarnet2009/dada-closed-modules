// SPDX-License-Identifier: Dual BSD/GPL
/* Reconstructed from the stock mibbr.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
/*
 * The stock module was built against 6.6.57, whose generic __access_ok() lacks the later "size <= limit" check;
 * provide that older form so that the inlined _copy_from_user() is identical.
 */
#define __access_ok __access_ok
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include "mibbr_internal.h"

/*
 * The stock module was linked with LTO, which folded the llvm.trap of the local-bounds instrumentation into the array-bounds trap.
 * no_sanitize("bounds") removes both here; the array-bounds trap (brk #0x5512) is written out by hand where the stock code has it.
 */
#define NO_BOUNDS_CHECK __attribute__((no_sanitize("bounds")))
#define ARRAY_BOUNDS_TRAP() do { asm volatile("brk #0x5512"); __builtin_unreachable(); } while (0)

static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long limit = TASK_SIZE_MAX;
	unsigned long addr = (unsigned long)ptr;

	return addr <= (limit - size);
}

/* ---- uid white list (/proc/mibbr/config) ---- */

#define BBR_UID_MAX	30

int bbr_uid_num;
int bbr_uid_list[BBR_UID_MAX];
static char bbr_data[256] = "clear\n";
struct proc_dir_entry *bbr_dir;
static struct proc_dir_entry *bbr_file;

bool is_mibbr_white_app(int uid)
{
	int i;

	for (i = 0; i < bbr_uid_num; i++) {
		if (bbr_uid_list[i] == uid)
			return true;
	}
	return false;
}

int update_bbr_uid_list(void)
{
	char buf[256] = { 0 };
	char *p = buf;
	char *token;
	unsigned int i = 0;

	memcpy(buf, bbr_data, strlen(bbr_data));
	memset(bbr_uid_list, 0, sizeof(bbr_uid_list));
	if (strlen(bbr_data) == 5 && !memcmp(bbr_data, "clear", 5)) {
		printk("mibbr:bbr uid is null");
		return 0;
	}
	for (;;) {
		token = strsep(&p, ",");
		if (!token || i >= BBR_UID_MAX)
			break;
		if (kstrtoint(token, 10, &bbr_uid_list[i])) {
			printk("mibbr:waning,uid error,token=%s", token);
		} else {
			printk("mibbr:i=%d,uid=%d,token=%s,size=%lu\n", i, bbr_uid_list[i], token, strlen(token));
			i++;
		}
	}
	return i;
}

static ssize_t bbr_file_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	ssize_t ret;

	ret = simple_read_from_buffer(buf, count, ppos, bbr_data, strlen(bbr_data));
	printk("mibbr:bbr_data=%s, len=%lu!\n", bbr_data, strlen(bbr_data));
	return ret;
}

static ssize_t bbr_file_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count >= sizeof(bbr_data))
		return -EINVAL;
	if (copy_from_user(bbr_data, buf, count))
		return -EFAULT;
	bbr_data[count] = '\0';
	printk("bbr_uid_update:%s,count=%lu\n", bbr_data, count);
	bbr_uid_num = update_bbr_uid_list();
	return count;
}

static const struct proc_ops bbr_fops = {
	.proc_read	= bbr_file_read,
	.proc_write	= bbr_file_write,
};

int bbr_config_init(void)
{
	bbr_dir = proc_mkdir("mibbr", NULL);
	if (!bbr_dir) {
		printk("mibbr:create bbr_dir fail");
		return -1;
	}
	bbr_file = proc_create_data("config", 0666, bbr_dir, &bbr_fops, NULL);
	if (!bbr_file) {
		printk("mibbr: %s Fail to create file: bbr_file\n", __func__);
		return -1;
	}
	printk("mibbr:config file create success!");
	return 0;
}

void mibbr_config_exit(void)
{
	proc_remove(bbr_dir);
}
