// SPDX-License-Identifier: GPL-2.0
/*
 * rss_monitor: per-process RSS memory change tracking via tracepoints.
 * Reconstructed from the HyperOS 4.0 stock module (no public source).
 *
 * rss_stat probe: for a thread-group leader whose RSS counter moved by more than
 * rss_threshold since the last report, queue a generic netlink event
 * (family RSS_MONITOR, group "events"); sched_process_exit drops the entry.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/hash.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/mm_types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/tracepoint.h>
#include <linux/jump_label.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <net/genetlink.h>
#include <net/net_namespace.h>

#define HT_BITS 10

struct rss_track_entry {
	struct hlist_node node;
	int tgid;
	int last[NR_MM_COUNTERS];
	int pad;
	unsigned long last_report;
	struct rcu_head rcu;
};

struct rss_nl_work {
	struct work_struct work;
	int tgid;
	int ppid;
	int uid;
	char comm[TASK_COMM_LEN];
	int new_rss[NR_MM_COUNTERS];
	int old_rss[NR_MM_COUNTERS];
};

static struct rss_mon_s {
	u64 rss_thresh_pages;
	unsigned long time_thresh_jiffies;
	struct hlist_head ht[1 << HT_BITS];
	int enabled;
	unsigned long rss_threshold_kb;
	unsigned long time_threshold_ms;
	int debug;
	unsigned long event_threshold;
	unsigned long max_pending_works;
	spinlock_t lock;
	struct kmem_cache *entry_cache;
	struct kmem_cache *work_cache;
	struct workqueue_struct *wq;
	atomic_t events_this_sec;
	unsigned long window_start;
	struct proc_dir_entry *dir;
	atomic_t entry_count;
	atomic_t dropped_events;
	atomic_t pending_works;
	struct static_key_false debug_key;
	atomic64_t probe_ns_total;
	atomic64_t probe_count;
} rss_mon;

struct interest {
	const char *name;
	void *probe;
	struct tracepoint *tp;
};

static void rss_stat_probe(void *data, struct mm_struct *mm, int member);
static void process_exit_probe(void *data, struct task_struct *p);

static struct interest interests[] = {
	{ "rss_stat", rss_stat_probe, NULL },
	{ "sched_process_exit", process_exit_probe, NULL },
};

static DEFINE_MUTEX(rss_enable_mutex);

static const struct genl_multicast_group rss_mon_mcgrps[] = {
	{ .name = "events" },
};

static struct genl_family rss_mon_genl_family __ro_after_init = {
	.name = "RSS_MONITOR",
	.version = 1,
	.maxattr = 7,
	.mcgrps = rss_mon_mcgrps,
	.n_mcgrps = ARRAY_SIZE(rss_mon_mcgrps),
	.module = THIS_MODULE,
};

static void lookup_tracepoints(struct tracepoint *tp, void *priv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(interests); i++)
		if (!strcmp(interests[i].name, tp->name))
			interests[i].tp = tp;
}

/* ---- proc files ---- */

static int proc_enable_open(struct inode *inode, struct file *file);
static int proc_enable_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", rss_mon.enabled);
	return 0;
}

static int proc_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_enable_show, NULL);
}

