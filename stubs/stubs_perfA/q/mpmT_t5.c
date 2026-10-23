// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock mi_perf_memory.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/task.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/vmscan.h>

/* bits of mi_memory.greclaim_enable (written by sysfs, 7 after init) */
#define GRECLAIM_ENABLE		0x2
#define SERVER_APP_ENABLE	0x4
#define LOG_ENABLE		0x10
#define LOG_ENABLE2		0x20

enum scan_balance {
	SCAN_NO,
	SCAN_ANON = 2,
	SCAN_FILE,
};

struct mi_memory {
	int low_free_enable;
	unsigned int greclaim_enable;
	int event;
	int reserved;
	atomic_t wakeup_reclaim_count;
	atomic_t wakeup_slowpath_count;
	atomic_t wakeup_kill_count;
	atomic_t wakeup_throttle_count;
	atomic_t rescue_throttle_count;
	unsigned long total_nr_reclaim;
	unsigned long total_reclaim_times;
	atomic_t reclaim_wmark;
	atomic_t interval_kill_time;
	atomic_t interval_reclaim_time;
	unsigned long file_down_threshold;
	unsigned long file_up_threshold;
	unsigned long anon_down_threshold;
	unsigned long anon_up_threshold;
	struct kobject *low_free_kobj;
	struct kobject *mi_reclaim_kobj;
	struct mutex greclaim_lock;
	atomic_t scan_type;
	atomic_t madvise_interrupt;
	atomic_t lmk_event;
	wait_queue_head_t epoll_event_wait;
};

static struct mi_memory *pg_memory;
static struct mi_memory g_memory;

/* ---------------- /proc/lowmemorykiller ---------------- */
static int lmk_event_show(struct seq_file *m, void *v)
{
	int val = atomic_read(&pg_memory->lmk_event);

	atomic_set(&pg_memory->lmk_event, 0);
	seq_printf(m, "%d\n", val);
	return 0;
}

static int lmk_event_open(struct inode *inode, struct file *file)
{
	return single_open(file, lmk_event_show, pde_data(inode));
}

/* the Xiaomi kernel's poll_wait() has no smp_mb() (the GKI snapshot on the build server has one) */
static inline void mi_poll_wait(struct file *filp, wait_queue_head_t *wait_address, poll_table *p)
{
	if (p && p->_qproc && wait_address)
		p->_qproc(filp, wait_address, p);
}

static __poll_t lmk_event_poll(struct file *file, poll_table *wait)
{
	mi_poll_wait(file, &pg_memory->epoll_event_wait, wait);
	return atomic_read(&pg_memory->lmk_event) > 0;
}

static const struct proc_ops event_proc_ops = {
	.proc_open = lmk_event_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_poll = lmk_event_poll,
};

/* ---------------- sysfs: low_free ---------------- */
static ssize_t low_free_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "enable %d\n", pg_memory->low_free_enable);
}

static ssize_t low_free_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;
	pg_memory->low_free_enable = val;
	return count;
}

static ssize_t low_free_parm_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			 "wakeup_reclaim_count %d\nwakeup_kill_count %d\nwakeup_throttle_count %d\nwakeup_slowpath_count %d\ninterval_kill_time %d\ninterval_reclaim_time %d\nreclaim_wmark %d\n",
			 atomic_read(&pg_memory->wakeup_reclaim_count),
			 atomic_read(&pg_memory->wakeup_kill_count),
			 atomic_read(&pg_memory->wakeup_throttle_count),
			 atomic_read(&pg_memory->wakeup_slowpath_count),
			 atomic_read(&pg_memory->interval_kill_time),
			 atomic_read(&pg_memory->interval_reclaim_time),
			 atomic_read(&pg_memory->reclaim_wmark));
}

static ssize_t low_free_parm_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val[3] = { 0 };

	if (sscanf(buf, "%d %d %d", &val[0], &val[1], &val[2]) != 3) {
		pr_info("usage: total three args, wmark kill_time reclaim_time");
		return -EINVAL;
	}
	atomic_set(&pg_memory->reclaim_wmark, val[0]);
	atomic_set(&pg_memory->interval_kill_time, val[1]);
	atomic_set(&pg_memory->interval_reclaim_time, val[2]);
	return count;
}

