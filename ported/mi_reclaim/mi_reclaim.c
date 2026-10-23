// SPDX-License-Identifier: GPL-2.0
/*
 * mi_reclaim: Xiaomi "Memory reclaim optimization module" (staging).
 * Reconstructed from the HyperOS 4.0 stock module (no public source), PARTIAL: all 30
 * functions are written from the disassembly; 27 are byte-identical to the stock and the
 * other 3 (event_store, is_important_process, init_module) differ only in code generation
 * (independent review 20.10.2026, see STATUS.md). NOT hardware-tested. DO NOT LOAD before
 * the checklist in STATUS.md.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/vmstat.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>
#include <linux/cpumask.h>
#include <linux/ktime.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/memcontrol.h>
#include <linux/gfp.h>
#include <linux/moduleparam.h>
#include <linux/arch_topology.h>
#include <linux/cpumask.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>
#include <trace/hooks/sched.h>

#define MAX_IMPORTANT_PIDS	32

enum important_proc_caller {
	IMP_CALLER_FILE_SCAN = 0,
	IMP_CALLER_SHRINK_SLAB = 1,
	IMP_CALLER_POOL = 2,
	IMP_CALLER_WMARK_BOOST = 3,
	IMP_CALLER_MAX = 4,
};

struct caller_cnt { atomic64_t v[9]; };
struct caller_adj { atomic64_t v[4]; };

/* the one global object (1008 bytes); offsets are those of the stock g_reclaim */
struct greclaim {
	struct kobject *kobj;				/* 0x000 */
	u32 enable_flags;				/* 0x008 */
	u32 efficiency_flags;				/* 0x00c */
	struct mutex reclaim_lock;			/* 0x010 */
	int event;					/* 0x040 */
	u8 pad044[4];
	unsigned long total_reclaim;			/* 0x048 */
	unsigned long reclaim_times;			/* 0x050 */
	unsigned long total_reclaim_time;		/* 0x058 */
	unsigned long last_reclaim_time;		/* 0x060 */
	spinlock_t pool_lock;				/* 0x068 */
	u8 pad06c[4];
	struct list_head pool_list;			/* 0x070 */
	atomic_t pool_count;				/* 0x080 */
	int pool_high;					/* 0x084 */
	int pool_low;					/* 0x088 */
	int pool_critical;				/* 0x08c */
	struct task_struct *refill_task;		/* 0x090 */
	bool refill_flag;				/* 0x098 */
	bool refill_enabled;				/* 0x099 */
	u8 pad09a[2];
	spinlock_t refill_lock;				/* 0x09c */
	u64 refill_start_ms;				/* 0x0a0 */
	int refill_count;				/* 0x0a8 */
	int refill_history;				/* 0x0ac */
	unsigned long refill_interval;			/* 0x0b0 */
	unsigned long refill_max_runtime;		/* 0x0b8 */
	unsigned long refill_prio_duration;		/* 0x0c0 */
	u32 refill_needed;				/* 0x0c8 */
	int boost_pages;				/* 0x0cc */
	u32 boost_native_pages;			/* 0x0d0 */
	int wmark_file_threshold;			/* 0x0d4 */
	u32 fg_file_scan_threshold;			/* 0x0d8 */
	int throttle_file_threshold;			/* 0x0dc */
	u64 pool_alloc_success;				/* 0x0e0 */
	u64 pool_alloc_fail;				/* 0x0e8 */
	u64 pool_alloc_skip;				/* 0x0f0 */
	u32 refill_wakeup_count;			/* 0x0f8 */
	u32 refill_success_count;			/* 0x0fc */
	u32 refill_fail_count;				/* 0x100 */
	u32 refill_timeout_count;			/* 0x104 */
	u32 refill_interval_skip;			/* 0x108 */
	u32 refill_urgent_wakeup;			/* 0x10c */
	u64 refill_total_time;				/* 0x110 */
	u64 refill_total_count;				/* 0x118 */
	u64 stats120[(0x1a0 - 0x120) / 8];		/* 0x120 .. 0x19f: skip-throttle / file-scan / wmark stats */
	int important_pids[MAX_IMPORTANT_PIDS];		/* 0x1a0 */
	int important_count;				/* 0x220 */
	spinlock_t important_lock;			/* 0x224 */
	u64 stats228[(0x250 - 0x228) / 8];		/* 0x228 */
	struct caller_cnt cnt[IMP_CALLER_MAX];		/* 0x250: per-caller proc stats, 72 bytes each */
	struct caller_adj adj[IMP_CALLER_MAX];		/* 0x370: per-caller adj stats, 32 bytes each */
};

static struct greclaim g_reclaim;
static char *refill_cpus;
u32 mi_reclaim_debug_flags;
u32 mi_reclaim_stats_flags;

extern void kern_qi_set_qos(struct task_struct *p, int qos);
extern bool is_qos_task(struct task_struct *p);

static noinline __used void tracing_mark_write(char *buf)
{
	trace_puts(buf);
}

#define MI_ATRACE_BEGIN(name) do {						\
	char __b[128] = {};							\
	snprintf(__b, sizeof(__b), "B|%d|%s\n", current->tgid, name);		\
	tracing_mark_write(__b);						\
} while (0)
#define MI_ATRACE_END() do {							\
	char __b[32] = {};							\
	snprintf(__b, sizeof(__b), "E|%d\n", current->tgid);			\
	tracing_mark_write(__b);						\
} while (0)

/* DONE */
static ssize_t event_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", READ_ONCE(g_reclaim.event));
}

static noinline __used void mi_task_clear_bit(struct task_struct *p, int bit)
{
	unsigned long *w = (unsigned long *)((char *)p + 0xdd8);
	unsigned long old;

	do {
		old = READ_ONCE(*w);
	} while (cmpxchg(w, old, old & ~1UL) != old);
}

static noinline __used void pool_add_page(struct page *page)
{
	unsigned long flags;

	spin_lock_irqsave(&g_reclaim.pool_lock, flags);
	list_add(&page->lru, &g_reclaim.pool_list);
	atomic_inc(&g_reclaim.pool_count);
	mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, 1);
	spin_unlock_irqrestore(&g_reclaim.pool_lock, flags);
}

static noinline __used struct page *pool_remove_page(struct zone *req_zone)
{
	struct page *page = NULL;
	unsigned long flags;

	if (zone_idx(req_zone) < 1)
		return NULL;
	spin_lock_irqsave(&g_reclaim.pool_lock, flags);
	if (!list_empty(&g_reclaim.pool_list)) {
		page = list_first_entry(&g_reclaim.pool_list, struct page, lru);
		list_del(&page->lru);
		atomic_dec(&g_reclaim.pool_count);
		mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, -1);
	}
	spin_unlock_irqrestore(&g_reclaim.pool_lock, flags);
	return page;
}

static noinline void mi_reclaim_cleanup(void)
{
	LIST_HEAD(tmp);
	struct page *page, *next;
	unsigned long flags;
	int count;

	spin_lock_irqsave(&g_reclaim.pool_lock, flags);
	list_splice_init(&g_reclaim.pool_list, &tmp);
	count = atomic_read(&g_reclaim.pool_count);
	atomic_set(&g_reclaim.pool_count, 0);
	spin_unlock_irqrestore(&g_reclaim.pool_lock, flags);
	list_for_each_entry_safe(page, next, &tmp, lru) {
		mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, -1);
		__free_pages(page, 0);
	}
	if (mi_reclaim_debug_flags)
		pr_info("mi_reclaim: cleanup freed %d pages\n", count);
}

static inline atomic64_t *gstat(unsigned int off)
{
	return (atomic64_t *)((char *)&g_reclaim + off);
}

