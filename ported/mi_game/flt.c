// SPDX-License-Identifier: GPL-2.0
/* mi_game: frame-load tracking (flt.c): tracepoints + small helpers. PARTIAL, DO NOT LOAD. */
#define CREATE_TRACE_POINTS
#include "mi_game.h"
#include "trace_mi_game.h"
#include <linux/fs.h>
#include <linux/cpufreq.h>

#pragma clang diagnostic ignored "-Wmissing-prototypes"

static struct flt_global global_frame_info;
static u32 mcd_time;
static u64 last_vsyncPeriod;
static u8 flt_vsync_initialized;
static u64 target_runtime1;
static struct frame_sf frame_sf_info;
static int flt_target_delta_orig __used = 1610;
static int flt_target_delta __used = 1610;
static int cal_cur_cid __used;
static int last_cal_cur_cid = -1;

unsigned int flt_target_fps, flt_fence_enable, flt_gpu_enable, glk_fbreak_enable;
/* G1w: these 13 module-param objects are LOCAL in the stock flt translation unit (were global in params.c) */
static unsigned int flt_ftrace_debug, flt_render_load, flt_render_hook, reasons_flag, force_update_freq;
static unsigned int flt_fpercent_other = 2, flt_deq_ajust_enable = 1, flt_render_cluster = 1, flt_new_render_cal_thresh = 4;
static unsigned int flt_choose_avg_num[4] = { 2, 2, 2, 2 };
static unsigned int flt_low_freq[4] = { 902400, 844800, 499200, 902400 };
static int flt_up_thresh_margin = 50, flt_down_thresh_margin = 150;
unsigned int glk_freq_limit_walt, glk_freq_limit_start;
unsigned int glk_maxfreq[4], glk_minfreq[4];

int flt_enable(void)
{
	return GFI_U32(0x20) && flt_target_fps;
}

unsigned int cpu_to_cluster(int cpu)
{
	unsigned int cl = get_cluster_type(cpu);

	return cl > num_sched_clusters ? 0 : cl;
}

static noinline int comp_target_vsync(int target_time)
{
	int fps;
	s64 period;

	if (target_time >= 514 && target_time < 569) {
		fps = 185;
	} else if (target_time >= 576 && target_time < 637) {
		fps = 165;
	} else if (target_time >= 659 && target_time < 721) {
		fps = 144;
	} else {
		switch (target_time / 100) {
		case 7 ... 10:
			fps = 120;
			break;
		case 11 ... 15:
			fps = 90;
			break;
		case 16 ... 19:
			fps = 60;
			break;
		case 31 ... 36:
			fps = 30;
			break;
		default:
			pr_err("flt: unknown traget_time %d, failed to identify target fps\n", target_time);
			fps = 0;
			break;
		}
	}
	period = frame_sf_info.vsyncPeriod;
	if (fps != (int)((1000000000 + period / 2) / period))
		target_time = max((int)period / 10000, target_time);
	return max(target_time, (int)mcd_time);
}

void mi_set_flt_target_delta(int delta)
{
	mcd_time = delta;
	flt_target_delta = comp_target_vsync(delta ? delta : flt_target_delta_orig);
}
EXPORT_SYMBOL_GPL(mi_set_flt_target_delta);

static struct frame_load_items *focused_frame_load;	/* G14c: tentative declaration, defined below in the stock order */

void flt_update_fence_pending(void)
{
	if (!flt_fence_enable || !focused_frame_load)
		return;
	global_frame_info.fence_pending = 1;
	irq_work_queue(&global_frame_info.irq);
}

void flt_update_gpu(s64 t)
{
	global_frame_info.fence_pending = 0;
	if (!flt_gpu_enable || !focused_frame_load)
		return;
	global_frame_info.gpu_time = t;
	target_runtime1 -= t;
	irq_work_queue(&global_frame_info.irq);
}

/* G1ai: WEAK in the stock (readelf: FUNC WEAK): that is why the stock never inlines or CSEs it (two calls in flt_cal_freq) */
__weak u64 get_frame_period(void)
{
	return GFI_U64(0x58);
}

void flt_force_maxfreq_break(bool v)
{
	GFI_U8(4) = v;
}

void flt_maxfreq_break(bool v)
{
	if (glk_fbreak_enable)
		GFI_U8(5) = !v;
}

void flt_minfreq_break(bool v)
{
	if (glk_fbreak_enable)
		GFI_U8(6) = !v;
}

void flt_update_fps_time(s64 vsync)
{
	static DEFINE_RATELIMIT_STATE(_rs, 5 * HZ, 10);

	if (vsync < 4000000 || vsync > 50000000) {
		if (__ratelimit(&_rs))
			pr_warn("flt: flt: invalid vsyncPeriod %lld\n", vsync);
	} else {
		u64 old = smp_load_acquire(&last_vsyncPeriod);

		frame_sf_info.vsyncPeriod = vsync;
		if (!flt_vsync_initialized) {
			last_vsyncPeriod = vsync;
			flt_vsync_initialized = 1;
		} else if (old != vsync) {
			last_vsyncPeriod = vsync;
			flt_target_delta = comp_target_vsync(flt_target_delta_orig);
		}
	}
}

struct flt_slot {
	u8 pad[0x60];
	u64 start;
	u64 period;
	u8 pad2[8];
};
#define GFI_SLOT(i)	((struct flt_slot *)((u8 *)&global_frame_info + (i) * sizeof(struct flt_slot)))

static __always_inline u64 adjust_slowpath_body(u64 now, u64 *period)
{
	int idx = global_frame_info.cur_idx;
	int i;

	for (i = idx; i > 0; i--) {
		*period = FLT_ITEM(i).frame_period;
		if (FLT_ITEM(i).frame_start < now)
			return FLT_ITEM(i).frame_start;
	}
	for (i = 20; i > idx; i--) {
		*period = FLT_ITEM(i).frame_period;
		if (FLT_ITEM(i).frame_start < now)
			return FLT_ITEM(i).frame_start;
	}
	return 0;
}

noinline u64 adjust_frame_start_slowpath(u64 now, u64 *period)
{
	return adjust_slowpath_body(now, period);
}

u64 adjust_frame_start(u64 start, u64 *period, u64 now)
{
	if (likely(now >= start))
		return start;
	if (unlikely(*period > start))
		*period = 0;
	return adjust_frame_start_slowpath(now, period);
}

static int set_flt_cur_cal_cid(const char *val, const struct kernel_param *kp)
{
	unsigned int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	if (v <= 4)
		cal_cur_cid = v;
	return 0;
}

static int get_flt_cur_cal_cid(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "cal_cur_cid:%d\n", cal_cur_cid);
}

static int set_flt_target_check(const char *val, const struct kernel_param *kp)
{
	int v = 0;

	if (sscanf(val, "%u\n", &v) != 1)
		return -EINVAL;
	flt_target_delta_orig = v;
	flt_target_delta = comp_target_vsync(v);
	return 0;
}

static int get_flt_target_check(char *buffer, const struct kernel_param *kp)
{
	return snprintf(buffer, PAGE_SIZE, "%d \n", flt_target_delta);
}

static const struct kernel_param_ops param_ops_flt_cur_cal_cid = { .set = set_flt_cur_cal_cid, .get = get_flt_cur_cal_cid };
module_param_cb(flt_cur_cal_cid, &param_ops_flt_cur_cal_cid, NULL, 0644);
static const struct kernel_param_ops param_ops_flt_target_check = { .set = set_flt_target_check, .get = get_flt_target_check };
module_param_cb(flt_target_delta, &param_ops_flt_target_check, NULL, 0644);

int proc_flt_dointvec(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos,
			     proc_handler *handler)
{
	static DEFINE_MUTEX(mutex);
	void *data = table->data;
	int ret, size;
	void *tmp;

	mutex_lock(&mutex);
	if (!handler)
		handler = proc_dointvec;
	if (write) {
		size = table->maxlen;
		tmp = kzalloc(size, GFP_KERNEL);
		if (!tmp) {
			ret = -ENOMEM;
			goto out;
		}
		memcpy(tmp, data, table->maxlen);
		ret = handler(table, write, buffer, lenp, ppos);
		if (ret)
			memcpy(data, tmp, table->maxlen);
		kfree(tmp);
	} else {
		ret = handler(table, write, buffer, lenp, ppos);
	}
out:
	mutex_unlock(&mutex);
	return ret;
}