/* ---------------- global reclaim ---------------- */
void do_reclaim(int reclaim_total)
{
	unsigned long nr_free, nr_cma_free, nr_active_anon, nr_inactive_anon, nr_active_file, nr_inactive_file;
	unsigned long nr_anon_total, nr_file_total, reclaimed = 0, target_pages, nr;
	int scan, event, loops, i;
	ktime_t start, end;

	if (!(pg_memory->greclaim_enable & GRECLAIM_ENABLE)) {
		pr_info("global reclaim function not enabled");
		return;
	}
	mutex_lock(&pg_memory->greclaim_lock);

	nr_free = global_zone_page_state(NR_FREE_PAGES);
	nr_cma_free = global_zone_page_state(NR_FREE_CMA_PAGES);
	nr_active_anon = global_node_page_state(NR_ACTIVE_ANON);
	nr_inactive_anon = global_node_page_state(NR_INACTIVE_ANON);
	nr_active_file = global_node_page_state(NR_ACTIVE_FILE);
	nr_inactive_file = global_node_page_state(NR_INACTIVE_FILE);
	nr_anon_total = nr_active_anon + nr_inactive_anon;
	nr_file_total = nr_active_file + nr_inactive_file;

	if (nr_anon_total < pg_memory->anon_down_threshold) {
		if (nr_file_total <= pg_memory->file_up_threshold) {
			if (pg_memory->greclaim_enable & LOG_ENABLE)
				pr_info("break reclaim: anon and file pages too lower nr_active_file_pages %lu, nr_inactive_file_pages %lu nr_active_anon_pages %lu, nr_inactive_anon_pages %lu anon_down_threshold %lu file_down_threshold %lu anon_up_threshold %lu file_up_threshold %lu",
					nr_active_file, nr_inactive_file, nr_active_anon, nr_inactive_anon,
					pg_memory->anon_down_threshold, pg_memory->file_down_threshold,
					pg_memory->anon_up_threshold, pg_memory->file_up_threshold);
			goto out_unlock;
		}
		scan = SCAN_FILE;
	} else if (nr_anon_total > pg_memory->anon_up_threshold) {
		scan = SCAN_ANON;
	} else {
		if (nr_file_total <= pg_memory->file_up_threshold)
			goto out_unlock;
		scan = SCAN_FILE;
	}

	if (reclaim_total & 1)
		scan = SCAN_FILE;
	if (reclaim_total & 2)
		scan = SCAN_ANON;
	event = reclaim_total & 3;
	atomic_set(&pg_memory->scan_type, scan);
	reclaim_total = min(reclaim_total, 1024);

	if (pg_memory->greclaim_enable & LOG_ENABLE)
		pr_info("reclaim start: free %lu cma_free %lu nr_anon_total %lu nr_file_total %lu nr_active_anon %lu nr_inactive_anon %lu nr_active_file %lu nr_inactive_file %lu",
			nr_free, nr_cma_free, nr_anon_total, nr_file_total,
			nr_active_anon, nr_inactive_anon, nr_active_file, nr_inactive_file);

	target_pages = (unsigned long)(reclaim_total << 20) >> PAGE_SHIFT;
	loops = max(reclaim_total / 10, 0);
	start = ktime_get();
	for (i = 0; i < loops;) {
		nr = try_to_free_mem_cgroup_pages(NULL, 2560, GFP_KERNEL, MEMCG_RECLAIM_MAY_SWAP);
		reclaimed += nr;
		i++;
		if (nr < 1280) {
			if (pg_memory->greclaim_enable & LOG_ENABLE)
				pr_info("break reclaim: inefficient want %dM, actual %lu pages, the last time %lu pages",
					reclaim_total, reclaimed, nr);
			break;
		}
		if (reclaimed > target_pages) {
			if (pg_memory->greclaim_enable & LOG_ENABLE)
				pr_info("break reclaim: the reclaim target has been achieved loops %d", i);
			break;
		}
	}
	end = ktime_get();
	if (pg_memory->greclaim_enable & LOG_ENABLE)
		pr_info("reclaim info: time %lld ms loops %d event %d scan_type %d want_reclaim %d M actual_reclaim %lu pages",
			ktime_to_ms(ktime_sub(end, start)), i, event, scan, reclaim_total, reclaimed);
	atomic_set(&pg_memory->scan_type, 0);
	pg_memory->total_nr_reclaim += reclaimed;
	pg_memory->total_reclaim_times++;
	mutex_unlock(&pg_memory->greclaim_lock);
	if (pg_memory->greclaim_enable & LOG_ENABLE)
		pr_info("reclaim end: free %lu cma_free %lu nr_anon_total %lu nr_file_total %lu nr_active_anon %lu nr_inactive_anon %lu nr_active_file %lu nr_inactive_file %lu",
			global_zone_page_state(NR_FREE_PAGES), global_zone_page_state(NR_FREE_CMA_PAGES),
			global_node_page_state(NR_ACTIVE_ANON) + global_node_page_state(NR_INACTIVE_ANON),
			global_node_page_state(NR_ACTIVE_FILE) + global_node_page_state(NR_INACTIVE_FILE),
			global_node_page_state(NR_ACTIVE_ANON), global_node_page_state(NR_INACTIVE_ANON),
			global_node_page_state(NR_ACTIVE_FILE), global_node_page_state(NR_INACTIVE_FILE));
	return;

out_unlock:
	mutex_unlock(&pg_memory->greclaim_lock);
	atomic_set(&pg_memory->scan_type, 0);
}