/* the statistics counters are atomic: the stock zeroes them with individual (volatile) stores */
#define SA64(off)	atomic64_set((atomic64_t *)(s + (off)), 0)
#define SA32(off)	atomic_set((atomic_t *)(s + (off)), 0)
static noinline __used void mi_reset_stats(void)
{
	char *s = (char *)&g_reclaim.total_reclaim;	/* g + 0x48 */
	unsigned int i;

	SA64(0x98 - 0x0);
	SA64(0xa0);
	SA64(0xa8);
	SA32(0xb0);
	SA32(0xb4);
	SA32(0xb8);
	SA32(0xbc);
#pragma clang loop unroll(full)
	for (i = 0xd8; i <= 0x118; i += 8)
		SA64(i);
	SA64(0xc8);
	SA64(0xd0);
#pragma clang loop unroll(full)
	for (i = 0x120; i <= 0x148; i += 8)
		SA64(i);
	SA32(0xc0);
	SA32(0xc4);
#pragma clang loop unroll(full)
	for (i = 0x1e0; i <= 0x3a0; i += 8)
		SA64(i);
	g_reclaim.total_reclaim = 0;
	g_reclaim.reclaim_times = 0;
	g_reclaim.total_reclaim_time = 0;
	g_reclaim.last_reclaim_time = 0;
}

static int add_important_pid(int pid)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&g_reclaim.important_lock, flags);
	for (i = 0; i < g_reclaim.important_count; i++) {
		if (g_reclaim.important_pids[i] == pid) {
			spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
			return 0;
		}
	}
	if (g_reclaim.important_count >= MAX_IMPORTANT_PIDS) {
		spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
		return -ENOSPC;
	}
	g_reclaim.important_pids[g_reclaim.important_count++] = pid;
	spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
	if (mi_reclaim_debug_flags)
		pr_info("mi_reclaim: added pid %d to important list (count=%d)\n",
			pid, g_reclaim.important_count);
	return 0;
}

static int remove_important_pid(int pid)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&g_reclaim.important_lock, flags);
	for (i = 0; i < g_reclaim.important_count; i++) {
		if (g_reclaim.important_pids[i] != pid)
			continue;
		for (j = i; j < g_reclaim.important_count - 1; j++)
			g_reclaim.important_pids[j] = g_reclaim.important_pids[j + 1];
		g_reclaim.important_count--;
		spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
		if (mi_reclaim_debug_flags)
			pr_info("mi_reclaim: removed pid %d from important list (count=%d)\n",
				pid, g_reclaim.important_count);
		return 0;
	}
	spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
	return -ENOENT;
}

static ssize_t pid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned long flags;
	int len, i;

	len = snprintf(buf, PAGE_SIZE, "important_pid_count: %d\n", g_reclaim.important_count);
	len += snprintf(buf + len, PAGE_SIZE - len, "important_pids: ");
	spin_lock_irqsave(&g_reclaim.important_lock, flags);
	for (i = 0; i < g_reclaim.important_count; i++)
		len += snprintf(buf + len, PAGE_SIZE - len, "%d ", g_reclaim.important_pids[i]);
	spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}

static ssize_t pid_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	char cmd[32] = {};
	int pid = 0, ret;
	unsigned long flags;

	if (sscanf(buf, "%31s", cmd) < 1)
		return -EINVAL;
	if (!strcmp(cmd, "clear")) {
		spin_lock_irqsave(&g_reclaim.important_lock, flags);
		g_reclaim.important_count = 0;
		spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
		if (mi_reclaim_debug_flags)
			pr_info("mi_reclaim: cleared important pid list\n");
		return count;
	}
	if (cmd[0] == '+') {
		if (kstrtoint(cmd + 1, 10, &pid))
			return -EINVAL;
		ret = add_important_pid(pid);
	} else if (cmd[0] == '-') {
		if (kstrtoint(cmd + 1, 10, &pid))
			return -EINVAL;
		ret = remove_important_pid(pid);
	} else {
		if (kstrtoint(cmd, 10, &pid))
			return -EINVAL;
		ret = add_important_pid(pid);
	}
	return ret ? ret : count;
}

static bool pid_is_important(int pid)
{
	unsigned long flags;
	int i, n;

	spin_lock_irqsave(&g_reclaim.important_lock, flags);
	n = g_reclaim.important_count;
	for (i = 0; i < n; i++)
		if (g_reclaim.important_pids[i] == pid)
			break;
	spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
	return i < n;
}

static void mi_free_task(void *data, struct task_struct *p)
{
	int tgid;

	if (p && p->group_leader == p) {
		tgid = p->tgid;
		if (pid_is_important(tgid)) {
			remove_important_pid(tgid);
		if (mi_reclaim_debug_flags)
			pr_info("mi_reclaim: removed exited tgid %d from important list\n", tgid);
		}
	}
}

/* ---- module logic (written from the OS4 stock disassembly; no longer placeholders) ---- */

static bool has_sufficient_free_memory(void);

static int refill_worker_fn(void *unused)
{
	struct cpumask mask = {};
	unsigned long flags;
	int ret;

	if (refill_cpus && *refill_cpus) {
		if (!cpulist_parse(refill_cpus, &mask) && !cpumask_empty(&mask))
			goto set_affinity;
		pr_warn("mi_reclaim: invalid refill_cpus '%s', using auto-detect\n", refill_cpus);
	}
	{
		unsigned long min = ULONG_MAX;
		int best = -1, cpu;

		unsigned long on = cpu_online_mask->bits[0] & 0xffffffffUL; /* hack: operand order of the stock ands */
		cpumask_clear(&mask);
		for_each_cpu(cpu, cpu_online_mask) {
			unsigned long cap = arch_scale_cpu_capacity(cpu);

			if (cap < min) {
				min = cap;
				best = cpu;
			}
		}
		if (best < 0 || !(mask.bits[0] = (topology_cluster_cpumask(best)->bits[0] & on)))
			cpumask_copy(&mask, cpu_online_mask);
		pr_info("mi_reclaim: refill worker CPUs: %*pbl\n", cpumask_pr_args(&mask));
	}
set_affinity:
	ret = set_cpus_allowed_ptr(current, &mask);
	if (ret)
		pr_warn("mi_reclaim: failed to set refill worker CPU affinity: %d\n", ret);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		if (!READ_ONCE(g_reclaim.refill_flag)) {
			schedule();
		} else {
			ktime_t start;
			s64 elapsed;
			int n;

			set_current_state(TASK_RUNNING);
			kern_qi_set_qos(current, 0x26);
			g_reclaim.refill_start_ms = ktime_get() / 1000000;
			WRITE_ONCE(g_reclaim.refill_count, 0);
			start = ktime_get();
			while (atomic_read(&g_reclaim.pool_count) < g_reclaim.pool_high) {
				bool ok;
				ktime_t t0, t1;

				if (g_reclaim.refill_max_runtime) {
					elapsed = (ktime_get() - start) / 1000000;
					if (elapsed >= g_reclaim.refill_max_runtime) {
						if (mi_reclaim_debug_flags)
							pr_info("mi_reclaim: refill: max runtime %lu ms exceeded, stopping\n",
								g_reclaim.refill_max_runtime);
						if (mi_reclaim_stats_flags & BIT(0))
							atomic_inc((atomic_t *)gstat(0x104));
						break;
					}
				}
				ok = has_sufficient_free_memory();
				if (!ok)
					g_reclaim.refill_needed = 1;
				t0 = ktime_get();
				n = 0;
				while (atomic_read(&g_reclaim.pool_count) < g_reclaim.pool_high) {
					struct page *page;

					(void)raw_smp_processor_id();
					page = __alloc_pages(0x112dc2, 0, 0, NULL);
					if (!page) {
						if (mi_reclaim_stats_flags & BIT(0))
							atomic_inc((atomic_t *)gstat(0x100));
						break;
					}
					pool_add_page(page);
					if (mi_reclaim_stats_flags & BIT(0))
						atomic_inc((atomic_t *)gstat(0xfc));
					if (++n == 512)
						break;
				}
				t1 = ktime_get();
				if (!ok)
					g_reclaim.refill_needed = 0;
				if (!n)
					break;
				if (mi_reclaim_stats_flags & BIT(0)) {
					atomic64_add((t1 - t0) / 1000000, gstat(0x110));
					atomic64_inc(gstat(0x118));
				}
				g_reclaim.refill_count += n;
				g_reclaim.refill_history += n;
			}
			spin_lock_irqsave(&g_reclaim.refill_lock, flags);
			g_reclaim.refill_flag = false;
			spin_unlock_irqrestore(&g_reclaim.refill_lock, flags);
		}
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	return 0;
}

