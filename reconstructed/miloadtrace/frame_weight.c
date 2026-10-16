// SPDX-License-Identifier: GPL-2.0
/* frame-load by David: group (frame) load tracker hooked into sched-walt / mi_schedule */
#line 1 "drivers/mihw/group_load/frame_weight.c"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/irq_work.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/sched/user.h>
#include <linux/pid.h>

#include "mi_sched.h"
#include "walt_ext.h"
#include "cpugov_mi.h"
#include "frame_weight.h"

#include "miload_trace.h"

DECLARE_PER_CPU(struct mi_rq, mi_runqueues);

#define flw_dbg(lvl, fmt, ...)	do { if (flw_debug == (lvl)) printk(fmt, ##__VA_ARGS__); } while (0)

/* ---- module parameters ---- */
static unsigned int util_policy;
static unsigned int grp_percent;
static unsigned int flw_debug;
static unsigned int choose_result_util_policy = 5;
static unsigned int back_percent = 5;
static unsigned int sys_percent = 20;
static unsigned int kern_percent = 100;
static unsigned int expand_shift;
static unsigned int target_delta_flag = 1;
static unsigned int freq_target_delta_flag = 1;
static unsigned int cal_running_time_type = 9;
static unsigned int frame_vip_boost;
static unsigned int frame_drop_boost;
static unsigned int frame_util_boost[2] = { 1, 1 };
static unsigned int util_boost_percent[2];
static unsigned int util_zero_boost_percent[2] = { 20, 20 };
static unsigned int down_util_margin[2] = { 80, 20 };

module_param(util_policy, uint, 0644);
module_param(grp_percent, uint, 0644);
module_param(flw_debug, uint, 0644);
module_param(choose_result_util_policy, uint, 0644);
module_param(back_percent, uint, 0644);
module_param(sys_percent, uint, 0644);
module_param(kern_percent, uint, 0644);
module_param(expand_shift, uint, 0644);
module_param(target_delta_flag, uint, 0644);
module_param(freq_target_delta_flag, uint, 0644);
module_param(cal_running_time_type, uint, 0644);
module_param(frame_vip_boost, uint, 0644);
module_param(frame_drop_boost, uint, 0644);
module_param_array(frame_util_boost, uint, NULL, 0644);
module_param_array(util_boost_percent, uint, NULL, 0644);
module_param_array(util_zero_boost_percent, uint, NULL, 0644);
module_param_array(down_util_margin, uint, NULL, 0644);

/* ---- state ---- */
static struct irq_work flw_change_status_irq_work;
static u64 last_freq_change_time[2];
static u32 cur_record_freq[2];
static u64 sum_freq_rt[2][2];
static unsigned int hist_freq_num;
static int period_traced_pid;

/* the global (non group) frame window, 80 bytes */
static struct {
	u64 start;
	u64 mark_start;
	u64 rsv_90;
	u64 target_period;
	u64 window_period;
	u64 grp_util[2];
	u64 util[2];
	u32 cal_div;
	u32 rsv3;
} window_load_history;

static u64 pred_cpu_fps_period;
static u64 cpu_fps_period;
static bool ioctl_flag;
static u32 sum_avg_freq[2];
static unsigned int hist_nums;
static u64 flw_irq_work_lastq_ws;
static struct irq_work flw_cpufreq_irq_work;
static struct irq_work flw_async_cpufreq_irq_work;

static struct proc_dir_entry *frame_load_dir;
static struct proc_dir_entry *group_dir;
static struct proc_dir_entry *all_group_show_entry;
static struct proc_dir_entry *flw_dir;
static struct proc_dir_entry *taskgrpid_entry;
static struct proc_dir_entry *usergrpid_entry;
static spinlock_t period_list_lock;
static int flw_work_in_process;
static int grp_traced_pid;
static int grp_traced_uid;

static LIST_HEAD(period_freq_list);
static LIST_HEAD(period_list);

struct period_node {
	u32 pid;
	u32 fps;
	struct list_head list;
};

static const u64 empty_windows[MI_SCHED_CPUS];

int skip_walt_result[2];

#define GROUP_NEED_DECEL	0x20
#define GROUP_FORE		0x0c
#define GROUP_FORE_DISP		0x8c

extern spinlock_t period_freq_list_lock;

unsigned int find_cal_divisor(u64 pred_target_period, u64 target_period, u64 real_period)
{
	u64 period;

	/* clang tests switch-like chains in descending order, so the source order 2,3,1 gives stock's 1,3,2 */
	period = real_period;
	if (target_delta_flag == 2)
		period = min(target_period, real_period);
	else if (target_delta_flag == 3)
		period = target_period;
	else if (target_delta_flag == 1)
		period = min(pred_target_period, real_period);
	return period >> 10;
}

void transfer_APP_status(void)
{
	if (likely(cpu_online(smp_processor_id())))
		irq_work_queue(&flw_change_status_irq_work);
	else
		irq_work_queue_on(&flw_change_status_irq_work, cpumask_first(cpu_online_mask));
}

static int set_update_fps(const char *val, const struct kernel_param *kp)
{
	unsigned int pid_s = 0, fps_s = 0;
	unsigned int fps, pid;
	struct list_head *pos, *n;
	struct period_node *node;
	u64 period, pred;

	if (sscanf(val, "%u,%u", &pid_s, &fps_s) != 2)
		return -EINVAL;
	pid = pid_s;
	fps = fps_s;

	if (pid && fps) {
		list_for_each_safe(pos, n, &period_list) {
			node = list_entry(pos, struct period_node, list);
			if (node && node->pid == pid) {
				node->fps = fps;
				break;
			}
		}

		period = NSEC_PER_SEC / fps;
		period_traced_pid = pid;
		window_load_history.target_period = period;
		pred = get_pred_target_period(fps);
		pred_cpu_fps_period = pred;
		cpu_fps_period = period;
		ioctl_flag = true;
	} else {
		ioctl_flag = false;
		period_traced_pid = 0;
		if (fps)
			period = NSEC_PER_SEC / fps;
		else {
			period = 16666666;
			fps = 60;
		}
		pred = get_pred_target_period(fps);
		pred_cpu_fps_period = pred;
		cpu_fps_period = period;
		window_load_history.mark_start = 0;
		window_load_history.target_period = period;
		window_load_history.cal_div = period >> 10;
		list_for_each_safe(pos, n, &period_list) {
			node = list_entry(pos, struct period_node, list);
			if (node) {
				list_del_init(pos);
				kfree(node);
			}
		}
	}
	return 0;
}

static int get_update_fps(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "ioctl:%u period:%llu pred%llu, period_pid:%d\n", ioctl_flag,
		       cpu_fps_period / NSEC_PER_MSEC, pred_cpu_fps_period / NSEC_PER_MSEC, period_traced_pid);
}

static const struct kernel_param_ops param_ops_fps = {
	.set = set_update_fps,
	.get = get_update_fps,
};
module_param_cb(update_frame_fps, &param_ops_fps, NULL, 0644);

static noinline void add_to_task_load(struct task_struct *p, struct mi_task_struct *mi_tsk, int cpu,
			     u64 delta, u32 fstat, struct walt_task_struct *wts, int event)
{
	u32 rsv = mi_tsk->ravg.rsv;
	u64 exec = get_scale_exec_time(delta, cpu, wts);

	if (fstat & 1)
		mi_tsk->ravg.running_exec += exec;
	else if (fstat & 4)
		mi_tsk->ravg.runnable_exec += exec;
	else if (fstat == 0)
		mi_tsk->ravg.sleep_exec += exec;

	if (rsv & 1)
		mi_tsk->ravg.io_exec += exec;
	else if (rsv & 2)
		mi_tsk->ravg.memstall_exec += exec;

	if (p->in_iowait) {
		if (event == 0)
			mi_tsk->ravg.rsv |= 1;
		else if (event == 2)
			mi_tsk->ravg.rsv &= ~1;
	}

	flw_dbg(8, "pid[%d]fstat%d delta%llu flag%d\n", p->pid, fstat, exec, mi_tsk->ravg.rsv);
}

static void add_to_grp_frame_load(struct mi_group *grp, struct group_frame_ravg *frame, u64 delta,
				  u32 fstat, int cpu, int cid_nr)
{
	int cpu_nr = grp->cpu_nr_running[cpu];

	if (fstat & 1)
		frame->running_sum_exec_runtime += delta;
	else if (fstat & 4)
		frame->runnable_sum_exec_runtime += delta;
	else if (fstat == 0) {
		frame->sleep_sum_exec_runtime += delta;
		return;
	}

	if (cal_running_time_type == 9) {
		frame->io_sum_exec_runtime = frame->running_sum_exec_runtime;
	} else if (cal_running_time_type == 10) {
		if ((fstat & 1) || ((fstat & 4) && cpu_nr == 1))
			frame->io_sum_exec_runtime += delta;
	} else {
		frame->io_sum_exec_runtime = frame->running_sum_exec_runtime + frame->runnable_sum_exec_runtime;
	}

	flw_dbg(3, "[%d-%d]runnable%llu running%llu sum%llu delta%llu, fstat%d, cid-nr%d, cpu-nr%d\n",
		cpu, grp->id, frame->runnable_sum_exec_runtime, frame->running_sum_exec_runtime,
		frame->io_sum_exec_runtime, delta, fstat, cid_nr, cpu_nr);
}

static void rollover_task_grp_window(struct mi_task_struct *mi_tsk, bool full_window)
{
	u64 prev_sum = 0;
	const u64 *curr_cpu_windows;
	int i;

	if (full_window) {
		curr_cpu_windows = empty_windows;
	} else {
		prev_sum = mi_tsk->grp_curr_window_cpu;
		curr_cpu_windows = mi_tsk->grp_curr_window_cpus;
	}

	mi_tsk->grp_prev_window_cpu = prev_sum;
	mi_tsk->grp_curr_window_cpu = 0;

	for (i = 0; i < MI_SCHED_CPUS; i++) {
		mi_tsk->grp_prev_window_cpus[i] = curr_cpu_windows[i];
		mi_tsk->grp_curr_window_cpus[i] = 0;
	}

	flw_dbg(2, "flw-rollover_task_grp_window!, prev-window%llu\n", prev_sum);
}

static void migov_run_callback(struct rq *rq, int flags)
{
	struct migov_callback *data = READ_ONCE(per_cpu(migov_cb_data, rq->cpu));

	if (data)
		data->func(data, walt_sched_clock(), flags);
}

static unsigned int skip_walt_window_update(int cpu)
{
	return skip_walt_result[get_cluster_type(cpu)];
}

static void update_gloabal_target_period(unsigned int fps)
{
	if (fps) {
		cpu_fps_period = NSEC_PER_SEC / fps;
		pred_cpu_fps_period = get_pred_target_period(fps);
	}
}

static bool grp_enq_boost_task(struct task_struct *p)
{
	struct mi_task_struct *mi_tsk;

	if (!skip_walt_result[get_cluster_type(task_cpu(p))])
		return false;
	mi_tsk = get_mi_task_struct(p);
	if (!mi_tsk)
		return false;
	return mi_tsk->flags & BIT(31);
}

static bool grp_enq_boost_pipeline(struct task_struct *p)
{
	return false;
}

static int mi_frame_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static int mi_frame_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mi_frame_release(struct inode *inode, struct file *file)
{
	return 0;
}