static ssize_t proc_enable_write(struct file *file, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	int val = 0, on, ret, i;

	ret = kstrtoint_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	on = val;
	if (on < 0 || on > 1)
		return -EINVAL;

	mutex_lock(&rss_enable_mutex);
	if (on == 1 && !rss_mon.enabled) {
		atomic_set(&rss_mon.dropped_events, 0);
		atomic_set(&rss_mon.events_this_sec, 0);
		ret = tracepoint_probe_register(interests[0].tp, interests[0].probe, NULL);
		if (ret) {
			mutex_unlock(&rss_enable_mutex);
			return ret;
		}
		rss_mon.enabled = 1;
		pr_err("rss_monitor: enabled\n");
	} else if (on == 0 && rss_mon.enabled) {
		rss_mon.enabled = 0;
		tracepoint_probe_unregister(interests[0].tp, interests[0].probe, NULL);
		tracepoint_synchronize_unregister();
		rcu_barrier();
		flush_workqueue(rss_mon.wq);
		spin_lock(&rss_mon.lock);
		for (i = 0; i < (1 << HT_BITS); i++) {
			struct hlist_node *pos, *tmp;

			hlist_for_each_safe(pos, tmp, &rss_mon.ht[i]) {
				hlist_del_init(pos);
				kmem_cache_free(rss_mon.entry_cache,
						hlist_entry(pos, struct rss_track_entry, node));
			}
		}
		atomic_set(&rss_mon.entry_count, 0);
		spin_unlock(&rss_mon.lock);
		atomic_set(&rss_mon.dropped_events, 0);
		atomic_set(&rss_mon.events_this_sec, 0);
		atomic64_set(&rss_mon.probe_ns_total, 0);
		atomic64_set(&rss_mon.probe_count, 0);
		pr_err("rss_monitor: disabled\n");
	}
	mutex_unlock(&rss_enable_mutex);
	return count;
}

static int proc_rss_threshold_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", READ_ONCE(rss_mon.rss_threshold_kb));
	return 0;
}

static int proc_rss_threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_rss_threshold_show, NULL);
}

static ssize_t proc_rss_threshold_write(struct file *file, const char __user *ubuf,
					size_t count, loff_t *ppos)
{
	unsigned long val = 0;
	int ret;

	ret = kstrtoul_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	rss_mon.rss_threshold_kb = val;
	rss_mon.rss_thresh_pages = (val << 10) >> PAGE_SHIFT;
	return count;
}

static int proc_time_threshold_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", READ_ONCE(rss_mon.time_threshold_ms));
	return 0;
}

static int proc_time_threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_time_threshold_show, NULL);
}

static ssize_t proc_time_threshold_write(struct file *file, const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	unsigned long val = 0;
	int ret;

	ret = kstrtoul_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	rss_mon.time_threshold_ms = val;
	rss_mon.time_thresh_jiffies = msecs_to_jiffies(val);
	return count;
}

static int proc_debug_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", rss_mon.debug);
	return 0;
}

static int proc_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_debug_show, NULL);
}

static ssize_t proc_debug_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	int val = 0, ret;

	ret = kstrtoint_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	if (val < 0 || val > 1)
		return -EINVAL;
	if (val && !rss_mon.debug) {
		static_branch_enable(&rss_mon.debug_key);
		rss_mon.debug = 1;
	} else if (!val && rss_mon.debug) {
		static_branch_disable(&rss_mon.debug_key);
		rss_mon.debug = 0;
	}
	return count;
}

static int proc_event_threshold_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", READ_ONCE(rss_mon.event_threshold));
	return 0;
}

static int proc_event_threshold_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_event_threshold_show, NULL);
}

static ssize_t proc_event_threshold_write(struct file *file, const char __user *ubuf,
					  size_t count, loff_t *ppos)
{
	unsigned long val = 0;
	int ret;

	ret = kstrtoul_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	if (!val)
		return -EINVAL;
	rss_mon.event_threshold = val;
	return count;
}

static int proc_max_pending_works_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", READ_ONCE(rss_mon.max_pending_works));
	return 0;
}

static int proc_max_pending_works_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_max_pending_works_show, NULL);
}

static ssize_t proc_max_pending_works_write(struct file *file, const char __user *ubuf,
					    size_t count, loff_t *ppos)
{
	unsigned long val = 0;
	int ret;

	ret = kstrtoul_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	if (!val)
		return -EINVAL;
	rss_mon.max_pending_works = val;
	return count;
}