static ssize_t info_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len = 0;
	u64 total_time, count, avg;

	len = snprintf(buf, PAGE_SIZE, "enable_flags: 0x%x (bit0=global_reclaim, bit1=efficiency)\n", g_reclaim.enable_flags);
	len += snprintf(buf + len, PAGE_SIZE - len, "efficiency_flags: 0x%x (bit0=pool, bit1=skip_throttle, bit2=file_scan, bit3=wmark_boost, bit4=clean_page)\n", g_reclaim.efficiency_flags);
	len += snprintf(buf + len, PAGE_SIZE - len, "debug_flags: 0x%x (bit0=global, bit1=efficiency)\n", mi_reclaim_debug_flags);
	len += snprintf(buf + len, PAGE_SIZE - len, "stats_flags: 0x%x (bit0=pool, bit1=skip_throttle, bit2=file_scan, bit3=wmark_boost, bit4=global, bit5=clean_page, bit6=important_proc, 0xff=all)\n", mi_reclaim_stats_flags);
	if (mi_reclaim_stats_flags & BIT(0)) {
		total_time = atomic64_read(gstat(0x110));
		count = atomic64_read(gstat(0x118));
		avg = count ? total_time / count : 0;
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [Pool Statistics]    |\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_enabled: %d\n", g_reclaim.efficiency_flags & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_enabled: %d\n", g_reclaim.refill_enabled);
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_count: %d\n", atomic_read(&g_reclaim.pool_count));
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_high_mark: %d\n", g_reclaim.pool_high);
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_low_mark: %d\n", g_reclaim.pool_low);
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_critical_mark: %d\n", g_reclaim.pool_critical);
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_alloc_success: %lld\n", atomic64_read(gstat(0xe0)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_alloc_fail: %lld\n", atomic64_read(gstat(0xe8)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| pool_alloc_skip: %lld\n", atomic64_read(gstat(0xf0)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_wakeup_count: %d\n", atomic_read((atomic_t *)gstat(0xf8)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_success_count: %d\n", atomic_read((atomic_t *)gstat(0xfc)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_fail_count: %d\n", atomic_read((atomic_t *)gstat(0x100)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_timeout_count: %d\n", atomic_read((atomic_t *)gstat(0x104)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_count: %d\n", g_reclaim.refill_count);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_history: %d\n", g_reclaim.refill_history);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_total_time: %llu ms\n", total_time);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_total_count: %llu\n", count);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_avg_time: %llu ms\n", avg);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_max_runtime: %lu ms\n", g_reclaim.refill_max_runtime);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_interval: %lu ms\n", g_reclaim.refill_interval);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_prio_duration: %lu ms\n", g_reclaim.refill_prio_duration);
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_interval_skip: %d\n", atomic_read((atomic_t *)gstat(0x108)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| refill_urgent_wakeup: %d\n", atomic_read((atomic_t *)gstat(0x10c)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
	}
	if (mi_reclaim_stats_flags & BIT(1)) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+---------------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [Skip Throttle Statistics]|\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+---------------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| skip_throttle_enabled: %d\n", (g_reclaim.efficiency_flags >> 1) & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| rescue_total: %lld\n", atomic64_read(gstat(0x120)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| rescue_skip_low_file: %lld\n", atomic64_read(gstat(0x128)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| kswapd_shrink_slab: %lld\n", atomic64_read(gstat(0x130)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+---------------------------+\n");
	}
	if (mi_reclaim_stats_flags & BIT(2)) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [File Scan Stats]    |\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| file_scan_enabled: %d\n", (g_reclaim.efficiency_flags >> 2) & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| fg_file_scan_thresh: %d\n", g_reclaim.fg_file_scan_threshold);
		len += snprintf(buf + len, PAGE_SIZE - len, "| throttle_file_thresh: %d\n", g_reclaim.throttle_file_threshold);
		len += snprintf(buf + len, PAGE_SIZE - len, "| file_scan_count: %lld\n", atomic64_read(gstat(0x168)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| skip_low_file: %lld\n", atomic64_read(gstat(0x170)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
	}
	if (mi_reclaim_stats_flags & BIT(3)) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [Wmark Boost Stats]  |\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| wmark_boost_enabled: %d\n", (g_reclaim.efficiency_flags >> 3) & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_pages: %d\n", g_reclaim.boost_pages);
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_native_pages: %d\n", g_reclaim.boost_native_pages);
		len += snprintf(buf + len, PAGE_SIZE - len, "| file_threshold: %d\n", g_reclaim.wmark_file_threshold);
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_count: %lld\n", atomic64_read(gstat(0x138)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_native: %lld\n", atomic64_read(gstat(0x140)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_background: %lld\n", atomic64_read(gstat(0x148)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_skip_once: %lld\n", atomic64_read(gstat(0x150)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_set_once: %lld\n", atomic64_read(gstat(0x158)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| boost_skip_reserves: %lld\n", atomic64_read(gstat(0x160)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
	}
	if (mi_reclaim_stats_flags & BIT(4)) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [Global Reclaim Stats]|\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| global_enabled: %d\n", g_reclaim.enable_flags & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| total_reclaim: %lu pages\n", g_reclaim.total_reclaim);
		len += snprintf(buf + len, PAGE_SIZE - len, "| reclaim_times: %lu\n", g_reclaim.reclaim_times);
		len += snprintf(buf + len, PAGE_SIZE - len, "| total_reclaim_time: %lu ms\n", g_reclaim.total_reclaim_time);
		len += snprintf(buf + len, PAGE_SIZE - len, "| last_reclaim_time: %lu ms\n", g_reclaim.last_reclaim_time);
		len += snprintf(buf + len, PAGE_SIZE - len, "| avg_reclaim_time: %lu ms\n", g_reclaim.reclaim_times ? g_reclaim.total_reclaim_time / g_reclaim.reclaim_times : 0);
		len += snprintf(buf + len, PAGE_SIZE - len, "| global_scan_file: %lld\n", atomic64_read(gstat(0x178)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| global_scan_anon: %lld\n", atomic64_read(gstat(0x180)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| global_scan_no: %lld\n", atomic64_read(gstat(0x188)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| global_scan_anon_and_file: %lld\n", atomic64_read(gstat(0x190)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| reclaim_task_skip: %lld\n", atomic64_read(gstat(0x198)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
	}
	if (mi_reclaim_stats_flags & BIT(5)) {
		len += snprintf(buf + len, PAGE_SIZE - len, "\n+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| [Clean Page Stats]   |\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
		len += snprintf(buf + len, PAGE_SIZE - len, "| clean_page_enabled: %d\n", (g_reclaim.efficiency_flags >> 4) & 1);
		len += snprintf(buf + len, PAGE_SIZE - len, "| set_reclaim: %lld\n", atomic64_read(gstat(0x228)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| skipped_anon: %lld\n", atomic64_read(gstat(0x230)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| skipped_large: %lld\n", atomic64_read(gstat(0x238)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| skipped_existing: %lld\n", atomic64_read(gstat(0x240)));
		len += snprintf(buf + len, PAGE_SIZE - len, "| skipped_not_reg: %lld\n", atomic64_read(gstat(0x248)));
		len += snprintf(buf + len, PAGE_SIZE - len, "+----------------------+\n");
	}
	return len;
}

static bool has_sufficient_free_memory(void);

static ssize_t info_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	char cmd[32] = {};
	unsigned int val = 0;
	unsigned int t;

	if (sscanf(buf, "%31s %u", cmd, &val) < 1)
		return -EINVAL;

	if (!strcmp(cmd, "enable")) {
		u32 old = g_reclaim.efficiency_flags;

		t = -(int)((val >> 1) & 1);
		val &= 3;
		g_reclaim.enable_flags = val;
		g_reclaim.efficiency_flags = (val & 2) ? 3 : 0;
		if (old & 1)
			goto pool_off;
	} else if (!strcmp(cmd, "efficiency")) {
		u32 old = g_reclaim.efficiency_flags;
		u32 raw = val;

		val &= 0x1f;
		g_reclaim.efficiency_flags = val;
		if (old & 1) {
			t = raw;
pool_off:
			if (!(t & 1))
				mi_reclaim_cleanup();
		}
	} else if (!strcmp(cmd, "debug")) {
		mi_reclaim_debug_flags = val & 3;
	} else if (!strcmp(cmd, "stats")) {
		mi_reclaim_stats_flags = val & 0x7f;
	} else if (!strcmp(cmd, "prefill")) {
		bool ok = has_sufficient_free_memory();
		int need = g_reclaim.pool_high - atomic_read(&g_reclaim.pool_count);
		bool boost = !ok;
		int n;

		if (need < 1)
			return count;
		if (!ok)
			g_reclaim.refill_needed = 1;
		for (n = 0; n < need; n++) {
			struct page *page;

			(void)raw_smp_processor_id();
			page = __alloc_pages(0x112dc2, 0, 0, NULL);
			if (!page)
				break;
			pool_add_page(page);
		}
		if (!ok)
			g_reclaim.refill_needed = 0;
		if (mi_reclaim_debug_flags)
			pr_info("mi_reclaim: prefilled %d pages, total %d (boost=%d)\n", n,
				atomic_read(&g_reclaim.pool_count), boost);
	} else if (!strcmp(cmd, "cleanup")) {
		mi_reclaim_cleanup();
	} else if (!strcmp(cmd, "watermark_boost") && val) {
		g_reclaim.boost_pages = val;
	} else if (!strcmp(cmd, "wmark_boost_native_pages") && val) {
		g_reclaim.boost_native_pages = val;
	} else if (!strcmp(cmd, "wmark_boost_file_threshold")) {
		g_reclaim.wmark_file_threshold = val;
	} else if (!strcmp(cmd, "fg_file_scan_threshold")) {
		g_reclaim.fg_file_scan_threshold = val;
	} else if (!strcmp(cmd, "throttle_file_threshold")) {
		g_reclaim.throttle_file_threshold = val;
	} else if (!strcmp(cmd, "reset_stats")) {
		mi_reset_stats();
	} else if (!strcmp(cmd, "refill_high_mark") && val) {
		g_reclaim.pool_high = val;
		g_reclaim.pool_low = val * 80 / 100;
		g_reclaim.pool_critical = val * 50 / 100;
	} else if (!strcmp(cmd, "refill_low_mark") && val) {
		g_reclaim.pool_low = val;
	} else if (!strcmp(cmd, "refill_max_runtime")) {
		g_reclaim.refill_max_runtime = val;
	} else if (!strcmp(cmd, "refill_interval")) {
		g_reclaim.refill_interval = val;
	} else if (!strcmp(cmd, "refill_prio_duration")) {
		g_reclaim.refill_prio_duration = val;
	} else if (!strcmp(cmd, "refill_enable")) {
		g_reclaim.refill_enabled = val != 0;
		if (mi_reclaim_debug_flags & BIT(1))
			pr_info("mi_reclaim efficiency: refill_enable set to %d\n", val);
	} else {
		return -EINVAL;
	}
	return count;
}

static ssize_t event_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	unsigned long *w;
	unsigned long old, reclaimed = 0, nr_free, nr_cma, nr_file, nr_anon;
	unsigned long target;
	int val = 0, mb, scan, loops = 0, iters = 0;
	ktime_t start;
	long ms;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	if (!(g_reclaim.enable_flags & BIT(0)))
		return count;
	int flags = val;
	char name[50] = {};

	mutex_lock(&g_reclaim.reclaim_lock);
	MI_ATRACE_BEGIN("greclaim_s");
	w = (unsigned long *)((char *)current + 0xdd8);
	do {
		old = READ_ONCE(*w);
	} while (cmpxchg(w, old, old | 4) != old);
	nr_file = global_node_page_state(NR_INACTIVE_FILE) + global_node_page_state(NR_ACTIVE_FILE);
	nr_anon = global_node_page_state(NR_INACTIVE_ANON) + global_node_page_state(NR_ACTIVE_ANON);
	nr_free = global_zone_page_state(NR_FREE_PAGES);
	nr_cma = global_zone_page_state(NR_FREE_CMA_PAGES);
	scan = (flags & 1) ? 3 : (flags & 2);
	mb = flags & ~3;
	MI_ATRACE_END();
	if (mb > 1024)
		mb = 1024;
	g_reclaim.event = scan;
	if (mi_reclaim_debug_flags & BIT(0))
		pr_info("mi_reclaim global: reclaim start: free %lu cma %lu file %lu anon %lu scan %d\n",
			nr_free, nr_cma, nr_file, nr_anon, scan);
	start = ktime_get();
	target = (unsigned long)(mb << 20) >> PAGE_SHIFT;
	iters = mb / 10;
	for (loops = 0; loops < iters; loops++) {
		unsigned long r = try_to_free_mem_cgroup_pages(NULL, 2560, GFP_KERNEL, 2);

		reclaimed += r;
		if (r < 1280 || reclaimed > target)
			break;
	}
	ms = ktime_to_ms(ktime_sub(ktime_get(), start));
	snprintf(name, sizeof(name), "real reclaimed %lu time %lld ms", reclaimed, (long long)ms);
	MI_ATRACE_BEGIN(name);
	if (mi_reclaim_debug_flags & BIT(0))
		pr_info("mi_reclaim global: reclaim done: time %lld ms loops %d reclaimed %lu pages\n",
			(long long)ms, loops, reclaimed);
	if (mi_reclaim_stats_flags & BIT(4)) {
		g_reclaim.total_reclaim += reclaimed;
		g_reclaim.reclaim_times++;
		g_reclaim.total_reclaim_time += ms;
		g_reclaim.last_reclaim_time = ms;
	}
	MI_ATRACE_END();
	g_reclaim.event = 0;
	do {
		old = READ_ONCE(*w);
	} while (cmpxchg(w, old, old & ~4UL) != old);
	mutex_unlock(&g_reclaim.reclaim_lock);
	return count;
}

static ssize_t usage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len = 0;

#define EMIT(s) (len += snprintf(buf + len, PAGE_SIZE - len, s))
	EMIT("mi_reclaim - Memory Reclaim Optimization Module\n");
	EMIT("================================================\n\n");
	EMIT("[Sysfs Nodes]\n");
	EMIT("  info  - Show statistics and configuration\n");
	EMIT("  event - Trigger global reclaim\n");
	EMIT("  pid   - Manage important process list\n");
	EMIT("  usage - Show this help message\n\n");
	EMIT("[info Node Commands]\n");
	EMIT("  echo \"enable <flags>\" > info\n");
	EMIT("    flags: 0x1=global_reclaim, 0x2=efficiency\n");
	EMIT("    example: echo \"enable 3\" > info\n\n");
	EMIT("  echo \"efficiency <flags>\" > info\n");
	EMIT("    flags: 0x01=pool, 0x02=skip_throttle, 0x04=file_scan\n");
	EMIT("           0x08=wmark_boost, 0x10=clean_page\n");
	EMIT("    example: echo \"efficiency 0x1f\" > info\n\n");
	EMIT("  echo \"debug <flags>\" > info\n");
	EMIT("    flags: 0x1=global, 0x2=efficiency\n\n");
	EMIT("  echo \"stats <flags>\" > info\n");
	EMIT("    flags: 0x01=pool, 0x02=skip_throttle, 0x04=file_scan\n");
	EMIT("           0x08=wmark_boost, 0x10=global, 0x20=clean_page\n");
	EMIT("           0x40=important_proc, 0xff=all\n\n");
	EMIT("  echo \"reset_stats\" > info\n");
	EMIT("    Reset all statistics counters\n\n");
	EMIT("  echo \"prefill\" > info\n");
	EMIT("    Prefill memory pool to high mark\n\n");
	EMIT("  echo \"cleanup\" > info\n");
	EMIT("    Free all pages in memory pool\n\n");
	EMIT("  echo \"watermark_boost <pages>\" > info\n");
	EMIT("    Set watermark boost pages\n\n");
	EMIT("  echo \"wmark_boost_native_pages <pages>\" > info\n");
	EMIT("    Set native process watermark boost pages (default 2560=10MB)\n\n");
	EMIT("  echo \"wmark_boost_file_threshold <pages>\" > info\n");
	EMIT("    Set file pages threshold for watermark boost (default 10%% of total memory)\n\n");
	EMIT("  echo \"fg_file_scan_threshold <pages>\" > info\n");
	EMIT("    Set file pages threshold for file-only scan\n\n");
	EMIT("  echo \"throttle_file_threshold <pages>\" > info\n");
	EMIT("    Set file pages threshold for throttle bypass\n\n");
	EMIT("[Refill Pool Commands]\n");
	EMIT("  echo \"refill_high_mark <pages>\" > info\n");
	EMIT("    Set pool high water mark (low_mark auto set to 80%%)\n\n");
	EMIT("  echo \"refill_low_mark <pages>\" > info\n");
	EMIT("    Set pool low water mark\n\n");
	EMIT("  echo \"refill_max_runtime <ms>\" > info\n");
	EMIT("    Set max runtime for refill worker\n\n");
	EMIT("  echo \"refill_interval <ms>\" > info\n");
	EMIT("    Set min interval between refills (default 50ms)\n\n");
	EMIT("  echo \"refill_prio_duration <ms>\" > info\n");
	EMIT("    Set priority duration for refill worker (default 100ms)\n\n");
	EMIT("  echo \"refill_enable <0|1>\" > info\n");
	EMIT("    Enable/disable pool refill (0=off, 1=on)\n");
	EMIT("    When disabled, pool stops background refill but alloc still works\n\n");
	EMIT("[Clean Page Commands]\n");
	EMIT("  Note: Enable via efficiency flags (bit4=0x10)\n");
	EMIT("  Set reclaim flag in writeback path for faster reclaim\n\n");
	EMIT("[event Node Commands]\n");
	EMIT("  echo \"<reclaim_mb>\" > event\n");
	EMIT("    Trigger global reclaim for specified MB\n");
	EMIT("    example: echo \"100\" > event\n\n");
	EMIT("  Special values (low 4 bits):\n");
	EMIT("    0x1=RECLAIM_FILE, 0x2=RECLAIM_ANON\n");
	EMIT("    example: echo \"257\" > event  (100MB + FILE flag)\n\n");
	EMIT("[pid Node Commands]\n");
	EMIT("  echo \"+<pid>\" > pid    - Add pid to important list\n");
	EMIT("  echo \"-<pid>\" > pid    - Remove pid from list\n");
	EMIT("  echo \"<pid>\" > pid     - Add pid (default)\n");
	EMIT("  echo \"clear\" > pid     - Clear all pids\n");
	EMIT("    example: echo \"+1234\" > pid\n\n");
	EMIT("[Examples]\n");
	EMIT("  cat /sys/kernel/mi_reclaim/info     # View stats\n");
	EMIT("  cat /sys/kernel/mi_reclaim/usage    # View this help\n");
	EMIT("  echo \"enable 3\" > /sys/kernel/mi_reclaim/info\n");
	EMIT("  echo \"100\" > /sys/kernel/mi_reclaim/event\n");
#undef EMIT
	return len;
}

static ssize_t proc_stats_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	static const char * const bg_caller_names[IMP_CALLER_MAX] = {
		"file_scan", "shrink_slab", "pool", "wmark_boost",
	};
	ssize_t len = 0;
	int c;

	if (!(mi_reclaim_stats_flags & BIT(6)))
		return 0;
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "| [Important Proc Stats]     |\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	for (c = 0; c < IMP_CALLER_MAX; c++) {
		struct caller_cnt *s = (struct caller_cnt *)((char *)&g_reclaim + 0x250) + c;
		s64 v0 = atomic64_read(&s->v[0]), v1 = atomic64_read(&s->v[1]);
		s64 v2 = atomic64_read(&s->v[2]), v3 = atomic64_read(&s->v[3]);
		s64 v4 = atomic64_read(&s->v[4]), v5 = atomic64_read(&s->v[5]);
		s64 v6 = atomic64_read(&s->v[6]), v7 = atomic64_read(&s->v[7]);
		s64 v8 = atomic64_read(&s->v[8]);
		s64 total = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8;

		len += snprintf(buf + len, PAGE_SIZE - len, "[%s] total: %lld\n", bg_caller_names[c], total);
		len += snprintf(buf + len, PAGE_SIZE - len, "  kthread   : %lld\n", v0);
		len += snprintf(buf + len, PAGE_SIZE - len, "  rt        : %lld\n", v1);
		len += snprintf(buf + len, PAGE_SIZE - len, "  vip       : %lld\n", v2);
		len += snprintf(buf + len, PAGE_SIZE - len, "  qos       : %lld\n", v3);
		len += snprintf(buf + len, PAGE_SIZE - len, "  exiting   : %lld\n", v4);
		len += snprintf(buf + len, PAGE_SIZE - len, "  imp_pid   : %lld\n", v5);
		len += snprintf(buf + len, PAGE_SIZE - len, "  fg        : %lld\n", v6);
		len += snprintf(buf + len, PAGE_SIZE - len, "  in_intr   : %lld\n", v7);
		len += snprintf(buf + len, PAGE_SIZE - len, "  not_imp   : %lld\n", v8);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "| [Background Proc Stats]     |\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	for (c = 0; c < IMP_CALLER_MAX; c++) {
		struct caller_adj *s = (struct caller_adj *)((char *)&g_reclaim + 0x370) + c;
		s64 v0 = atomic64_read(&s->v[0]), v1 = atomic64_read(&s->v[1]);
		s64 v2 = atomic64_read(&s->v[2]), v3 = atomic64_read(&s->v[3]);

		s64 total = v0 + v1 + v2 + v3;

		len += snprintf(buf + len, PAGE_SIZE - len, "[%s] total: %lld\n", bg_caller_names[c], total);
		len += snprintf(buf + len, PAGE_SIZE - len, "  high_adj   : %lld\n", v0);
		len += snprintf(buf + len, PAGE_SIZE - len, "  native_lp  : %lld\n", v1);
		len += snprintf(buf + len, PAGE_SIZE - len, "  low_adj_lp : %lld\n", v2);
		len += snprintf(buf + len, PAGE_SIZE - len, "  not_bg     : %lld\n", v3);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "+-----------------------------+\n");
	return len;
}

static noinline __used bool has_sufficient_free_memory(void)
{
	struct pglist_data *pgdat = NODE_DATA(0);
	int i;

	(void)raw_smp_processor_id();

	for (i = 1; i >= 0; i--) {
		struct zone *z = &pgdat->node_zones[i];
		long free, high;

		if (!managed_zone(z))
			continue;
		free = zone_page_state(z, NR_FREE_PAGES);
		if (z->percpu_drift_mark && free < z->percpu_drift_mark)
			free = zone_page_state_snapshot(z, NR_FREE_PAGES);
		high = high_wmark_pages(z);
		free -= zone_page_state(z, NR_FREE_CMA_PAGES);
		free -= z->nr_reserved_highatomic;
		if (free > high)
			return true;
	}
	return false;
}

#define PS1(c, k)	(&((struct caller_cnt *)((char *)&g_reclaim + 0x250))[(c)].v[(k)])
#define PS2(c, k)	(&((struct caller_adj *)((char *)&g_reclaim + 0x370))[(c)].v[(k)])
#define PS1G(c, k)	(&((struct caller_cnt *)((char *)g + 0x250))[(c)].v[(k)])
#define PS2G(c, k)	(&((struct caller_adj *)((char *)g + 0x370))[(c)].v[(k)])
#define PS_ON		(mi_reclaim_stats_flags & BIT(6))

static noinline __used bool is_important_process(enum important_proc_caller caller)
{
	unsigned long flags;
	bool found = false;
	int adj, prio, tgid, i, n;
	struct greclaim *g = &g_reclaim;

	OPTIMIZER_HIDE_VAR(g);

	if (unlikely(in_interrupt())) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 7));
		return false;
	}
	if (current->prio < MAX_RT_PRIO) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 1));
		return true;
	}
	if (is_qos_task(current)) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 3));
		return true;
	}
	if (fatal_signal_pending(current) || (current->flags & PF_EXITING)) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 4));
		return true;
	}
	tgid = current->tgid;
	spin_lock_irqsave(&g_reclaim.important_lock, flags);
	n = g_reclaim.important_count;
	for (i = 0; i < n; i++)
		if (g->important_pids[i] == tgid)
			break;
	spin_unlock_irqrestore(&g_reclaim.important_lock, flags);
	found = i < n;
	if (found) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 5));
		return true;
	}
	if (current->flags & PF_KTHREAD) {
		if (PS_ON)
			atomic64_inc(PS1G(caller, 0));
		return true;
	}
	adj = current->signal->oom_score_adj;
	prio = current->prio;
	if (adj >= 251) {
		if (PS_ON)
			atomic64_inc(PS2G(caller, 0));
	} else if (adj == -1000 && prio >= 120) {
		if (PS_ON)
			atomic64_inc(PS2G(caller, 1));
	} else if (adj < 0 && prio >= 121) {
		if (PS_ON)
			atomic64_inc(PS2G(caller, 2));
	} else {
		if (PS_ON)
			atomic64_inc(PS2G(caller, 3));
		if (PS_ON)
			atomic64_inc(PS1G(caller, 6));
		return true;
	}
	if (PS_ON)
		atomic64_inc(PS1G(caller, 8));
	return false;
}