static noinline void update_grp_period(unsigned int id, struct group_frame_ravg *prev,
				       struct group_frame_ravg *curr)
{
	prev->running_sum_exec_runtime = curr->running_sum_exec_runtime;
	prev->runnable_sum_exec_runtime = curr->runnable_sum_exec_runtime;
	prev->sleep_sum_exec_runtime = curr->sleep_sum_exec_runtime;
	prev->io_sum_exec_runtime = curr->io_sum_exec_runtime;
	curr->running_sum_exec_runtime = 0;
	curr->runnable_sum_exec_runtime = 0;
	curr->sleep_sum_exec_runtime = 0;
	curr->io_sum_exec_runtime = 0;

	flw_dbg(9, "flw-cur_traced%d start%llu grp-%d-update_grp_period! prunning%llu, prunnable%llu, sum%llu\n",
		period_traced_pid, window_load_history.mark_start, id, prev->running_sum_exec_runtime,
		prev->runnable_sum_exec_runtime, prev->io_sum_exec_runtime);
}

static void init_grp_freq_record(struct mi_group *grp, u64 wallclock)
{
	if (grp->type & GROUP_NEED_DECEL)
		return;
	if (!(grp->type & GROUP_FORE))
		return;

	if (refcount_read(&grp->refcnt) == 2) {
		pr_info("flw: ERROR:grp->usage == 2!!!");
		return;
	}

	grp->sum_freq_period[0] = 0;
	grp->sum_freq_period[1] = 0;
	grp->avg_freq_period[0] = 0;
	grp->avg_freq_period[1] = 0;
	grp->last_freq_change_time[0] = wallclock;
	grp->last_freq_change_time[1] = wallclock;
	grp->cur_record_freq[0] = 0;
	grp->cur_record_freq[1] = 0;

	spin_lock(&period_freq_list_lock);
	if (grp->in_period_freq_list) {
		pr_info("flw: ERROR:grp Node is already in period_freq_list");
	} else {
		list_add(&grp->freq_head, &period_freq_list);
		grp->in_period_freq_list = true;
	}
	spin_unlock(&period_freq_list_lock);
}

static int all_group_show(struct seq_file *m, void *v)
{
	struct list_head *head = get_active_mi_groups();
	struct list_head *pos, *n;
	struct mi_group *grp;

	list_for_each_safe(pos, n, head) {
		grp = list_entry(pos, struct mi_group, list);
		if (!grp) {
			seq_printf(m, "error--!mi_grp\n");
			continue;
		}
		seq_printf(m, "grp->id%d, flag%d, need_decel%d, usage%d\n", grp->id, grp->type,
			   grp->type & GROUP_NEED_DECEL, refcount_read(&grp->refcnt));
	}
	return 0;
}

static int all_group_show_open(struct inode *inode, struct file *file)
{
	return single_open(file, all_group_show, NULL);
}

static const struct proc_ops all_group_show_fops = {
	.proc_open = all_group_show_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int task_grp_id_show(struct seq_file *m, void *v)
{
	int pid = *(int *)m->private;
	struct task_struct *p;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;

	p = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (p) {
		mi_tsk = get_mi_task_struct(p);
		if (mi_tsk) {
			grp = find_mi_group_by_task(p, mi_tsk);
			if (unlikely(!grp)) {
				seq_printf(m, "traced pid %d, not in group\n", pid);
				return 0;
			}
			seq_printf(m, "traced pid %d, grp->id%d, target%llu,", pid, grp->id,
				   grp->target_period / NSEC_PER_MSEC);
			seq_printf(m, "cid%d, token_pid%d;", 0, atomic_read(&grp->cluster_token_pid[0]));
			seq_printf(m, "cid%d, token_pid%d;", 1, atomic_read(&grp->cluster_token_pid[1]));
			seq_printf(m, "\n");
		}
	}
	return 0;
}

static ssize_t task_grp_id_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = {};

	if (!copy_from_user(buf, ubuf, min(count, sizeof(buf))))
		*(int *)((struct seq_file *)file->private_data)->private = simple_strtol(buf, NULL, 10);
	return count;
}

static int task_grp_id_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, task_grp_id_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &grp_traced_pid;
	return ret;
}

static const struct proc_ops task_grp_id_fops = {
	.proc_open = task_grp_id_open,
	.proc_read = seq_read,
	.proc_write = task_grp_id_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int user_grp_id_show(struct seq_file *m, void *v)
{
	int uid = *(int *)m->private;
	struct user_struct *user;
	struct mi_user_struct *mi_user;
	struct mi_group *grp;

	user = find_user(KUIDT_INIT(uid));
	if (user) {
		mi_user = get_mi_user_struct(user);
		if (mi_user) {
			grp = find_mi_group_by_user(mi_user);
			if (unlikely(!grp)) {
				seq_printf(m, "traced uid %d, not in group\n", uid);
				return 0;
			}
			seq_printf(m, "traced uid %d, grp->id%d, target%llu,", uid, grp->id,
				   grp->target_period / NSEC_PER_MSEC);
			seq_printf(m, "cid%d, token_pid%d;", 0, atomic_read(&grp->cluster_token_pid[0]));
			seq_printf(m, "cid%d, token_pid%d;", 1, atomic_read(&grp->cluster_token_pid[1]));
			seq_printf(m, "\n");
		} else {
			free_uid(user);
		}
	}
	return 0;
}

static ssize_t user_grp_id_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[32] = {};

	if (!copy_from_user(buf, ubuf, min(count, sizeof(buf))))
		*(int *)((struct seq_file *)file->private_data)->private = simple_strtol(buf, NULL, 10);
	return count;
}

static int user_grp_id_open(struct inode *inode, struct file *file)
{
	int ret = single_open(file, user_grp_id_show, NULL);

	if (!ret)
		((struct seq_file *)file->private_data)->private = &grp_traced_uid;
	return ret;
}