static int proc_flt_dotload(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = proc_flt_dointvec(table, write, buffer, lenp, ppos, NULL);

	if (GFI_U32(0x28)) {
		u32 *p = &GFI_U32(0x2c);
		int i;

		for (i = 0; i < 4; i++)
			pr_err("flt: cid %d minload %d maxload %d\n", i, p[i + 4], p[i]);
	}
	return ret;
}

static int proc_flt_dofreq(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = proc_flt_dointvec(table, write, buffer, lenp, ppos, NULL);

	if (GFI_U32(0x28)) {
		int i;

		for (i = 0; i < 4; i++)
			pr_err("flt: cid %d minfreq %d maxfreq %d\n", i, glk_minfreq[i], glk_maxfreq[i]);
	}
	return ret;
}

/* ---- procfs views (first copy of the pair lives in mi_game_load.c) ---- */
struct cpumask cluster_cpumask[2];
static int cluster_number __used;
static const char cluster_char[4] = "lmb";
struct frame_load {
	spinlock_t loading_slock;
	u64 rt[3];
	int f32;
};
static DEFINE_PER_CPU(struct frame_load, load_info);

static void dump_his_load_info(int idx, struct seq_file *m)
{
	struct frame_load_items *it = &global_frame_info.items[idx % 21];
	int c;

	if (!it->frame_period)
		return;
	seq_printf(m, "frame period %llu:  \n", it->frame_period);
	for (c = 0; c < cluster_number; c++) {
		seq_printf(m, "[%c]: [flt_util %llu | freq %lu | \t", cluster_char[c], it->cap_states[c].cpu_util,
			   it->cap_states[c].freq);
		seq_printf(m, "avg:%9u, sum_avg %9u]", it->avg_freq[c], it->sum_avg_freq[c]);
		seq_printf(m, "real_util %9llu |real_freq %9u]\n", it->real_util[c], it->real_freq[c]);
	}
	seq_printf(m, "\n");
}

static int history_load_show(struct seq_file *m, void *v)
{
	int idx = global_frame_info.cur_idx;
	int i;

	for (i = idx + 1; i < 21; i++)
		dump_his_load_info(i, m);
	for (i = 0; i <= idx; i++)
		dump_his_load_info(i, m);
	return 0;
}

static int history_load_open(struct inode *inode, struct file *file)
{
	return single_open(file, history_load_show, NULL);
}

static int cpu_load_show(struct seq_file *m, void *v)
{
	unsigned int idx = global_frame_info.cur_idx;
	u64 window = global_frame_info.window;
	int c, cpu;

	seq_printf(m, "Period :%d\n", (int)global_frame_info.items[idx ? idx - 1 : 20].frame_period);
	for (c = 0; c < cluster_number; c++) {
		seq_printf(m, "---cluster %d--- \n", c);
		for_each_cpu(cpu, &cluster_cpumask[c]) {
			if (window <= 1)
				window = 1;
			/* g62a: the stock reads rt[0] twice (once for the debug printk, again for the division: S+118 / S+f0) */
			if (global_frame_info.debug)
				printk(KERN_ERR "flt: sum_exec_runtime:%llu,%lu", per_cpu(load_info, cpu).rt[0], (unsigned long)window);
			seq_printf(m, "%9lu \t", (unsigned long)div_u64(per_cpu(load_info, cpu).rt[0], window));
		}
		seq_printf(m, "\n");
	}
	return 0;
}

static int cpu_load_open(struct inode *inode, struct file *file)
{
	return single_open(file, cpu_load_show, NULL);
}