static int proc_stats_show(struct seq_file *m, void *v)
{
	int cnt = atomic_read(&rss_mon.entry_count);
	s64 total, n;

	seq_printf(m, "entry_count: %d\n", cnt);
	seq_printf(m, "memory_bytes: %zu\n", (size_t)cnt * sizeof(struct rss_track_entry));
	seq_printf(m, "events_this_sec: %d\n", atomic_read(&rss_mon.events_this_sec));
	seq_printf(m, "dropped_events: %d\n", atomic_read(&rss_mon.dropped_events));
	seq_printf(m, "pending_works: %d\n", atomic_read(&rss_mon.pending_works));
	if (rss_mon.debug) {
		total = atomic64_read(&rss_mon.probe_ns_total);
		n = atomic64_read(&rss_mon.probe_count);
		seq_printf(m, "probe_avg_ns: %lld\n", n >= 1 ? total / n : 0);
	}
	return 0;
}

static int proc_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_stats_show, NULL);
}

static ssize_t proc_stats_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	int val = 0, ret;

	ret = kstrtoint_from_user(ubuf, count, 10, &val);
	if (ret)
		return ret;
	if (val != 1)
		return -EINVAL;
	atomic_set(&rss_mon.dropped_events, 0);
	atomic_set(&rss_mon.events_this_sec, 0);
	atomic64_set(&rss_mon.probe_ns_total, 0);
	atomic64_set(&rss_mon.probe_count, 0);
	return count;
}

#define RSS_PROC_OPS(name)						\
static const struct proc_ops proc_##name##_ops = {			\
	.proc_open	= proc_##name##_open,				\
	.proc_read	= seq_read,					\
	.proc_write	= proc_##name##_write,				\
	.proc_lseek	= seq_lseek,					\
	.proc_release	= single_release,				\
}

RSS_PROC_OPS(enable);
RSS_PROC_OPS(rss_threshold);
RSS_PROC_OPS(time_threshold);
RSS_PROC_OPS(debug);
RSS_PROC_OPS(event_threshold);
RSS_PROC_OPS(max_pending_works);
RSS_PROC_OPS(stats);

/* ---- tracepoint probes ---- */

static bool rss_comm_filtered(const char *comm);
static bool rss_rate_allow(void);
static void rss_nl_send_work(struct work_struct *work);

static void rss_track_entry_free_rcu(struct rcu_head *head)
{
	kmem_cache_free(rss_mon.entry_cache, container_of(head, struct rss_track_entry, rcu));
}

static void process_exit_probe(void *data, struct task_struct *p)
{
	int tgid = p->pid;
	struct rss_track_entry *e;

	if (p->pid != p->tgid)
		return;
	spin_lock(&rss_mon.lock);
	hlist_for_each_entry(e, &rss_mon.ht[hash_32(tgid, HT_BITS)], node) {
		if (e->tgid == tgid) {
			hlist_del_init_rcu(&e->node);
			atomic_dec(&rss_mon.entry_count);
			spin_unlock(&rss_mon.lock);
			if (static_branch_unlikely(&rss_mon.debug_key))
				pr_err("rss_monitor: exit tgid=%d\n", tgid);
			call_rcu(&e->rcu, rss_track_entry_free_rcu);
			return;
		}
	}
	spin_unlock(&rss_mon.lock);
}

static noinline void rss_ht_try_insert(int tgid, struct task_struct *p, struct mm_struct *mm)
{
	struct rss_track_entry *e, *e2;
	struct hlist_head *head;
	int i;

	if (rss_comm_filtered(p->comm)) {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: filtered tgid=%d comm=%s\n", tgid, p->comm);
		return;
	}
	e = kmem_cache_alloc(rss_mon.entry_cache, GFP_ATOMIC);
	if (!e)
		return;
	e->tgid = tgid;
	for (i = 0; i < NR_MM_COUNTERS; i++)
		e->last[i] = percpu_counter_read_positive(&mm->rss_stat[i]);
	e->last_report = 0;
	e->pad = 0;

	spin_lock(&rss_mon.lock);
	head = &rss_mon.ht[hash_32(tgid, HT_BITS)];
	hlist_for_each_entry(e2, head, node) {
		if (e2->tgid == tgid) {
			spin_unlock(&rss_mon.lock);
			kmem_cache_free(rss_mon.entry_cache, e);
			if (static_branch_unlikely(&rss_mon.debug_key))
				pr_err("rss_monitor: dup tgid=%d, skip insert\n", tgid);
			return;
		}
	}
	hlist_add_head_rcu(&e->node, head);
	atomic_inc(&rss_mon.entry_count);
	spin_unlock(&rss_mon.lock);
	if (static_branch_unlikely(&rss_mon.debug_key))
		pr_err("rss_monitor: insert tgid=%d comm=%s\n", tgid, p->comm);
}

