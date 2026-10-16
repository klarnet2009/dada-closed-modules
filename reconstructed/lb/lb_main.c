// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (module glue, ioctl, init) */
#include "lb.h"

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

/* ---- module parameters / globals ---- */
int g_lb_msg_level = 2;
bool g_only_collect;
bool g_atrace_enable;
bool g_collect_specify_filetype;
bool g_collect_pinfile = true;
bool g_debug_verbose;
bool g_rlock_expire_interruput_enable = true;
bool g_swapin_expire_interruput_enable = true;
unsigned long long g_rlock_acquire_threshold = 3000;
unsigned long long g_swapin_total_time_threshold = 3000000;
bool g_pte_mark_file_anon_enable = true;
bool g_pte_fadvise_enable = true;
bool g_pte_anon_swapin_enable = true;
unsigned int g_pte_max_active_apps = 5;
unsigned int g_max_pte_segment_count = 20000;
struct lb_owner debug_owner;

module_param_named(lb_debug, g_lb_msg_level, int, 0644);
MODULE_PARM_DESC(lb_debug, "iorap debug msg level");
module_param_named(lb_only_collect, g_only_collect, bool, 0644);
MODULE_PARM_DESC(lb_only_collect, "iorap only collect");
module_param_named(lb_atrace_enable, g_atrace_enable, bool, 0644);
MODULE_PARM_DESC(lb_atrace_enable, "iorap atrace enable");
module_param_named(lb_collect_specify_filetype, g_collect_specify_filetype, bool, 0644);
MODULE_PARM_DESC(lb_collect_specify_filetype, "iorap only collect specify filetype enable");
module_param_named(lb_collect_pinfile, g_collect_pinfile, bool, 0644);
MODULE_PARM_DESC(lb_collect_pinfile, "iorap collect pinfile");
module_param_named(lb_debug_verbose, g_debug_verbose, bool, 0644);
MODULE_PARM_DESC(lb_debug_verbose, "iorap debug verbose");
module_param_named(lb_rlock_expire_interruput_enable, g_rlock_expire_interruput_enable, bool, 0644);
MODULE_PARM_DESC(lb_rlock_expire_interruput_enable, "interrupt preread when mmap lock acquire time exceed threshold");
module_param_named(lb_swapin_expire_interruput_enable, g_swapin_expire_interruput_enable, bool, 0644);
MODULE_PARM_DESC(lb_swapin_expire_interruput_enable, "interrupt preread when swapin time exceed threshold");
module_param_named(lb_rlock_acquire_threshold, g_rlock_acquire_threshold, ullong, 0644);
MODULE_PARM_DESC(lb_rlock_acquire_threshold, "(in us) mmap lock acquire time exceed this threshold will stop preread");
module_param_named(lb_swapin_total_time_threshold, g_swapin_total_time_threshold, ullong, 0644);
MODULE_PARM_DESC(lb_swapin_total_time_threshold, "(in us) swapin time exceed this threshold will stop preread");
module_param_named(lb_pte_mark_file_anon_enable, g_pte_mark_file_anon_enable, bool, 0644);
MODULE_PARM_DESC(lb_pte_mark_file_anon_enable, "mark pte_segment as anon and file or not");
module_param_named(lb_pte_fadvise_enable, g_pte_fadvise_enable, bool, 0644);
MODULE_PARM_DESC(lb_pte_fadvise_enable, "fadvise enable or not");
module_param_named(lb_pte_anon_swapin_enable, g_pte_anon_swapin_enable, bool, 0644);
MODULE_PARM_DESC(lb_pte_anon_swapin_enable, "anon swapin enable or not");
module_param_named(lb_pte_max_active_apps, g_pte_max_active_apps, uint, 0644);
MODULE_PARM_DESC(lb_pte_max_active_apps, "max active apps");
module_param_named(lb_max_pte_segment_count, g_max_pte_segment_count, uint, 0644);
MODULE_PARM_DESC(lb_max_pte_segment_count, "max pte segment count to limit memory usage");

static struct {
	int major;
	struct class *cls;
	struct device *dev;
	void *unused;
} g_lb_data;