static const struct proc_ops history_load_fops = {
	.proc_open = history_load_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static const struct proc_ops cpu_load_fops = {
	.proc_open = cpu_load_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

void create_frame_load_proc(void *rootdir_)
{
	struct proc_dir_entry *rootdir = rootdir_;
	if (!rootdir)
		return;
	global_frame_info.dir = proc_mkdir("flt", rootdir);
	if (!global_frame_info.dir)
		return;
	global_frame_info.history = proc_create("frame_history_load", 0664, global_frame_info.dir, &history_load_fops);
	global_frame_info.cpu = proc_create("frame_cpu_load", 0664, global_frame_info.dir, &cpu_load_fops);
}

void delete_frame_load_proc(void)
{
	if (!global_frame_info.dir)
		return;
	if (global_frame_info.cpu)
		proc_remove(global_frame_info.cpu);
	if (global_frame_info.history)
		proc_remove(global_frame_info.history);
	proc_remove(global_frame_info.dir);
}

static struct ctl_table frame_load_table[] __used = {
	{ .procname = "glk_disable", .data = &glk_disable, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "glk_freq_limit_walt", .data = &glk_freq_limit_walt, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "frame_hispeed_load", .data = &global_frame_info.hispeed_load, .maxlen = 16, .mode = 0644,
	  .proc_handler = proc_flt_dotload },
	{ .procname = "frame_lowspeed_load", .data = &global_frame_info.lowspeed_load, .maxlen = 16, .mode = 0644,
	  .proc_handler = proc_flt_dotload },
	{ .procname = "frame_minfreq_limit", .data = glk_minfreq, .maxlen = sizeof(glk_minfreq), .mode = 0644,
	  .proc_handler = proc_flt_dofreq },
	{ .procname = "freq_break_enable", .data = &glk_fbreak_enable, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "frame_maxfreq_limit", .data = glk_maxfreq, .maxlen = sizeof(glk_maxfreq), .mode = 0644,
	  .proc_handler = proc_flt_dofreq },
	{ .procname = "flt_debug", .data = &global_frame_info.debug, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
};

void mist_frame_load_exit(void)
{
	irq_work_sync(&global_frame_info.irq);
	unregister_plat_func(35);	/* G14c: ids 31..34 of the CN stock moved to 35..38 (4 hooks inserted in the global plat_common) */
	unregister_plat_func(36);
	unregister_plat_func(38);
	unregister_plat_func(37);
	unregister_sysctl_table(global_frame_info.header);
	global_frame_info.header = NULL;
}

/* ---- frequency residency accounting on every cpufreq transition (rvh hook, registered by mist_frame_load_init) ---- */
static unsigned int frame_time_num __used;
static u64 sum_freq_rt[2][2] __used;
static u64 last_freq_change_time[2] __used;
static unsigned int cur_record_freq[2] __used;

static void android_rvh_cpufreq_transition(void *unused, struct cpufreq_policy *policy)
{
	u64 now = walt_sched_clock();
	unsigned int cl = get_cluster_type(policy->cpu);

	if (cl > num_sched_clusters)
		cl = 0;
	sum_freq_rt[cl][frame_time_num] += (now - last_freq_change_time[cl]) * cur_record_freq[cl];
	last_freq_change_time[cl] = now;
	cur_record_freq[cl] = policy->cur;
	if (global_frame_info.debug == 4)
		pr_info("flt: [%d]%llu, %d, %s", cl, now, policy->cur, __func__);
}

/* ================= mist frame load (G1t, 17.10.2026): written from the OS4 disassembly, NOT hardware-tested ================= */
#include <linux/sched/cpufreq.h>

extern unsigned int flt_trigger_source, flt_normal_scaling[4], flt_boost_scaling[4];
extern unsigned int flt_in_frame_enable, flt_in_frame_up_margin, flt_in_frame_down_margin, flt_preboost_enable;
extern unsigned int migt_thresh __asm__("migt_thresh.llvm.597625021974665233") __attribute__((visibility("hidden")));
extern void flt_irq_work(struct irq_work *work);
extern void update_frame_load(struct task_struct *p, struct rq *rq, unsigned int fstat, u64 wallclock, u64 irqtime, int unused);
static void mi_force_update_freq(unsigned int freq);
extern void get_wakeup_info(struct task_struct *p);
extern struct mi_game_task *get_gt_task(struct task_struct *p);
extern u64 get_scale_exec_time(u64 delta, int cpu, void *wts);
extern bool is_new_task(struct task_struct *p);
extern int cpu_is_waiting_on_io(void *rq);
#define TASK_U64(p, off)	(*(u64 *)((u8 *)(p) + (off)))
#define TASK_U8(p, off)		(*(u8 *)((u8 *)(p) + (off)))
#define RQ_CPU(rq)		(*(int *)((u8 *)(rq) + 0xbb8))
#define TASK_WTS(p)		((void *)((u8 *)(p) + 0xbc0))
#define WTS_MARK_START(p)	TASK_U64(p, 0xbc8)
extern void update_flt_load(struct frame_load_items *cur);
/* the walt governor callback (struct waltgov_callback in the vendor tree; NOT update_util_data: the kCFI hash of func depends on the struct name) */
struct waltgov_callback {
	void (*func)(struct waltgov_callback *cb, u64 time, unsigned int flags);
};
DECLARE_PER_CPU(struct waltgov_callback *, waltgov_cb_data);
/* G14c: struct waltgov_cpu / waltgov_policy of the vendor walt governor (new import of the global stock); only the two fields read by
 * mi_force_update_freq are declared: waltgov_cpu.wg_policy (+8), walt_load.ws at +0x30, and waltgov_policy.last_ws at +8 (offsets PROVEN by the sched-walt printk, G14i review) */
struct waltgov_policy { u64 f0; u64 last_ws; };
struct waltgov_cpu { struct waltgov_callback *cb; struct waltgov_policy *wg_policy; u8 pad[0x20]; u64 walt_load_ws; };
DECLARE_PER_CPU(struct waltgov_cpu, waltgov_cpu);
/* waltgov_run_callback() of the vendor walt scheduler: the cpu index comes from rq->cpu */
static __always_inline void waltgov_run_callback(void *rq, unsigned int flags)
{
	struct waltgov_callback *cb = smp_load_acquire(&per_cpu(waltgov_cb_data, RQ_CPU(rq)));

	if (cb)
		cb->func(cb, walt_sched_clock(), flags);
}

extern struct tracepoint __tracepoint_android_rvh_cpufreq_transition;
extern int android_rvh_probe_register(struct tracepoint *tp, void *probe, void *data);
static u64 last_frame_period[2];
static unsigned int cal_cur_cpu, render_scaling;
static unsigned int preboostCheck, preboostStepSet, flt_in_preboost, flt_in_frame_boost;
static struct frame_load_items *focused_frame_load;
static unsigned int old_migt_thresh __used;
static u64 frame_tperiod __used;
static atomic_t boost_step;
static u64 boostCheckTime1, boostCheckTime2, target_runtime2, first_render_runtime, wakeup_delta;
static u64 ui_runtime_perframe, render_runtime_perframe, binder_time, new_render_frame;

/* migt_thresh (5..33 -> frame period in ns); stock .rodata table of 29 u64 */
static const u64 flt_tperiod_map[29] = {
	5405405, 6060606, 8333333, 8333333, 8333333, 8333333,
	11111111, 11111111, 11111111, 11111111, 11111111, 11111111,
	16666666, 16666666, 16666666, 16666666, 16666666, 16666666, 16666666, 16666666,
	25000000, 25000000, 25000000, 25000000, 25000000, 25000000, 25000000, 25000000, 25000000,
};

/* frequency clamp of the walt governor (called from xr_set_next_freq): per-cluster glk_maxfreq/glk_minfreq while frame boost is active */
unsigned int freq_limit_walt(struct cpufreq_policy *policy, unsigned int *freq)
{
	unsigned int cl, maxf, minf, f;

	if (!glk_freq_limit_walt || !glk_freq_limit_start || !GFI_U32(0x20))
		return *freq;
	cl = per_cpu(load_info, policy->cpu).f32;
	maxf = (GFI_U8(5) && !GFI_U8(4)) ? glk_maxfreq[cl] : policy->max;
	minf = GFI_U8(6) ? glk_minfreq[cl] : policy->min;
	if (!maxf)
		maxf = policy->max;
	f = max(*freq, max(policy->min, minf));
	f = min(maxf, f);
	*freq = f;
	return f;
}

void update_freq_limit(u64 frame_period)
{
}

void flt_irq_work(struct irq_work *work)
{
	struct walt_sched_cluster *cluster;
	int cpu;
	u64 now;

	if (list_empty(&cluster_head)) {
		printk_deferred("flt_irq_work: cluster_head is NULL\n");
		return;
	}
	if (smp_load_acquire(&GFI_U32(0)))
		return;
	GFI_U32(0) = 1;
	for_each_possible_cpu(cpu)
		raw_spin_lock((raw_spinlock_t *)per_cpu_ptr(&runqueues, cpu));
	now = walt_sched_clock();
	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		raw_spin_lock(&cluster->load_lock);
		for_each_cpu(cpu, &cluster->cpus) {
			void *rq = per_cpu_ptr(&runqueues, cpu);

			if (RQ_CURR(rq))
				walt_update_task_ravg(RQ_CURR(rq), rq, 4, now, 0);
		}
		raw_spin_unlock(&cluster->load_lock);
	}
	update_flt_load(focused_frame_load);
	new_render_frame++;
	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		struct cpumask online;
		int remaining;

		cpumask_and(&online, &cluster->cpus, cpu_online_mask);
		remaining = cpumask_weight(&online) - 1;
		for_each_cpu(cpu, &online) {
			void *rq = per_cpu_ptr(&runqueues, cpu);

			if (remaining)
				waltgov_run_callback(rq, 2);
			else
				waltgov_run_callback(rq, 0);
			remaining--;
		}
	}
	for_each_possible_cpu(cpu)
		raw_spin_unlock((raw_spinlock_t *)per_cpu_ptr(&runqueues, cpu));
	GFI_U32(0) = 0;
}

unsigned long get_frame_vutil(u64 delta)
{
	int tgt = flt_target_delta;
	int max_time = tgt / 100;
	int delta_ms = (int)div_u64(delta, 1000000);
	unsigned long vutil = 1024;
	int tmp = 0;

	if (tgt >= 100 && max_time >= delta_ms) {
		tmp = 1024U / max_time + delta_ms;
		if (tmp > max_time)	/* g64a: stock compares tmp > max_time (subs/b.le), no wrapped difference */
			vutil = (long)(int)((tmp - max_time) * delta_ms);
		else
			vutil = 0;
	}
	if (GFI_U32(0x28) == 33)
		trace_printk("get_frame_vutil flt_in_frame_down_margin=%d,max_time=%d,delta_ms=%d,delta=%llu,vutil=%lu,tmp=%d\n",
			     flt_in_frame_down_margin, max_time, delta_ms, delta, vutil, tmp);
	return vutil;
}

static void set_boost_step(void)
{
	u64 a = 0, b = 0, ratio;
	unsigned int ps = smp_load_acquire(&preboostStepSet);

	if (!ps)
		return;
	if (ps == 2) {
		a = render_runtime_perframe - first_render_runtime;
		b = target_runtime2;
	} else if (ps == 1) {
		a = render_runtime_perframe;
		b = target_runtime1;
		first_render_runtime = a;
	}
	ratio = b ? div64_u64(a * 1000, b) : 0;
	if ((int)ratio <= 499)
		atomic_add(2, &boost_step);
	else if ((unsigned int)ratio < 0x384)
		atomic_add(1, &boost_step);
	if (frame_sf_info.vsyncPeriod * frame_sf_info.buffer_count < (s64)(wakeup_delta << 1)) {
		atomic_set(&boost_step, 0);
		flt_in_preboost = 0;
	}
	preboostStepSet = 0;
	trace_flt_preboost_runtime_curinfo(ui_runtime_perframe, a, b, smp_load_acquire(&preboostStepSet),
					   smp_load_acquire(&boost_step.counter), wakeup_delta);
}

/* ---- G1w (17.10.2026): uclamp_frame_load + flt_cal_freq, from the OS4 disassembly. NOT hardware-tested. ---- */
extern u8 force_frame_boost, in_scroll;
extern unsigned int frame_boost_enable, disable_soc_util, flt_frame_divisor, flt_preboost_step;
extern unsigned int reasons_flag_enable[4], flt_in_frame_enable_cluster[4], flt_in_frame_freq[4], flt_cal_freq_enable[4];
extern unsigned int flt_preboost_cluster_enable[4];
extern int mi_dflt_get_latency_enable(void);
extern int mist_update_ddr_freq(int cluster, unsigned int freq);

/* a * b / 100 without overflowing for a up to 2^64 / b: the stock open-codes it exactly like this */
static inline u64 flt_mulpct(u64 a, u64 b)
{
	u64 q = a / 100;

	return q * b + (a - q * 100) * b / 100;
}

/* a * c / b, same quotient/remainder split */
static inline u64 flt_muldiv(u64 a, u64 b, u64 c)
{
	u64 q = a / b;

	return q * c + (a - q * b) * c / b;
}

/*
 * In-frame boost: per cluster compare the load of the running frame (rt[2]) with the one of the last frame (rt[0])
 * and decide a utilization floor (vutil based) + the boost reason (1..4, 0 = none). Sets force_frame_boost.
 */
u64 uclamp_frame_load(int cluster)
{
	u64 now, start, period, elapsed, lim, d10k, scaled = 0, max_cur, max_load;
	unsigned long util_cur = 0, util_prev = 0;
	u64 u_cur, u_prev, x;
	unsigned long vutil, cpu_util = 0, capacity = 0, use;
	unsigned int up, down, tdelta, elapsed_us;
	int i, cpu, reason, extra;

	if (!focused_frame_load)
		return 0;
	now = walt_sched_clock();
	start = FLT_ITEM(global_frame_info.cur_idx).frame_start;
	up = flt_in_frame_up_margin;
	tdelta = focused_frame_load->need_target_delta;
	down = flt_in_frame_down_margin;
	period = get_frame_period();
	if (cluster_number < 1)
		return 0;
	lim = ((u64)(up - down) + tdelta) * 10000;
	d10k = (u64)tdelta * 10000;
	elapsed = now - start;
	elapsed_us = (u32)(elapsed >> 10);
	for (i = 0; i < cluster_number; i++) {
		if (i != cluster)
			continue;
		max_cur = 0;
		max_load = 0;
		for_each_cpu(cpu, &cluster_cpumask[i]) {
			max_load = max(max_load, per_cpu(load_info, cpu).rt[0]);
			max_cur = max(max_cur, per_cpu(load_info, cpu).rt[2]);
			capacity = RQ_CAPACITY(per_cpu_ptr(&runqueues, cpu));
		}
		vutil = get_frame_vutil(elapsed);
		force_frame_boost = 0;
		reason = 0;
		extra = 0;
		x = 0;
		if (flt_trigger_source == 0) {
			if (period)
				scaled = flt_muldiv(max_load, period, d10k);
			u_cur = div_u64(max_cur, elapsed_us);
			u_prev = div_u64(max_load, (u32)global_frame_info.window);
			use = vutil;
			if (max_cur > max_load ||
			    (max_cur > flt_mulpct(max_load, flt_frame_divisor) && u_cur > u_prev)) {
				reason = 2;
			} else if (max_cur > scaled) {
				reason = 3;
				extra = 1;
			} else if (u_cur > u_prev && max_cur > flt_mulpct(scaled, flt_frame_divisor)) {
				reason = 3;
				extra = 1;
			} else if (elapsed > lim) {
				reason = 4;
				extra = 1;
				use = 1024;
			}
			if (reason) {
				force_frame_boost = 1;
				vutil = use;
				util_cur = u_cur;
				util_prev = u_prev;
				x = (u_cur > use / 2) ? max_t(u64, u_cur, use) : use / 2;
				if (extra || cluster + 1 == cluster_number)
					x += x >> 2;
			} else {
				util_cur = u_cur;
				util_prev = u_prev;
			}
		} else if (flt_trigger_source == 1) {
			if (max_cur > flt_mulpct(max_load, flt_frame_divisor)) {
				reason = 1;
				max_load = max(max_cur, max_load);
				u_prev = div_u64(max_load, (u32)global_frame_info.window);
				util_cur = div_u64(max_cur, (u32)global_frame_info.window);
				x = max_t(u64, u_prev, vutil);
				if (util_cur < vutil / 2)
					x = u_prev;
				force_frame_boost = 1;
			}
		}
		cpu_util = min_t(u64, capacity, x);
		if (global_frame_info.debug == 0x21) {
			trace_printk("uclamp_frame_load max_load=%llu , cpu_util=%lu, vutil=%lu ,cpu_util_cur=%lu capacity=%lu,index=%d, flt_trigger_source=%d\n",
				     max_load, cpu_util, vutil, util_cur, capacity, cluster, flt_trigger_source);
			printk(KERN_INFO "flt: uclamp_frame_load max_load=%llu , cpu_util=%lu, vutil=%lu ,cpu_util_cur=%lu capacity=%lu,index=%d, flt_trigger_source=%d\n",
			       max_load, cpu_util, vutil, util_cur, capacity, cluster, flt_trigger_source);
		}
		trace_flt_uclamp_frame_load_mark(cluster, cpu_util, vutil, util_cur, util_prev, max_cur, max_load, reason,
						 d10k, period, elapsed, lim);
	}
	return cpu_util;
}

static inline unsigned int flt_add_margin(unsigned int t)
{
	return t + (flt_fpercent_other ? t >> flt_fpercent_other : 0);
}

/* target frequency of a cluster from the frame load: called by the walt governor hook (xr_set_next_freq) for every update */
unsigned long flt_cal_freq(struct cpufreq_policy *policy, unsigned long cpu_util, unsigned long max,
			   unsigned long next_freq, unsigned int adaptive_low_freq, unsigned int adaptive_high_freq,
			   unsigned int reasons)
{
	/* g62a: the stock keeps target in a w register: next_freq is truncated to 32 bits (S+3e0/5b0, `mov w25,w25` at out) */
	unsigned int target = 0;
	unsigned long frame_load = 0, capacity;
	u64 max_load = 0, flt_load = 0, frame_period;
	unsigned int cl, last, lo, hi, low, avg, t, rs, max_freq;

	if (!global_frame_info.enable || !flt_target_fps)
		return 0;
	last = global_frame_info.cur_idx ? global_frame_info.cur_idx - 1 : 20;
	cl = per_cpu(load_info, policy->cpu).f32;
	max_freq = policy->cpuinfo.max_freq;	/* g62a: read once on entry in the stock (S+80) */
	if (flt_render_hook && cl == flt_render_cluster)
		return 0;
	FLT_ITEM(last).real_util[cl] = cpu_util;
	FLT_ITEM(last).real_freq[cl] = next_freq;
	if (smp_load_acquire(&flt_in_frame_boost) && flt_in_frame_enable_cluster[cl]) {
		if (global_frame_info.debug == 0x208)
			printk(KERN_ERR "flt: flt_in_frame_boost %s\n", __func__);
		return flt_in_frame_freq[cl];
	}
	if (flt_fence_enable && global_frame_info.fence_pending) {
		target = policy->min;
		goto out;
	}
	if (!flt_cal_freq_enable[cl]) {
		target = next_freq;
		goto after_clamp;
	}
	frame_period = FLT_ITEM(last).frame_period;
	max_load = FLT_ITEM(last).cap_states[cl].cpu_util;
	if (!max_load)
		max_load = FLT_ITEM(last ? last - 1 : 20).cap_states[cl].cpu_util;
	if (frame_boost_enable && (flt_trigger_source == 1 || (flt_trigger_source == 0 && in_scroll))) {
		frame_load = uclamp_frame_load(cl);
		if (!disable_soc_util) {
			if (force_frame_boost)
				max_load = max(frame_load, max_load);
		} else {
			if (force_frame_boost)
				cpu_util = max(frame_load, cpu_util);
			flt_load = cpu_util * 100 / max;
			t = flt_load * max_freq / 100;
			target = flt_add_margin(t);
			goto out;
		}
	}
	flt_load = max_load * 100 / max;
	if (frame_period > get_frame_period() + (get_frame_period() >> 1)) {
		if (global_frame_info.debug)
			printk(KERN_ERR "flt: frame period is %llu, set to max\n", frame_period);
	}
	if (flt_load > global_frame_info.hispeed_load[cl]) {
		if (global_frame_info.debug)
			printk(KERN_ERR "flt: flt_load, %llu, set to soft_maxfreq\n", flt_load);
	}
	if (flt_load < global_frame_info.lowspeed_load[cl]) {
		if (global_frame_info.debug)
			printk(KERN_ERR "flt: flt_load, %llu, set to soft_minfreq\n", flt_load);
	}
	t = flt_load * max_freq / 100;
	t = flt_add_margin(t);
	if (cal_cur_cid == cl && new_render_frame < flt_new_render_cal_thresh) {
		target = t;
	} else {
		switch (flt_choose_avg_num[cl]) {
		case 3:
			target = t;
			goto scale;
		case 2:
			avg = FLT_ITEM(last).sum_avg_freq[cl];
			break;
		default:
			avg = FLT_ITEM(last).avg_freq[cl];
			break;
		}
		target = t;
		if (avg && !(avg >= t || force_frame_boost)) {
			if (global_frame_info.debug == 3)
				pr_info("flt: cpu %d, cluster [%d]target %d > avg %d, new_render_frame %llu %s", policy->cpu, cl, t,
					avg, new_render_frame, __func__);
			target = avg;
		}
	}
scale:
	if (cal_cur_cid == cl) {
		t = target;
		rs = render_scaling;
		target = rs * (t >> 10) + ((rs * (t & 0x3ff)) >> 10);
	}
	low = flt_low_freq[cl];
	if (adaptive_high_freq - 1 <= 1199998) {
		lo = max(low, adaptive_low_freq);
		hi = max(low, adaptive_high_freq);
	} else {
		lo = hi = low;
		if (!low) {
			adaptive_low_freq = 0;
			adaptive_high_freq = low;
			goto no_clamp;
		}
	}
	adaptive_low_freq = lo;
	adaptive_high_freq = hi;
	target = ((unsigned int)target < lo) ? lo : max((unsigned int)target, hi);
no_clamp:
	if (global_frame_info.debug == 3)
		printk(KERN_ERR "flt: cpu %d, frame_load %llu, util %lu, target_freq %d,flt_load %llu max:%lu, capacity %lu, %s\t",
		       policy->cpu, max_load, cpu_util, (int)target, flt_load, max,
		       RQ_CAPACITY(per_cpu_ptr(&runqueues, policy->cpu)), __func__);
	if (flt_ftrace_debug == 0x2d4)
		trace_printk("cpu %d, frame_load=%llu, util=%lu, target_freq=%d,flt_load=%llu max=%lu, capacity=%lu\n", policy->cpu,
			     max_load, cpu_util, (int)target, flt_load, max, RQ_CAPACITY(per_cpu_ptr(&runqueues, policy->cpu)));
	FLT_ITEM(last).cap_states[cl].freq = (unsigned int)target;
after_clamp:
	if (flt_ftrace_debug == 0x309)
		trace_printk("cpu = %d, reasons = %u %s\n", policy->cpu, reasons, __func__);
	if (reasons_flag_enable[cl] && (reasons_flag & reasons)) {
		if (flt_ftrace_debug == 0x30a)
			trace_printk("cpu = %d, reasons = %u %s\n", policy->cpu, reasons, __func__);
		target = next_freq;
	}
	if (mi_dflt_get_latency_enable()) {
		int r = mist_update_ddr_freq(cl, target);

		target = flt_cal_freq_enable[cl] ? (r ? (unsigned int)r : (unsigned int)target) : 0;
	}
	if (flt_preboost_enable && flt_preboost_cluster_enable[cl] && smp_load_acquire(&flt_in_preboost) && target) {
		unsigned int cur, f, k;

		set_boost_step();
		cur = policy->min;
		f = ((unsigned int)target > cur) ? (unsigned int)target : cur;
		k = flt_preboost_step * smp_load_acquire(&boost_step.counter);
		k = k * frame_sf_info.buffer_count + 1024;
		target = (unsigned int)((f >> 10) * k + (u32)(((s64)(f & 0x3ff) * (s32)k) >> 10));
		trace_flt_preboost_freq(f, (unsigned int)target, smp_load_acquire(&boost_step.counter),
					frame_sf_info.buffer_count, flt_preboost_step);
	}
out:
	capacity = RQ_CAPACITY(per_cpu_ptr(&runqueues, policy->cpu));
	trace_flt_cal_freq_mark(policy->cpu, max_load, frame_load, cpu_util, target, flt_load, max, capacity,
				render_scaling, adaptive_low_freq, adaptive_high_freq, next_freq);
	return target;
}

struct frame_args {
	u64 frame_start;
	bool task_busy, cpu_busy, waiting_on_io;
};
static void update_cpu_frame_load(struct task_struct *p, void *rq, struct frame_args *fa, u64 wallclock,
				  u64 irqtime, int unused);

/*
 * walt-like per-rq frame accounting (G1t, from the stock disassembly): splits the exec time of task p on rq into the part that belongs
 * to the previous frame (mi_rq.prev) and the current one (mi_rq.cur) and publishes both in the per-cpu load_info.
 * c is the "case" number of the stock (only shown in the debug print).
 */
static void update_cpu_frame_load(struct task_struct *p, void *rq, struct frame_args *fa, u64 wallclock, u64 irqtime, int unused)
{
	int cpu = RQ_CPU(rq);
	struct task_struct *curr = *(struct task_struct **)((u8 *)rq + 0xaa8);
	u64 mark_start = WTS_MARK_START(p);
	u64 frame_start = fa->frame_start;
	u64 period = get_frame_period();
	bool new_task = is_new_task(p), cpu_busy;
	struct mi_game_task *gt;
	struct mi_rq *mrq;
	unsigned long flags;
	u64 d, d2, last;
	int c = 0;

	gt = get_gt_task(p);
	cpu_busy = fa->cpu_busy;
	if (flt_render_load && !gt)
		return;
	mrq = &per_cpu(mi_runqueues, cpu);
	if (frame_start > wallclock) {
		if (period > frame_start)
			period = 0;
		frame_start = adjust_frame_start_slowpath(wallclock, &period);
	}
	if (frame_start < mark_start) {
		/* the task last ran inside the current frame */
		if (cpu_busy) {
			d = irqtime;
			if (!irqtime || !(TASK_U8(p, 0x44) & 2) || cpu_is_waiting_on_io(rq))
				d = wallclock - mark_start;
			d = get_scale_exec_time(d, cpu, TASK_WTS(p));
			mrq->cur_frame_load.exec += d;
			if (new_task)
				mrq->nt_cur_frame_load.exec += d;
			c = 2;
		}
		goto publish;
	}
	if (mrq->frame_start != frame_start) {
		/* first update of this rq in the new frame: roll cur into prev */
		u64 cur = mrq->cur_frame_load.exec, nt_cur = mrq->nt_cur_frame_load.exec;

		mrq->cur_frame_load.exec = 0;
		mrq->nt_cur_frame_load.exec = 0;
		mrq->prev_frame_load.exec = cur;
		mrq->nt_prev_frame_load.exec = nt_cur;
		mrq->frame_start = frame_start;
		if (GFI_U32(0x28))
			printk(KERN_ERR "flt: new frame---> pid %3d comm %16s rq %d frame_start %llu frame_period %llu aging %d\n",
			       p->pid, p->comm, cpu, frame_start, period, frame_start - mark_start >= period);	/* G22b: stock prints w6 = cset hs of (frame_start - mark_start) >= period, not the constant 10 */
		c = 10;
	}
	if (!cpu_busy)
		goto publish;
	{
		u64 since = frame_start - mark_start;

		if (curr == p && irqtime && (TASK_U8(p, 0x44) & 2) && !cpu_is_waiting_on_io(rq)) {
			/* the stock re-tests the flag here and BUG()s (brk #0x800) when it is gone; no new-task accounting on this path */
#line 965 "kernel/sched/xiaomi_sched/qcom/mi_game/flt.c"
			BUG_ON(!(TASK_U8(p, 0x44) & 2));
#line 975 "flt.c"
			if (frame_start < wallclock - irqtime) {
				d = get_scale_exec_time(irqtime, cpu, TASK_WTS(p));
				mrq->cur_frame_load.exec += d;
				c += 6;
				goto publish;
			}
			d = get_scale_exec_time(min(frame_start - (wallclock - irqtime), period), cpu, TASK_WTS(p));
			mrq->prev_frame_load.exec += d;
			d2 = get_scale_exec_time(wallclock - frame_start, cpu, TASK_WTS(p));
			mrq->cur_frame_load.exec += d2;
			c += 7;
			goto publish;
		}
		if (curr == p) {
			d = get_scale_exec_time(min(since, period), cpu, TASK_WTS(p));
			c += since >= period ? 5 : 4;
		} else if (since >= period) {
			c += 3;
			d = get_scale_exec_time(period, cpu, TASK_WTS(p));
		} else {
			c += 2;
			d = get_scale_exec_time(since, cpu, TASK_WTS(p));
		}
		mrq->prev_frame_load.exec += d;
		if (new_task)
			mrq->nt_prev_frame_load.exec += d;
		d2 = get_scale_exec_time(wallclock - frame_start, cpu, TASK_WTS(p));
		mrq->cur_frame_load.exec += d2;
		if (new_task)
			mrq->nt_cur_frame_load.exec += d2;
	}
publish:
	raw_spin_lock_irqsave(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
	per_cpu(load_info, cpu).rt[0] = mrq->prev_frame_load.exec;
	per_cpu(load_info, cpu).rt[1] = mrq->nt_prev_frame_load.exec;
	per_cpu(load_info, cpu).rt[2] = mrq->cur_frame_load.exec;
	if (GFI_U32(0x28) == 5)
		printk(KERN_INFO "flt: rq-%d prev %10llu, cur %10llu[%d]", cpu, mrq->prev_frame_load.exec,
		       mrq->cur_frame_load.exec, c);
	if (flt_ftrace_debug == 0x1c6)
		trace_printk("rq-%d prev %10llu, cur %10llu[%d] \n", cpu, mrq->prev_frame_load.exec,
			     mrq->cur_frame_load.exec, c);
	raw_spin_unlock_irqrestore(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
	/* frame boost: the frame has been running longer than target + margin since the last update */
	last = GFI_U64(8);
	if (GFI_U64(0x10) >= last)
		return;
	if (wallclock < last) {
		if (GFI_U32(0x28))
			printk(KERN_ERR "flt: frame_period %llu wallclock %llu  frame_last_uptime %llu\n", period, wallclock, last);
		return;
	}
	if (flt_in_frame_enable) {
		u32 base = focused_frame_load->need_target_delta * 10000;
		u64 thr = (u64)(flt_in_frame_up_margin - (u64)flt_in_frame_down_margin) * 10000 + base;

		if (wallclock - last > thr) {
			GFI_U64(0x10) = wallclock;
			if (GFI_U32(0x28))
				printk(KERN_ERR "flt: walt adjust freq %llu %llu %llu\n", (u64)base, wallclock, last);
			flt_in_frame_boost = 1;
			irq_work_queue(&global_frame_info.irq);
		}
	}
}

/* per-task frame load roll: prev = min(cur + delta, period), cur = 0 */
static __always_inline void gt_roll(struct mi_game_task *gt, u64 delta, u64 period)
{
	u64 cur = gt->cur_frame_load + delta;

	if (cur > period)
		cur = period;
	gt->prev_frame_load = cur;
	gt->cur_frame_load = 0;
}

/* called from the walt task-ravg update (plat func 31): frame load bookkeeping of task p on rq */
void update_frame_load(struct task_struct *p, struct rq *rq, unsigned int fstat, u64 wallclock, u64 irqtime, int unused)
{
	struct frame_args fa;
	struct mi_game_task *gt;
	u64 frame_start, mark_start, period, d, bt;
	int cpu, idx;

	if (!GFI_U32(0x20) || !flt_target_fps || !GFI_U64(8))
		return;
	idx = global_frame_info.cur_idx;
	frame_start = FLT_ITEM(idx).frame_start;
	if (!frame_start)
		return;
	fa.frame_start = frame_start;
	fa.task_busy = (fstat >> 2) & 1;
	fa.cpu_busy = (fstat >> 1) & 1;
	fa.waiting_on_io = (fstat >> 3) & 1;
	/* g62a: the stock tests mark_start against the binder_time read of this test (S+c0/dc) and re-reads it for the value (S+e4) */
	if (flt_preboost_enable && (bt = binder_time)) {
		gt = get_gt_task(p);
		if (gt) {
			if (fa.task_busy) {
				u64 st = WTS_MARK_START(p) < bt ? binder_time : WTS_MARK_START(p);

				d = wallclock - st;
				if (gt->type & 0x100)
					ui_runtime_perframe += d;
				if (gt->type & 0xc0)
					render_runtime_perframe += d;
			}
			if (smp_load_acquire(&preboostCheck) == 0 && boostCheckTime1 < wallclock) {
				preboostCheck = 1;
				preboostStepSet = 1;
				flt_in_preboost = 1;
				irq_work_queue(&global_frame_info.irq);
			}
			if (smp_load_acquire(&preboostCheck) == 1 && boostCheckTime2 && boostCheckTime2 < wallclock) {
				preboostCheck = 2;
				preboostStepSet = 2;
				flt_in_preboost = 1;
				irq_work_queue(&global_frame_info.irq);
			}
		}
	}
	cpu = RQ_CPU(rq);
	mark_start = WTS_MARK_START(p);
	period = GFI_U64(0x58);
	gt = get_gt_task(p);
	if (gt) {
		if (frame_start > wallclock)
			frame_start = adjust_slowpath_body(wallclock, &period);
		if (fa.task_busy) {
			if (frame_start > mark_start) {
				if (!(TASK_U8(p, 0x44) & 2))
					gt_roll(gt, get_scale_exec_time(frame_start - mark_start, cpu, TASK_WTS(p)), period);
				d = get_scale_exec_time(wallclock - frame_start, cpu, TASK_WTS(p));
			} else {
				d = get_scale_exec_time(wallclock - mark_start, cpu, TASK_WTS(p));
			}
			gt->cur_frame_load = min(gt->cur_frame_load + d, period);
		} else if (frame_start > mark_start && !(TASK_U8(p, 0x44) & 2)) {
			gt_roll(gt, get_scale_exec_time(0, cpu, TASK_WTS(p)), period);
		}
	}
	update_cpu_frame_load(p, rq, &fa, wallclock, irqtime, unused);
}

/*
 * Frame average frequency of the frame that just ended (inlined into mist_frame_load_history_update in the stock).
 * Returns the "real" target delta of the current calibration cluster.
 */
static __always_inline int update_avg_freq(struct frame_load_items *cur, u64 now)
{
	u64 total = last_frame_period[0];
	int cnt = total ? 1 : 0;
	int real = 0, j;
	u32 prev = cur->need_target_delta;

	if (total && GFI_U32(0x28) == 33)
		printk(KERN_INFO "flt: last_frame_period %llu, counts %d %s", div_u64(total, 10000), 1, __func__);
	if (last_frame_period[1]) {
		total += last_frame_period[1];
		cnt++;
		if (GFI_U32(0x28) == 33)
			printk(KERN_INFO "flt: last_frame_period %llu, counts %d %s",
			       div_u64(last_frame_period[1], 10000), cnt, __func__);
	}
	for (j = 0; j < cluster_number; j++) {
		int sum_hm = cnt * flt_target_delta - (int)div_u64(total, 10000);
		int hm = prev - (int)div_u64(last_frame_period[frame_time_num], 10000);
		int d, delta_ms;
		u64 delta, s0, s1, sum_avg;
		u32 avg;

		if (GFI_U32(0x28) == 33)
			printk(KERN_INFO "flt: sum_history_margin %d, history_margin %d %s", sum_hm, hm, __func__);
		d = hm > flt_up_thresh_margin ? flt_target_delta + flt_up_thresh_margin :
		    (hm + flt_down_thresh_margin < 0 ? flt_target_delta - flt_down_thresh_margin : flt_target_delta + hm);
		if (j == cal_cur_cid && flt_choose_avg_num[j] == 1) {
			real = d;
			if (GFI_U32(0x28) == 33)
				printk(KERN_INFO "flt: cal_cur_cid %d, real_target_delta %d %s", cal_cur_cid, d, __func__);
		}
		delta = now - last_freq_change_time[j];
		last_freq_change_time[j] = now;
		sum_freq_rt[j][frame_time_num] += delta * cur_record_freq[j];
		if (GFI_U32(0x28) == 33)
			printk(KERN_INFO "flt: cur_record_freq[j] %d, delta %llu %s", cur_record_freq[j], delta, __func__);
		avg = div64_u64(sum_freq_rt[j][frame_time_num], (u64)(u32)d * 10000);
		cur->avg_freq[j] = avg;
		d = sum_hm > flt_up_thresh_margin ? flt_target_delta + flt_up_thresh_margin :
		    (sum_hm + flt_down_thresh_margin < 0 ? flt_target_delta - flt_down_thresh_margin : flt_target_delta + sum_hm);
		s0 = sum_freq_rt[j][0];
		s1 = sum_freq_rt[j][1];
		cnt = !!s0 + !!s1;	/* G48a: stock reuses w27 (cnt): cluster j >= 1 takes sum_history_margin with the count of cluster j-1 (madd S+228, cinc S+35c) */
		delta_ms = cnt * flt_target_delta;
		sum_avg = div64_u64(s0 + s1, (u64)((s64)delta_ms * 10000));
		cur->sum_avg_freq[j] = min(avg, (u32)sum_avg);
		if (GFI_U32(0x28) == 33)
			printk(KERN_INFO "flt: avg_freq: %d , sum_avg_freq: %d, %s, j: %d, real_target_delta %d", avg,
			       (u32)sum_avg, __func__, j, d);
		if (real == 0 && j == cal_cur_cid)
			real = d;
	}
	return real;
}


extern unsigned int flt_wakeup_enable;
static u8 is_first_wakeup __used;

/* roll the per-cpu frame runtimes of the focused frame into cap_states[].cpu_util (cluster max, scaled to the target frame) */
void update_flt_load(struct frame_load_items *cur)
{
	unsigned int scale;
	u64 window, quot, rem;
	unsigned long capacity = 0; /* G20b: the stock keeps the last cpu's capacity across clusters (x21 is not reset per cluster) */
	int c, cpu;

	if (smp_load_acquire(&flt_in_frame_boost))
		return;
	if (cluster_number < 1)
		return;
	window = GFI_U64(0x50);
	scale = mult_frac(1024U, window, (u32)(cur->need_target_delta * 10)); /* stock: quot/rem split (udiv + msub), not div64 */
	quot = (int)scale / 1024;
	rem = (int)scale % 1024;
	for (c = 0; c < cluster_number; c++) {
		u64 max_load = 0, util;
		unsigned long cpu_util;

		for_each_cpu(cpu, &cluster_cpumask[c]) {
			u64 load = per_cpu(load_info, cpu).rt[0];

			capacity = RQ_CAPACITY(per_cpu_ptr(&runqueues, cpu));
			max_load = max(max_load, load);
			if (GFI_U32(0x28))
				printk(KERN_ERR "flt: cpu %i capacity %lu cpu_load %llu max_load %llu cpu_util %llu \t", cpu,
				       capacity, load, max_load, div_u64(load, (u32)GFI_U64(0x50)));
			if (flt_ftrace_debug == 0x220) /* G20b: stock: printk and trace_printk are independent (the printk path falls into the 0x220 test) */
				trace_printk("cpu %i capacity %lu cpu_load %llu max_load %llu cpu_util %llu \n", cpu, capacity,
					     load, max_load, div_u64(load, (u32)GFI_U64(0x50)));
		}
		/* G23a D2: stock re-reads the window (GFI+0x50) at each use (printk f13c/f1c0, util f258); `window` is only the scale input */
		util = div_u64(max_load, (u32)GFI_U64(0x50));
		if (flt_trigger_source == 1)
			util = util * quot + ((util * rem) >> 10);
		cpu_util = min_t(u64, capacity, util);
		cur->cap_states[c].cpu_util = cpu_util;
		if (GFI_U32(0x28))
			printk(KERN_ERR "flt: cluter %d cpu_util %lu, target_frame_sclaing %d\t", c, cpu_util, scale);
		trace_update_flt_load_mark(c, cpu_util, max_load, GFI_U64(0x58));
	}
}

/* plat func 33: first wakeup of a render task by a non-binder waker -> remember how far we are from the preboost check time */
void get_wakeup_info(struct task_struct *p)
{
	struct mi_game_task *gt;
	u64 now;

	if (!flt_wakeup_enable)
		return;
	gt = get_gt_task(p);
	if (!gt || !(gt->type & 0xc0))
		return;
	now = walt_sched_clock();
	if (!strncmp(current->comm, "binder", 6))
		return;
	if (!is_first_wakeup)
		return;
	wakeup_delta = boostCheckTime1 - now;
	trace_flt_wakeup_info(wakeup_delta, boostCheckTime1, now, p->pid, current->pid);
	is_first_wakeup = 0;
}

/* plat func 34: poke the walt governor of the first cpu of every cluster */
static void mi_force_update_freq(unsigned int flags)
{
	struct walt_sched_cluster *cluster;

	if (!GFI_U32(0x20) || !flt_target_fps || !force_update_freq)
		return;
	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		raw_spinlock_t *lock;
		struct waltgov_cpu *wgc;
		struct waltgov_policy *wgp;
		int cpu;

		cpu = cpumask_first(&cluster->cpus);
		if (cpu >= nr_cpu_ids)
			continue;
		wgc = per_cpu_ptr(&waltgov_cpu, cpu);	/* G14c: skip a cluster whose governor is not set up or whose cpu window (walt_load.ws) is older than the policy last_ws */
		wgp = smp_load_acquire(&wgc->wg_policy);
		if (!wgp)
			continue;
		if ((s64)(smp_load_acquire(&wgc->walt_load_ws) - smp_load_acquire(&wgp->last_ws)) < 0)
			continue;
		lock = (raw_spinlock_t *)per_cpu_ptr(&runqueues, cpu);
		if (lock == (raw_spinlock_t *)this_cpu_ptr(&runqueues)) {
			waltgov_run_callback(lock, flags);
			continue;
		}
		if (!raw_spin_trylock(lock))
			continue;
		waltgov_run_callback(lock, flags);
		raw_spin_unlock(lock);
	}
}

void mist_frame_load_history_update(int trigger_source)
{
	u64 now = walt_sched_clock();
	struct frame_load_items *cur;
	int idx, real, c;
	u64 period;

	if (!flt_enable())
		return;
	GFI_U64(8) = now;
	idx = global_frame_info.cur_idx;
	flt_trigger_source = trigger_source;
	cur = &global_frame_info.items[idx];
	focused_frame_load = cur;
	cal_cur_cpu = raw_smp_processor_id();
	if (!cur->frame_start) {
		cur->frame_start = now;
		cur->need_target_delta = flt_target_delta;
		cal_cur_cid = cpu_to_cluster(cal_cur_cpu);
		return;
	}
	period = now - cur->frame_start;
	cur->frame_period = period;
	if (flt_gpu_enable && GFI_U64(0xa78)) {
		u64 gpu = GFI_U64(0xa78), adj = period;

		if (period > gpu) {
			adj = period - gpu;
			cur->frame_period = adj;
		}
		trace_flt_calibrate_frame_period(period, gpu, adj);
	}
	last_frame_period[frame_time_num] = cur->frame_period;
	last_cal_cur_cid = cal_cur_cid;
	cal_cur_cid = cpu_to_cluster(cal_cur_cpu);
	GFI_U64(0x58) = cur->frame_period;
	GFI_U64(0x50) = cur->frame_period >> 10;
	if (cur->frame_period < 0x400)
		return;
	if (GFI_U32(0x28) == 3)
		pr_info("flt: cal-cid[%d] last_cal_cur_cid : %d, cal_cur_cpu:%d, flt_trigger_source:%d, %s", cal_cur_cid,
			last_cal_cur_cid, cal_cur_cpu, flt_trigger_source, __func__);
	real = update_avg_freq(cur, now);
	if (GFI_U32(0x28) == 33)
		pr_info("flt: cal-cid[%d]: real_target_delta:%d, %s", cal_cur_cid, real, __func__);
	if (flt_ftrace_debug == 0x115)
		trace_printk("last_frame_period, %lu", (unsigned long)GFI_U64(0x58));
	idx = (global_frame_info.cur_idx + 1) % 21;
	global_frame_info.cur_idx = idx;
	cur = &global_frame_info.items[idx];
	for (c = 0; c < cluster_number; c++) {
		cur->cap_states[c].cpu_util = 0;
		cur->real_util[c] = 0;
		cur->real_freq[c] = 0;
		cur->avg_freq[c] = 0;
		cur->sum_avg_freq[c] = 0;
	}
	cur->frame_period = GFI_U64(0x58);
	cur->need_target_delta = real;
	cur->frame_start = now;
	frame_time_num = ~frame_time_num & 1;
	sum_freq_rt[0][frame_time_num] = 0;
	sum_freq_rt[1][frame_time_num] = 0;
	preboostCheck = 2;
	flt_in_frame_boost = 0;
	flt_in_preboost = 0;
	render_scaling = flt_normal_scaling[cal_cur_cid];
	irq_work_queue(&global_frame_info.irq);
}

void mist_frame_load_reset(void)
{
	unsigned long flags;
	int i, c, cpu, k;

	if (!flt_enable())
		return;
	for (i = 0; i < 21; i++) {
		struct frame_load_items *it = &global_frame_info.items[i];

		it->frame_start = 0;
		it->frame_period = 0;
		it->need_target_delta = 0;
		for (c = 0; c < cluster_number; c++) {
			it->cap_states[c].cpu_util = 0;
			it->cap_states[c].freq = 0;
			it->cap_states[c].max_freq = 0;
			it->real_util[c] = 0;
			it->real_freq[c] = 0;
			it->avg_freq[c] = 0;
			it->sum_avg_freq[c] = 0;
		}
	}
	cur_record_freq[0] = cur_record_freq[1] = 0;
	last_freq_change_time[0] = last_freq_change_time[1] = 0;
	sum_freq_rt[0][0] = sum_freq_rt[0][1] = sum_freq_rt[1][0] = sum_freq_rt[1][1] = 0;
	last_frame_period[0] = last_frame_period[1] = 0;
	if (old_migt_thresh != migt_thresh || !frame_tperiod) {
		unsigned int t = migt_thresh - 5;

		old_migt_thresh = migt_thresh;
		frame_tperiod = t < 29 ? flt_tperiod_map[t] : 33333333;
	}
	GFI_U64(0x58) = (u32)frame_tperiod;
	GFI_U64(0x50) = (u32)frame_tperiod >> 10;	/* G20b: stock ubfx #10,#22 = 32-bit shift */
	global_frame_info.cur_idx = 0;
	for (c = 0; c < cluster_number; c++) {
		for_each_cpu(cpu, &cluster_cpumask[c]) {
			raw_spin_lock_irqsave(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
			for (k = 0; k < 3; k++)
				per_cpu(load_info, cpu).rt[k] = 0;
			raw_spin_unlock_irqrestore(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
		}
	}
	GFI_U64(8) = 0;
}

int mist_frame_load_init(void)
{
	struct cpumask rest, mask;
	int cpu, c, n = 0;

	GFI_U8(4) = 0;
	GFI_U8(5) = 1;
	GFI_U8(6) = 1;
	global_frame_info.gpu_time = 0;
	global_frame_info.fence_pending = 0;
	for (c = 0; c < 4; c++) {
		global_frame_info.hispeed_load[c] = 90;
		global_frame_info.lowspeed_load[c] = 10;
	}
	cpumask_copy(&rest, cpu_possible_mask);
	for_each_cpu(cpu, &rest) {
		cpumask_clear(&mask);
		mi_cpu_coregroup_mask(cpu, &mask);
		if (cpumask_first(&mask) != cpu)
			continue;
		for_each_cpu(c, &mask) {
			struct mi_rq *rq = &per_cpu(mi_runqueues, c);

			per_cpu(load_info, c).f32 = n;
			rq->prev_frame_load.exec = 0;
			rq->cur_frame_load.exec = 0;
			rq->nt_prev_frame_load.exec = 0;
			rq->nt_cur_frame_load.exec = 0;
			rq->vip_tasks[0] = rq->vip_tasks[1] = 0;
			per_cpu(load_info, c).loading_slock = __SPIN_LOCK_UNLOCKED(loading_slock);
		}
		cluster_cpumask[n] = mask;
		cpumask_andnot(&rest, &rest, &mask);
		n++;
	}
	cluster_number = n;
	GFI_U32(0) = 0;
#line 2258 "kernel/sched/xiaomi_sched/qcom/mi_game/flt.c"
	WARN_ON(global_frame_info.header);
#line 1439 "flt.c"
	init_irq_work(&global_frame_info.irq, flt_irq_work);
	global_frame_info.header = register_sysctl_sz("migt", frame_load_table, 8);
	register_plat_func(35, update_frame_load);
	register_plat_func(36, flt_enable);
	register_plat_func(38, mi_force_update_freq);
	register_plat_func(37, get_wakeup_info);
	android_rvh_probe_register(&__tracepoint_android_rvh_cpufreq_transition, android_rvh_cpufreq_transition, NULL);
	GFI_U32(0x20) = 1;	/* G56a: the stock sets the enable word (+0x20) here, not +0 (x57: store to global_frame_info+32 vs +0) */
	printk(KERN_ERR "flt: frame load init success\n");
	return 0;
}

module_param(flt_fpercent_other, uint, 0644);
module_param(flt_ftrace_debug, uint, 0644);
module_param(flt_render_load, uint, 0644);
module_param(flt_deq_ajust_enable, uint, 0644);
module_param(flt_render_hook, uint, 0644);
module_param(force_update_freq, uint, 0644);
module_param(flt_render_cluster, uint, 0644);
module_param_array(flt_low_freq, uint, NULL, 0644);
module_param_array(flt_choose_avg_num, uint, NULL, 0644);
module_param(flt_new_render_cal_thresh, uint, 0644);
module_param(reasons_flag, uint, 0644);
module_param(flt_up_thresh_margin, int, 0644);
module_param(flt_down_thresh_margin, int, 0644);

/* ioctl 7 (dequeue): shrink render_scaling when the frame is close to its deadline */
void flt_adjust_dequeue(void)
{
	u64 now = walt_sched_clock();
	u64 period = get_frame_period();
	u64 dq, end, left;
	int cid;
	unsigned int sc, s2;

	if (!flt_deq_ajust_enable)
		return;
	if (focused_frame_load)
		dq = (u32)(focused_frame_load->need_target_delta * 10000);
	else
		dq = (s64)(int)(flt_target_delta * 10000);
	end = GFI_U64(8) + dq;
	if (end <= now)
		return;
	left = end - now;
	if (left >= dq)
		return;
	cid = cal_cur_cid;
	s2 = (u32)flt_muldiv(0x400, left, dq);
	sc = flt_boost_scaling[cid];
	render_scaling = (int)sc < (int)s2 ? sc : s2;
	if (GFI_U32(0x28))
		printk(KERN_ERR "flt: dequeue_delta %llu render_scaling %d  frame_period %llu, target_frame_time %llu\n", left,
		       render_scaling, period, dq);
}

/* ioctl 9: new surfaceflinger frame info: reset the preboost bookkeeping and the two boost check times */
void flt_update_sf(struct frame_sf *sf)
{
	u64 now, b1, b2, half, vs, tr;
	int bufs;

	if (!flt_preboost_enable)
		return;
	trace_flt_preboost_runtime_lastframeinfo(frame_sf_info.target_runtime, boostCheckTime1, boostCheckTime2,
						 render_runtime_perframe, ui_runtime_perframe, GFI_U64(0x58));
	frame_sf_info = *sf;
	now = walt_sched_clock();
	binder_time = now;
	preboostCheck = 0;
	render_runtime_perframe = 0;
	ui_runtime_perframe = 0;
	atomic_set(&boost_step, 0);
	wakeup_delta = 0;
	is_first_wakeup = 1;
	flt_in_preboost = 0;
	vs = frame_sf_info.vsyncPeriod;
	tr = frame_sf_info.target_runtime;
	bufs = frame_sf_info.buffer_count;
	b1 = tr + now;
	half = (s64)vs / 2;
	boostCheckTime1 = b1;
	target_runtime1 = tr;
	b2 = half + b1;
	target_runtime2 = half;
	boostCheckTime2 = b2;
	if ((s64)tr <= 0x1e847f && bufs == 1) {
		boostCheckTime1 = b2;
		target_runtime1 = b2 - now;
		boostCheckTime2 = 0;
	} else if (bufs >= 2) {
		u64 off = vs * (bufs - 1);

		boostCheckTime2 = b2 + off;
		boostCheckTime1 = b1 + off;
		target_runtime1 = tr + off;
		target_runtime2 = half + off;
	}
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Frame Loading Tracking by David");