/* ---------------- sysfs: mi_reclaim ---------------- */
static ssize_t greclaim_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "enable %d\n", pg_memory->greclaim_enable);
}

static ssize_t greclaim_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;
	pg_memory->greclaim_enable = val;
	return count;
}

static ssize_t event_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "event %d\n", pg_memory->event);
}

static ssize_t event_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;
	do_reclaim(val);
	return count;
}

static ssize_t madvise_interrupt_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "event %d\n", atomic_read(&pg_memory->madvise_interrupt));
}

static ssize_t madvise_interrupt_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;
	atomic_set(&pg_memory->madvise_interrupt, val);
	return count;
}

static ssize_t greclaim_parm_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	unsigned long file_down, file_up, anon_down, anon_up, total_nr, total_times;

	mutex_lock(&pg_memory->greclaim_lock);
	file_down = pg_memory->file_down_threshold;
	file_up = pg_memory->file_up_threshold;
	anon_down = pg_memory->anon_down_threshold;
	anon_up = pg_memory->anon_up_threshold;
	total_nr = pg_memory->total_nr_reclaim;
	total_times = pg_memory->total_reclaim_times;
	mutex_unlock(&pg_memory->greclaim_lock);
	return scnprintf(buf, PAGE_SIZE,
			 "file_down_threshold %lu\nfile_up_threshold %lu\nanon_down_threshold %lu\nanon_up_threshold %lu\ntotal_nr_reclaim %lu\ntotal_reclaim_times %lu\nrescue_throttle_count %d\n",
			 file_down, file_up, anon_down, anon_up, total_nr, total_times,
			 atomic_read(&pg_memory->rescue_throttle_count));
}

static ssize_t greclaim_parm_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val[4] = { 0 };

	if (sscanf(buf, "%lu %lu %lu %lu", &val[0], &val[1], &val[2], &val[3]) != 4) {
		pr_info("usage: file down threshold, file up thresholdanon down threshold, anon up threshold");
		return -EINVAL;
	}
	mutex_lock(&pg_memory->greclaim_lock);
	pg_memory->file_down_threshold = val[0];
	pg_memory->file_up_threshold = val[1];
	pg_memory->anon_down_threshold = val[2];
	pg_memory->anon_up_threshold = val[3];
	mutex_unlock(&pg_memory->greclaim_lock);
	return count;
}