static noinline bool rss_check_and_report(struct rss_track_entry *e, struct task_struct *p,
				 int member, int old, int new)
{
	unsigned long now = jiffies;
	struct rss_nl_work *w;
	kuid_t uid;

	if (e->last_report && time_before(now, e->last_report + READ_ONCE(rss_mon.time_thresh_jiffies))) {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: throttled tgid=%d\n", e->tgid);
		return false;
	}
	if (!rss_rate_allow())
		return false;
	if (atomic_read(&rss_mon.pending_works) >= READ_ONCE(rss_mon.max_pending_works)) {
		atomic_inc(&rss_mon.dropped_events);
		return false;
	}
	w = kmem_cache_alloc(rss_mon.work_cache, GFP_ATOMIC);
	if (!w) {
		atomic_inc(&rss_mon.dropped_events);
		return false;
	}
	w->tgid = e->tgid;
	w->ppid = p->real_parent->tgid;
	rcu_read_lock();
	uid = __task_cred(p)->uid;
	rcu_read_unlock();
	w->uid = uid.val;
	memcpy(w->comm, p->comm, TASK_COMM_LEN);
	memcpy(w->new_rss, e->last, sizeof(w->new_rss));
	memcpy(w->old_rss, e->last, sizeof(w->old_rss));
	w->new_rss[member] = new;
	w->old_rss[member] = old;
	INIT_WORK(&w->work, rss_nl_send_work);
	atomic_inc(&rss_mon.pending_works);
	queue_work(rss_mon.wq, &w->work);
	e->last_report = now;
	if (static_branch_unlikely(&rss_mon.debug_key))
		pr_err("rss_monitor: report tgid=%d comm=%s member=%d delta=%d pages\n",
		       e->tgid, p->comm, member, new - old);
	return true;
}

static void rss_stat_probe(void *data, struct mm_struct *mm, int member)
{
	struct task_struct *p;
	struct rss_track_entry *e;
	u64 start = 0;
	int tgid, cur, old;
	int *last;

	if (static_branch_unlikely(&rss_mon.debug_key))
		start = ktime_get();
	p = current;
	if (unlikely(p->mm != mm)) {
		p = READ_ONCE(mm->owner);
		if (!p)
			goto out;
	}
	if (p->flags & PF_KTHREAD)
		goto out;
	tgid = p->tgid;
	if (p->pid != tgid)
		goto out;
	hlist_for_each_entry_rcu(e, &rss_mon.ht[hash_32(tgid, HT_BITS)], node) {
		if (e->tgid == tgid)
			goto found;
	}
	rss_ht_try_insert(tgid, p, mm);
	goto out;
found:
	cur = percpu_counter_read_positive(&mm->rss_stat[member]);
	last = &e->last[member];
	old = READ_ONCE(*last);
	if (unlikely(READ_ONCE(rss_mon.rss_thresh_pages) <= (long)abs(cur - old))) {
		if (rss_check_and_report(e, p, member, old, cur))
			*last = cur;
	}
out:
	if (static_branch_unlikely(&rss_mon.debug_key) && start) {
		atomic64_add(ktime_get() - start, &rss_mon.probe_ns_total);
		atomic64_inc(&rss_mon.probe_count);
	}
}

static noinline bool rss_comm_filtered(const char *comm)
{
	return !strcmp(comm, "sh") || !strcmp(comm, "dumpsys");
}