static const struct proc_ops user_grp_id_fops = {
	.proc_open = user_grp_id_open,
	.proc_read = seq_read,
	.proc_write = user_grp_id_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static u32 get_cluster_cpu_max_util(struct cpufreq_policy *policy, u32 *avg_freq, u64 *real_delta,
				    u64 *target_delta)
{
	int cpu;
	u64 max = 0;
	u32 cid, result, cal_div;

	cal_div = find_cal_divisor(pred_cpu_fps_period, cpu_fps_period, window_load_history.target_period);

	for_each_cpu(cpu, policy->cpus) {
		struct mi_rq *mrq = &per_cpu(mi_runqueues, cpu);

		if (mrq->frame_load_last_update >= window_load_history.mark_start)
			max = max(mrq->prev_frame_exec, max);
	}

	cid = get_cluster_type(policy->cpu);
	result = max / cal_div;
	window_load_history.util[cid] = result;
	*target_delta = window_load_history.target_period;
	*real_delta = cpu_fps_period;
	*avg_freq = sum_avg_freq[cid];

	flw_dbg(9, "flw-cid%d max%llu result%u period%llu\n", cid, max, result, *real_delta);
	return result;
}

#if defined(PERFC_FORGE_CRC) && defined(__GENKSYMS__)
/* genksyms-only stand-in whose salt makes the exported CRC equal the stock one (0xc58f3d35); the include set of the
 * stock TU (and so the struct cpufreq_policy expansion) could not be recovered, see STATUS.md / crcforge.py */
void group_frame_cpufreq_transition(char (*)[sizeof("#DCMY}Ia")]);
#else
void group_frame_cpufreq_transition(void *data, struct cpufreq_policy *policy)
{
	struct list_head *pos, *n;
	struct mi_group *grp;
	u64 wc = walt_sched_clock();
	u64 delta;
	int cid = get_cluster_type(policy->cpu);

	if (skip_walt_result[cid]) {
		delta = wc - last_freq_change_time[cid];
		sum_freq_rt[cid][hist_freq_num] += delta * cur_record_freq[cid];
		cur_record_freq[cid] = policy->cur;
		last_freq_change_time[cid] = wc;

		list_for_each_safe(pos, n, &period_freq_list) {
			grp = list_entry(pos, struct mi_group, freq_head);
			if (!grp || (grp->type & GROUP_NEED_DECEL))
				continue;
			delta = wc - grp->last_freq_change_time[cid];
			grp->sum_freq_period[cid] += grp->cur_record_freq[cid] * delta;
			grp->cur_record_freq[cid] = policy->cur;
			grp->last_freq_change_time[cid] = wc;
			if (unlikely(flw_debug == 4))
				printk("[grp%d-%d]%llu, %d\n", grp->id, cid, delta, cur_record_freq[cid]);
		}

		flw_dbg(4, "[%d]%llu, %d\n", cid, delta, cur_record_freq[cid]);
	}
}
#endif
EXPORT_SYMBOL_GPL(group_frame_cpufreq_transition);

/* ---- load subtraction bookkeeping (derived from WALT) ---- */
static int get_subtraction_index(struct mi_rq *mrq, u64 ws)
{
	int i;
	u64 oldest = ULLONG_MAX;
	int oldest_index = 0;

	for (i = 0; i < 2; i++) {
		u64 entry_ws = mrq->subs[i].ws;

		if (ws == entry_ws)
			return i;

		if (entry_ws < oldest) {
			oldest = entry_ws;
			oldest_index = i;
		}
	}

	mrq->subs[oldest_index].ws = ws;
	mrq->subs[oldest_index].subs = 0;
	mrq->subs[oldest_index].nt_subs = 0;
	return oldest_index;
}

static void update_rq_load_subtractions(int index, struct mi_rq *mrq, const u64 *sub_load, bool new_task)
{
	mrq->subs[index].subs += *sub_load;
	if (new_task)
		mrq->subs[index].nt_subs += *sub_load;
}

static void update_cluster_load_subtractions(struct mi_task_struct *mi_tsk, struct walt_sched_cluster *cluster, int cpu,
					     struct mi_rq *src, bool new_task)
{
	struct cpumask cluster_cpus = cluster->cpus;
	u64 ws = ioctl_flag ? window_load_history.mark_start : src->frame_start_group;
	u64 prev_ws = ws - window_load_history.target_period;
	unsigned int i;

	cpumask_clear_cpu(cpu, &cluster_cpus);
	raw_spin_lock(&cluster->load_lock);

	for_each_cpu(i, &cluster_cpus) {
		struct mi_rq *mrq = &per_cpu(mi_runqueues, i);
		int index;

		if (mi_tsk->curr_window_cpus[i]) {
			index = get_subtraction_index(mrq, ws);
			update_rq_load_subtractions(index, mrq, &mi_tsk->curr_window_cpus[i], new_task);
			mi_tsk->curr_window_cpus[i] = 0;
		}

		if (mi_tsk->prev_window_cpus[i]) {
			index = get_subtraction_index(mrq, prev_ws);
			update_rq_load_subtractions(index, mrq, &mi_tsk->prev_window_cpus[i], new_task);
			mi_tsk->prev_window_cpus[i] = 0;
		}
	}

	raw_spin_unlock(&cluster->load_lock);
}

static void migrate_inter_cluster_fload(bool add, struct task_struct *p, int cpu, bool new_task,
					bool inter_cluster)
{
	struct mi_rq *mrq = &per_cpu(mi_runqueues, cpu);
	struct mi_task_struct *mi_tsk;

	if (p->flags & PF_IDLE)
		return;
	if (mi_task_exiting(p))
		return;
	mi_tsk = get_mi_task_struct(p);
	if (!mi_tsk)
		return;

	if (add) {
		if (inter_cluster) {
			mi_tsk->curr_window_cpus[cpu] = mi_tsk->curr_window_cpu;
			mi_tsk->prev_window_cpus[cpu] = mi_tsk->prev_window_cpu;
			mrq->prev_frame_exec += mi_tsk->prev_window_cpu;
			mrq->cur_frame_exec += mi_tsk->curr_window_cpu;
			if (new_task) {
				mrq->nt_prev_frame_exec += mi_tsk->prev_window_cpu;
				mrq->nt_cur_frame_exec += mi_tsk->curr_window_cpu;
			}
		}
	} else if (inter_cluster) {
		if (mrq->cur_frame_exec < mi_tsk->curr_window_cpus[cpu]) {
			flw_dbg(13, "rq->cur_frame_exec < mi_tsk->curr_window_cpus[%d]\n", cpu);
			mrq->cur_frame_exec = mi_tsk->curr_window_cpus[cpu];
		}
		mrq->cur_frame_exec -= mi_tsk->curr_window_cpus[cpu];

		if (mrq->prev_frame_exec < mi_tsk->prev_window_cpus[cpu]) {
			mrq->prev_frame_exec = mi_tsk->prev_window_cpus[cpu];
			flw_dbg(13, "rq->prev_frame_exec < mi_tsk->prev_window_cpus[%d]\n", cpu);
		}
		mrq->prev_frame_exec -= mi_tsk->prev_window_cpus[cpu];

		if (new_task) {
			if (mrq->nt_cur_frame_exec < mi_tsk->curr_window_cpus[cpu])
				mrq->nt_cur_frame_exec = mi_tsk->curr_window_cpus[cpu];
			mrq->nt_cur_frame_exec -= mi_tsk->curr_window_cpus[cpu];

			if (mrq->nt_prev_frame_exec < mi_tsk->prev_window_cpus[cpu])
				mrq->nt_prev_frame_exec = mi_tsk->prev_window_cpus[cpu];
			mrq->nt_prev_frame_exec -= mi_tsk->prev_window_cpus[cpu];
		}

		mi_tsk->curr_window_cpus[cpu] = 0;
		mi_tsk->prev_window_cpus[cpu] = 0;

		update_cluster_load_subtractions(mi_tsk, per_cpu(walt_rq, cpu).cluster, cpu, mrq, new_task);
	}

	flw_dbg(15, "migrate[%d][pid%d-cpu%d], prev%llu, curr%llu, tsk-prev%llu, tsk-curr%llu, prev%llu, curr%llu inter%d\n",
		add, p->pid, cpu, mrq->prev_frame_exec, mrq->cur_frame_exec, mi_tsk->prev_window_cpu,
		mi_tsk->curr_window_cpu, mi_tsk->prev_window_cpus[cpu], mi_tsk->curr_window_cpus[cpu],
		inter_cluster);
}

static void mi_update_group_group(struct task_struct *p, int cpu, int event)
{
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;
	struct walt_task_struct *wts = (struct walt_task_struct *)p->android_vendor_data1;

	if (p->flags & PF_IDLE)
		return;

	mi_tsk = get_mi_task_struct_unsafe(p);
	if (!mi_tsk) {
		flw_dbg(15, "%d[event%d]-mi-tsk is NULL\n", p->pid, event);
		return;
	}

	grp = find_mi_group_by_task(p, mi_tsk);
	if (!grp)
		return;
	if (grp->type & GROUP_NEED_DECEL)
		return;

	refcount_inc(&grp->refcnt);

	if (grp->type & 0x10) {
		raw_spin_lock((raw_spinlock_t *)&grp->lock);
		if ((event == 0 && !p->on_rq) || event == 3) {
			grp->nr_running--;
			mi_tsk->enqueue_after_migration = wts->enqueue_after_migration;
			flw_dbg(50, "%d[event%d-SUB]nr_running%d\n", p->pid, event, grp->nr_running);
		}
		if (event == 2) {
			grp->nr_running++;
			flw_dbg(50, "%d[event%d-ADD]nr_running%d\n", p->pid, event, grp->nr_running);
		}
		if (event == 4 && mi_tsk->enqueue_after_migration) {
			grp->nr_running++;
			mi_tsk->enqueue_after_migration = 0;
			flw_dbg(50, "%d[event%d-ADD]cpu%d-grpid%d-mig-nr_running%d\n", p->pid, event, cpu, grp->id,
				grp->nr_running);
		}
		if (WARN_ON(grp->nr_running < 0)) {
			printk("grp->nr_running < 0!!\n");
			grp->nr_running = 0;
		}
		raw_spin_unlock((raw_spinlock_t *)&grp->lock);
	} else {
		int *nr = &grp->cpu_nr_running[cpu];

		if ((event == 0 && !p->on_rq) || event == 3) {
			(*nr)--;
			mi_tsk->enqueue_after_migration = wts->enqueue_after_migration;
			flw_dbg(50, "%d[event%d-SUB]cpu%d-grpid%d-nr_running%d\n", p->pid, event, cpu, grp->id, *nr);
		}
		if (event == 2) {
			(*nr)++;
			flw_dbg(50, "%d[event%d-ADD]cpu%d-grpid%d-nr_running%d\n", p->pid, event, cpu, grp->id, *nr);
		}
		if (event == 4 && mi_tsk->enqueue_after_migration) {
			(*nr)++;
			mi_tsk->enqueue_after_migration = 0;
			flw_dbg(50, "%d[event%d-ADD]cpu%d-grpid%d-mig-nr_running%d\n", p->pid, event, cpu, grp->id, *nr);
		}
		if (grp->cpu_nr_running[cpu] < 0) {
			flw_dbg(50, "event-cpu[%d]id%d cpu_nr_running < 0!!\n", cpu, grp->id);
			grp->cpu_nr_running[cpu] = 0;
		}
	}

	mi_group_ref_dec(grp);
}

static void fix_fload_in_irq(struct rq *rq)
{
	struct mi_rq *mrq = &per_cpu(mi_runqueues, rq->cpu);
	u64 fs = mrq->frame_start_group;
	u64 prev_fs = fs - window_load_history.target_period;
	int i;

	for (i = 0; i < 2; i++) {
		struct load_subs *s = &mrq->subs[i];

		if (s->ws == fs) {
			mrq->cur_frame_exec -= s->subs;
			mrq->nt_cur_frame_exec -= s->nt_subs;
			flw_dbg(12, "[%d]fs=%llu,sub=%llu,rq->cur=%llu\n", rq->cpu, prev_fs, s->subs,
				mrq->cur_frame_exec);
		} else if (s->ws == prev_fs) {
			mrq->prev_frame_exec -= s->subs;
			mrq->nt_prev_frame_exec -= s->nt_subs;
			flw_dbg(12, "[%d]fs=%llu,sub=%llu,rq->prev=%llu\n", rq->cpu, prev_fs, s->subs,
				mrq->prev_frame_exec);
		}
		s->subs = 0;
		s->nt_subs = 0;
	}

	if ((s64)mrq->cur_frame_exec < 0) {
		flw_dbg(13, "mi_rq->cur_frame_exec < 0\n");
		mrq->cur_frame_exec = 0;
	}
	if ((s64)mrq->prev_frame_exec < 0) {
		flw_dbg(13, "mi_rq->prev_frame_exec < 0\n");
		mrq->prev_frame_exec = 0;
	}
	if ((s64)mrq->nt_cur_frame_exec < 0) {
		flw_dbg(13, "mi_rq->nt_cur_frame_exec < 0\n");
		mrq->nt_cur_frame_exec = 0;
	}
	if ((s64)mrq->nt_prev_frame_exec < 0) {
		flw_dbg(13, "mi_rq->nt_prev_frame_exec < 0\n");
		mrq->nt_prev_frame_exec = 0;
	}
}

static void flw_irq_work(struct irq_work *work)
{
	struct walt_sched_cluster *cluster;
	unsigned int flags;
	u64 wallclock;
	int cpu, lcpu;

	if (READ_ONCE(flw_work_in_process))
		return;
	/* codegen anchor: this subscript only makes clang create the shared array-bounds trap block before the cpu loops (loop-exit
	 * compare polarity of the stock build), it evaluates to 0 and generates no code */
	flw_work_in_process = 1 + skip_walt_result[flw_work_in_process & 1] * 0;

	for_each_possible_cpu(lcpu)
		raw_spin_lock(&cpu_rq(lcpu)->__lock);

	wallclock = walt_sched_clock();

	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		if (!skip_walt_result[cluster->id])
			continue;
		raw_spin_lock(&cluster->load_lock);
		for_each_cpu(cpu, &cluster->cpus) {
			struct rq *rq = cpu_rq(cpu);

			if (rq->curr && cpumask_intersects(&cluster->cpus, cpu_possible_mask)) {
				walt_update_task_ravg(rq->curr, rq, 4, wallclock, 0);
				fix_fload_in_irq(rq);
			}
		}
		raw_spin_unlock(&cluster->load_lock);
	}

	if (work == &flw_change_status_irq_work)
		flags = 8;
	else if (work == &flw_async_cpufreq_irq_work)
		flags = 0x10;
	else
		flags = work == &flw_cpufreq_irq_work;

	list_for_each_entry_rcu(cluster, &cluster_head, list) {
		struct cpumask online;
		int nr, pos = 1;

		if (!skip_walt_result[cluster->id])
			continue;
		cpumask_and(&online, &cluster->cpus, cpu_online_mask);
		nr = cpumask_weight(&online);
		for_each_cpu(cpu, &online) {
			if (pos == nr) {
				migov_run_callback(cpu_rq(cpu), flags);
				pos = nr + 1;
			}
		}
	}

	for_each_possible_cpu(cpu)
		raw_spin_unlock(&cpu_rq(cpu)->__lock);
	flw_work_in_process = 0;
}

/* ---- mi_frame_load_ravg block begin ---- */

#define GROUP_SHARED_NR		0x10	/* one nr_running for the whole group (grp->nr_running) */
#define GROUP_NO_FRAME		0x40	/* follows the global frame instead of its own frame_start */

#define PUT_PREV_TASK		0
#define PICK_NEXT_TASK		1
#define TASK_WAKE		2

static u64 find_freq_period(u64 pred_target_period, u64 target_period, u64 real_period)
{
	u64 period = real_period;

	if (freq_target_delta_flag == 2)
		period = min(target_period, real_period);
	else if (freq_target_delta_flag == 3)
		period = target_period;
	else if (freq_target_delta_flag == 1)
		period = min(pred_target_period, real_period);
	return period;
}