static noinline __used int get_wmark_boost_pages(int adj, int x)
{
	int boost;

	if (adj == -1000 && x >= 120) {
		boost = g_reclaim.boost_native_pages;
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x140));
		return boost;
	}
	if ((unsigned int)(adj - 251) <= 749) {
		boost = (unsigned long)((long)g_reclaim.boost_pages * (unsigned int)adj) / 1000;
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x148));
		return boost;
	}
	return 0;
}

static void mi_alloc_pages_reclaim_bypass(void *data, gfp_t gfp_mask, int order,
					  int alloc_flags, int migratetype, struct page **page)
{
	struct task_struct *p;
	struct zoneref *z;
	unsigned int highidx;

	u8 en = *(volatile u8 *)&g_reclaim.enable_flags;

	if (!(en & BIT(1)))
		return;
	barrier(); /* hack: keeps the stock order tbz(enable) before cbnz(order) */
	if (order)
		return;
	if (!(*(volatile u32 *)&g_reclaim.efficiency_flags & BIT(0)))
		return;
	if (*page) {
		atomic64_inc(gstat(0xf0));
		return;
	}
	if (!is_important_process(IMP_CALLER_POOL)) {
		MI_ATRACE_BEGIN("pool: isn't important process");
		MI_ATRACE_END();
		return;
	}
	p = current;
	if (g_reclaim.refill_task == p)
		return;
	highidx = (0x250015U >> ((gfp_mask & 0xf) * 2)) & 3;
	if (highidx == 2)
		highidx = 4;
	if (!highidx)
		return;
	(void)raw_smp_processor_id();
	z = NODE_DATA(0)->node_zonelists[0]._zonerefs;
	while (zonelist_zone_idx(z) > highidx)
		z++;
	if (!z || !z->zone)
		return;
	*page = pool_remove_page(z->zone);
	if (mi_reclaim_stats_flags & BIT(0)) {
		if (*page) {
			MI_ATRACE_BEGIN("s");
			atomic64_inc(gstat(0xe0));
		} else {
			MI_ATRACE_BEGIN("f");
			atomic64_inc(gstat(0xe8));
		}
		MI_ATRACE_END();
	}
	if (!*page)
		return;
	if (mi_reclaim_debug_flags & BIT(1))
		pr_info("mi_reclaim efficiency: pool alloc: %s got page from pool\n", p->comm);
	MI_ATRACE_BEGIN("pool: null");
	MI_ATRACE_END();
}


