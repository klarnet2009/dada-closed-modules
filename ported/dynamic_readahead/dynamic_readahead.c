// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 * Reconstructed from the HyperOS 4.0 stock binary; base: XRING mi_dynamic_readahead.
 */
#define pr_fmt(fmt) "dynamic_readahead: " fmt
#include <linux/types.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/mm_types.h>
#include <linux/mmzone.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/vmstat.h>
#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/bitops.h>
#include <trace/hooks/mm.h>
#include <linux/cgroup.h>

#define FA_UID_BASE	10000
#define FA_APPID_MAX	512
#define EROFS_SUPER_MAGIC_V1	0xE0F5E1E2

static unsigned long long high_wm = 0;
/* true by default, false when dynamic_readahead=N in cmdline */
static bool enable = true;
static bool ra_close_large_folio_enable = true;
static bool dynamic_fault_around_inited;
static DECLARE_BITMAP(fault_around_bitmap, FA_APPID_MAX);

static struct zone *next_zone_(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;
	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else
		zone = NULL;
	return zone;
}

#define for_each_zone_(zone)			        \
	for (zone = (NODE_DATA(numa_node_id()))->node_zones; \
	     zone;					\
	     zone = next_zone_(zone))

static inline bool is_lowmem(void)
{
	return global_zone_page_state(NR_FREE_PAGES) < high_wm;
}

static bool current_is_background(void)
{
	return !strncmp(task_css(current, cpuset_cgrp_id)->cgroup->kn->name, "background", 11);
}

static void adjust_readahead(void *ignore, struct readahead_control *ractl,
			     unsigned long *max_pages)
{
	struct file_ra_state *ra = ractl->ra;

	if (enable && current_is_background() && !rt_task(current) && is_lowmem())
		*max_pages = min_t(long, *max_pages, ra->ra_pages / 2);
}

static void adjust_readaround(void *ignore, unsigned int ra_pages, pgoff_t pgoff,
			      pgoff_t *start, unsigned int *size, unsigned int *async_size)
{
	unsigned int dy_ra_pages = ra_pages / 2;

	if (enable && current_is_background() && !rt_task(current) && is_lowmem()) {
		*start = max_t(long, 0, pgoff - dy_ra_pages / 2);
		*size = dy_ra_pages;
		*async_size = dy_ra_pages / 4;
	}
}

static void ra_close_lf_page_cache_ra_order_bypass(void *ignore, struct readahead_control *ractl,
						   struct file_ra_state *ra, int new_order,
						   gfp_t *gfp, bool *bypass)
{
	if (ractl && ra_close_large_folio_enable && ractl->mapping &&
	    ractl->mapping->host && ractl->mapping->host->i_sb &&
	    ractl->mapping->host->i_sb->s_magic == EROFS_SUPER_MAGIC_V1)
		*bypass = true;
}

static void should_fault_around(void *ignore, struct vm_fault *vmf, bool *should_around)
{
	unsigned int uid = __kuid_val(current_uid());

	if (uid < FA_UID_BASE) {
		*should_around = false;
		return;
	}
	uid %= FA_UID_BASE;
	if (uid < FA_APPID_MAX)
		*should_around = !test_bit(uid, fault_around_bitmap);
	else
		*should_around = true;
}

static int fault_around_set(const char *val, const struct kernel_param *kp)
{
	unsigned int uid = 0, appid;
	int ret = kstrtouint(val, 10, &uid);

	if (ret)
		return ret;
	if (uid == 0) {
		int i;

		for (i = 0; i < FA_APPID_MAX; i++)
			clear_bit(i, fault_around_bitmap);
		return 0;
	}
	if (uid >= FA_UID_BASE) {
		appid = uid % FA_UID_BASE;
		if (appid >= FA_APPID_MAX)
			return -ENOSPC;
		set_bit(appid, fault_around_bitmap);
		return 0;
	}
	return -EINVAL;
}

static int fault_around_get(char *buffer, const struct kernel_param *kp)
{
	int i, len = 0;

	for (i = 0; i < FA_APPID_MAX; i++)
		if (test_bit(i, fault_around_bitmap))
			len += scnprintf(buffer + len, PAGE_SIZE - len, "%u\n", i + FA_UID_BASE);
	return len;
}

static int register_dymamic_readahead_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_ra_tuning_max_page failed! ret=%d\n", ret);
		return ret;
	}
	ret = register_trace_android_vh_tune_mmap_readaround(adjust_readaround, NULL);
	if (ret != 0) {
		unregister_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
		pr_err("register_trace_android_vh_tune_mmap_readaround failed! ret=%d\n", ret);
		return ret;
	}
	ret = register_trace_android_vh_page_cache_ra_order_bypass(ra_close_lf_page_cache_ra_order_bypass, NULL);
	if (ret != 0) {
		unregister_trace_android_vh_tune_mmap_readaround(adjust_readaround, NULL);
		unregister_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
		pr_err("register hook trace_android_vh_page_cache_ra_order_bypass failed! ret=%d\n", ret);
		return ret;
	}
	ret = register_trace_android_vh_should_fault_around(should_fault_around, NULL);
	if (ret != 0)
		pr_err("register_trace_android_vh_should_fault_around failed: %d\n", ret);
	else
		dynamic_fault_around_inited = true;
	return 0;
}

static void unregister_dymamic_readahead_hooks(void)
{
	if (dynamic_fault_around_inited == true) {
		unregister_trace_android_vh_should_fault_around(should_fault_around, NULL);
		dynamic_fault_around_inited = false;
	}
	unregister_trace_android_vh_ra_tuning_max_page(adjust_readahead, NULL);
	unregister_trace_android_vh_tune_mmap_readaround(adjust_readaround, NULL);
	unregister_trace_android_vh_page_cache_ra_order_bypass(ra_close_lf_page_cache_ra_order_bypass, NULL);
}

static int __init dynamic_readahead_init(void)
{
	struct zone *zone;
	int ret;

	ret = register_dymamic_readahead_hooks();
	if (ret != 0)
		return ret;
	for_each_zone_(zone) {
		high_wm += high_wmark_pages(zone);
	}
	pr_info("dynamic_readahead_init high_wm: %llu\n", high_wm);
	return 0;
}

static void __exit dynamic_readahead_exit(void)
{
	unregister_dymamic_readahead_hooks();
}

module_init(dynamic_readahead_init);
module_exit(dynamic_readahead_exit);

module_param(enable, bool, 0600);
MODULE_PARM_DESC(enable, "dynamic_readahead.enable=1 to indicate supporting dynamic readahead or not ");
module_param(ra_close_large_folio_enable, bool, 0600);
MODULE_PARM_DESC(ra_close_large_folio_enable, "ra_close_large_folio_enable = 1 to indicate supporting ra_close_large_folio");
module_param_call(fault_around, fault_around_set, fault_around_get, NULL, 0660);
MODULE_PARM_DESC(fault_around, "write uid to disable fault_around for that app, write 0 to clear all, tracked appid range: [0,512)");
MODULE_LICENSE("GPL v2");