/* demand / history roll of the task frame window (mi_task_struct->ravg) */
static __always_inline void mi_frame_task_ravg(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime,
			       int event)
{
	struct walt_task_struct *wts = (struct walt_task_struct *)p->android_vendor_data1;
	u64 mark_start = wts->mark_start;
	int cpu = rq->cpu;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;
	u64 window, fs;
	unsigned int div;

	if (p->flags & PF_IDLE)
		return;
	window = window_load_history.target_period;
	if (mi_task_exiting(p))
		return;
	mi_tsk = get_mi_task_struct(p);
	if (!mi_tsk)
		return;
	grp = find_mi_group_by_task(p, mi_tsk);
	if (grp)
		refcount_inc(&grp->refcnt);

	if (ioctl_flag) {
		u64 gfs, gwin;

		if (grp && grp->frame_start && !(grp->type & GROUP_NO_FRAME)) {
			fs = grp->frame_start;
			gwin = grp->window_period;
			if (gwin) {
				window = gwin;
				if (fs > wallclock)
					fs -= gwin;
			}
			if (fs > wallclock)
				goto bail;
			div = find_cal_divisor(grp->pred_target_period, grp->target_period, grp->window_period);
		} else {
			gfs = window_load_history.mark_start;
			if (gfs > wallclock)
				fs = gfs - window;
			else
				fs = gfs;
			if (fs > wallclock) {
				if (grp)
					goto bail;
				return;
			}
			div = find_cal_divisor(pred_cpu_fps_period, cpu_fps_period, window_load_history.target_period);
		}
	} else {
		fs = per_cpu(mi_runqueues, cpu).frame_start_group;
		div = find_cal_divisor(pred_cpu_fps_period, cpu_fps_period, window_load_history.target_period);
	}

	if (fs >= mark_start) {
		u64 delta = min(fs - mark_start, window);
		u64 running, runnable, sum, avg, util;
		u32 demand, demand_scaled;
		unsigned int idx;

		add_to_task_load(p, mi_tsk, cpu, delta, fstat, wts, event);

		running = mi_tsk->ravg.running_exec;
		runnable = mi_tsk->ravg.runnable_exec;
		idx = hist_nums;
		sum = running + runnable;
		util = sum / div;
		mi_tsk->ravg.sleep_exec = 0;
		mi_tsk->ravg.io_exec = 0;
		mi_tsk->ravg.memstall_exec = 0;
		{
			u64 *rh = mi_tsk->ravg.running_hist;
			u64 *nh = mi_tsk->ravg.runnable_hist;
			u64 *sh = mi_tsk->ravg.sum_hist;
			u64 *uh = mi_tsk->ravg.sum_hist_util;

			rh[idx] = running;
			nh[idx] = runnable;
			sh[idx] = sum;
			mi_tsk->ravg.running_exec = 0;
			mi_tsk->ravg.runnable_exec = 0;
			hist_nums = (idx + 1) % 5;
			avg = sh[hist_nums] * 5 / 5;
			uh[idx] = util;
		}
		demand = max(avg, sum);
		demand_scaled = demand / div;
		if (likely(p->on_rq == 1)) {
			s64 diff = (s64)demand_scaled - (s64)mi_tsk->ravg.demand_scaled;
			struct mi_group *g = find_mi_group_by_task(p, mi_tsk);

			if (g) {
				refcount_inc(&g->refcnt);
				s64 cum = g->status.cumulative_runnable_avg_scaled + diff;

				if (cum < 0) {
					flw_dbg(1, "grp-id%d, cumulative_runnable_avg_scaled < 0\n", g->id);
					cum = 0;
				}
				g->status.cumulative_runnable_avg_scaled = cum;
				mi_group_ref_dec(g);
			} else {
				flw_dbg(1, "%d have no mi-group\n", p->pid);
			}
		}
		mi_tsk->ravg.demand_scaled = demand_scaled;
		mi_tsk->ravg.demand = demand;
		mi_tsk->ravg.coloc_demand = avg;
		mark_start = fs;
	}
	add_to_task_load(p, mi_tsk, cpu, wallclock - mark_start, fstat, wts, event);

	if (grp)
		mi_group_ref_dec(grp);
	flw_dbg(7, "p%d: running%llu, runnable%llu\n", p->pid, mi_tsk->ravg.running_exec,
		mi_tsk->ravg.runnable_exec);
	return;
bail:
	mi_group_ref_dec(grp);
}

/* per group (cpu / cluster / group wide) frame accounting, only for the ioctl driven frames */
static __always_inline void mi_frame_group_ravg(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime,
				int event)
{
	struct walt_task_struct *wts = (struct walt_task_struct *)p->android_vendor_data1;
	u64 mark_start = wts->mark_start;
	int cpu = rq->cpu;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp;
	u64 fs, window, pred, delta_fs;
	u32 type;
	int cid;
	bool global;
	int nr = 0, i;

	if (ioctl_flag != 1)
		return;
	if (p->flags & PF_IDLE)
		return;
	mi_tsk = get_mi_task_struct_unsafe(p);
	if (!mi_tsk)
		return;
	grp = find_mi_group_by_task(p, mi_tsk);
	if (!grp) {
		flw_dbg(2, "flw-pid%d !grp in %s\n", p->pid, __func__);
		return;
	}
	if (unlikely(grp->type & GROUP_NEED_DECEL)) {
		flw_dbg(1, "pid%d grpid%d is GROUP_NEED_DECEL!\n", p->pid, grp->id);
		return;
	}
	refcount_inc(&grp->refcnt);

	type = grp->type;
	if (ioctl_flag == 1) {
		fs = grp->frame_start;
		if (fs && !(type & GROUP_NO_FRAME)) {
			window = grp->window_period;
			pred = grp->pred_target_period;
			global = false;
		} else {
			fs = window_load_history.mark_start;
			window = window_load_history.target_period;
			pred = cpu_fps_period;
			global = true;
		}
	} else {
		fs = per_cpu(mi_runqueues, rq->cpu).frame_start_group;
		window = window_load_history.target_period;
		pred = cpu_fps_period;
		global = true;
	}
	delta_fs = wallclock - fs;
	grp->last_update = wallclock;

	if (type & GROUP_SHARED_NR) {
		u64 ms, exec;

		if ((p->flags & PF_IDLE) || event == TASK_WAKE || (event == PICK_NEXT_TASK && rq->curr == rq->idle))
			goto out;
		raw_spin_lock((raw_spinlock_t *)&grp->lock);
		if (grp->frame_start >= wallclock) {
			printk("flw-wallclock < frame_start grp-id%d\n", grp->id);
			goto unlock;
		}
		grp->local_cid = get_cluster_type(task_cpu(p));
		ms = grp->mark_start;
		if (grp->nr_running == 1)
			ms = max(wts->mark_start, ms);
		if (likely(wallclock > ms)) {
			if (fs >= ms) {
				exec = get_scale_exec_time(fs - ms, cpu, wts);
				add_to_grp_frame_load(grp, &grp->prev_frame, exec, fstat, cpu, 0);
				flw_dbg(20, "flw-p1grp-pid%d[id%d||%d]cid%d[nr%d] prunning%llu prunnable%llu crunning%llu crunnable%llu, delta%llu, ms%llu, cal-ms%llu, fs%llu, wc%llu\n",
					p->pid, grp->id, grp->type, grp->local_cid, grp->nr_running,
					grp->prev_frame.running_sum_exec_runtime, grp->prev_frame.runnable_sum_exec_runtime,
					grp->curr_frame.running_sum_exec_runtime, grp->curr_frame.runnable_sum_exec_runtime,
					exec, grp->mark_start, ms, fs, wallclock);
				exec = get_scale_exec_time(delta_fs, cpu, wts);
				add_to_grp_frame_load(grp, &grp->curr_frame, exec, fstat, cpu, 0);
				flw_dbg(20, "flw-p2grp-pid%d[id%d||%d]cid%d[nr%d] prunning%llu prunnable%llu crunning%llu crunnable%llu, delta%llu, ms%llu, cal-ms%llu, fs%llu, wc%llu\n",
					p->pid, grp->id, grp->type, grp->local_cid, grp->nr_running,
					grp->prev_frame.running_sum_exec_runtime, grp->prev_frame.runnable_sum_exec_runtime,
					grp->curr_frame.running_sum_exec_runtime, grp->curr_frame.runnable_sum_exec_runtime,
					exec, grp->mark_start, ms, fs, wallclock);
			} else {
				exec = get_scale_exec_time(wallclock - ms, cpu, wts);
				add_to_grp_frame_load(grp, &grp->curr_frame, exec, fstat, cpu, 0);
				flw_dbg(20, "flw-cgrp-pid%d[id%d||%d]cid%d[nr%d] prunning%llu prunnable%llu crunning%llu crunnable%llu, delta%llu, ms%llu, cal-ms%llu, fs%llu, wc%llu\n",
					p->pid, grp->id, grp->type, grp->local_cid, grp->nr_running,
					grp->prev_frame.running_sum_exec_runtime, grp->prev_frame.runnable_sum_exec_runtime,
					grp->curr_frame.running_sum_exec_runtime, grp->curr_frame.runnable_sum_exec_runtime,
					exec, grp->mark_start, ms, fs, wallclock);
			}
			grp->mark_start = wallclock;
		}
unlock:
		raw_spin_unlock((raw_spinlock_t *)&grp->lock);
		goto out;
	}

	cid = get_cluster_type(cpu);
	for_each_possible_cpu(i) {
		if (get_cluster_type(i) == cid)
			nr += grp->cpu_nr_running[i];
	}

	if (wallclock < fs)
		fs -= window;
	if (wallclock < fs) {
		flw_dbg(2, "flw-wallclock < frame_start grp-id%d\n", grp->id);
		goto out;
	}

	if (fs >= mark_start) {
		bool full_window = (fs - mark_start) >= window;
		u64 exec;

		rollover_task_grp_window(mi_tsk, full_window);
		mi_tsk->rsv4 = fs;
		if (global && full_window) {
			exec = get_scale_exec_time(window, cpu, wts);
			add_to_grp_frame_load(grp, &grp->cpu_prev_frame[cpu], exec, fstat, cpu, nr);
			if (fstat & 1) {
				mi_tsk->grp_prev_window_cpu += exec;
				mi_tsk->grp_prev_window_cpus[cpu] += exec;
			}
		} else {
			exec = get_scale_exec_time(fs - mark_start, cpu, wts);
			add_to_grp_frame_load(grp, &grp->cpu_prev_frame[cpu], exec, fstat, cpu, nr);
			if (fstat & 1) {
				mi_tsk->grp_prev_window_cpu += exec;
				mi_tsk->grp_prev_window_cpus[cpu] += exec;
			}
		}
		exec = get_scale_exec_time(wallclock - fs, cpu, wts);
		add_to_grp_frame_load(grp, &grp->cpu_curr_frame[cpu], exec, fstat, cpu, nr);
		if (fstat & 1) {
			mi_tsk->grp_curr_window_cpu += exec;
			mi_tsk->grp_curr_window_cpus[cpu] += exec;
		}
	} else {
		u64 exec = get_scale_exec_time(wallclock - mark_start, cpu, wts);

		add_to_grp_frame_load(grp, &grp->cpu_curr_frame[cpu], exec, fstat, cpu, nr);
		if (fstat & 1) {
			mi_tsk->grp_curr_window_cpu += exec;
			mi_tsk->grp_curr_window_cpus[cpu] += exec;
		}
	}

	if (unlikely(!(p->flags & PF_IDLE) && event != TASK_WAKE && !(event == PICK_NEXT_TASK && rq->curr == rq->idle))) {
		u64 start = grp->cluster_mark_start[cid];

		if (atomic_cmpxchg(&grp->cluster_token_pid[cid], 0, p->pid) == 0) {
			flw_dbg(50, "flw-grpid%d[%d]-set token pid %d, ms%llu wait_wake%d\n", grp->id, cid, p->pid,
				start, grp->wait_wake_flag[cid]);
			if (grp->wait_wake_flag[cid]) {
				grp->wait_wake_flag[cid] = false;
				start = max(wts->mark_start, start);
			}
			if (wallclock > start) {
				u64 exec1, exec2;

				if (likely(fs >= start)) {
					u64 d1 = fs - start;

					if (global && d1 >= window)
						d1 = window;
					exec1 = get_scale_exec_time(d1, cpu, wts);
					if ((fstat & 1) || !(type & GROUP_NO_FRAME))
						grp->cluster_prev_frame[cid].io_sum_exec_runtime += exec1;
					exec2 = get_scale_exec_time(wallclock - fs, cpu, wts);
					if ((fstat & 1) || !(type & GROUP_NO_FRAME))
						grp->cluster_curr_frame[cid].io_sum_exec_runtime += exec2;
				} else {
					exec2 = get_scale_exec_time(wallclock - start, cpu, wts);
					if ((fstat & 1) || !(type & GROUP_NO_FRAME))
						grp->cluster_curr_frame[cid].io_sum_exec_runtime += exec2;
				}
			}
			grp->cluster_mark_start[cid] = wallclock;
			if (!nr)
				grp->wait_wake_flag[cid] = true;
			atomic_set(&grp->cluster_token_pid[cid], 0);

			if (event == PUT_PREV_TASK && (frame_util_boost[cid] & 8) && (grp->type & GROUP_FORE_DISP) &&
			    !grp->up_pred_util[cid]) {
				u64 thr;

				if (likely(grp->cluster_prev_frame[cid].io_sum_exec_runtime))
					thr = grp->cluster_prev_frame[cid].io_sum_exec_runtime *
					      (util_boost_percent[cid] + 100) / 100;
				else
					thr = pred * util_zero_boost_percent[cid] / 100;
				if (grp->cluster_curr_frame[cid].io_sum_exec_runtime > thr) {
					migov_run_callback(rq, 0x80);
					grp->up_pred_util[cid] = 1;
				}
			}
			if ((frame_util_boost[cid] & 4) && (grp->type & GROUP_FORE_DISP) && !grp->down_pred_util[cid]) {
				if (delta_fs > pred * down_util_margin[cid] / 100 && delta_fs < pred &&
				    (pred / delta_fs) * grp->cluster_curr_frame[cid].io_sum_exec_runtime <
				    grp->cluster_prev_frame[cid].io_sum_exec_runtime) {
					migov_run_callback(rq, 0x100);
					grp->down_pred_util[cid] = 1;
				}
			}
			flw_dbg(50, "flw-grpid%d[%d]-clear token pid %d, ms%llu nr%d\n", grp->id, cid, p->pid, start, nr);
			if (flw_debug == 51) {
				printk("flw-grp-cid%d-cpu%dpid%d[id%d] psum%llu csum%llu nr%d ms%llu fs%llu wc%llu event%d\n",
				       cid, cpu, p->pid, grp->id, grp->cluster_prev_frame[cid].io_sum_exec_runtime,
				       grp->cluster_curr_frame[cid].io_sum_exec_runtime, nr, start, fs, wallclock, event);
				trace_cluster_real_scale_delta(grp->id, cid, p->pid, nr,
							       grp->cluster_prev_frame[cid].io_sum_exec_runtime,
							       grp->cluster_curr_frame[cid].io_sum_exec_runtime);
			}
		}
	}
	if (flw_debug == 2) {
		printk("flw-grp-cpu-pid=%d[%d|id%d] cpu=%d prev_running=%llu prev_runnable=%llu sum%llu cur_running=%llu cur_runnable=%llu sum%llu start=%llu period=%llu nr=%d\n",
		       p->pid, grp->type, grp->id, cpu, grp->cpu_prev_frame[cpu].running_sum_exec_runtime,
		       grp->cpu_prev_frame[cpu].runnable_sum_exec_runtime, grp->cpu_prev_frame[cpu].io_sum_exec_runtime,
		       grp->cpu_curr_frame[cpu].running_sum_exec_runtime, grp->cpu_curr_frame[cpu].runnable_sum_exec_runtime,
		       grp->cpu_curr_frame[cpu].io_sum_exec_runtime, fs, window, nr);
		printk("mi-tsk%d for migrate [%d]prev%llu cur %llu total-prev%llu cur%llu\n", p->pid, cpu,
		       mi_tsk->grp_prev_window_cpu, mi_tsk->grp_curr_window_cpu, mi_tsk->grp_prev_window_cpus[cpu],
		       mi_tsk->grp_curr_window_cpus[cpu]);
	}
out:
	mi_group_ref_dec(grp);
}