static unsigned long mi_file_pages(void)
{
	return global_node_page_state(NR_INACTIVE_FILE) + global_node_page_state(NR_ACTIVE_FILE);
}

static void mi_get_page_wmark_handler(void *data, unsigned int alloc_flags,
				      unsigned long *page_wmark)
{
	unsigned long *w = (unsigned long *)((char *)current + 0xdd8);
	int adj, boost;

	if (mi_file_pages() < (unsigned long)(long)g_reclaim.wmark_file_threshold)
		return;
	if (g_reclaim.refill_task == current) {
		if (!READ_ONCE(g_reclaim.refill_needed))
			return;
		*page_wmark += g_reclaim.boost_pages;
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x138));
		return;
	}
	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if (!(g_reclaim.efficiency_flags & BIT(3)))
		return;
	if (READ_ONCE(*w) & 2UL) {
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x150));
		return;
	}
	if (alloc_flags & 0x238) {
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x160));
		return;
	}
	if (is_important_process(IMP_CALLER_WMARK_BOOST))
		return;
	adj = current->signal->oom_score_adj;
	boost = get_wmark_boost_pages(adj, current->prio);
	if (boost < 1)
		return;
	*page_wmark += (unsigned int)boost;
	if (mi_reclaim_stats_flags & BIT(3))
		atomic64_inc(gstat(0x138));
	if (mi_reclaim_debug_flags & BIT(1))
		pr_info("mi_reclaim efficiency: wmark boost: %s adj=%d boost_pages=%d\n",
			current->comm, adj, boost);
}

