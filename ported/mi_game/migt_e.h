/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: migt package-load / cpu choice hooks, sched init/exit (the two exports) and module init/exit (G1z,
 * 18.10.2026). Included at the end of params.c after migt_d.h. Logic recovered from the OS4 stock disassembly,
 * NOT hardware-tested: DO NOT LOAD.
 */
#include <linux/hrtimer.h>
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/sched/clock.h>

extern unsigned long cpu_util_without(int cpu, struct task_struct *p);
extern unsigned long get_capacity_orig_of(int cpu);
extern unsigned long **metis_lb_cpu_array;
extern bool plat_is_relaxed_rt(struct task_struct *p);
extern struct task_struct *get_xr_task_struct_x(struct task_struct *p) __asm__("get_xr_task_struct");
extern void game_get_ravg_window(unsigned int window);
extern bool game_vvip_task(struct task_struct *p);
extern bool migt_cpu_check_rt(struct task_struct *p);
extern bool migt_skip_cpus(struct cpumask *mask);
extern bool migt_skip_cpus_rt(struct task_struct *p, struct cpumask *mask);
extern bool migt_should_skip_this_cpu_rt(struct task_struct *p, int cpu);
extern bool migt_choose_exclusive_cpu_enable(void);
extern void android_rvh_try_to_wake_up_success(void *unused, struct task_struct *p);
extern int migt_choose_exclusive_cpu(struct task_struct *p);
extern bool migt_enable_gpsync(void);
extern void xr_game_find_energy_hook(struct task_struct *p, bool *need_idle);
extern int game_load_init(void);
extern void game_load_exit(void);
extern int mist_frame_load_init(void);
extern void mist_frame_load_exit(void);
extern int migt_mmap(struct file *file, struct vm_area_struct *vma);
extern int migt_open(struct inode *inode, struct file *file);
extern int migt_release(struct inode *inode, struct file *file);
extern int migt_reset_open(struct inode *inode, struct file *file);
extern int migt_reset(struct inode *inode, struct file *file);
extern void migt_sched_exit(void);
extern int migt_sched_init(void);
extern struct tracepoint __tracepoint_android_rvh_try_to_wake_up_success;
extern int android_rvh_probe_register(struct tracepoint *tp, void *probe, void *data);
extern void create_frame_load_proc(void *rootdir);
extern void delete_frame_load_proc(void);

static spinlock_t migt_lock;
static u32 sys_migt_count;
static u64 migt_fast_div, migt_monitor_thresh;
static u64 last_update_time;
static u8 migt_boost_flag;
static u64 top_task_util[2];
static struct hrtimer hrtimer, vtask_timer;
static struct proc_dir_entry *migt_dir, *exec_buckets_entry;
static struct task_struct *migt_thread_unused __maybe_unused;

static long migt_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int migt_hardwork(void *data);

/* ---- sysctl dir "migt" ---- */
static struct ctl_table migt_table[] = {
	{ .procname = "enable_pkg_monitor", .data = &enable_pkg_monitor, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "migt_sched_debug", .data = &migt_debug, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "render_stask", .data = &set_render_as_stask, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "stask_candidate_num", .data = &stask_candidate_num, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "vip_task_max_num", .data = &vip_task_max_num, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "ip_task_max_num", .data = &ip_task_max_num, .maxlen = sizeof(int), .mode = 0644, .proc_handler = proc_dointvec },
};
static struct ctl_table_header *migt_sched_header;