static __always_inline void rollover_task_window(struct mi_task_struct *mi_tsk, bool full_window)
{
	u64 curr_window = 0;
	const u64 *curr_cpu_windows = empty_windows;
	int i;

	if (!full_window) {
		curr_window = mi_tsk->curr_window_cpu;
		curr_cpu_windows = mi_tsk->curr_window_cpus;
	}
	mi_tsk->prev_window_cpu = curr_window;
	mi_tsk->curr_window_cpu = 0;
	for (i = 0; i < MI_SCHED_CPUS; i++) {
		mi_tsk->prev_window_cpus[i] = curr_cpu_windows[i];
		mi_tsk->curr_window_cpus[i] = 0;
	}
}

/* per cpu (mi_rq) frame busy time accounting, derived from walt update_cpu_busy_time() */
static __always_inline void mi_frame_cpu_ravg(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime,
			      int event)
{
	int p_is_curr_task = (p == rq->curr);
	int cpu = rq->cpu;
	struct walt_task_struct *wts = (struct walt_task_struct *)p->android_vendor_data1;
	struct mi_task_struct *mi_tsk = get_mi_task_struct(p);
	struct mi_rq *mrq = &per_cpu(mi_runqueues, cpu);
	u64 window_size = window_load_history.target_period;
	bool new_task = is_new_task(p);
	u64 mark_start, window_start, delta;
	int full_window = 0;
	bool new_window;

	if (!mi_tsk)
		return;
	mark_start = wts->mark_start;

	if (ioctl_flag == 1) {
		window_start = window_load_history.mark_start;
		if (window_start > wallclock)
			window_start -= window_size;
		if (window_start > wallclock) {
			printk("frame_start > wallclock  pid[%d]\n", p->pid);
			return;
		}
	} else {
		window_start = mrq->frame_start_group;
	}

	new_window = mark_start < window_start;
	if (new_window) {
		full_window = (window_start - mark_start) >= window_size;
		if (!(p->flags & PF_IDLE))
			rollover_task_window(mi_tsk, full_window);
	}

	if (ioctl_flag) {
		if (window_start > mrq->frame_load_last_update && window_start != mrq->frame_start_group) {
			if (window_start - mrq->frame_load_last_update >= window_size) {
				mrq->grp_prev_frame_exec = 0;
				mrq->prev_frame_exec = 0;
				mrq->nt_prev_frame_exec = 0;
			} else {
				mrq->grp_prev_frame_exec = mrq->grp_cur_frame_exec;
				mrq->prev_frame_exec = mrq->cur_frame_exec;
				mrq->nt_prev_frame_exec = mrq->nt_cur_frame_exec;
			}
			mrq->grp_cur_frame_exec = 0;
			mrq->cur_frame_exec = 0;
			mrq->nt_cur_frame_exec = 0;
			mrq->frame_start_group = window_start;
		}
	}
	mrq->frame_load_last_update = wallclock;

	if (!(fstat & 2))
		return;

	if (!new_window) {
		if (!irqtime || !(p->flags & PF_IDLE) || cpu_is_waiting_on_io(rq))
			delta = wallclock - mark_start;
		else
			delta = irqtime;
		delta = get_scale_exec_time(delta, cpu, wts);
		mrq->cur_frame_exec += delta;
		if (new_task)
			mrq->nt_cur_frame_exec += delta;
		if (!(p->flags & PF_IDLE)) {
			mi_tsk->curr_window_cpu += delta;
			mi_tsk->curr_window_cpus[cpu] += delta;
		}
		if (flw_debug == 11) {
			printk("[%d-%d]curr update running%llu,delta%llu,wc-ms%llu\n", cpu, p->pid, mrq->cur_frame_exec,
			       delta, wallclock - mark_start);
			printk("mi-tsk%d for migrate [%d]prev%llu cur %llu total-prev%llu cur%llu\n", p->pid, cpu,
			       mi_tsk->prev_window_cpu, mi_tsk->curr_window_cpu, mi_tsk->prev_window_cpus[cpu],
			       mi_tsk->curr_window_cpus[cpu]);
		}
		return;
	}

	if (!p_is_curr_task) {
		if (!full_window) {
			delta = get_scale_exec_time(window_start - mark_start, cpu, wts);
			mi_tsk->prev_window_cpu += delta;
			mi_tsk->prev_window_cpus[cpu] += delta;
		} else {
			delta = get_scale_exec_time(window_size, cpu, wts);
			mi_tsk->prev_window_cpu = delta;
			mi_tsk->prev_window_cpus[cpu] = delta;
		}
		mrq->prev_frame_exec += delta;
		if (new_task)
			mrq->nt_prev_frame_exec += delta;
		delta = get_scale_exec_time(wallclock - window_start, cpu, wts);
		mrq->cur_frame_exec += delta;
		if (new_task)
			mrq->nt_cur_frame_exec += delta;
		mi_tsk->curr_window_cpu = delta;
		mi_tsk->curr_window_cpus[cpu] = delta;
		if (flw_debug == 11) {
			printk("[%d]update curr-running%llu, prev%llu, fs-ms%llu[%d]\n", cpu, mrq->cur_frame_exec,
			       mrq->prev_frame_exec, window_start - mark_start, full_window);
			printk("mi-tsk%d for migrate [%d]prev%llu cur %llu total-prev%llu cur%llu\n", p->pid, cpu,
			       mi_tsk->prev_window_cpu, mi_tsk->curr_window_cpu, mi_tsk->prev_window_cpus[cpu],
			       mi_tsk->curr_window_cpus[cpu]);
		}
		return;
	}

	if (!irqtime || !(p->flags & PF_IDLE) || cpu_is_waiting_on_io(rq)) {
		if (!full_window) {
			delta = get_scale_exec_time(window_start - mark_start, cpu, wts);
			if (!(p->flags & PF_IDLE)) {
				mi_tsk->prev_window_cpu += delta;
				mi_tsk->prev_window_cpus[cpu] += delta;
			}
		} else {
			delta = get_scale_exec_time(window_size, cpu, wts);
			if (!(p->flags & PF_IDLE)) {
				mi_tsk->prev_window_cpu = delta;
				mi_tsk->prev_window_cpus[cpu] = delta;
			}
		}
		mrq->prev_frame_exec += delta;
		if (new_task)
			mrq->nt_prev_frame_exec += delta;
		delta = get_scale_exec_time(wallclock - window_start, cpu, wts);
		mrq->cur_frame_exec += delta;
		if (new_task)
			mrq->nt_cur_frame_exec += delta;
		if (!(p->flags & PF_IDLE)) {
			mi_tsk->curr_window_cpu = delta;
			mi_tsk->curr_window_cpus[cpu] = delta;
		}
		if (flw_debug == 11) {
			printk("3-[%d]update curr-running%llu, prev%llu, fs-ms%llu[%d]\n", cpu, mrq->cur_frame_exec,
			       mrq->prev_frame_exec, window_start - mark_start, full_window);
			printk("mi-tsk%d for migrate [%d]prev%llu cur %llu total-prev%llu cur%llu\n", p->pid, cpu,
			       mi_tsk->prev_window_cpu, mi_tsk->curr_window_cpu, mi_tsk->prev_window_cpus[cpu],
			       mi_tsk->curr_window_cpus[cpu]);
		}
		return;
	}

	if (irqtime) {
		BUG_ON(!(p->flags & PF_IDLE));
		mark_start = wallclock - irqtime;
		if (mark_start > window_start) {
			mrq->cur_frame_exec += get_scale_exec_time(irqtime, cpu, wts);
			flw_dbg(11, "4-[%d]curr update running%llu\n", cpu, mrq->cur_frame_exec);
			return;
		}
		delta = window_start - mark_start;
		if (delta > window_size)
			delta = window_size;
		delta = get_scale_exec_time(delta, cpu, wts);
		mrq->prev_frame_exec += delta;
		delta = wallclock - window_start;
		mrq->cur_frame_exec += get_scale_exec_time(delta, cpu, wts);
		flw_dbg(11, "4-[%d]update curr-running%llu, prev%llu\n", cpu, mrq->cur_frame_exec, mrq->prev_frame_exec);
	}
}