static void mi_alloc_pages_slowpath_start(void *data, u64 *stime)
{
	unsigned long flags;
	s64 delta;
	int pool, crit;

	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if ((g_reclaim.efficiency_flags & BIT(0)) && READ_ONCE(g_reclaim.refill_enabled) &&
	    atomic_read(&g_reclaim.pool_count) < g_reclaim.pool_low) {
		pool = atomic_read(&g_reclaim.pool_count);
		crit = g_reclaim.pool_critical;
		delta = ktime_to_ms(ktime_get()) - READ_ONCE(g_reclaim.refill_start_ms);
		if (pool >= crit && (u64)delta < g_reclaim.refill_interval) {
			if (mi_reclaim_stats_flags & BIT(0)) {
				unsigned long free = global_zone_page_state(NR_FREE_PAGES);

				atomic_inc((atomic_t *)gstat(0x108));
				if (mi_reclaim_debug_flags & BIT(1))
					pr_info("mi_reclaim efficiency: refill interval skip: pool=%d free=%lu pages delta=%lld ms\n",
						atomic_read(&g_reclaim.pool_count), free, delta);
			}
		} else {
			spin_lock_irqsave(&g_reclaim.refill_lock, flags);
			if (READ_ONCE(g_reclaim.refill_flag)) {
				spin_unlock_irqrestore(&g_reclaim.refill_lock, flags);
			} else {
				WRITE_ONCE(g_reclaim.refill_flag, true);
				if (mi_reclaim_stats_flags & BIT(0)) {
					atomic_inc((atomic_t *)gstat(0xf8));
					if (pool < crit) {
						atomic_inc((atomic_t *)gstat(0x10c));
						if (mi_reclaim_debug_flags & BIT(1))
							pr_info("mi_reclaim efficiency: refill urgent wakeup: pool=%d delta=%lld ms\n",
								atomic_read(&g_reclaim.pool_count), delta);
					}
				}
				spin_unlock_irqrestore(&g_reclaim.refill_lock, flags);
				wake_up_process(g_reclaim.refill_task);
			}
		}
	}
	if (g_reclaim.efficiency_flags & BIT(3)) {
		unsigned long *w = (unsigned long *)((char *)current + 0xdd8);
		unsigned long old;

		do {
			old = READ_ONCE(*w);
		} while (cmpxchg(w, old, old & ~2UL) != old);
	}
	if (mi_reclaim_debug_flags & BIT(1))
		pr_info("mi_reclaim efficiency: slowpath start: %s (pool_count=%d)\n",
			current->comm, atomic_read(&g_reclaim.pool_count));
}