static noinline bool rss_rate_allow(void)
{
	unsigned long now = jiffies;
	long cnt;
	unsigned long thr;

	if (time_after(now, READ_ONCE(rss_mon.window_start) + HZ)) {
		atomic_set(&rss_mon.events_this_sec, 0);
		rss_mon.window_start = now;
	}
	cnt = atomic_inc_return(&rss_mon.events_this_sec);
	thr = READ_ONCE(rss_mon.event_threshold);
	if (thr < cnt) {
		atomic_inc(&rss_mon.dropped_events);
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: rate limited, dropped=%d\n",
			       atomic_read(&rss_mon.dropped_events));
	}
	return thr >= cnt;
}

static void rss_nl_send_work(struct work_struct *work)
{
	struct rss_nl_work *w = container_of(work, struct rss_nl_work, work);
	struct sk_buff *skb;
	void *hdr;
	u64 newb[NR_MM_COUNTERS], oldb[NR_MM_COUNTERS];
	int i, ret;

	for (i = 0; i < NR_MM_COUNTERS; i++) {
		newb[i] = (u64)(long)w->new_rss[i] << PAGE_SHIFT;
		oldb[i] = (u64)(long)w->old_rss[i] << PAGE_SHIFT;
	}
	skb = genlmsg_new(nla_total_size(4) * 4 + nla_total_size(TASK_COMM_LEN) +
			  nla_total_size(sizeof(newb)) * 2, GFP_KERNEL);
	if (!skb) {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: send failed tgid=%d ret=-ENOMEM\n", w->tgid);
		goto out;
	}
	hdr = genlmsg_put(skb, 0, 0, &rss_mon_genl_family, 0, 0);
	if (!hdr)
		goto nla_fail;
	if (nla_put_u32(skb, 1, 50004) ||
	    nla_put_u32(skb, 2, w->tgid) ||
	    nla_put_u32(skb, 3, w->ppid) ||
	    nla_put_u32(skb, 4, w->uid) ||
	    nla_put(skb, 5, TASK_COMM_LEN, w->comm) ||
	    nla_put(skb, 6, sizeof(newb), newb) ||
	    nla_put(skb, 7, sizeof(oldb), oldb))
		goto nla_fail;
	genlmsg_end(skb, hdr);
	ret = genlmsg_multicast(&rss_mon_genl_family, skb, 0, 0, GFP_KERNEL);
	if (ret == -ESRCH) {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: no listener, skip send\n");
	} else if (ret) {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: send failed tgid=%d ret=%d\n", w->tgid, ret);
	} else {
		if (static_branch_unlikely(&rss_mon.debug_key))
			pr_err("rss_monitor: sent tgid=%d\n", w->tgid);
	}
	goto out;
nla_fail:
	if (static_branch_unlikely(&rss_mon.debug_key))
		pr_err("rss_monitor: send failed tgid=%d ret=-EMSGSIZE\n", w->tgid);
	nlmsg_free(skb);
out:
	atomic_dec(&rss_mon.pending_works);
	kmem_cache_free(rss_mon.work_cache, w);
}