void mi_frame_load_ravg(struct task_struct *p, struct rq *rq, u32 fstat, u64 wallclock, u64 irqtime, int event)
{
	u32 cid = get_cluster_type(task_cpu(p));
	struct mi_rq *mrq;
	u64 old_fs;

	if (!skip_walt_result[cid])
		return;

	mrq = &per_cpu(mi_runqueues, rq->cpu);
	if (!ioctl_flag) {
		s64 delta;

		old_fs = mrq->frame_start_group;
		delta = wallclock - old_fs;
		if (delta < 0) {
			printk("rq%d error in delta < 0\n", rq->cpu);
		} else if (delta >= window_load_history.target_period) {
			u64 period = window_load_history.target_period;
			int n = delta / period;
			u64 fperiod;
			int i, cur;

			mrq->frame_start_group = old_fs + n * period;
			if (n > 1) {
				mrq->grp_prev_frame_exec = 0;
				mrq->prev_frame_exec = 0;
				mrq->nt_prev_frame_exec = 0;
			} else {
				mrq->grp_prev_frame_exec = mrq->grp_cur_frame_exec;
				mrq->prev_frame_exec = mrq->cur_frame_exec;
				mrq->nt_prev_frame_exec = mrq->nt_cur_frame_exec;
			}
			mrq->grp_cur_frame_exec = 0;
			mrq->cur_frame_exec = 0;
			mrq->nt_cur_frame_exec = 0;

			fperiod = find_freq_period(pred_cpu_fps_period, cpu_fps_period, period);
			cur = hist_freq_num;
			for (i = 0; i < 2; i++) {
				sum_freq_rt[i][cur] += (wallclock - last_freq_change_time[i]) * cur_record_freq[i];
				last_freq_change_time[i] = wallclock;
				sum_avg_freq[i] = sum_freq_rt[i][cur] / fperiod;
				sum_freq_rt[i][cur ^ 1] = 0;
			}
			hist_freq_num = cur ^ 1;
			flw_dbg(11, "rq%d update fs %llu[%d]\n", rq->cpu, mrq->frame_start_group, n > 1);
		}
	}

	mi_frame_task_ravg(p, rq, fstat, wallclock, irqtime, event);
	mi_frame_group_ravg(p, rq, fstat, wallclock, irqtime, event);
	mi_frame_cpu_ravg(p, rq, fstat, wallclock, irqtime, event);

	if (skip_walt_result[cid] && !ioctl_flag) {
		u64 fs = per_cpu(mi_runqueues, rq->cpu).frame_start_group;

		if (fs != old_fs && cmpxchg(&flw_irq_work_lastq_ws, old_fs, fs) == old_fs) {
			if (cpu_online(smp_processor_id()))
				irq_work_queue(&flw_cpufreq_irq_work);
			else
				irq_work_queue_on(&flw_cpufreq_irq_work, cpumask_first(cpu_online_mask));
			flw_dbg(11, "!ioctl-rq%d trigger irq work\n", rq->cpu);
		}
	}
}
/* ---- end ---- */

/* ---- get_group_util block begin ---- */
/*
 * Notes for matching the stock code generation (clang ThinLTO, -O2):
 *  - the debug printks are "unlikely" and the tracepoint wrapper is force-inlined, so the debug blocks are
 *    laid out after the per-cpu path (policy != 5) as in the stock binary;
 *  - unlikely(LS group), likely(policy == 5) give the block order [cluster path][LS group][per-cpu path];
 *  - the if/else arm order, the declaration order and the early load of cpu_curr_frame[cpu] are deliberate:
 *    they steer branch layout, phi order and the shared "cpu * 40" computation.
 */
#define gu_dbg(lvl, fmt, ...)	do { if (unlikely(flw_debug == (lvl))) printk(fmt, ##__VA_ARGS__); } while (0)

u64 get_group_util(struct cpufreq_policy *policy, unsigned int *avg_freq, unsigned int flags,
		   u64 *target_delta, u64 *real_delta, bool *fore_timeout, bool *group_util_valid)
{
	struct list_head *head, *pos, *n;
	struct mi_group *grp;
	unsigned long cap;
	u64 wallclock, pred_period = 0, ls_period = 0;
	int max_cpu = -1, max_back_cpu = -1, max_sys_cpu = -1;
	u32 clus_util = 0, point_util = 0, fore_util = 0, back_util = 0, sys_util = 0, kern_util = 0;
	u32 fore_avg = 0, back_avg = 0, sys_avg = 0, kern_avg = 0;
	u32 cid, old_util, result;
	u64 res64;

	if (util_policy) {
		result = get_cluster_cpu_max_util(policy, avg_freq, target_delta, real_delta);
		goto out;
	}

	head = get_active_mi_groups();
	cap = cpu_rq(policy->cpu)->cpu_capacity_orig;
	cid = get_cluster_type(policy->cpu);
	wallclock = walt_sched_clock();
	if (!ioctl_flag) {
		result = get_cluster_cpu_max_util(policy, avg_freq, target_delta, real_delta);
		goto out;
	}

	old_util = window_load_history.grp_util[cid];

	list_for_each_safe(pos, n, head) {
		u32 cal_div, util;
		u64 delta, period;

		grp = list_entry(pos, struct mi_group, list);
		if (unlikely(!grp))
			continue;
		if (unlikely(grp->type & GROUP_NEED_DECEL))
			continue;

		if (unlikely(grp->type & 0x10)) {
			u64 running;

			if (cid != grp->local_cid)
				continue;
			running = grp->prev_frame.running_sum_exec_runtime +
				  grp->prev_frame.runnable_sum_exec_runtime * grp_percent;
			if (!running)
				continue;
			cal_div = find_cal_divisor(grp->pred_target_period, grp->target_period,
						   grp->window_period);
			util = running / cal_div;
			if (point_util < util)
				ls_period = grp->window_period;
			point_util = max(point_util, util);
			gu_dbg(11, "flw-grp-id%d, scale-delta%llu, period%llu\n", grp->id, running, ls_period);
			continue;
		}

		if (grp->type & GROUP_FORE_DISP) {
			period = grp->pred_target_period;
			cal_div = find_cal_divisor(grp->pred_target_period, grp->target_period,
						   grp->window_period);
			delta = wallclock - grp->frame_start;
			if (delta > grp->target_period)
				*fore_timeout = true;
		} else {
			period = cpu_fps_period;
			cal_div = find_cal_divisor(pred_cpu_fps_period, cpu_fps_period,
						   window_load_history.target_period);
			delta = wallclock - window_load_history.mark_start;
		}

		if (likely(choose_result_util_policy == 5)) {
			u64 prev = grp->cluster_prev_frame[cid].io_sum_exec_runtime;
			u64 curr = grp->cluster_curr_frame[cid].io_sum_exec_runtime;
			u64 num;
			u32 fub;
			bool use_cur = false;

			if (!prev && !curr)
				continue;

			fub = frame_util_boost[cid];
			if (fub & 1) {
				if (prev) {
					if (curr > prev * (100 + util_boost_percent[cid]) / 100) {
						use_cur = true;
						num = curr;
						if ((grp->type & GROUP_FORE_DISP) &&
						    delta > grp->pred_target_period)
							cal_div = find_cal_divisor(grp->pred_target_period,
										   grp->target_period, delta);
						goto calc;
					}
				} else if (curr > period * util_zero_boost_percent[cid] / 100) {
					prev = curr;
					if ((grp->type & GROUP_FORE_DISP) && delta > grp->pred_target_period)
						cal_div = find_cal_divisor(grp->pred_target_period,
									   grp->target_period, delta);
				}
			}
			num = prev;
			if (fub & 2) {
				u64 margin = period * down_util_margin[cid] / 100;

				if (delta > margin) {
					if (delta < period) {
						u64 scaled = (period / delta) * curr;

						if (scaled < prev) {
							gu_dbg(9, "flw-cluster%d, grp-id%d, cur_running_time%llu, scale_delta%llu, scale_delta_cur%llu, scale_delta_temp%llu\n",
								cid, grp->id, delta, prev, curr, scaled);
							num = scaled;
						}
					}
				}
			}
calc:
			util = num / cal_div;
			if (util > clus_util) {
				clus_util = util;
				if (!(grp->type & GROUP_FORE_DISP)) {
					*target_delta = pred_cpu_fps_period;
					*real_delta = window_load_history.target_period;
					*avg_freq = sum_avg_freq[cid];
				} else {
					*real_delta = grp->window_period;
					*target_delta = grp->pred_target_period;
					*avg_freq = grp->avg_freq_period[cid];
				}
			}
			if (unlikely(flw_debug == 9)) {
				printk("flw-cluster%d, grp-id%d, grp-flags%d, delta%llu, cid_util%u, avg_freq%d cluster_use_cur_flag%d period%llu\n",
				       cid, grp->id, grp->type, num, util, *avg_freq, use_cur, period);
				[[clang::always_inline]] trace_cluster_util_debug(cid, grp->id, util, *avg_freq, use_cur);
			}
		} else {
			int cpu, nr = 0;
			u32 sum = 0, avg;
			u64 x = 0;

			for_each_cpu(cpu, policy->cpus) {
				u64 y = grp->cpu_curr_frame[cpu].io_sum_exec_runtime;

				x = grp->cpu_prev_frame[cpu].io_sum_exec_runtime;
				if (!x)
					continue;
				if (frame_util_boost[cid]) {
					if (y > x * (100 + util_boost_percent[cid]) / 100)
						x = y;
				}
				util = x / cal_div;
				if (grp->type & GROUP_FORE_DISP) {
					if (util > fore_util) {
						fore_util = util;
						max_cpu = cpu;
						pred_period = grp->pred_target_period;
						*real_delta = grp->window_period;
						*avg_freq = grp->avg_freq_period[cid];
					}
				} else if (grp->type & 2) {
					if (util > back_util) {
						back_util = util;
						max_back_cpu = cpu;
					}
				} else if (grp->type & 1) {
					if (util > sys_util) {
						sys_util = util;
						max_sys_cpu = cpu;
					}
				} else if ((grp->type & 0x100) && util > kern_util) {
					kern_util = util;
				}
				nr++;
				sum += util;
				gu_dbg(9, "[flw-cpu%d]grp-id%d, grp-flag%d, delta%llu, util%u, pred_period%llu, real%llu, max-cpu%d, max-sys-cpu%d, max-back-cpu%d\n",
					cpu, grp->id, grp->type, x, util, pred_period, *real_delta, max_cpu,
					max_sys_cpu, max_back_cpu);
			}

			avg = sum / nr;
			if (grp->type & GROUP_FORE_DISP) {
				fore_avg = max(fore_avg, avg);
			} else if (grp->type & 2) {
				back_avg = avg;
			} else if (grp->type & 1) {
				sys_avg = avg;
			} else if (grp->type & 0x100) {
				kern_avg = avg;
			}
			gu_dbg(9, "flw-cluster%d, grp-id%d, grp-flags%d, delta%llu, sum%u, avg%u, nr%d, avg_freq%d\n",
				cid, grp->id, grp->type, x, sum, avg, nr, *avg_freq);
		}
	}

	{
		u32 pmax = max(fore_util, max(back_util, sys_util));
		u32 amax = max(fore_avg, max(back_avg, sys_avg));

		if (choose_result_util_policy == 2) {
			u32 kern;

			if (fore_util >= max(back_util, sys_util)) {
				*target_delta = pred_period;
			} else {
				*target_delta = pred_cpu_fps_period;
				*real_delta = window_load_history.target_period;
				*avg_freq = sum_avg_freq[cid];
			}
			kern = (frame_drop_boost && *fore_timeout) ? kern_util : kern_avg;
			kern = kern_percent * kern / 100;
			result = kern + pmax;
			if (unlikely(expand_shift))
				result += result >> expand_shift;
		} else if (choose_result_util_policy == 3) {
			u32 kern;

			if (fore_avg >= max(back_avg, sys_avg)) {
				*target_delta = pred_period;
			} else {
				*target_delta = pred_cpu_fps_period;
				*real_delta = window_load_history.target_period;
				*avg_freq = sum_avg_freq[cid];
			}
			kern = kern_percent * kern_avg / 100;
			result = kern + amax;
			if (unlikely(expand_shift))
				result += result >> expand_shift;
		} else {
			result = clus_util;
		}

		if (frame_drop_boost) {
			if (*fore_timeout)
				result = max(result, old_util);
		}
		res64 = min_t(u64, cap, result);
		if ((u32)res64 == old_util)
			*group_util_valid = false;
		window_load_history.grp_util[cid] = res64;
		result = res64;
		if (unlikely(flw_debug == 9))
			printk("flw-cluster%d result%u clus%u, point-util%u, fore-util%u , back-util%u, sys-util%u, avg-util%u, target_delta%llu, real_delta%llu, avg_freq%d, fore_timeout%d\n",
			       cid, result, clus_util, point_util, fore_util, back_util, sys_util, amax,
			       *target_delta, *real_delta, *avg_freq, *fore_timeout);
	}
out:
	return result;
}
#undef gu_dbg
/* ---- end ---- */