#define MI_ATTR(_name) \
	static struct kobj_attribute kobj_attr_##_name = __ATTR(_name, 0660, _name##_show, _name##_store)

MI_ATTR(low_free_enable);
MI_ATTR(low_free_parm);
static struct attribute *low_free_attrs[] = {
	&kobj_attr_low_free_enable.attr,
	&kobj_attr_low_free_parm.attr,
	NULL,
};
static struct attribute_group low_free_attr_group = {
	.attrs = low_free_attrs,
};

MI_ATTR(greclaim_enable);
MI_ATTR(event);
MI_ATTR(madvise_interrupt);
MI_ATTR(greclaim_parm);
static struct attribute *mi_reclaim_attrs[] = {
	&kobj_attr_greclaim_enable.attr,
	&kobj_attr_event.attr,
	&kobj_attr_madvise_interrupt.attr,
	&kobj_attr_greclaim_parm.attr,
	NULL,
};
static struct attribute_group mi_reclaim_attr_group = {
	.attrs = mi_reclaim_attrs,
};

/* ---------------- vendor hooks ---------------- */
static void tune_scan_type(void *data, enum scan_balance *scan_type)
{
	struct task_struct *p = current;

	if (p->flags & PF_KTHREAD)
		return;
	if (!atomic_read(&pg_memory->scan_type))
		return;
	get_task_struct(p);
	if (!strncmp(p->comm, "MiuiIntegratedM", 15) || (pg_memory->greclaim_enable & LOG_ENABLE)) {
		*scan_type = atomic_read(&pg_memory->scan_type);
		if (pg_memory->greclaim_enable & LOG_ENABLE)
			pr_info("scan type %d", *scan_type);
	} else if (pg_memory->greclaim_enable & SERVER_APP_ENABLE) {
		if (rt_task(p) ||
		    !strncmp(p->group_leader->comm, "com.miui.home", 13) ||
		    !strncmp(p->group_leader->comm, "surfaceflinger", 14) ||
		    !strncmp(p->group_leader->comm, ".globallauncher", 15) ||
		    !strncmp(p->group_leader->comm, "system_server", 13)) {
			*scan_type = SCAN_FILE;
			if (pg_memory->greclaim_enable & LOG_ENABLE2)
				pr_info("scan type %d for server app", *scan_type);
		}
	}
	put_task_struct(p);
}

static void shrink_slab_bypass(void *data, gfp_t gfp_mask, int nid, struct mem_cgroup *memcg, int priority, bool *bypass)
{
	struct task_struct *p = current;

	get_task_struct(p);
	if (!strncmp(p->comm, "MiuiIntegratedM", 15)) {
		*bypass = true;
	} else if (pg_memory->greclaim_enable & SERVER_APP_ENABLE) {
		if (rt_task(p) ||
		    !strncmp(p->group_leader->comm, "com.miui.home", 13) ||
		    !strncmp(p->group_leader->comm, "surfaceflinger", 14) ||
		    !strncmp(p->group_leader->comm, ".globallauncher", 15) ||
		    !strncmp(p->group_leader->comm, "system_server", 13))
			*bypass = true;
	}
	put_task_struct(p);
}

static void throttle_direct_reclaim_bypass(void *data, bool *bypass)
{
	struct task_struct *p = current;

	if (!(pg_memory->greclaim_enable & SERVER_APP_ENABLE)) {
		pr_info("skip direct reclaim fuc not enabled");
		return;
	}
	if (rt_task(p)) {
		if (pg_memory->greclaim_enable & LOG_ENABLE2)
			pr_info("skip %s throttle", p->group_leader->comm);
		atomic_inc(&pg_memory->rescue_throttle_count);
		*bypass = true;
	} else if (!strncmp(p->group_leader->comm, "com.miui.home", 13) ||
		    !strncmp(p->group_leader->comm, "surfaceflinger", 14) ||
		    !strncmp(p->group_leader->comm, ".globallauncher", 15) ||
		    !strncmp(p->group_leader->comm, "system_server", 13)) {
		if (pg_memory->greclaim_enable & LOG_ENABLE2)
			pr_info("skip %s throttle", p->group_leader->comm);
		atomic_inc(&pg_memory->rescue_throttle_count);
		*bypass = true;
	}
}