/* ---- fops / devices ---- */
static const struct file_operations migt_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = migt_ioctl,
	.compat_ioctl = migt_ioctl,
	.mmap = migt_mmap,
	.open = migt_open,
	.release = migt_release,
};
static struct miscdevice migt_misc = { .minor = MISC_DYNAMIC_MINOR, .name = "migt", .fops = &migt_fops };
static struct file_operations migt_reset_fops = {
	.owner = THIS_MODULE,
	.open = migt_reset_open,
	.release = migt_reset,
};
static struct { dev_t devt; struct cdev cdev; } migt_reset_dev;
static struct { struct class *class; struct device *dev; } migt_reset_device;
static const struct proc_ops exec_buckets_fops = {
	.proc_open = exec_buckets_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* ---- cpu choice hooks (plat funcs 23 / 27) ---- */
static __always_inline bool uid_traced_inl(struct task_struct *p)
{
	bool ret = false;

	rcu_read_lock();
	if (likely(rcu_dereference(p->real_cred)))
		ret = task_uid(p).val == last_traced_uid;
	rcu_read_unlock();
	return ret;
}

static __always_inline bool is_render_inl(struct task_struct *p)
{
	struct plat_task_struct *xr = get_xr_task_struct(p);

	return XR_GAME_VALID(xr) && xr->game && (xr->game->type & 0xc0);
}

static __used int migt_prefer_cpu(struct task_struct *p)
{
	u64 util;

	if (!migt_enable() || !choose_cpu_enable)
		return -1;
	if (!p || !migt_enable())
		return -1;
	if (!uid_traced_inl(p))
		return -1;
	util = READ_ONCE(top_task_util[0]) + READ_ONCE(top_task_util[1]);
	if (unlikely(migt_debug))
		trace_printk("migt: prefer cpu: util=%llu top=%llu/%llu window=%u\n", util, top_task_util[0],
			     top_task_util[1], sched_ravg_window);
	if (game_super_task(p))
		return util < task_util_thresh ? stask_prefer_cpu_low : stask_prefer_cpu_high;
	if (is_render_inl(p))
		return util < task_util_thresh ? render_prefer_cpu_low : render_prefer_cpu_high;
	if (is_uuser_set_vip(p))
		return util < task_util_thresh ? vip_prefer_cpu_low : vip_prefer_cpu_high;
	return -1;
}

static __always_inline bool reserve_usable_inl(void)
{
	u32 reserve = migt_reserve_cpus;

	return migt_enable() && choose_cpu_exclusive_enable && choose_cpu_exclusive_enable_vote && reserve &&
	       !(reserve & ~cpumask_bits(cpu_online_mask)[0]);
}

static __used bool migt_should_skip_migrate(struct task_struct *p, int cpu)
{
	if (!reserve_usable_inl())
		return false;
	if (test_bit(cpu, &migt_reserve_cpus)) {
		struct task_struct *curr;

		if (!p)
			return true;
		if (!migt_enable() || !uid_traced_inl(p))
			return !plat_is_relaxed_rt(p);
		curr = RQ_CURR(per_cpu_ptr(&runqueues, cpu));
		if (game_super_task(curr) && stask_prefer_cpu == cpu)
			return true;
		if (is_render_inl(curr) && render_prefer_cpu == cpu)
			return true;
		if (is_render_inl(p) && render_prefer_cpu != -1)
			return false;
		if (game_super_task(p) && stask_prefer_cpu != -1)
			return false;
		if (is_uuser_set_vip(p) && vip_prefer_cpu != -1)
			return false;
		return !is_uuser_set_ip(p) || ip_prefer_cpu == -1;
	}
	if (!p || !migt_enable() || !uid_traced_inl(p))
		return false;
	if (is_render_inl(p) && render_prefer_cpu != -1)
		return true;
	if (game_super_task(p) && stask_prefer_cpu != -1)
		return true;
	if (is_uuser_set_vip(p) && vip_prefer_cpu != -1)
		return true;
	return is_uuser_set_ip(p) && ip_prefer_cpu != -1;
}

/* ---- package load accounting (plat func 14) ---- */
static __always_inline void migt_hook_thresh(unsigned int thresh)
{
	static unsigned int old_migt_thresh;

	if (old_migt_thresh && old_migt_thresh == thresh)
		return;
	old_migt_thresh = thresh;
	migt_monitor_thresh = (s64)((s64)(int)thresh * 1000000 * migt_viptask_thresh) / 100;
	migt_fast_div = 10000 / (int)thresh;
}

static __always_inline u64 fast_div_get(void)
{
	u64 d = migt_fast_div;

	if (!d) {
		d = 10000 / migt_thresh;
		migt_fast_div = d;
	}
	return d;
}

static __always_inline void vtask_boost(void)
{
	static u8 flag;

	if (spin_trylock(&migt_lock)) {
		if (!(migt_boost_flag & 1))
			flag = 1;
		migt_boost_flag = 1;
		spin_unlock(&migt_lock);
	}
	if (flag == 1 && !work_pending(&irq_boost_work) && !hrtimer_active(&vtask_timer))
		hrtimer_start_range_ns(&vtask_timer, 0, 0, HRTIMER_MODE_REL);
}

static __used void update_pkg_load(struct task_struct *p, int cpu, int event, u64 wallclock, u64 delta)
{
	struct mi_game_task *gt;
	u64 now, elapsed, exec, est, fast_div, fps;
	u32 *hist, cnt, rem;
	unsigned int thresh;
	int i;

	update_task_runtime_info(p, delta, cpu);
	now = sched_clock();
	if (!p)
		return;
	gt = get_gt_task(p);
	if (!gt)
		return;
	if (migt_boost_policy < 2)
		return;
	if ((u32)gt->f116 / 16 < 0x271)
		return;
	if (gt->f116 != last_traced_uid)
		return;
	elapsed = now >= last_update_time ? now - last_update_time : 0;
	cnt = sys_migt_count;
	hist = (u32 *)gt->f124;
	if (cnt != gt->migt_count) {
		thresh = migt_thresh;
		migt_hook_thresh(thresh);
		exec = gt->fps_exec;
		if (delta > elapsed) {
			exec += delta - elapsed;
			gt->fps_exec = exec;
			delta = elapsed;
		}
		est = gt->fps_exec_est;
		{
			u64 lim = (u64)1000000 * (u32)(thresh * 2);
			u64 m = min(exec, lim);

			gt->fps_exec_est = est ? (est + m) >> 1 : m;
		}
		if (exec > migt_monitor_thresh) {
			if (fps_mexec_update_policy == 1)
				gt->fps_mexec = (gt->fps_mexec + exec) >> 1;
			else if (fps_mexec_update_policy == 0)
				gt->fps_mexec++;
			else if (exec > gt->fps_mexec)
				gt->fps_mexec = exec;
		}
		fast_div = migt_fast_div;
		if (!fast_div) {
			fast_div = 10000 / thresh;
			migt_fast_div = fast_div;
		}
		rem = (u32)(fast_div * exec / 1000000000ULL) % 10;
		for (i = 0; i <= rem; i++)
			hist[i]++;
		fps = delta;
		gt->migt_count = cnt;
	} else {
		fast_div = migt_fast_div;
		fps = delta + gt->fps_exec;
	}
	gt->fps_exec = fps;
	fast_div = fast_div ? fast_div : fast_div_get();
	rem = (u32)(fast_div * fps / 1000000000ULL) % 10;
	if (fps_variance_ratio * hist[rem] >= hist[0])
		return;
	if (migt_boost_flag & 1)
		return;
	if (!game_ip_task(p))
		return;
	if (gt->fps_exec <= migt_monitor_thresh)
		return;
	vtask_boost();
}

/* ---- cpu choice fast path (plat func 20): idle / least loaded cpu of the preferred cluster for a vip task ---- */
struct metis_choose_cpu_args {	/* BTF of the stock, 48 bytes */
	unsigned long min_task_util;
	struct cpumask *skip_cpumask;
	struct cpumask *start_clus_cpumask;
	struct cpumask *render_clus_cpumask;
	struct cpumask *cpu_halt_cpumask;
	int order_index;
	int end_index;
};

static __used int game_choose_cpu_fastpath(struct task_struct *p, struct metis_choose_cpu_args *a, bool *should_skip)
{
	unsigned long avail, min_util, util;
	u32 m;
	int best, c, i, prev_cpu, order;

	debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	if (!migt_boost_policy)
		return -1;
	avail = cpumask_bits(cpu_active_mask)[0] & ~(u32)cpumask_bits(a->cpu_halt_cpumask)[0];
	avail &= cpumask_bits(p->cpus_ptr)[0];
	if (test_ti_thread_flag(task_thread_info(p), TIF_32BIT)) {
		avail &= cpumask_bits(system_32bit_el0_cpumask())[0];
		debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	} else {
		debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	}
	if (migt_boost_policy) {
		a->end_index = 0;
		avail &= cpumask_bits(a->start_clus_cpumask)[0];
		if (migt_choose_exclusive_cpu_enable())
			avail &= ~migt_reserve_cpus;
	}
	if (!game_vip_task(p) || !metis_lb_cpu_array)
		return -1;
	best = find_first_bit(&avail, 32);
	if (best >= nr_cpu_ids)
		return -1;
	prev_cpu = READ_ONCE(task_thread_info(p)->cpu);
	if (test_bit(prev_cpu, &avail) && available_idle_cpu(prev_cpu))
		return prev_cpu;
	order = a->order_index;
	min_util = cpu_util_without(best, p);
	for (i = 0; i < num_sched_clusters; i++) {
		unsigned long mask = (u32)avail & metis_lb_cpu_array[order][i];

		for_each_set_bit(c, &mask, 32) {
			if (available_idle_cpu(c))
				return c;
			util = cpu_util_without(c, p);
			if (util < min_util) {
				min_util = util;
				best = c;
			}
		}
		if (i >= a->end_index)
			break;
	}
	util = min_util + a->min_task_util;
	if (util < get_capacity_orig_of(best))
		return best;
	if (!game_choose_cpu_overutiled(p))
		return -1;
	if (unlikely(migt_debug))
		printk(KERN_INFO "migt: choose cpu overutiled: pid=%d, comm=%s, util=%lu, capacity=%lu\n", p->pid, p->comm,
		       util, get_capacity_orig_of(best));
	return best;
}

/* ---- the two exports: scheduler hook registration / removal ---- */
int migt_sched_init(void)
{
	struct task_struct *t;

	t = kthread_run(migt_hardwork, &g_work, "migt_thread");
	migt_thread = t;
	if (!t) {
		printk(KERN_ERR "migt-gtc: migt_thread %s: error create thread!\n", "migt_sched_init");
		return -1;
	}
	update_migt_reserve_cpus();
	render_info[0].seq = 0;
	spin_lock_init(&render_info[0].lock);
	reset_render_info(0);
	render_info[1].seq = 0;
	spin_lock_init(&render_info[1].lock);
	reset_render_info(1);
	register_plat_func(16, game_get_ravg_window);
	android_rvh_probe_register(&__tracepoint_android_rvh_try_to_wake_up_success,
				   android_rvh_try_to_wake_up_success, NULL);
	register_plat_func(22, migt_choose_exclusive_cpu);
	register_plat_func(27, migt_should_skip_migrate);
	register_plat_func(24, migt_skip_cpus);
	register_plat_func(25, migt_skip_cpus_rt);
	register_plat_func(26, migt_should_skip_this_cpu_rt);
	register_plat_func(23, migt_prefer_cpu);
	register_plat_func(29, game_vvip_task);
	register_plat_func(28, migt_cpu_check_rt);
	printk(KERN_INFO "migt-gtc: migt_sched %s: inited!\n", "migt_sched_init");
	migt_init_sucess = 1;
	if (!migt_sched_header)
		migt_sched_header = register_sysctl_sz("migt", migt_table, ARRAY_SIZE(migt_table));
	return 0;
}

EXPORT_SYMBOL_GPL(migt_sched_init);

void migt_sched_exit(void)
{
	migt_init_sucess = 0;
	unregister_sysctl_table(migt_sched_header);
	unregister_plat_func(16);
	unregister_plat_func(22);
	unregister_plat_func(27);
	unregister_plat_func(24);
	unregister_plat_func(25);
	unregister_plat_func(26);
	unregister_plat_func(23);
	unregister_plat_func(29);
	unregister_plat_func(28);
	migt_sched_header = NULL;
	if (migt_thread && !IS_ERR(migt_thread)) {
		kthread_stop(migt_thread);
		printk(KERN_INFO "migt-gtc: %s, success \n", "migt_sched_exit");
	}
}
EXPORT_SYMBOL_GPL(migt_sched_exit);

/* ---- module init / exit ---- */
static int __init migt_init(void)
{
	int ret = 0;
	unsigned int cpu;

	xr_sched_init();
	migt_sched_init();
	game_load_init();
	mist_frame_load_init();
	cpumask_copy(&cpus_visit, cpu_possible_mask);
	for_each_present_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (!policy) {
			printk(KERN_ERR "migt: %s: Failed to get cpufreq policy for cpu%d\n",
			       "migt_freq_qos_request_init_first", cpu);
			goto first_failed;
		}
		ret = freq_qos_add_request(&policy->constraints, &per_cpu(qos_req_min, cpu), FREQ_QOS_MIN, 0);
		if (ret < 0) {
			printk(KERN_ERR "migt: %s: Failed to add min freq constraint (%d)\n",
			       "migt_freq_qos_request_init_first", ret);
			cpufreq_cpu_put(policy);
			goto first_failed;
		}
		ret = freq_qos_add_request(&policy->constraints, &per_cpu(qos_req_max, cpu), FREQ_QOS_MAX, INT_MAX);
		if (ret < 0) {
			printk(KERN_ERR "migt: %s: Failed to add max freq constraint (%d)\n",
			       "migt_freq_qos_request_init_first", ret);
			cpufreq_cpu_put(policy);
			goto first_failed;
		}
		cpufreq_cpu_put(policy);
	}
	goto first_ok;
first_failed:
	for_each_present_cpu(cpu) {
		if (freq_qos_request_active(&per_cpu(qos_req_min, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_min, cpu));
		if (freq_qos_request_active(&per_cpu(qos_req_max, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_max, cpu));
	}
	migt_err_at(1871, "migt_freq failed %d to wq!\n");
	ret = cpufreq_register_notifier(&init_freq_qos_notifier, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		cpufreq_unregister_notifier(&init_freq_qos_notifier, CPUFREQ_POLICY_NOTIFIER);
		game_load_exit();
		migt_sched_exit();
		return ret;
	}
first_ok:
	hrtimer_init(&hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer.function = do_boost_work;
	hrtimer_init(&vtask_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	vtask_timer.function = do_boost_work;
	migt_wq = alloc_workqueue("migt_wq", WQ_HIGHPRI, 0);
	if (!migt_wq) {
		ret = -EFAULT;
		goto unwind_qos;
	}
	migt_dir = proc_mkdir("migt", NULL);
	if (!migt_dir) {
		ret = -EFAULT;
		goto destroy_wq;
	}
	exec_buckets_entry = proc_create("show_exec_buckets", 0664, migt_dir, &exec_buckets_fops);
	create_frame_load_proc(migt_dir);
	ret = misc_register(&migt_misc);
	if (ret)
		goto remove_proc;
	ret = alloc_chrdev_region(&migt_reset_dev.devt, 0, 1, "migt_reset");
	if (ret) {
		printk(KERN_ERR "migt: alloc_chrdev_region failed\n");
		goto misc_dereg;
	}
	cdev_init(&migt_reset_dev.cdev, &migt_reset_fops);
	migt_reset_dev.cdev.owner = THIS_MODULE;
	ret = cdev_add(&migt_reset_dev.cdev, migt_reset_dev.devt, 1);
	if (ret) {
		printk(KERN_ERR "migt: cdev_add failed\n");
		unregister_chrdev_region(migt_reset_dev.devt, 1);
		goto misc_dereg;
	}
	migt_reset_device.class = class_create("migt_reset");
	if (!migt_reset_device.class || IS_ERR(migt_reset_device.class)) {
		printk(KERN_ERR "migt: class_create failed\n");
		ret = PTR_ERR(migt_reset_device.class);
		goto cdev_out;
	}
	migt_reset_device.dev = device_create(migt_reset_device.class, NULL, migt_reset_dev.devt, NULL, "migt_reset");
	if (!migt_reset_device.dev || IS_ERR(migt_reset_device.dev)) {
		printk(KERN_ERR "migt: device_create failed\n");
		ret = PTR_ERR(migt_reset_device.dev);
		class_destroy(migt_reset_device.class);
		goto cdev_out;
	}
	goto devices_ok;
cdev_out:
	cdev_del(&migt_reset_dev.cdev);
	unregister_chrdev_region(migt_reset_dev.devt, 1);
	if (ret)
		goto misc_dereg;
devices_ok:
	INIT_DELAYED_WORK(&migt_work, do_migt);
	INIT_WORK(&irq_boost_work, do_migt);
	INIT_DELAYED_WORK(&migt_rem, do_migt_rem);
	INIT_DELAYED_WORK(&ceiling_work, do_ceiling_limit);
	INIT_DELAYED_WORK(&ceiling_rem, do_ceiling_rem);
	spin_lock_init(&migt_lock);
	for_each_possible_cpu(cpu) {
		struct migt *mi = &per_cpu(migt_info, cpu);

		mi->enabled = cpu;
		mi->migt_ceiling_max = INT_MAX;
	}
	register_plat_func(13, xr_game_find_energy_hook);
	register_plat_func(14, update_pkg_load);
	register_plat_func(15, xr_game_walt_get_indicies);
	register_plat_func(20, game_choose_cpu_fastpath);
	register_plat_func(35, migt_enable_gpsync);
	register_plat_func(30, migt_enable);
	return 0;
misc_dereg:
	misc_deregister(&migt_misc);
remove_proc:
	if (migt_dir) {
		if (exec_buckets_entry)
			proc_remove(exec_buckets_entry);
		proc_remove(migt_dir);
	}
destroy_wq:
	__flush_workqueue(migt_wq);
	destroy_workqueue(migt_wq);
unwind_qos:
	for_each_present_cpu(cpu) {
		if (freq_qos_request_active(&per_cpu(qos_req_min, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_min, cpu));
		if (freq_qos_request_active(&per_cpu(qos_req_max, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_max, cpu));
	}
	return ret;
}

static void __exit migt_exit(void)
{
	unsigned int cpu;

	unregister_plat_func(30);
	unregister_plat_func(13);
	unregister_plat_func(14);
	unregister_plat_func(15);
	unregister_plat_func(20);
	unregister_plat_func(35);
	misc_deregister(&migt_misc);
	device_destroy(migt_reset_device.class, migt_reset_dev.devt);
	class_destroy(migt_reset_device.class);
	cdev_del(&migt_reset_dev.cdev);
	unregister_chrdev_region(migt_reset_dev.devt, 1);
	if (migt_dir) {
		if (exec_buckets_entry)
			proc_remove(exec_buckets_entry);
		delete_frame_load_proc();
		proc_remove(migt_dir);
	}
	__flush_workqueue(migt_wq);
	destroy_workqueue(migt_wq);
	for_each_present_cpu(cpu) {
		if (freq_qos_request_active(&per_cpu(qos_req_min, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_min, cpu));
		if (freq_qos_request_active(&per_cpu(qos_req_max, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_max, cpu));
	}
	game_load_exit();
	migt_sched_exit();
	xr_sched_exit();
	printk("%s success\n", __func__);
	mist_frame_load_exit();
}

module_init(migt_init);
module_exit(migt_exit);