static int __init rss_monitor_init(void)
{
	int ret, i;

	rss_mon.rss_threshold_kb = 3072;
	rss_mon.time_threshold_ms = 2500;
	rss_mon.time_thresh_jiffies = msecs_to_jiffies(2500);
	rss_mon.rss_thresh_pages = 768;
	rss_mon.event_threshold = 10;
	rss_mon.enabled = 0;
	rss_mon.debug = 0;
	rss_mon.max_pending_works = 256;
	memset(rss_mon.ht, 0, sizeof(rss_mon.ht));
	spin_lock_init(&rss_mon.lock);
	atomic_set(&rss_mon.entry_count, 0);
	atomic_set(&rss_mon.dropped_events, 0);
	atomic_set(&rss_mon.pending_works, 0);
	atomic_set(&rss_mon.events_this_sec, 0);
	rss_mon.window_start = jiffies;
	atomic64_set(&rss_mon.probe_ns_total, 0);
	atomic64_set(&rss_mon.probe_count, 0);

	rss_mon.entry_cache = kmem_cache_create("rss_track_entry", sizeof(struct rss_track_entry), 0, 0, NULL);
	if (!rss_mon.entry_cache)
		return -ENOMEM;
	rss_mon.work_cache = kmem_cache_create("rss_nl_work", sizeof(struct rss_nl_work), 0, 0, NULL);
	if (!rss_mon.work_cache) {
		ret = -ENOMEM;
		goto err_entry_cache;
	}
	rss_mon.wq = alloc_workqueue("rss_mon_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
	if (!rss_mon.wq) {
		ret = -ENOMEM;
		goto err_work_cache;
	}
	ret = genl_register_family(&rss_mon_genl_family);
	if (ret)
		goto err_wq;
	rss_mon.dir = proc_mkdir("rss_monitor", NULL);
	if (!rss_mon.dir) {
		ret = -ENOMEM;
		goto err_genl;
	}
	if (!proc_create("enable", 0644, rss_mon.dir, &proc_enable_ops) ||
	    !proc_create("rss_threshold", 0644, rss_mon.dir, &proc_rss_threshold_ops) ||
	    !proc_create("time_threshold", 0644, rss_mon.dir, &proc_time_threshold_ops) ||
	    !proc_create("debug", 0644, rss_mon.dir, &proc_debug_ops) ||
	    !proc_create("event_threshold", 0644, rss_mon.dir, &proc_event_threshold_ops) ||
	    !proc_create("max_pending_works", 0644, rss_mon.dir, &proc_max_pending_works_ops) ||
	    !proc_create("stats", 0644, rss_mon.dir, &proc_stats_ops)) {
		ret = -ENOMEM;
		goto err_proc;
	}
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);
	for (i = 0; i < ARRAY_SIZE(interests); i++) {
		if (!interests[i].tp) {
			pr_err("rss_monitor: tracepoint %s not found\n", interests[i].name);
			ret = -ENOENT;
			goto err_proc;
		}
	}
	ret = tracepoint_probe_register(interests[1].tp, interests[1].probe, NULL);
	if (ret)
		goto err_proc;
	pr_err("rss_monitor: loaded\n");
	return 0;

err_proc:
	proc_remove(rss_mon.dir);
err_genl:
	genl_unregister_family(&rss_mon_genl_family);
err_wq:
	destroy_workqueue(rss_mon.wq);
err_work_cache:
	kmem_cache_destroy(rss_mon.work_cache);
err_entry_cache:
	kmem_cache_destroy(rss_mon.entry_cache);
	return ret;
}

static void __exit rss_monitor_exit(void)
{
	int i;

	if (rss_mon.enabled)
		tracepoint_probe_unregister(interests[0].tp, interests[0].probe, NULL);
	tracepoint_probe_unregister(interests[1].tp, interests[1].probe, NULL);
	tracepoint_synchronize_unregister();
	rcu_barrier();
	flush_workqueue(rss_mon.wq);
	destroy_workqueue(rss_mon.wq);
	proc_remove(rss_mon.dir);
	genl_unregister_family(&rss_mon_genl_family);
	spin_lock(&rss_mon.lock);
	for (i = 0; i < (1 << HT_BITS); i++) {
		struct hlist_node *pos, *tmp;

		hlist_for_each_safe(pos, tmp, &rss_mon.ht[i]) {
			hlist_del_init(pos);
			kmem_cache_free(rss_mon.entry_cache,
					hlist_entry(pos, struct rss_track_entry, node));
		}
	}
	atomic_set(&rss_mon.entry_count, 0);
	spin_unlock(&rss_mon.lock);
	kmem_cache_destroy(rss_mon.entry_cache);
	kmem_cache_destroy(rss_mon.work_cache);
	pr_err("rss_monitor: unloaded\n");
}

module_init(rss_monitor_init);
module_exit(rss_monitor_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("gaoxiang17 <gaoxiang17@xiaomi.com>");
MODULE_DESCRIPTION("Per-process RSS memory change tracking via tracepoints");