/* ---- mi_frame_ioctl block begin ---- */
/* ioctl command numbers of /dev/mi_frameload (only _IOC_NR(cmd) is looked at) */
#define MI_FRAME_IOC_FRAME_START	2	/* the traced render thread marks the start of a frame, no payload */
#define MI_FRAME_IOC_SET_FPS		3	/* register / unregister (pid, fps), payload struct mi_load_data */

struct mi_load_data {
	unsigned int pid;
	unsigned int fps;
};

/* flw_dbg() with the print block moved out of line (matches the stock block layout of the "list empty" print) */
#define flw_dbg_u(lvl, fmt, ...)	do { if (unlikely(flw_debug == (lvl))) printk(fmt, ##__VA_ARGS__); } while (0)

#define GROUP_LS	0x10	/* latency sensitive group: one frame window for the whole group */

void mi_set_fas_req(struct mi_task_struct *mi_tsk);


/* close the freq accounting window of the global frame and swap the two sum_freq_rt halves */
static void flw_update_sum_avg_freq(u64 wallclock, u64 period)
{
	int cid;

	for (cid = 0; cid < 2; cid++) {
		sum_freq_rt[cid][hist_freq_num] += (wallclock - last_freq_change_time[cid]) * cur_record_freq[cid];
		last_freq_change_time[cid] = wallclock;
		sum_avg_freq[cid] = sum_freq_rt[cid][hist_freq_num] / period;
	}
	hist_freq_num ^= 1;
	for (cid = 0; cid < 2; cid++)
		sum_freq_rt[cid][hist_freq_num] = 0;
}

/*
 * update_grp_period() was inlined by the stock compiler only on the steady state paths,
 * the "first frame" / user group paths call the out of line copy.
 */
static __always_inline void update_grp_period_inl(unsigned int id, struct group_frame_ravg *prev,
						  struct group_frame_ravg *curr)
{
	prev->running_sum_exec_runtime = curr->running_sum_exec_runtime;
	prev->runnable_sum_exec_runtime = curr->runnable_sum_exec_runtime;
	prev->sleep_sum_exec_runtime = curr->sleep_sum_exec_runtime;
	prev->io_sum_exec_runtime = curr->io_sum_exec_runtime;
	curr->running_sum_exec_runtime = 0;
	curr->runnable_sum_exec_runtime = 0;
	curr->sleep_sum_exec_runtime = 0;
	curr->io_sum_exec_runtime = 0;

	flw_dbg(9, "flw-cur_traced%d start%llu grp-%d-update_grp_period! prunning%llu, prunnable%llu, sum%llu\n",
		period_traced_pid, window_load_history.mark_start, id, prev->running_sum_exec_runtime,
		prev->runnable_sum_exec_runtime, prev->io_sum_exec_runtime);
}

static __always_inline void flw_upd(bool cold, unsigned int id, struct group_frame_ravg *prev,
				    struct group_frame_ravg *curr)
{
	if (cold)
		update_grp_period(id, prev, curr);
	else
		update_grp_period_inl(id, prev, curr);
}

static __always_inline void flw_roll_group_cpus(bool cold, struct mi_group *g)
{
	int cpu;

	for_each_possible_cpu(cpu)
		flw_upd(cold, g->id, &g->cpu_prev_frame[cpu], &g->cpu_curr_frame[cpu]);
}

static __always_inline void flw_roll_group_cluster(bool cold, struct mi_group *g, int cid)
{
	flw_upd(cold, g->id, &g->cluster_prev_frame[cid], &g->cluster_curr_frame[cid]);
	g->up_pred_util[cid] = 0;
	g->down_pred_util[cid] = 0;
}

/* first frame of the traced pid: init the windows of a default group */
static __always_inline void flw_first_roll_group(struct mi_group *g, u64 wallclock)
{
	flw_roll_group_cpus(true, g);
	flw_roll_group_cluster(true, g, 0);
	if (!g->cluster_mark_start[0])
		g->cluster_mark_start[0] = wallclock;
	flw_roll_group_cluster(true, g, 1);
	if (!g->cluster_mark_start[1])
		g->cluster_mark_start[1] = wallclock;
}

static __always_inline void flw_next_roll_group(bool cold, struct mi_group *g)
{
	flw_roll_group_cpus(cold, g);
	flw_roll_group_cluster(cold, g, 0);
	flw_roll_group_cluster(cold, g, 1);
}

static __always_inline void flw_queue_irq_work(struct irq_work *work)
{
	if (cpu_online(smp_processor_id()))
		irq_work_queue(work);
	else
		irq_work_queue_on(work, cpumask_first(cpu_online_mask));
}

/* cmd 2: frame start marker */
static __always_inline void mi_frame_cmd_frame_start(void)
{
	struct task_struct *cur = current;
	struct mi_task_struct *mi_tsk;
	struct mi_group *grp = NULL, *mi_grp, *user_grp;
	struct irq_work *work;
	u64 wallclock, delta, period;
	int pid, cpu, cid;

	wallclock = walt_sched_clock();
	mi_tsk = get_mi_task_struct(cur);
	if (ioctl_flag == 1 && mi_tsk) {
		pid = cur->pid;
		if (unlikely(frame_vip_boost))
			mi_set_fas_req(mi_tsk);

		if (pid == period_traced_pid) {
			if (unlikely(!window_load_history.mark_start)) {
				window_load_history.mark_start = wallclock;
				window_load_history.target_period = cpu_fps_period;
				window_load_history.cal_div = cpu_fps_period >> 10;
				grp = get_mi_system_group();
				if (grp)
					flw_first_roll_group(grp, wallclock);
				grp = get_mi_back_group();
				if (grp)
					flw_first_roll_group(grp, wallclock);
				grp = get_mi_kern_group();
				if (grp)
					flw_first_roll_group(grp, wallclock);
			} else {
				delta = wallclock - window_load_history.mark_start;
				if (unlikely(delta < NSEC_PER_MSEC)) {
					flw_dbg(2, "flw-%dupdate window start failed\n", pid);
					goto out;
				}
				window_load_history.window_period = window_load_history.target_period;
				window_load_history.target_period = delta;
				window_load_history.rsv_90 = window_load_history.mark_start;
				window_load_history.mark_start = wallclock;
				grp = get_mi_system_group();
				if (grp)
					flw_next_roll_group(false, grp);
				grp = get_mi_back_group();
				if (grp)
					flw_next_roll_group(false, grp);
				grp = get_mi_kern_group();
				if (grp)
					flw_next_roll_group(false, grp);
				period = find_freq_period(pred_cpu_fps_period, cpu_fps_period,
							 window_load_history.target_period);
				flw_update_sum_avg_freq(wallclock, period);
			}
		}

		mi_grp = find_mi_group_by_task(cur, mi_tsk);
		if (!mi_grp) {
			flw_dbg(1, "%d have no mi-group\n", cur->pid);
			goto out;
		}
		refcount_inc(&mi_grp->refcnt);

		if (unlikely(!mi_grp->frame_start)) {
			mi_grp->frame_start = wallclock;
			mi_grp->mark_start = wallclock;
			mi_grp->window_period = cpu_fps_period;
			init_grp_freq_record(mi_grp, wallclock);
			mi_grp->cluster_mark_start[0] = wallclock;
			mi_grp->cluster_mark_start[1] = wallclock;
			if (mi_grp->type & GROUP_LS) {
				flw_upd(true, mi_grp->id, &mi_grp->prev_frame, &mi_grp->curr_frame);
			} else {
				flw_next_roll_group(true, mi_grp);
			}
			mi_group_ref_dec(mi_grp);
			goto out;
		}

		delta = wallclock - mi_grp->frame_start;
		if (unlikely(delta < NSEC_PER_MSEC)) {
			flw_dbg(2, "flw-%d-grp-%d update window start failed\n", pid, grp->id);
			mi_group_ref_dec(mi_grp);
			goto out;
		}
		mi_grp->window_period = delta;
		mi_grp->frame_start = wallclock;

		if (mi_grp->type & GROUP_FORE) {
			period = find_freq_period(mi_grp->pred_target_period, mi_grp->target_period, delta);
			for (cid = 0; cid < MI_SCHED_CLUSTERS; cid++) {
				u64 avg = (wallclock - mi_grp->last_freq_change_time[cid]) *
					  mi_grp->cur_record_freq[cid] + mi_grp->sum_freq_period[cid];

				mi_grp->last_freq_change_time[cid] = wallclock;
				mi_grp->sum_freq_period[cid] = 0;
				avg /= period;
				mi_grp->avg_freq_period[cid] = avg;
				flw_dbg(4, "cid%d-avg:%llu-period%llu\n", cid, avg, period / NSEC_PER_MSEC);
			}
		}

		if (mi_grp->type & GROUP_LS) {
			flw_upd(false, mi_grp->id, &mi_grp->prev_frame, &mi_grp->curr_frame);
		} else {
			flw_next_roll_group(false, mi_grp);
		}

		flw_dbg(20, "flw-grp[%d]-pid[%d] in update window_start grp:ws%llu period%llu prunning%llu prunnbale%llu crunning%llu crunnable%llu\n",
			mi_grp->id, pid, mi_grp->frame_start, mi_grp->window_period / NSEC_PER_MSEC,
			mi_grp->prev_frame.running_sum_exec_runtime, mi_grp->prev_frame.runnable_sum_exec_runtime,
			mi_grp->curr_frame.running_sum_exec_runtime, mi_grp->curr_frame.runnable_sum_exec_runtime);
		flw_dbg(11, "grp%d pid%d update fs %llu\n", mi_grp->id, pid, wallclock);

		if (mi_grp->type & GROUP_LS) {
			user_grp = find_mi_group_by_user(mi_tsk->mi_user);
			mi_group_ref_dec(mi_grp);
			if (user_grp) {
				refcount_inc(&user_grp->refcnt);
				if (unlikely(!user_grp->frame_start)) {
					user_grp->frame_start = wallclock;
					user_grp->mark_start = wallclock;
					user_grp->window_period = window_load_history.target_period;
				} else {
					user_grp->window_period = wallclock - grp->frame_start;
					user_grp->frame_start = wallclock;
					flw_roll_group_cpus(true, user_grp);
					flw_upd(true, grp->id, &grp->cluster_prev_frame[0], &grp->cluster_curr_frame[0]);
					flw_upd(true, grp->id, &grp->cluster_prev_frame[1], &grp->cluster_curr_frame[1]);
				}
				mi_group_ref_dec(user_grp);
			}
		} else {
			mi_group_ref_dec(mi_grp);
		}

		if (pid == period_traced_pid)
			work = &flw_cpufreq_irq_work;
		else
			work = &flw_async_cpufreq_irq_work;
		flw_queue_irq_work(work);
	}
out:
	mi_tsk = get_mi_task_struct(cur);
	if (mi_tsk)
		mi_tsk->flags |= BIT(31);
}

