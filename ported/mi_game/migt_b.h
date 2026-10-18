/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: second part of the migt translation unit (G1x, 18.10.2026): boost / ceiling workers, cpufreq qos
 * requests, module-param callbacks, procfs/char-dev glue. Included at the end of params.c (same TU, so the stock's
 * file-local objects stay static). Logic recovered from the OS4 stock disassembly, NOT hardware-tested: DO NOT LOAD.
 */
#include <linux/cpufreq.h>
#include <linux/arch_topology.h>
#include <linux/fs.h>
#include <linux/pm_qos.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>

/* imports of sched-walt / glk (defined in mi_game_load.c) */
extern int core_ctl_set_boost(bool boost);
static int get_migt_flag(char *buf, const struct kernel_param *kp);
extern void update_migt_reserve_cpus(void);
extern void glk_maxfreq_break(bool v);
extern u8 force_maxfreq_break __asm__("force_maxfreq_break.llvm.963317314573916283") __attribute__((visibility("hidden")));
extern int last_traced_uid __asm__("last_traced_uid.llvm.597625021974665233") __attribute__((visibility("hidden")));
extern unsigned int migt_thresh __asm__("migt_thresh.llvm.597625021974665233") __attribute__((visibility("hidden")));
extern struct mi_game_task *get_gt_task(struct task_struct *p);
extern int game_ip_task(struct task_struct *p);
extern bool is_uuser_set_vip(struct task_struct *p);
extern bool is_uuser_set_ip(struct task_struct *p);
extern void user_set_game_vip(struct task_struct *p, bool set);
extern void user_set_game_ip(struct task_struct *p, bool set);

#define migt_err(fmt, ...)	printk(KERN_ERR "migt: " fmt, ##__VA_ARGS__)
/* the stock prints __LINE__ of its own source: keep the recovered numbers */
#define migt_err_at(line, fmt, ...)	printk(KERN_ERR "migt: " fmt, line, ##__VA_ARGS__)

struct migt {	/* BTF of the stock, 20 bytes, per cpu */
	int enabled;
	u32 migt_min;		/* +4: boost floor requested through qos_req_min */
	u32 boost_freq;		/* +8 */
	u32 migt_ceiling_max;	/* +12: ceiling requested through qos_req_max */
	u32 ceiling_freq;	/* +16 */
};

static DEFINE_PER_CPU(struct migt, migt_info);
static DEFINE_PER_CPU(struct freq_qos_request, qos_req_min);
static DEFINE_PER_CPU(struct freq_qos_request, qos_req_max);

struct workqueue_struct *migt_wq;
struct task_struct *migt_thread;
static unsigned int migt_boost_policy;		/* local in the stock (migt TU) */
static u8 migt_enabled;
static unsigned int boost_cycle;
static u8 ceiling_freq_limit_work;
static unsigned int ceiling_users[11];	/* [0..9] uids, [10] count */
static unsigned int migt_has_work_todo, migt_init_sucess, g_work;
static struct work_struct irq_boost_work;
static struct delayed_work migt_work, migt_rem, ceiling_work, ceiling_rem;

static struct cpumask cpus_visit;
static void finish_freq_qos_workfn(struct work_struct *work);
static DECLARE_WORK(finish_freq_qos_work, finish_freq_qos_workfn);
static int migt_freq_qos_init(struct notifier_block *nb, unsigned long event, void *data);
static struct notifier_block init_freq_qos_notifier = { .notifier_call = migt_freq_qos_init };

static __used enum hrtimer_restart do_boost_work(struct hrtimer *timer)
{
	if (!work_pending(&irq_boost_work))
		queue_work(migt_wq, &irq_boost_work);
	return HRTIMER_NORESTART;
}

