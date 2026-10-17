// SPDX-License-Identifier: GPL-2.0
/* perf_actuator: ioctl dispatch table for the performance modules (rtg, vip_sched ...).
 * Reconstructed from the HyperOS 4.0 stock binary (no public source). */
#define pr_fmt(fmt) "perf_actuator: " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/rwsem.h>
#include <linux/ioctl.h>
#include "perf_actuator.h"

static DECLARE_RWSEM(g_table_rwsem);
static struct actuator_entry g_perf_actuator_table[PERF_ACTUATOR_CMD_MAX];

int register_perf_actuator(unsigned int cmd, PERF_ACTUATOR_FUNC func)
{
	unsigned int idx = _IOC_NR(cmd);

	if (idx >= PERF_ACTUATOR_CMD_MAX || !func) {
		pr_err("invalid cmd:%d or func:%p\n", cmd, func);
		return -EINVAL;
	}
	down_write(&g_table_rwsem);
	if (g_perf_actuator_table[idx].func)
		pr_err("perf_actuator: register %u conflict\n", cmd);
	g_perf_actuator_table[idx].cmd = cmd;
	g_perf_actuator_table[idx].func = func;
	up_write(&g_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(register_perf_actuator);

int unregister_perf_actuator(unsigned int cmd)
{
	unsigned int idx = _IOC_NR(cmd);

	if (idx >= PERF_ACTUATOR_CMD_MAX)
		return -EINVAL;
	down_write(&g_table_rwsem);
	if (!(g_perf_actuator_table[idx].func && g_perf_actuator_table[idx].cmd == cmd))
		pr_err("perf_actuator: unregister %u conflict\n", cmd);
	g_perf_actuator_table[idx].cmd = 0;
	g_perf_actuator_table[idx].func = NULL;
	up_write(&g_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_perf_actuator);

static long perf_actuator_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int idx = _IOC_NR(cmd);
	long ret;

	pr_info("perf_actuator: perf_actuator_ioctl idx %d\n", idx);
	if (!arg) {
		pr_err("perf_actuator: invalid user uarg\n");
		return -EINVAL;
	}
	if (_IOC_TYPE(cmd) != PERF_ACTUATOR_MAGIC) {
		pr_err("perf_actuator: invalid magic %u\n", _IOC_TYPE(cmd));
		return -EINVAL;
	}
	if (idx >= PERF_ACTUATOR_CMD_MAX) {
		pr_err("perf_actuator: invalid cmd, %u > %d\n", idx, PERF_ACTUATOR_CMD_MAX);
		return -ERANGE;
	}
	down_read(&g_table_rwsem);
	if (!g_perf_actuator_table[idx].func || g_perf_actuator_table[idx].cmd != cmd) {
		pr_err("perf_actuator: idx %d, cmd %d not match or not registered!\n", idx, cmd);
		ret = -ENOENT;
	} else {
		ret = g_perf_actuator_table[idx].func((void *)arg);
	}
	up_read(&g_table_rwsem);
	return ret;
}

static int perf_actuator_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int perf_actuator_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations perf_actuator_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = perf_actuator_ioctl,
	.open = perf_actuator_open,
	.release = perf_actuator_release,
};

static struct miscdevice perf_actuator_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xr_perf_actuator",
	.fops = &perf_actuator_fops,
};

static int __init perf_actuator_init(void)
{
	int ret = misc_register(&perf_actuator_device);

	if (ret) {
		pr_info("perf_actuator: init failed!\n");
		return ret;
	}
	pr_info("perf_actuator: init success!\n");
	return xr_fbt_dev_init();
}

static void __exit perf_actuator_exit(void)
{
	misc_deregister(&perf_actuator_device);
	xr_fbt_dev_exit();
}

module_init(perf_actuator_init);
module_exit(perf_actuator_exit);
MODULE_LICENSE("GPL v2");
