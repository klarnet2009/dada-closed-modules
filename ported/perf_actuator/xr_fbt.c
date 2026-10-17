// SPDX-License-Identifier: GPL-2.0
/* xr_fbt: second ioctl dispatch table, linked into perf_actuator.ko. Reconstructed from the stock binary. */
#define pr_fmt(fmt) "xr_fbt: " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/rwsem.h>
#include <linux/ioctl.h>
#include "perf_actuator.h"

static DECLARE_RWSEM(g_fbt_table_rwsem);
static struct xr_fbt_entry g_xr_fbt_table[XR_FBT_CMD_MAX];

int register_xr_fbt(unsigned int cmd, XR_FBT_FUNC func)
{
	unsigned int idx = _IOC_NR(cmd);

	if (idx >= XR_FBT_CMD_MAX || !func)
		return -EINVAL;
	down_write(&g_fbt_table_rwsem);
	if (g_xr_fbt_table[idx].func)
		pr_err("xr_fbt: register %u conflict\n", cmd);
	g_xr_fbt_table[idx].cmd = cmd;
	g_xr_fbt_table[idx].func = func;
	up_write(&g_fbt_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(register_xr_fbt);

int unregister_xr_fbt(unsigned int cmd)
{
	unsigned int idx = _IOC_NR(cmd);

	if (idx >= XR_FBT_CMD_MAX)
		return -EINVAL;
	down_write(&g_fbt_table_rwsem);
	if (!(g_xr_fbt_table[idx].func && g_xr_fbt_table[idx].cmd == cmd))
		pr_err("xr_fbt: unregister %u conflict\n", cmd);
	g_xr_fbt_table[idx].cmd = 0;
	g_xr_fbt_table[idx].func = NULL;
	up_write(&g_fbt_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_xr_fbt);

static long xr_fbt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int idx;
	long ret;

	if (!arg) {
		pr_err("xr_fbt: invalid user uarg\n");
		return -EINVAL;
	}
	if (_IOC_TYPE(cmd) != PERF_ACTUATOR_MAGIC) {
		pr_err("xr_fbt: invalid magic %u\n", _IOC_TYPE(cmd));
		return -EINVAL;
	}
	idx = _IOC_NR(cmd);
	if (idx >= XR_FBT_CMD_MAX) {
		pr_err("xr_fbt: invalid cmd, %u > %d\n", idx, XR_FBT_CMD_MAX);
		return -ERANGE;
	}
	down_read(&g_fbt_table_rwsem);
	if (!g_xr_fbt_table[idx].func || g_xr_fbt_table[idx].cmd != cmd) {
		pr_err("xr_fbt: cmd not match or not registered!\n");
		ret = -ENOENT;
	} else {
		ret = g_xr_fbt_table[idx].func((void *)arg);
	}
	up_read(&g_fbt_table_rwsem);
	return ret;
}

static int xr_fbt_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int xr_fbt_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations xr_fbt_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = xr_fbt_ioctl,
	.open = xr_fbt_open,
	.release = xr_fbt_release,
};

/* ThinLTO promoted this object: the stock symbol is xr_fbt_device.llvm.<bitcode hash> (hash not reproducible) */
struct miscdevice xr_fbt_device __asm__("xr_fbt_device.llvm.11015404421372416167") __attribute__((visibility("hidden"))) = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xr_fbt",
	.fops = &xr_fbt_fops,
};

int xr_fbt_dev_init(void)
{
	return misc_register(&xr_fbt_device);
}

void xr_fbt_dev_exit(void)
{
	misc_deregister(&xr_fbt_device);
}
