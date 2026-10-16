// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock scene_swappiness.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#define pr_fmt(fmt) "scene_swappiness: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/vmscan.h>

static struct proc_dir_entry *swappiness_entry;

static int h_anon_swappiness = 160;
static int s_anon_swappiness = 60;
static unsigned int free_swap_threshold = 25600;

module_param_named(kswapd_vm_swappiness, h_anon_swappiness, int, 0644);
module_param_named(vm_swappiness, s_anon_swappiness, int, 0644);

static ssize_t swappiness_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[128] = { 0 };
	int len = 0;
	len += snprintf(&buffer[len], 128 - len, "Tend to recycle file pages,s_anon_swappiness: %d\n", s_anon_swappiness);
	len += snprintf(&buffer[len], 128 - len, "h_anon_swappiness: %d\n", h_anon_swappiness);
	len += snprintf(&buffer[len], 128 - len, "free_swap_threshold: %d\n", free_swap_threshold);
	if (len == 128)
		buffer[127] = 0;
	loff_t pos = *ppos;
	char *p = &buffer[pos];
	size_t n = len > pos ? (int)(len - pos) : 0;
	if (count > n)
		count = n;
	if (copy_to_user(buf, p, count))
		return -EFAULT;
	*ppos += count;
	return count;
}

static ssize_t swappiness_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char buffer[128] = { 0 };
	unsigned long val = 0;
	char *s, *p;

	if (count >= sizeof(buffer)) {
		pr_err("len %lu is too long\n", count);
		return -EINVAL;
	}
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	buffer[count] = '\0';
	s = strim(buffer);
	if (!s) {
		pr_err("buff %s is invalid\n", buffer);
		return -EINVAL;
	}

	p = strstr(s, "h_anon_swappiness=");
	if (p && !kstrtoul(p + 18, 0, &val)) {
		if (val <= 200) {
			h_anon_swappiness = val;
			return count;
		}
		pr_err("%lu is invalid\n", val);
	}
	p = strstr(s, "s_anon_swappiness=");
	if (p && !kstrtoul(p + 18, 0, &val)) {
		if (val <= 200) {
			s_anon_swappiness = val;
			return count;
		}
		pr_err("%lu is invalid\n", val);
	}
	p = strstr(s, "free_swap_threshold=");
	if (p && !kstrtoul(p + 20, 0, &val)) {
		if (val <= 200) {
			free_swap_threshold = val * 5 * 512;
			return count;
		}
		pr_err("%lu is invalid\n", val);
	}
	return -EINVAL;
}

static const struct proc_ops proc_swappiness_ops = {
	.proc_read = swappiness_read,
	.proc_write = swappiness_write,
	.proc_lseek = default_llseek,
};

static void to_set_swappiness(void *data, int *swappiness)
{
	if (atomic_long_read(&nr_swap_pages) < free_swap_threshold) {
		*swappiness = 0;
		return;
	}
	if (current_is_kswapd())
		*swappiness = h_anon_swappiness;
	else
		*swappiness = s_anon_swappiness;
}

static int __init create_swappiness_proc(void)
{
	struct proc_dir_entry *dir;
	int ret = 0;

	dir = proc_mkdir("mi_mem_swappiness", NULL);
	swappiness_entry = proc_create(dir ? "swappiness_para" : "mi_mem_swappiness/swappiness_para",
				       0666, dir, &proc_swappiness_ops);
	if (!swappiness_entry) {
		pr_err("Register swappiness interface failed TVT.\n");
		ret = -ENOMEM;
	} else {
		printk("Register swappiness interface success.\n");
	}
	return ret;
}

static int __init scene_swappiness_init(void)
{
	int ret;

	ret = create_swappiness_proc();
	if (ret)
		return ret;

	ret = register_trace_android_vh_tune_swappiness(to_set_swappiness, NULL);
	if (ret) {
		pr_err("register_trace_android_vh_set_swappiness failed! ret=%d\n", ret);
		proc_remove(swappiness_entry);
		swappiness_entry = NULL;
		return ret;
	}
	pr_info("scene_swappiness_init succeed!\n");
	return 0;
}

static void __exit scene_swappiness_exit(void)
{
	unregister_trace_android_vh_tune_swappiness(to_set_swappiness, NULL);
	proc_remove(swappiness_entry);
	swappiness_entry = NULL;
	pr_info("scene_swappiness_exit succeed!\n");
}

module_init(scene_swappiness_init);
module_exit(scene_swappiness_exit);
MODULE_LICENSE("GPL v2");