/* push the per-cpu migt_info requests (min / ceiling) into the cpufreq qos requests of every cluster */
static __used noinline int update_policy_online(bool restore)
{
	struct cpumask visited;
	unsigned int cpu, cnt = 0;
	int err = 0;

	cpumask_clear(&visited);
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		struct cpu_topology *t = &cpu_topology[cpu];
		struct cpufreq_policy *policy;
		struct migt *mi;
		unsigned int c2;
		bool go;

		if (!cpu_online(cpu))
			err = 1;
		cpumask_clear(&visited);
		if (t->cluster_id != -1) {
			for_each_possible_cpu(c2)
				if (cpu_topology[c2].cluster_id == t->cluster_id)
					cpumask_set_cpu(c2, &visited);
		}
		if (cpumask_first_and(cpu_online_mask, &visited) != cpu)
			continue;
		if (migt_boost_policy - 2 < 2)
			go = cnt != 0;
		else if (migt_boost_policy == 1)
			go = cnt == 1;
		else
			go = false;
		if (!go && !restore) {
			cnt++;
			continue;
		}
		mi = per_cpu_ptr(&migt_info, cpu);
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			if (freq_qos_update_request(&per_cpu(qos_req_min, policy->cpu), mi->migt_min) < 0 ||
			    freq_qos_update_request(&per_cpu(qos_req_max, policy->cpu), mi->migt_ceiling_max) < 0) {
				cpufreq_cpu_put(policy);
				err = 1;
				break;
			}
			cpufreq_cpu_put(policy);
		}
		cnt++;
	}
	cpus_read_unlock();
	return err | !restore;
}

/* boost window start: raise the floor of every cluster, arm the end worker */
static __used void do_migt(struct work_struct *work)
{
	bool pending = cancel_delayed_work_sync(&migt_rem);
	unsigned int cpu;

	if (migt_debug)
		migt_err_at(839, "migt boost start....%d %llu", ktime_to_us(ktime_get()));
	vip_task_schedboost = 1;
	if (!pending)
		core_ctl_set_boost(true);
	glk_maxfreq_break(true);
	for_each_possible_cpu(cpu)
		per_cpu(migt_info, cpu).migt_min = per_cpu(migt_info, cpu).boost_freq;
	update_policy_online(false);
	queue_delayed_work(migt_wq, &migt_rem, msecs_to_jiffies(migt_ms));
	boost_cycle = cpu_boost_cycle;
}

/* boost window cycle end: re-arm while cycles are left, else drop the floor again */
static __used void do_migt_rem(struct work_struct *work)
{
	unsigned int cpu;
	int err;

	if (--boost_cycle >= 1) {
		queue_delayed_work(migt_wq, &migt_rem, msecs_to_jiffies(migt_ms));
		force_maxfreq_break = 1;
		return;
	}
	force_maxfreq_break = 0;
	if (migt_debug)
		migt_err_at(781, "migt boost end...%d %llu", ktime_to_us(ktime_get()));
	glk_maxfreq_break(false);
	for_each_possible_cpu(cpu)
		per_cpu(migt_info, cpu).migt_min = 0;
	err = update_policy_online(true);
	core_ctl_set_boost(false);
	if (err) {
		/* qos requests not registered yet: keep the boost and retry in 3750 jiffies */
		for_each_possible_cpu(cpu) {
			if (per_cpu(qos_req_min, cpu).pnode.prio == 0) {
				core_ctl_set_boost(true);
				queue_delayed_work(migt_wq, &migt_rem, 3750);
				break;
			}
		}
	}
	vip_task_schedboost = 0;
}

/* ceiling window start: only for the traced uid of the ceiling user list */
static __used void do_ceiling_limit(struct work_struct *work)
{
	unsigned int cpu;

	if (!ceiling_freq_limit_work) {
		unsigned int n = ceiling_users[10], i;
		bool found = false;

		if ((int)n < 1)
			return;
		if ((int)n > 10)
			n = 10;
		for (i = 0; i < n; i++) {
			if (ceiling_users[i] == last_traced_uid) {
				found = true;
				break;
			}
		}
		if (!found)
			return;
		ceiling_freq_limit_work = 1;
	}
	if (migt_debug)
		migt_err_at(1057, "migt trigger_ceiling_limit ....%d %llu\n", ktime_to_us(ktime_get()));
	if (ceiling_from_user) {
		for_each_possible_cpu(cpu)
			per_cpu(migt_info, cpu).migt_ceiling_max = per_cpu(migt_info, cpu).ceiling_freq;
		update_policy_online(false);
	}
}