static int __init mi_memory_init(void)
{
	struct kobject *kobj;
	int ret;

	pg_memory = &g_memory;
	atomic_set(&pg_memory->reclaim_wmark, 200);
	atomic_set(&pg_memory->interval_kill_time, 100);
	atomic_set(&pg_memory->interval_reclaim_time, 100);
	atomic_set(&pg_memory->madvise_interrupt, 0);
	pg_memory->file_down_threshold = 0x60000;
	pg_memory->file_up_threshold = 0x80000;
	pg_memory->anon_down_threshold = 0x60000;
	pg_memory->anon_up_threshold = 0x6cccc;
	mutex_init(&pg_memory->greclaim_lock);
	init_waitqueue_head(&pg_memory->epoll_event_wait);
	pg_memory->event = 0;
	if (!proc_create("lowmemorykiller", 0, NULL, &event_proc_ops))
		pr_err("error creating kernel lmk event file\n");
	mb();

	kobj = kobject_create_and_add("low_free", kernel_kobj);
	if (!kobj) {
		pr_err("failed to create low_free node.\n");
		goto fail_low_free;
	}
	ret = sysfs_create_group(kobj, &low_free_attr_group);
	if (ret) {
		pr_err("failed to create low_free attrs.\n");
		kobject_put(kobj);
fail_low_free:
		pg_memory->low_free_kobj = NULL;
		return -1;
	}
	pg_memory->low_free_kobj = kobj;

	kobj = kobject_create_and_add("mi_reclaim", kernel_kobj);
	if (!kobj) {
		pr_err("failed to create mi_reclaim node.\n");
		goto fail_mi_reclaim;
	}
	ret = sysfs_create_group(kobj, &mi_reclaim_attr_group);
	if (ret) {
		pr_err("failed to create mi_reclaim attrs.\n");
		kobject_put(kobj);
fail_mi_reclaim:
		pg_memory->mi_reclaim_kobj = NULL;
		return -1;
	}
	pg_memory->mi_reclaim_kobj = kobj;

	ret = register_trace_android_vh_tune_scan_type(tune_scan_type, NULL);
	if (ret) {
		pr_err("Failed to register tune_scan_type hook.\n");
		goto done;
	}
	ret = register_trace_android_vh_shrink_slab_bypass(shrink_slab_bypass, NULL);
	if (ret) {
		pr_err("Failed to register shrink slab hook.\n");
		goto unreg_tune;
	}
	ret = register_trace_android_vh_throttle_direct_reclaim_bypass(throttle_direct_reclaim_bypass, NULL);
	if (ret) {
		pr_err("Failed to register throttle reclaim hook.\n");
		unregister_trace_android_vh_shrink_slab_bypass(shrink_slab_bypass, NULL);
unreg_tune:
		unregister_trace_android_vh_tune_scan_type(tune_scan_type, NULL);
	}
done:
	mb();
	pg_memory->greclaim_enable = 7;
	pr_info("mi memory init ok\n");
	return 0;
}

static void __exit mi_memory_exit(void)
{
	if (pg_memory->low_free_kobj)
		kobject_put(pg_memory->low_free_kobj);
	pg_memory->low_free_kobj = NULL;
	if (pg_memory->mi_reclaim_kobj)
		kobject_put(pg_memory->mi_reclaim_kobj);
	pg_memory->mi_reclaim_kobj = NULL;
	unregister_trace_android_vh_tune_scan_type(tune_scan_type, NULL);
	unregister_trace_android_vh_shrink_slab_bypass(shrink_slab_bypass, NULL);
	unregister_trace_android_vh_throttle_direct_reclaim_bypass(throttle_direct_reclaim_bypass, NULL);
}

module_init(mi_memory_init);
module_exit(mi_memory_exit);
MODULE_AUTHOR("zhangcang<zhangcang@xaiomi.com>");
MODULE_DESCRIPTION("memory");
MODULE_LICENSE("GPL");