static void lb_cold_exit(void)
{
	unregister_trace_android_vh_filemap_read(mi_lb_page_collect_on_readfile, NULL);
	unregister_trace_android_vh_filemap_map_pages_range(mi_lb_page_collect_on_pagefault, NULL);
	mi_lb_exit_manager();
	if (g_lb_data.cls)
		class_destroy(g_lb_data.cls);
	if (g_lb_data.dev)
		__unregister_chrdev(g_lb_data.major, 0, 256, "iorap_dev");
}

static long __attribute__((no_sanitize("bounds"))) mi_lb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct lb_owner owner = { 0 };
	struct lb_app_preread_info_size info_size = { 0 };
	void __user *argp = (void __user *)arg;
	struct lb_app_list *app_list;
	struct lb_app_preread_info *info;
	unsigned long buf_size;
	int ret = 0;

	switch (cmd) {
	case 0xc1084c01 + 0:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(98, "copy from user fail");
			return -EFAULT;
		}
		ret = mi_lb_dealwith_collect_cmd(APP_COLLECT_START, &owner);
		if (ret)
			LB_E(103, "lb collect cmd fail");
		break;
	case 0xc1084c01 + 18:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(225, "copy from user fail");
			return -EFAULT;
		}
		ret = mi_lb_dealwith_pte_interrupt_cmd(&owner);
		if (ret)
			LB_E(230, "lb pte interrupt fail");
		break;
	case 0xc1084c01 + 5:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(174, "copy from user fail");
			return -EFAULT;
		}
		debug_owner.uid = -1;
		ret = mi_lb_dealwith_collect_cmd(APP_LAUNCH_ABORT, &owner);
		if (ret)
			LB_E(180, "lb collect abort fail");
		break;
	case 0xc1084c01 + 6:
		if (g_only_collect)
			return -1;
		if (copy_from_user(&info_size, argp, sizeof(info_size))) {
			LB_E(110, "copy from user fail");
			return -EFAULT;
		}
		ret = mi_lb_dealwith_preread_size_cmd(&info_size);
		if (ret <= -2) {
			LB_E(115, "lb preread size fail");
			return -1;
		}
		if (ret == -1) {
			LB_E(119, "lb app %s cold start", info_size.owner.package_name);
			return -2;
		}
		if (copy_to_user(argp, &info_size, sizeof(info_size))) {
			LB_E(123, "LB_GET_PREREAD_INFO_SIZE: copy to user fail");
			return -EFAULT;
		}
		break;
	case 0xc1084c01 + 4:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(164, "copy from user fail");
			return -EFAULT;
		}
		debug_owner.uid = -1;
		ret = mi_lb_dealwith_collect_cmd(APP_LAUNCH_FINISH, &owner);
		if (ret)
			LB_E(170, "lb collect finish fail");
		break;
	case 0xc1084c01 + 2:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(184, "copy from user fail");
			return -EFAULT;
		}
		mi_lb_dealwith_pte_clean_cmd(&owner);
		mi_lb_dealwith_clear_cmd(&owner);
		ret = 0;
		break;
	case 0xc1084c01 + 1:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(86, "LB_PREREAD copy from user fail");
			return -EFAULT;
		}
		debug_owner.uid = owner.uid;
		ret = 0;
		break;
	case 0xc1084c01 + 7:
		if (get_user(buf_size, (unsigned long __user *)(arg + 0x110))) {
			LB_E(130, "get_user fail");
			return -EFAULT;
		}
		if (buf_size > 0xa00000) {
			LB_E(135, "buf_size %lu too large", buf_size);
			return -EINVAL;
		}
		info = kvmalloc(buf_size + sizeof(*info), GFP_KERNEL);
		if (!info) {
			LB_E(142, "alloc app_preread_info err");
			return -ENOMEM;
		}
		if (copy_from_user(info, argp, sizeof(*info))) {
			LB_E(146, "copy from user fail");
			kvfree(info);
			return -EFAULT;
		}
		if (mi_lb_dealwith_preread_info_cmd(info)) {
			LB_E(152, "lb pre read fail");
			kvfree(info);
			return -1;
		}
		if (copy_to_user(argp, info, info->file_info_buf_size)) {
			LB_E(157, "LB_GET_PREREAD_INFO_SIZE: copy to user fail");
			ret = -EFAULT;
		} else {
			ret = 0;
		}
		kvfree(info);
		break;
	case 0xc1084c01 + 8:
		app_list = kvmalloc(sizeof(*app_list), GFP_KERNEL);
		if (!app_list) {
			LB_E(193, "alloc app_list err");
			return -ENOMEM;
		}
		ret = mi_lb_dealwith_get_app_names(app_list);
		if (ret)
			LB_E(198, "lb get app names fail");
		if (copy_to_user(argp, app_list, sizeof(*app_list))) {
			LB_E(200, "LB_GET_APP_NAMES: copy to user fail");
			ret = -EFAULT;
		}
		kvfree(app_list);
		break;
	case 0xc1084c01 + 16:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(216, "copy from user fail");
			return -EFAULT;
		}
		ret = mi_lb_dealwith_pte_preread_cmd(&owner);
		if (ret)
			LB_E(221, "lb pte preread fail");
		break;
	case 0xc1084c01 + 15:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(207, "copy from user fail");
			return -EFAULT;
		}
		ret = mi_lb_dealwith_pte_snapshot_cmd(&owner);
		if (ret)
			LB_E(212, "lb pte snapshot fail");
		break;
	default:
		LB_E(233, "unknown cmd, cmd is %u", cmd);
		ret = -ENOIOCTLCMD;
		break;
	}
	return ret;
}