static __used void do_ceiling_rem(struct work_struct *work)
{
	unsigned int cpu;

	if (ceiling_freq_limit_work != 1)
		return;
	if (migt_debug == 1)
		migt_err_at(949, "migt do ceiling_rem....%d %llu", ktime_to_us(ktime_get()));
	for_each_possible_cpu(cpu)
		per_cpu(migt_info, cpu).migt_ceiling_max = INT_MAX;
	update_policy_online(true);
	ceiling_freq_limit_work = 0;
}

static __used void finish_freq_qos_workfn(struct work_struct *work)
{
	cpufreq_unregister_notifier(&init_freq_qos_notifier, CPUFREQ_POLICY_NOTIFIER);
	migt_err("finish_freq_qos_workfn!");
}

/* cpufreq policy-create notifier: add one min and one max freq qos request per cpu, then retire the notifier */
static __used int migt_freq_qos_init(struct notifier_block *nb, unsigned long event, void *data)
{
	unsigned int cpu;
	int ret = 0;

	if (event != CPUFREQ_CREATE_POLICY)
		return 0;
	for_each_present_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (!policy) {
			migt_err("%s: Failed to get cpufreq policy for cpu%d\n", __func__, cpu);
			ret = -EAGAIN;
			goto unwind;
		}
		cpumask_andnot(&cpus_visit, &cpus_visit, policy->related_cpus);
		ret = freq_qos_add_request(&policy->constraints, &per_cpu(qos_req_min, cpu), FREQ_QOS_MIN, 0);
		if (ret < 0) {
			migt_err("%s: Failed to add min freq constraint (%d)\n", __func__, ret);
			cpufreq_cpu_put(policy);
			goto unwind;
		}
		ret = freq_qos_add_request(&policy->constraints, &per_cpu(qos_req_max, cpu), FREQ_QOS_MAX, INT_MAX);
		if (ret < 0) {
			migt_err("%s: Failed to add max freq constraint (%d)\n", __func__, ret);
			cpufreq_cpu_put(policy);
			goto unwind;
		}
		cpufreq_cpu_put(policy);
	}
	if (cpumask_empty(&cpus_visit))
		queue_work(system_wq, &finish_freq_qos_work);
	return 0;
unwind:
	for_each_present_cpu(cpu) {
		if (freq_qos_request_active(&per_cpu(qos_req_min, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_min, cpu));
		if (freq_qos_request_active(&per_cpu(qos_req_max, cpu)))
			freq_qos_remove_request(&per_cpu(qos_req_max, cpu));
	}
	return ret;
}

/* hand a command to the migt worker thread (g_work) */
__used noinline void add_work_to_migt(int cmd)
{
	if (READ_ONCE(migt_has_work_todo) || !READ_ONCE(migt_init_sucess)) {
		if (migt_debug_l)
			printk(KERN_ERR "migt-gtc: %s, %d, %d \n", __func__, 706, cmd);
		return;
	}
	g_work = cmd;
	migt_has_work_todo = 1;
	wake_up_process(migt_thread);
}

static noinline int exec_buckets_show(struct seq_file *m, void *v);

static noinline int exec_buckets_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, exec_buckets_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &last_traced_uid;
	return ret;
}

#include "migt_c.h"

/* /proc/migt/exec_buckets: per thread of the traced uid the 10 exec-time bucket counters of its mi_game_task + the role tag */
static noinline int exec_buckets_show(struct seq_file *m, void *v)
{
	int uid = *(int *)m->private;
	struct task_struct *p, *t;
	int i;

	rcu_read_lock();
	for_each_process(p) {
		if (task_uid(p).val != uid)
			continue;
		for_each_thread(p, t) {
			struct mi_game_task *gt = get_gt_task(t);
			const char *tag;

			if (!gt)
				continue;
			seq_printf(m, "%5d,%16s buckests:{", t->pid, t->comm);
			for (i = 0; i < 10; i++)
				seq_printf(m, " %8d", ((int *)gt->f124)[i]);
			if (game_super_task(t))
				tag = "sup ";
			else if (is_render_thread(t))
				tag = "ren ";
			else if (is_uuser_set_vip(t))
				tag = "uvip";
			else if (is_uuser_set_ip(t))
				tag = "uip ";
			else if (game_vip_task(t))
				tag = "vip ";
			else
				tag = game_ip_task(t) ? "imp " : "nor ";
			seq_printf(m, "  } %s  flag:%d\n", tag, gt->type);
		}
	}
	rcu_read_unlock();
	return 0;
}

