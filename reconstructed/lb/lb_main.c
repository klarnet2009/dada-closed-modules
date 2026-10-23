// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock lb.ko by reverse engineering; not original source. (module glue, ioctl, init) */
#include "lb.h"

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

/* ---- module parameters / globals ---- */
int g_lb_msg_level = 3;
bool g_only_collect;
bool g_atrace_enable;
bool g_collect_specify_filetype;
bool g_collect_pinfile = true;
bool g_debug_verbose;
bool g_rlock_expire_interruput_enable = true;
bool g_swapin_expire_interruput_enable = true;
unsigned long long g_rlock_acquire_threshold = 2000;
unsigned long long g_swapin_total_time_threshold = 30000;
bool g_pte_mark_file_anon_enable = true;
bool g_pte_fadvise_enable;
bool g_pte_anon_swapin_enable = true;
unsigned int g_pte_max_active_apps = 50;
unsigned int g_max_pte_segment_count = 1638400;
bool g_file_page_reclaim_supported;
bool g_real_timing_supported;
unsigned int g_max_gap_range = 4;
unsigned int g_max_merge_range = 128;
unsigned int g_max_real_timing_apps = 50;
unsigned int g_max_file_sequential_apps = 50;
bool g_skip_background_readahead_enable;
bool g_background_faultaround_disable;
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
module_param_named(lb_file_page_reclaim_supported, g_file_page_reclaim_supported, bool, 0644);
MODULE_PARM_DESC(lb_file_page_reclaim_supported, "precise file-backed page reclaim supported");
module_param_named(lb_real_timing_supported, g_real_timing_supported, bool, 0644);
MODULE_PARM_DESC(lb_real_timing_supported, "real timing app launch boost supported");
module_param_named(lb_max_gap_range, g_max_gap_range, uint, 0644);
MODULE_PARM_DESC(lb_max_gap_range, "max gap range for file range merge");
module_param_named(lb_max_merge_range, g_max_merge_range, uint, 0644);
MODULE_PARM_DESC(lb_max_merge_range, "max merge range for file range merge");
module_param_named(lb_max_real_timing_apps, g_max_real_timing_apps, uint, 0644);
MODULE_PARM_DESC(lb_max_real_timing_apps, "max number of real timing apps");
module_param_named(lb_max_file_sequential_apps, g_max_file_sequential_apps, uint, 0644);
MODULE_PARM_DESC(lb_max_file_sequential_apps, "max number of file sequential apps");
module_param_named(lb_skip_background_readahead_enable, g_skip_background_readahead_enable, bool, 0644);
MODULE_PARM_DESC(lb_skip_background_readahead_enable, "skip background readahead enable or not");
module_param_named(lb_background_faultaround_disable, g_background_faultaround_disable, bool, 0644);
MODULE_PARM_DESC(lb_background_faultaround_disable, "background fault around disable or not");

static struct {
	int major;
	struct class *cls;
	struct device *dev;
	void *unused;
} g_lb_data;

static bool is_real_timing_package(const char *package_name)
{
	if (package_name) {
		if (!strcmp(package_name, "com.tencent.jkchess") ||
		    !strcmp(package_name, "com.eg.android.AlipayGphone") ||
		    !strcmp(package_name, "com.kugou.android") ||
		    !strcmp(package_name, "com.tencent.KiHan") ||
		    !strcmp(package_name, "com.tencent.tmgp.supercell.brawlstars") ||
		    !strcmp(package_name, "com.tencent.msgame"))
			return true;
	}
	return g_real_timing_supported;
}

static inline void lb_chrdev_unregister(void)
{
	if (g_lb_data.dev)
		device_destroy(g_lb_data.cls, MKDEV(g_lb_data.major, 0));
	if (g_lb_data.cls)
		class_destroy(g_lb_data.cls);
	if (g_lb_data.major >= 1)
		__unregister_chrdev(g_lb_data.major, 0, 256, "iorap_dev");
}

static void lb_cold_exit(void)
{
	unregister_trace_android_vh_filemap_read(mi_lb_page_collect_on_readfile, NULL);
	unregister_trace_android_vh_filemap_map_pages_range(mi_lb_page_collect_on_pagefault, NULL);
	unregister_trace_android_vh_do_async_mmap_readahead(mi_lb_skip_async_mmap_readahead, NULL);
	unregister_trace_android_vh_should_fault_around(should_skip_fault_around, NULL);
	unregister_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
	unregister_trace_android_vh_tune_mmap_readaround(adjust_readaround, NULL);
	mi_lb_exit_manager();
	lb_chrdev_unregister();
}

