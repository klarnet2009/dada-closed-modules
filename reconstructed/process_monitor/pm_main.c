// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock process_monitor.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include "process_monitor.h"

#define PM_ERR(fmt, ...)  printk(KERN_ERR "process_monitor-default: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define PM_INFO(fmt, ...) printk(KERN_INFO "process_monitor-default: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

struct proc_dir_entry *g_entry_process_monitor;

static int __init process_monitor_init(void)
{
	int ret;

	g_entry_process_monitor = proc_mkdir("process_monitor", NULL);
	if (!g_entry_process_monitor) {
#line 48
		PM_ERR("Can't create process_monitor proc entry\n");
		return -ENOMEM;
	}
	ret = init_signal_record(g_entry_process_monitor);
	if (ret) {
#line 55
		PM_ERR("signal_record init failed!\n");
		return ret;
	}
	ret = init_D_task(g_entry_process_monitor);
	if (ret) {
#line 61
		PM_ERR("D task init failed!\n");
		return ret;
	}
#line 65
	PM_INFO("init done!\n");
	return ret;
}

static void __exit process_monitor_exit(void)
{
	exit_signal_record(g_entry_process_monitor);
	exit_D_task(g_entry_process_monitor);
	remove_proc_entry("process_monitor", NULL);
	g_entry_process_monitor = NULL;
}

module_init(process_monitor_init);
module_exit(process_monitor_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xiaomi Kernel Process Monitor");
MODULE_AUTHOR("wangting11@xiaomi.com");