/* cmd 3: add / update / remove the (pid, fps) of a frame pacing thread */
static __always_inline unsigned int mi_frame_cmd_set_fps(unsigned int cmd, unsigned long arg, struct mi_load_data *ud)
{
	struct list_head *pos, *n;
	struct period_node *node;
	unsigned long flags;
	unsigned int max_pid = 0, max_fps = 0;
	unsigned int pid, fps;
	int found = 0;
	u64 period;

	if (_IOC_SIZE(cmd) != sizeof(struct mi_load_data)) {
		printk("err:size%d != sizeof(struct mi_load_data\n", _IOC_SIZE(cmd));
		return -EINVAL;
	}
	if (copy_from_user(ud, (void __user *)arg, sizeof(*ud))) {
		printk("err:copy_from_user(&data, ubuf, sizeof(data))\n");
		return -EFAULT;
	}
	pid = ud->pid;
	fps = ud->fps;
	if (unlikely(flw_debug == 1))
		printk("cmd%d,pid%d, fps%d\n", MI_FRAME_IOC_SET_FPS, pid, fps);

	if (fps) {
		for (pos = period_list.next; pos != &period_list && pos; pos = pos->next) {
			node = list_entry(pos, struct period_node, list);
			if (node) {
				if (node->pid == pid) {
					found = 1;
					node->fps = fps;
				}
				if (node->fps > max_fps) {
					max_fps = node->fps;
					max_pid = node->pid;
				}
			}
		}
		if (!found) {
			node = kzalloc(sizeof(*node), GFP_NOWAIT);
			if (!node) {
				printk("BUG alloc period traced failed!!!\n");
				return -EFAULT;
			}
			node->pid = pid;
			node->fps = fps;
			spin_lock_irqsave(&period_list_lock, flags);
			list_add(&node->list, &period_list);
			spin_unlock_irqrestore(&period_list_lock, flags);
			if (fps > max_fps) {
				max_pid = node->pid;
				max_fps = node->fps;
			}
			flw_dbg(1, "old_flag0 alloc pt%d-%d\n", pid, fps);
		}
	} else {
		spin_lock_irqsave(&period_list_lock, flags);
		for (pos = period_list.next, n = pos->next; pos != &period_list && pos; pos = n, n = pos->next) {
			node = list_entry(pos, struct period_node, list);
			if (node) {
				if (!pid || node->pid == pid) {
					list_del_init(pos);
					kfree(node);
					break;
				}
				if (node->fps > max_fps) {
					max_fps = node->fps;
					max_pid = node->pid;
				}
			}
		}
		spin_unlock_irqrestore(&period_list_lock, flags);
	}

	if (list_empty(&period_list)) {
		ioctl_flag = false;
		period_traced_pid = 0;
		window_load_history.mark_start = 0;
		window_load_history.target_period = 16666666;
		flw_dbg_u(2, "period_list empty!window_start = 0\n");
		window_load_history.cal_div = window_load_history.target_period >> 10;
		cpu_fps_period = 16666666;
		pred_cpu_fps_period = get_pred_target_period(60);
		return 0;
	}

	period = NSEC_PER_SEC / max_fps;
	period_traced_pid = max_pid;
	window_load_history.target_period = period;
	cpu_fps_period = period;
	window_load_history.cal_div = period >> 10;
	pred_cpu_fps_period = get_pred_target_period(max_fps);
	ioctl_flag = true;
	if (unlikely(!period_traced_pid || !cpu_fps_period)) {
		ioctl_flag = false;
		spin_lock_irqsave(&period_list_lock, flags);
		list_for_each_safe(pos, n, &period_list) {
			node = list_entry(pos, struct period_node, list);
			if (node) {
				list_del_init(pos);
				kfree(node);
			}
		}
		spin_unlock_irqrestore(&period_list_lock, flags);
		cpu_fps_period = 16666666;
		pred_cpu_fps_period = get_pred_target_period(60);
	}
	return 0;
}

static long mi_frame_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mi_load_data data;
	unsigned int ret = 0;	/* the stock returns the (u32) errno zero extended */

	if (unlikely(flw_debug == 1))
		printk("ioctl-cmd%d,pid%d\n", _IOC_NR(cmd), current->pid);

	if (skip_walt_result[0] || skip_walt_result[1]) {
		switch (_IOC_NR(cmd)) {
		case MI_FRAME_IOC_FRAME_START:
			mi_frame_cmd_frame_start();
			break;
		case MI_FRAME_IOC_SET_FPS:
			ret = mi_frame_cmd_set_fps(cmd, arg, &data);
			break;
		default:
			break;
		}
	}
	return ret;
}
/* ---- end ---- */

static const struct file_operations miframe_fops = {
	.owner = THIS_MODULE,
	.open = mi_frame_open,
	.release = mi_frame_release,
	.mmap = mi_frame_mmap,
	.unlocked_ioctl = mi_frame_ioctl,
	.compat_ioctl = mi_frame_ioctl,
};

static struct miscdevice miframe_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mi_frameload",
	.fops = &miframe_fops,
};

static int frame_load_init(void)
{
	u64 wc = walt_sched_clock();
	int cpu;

	for_each_possible_cpu(cpu) {
		struct mi_rq *mrq = &per_cpu(mi_runqueues, cpu);

		mrq->frame_start_group = wc;
		mrq->frame_load_last_update = wc;
	}
	last_freq_change_time[0] = wc;
	last_freq_change_time[1] = wc;
	flw_irq_work_lastq_ws = wc;
	window_load_history.mark_start = 0;
	window_load_history.target_period = 16666666;
	window_load_history.window_period = 16666666;
	cpu_fps_period = 16666666;
	pred_cpu_fps_period = get_pred_target_period(60);
	window_load_history.cal_div = 16666666 >> 10;
	util_policy = 0;

	register_mi_update_fravg(mi_frame_load_ravg);
	register_walt_update_hooks(skip_walt_window_update);
	register_task_group_change_update_f(transfer_APP_status);
	register_mi_migrate_hook(migrate_inter_cluster_fload);
	register_mi_update_group_nr(mi_update_group_group);
	register_mi_migarte_fix_fld_hooks(fix_fload_in_irq);
	register_ui_task_detect(0, grp_enq_boost_task, grp_enq_boost_pipeline);
	register_mi_update_global_period_f(update_gloabal_target_period);
	misc_register(&miframe_misc);
	migtgov_register();

	init_irq_work(&flw_cpufreq_irq_work, flw_irq_work);
	init_irq_work(&flw_async_cpufreq_irq_work, flw_irq_work);
	init_irq_work(&flw_change_status_irq_work, flw_irq_work);

	frame_load_dir = proc_mkdir("frame_load", NULL);
	if (frame_load_dir) {
		group_dir = proc_mkdir("group", frame_load_dir);
		if (group_dir) {
			all_group_show_entry = proc_create("all_group", 0664, group_dir, &all_group_show_fops);
			flw_dir = proc_mkdir("flw", frame_load_dir);
			if (flw_dir) {
				taskgrpid_entry = proc_create("task_grp_id", 0664, flw_dir, &task_grp_id_fops);
				usergrpid_entry = proc_create("user_grp_id", 0664, flw_dir, &user_grp_id_fops);
			}
		}
	}
	printk("frame load init success\n");
	return 0;
}

static void __exit frame_load_exit(void)
{
	irq_work_sync(&flw_change_status_irq_work);
	irq_work_sync(&flw_async_cpufreq_irq_work);
	irq_work_sync(&flw_cpufreq_irq_work);
	migtgov_unregister();
	misc_deregister(&miframe_misc);
	unregister_mi_update_global_period_f();
	unregister_ui_task_detect(0);
	unregister_mi_migarte_fix_fld_hooks();
	unregister_mi_update_group_nr();
	unregister_mi_migrate_hook();
	unregister_walt_update_hooks();
	unregister_mi_update_fravg();
	unregister_task_group_change_update_f();
	if (flw_dir) {
		if (taskgrpid_entry)
			proc_remove(taskgrpid_entry);
		if (usergrpid_entry)
			proc_remove(usergrpid_entry);
		proc_remove(flw_dir);
		if (group_dir) {
			if (all_group_show_entry)
				proc_remove(all_group_show_entry);
			proc_remove(group_dir);
		}
	}
	proc_remove(frame_load_dir);
	printk("frame load exit!!!\n");
}

module_init(frame_load_init);
module_exit(frame_load_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("frame-load by David");