static const struct file_operations lb_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = mi_lb_ioctl,
	.compat_ioctl = mi_lb_ioctl,
};

static inline int mi_lb_chrdev_register(void)
{
	struct class *cls;
	long err;

	memset(&g_lb_data, 0, sizeof(g_lb_data));
	g_lb_data.major = __register_chrdev(0, 0, 256, "iorap_dev", &lb_fops);
	if (g_lb_data.major < 0) {
		LB_E(256, "register chrdev fail\n");
		return -ENXIO;
	}
	g_lb_data.cls = cls = class_create("iorap");
	if (IS_ERR_OR_NULL(cls)) {
		LB_E(261, "class create fail");
		err = PTR_ERR(cls);
		goto unreg;
	}
	g_lb_data.dev = device_create(cls, NULL, MKDEV(g_lb_data.major, 0), NULL, "iorap_dev");
	if (IS_ERR_OR_NULL(g_lb_data.cls)) {
		LB_E(269, "device create fail");
		err = PTR_ERR(g_lb_data.cls);
		goto unreg;
	}
	dev_set_drvdata(g_lb_data.dev, &g_lb_data);
	return 0;
unreg:
	__unregister_chrdev(g_lb_data.major, 0, 256, "iorap_dev");
	return err;
}

static inline int mi_lb_register_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_filemap_read(mi_lb_page_collect_on_readfile, NULL);
	if (ret) {
		LB_E(52, "register hook mi_lb_page_collect_on_readfile failed");
		return ret;
	}
	ret = register_trace_android_vh_filemap_map_pages_range(mi_lb_page_collect_on_pagefault, NULL);
	if (ret) {
		LB_E(59, "register hook mi_lb_page_collect_on_pagefault failed");
		unregister_trace_android_vh_filemap_read(mi_lb_page_collect_on_readfile, NULL);
		return ret;
	}
	return 0;
}

static int __init mi_lb_init(void)
{
	int ret;

	ret = mi_lb_chrdev_register();
	if (!ret)
		ret = mi_lb_init_manager();
	if (!ret)
		ret = mi_lb_register_hooks();
	if (ret) {
		LB_E(316, "lb_cold_init failed\n");
		return ret;
	}
	ret = lb_hot_init();
	if (ret) {
		LB_E(321, "lb_cold_init failed\n");
		lb_cold_exit();
		return ret;
	}
	ret = mi_lb_dbg_init();
	if (ret) {
		LB_E(327, "dbg init failed\n");
		lb_hot_exit();
		lb_cold_exit();
		return ret;
	}
	return 0;
}

static void __exit mi_lb_exit(void)
{
	lb_hot_exit();
	lb_cold_exit();
}

module_init(mi_lb_init);
module_exit(mi_lb_exit);
MODULE_LICENSE("GPL v2");
/* @@END@@ */
