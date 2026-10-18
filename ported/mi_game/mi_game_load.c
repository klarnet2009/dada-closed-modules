// SPDX-License-Identifier: GPL-2.0
/* mi_game: game load tracking (glk, mi_game_load.c): small helpers. PARTIAL, DO NOT LOAD. */
#include "mi_game.h"
#include <linux/fs.h>
#include <linux/irq_work.h>

#pragma clang diagnostic ignored "-Wmissing-prototypes"

extern int migt_enable(void);

unsigned int glk_disable = 1;
static u64 last_update_time __used;
static u8 glk_maxfreq_limit __used = 1;
static u8 glk_minfreq_limit __used = 1;
u8 force_maxfreq_break __asm__("force_maxfreq_break.llvm.963317314573916283") __attribute__((visibility("hidden")));

void glk_update_util(struct rq *rq, unsigned int flags)
{
}

int glk_enable(void)
{
	if (glk_disable)
		return false;
	if (sched_clock() - last_update_time > 999999999)
		return false;
	return migt_enable() > 2;
}

void glk_force_maxfreq_break(bool v)
{
	force_maxfreq_break = v;
}

void glk_maxfreq_break(bool v)
{
	if (glk_fbreak_enable)
		glk_maxfreq_limit = !v;
}

void glk_minfreq_break(bool v)
{
	if (glk_fbreak_enable)
		glk_minfreq_limit = !v;
}

/* ---- history / cpu load procfs views (second copy of the pair lives in flt.c) ---- */
struct game_runtime {
	u64 exec_time;
	u64 sum_exec_runtime;
};
struct game_load_info {
	struct game_runtime rt[5];
	u64 prev_idle_time, prev_wall_time;
	spinlock_t loading_slock;
	int cluster;
};
struct game_his {		/* game_load_history[21], 80 bytes */
	u64 util[4];
	u64 f20;
	u64 period;
	unsigned long freq[4];
};
static DEFINE_PER_CPU(struct game_load_info, load_info);
static struct game_his game_load_history[21] __used;
static int cur_history_items __used;
static int cluster_number __used;
static struct cpumask cluster_cpumask[4] __used;
static unsigned long cpu_util_freq_divisor __used;
static const char cluster_char[4] = "lmb";

static void dump_his_load_info(int idx, struct seq_file *m)
{
	int i = idx % 21;
	int c;

	if (!game_load_history[i].period)
		return;
	seq_printf(m, "period %llu: \n", game_load_history[i].period);
	for (c = 0; c < cluster_number; c++)
		seq_printf(m, "%c: %llu %lu\t\n", cluster_char[c], game_load_history[i].util[c], game_load_history[i].freq[c]);
	seq_printf(m, "\n");
}

static int history_load_show(struct seq_file *m, void *v)
{
	int idx = cur_history_items;
	int i;

	for (i = idx; i < 20;)
		dump_his_load_info(++i, m);
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
	int i, k, cpu;
	unsigned int idx = cur_history_items;

	seq_printf(m, "Period :%d\n", (int)game_load_history[idx ? idx - 1 : 20].period);
	for (i = 0; i < cluster_number; i++) {
		seq_printf(m, "---cluster %d--- \n", i);
		for_each_cpu(cpu, &cluster_cpumask[i]) {
			for (k = 0; k < 5; k++) {
				if (!cpu_util_freq_divisor)
					cpu_util_freq_divisor = 1;
				seq_printf(m, "%lu \t", (unsigned long)(per_cpu(load_info, cpu).rt[k].sum_exec_runtime / cpu_util_freq_divisor));
			}
		}
		seq_printf(m, "\n");
	}
	return 0;
}

static int cpu_load_open(struct inode *inode, struct file *file)
{
	return single_open(file, cpu_load_show, NULL);
}

static struct proc_dir_entry *game_load_dir, *history_load_entry, *cpu_load_entry;
static struct ctl_table_header *game_load_header;
static struct irq_work glk_cpufreq_irq_work;
static u8 game_load_has_init __read_mostly;
static int sysctl_game_load_choose_policy __read_mostly;
static unsigned int perf_mod __read_mostly;
static unsigned int game_load_debug;
static unsigned int glk_highspeed_load[4] = { 90, 90, 90, 90 };
static unsigned int glk_lowspeed_load[4] = { 30, 30, 30, 30 };
/* migt_thresh (hidden global of migt.c) -> frame time period (ms) table, the stock keeps it in .rodata */
extern unsigned int migt_thresh __asm__("migt_thresh.llvm.597625021974665233") __attribute__((visibility("hidden")));
static const u32 tperiod_map[26] = { 8, 8, 8, 11, 11, 11, 11, 11, 11, 16, 16, 16, 16, 16, 16, 16, 16,
				     25, 25, 25, 25, 25, 25, 25, 25, 25 };