static void mi_direct_reclaim_end(void *data, int prio)
{
	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if (g_reclaim.efficiency_flags & BIT(3)) {
		unsigned long *w = (unsigned long *)((char *)current + 0xdd8);
		unsigned long old;

		do {
			old = READ_ONCE(*w);
		} while (cmpxchg(w, old, old | 2UL) != old);
		if (mi_reclaim_stats_flags & BIT(3))
			atomic64_inc(gstat(0x158));
	}
	if (mi_reclaim_debug_flags & BIT(1))
		pr_info("mi_reclaim efficiency: direct reclaim end: %s\n", current->comm);
}

static void mi_shrink_slab_bypass(void *data, gfp_t gfp_mask, int nid,
				  struct mem_cgroup *memcg, int priority, bool *bypass)
{
	unsigned long *w = (unsigned long *)((char *)current + 0xdd8);

	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if (current->flags & PF_KSWAPD) {
		if (mi_reclaim_stats_flags & BIT(1))
			atomic64_inc(gstat(0x130));
		return;
	}
	if (g_reclaim.refill_task == current || (READ_ONCE(*w) & 4UL)) {
		*bypass = true;
		return;
	}
	if ((g_reclaim.efficiency_flags & BIT(1)) && (READ_ONCE(*w) & 1UL)) {
		*bypass = true;
		mi_task_clear_bit(current, 0);
		if (mi_reclaim_debug_flags & BIT(1))
			pr_info("mi_reclaim efficiency: shrink slab bypass: %s (throttle)\n", current->comm);
		return;
	}
	if (is_important_process(IMP_CALLER_SHRINK_SLAB)) {
		*bypass = true;
		if (mi_reclaim_debug_flags & BIT(1))
			pr_info("mi_reclaim efficiency: shrink slab bypass: %s (foreground)\n", current->comm);
	}
}

static void mi_tune_scan_type(void *data, enum scan_balance *scan_type)
{
	unsigned long *w = (unsigned long *)((char *)current + 0xdd8);
	int ev;

	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if ((READ_ONCE(*w) & BIT(6)) || (READ_ONCE(*w) & BIT(7))) {
		if (mi_reclaim_stats_flags & BIT(4))
			atomic64_inc(gstat(0x198));
		return;
	}
	if ((g_reclaim.efficiency_flags & BIT(1)) && (READ_ONCE(*w) & 1UL)) {
		MI_ATRACE_BEGIN("scan file");
		*(int *)scan_type = 3;
		MI_ATRACE_END();
		return;
	}
	if (READ_ONCE(*w) & 4UL) {
		const char *name;

		ev = READ_ONCE(g_reclaim.event);
		if (mi_reclaim_debug_flags & BIT(0)) {
			name = ev == 2 ? "SCAN_ANON" : ev == 3 ? "SCAN_FILE" :
			       ev == 5 ? "SCAN_ANON+FILE" : "SCAN_NO";
			pr_info("mi_reclaim global: tune scan_type: %d (%s)\n", ev, name);
		}
		if (ev) {
			*(int *)scan_type = ev;
			if (mi_reclaim_stats_flags & BIT(4)) {
				if (ev == 3)
					atomic64_inc(gstat(0x178));
				else if (ev == 2)
					atomic64_inc(gstat(0x180));
				else if (ev == 5)
					atomic64_inc(gstat(0x190));
			}
		} else if (mi_reclaim_stats_flags & BIT(4)) {
			atomic64_inc(gstat(0x188));
		}
		return;
	}
	if (!(g_reclaim.efficiency_flags & BIT(2)))
		return;
	if (!is_important_process(IMP_CALLER_FILE_SCAN))
		return;
	if (global_node_page_state(NR_INACTIVE_FILE) <= (unsigned long)(long)(int)g_reclaim.fg_file_scan_threshold) {
		if (mi_reclaim_stats_flags & BIT(2))
			atomic64_inc(gstat(0x170));
		return;
	}
	MI_ATRACE_BEGIN("scan file");
	*(int *)scan_type = 3;
	MI_ATRACE_END();
	if (mi_reclaim_stats_flags & BIT(2))
		atomic64_inc(gstat(0x168));
}

static void mi_throttle_direct_reclaim_bypass(void *data, bool *bypass)
{
	unsigned long *w = (unsigned long *)((char *)current + 0xdd8);
	unsigned long old;

	if (!(g_reclaim.enable_flags & BIT(1)))
		return;
	if (!(g_reclaim.efficiency_flags & BIT(1)))
		return;
	if (mi_file_pages() < (unsigned long)(long)g_reclaim.throttle_file_threshold) {
		if (mi_reclaim_stats_flags & BIT(1))
			atomic64_inc(gstat(0x128));
		return;
	}
	do {
		old = READ_ONCE(*w);
	} while (cmpxchg(w, old, old | 1UL) != old);
	if (mi_reclaim_debug_flags & BIT(1))
		pr_info("mi_reclaim efficiency: skip throttle: %s\n", current->group_leader->comm);
	if (mi_reclaim_stats_flags & BIT(1))
		atomic64_inc(gstat(0x120));
	MI_ATRACE_BEGIN("skip throttle");
	*bypass = true;
	MI_ATRACE_END();
}

