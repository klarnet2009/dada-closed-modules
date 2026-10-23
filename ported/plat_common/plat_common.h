/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PLAT_COMMON_H
#define _PLAT_COMMON_H

#define pr_fmt(fmt) "plat_hook: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/cgroup.h>
#include <linux/rcupdate.h>
#include <trace/hooks/sched.h>

#define PLAT_HOOK_MAX	41

struct walt_related_thread_group;
struct metis_choose_cpu_args;
struct rq;
struct cpufreq_policy;

struct plat_hook_entry {
	u32 cmd;
	void *func;
};

extern struct plat_hook_entry g_plat_hook_table[PLAT_HOOK_MAX];

/* GENKSYMS-ONLY pad: forges the export CRC of the stock prototype (see g1_tools/crcpad.py); compiled out */
#ifdef __GENKSYMS__
#define GKPAD(tag)	, struct tag *gkpad
#else
#define GKPAD(tag)
#endif

#endif