static unsigned int old_migt_thresh __used;
static unsigned int frame_tperiod __used;

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

void create_game_load_proc(void *rootdir_)
{
	struct proc_dir_entry *rootdir = rootdir_;
	if (!rootdir)
		return;
	game_load_dir = proc_mkdir("glk", rootdir);
	if (!game_load_dir)
		return;
	history_load_entry = proc_create("game_history_load", 0664, game_load_dir, &history_load_fops);
	cpu_load_entry = proc_create("game_cpu_load", 0664, game_load_dir, &cpu_load_fops);
}

void delete_game_load_proc(void)
{
	if (!game_load_dir)
		return;
	if (cpu_load_entry)
		proc_remove(cpu_load_entry);
	if (history_load_entry)
		proc_remove(history_load_entry);
	proc_remove(game_load_dir);
}

int proc_glk_dointvec(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos,
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

static int proc_glk_dotload(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = proc_glk_dointvec(table, write, buffer, lenp, ppos, NULL);

	if (game_load_debug) {
		int i;

		for (i = 0; i < 4; i++)
			pr_info("glk: cid %d minload %d maxload %d\n", i, glk_lowspeed_load[i], glk_highspeed_load[i]);
	}
	return ret;
}

static int proc_glk_dofreq(struct ctl_table *table, int write, void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = proc_glk_dointvec(table, write, buffer, lenp, ppos, NULL);

	if (game_load_debug) {
		int i;

		for (i = 0; i < 4; i++)
			pr_info("glk: cid %d minfreq %d maxfreq %d\n", i, glk_minfreq[i], glk_minfreq[i]);
	}
	return ret;
}

static struct ctl_table game_load_table[] = {
	{ .procname = "game_load_choose_policy", .data = &sysctl_game_load_choose_policy, .maxlen = sizeof(int),
	  .mode = 0644, .proc_handler = proc_dointvec },
	{ .procname = "glk_disable", .data = &glk_disable, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "glk_freq_limit_walt", .data = &glk_freq_limit_walt, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "game_hispeed_load", .data = glk_highspeed_load, .maxlen = sizeof(glk_highspeed_load),
	  .mode = 0644, .proc_handler = proc_glk_dotload },
	{ .procname = "game_lowspeed_load", .data = glk_lowspeed_load, .maxlen = sizeof(glk_lowspeed_load),
	  .mode = 0644, .proc_handler = proc_glk_dotload },
	{ .procname = "game_minfreq_limit", .data = glk_minfreq, .maxlen = sizeof(glk_minfreq), .mode = 0644,
	  .proc_handler = proc_glk_dofreq },
	{ .procname = "freq_break_enable", .data = &glk_fbreak_enable, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "game_maxfreq_limit", .data = glk_maxfreq, .maxlen = sizeof(glk_maxfreq), .mode = 0644,
	  .proc_handler = proc_glk_dofreq },
	{ .procname = "perf_mod", .data = &perf_mod, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
	{ .procname = "load_debug", .data = &game_load_debug, .maxlen = sizeof(int), .mode = 0644,
	  .proc_handler = proc_dointvec },
};

/* account the delta of task p on cpu into the per-cpu game load buckets (all / game / super / vip / render) */
void game_load_update(struct task_struct *p, u64 delta, int cpu)
{
	unsigned long flags;
	bool game, vip, super, render;

	if (!(game_load_has_init & 1))
		return;
	game = game_task(p);
	vip = game_vip_task(p);
	super = game_super_task(p);
	render = is_render_thread(p);
	flags = 0;
	raw_spin_lock_irqsave(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
	per_cpu(load_info, cpu).rt[0].exec_time += delta;
	if (game)
		per_cpu(load_info, cpu).rt[1].exec_time += delta;
	if (super)
		per_cpu(load_info, cpu).rt[2].exec_time += delta;
	if (vip)
		per_cpu(load_info, cpu).rt[3].exec_time += delta;
	if (render)
		per_cpu(load_info, cpu).rt[4].exec_time += delta;
	raw_spin_unlock_irqrestore(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
}

void game_load_reset(void)
{
	unsigned long flags;
	int i, c, cpu, k;

	if (game_load_has_init != 1)
		return;
	for (i = 0; i < 21; i++) {
		for (c = 0; c < cluster_number; c++)
			game_load_history[i].util[c] = 0;
		game_load_history[i].f20 = 0;
		game_load_history[i].period = 0;
	}
	cur_history_items = 0;
	for (c = 0; c < cluster_number; c++) {
		for_each_cpu(cpu, &cluster_cpumask[c]) {
			raw_spin_lock_irqsave(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
			for (k = 0; k < 5; k++) {
				per_cpu(load_info, cpu).rt[k].exec_time = 0;
				per_cpu(load_info, cpu).rt[k].sum_exec_runtime = 0;
			}
			raw_spin_unlock_irqrestore(&per_cpu(load_info, cpu).loading_slock.rlock, flags);
		}
	}
}

/* irq_work: bring the walt window accounting of every running task up to date */
static void glk_irq_work(struct irq_work *work)
{
	struct walt_sched_cluster *cluster;
	int cpu, i;
	u64 now;

	if (game_load_has_init != 1)
		return;
	for_each_possible_cpu(cpu)
		raw_spin_lock((raw_spinlock_t *)per_cpu_ptr(&runqueues, cpu));
	now = sched_clock();
	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		raw_spin_lock(&cluster->load_lock);
		for_each_cpu(cpu, &cluster->cpus) {
			void *rq = per_cpu_ptr(&runqueues, cpu);

			if (RQ_CURR(rq))
				walt_update_task_ravg(RQ_CURR(rq), rq, 4, now, 0);
		}
		raw_spin_unlock(&cluster->load_lock);
	}
	i = 0;
	list_for_each_entry_rcu(cluster, &cluster_head, list)
		i += cpumask_weight_and(&cluster->cpus, cpu_online_mask);
	for_each_possible_cpu(cpu)
		raw_spin_unlock((raw_spinlock_t *)per_cpu_ptr(&runqueues, cpu));
}

/* called once per frame/period: roll the per-cpu buckets into the history ring and kick the freq irq_work */
void game_load_history_update(u64 unused)
{
	u64 now = sched_clock();
	u64 vals[5];
	int idx, c, cpu, k;

	if (game_load_has_init != 1)
		return;
	last_update_time = now;
	idx = cur_history_items;
	if (!game_load_history[idx].f20) {
		game_load_history[idx].f20 = now;
		return;
	}
	game_load_history[idx].period = now - game_load_history[idx].f20;
	if (old_migt_thresh != migt_thresh || !frame_tperiod) {
		unsigned int t = migt_thresh - 8;

		old_migt_thresh = migt_thresh;
		frame_tperiod = t < 26 ? tperiod_map[t] : 33;
	}
	cpu_util_freq_divisor = frame_tperiod << 10;
	for (c = 0; c < cluster_number; c++) {
		u64 max0 = 0, sum[5] = { 0 };

		for_each_cpu(cpu, &cluster_cpumask[c]) {
			for (k = 0; k < 5; k++) {
				u64 v = per_cpu(load_info, cpu).rt[k].exec_time;

				per_cpu(load_info, cpu).rt[k].exec_time = 0;
				per_cpu(load_info, cpu).rt[k].sum_exec_runtime = v;
				if (k == 0)
					max0 = max(max0, v);
				else
					sum[k] += v;
			}
		}
		vals[0] = max0;
		for (k = 1; k < 5; k++)
			vals[k] = sum[k];
		game_load_history[idx].util[c] = (int)(((u32)sysctl_game_load_choose_policy <= 4 ?
			vals[sysctl_game_load_choose_policy] : sum[2]) / cpu_util_freq_divisor);
	}
	idx = (idx + 1) % 21;
	cur_history_items = idx;
	for (c = 0; c < cluster_number; c++)
		game_load_history[idx].util[c] = 0;
	game_load_history[idx].f20 = now;
	game_load_history[idx].period = 0;
	irq_work_queue(&glk_cpufreq_irq_work);
}

/* ---- G1w (17.10.2026): xr_set_next_freq (plat func 0x11), from the OS4 disassembly. NOT hardware-tested. ---- */
extern unsigned int in_perf_mod, perf_min_freq[4];
extern int mist_perf_api_enable(void);
extern void mist_set_next_freq_hook(unsigned long a1, unsigned long a2, unsigned long a3, unsigned long *next_freq,
				    struct cpufreq_policy *policy, bool *force_update);
extern unsigned long flt_cal_freq(struct cpufreq_policy *policy, unsigned long cpu_util, unsigned long max,
				  unsigned long next_freq, unsigned int adaptive_low_freq, unsigned int adaptive_high_freq,
				  unsigned int reasons);

/* clamp f to [max(policy->min, glk minfreq), glk maxfreq] while the game-load frequency limiter is armed */
static unsigned int glk_limit_freq(struct cpufreq_policy *policy, unsigned int f)
{
	unsigned int cl, maxf, minf;

	if (!glk_freq_limit_walt || !glk_freq_limit_start || game_load_has_init != 1)
		return f;
	cl = per_cpu(load_info, policy->cpu).cluster;
	maxf = (glk_maxfreq_limit && !force_maxfreq_break) ? glk_maxfreq[cl] : policy->max;
	minf = glk_minfreq_limit ? glk_minfreq[cl] : policy->min;
	if (!maxf)
		maxf = policy->max;
	return min(maxf, max(max(policy->min, minf), f));
}

static void xr_set_next_freq(void *nouse, unsigned long a1, unsigned long a2, unsigned long a3, unsigned long *next_freq,
			     struct cpufreq_policy *policy, bool *force_update, unsigned int adaptive_low_freq,
			     unsigned int adaptive_high_freq, unsigned int reasons)
{
	unsigned long old, flt, f;
	unsigned int glk = 0, cl, last, a, b, idx;

	if (game_load_has_init != 1)
		return;
	old = *next_freq;
	if (!migt_enable()) {
		if (in_perf_mod) {
			cl = per_cpu(load_info, policy->cpu).cluster;
			adaptive_low_freq = max(perf_min_freq[cl], policy->min);
			if (game_load_debug)
				pr_info("glk: min_freq %lu\n", (unsigned long)adaptive_low_freq);
			*next_freq = max((unsigned long)adaptive_low_freq, *next_freq);
		}
		goto hook;
	}
	if (!glk_disable) {
		idx = cur_history_items;
		if (sched_clock() - last_update_time <= 999999999 && migt_enable() >= 3 && game_load_has_init == 1) {
			last = idx ? idx - 1 : 20;
			cl = per_cpu(load_info, policy->cpu).cluster;
			a = glk_maxfreq_limit ? glk_maxfreq[cl] : policy->max;
			b = glk_minfreq_limit ? glk_minfreq[cl] : policy->min;
			if (b >= a && game_load_history[last].period >= 1000000) {
				u64 period_ms = game_load_history[last].period / 1000000;
				unsigned long capacity = RQ_CAPACITY(per_cpu_ptr(&runqueues, policy->cpu));
				u64 pct;

				if (!glk_minfreq[cl])
					glk_minfreq[cl] = policy->min;
				if (period_ms > (u64)migt_thresh * 2) {
					glk = b;
					if (policy->cur >= b && perf_mod)
						glk = perf_mod;
				} else {
					glk = b;
					pct = game_load_history[last].util[cl] * 100 / capacity;
					if (pct <= glk_highspeed_load[cl]) {
						glk = a;
						if (pct >= glk_lowspeed_load[cl]) {
							unsigned int t = b + (b >> 3);

							glk = max(a, min(b, (unsigned int)((u64)pct * t / 100)));
							if (game_load_debug)
								pr_info("glk: glk_load %llu, target_freq %d\n", pct, glk);
						}
					}
				}
			}
		}
	}
	flt = flt_cal_freq(policy, a1, a3, *next_freq, adaptive_low_freq, adaptive_high_freq, reasons);
	if (!glk && !flt)
		f = glk_limit_freq(policy, *next_freq);
	else if (flt)
		f = glk_limit_freq(policy, flt);
	else
		f = glk;
	*next_freq = f;
hook:
	if (mist_perf_api_enable())
		mist_set_next_freq_hook(a1, a2, a3, next_freq, policy, force_update);
	if (*next_freq != old)
		*force_update = true;
}

int game_load_init(void)
{
	struct cpumask rest, mask;
	int cpu, c, n = 0;

	cpumask_copy(&rest, cpu_possible_mask);
	for_each_cpu(cpu, &rest) {
		if (n > 3)
			break;
		cpumask_clear(&mask);
		mi_cpu_coregroup_mask(cpu, &mask);
		if (cpumask_first(&mask) != cpu)
			continue;
		for_each_cpu(c, &mask) {
			per_cpu(load_info, c).cluster = n;
			per_cpu(load_info, c).loading_slock = __SPIN_LOCK_UNLOCKED(loading_slock);
		}
		cluster_cpumask[n] = mask;
		cpumask_andnot(&rest, &rest, &mask);
		n++;
	}
	cluster_number = n;
	BUG_ON(game_load_header);
	init_irq_work(&glk_cpufreq_irq_work, glk_irq_work);
	game_load_header = register_sysctl_sz("migt/glk", game_load_table, ARRAY_SIZE(game_load_table));
	register_plat_func(0x11, xr_set_next_freq);
	game_load_has_init = 1;
	printk(KERN_ERR "glk: game load init success\n");
	return 0;
}
EXPORT_SYMBOL_GPL(game_load_init);

void game_load_exit(void)
{
	game_load_has_init = 0;
	unregister_plat_func(0x11);
	irq_work_sync(&glk_cpufreq_irq_work);
	if (game_load_header)
		unregister_sysctl_table(game_load_header);
	game_load_header = NULL;
	pr_info("glk: game load exit success\n");
}
EXPORT_SYMBOL_GPL(game_load_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("vip-task detected by David");