/* ---- sysfs / init ---- */

static struct kobj_attribute kobj_attr_info = __ATTR(info, 0660, info_show, info_store);
static struct kobj_attribute kobj_attr_event = __ATTR(event, 0660, event_show, event_store);
static struct kobj_attribute kobj_attr_pid = __ATTR(pid, 0660, pid_show, pid_store);
static struct kobj_attribute kobj_attr_usage = __ATTR_RO(usage);
static struct kobj_attribute kobj_attr_proc_stats = __ATTR_RO(proc_stats);

static struct attribute *mi_reclaim_attrs[] = {
	&kobj_attr_info.attr,
	&kobj_attr_event.attr,
	&kobj_attr_pid.attr,
	&kobj_attr_usage.attr,
	&kobj_attr_proc_stats.attr,
	NULL,
};

static const struct attribute_group mi_reclaim_attr_group = {
	.attrs = mi_reclaim_attrs,
};

static int __init mi_reclaim_init(void)
{
	struct greclaim *pg = &g_reclaim;
	struct task_struct *t;
	int ret;

	spin_lock_init(&g_reclaim.pool_lock);
	INIT_LIST_HEAD(&g_reclaim.pool_list);
	atomic_set(&g_reclaim.pool_count, 0);
	spin_lock_init(&g_reclaim.refill_lock);
	pg->pool_high = (int)(totalram_pages() * 4 / 1000);
	pg->pool_low = pg->pool_high * 80 / 100;
	pg->pool_critical = pg->pool_high * 50 / 100;
	g_reclaim.refill_interval = 50;
	g_reclaim.refill_max_runtime = 100;
	g_reclaim.refill_prio_duration = 100;
	g_reclaim.refill_flag = 0;
	g_reclaim.refill_enabled = 0;
	g_reclaim.boost_pages = 25600;
	g_reclaim.boost_native_pages = 2560;
	g_reclaim.wmark_file_threshold = totalram_pages() * 10 / 100;
	g_reclaim.refill_needed = 0;
	g_reclaim.fg_file_scan_threshold = 51200;
	g_reclaim.throttle_file_threshold = 12800;
	g_reclaim.important_count = 0;
	spin_lock_init(&g_reclaim.important_lock);
	mutex_init(&pg->reclaim_lock);
	g_reclaim.event = 0;

	g_reclaim.kobj = kobject_create_and_add("mi_reclaim", kernel_kobj);
	if (!g_reclaim.kobj)
		return -ENOMEM;
	ret = sysfs_create_group(g_reclaim.kobj, &mi_reclaim_attr_group);
	if (ret) {
		kobject_put(g_reclaim.kobj);
		g_reclaim.kobj = NULL;
		return ret;
	}
	t = kthread_create_on_node(refill_worker_fn, NULL, NUMA_NO_NODE, "umms_refill");
	g_reclaim.refill_task = t;
	if (IS_ERR(t)) {
		pr_err("mi_reclaim: failed to create refill worker\n");
		if (g_reclaim.kobj) {
			kobject_put(g_reclaim.kobj);
			g_reclaim.kobj = NULL;
		}
		return PTR_ERR(g_reclaim.refill_task);
	}
	g_reclaim.refill_flag = 1;
	wake_up_process(t);

	ret = register_trace_android_vh_alloc_pages_reclaim_bypass(mi_alloc_pages_reclaim_bypass, NULL);
	if (ret)
		goto fail0;
	ret = register_trace_android_vh_get_page_wmark(mi_get_page_wmark_handler, NULL);
	if (ret)
		goto fail1;
	ret = register_trace_android_vh_alloc_pages_slowpath_start(mi_alloc_pages_slowpath_start, NULL);
	if (ret)
		goto fail2;
	ret = register_trace_android_vh_direct_reclaim_end(mi_direct_reclaim_end, NULL);
	if (ret)
		goto fail3;
	ret = register_trace_android_vh_shrink_slab_bypass(mi_shrink_slab_bypass, NULL);
	if (ret)
		goto fail4;
	ret = register_trace_android_vh_tune_scan_type(mi_tune_scan_type, NULL);
	if (ret)
		goto fail5;
	ret = register_trace_android_vh_throttle_direct_reclaim_bypass(mi_throttle_direct_reclaim_bypass, NULL);
	if (ret)
		goto fail6;
	ret = register_trace_android_vh_free_task(mi_free_task, NULL);
	if (ret)
		goto fail7;

	g_reclaim.enable_flags = 3;
	g_reclaim.efficiency_flags = 3;
	pr_info("  enable_flags: 0x%x, efficiency_flags: 0x%x\n",
		g_reclaim.enable_flags, g_reclaim.efficiency_flags);
	return 0;
fail7:
	unregister_trace_android_vh_throttle_direct_reclaim_bypass(mi_throttle_direct_reclaim_bypass, NULL);
fail6:
	unregister_trace_android_vh_tune_scan_type(mi_tune_scan_type, NULL);
fail5:
	unregister_trace_android_vh_shrink_slab_bypass(mi_shrink_slab_bypass, NULL);
fail4:
	unregister_trace_android_vh_direct_reclaim_end(mi_direct_reclaim_end, NULL);
fail3:
	unregister_trace_android_vh_alloc_pages_slowpath_start(mi_alloc_pages_slowpath_start, NULL);
fail2:
	unregister_trace_android_vh_get_page_wmark(mi_get_page_wmark_handler, NULL);
fail1:
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(mi_alloc_pages_reclaim_bypass, NULL);
fail0:
	kthread_stop(g_reclaim.refill_task);
	if (g_reclaim.kobj) {
		kobject_put(g_reclaim.kobj);
		g_reclaim.kobj = NULL;
	}
	return ret;
}

static void __exit mi_reclaim_exit(void)
{
	unregister_trace_android_vh_free_task(mi_free_task, NULL);
	unregister_trace_android_vh_throttle_direct_reclaim_bypass(mi_throttle_direct_reclaim_bypass, NULL);
	unregister_trace_android_vh_tune_scan_type(mi_tune_scan_type, NULL);
	unregister_trace_android_vh_shrink_slab_bypass(mi_shrink_slab_bypass, NULL);
	unregister_trace_android_vh_direct_reclaim_end(mi_direct_reclaim_end, NULL);
	unregister_trace_android_vh_alloc_pages_slowpath_start(mi_alloc_pages_slowpath_start, NULL);
	unregister_trace_android_vh_get_page_wmark(mi_get_page_wmark_handler, NULL);
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(mi_alloc_pages_reclaim_bypass, NULL);
	if (g_reclaim.refill_task)
		kthread_stop(g_reclaim.refill_task);
	mi_reclaim_cleanup();
	if (g_reclaim.kobj) {
		kobject_put(g_reclaim.kobj);
		g_reclaim.kobj = NULL;
	}
	pr_info("mi_reclaim: unloaded\n");
}

module_param(refill_cpus, charp, 0644);
MODULE_PARM_DESC(refill_cpus, "CPU list for refill worker (e.g., '0,1,2,3' or '0-3'). Auto-detect if empty.");
module_init(mi_reclaim_init);
module_exit(mi_reclaim_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("cang from MI Performance Team");
MODULE_DESCRIPTION("Memory reclaim optimization module");
MODULE_INFO(staging, "Y");