static long __attribute__((no_sanitize("array-bounds"))) mi_lb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct lb_owner owner = { 0 };
	struct cmd_app_info app_info = { 0 };
	struct lb_app_preread_info_size info_size = { 0 };
	void __user *argp = (void __user *)arg;
	struct lb_app_preread_info *info;
	unsigned long buf_size;
	unsigned int version;
	int ret = 0;

	switch (cmd) {
	case 0xc1084c01 + 0:
		if (copy_from_user(&app_info.owner, argp, sizeof(app_info.owner))) {
			LB_E(147, "copy from user fail");
			return -EFAULT;
		}
		app_info.owner.package_name[255] = '\0';
		app_info.is_real_timing = is_real_timing_package(app_info.owner.package_name);
		ret = mi_lb_dealwith_collect_cmd(APP_COLLECT_START, &app_info);
		if (ret)
			LB_E(154, "lb collect cmd fail");
		break;
	case 0xc1084c01 + 18:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(271, "copy from user fail");
			return -EFAULT;
		}
		owner.package_name[255] = '\0';
		ret = mi_lb_dealwith_pte_interrupt_cmd(&owner);
		if (ret)
			LB_E(277, "lb pte interrupt fail");
		break;
	case 0xc1084c01 + 5:
		if (copy_from_user(&app_info.owner, argp, sizeof(app_info.owner))) {
			LB_E(230, "copy from user fail");
			return -EFAULT;
		}
		debug_owner.uid = -1;
		app_info.owner.package_name[255] = '\0';
		app_info.is_real_timing = is_real_timing_package(app_info.owner.package_name);
		ret = mi_lb_dealwith_collect_cmd(APP_LAUNCH_ABORT, &app_info);
		if (ret)
			LB_E(238, "lb collect abort fail");
		break;
	case 0xc1084c01 + 6:
		if (g_only_collect)
			return -1;
		if (copy_from_user(&info_size, argp, sizeof(info_size))) {
			LB_E(161, "copy from user fail");
			return -EFAULT;
		}
		info_size.app_info.owner.package_name[255] = '\0';
		info_size.app_info.is_real_timing = is_real_timing_package(info_size.app_info.owner.package_name);
		ret = mi_lb_dealwith_preread_size_cmd(&info_size);
		if (ret <= -2) {
			LB_E(169, "lb preread size fail");
			return -1;
		}
		if (ret == -1) {
			LB_E(173, "lb app %s cold start", info_size.app_info.owner.package_name);
			return -2;
		}
		if (copy_to_user(argp, &info_size, sizeof(info_size))) {
			LB_E(177, "LB_GET_PREREAD_INFO_SIZE: copy to user fail");
			return -EFAULT;
		}
		break;
	case 0xc1084c01 + 4:
		if (copy_from_user(&app_info.owner, argp, sizeof(app_info.owner))) {
			LB_E(218, "copy from user fail");
			return -EFAULT;
		}
		debug_owner.uid = -1;
		app_info.owner.package_name[255] = '\0';
		app_info.is_real_timing = is_real_timing_package(app_info.owner.package_name);
		ret = mi_lb_dealwith_collect_cmd(APP_LAUNCH_FINISH, &app_info);
		if (ret)
			LB_E(226, "lb collect finish fail");
		break;
	case 0xc1084c01 + 2:
		if (copy_from_user(&app_info.owner, argp, sizeof(app_info.owner))) {
			LB_E(242, "copy from user fail");
			return -EFAULT;
		}
		app_info.owner.package_name[255] = '\0';
		app_info.is_real_timing = is_real_timing_package(app_info.owner.package_name);
		mi_lb_dealwith_clear_cmd(&app_info);
		ret = 0;
		break;
	case 0xc1084c01 + 7:
		if (get_user(buf_size, (unsigned long __user *)(arg + 0x110))) {
			LB_E(184, "get_user fail");
			return -EFAULT;
		}
		if (buf_size > 0xa00000) {
			LB_E(188, "buf_size %lu too large", buf_size);
			return -EINVAL;
		}
		info = kvmalloc(buf_size, GFP_KERNEL);
		if (!info) {
			LB_E(193, "alloc app_preread_info err");
			return -ENOMEM;
		}
		if (copy_from_user(info, argp, sizeof(*info))) {
			LB_E(197, "copy from user fail");
			kvfree(info);
			return -EFAULT;
		}
		info->app_info.owner.package_name[255] = '\0';
		info->app_info.is_real_timing = is_real_timing_package(info->app_info.owner.package_name);
		if (mi_lb_dealwith_preread_info_cmd(info)) {
			LB_E(206, "lb pre read fail");
			kvfree(info);
			return -1;
		}
		if (copy_to_user(argp, info, info->preread_info_buf_size)) {
			LB_E(211, "LB_GET_PREREAD_INFO_SIZE: copy to user fail");
			ret = -EFAULT;
		} else {
			ret = 0;
		}
		kvfree(info);
		break;
	case 0xc1084c01 + 16:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(261, "copy from user fail");
			return -EFAULT;
		}
		owner.package_name[255] = '\0';
		ret = mi_lb_dealwith_pte_preread_cmd(&owner);
		if (ret)
			LB_E(267, "lb pte preread fail");
		break;
	case 0xc1084c01 + 15:
		if (copy_from_user(&owner, argp, sizeof(owner))) {
			LB_E(251, "copy from user fail");
			return -EFAULT;
		}
		owner.package_name[255] = '\0';
		ret = mi_lb_dealwith_pte_snapshot_cmd(&owner);
		if (ret)
			LB_E(257, "lb pte snapshot fail");
		break;
	case 0x80044c14:
		version = 0x30000;
		if (copy_to_user(argp, &version, sizeof(version))) {
			LB_E(284, "LB_GET_VERSION: copy to user fail");
			return -EFAULT;
		}
		LB_I(288, "iorap version %u.%u.%u", 3, 0, 0);
		break;
	default:
		LB_E(292, "unknown cmd, cmd is %u", cmd);
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

	memset(&g_lb_data, 0, sizeof(g_lb_data));	/* stock zeroes the state at the start of the chrdev registration */
	LB_D(313, "enter");
	g_lb_data.major = __register_chrdev(0, 0, 256, "iorap_dev", &lb_fops);
	if (g_lb_data.major < 0) {
		LB_E(317, "register chrdev fail\n");
		return -ENXIO;
	}
	g_lb_data.cls = cls = class_create("iorap");
	if (IS_ERR_OR_NULL(cls)) {
		LB_E(322, "class create fail");
		err = PTR_ERR(g_lb_data.cls);
		goto unreg;
	}
	g_lb_data.dev = device_create(cls, NULL, MKDEV(g_lb_data.major, 0), NULL, "iorap_dev");
	if (IS_ERR_OR_NULL(g_lb_data.dev)) {
		LB_E(330, "device create fail");
		class_destroy(g_lb_data.cls);
		err = PTR_ERR(g_lb_data.dev);
		goto unreg;
	}
	dev_set_drvdata(g_lb_data.dev, &g_lb_data);
	LB_D(339, "exit");
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
		LB_E(92, "register hook mi_lb_page_collect_on_readfile failed");
		return ret;
	}
	ret = register_trace_android_vh_filemap_map_pages_range(mi_lb_page_collect_on_pagefault, NULL);
	if (ret) {
		LB_E(99, "register hook mi_lb_page_collect_on_pagefault failed");
		unregister_trace_android_vh_filemap_read(mi_lb_page_collect_on_readfile, NULL);
		return ret;
	}
	ret = register_trace_android_vh_do_async_mmap_readahead(mi_lb_skip_async_mmap_readahead, NULL);
	if (ret)
		LB_E(106, "register hook mi_lb_page_collect_on_pagefault failed");
	ret = register_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
	if (ret)
		LB_E(111, "register_trace_android_vh_ra_tuning_max_page failed! ret=%d\n", ret);
	ret = register_trace_android_vh_tune_mmap_readaround(adjust_readaround, NULL);
	if (ret)
		LB_E(116, "register_trace_android_vh_tune_mmap_readaround failed! ret=%d\n", ret);
	ret = register_trace_android_vh_should_fault_around(should_skip_fault_around, NULL);
	if (ret)
		LB_E(121, "register_trace_android_vh_should_fault_around failed! ret=%d\n", ret);
	return 0;
}

static int __init mi_lb_init(void)
{
	int ret;

	ret = mi_lb_chrdev_register();
	if (!ret) {
		ret = mi_lb_init_manager();
		if (!ret) {
			ret = mi_lb_register_hooks();
			if (ret)
				mi_lb_exit_manager();
		}
		if (ret)
			lb_chrdev_unregister();
	}
	if (ret) {
		LB_E(392, "lb_cold_init failed\n");
		return ret;
	}
	ret = lb_hot_init();
	if (ret) {
		LB_E(397, "lb_cold_init failed\n");
		lb_cold_exit();
		return ret;
	}
	ret = mi_lb_dbg_init();
	if (ret) {
		LB_E(403, "dbg init failed\n");
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