/* ---- xr task accounting / render thread info (G1x) ---- */
#include <linux/sched/user.h>
#include <linux/sched/signal.h>
extern void restore_vip_prio(struct task_struct *p);
static void set_render_flag(struct task_struct *p, enum MI_RENDER_TYPE idx);
static void clean_render_flag(struct task_struct *p, enum MI_RENDER_TYPE idx);

/* the two render threads (queue / dequeue) of the traced game: info record with a seqcount for lockless readers */
struct render_info_s {
	unsigned int seq;
	spinlock_t lock;
	bool valid;
	int pid;
	unsigned long last_update;
	char name[16];
	u8 pad[24];
};
static struct render_info_s render_info[2];

__used noinline void reset_render_info(enum MI_RENDER_TYPE idx)
{
	struct render_info_s *ri = &render_info[idx];
	int old;

	spin_lock(&ri->lock);
	ri->seq++;
	smp_wmb();
	ri->valid = false;
	ri->last_update = 0;
	memset(ri->name, 0, sizeof(ri->name));
	old = ri->pid;
	ri->pid = -1;
	smp_wmb();
	ri->seq++;
	spin_unlock(&ri->lock);
	if (old >= 1) {
		struct task_struct *t;

		rcu_read_lock();
		t = find_task_by_vpid(old);
		if (t) {
			struct plat_task_struct *xr = get_xr_task_struct(t);

			if (XR_GAME_VALID(xr) && xr->game) {
				xr->game->type &= ~(0x40 << idx);
				restore_vip_prio(t);
			}
		}
		rcu_read_unlock();
	}
}

__used noinline void update_render_info(struct task_struct *p, enum MI_RENDER_TYPE idx)
{
	struct render_info_s *ri = &render_info[idx];
	int old;

	spin_lock(&ri->lock);
	ri->seq++;
	smp_wmb();
	if (ri->pid == p->pid) {
		smp_wmb();
		ri->seq++;
		spin_unlock(&ri->lock);
		return;
	}
	ri->valid = true;
	set_render_flag(p, idx);
	ri->valid = true;
	ri->last_update = jiffies;
	old = ri->pid;
	memset(ri->name, 0, sizeof(ri->name));
	ri->pid = p->pid;
	strncpy(ri->name, p->comm, strnlen(p->comm, 16));
	smp_wmb();
	ri->seq++;
	spin_unlock(&ri->lock);
	if (old >= 1 && old != ri->pid) {
		struct task_struct *t;

		rcu_read_lock();
		t = find_task_by_vpid(old);
		if (t)
			clean_render_flag(t, idx);
		rcu_read_unlock();
	}
}

extern unsigned int stask_prefer_cluster, render_prefer_cluster, vip_prefer_cluster, ip_prefer_cluster, force_viptask_select_rq;
extern int migt_enable(void);

/* should this task's wakeup be forced into its prefer cluster (overutilized cpu check bypass) */
__used noinline bool game_choose_cpu_overutiled(struct task_struct *p)
{
	struct plat_task_struct *xr;
	int en = migt_enable();

	if (!p || !en || !force_viptask_select_rq)
		return false;
	if (game_super_task(p) && (int)stask_prefer_cluster > 0)
		return true;
	xr = get_xr_task_struct(p);
	if (XR_GAME_VALID(xr) && xr->game && (xr->game->type & 0xc0) && (int)render_prefer_cluster > 0)
		return true;
	if ((int)force_viptask_select_rq <= 1) {
		if (is_uuser_set_vip(p) && (int)vip_prefer_cluster > 0)
			return true;
		if (!is_uuser_set_ip(p))
			return false;
	} else {
		if (game_vip_task(p) && (int)vip_prefer_cluster > 0)
			return true;
		if (!game_ip_task(p))
			return false;
	}
	return (int)ip_prefer_cluster > 0;
}

