// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Xiaomi QOS sched driver (OS4): qos events -> RT/VIP/RTG scheduling. Reconstructed from the stock binary.
 */

#define pr_fmt(fmt) "qs: " fmt

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <trace/hooks/binder.h>

#include "xr_qs.h"

#define CREATE_TRACE_POINTS
#include "trace_xr_qs.h"

#define SYS_MODE	0640

struct draw_rtg_info {
	u32 id;
	u32 valid;
	u32 tgid;
	u32 pad;
	union {
		struct {
			struct task_struct *ui;
			struct task_struct *render;
		};
		struct task_struct *task[2];
	};
};

struct binder_proc_view {
	u8 pad0[0x48];
	struct task_struct *tsk;
	u8 pad1[0x198 - 0x50];
	struct {
		unsigned int sched_policy;
		int prio;
	} default_priority;
};

int sl_init(void);

static struct draw_rtg_info g_draw_grp[4];
static u32 g_qs_enable;
static DEFINE_SPINLOCK(g_qs_lock);
static int g_ui_prio = 98;
static int g_render_prio = 98;

static void android_vh_binder_preset(void *unused, struct hlist_head *hhead,
				     struct mutex *lock, struct binder_proc *proc)
{
	struct binder_proc_view *p = (struct binder_proc_view *)proc;
	struct qs_struct *qs;

	if (!p)
		return;
	if (!p->tsk)
		return;
	qs = task_to_qs(p->tsk);
	if (!qs)
		return;
	if (!qs->rt_sched)
		return;
	if (qs->policy_backup == SCHED_NORMAL || qs->policy_backup == SCHED_BATCH) {
		p->default_priority.sched_policy = SCHED_NORMAL;
		p->default_priority.prio = NICE_TO_PRIO(0);
		trace_qs_binder_preset(p->tsk);
	}
}

static ssize_t sched_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "QS_ENABLE:\t%d\n", !!g_qs_enable);
}

static ssize_t sched_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	u32 enable = 0;

	if (kstrtouint(buf, 0, &enable)) {
		pr_err("sched enable store: parse enable failed\n");
		return -EINVAL;
	}
	g_qs_enable = !!enable;
	return count;
}

static ssize_t rt_prio_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "ui prio:\t%d\nrender prio:\t%d\n", g_ui_prio, g_render_prio);
}

static ssize_t rt_prio_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int ui_prio = 0;
	u32 render_prio = 0;

	if (sscanf(buf, "%d %d", &ui_prio, &render_prio) != 2) {
		pr_err("prio store: ui/render prio failed\n");
		return -EINVAL;
	}
	if (ui_prio < 90 || ui_prio > 92 || render_prio > 92 || render_prio < 90) {
		pr_err("invalid prio: ui/render[%d %d]\n", ui_prio, render_prio);
		return -EINVAL;
	}
	g_ui_prio = ui_prio;
	g_render_prio = render_prio;
	return count;
}

static void restore_rt_sched(struct task_struct *p, struct qs_struct *qs)
{
	struct sched_param param = { 0 };

	if (!qs->rt_sched)
		return;
	if (qs->policy_backup != SCHED_NORMAL && qs->policy_backup != SCHED_BATCH)
		param.sched_priority = MAX_RT_PRIO - 1 - qs->prio_backup;
	sched_setscheduler_nocheck(p, qs->policy_backup, &param);
	trace_qs_restore_rt_sched(p, qs->policy_backup, qs->prio_backup);
	qs->rt_sched = 0;
	qs->policy_backup = 0;
	qs->prio_backup = 0;
}

static void correct_qos_sched(struct qievent_struct *data)
{
	struct task_struct *p = data->to;
	struct sched_param param = { 0 };
	struct qs_struct *qs;

	get_task_struct(p);
	qs = task_to_qs(p);
	if (qs && qs->rt_sched &&
	    (p->policy == SCHED_NORMAL || p->policy == SCHED_BATCH)) {
		param.sched_priority = MAX_RT_PRIO - 1 - data->prio;
		sched_setscheduler_nocheck(p, SCHED_FIFO | SCHED_RESET_ON_FORK, &param);
		trace_qs_correct_rt_sched(p, data->prio);
	}
	put_task_struct(p);
}


static inline int fair_policy(int policy)
{
	return policy == SCHED_NORMAL || policy == SCHED_BATCH;
}

static bool is_render_thread(struct task_struct *p)
{
	return !strcmp(p->comm, "RenderThread") || !strcmp(p->comm, p->group_leader->comm);
}

#define RTG_DRAW_ID	12
#define RTG_ID_VALID(id)	((id) >= 2 && (id) <= 10)

static noinline int move_existed_draw_rtg(bool is_ui, struct task_struct *p, struct draw_rtg_info *d)
{
	int ret = plat_sched_set_group_id(p, RTG_DRAW_ID);

	if (is_ui) {
		g_draw_grp[0].ui = p;
		d->ui = NULL;
		if (d->render)
			return ret;
	} else {
		g_draw_grp[0].render = p;
		d->render = NULL;
		if (d->ui)
			return ret;
	}
	d->valid = 0;
	d->tgid = 0;
	return ret;
}

static noinline int exchange_draw_rtg(bool is_ui, struct task_struct *p,
				      struct draw_rtg_info *from, struct draw_rtg_info *to)
{
	struct task_struct *old_ui = NULL, *old_render = NULL;
	u32 old_tgid = 0;
	int ret;

	if (to->valid) {
		old_ui = to->ui;
		old_render = to->render;
		old_tgid = to->tgid;
	}
	ret = plat_sched_set_group_id(p, to->id);
	if (ret < 0)
		return ret;

	to->tgid = p->tgid;
	to->valid = 1;
	to->ui = is_ui ? p : NULL;
	to->render = is_ui ? NULL : p;
	from->valid = 0;
	from->tgid = 0;
	from->ui = NULL;
	from->render = NULL;

	if (old_ui && !old_ui->exit_state && old_ui->pid == old_tgid) {
		get_task_struct(old_ui);
		plat_sched_set_group_id(old_ui, from->id);
		trace_qs_exchange_rtg_grp(old_ui, to->id, from->id, ret);
		from->ui = old_ui;
		from->valid = 1;
		from->tgid = old_tgid;
		put_task_struct(old_ui);
	}
	if (old_render && !old_render->exit_state && old_render->tgid == old_tgid) {
		get_task_struct(old_render);
		plat_sched_set_group_id(old_render, from->id);
		trace_qs_exchange_rtg_grp(old_render, to->id, from->id, ret);
		from->render = old_render;
		if (!from->valid) {
			from->valid = 1;
			from->tgid = old_tgid;
		}
		put_task_struct(old_render);
	}
	return ret;
}

/* detach @p from the draw table (g_qs_lock held) */
static void clear_draw_rtg(struct task_struct *p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_draw_grp); i++) {
		struct draw_rtg_info *d = &g_draw_grp[i];
		struct task_struct **slot, *other, *u;

		if (!d->valid)
			continue;
		u = d->ui;
		if (u == p) {
			slot = &d->ui;
			other = d->render;
		} else {
			slot = &d->render;
			if (*slot != p)
				continue;
			other = u;
		}
		*slot = NULL;
		if (!other) {
			d->tgid = 0;
			d->valid = 0;
		}
		break;
	}
}

static int get_draw_idx(u32 grp)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(g_draw_grp); i++)
		if (g_draw_grp[i].id == grp)
			return i;
	return 0;
}

static noinline void set_rtg_grp(struct qievent_struct *data, struct qs_struct *qs, u32 lvl, bool set)
{
	struct task_struct *from = data->from;
	struct task_struct *p = data->to;
	u32 qos = data->to_qos.qos;
	u32 orig_grp, new_grp, grp;
	int ret = 0;
	int i, idx;
	bool is_ui;

	orig_grp = plat_sched_get_group_id(p);
	if (RTG_ID_VALID(orig_grp)) {
		trace_qs_skip_rtg_grp(p, orig_grp, 0);
		return;
	}

	if (from) {
		grp = plat_sched_get_group_id(from);
		if (RTG_ID_VALID(grp)) {
			new_grp = grp;
			ret = plat_sched_set_group_id(p, new_grp);
			if ((orig_grp == RTG_DRAW_ID || get_draw_idx(orig_grp)) && p) {
				spin_lock(&g_qs_lock);
				clear_draw_rtg(p);
				spin_unlock(&g_qs_lock);
			}
			goto done;
		}
	}

	spin_lock(&g_qs_lock);
	switch (lvl) {
	case 6:
		new_grp = 0;
		for (i = 1; i < ARRAY_SIZE(g_draw_grp); i++) {
			struct draw_rtg_info *d = &g_draw_grp[i];

			if (p && d->valid && p->tgid == d->tgid) {
				new_grp = d->id;
				break;
			}
			if (!d->valid && !new_grp)
				new_grp = d->id;
		}
		break;
	case 10:
		new_grp = 11;
		break;
	case 9:
		new_grp = RTG_DRAW_ID;
		break;
	case 7:
		new_grp = 14;
		break;
	case 8:
		new_grp = 13;
		break;
	default:
		new_grp = 0;
		break;
	}
	if (qos & 0xc0)
		new_grp = 13;

	if (orig_grp == new_grp || !new_grp) {
		spin_unlock(&g_qs_lock);
		trace_qs_skip_rtg_grp(p, orig_grp, new_grp);
		return;
	}

	if (!set) {
		if (get_static_qos_lvl(p) > 5)
			ret = -1;
		else
			ret = plat_sched_set_group_id(p, new_grp);
		goto unlock;
	}

	is_ui = p->pid == p->tgid;
	if (!is_ui && !is_render_thread(p)) {
		ret = plat_sched_set_group_id(p, new_grp);
		goto unlock;
	}

	idx = get_draw_idx(new_grp);
	if (idx) {
		struct draw_rtg_info *d = &g_draw_grp[idx];

		if (orig_grp == RTG_DRAW_ID) {
			if (d->valid == 1 && d->tgid == p->tgid) {
				ret = plat_sched_set_group_id(p, new_grp);
				if (is_ui) {
					d->ui = p;
					g_draw_grp[0].ui = NULL;
					if (g_draw_grp[0].render)
						goto unlock;
				} else {
					d->render = p;
					g_draw_grp[0].render = NULL;
					if (g_draw_grp[0].ui)
						goto unlock;
				}
				g_draw_grp[0].valid = 0;
				g_draw_grp[0].tgid = 0;
			} else {
				ret = exchange_draw_rtg(is_ui, p, &g_draw_grp[0], d);
			}
		} else {
			struct task_struct *other;

			ret = plat_sched_set_group_id(p, new_grp);
			if (is_ui) {
				other = d->render;
				d->ui = p;
				if (other && other->tgid != p->tgid)
					g_draw_grp[idx].render = NULL;
			} else {
				other = d->ui;
				d->render = p;
				if (other && other->tgid != p->tgid)
					g_draw_grp[idx].ui = NULL;
			}
			if (!d->valid) {
				d->valid = 1;
				d->tgid = p->tgid;
			}
		}
		goto unlock;
	}

	if (new_grp != RTG_DRAW_ID) {
		struct draw_rtg_info *d = NULL;

		ret = plat_sched_set_group_id(p, new_grp);
		if (orig_grp == RTG_DRAW_ID)
			d = &g_draw_grp[0];
		else if ((idx = get_draw_idx(orig_grp)))
			d = &g_draw_grp[idx];
		if (d) {
			if (is_ui) {
				d->ui = NULL;
				if (d->render)
					goto unlock;
			} else {
				d->render = NULL;
				if (d->ui)
					goto unlock;
			}
			d->valid = 0;
			d->tgid = 0;
		}
		goto unlock;
	}

	/* new_grp == RTG_DRAW_ID */
	idx = get_draw_idx(orig_grp);
	if (idx) {
		struct draw_rtg_info *src = &g_draw_grp[idx];

		if (g_draw_grp[0].valid == 1 && g_draw_grp[0].tgid == p->tgid)
			ret = move_existed_draw_rtg(is_ui, p, src);
		else
			ret = exchange_draw_rtg(is_ui, p, src, &g_draw_grp[0]);
		new_grp = RTG_DRAW_ID;
		goto unlock;
	}

	if (g_draw_grp[0].valid == 1 && g_draw_grp[0].tgid == p->tgid) {
		ret = plat_sched_set_group_id(p, RTG_DRAW_ID);
		if (is_ui)
			g_draw_grp[0].ui = p;
		else
			g_draw_grp[0].render = p;
		new_grp = RTG_DRAW_ID;
		goto unlock;
	}

	{
		struct draw_rtg_info *free = NULL;

		grp = 0;
		for (i = 1; i < ARRAY_SIZE(g_draw_grp); i++)
			if (!g_draw_grp[i].valid && !grp)
				grp = g_draw_grp[i].id;
		for (i = 1; i < ARRAY_SIZE(g_draw_grp); i++)
			if (g_draw_grp[i].id == grp) {
				free = &g_draw_grp[i];
				break;
			}
		if (likely(free && g_draw_grp[0].valid)) {
			bool moved = false;
			int mret;

			int j;

#pragma clang loop unroll(full)
			for (j = 0; j < 2; j++) {
				struct task_struct *t = g_draw_grp[0].task[j];

				if (t && !t->exit_state) {
					get_task_struct(t);
					mret = plat_sched_set_group_id(g_draw_grp[0].task[j], free->id);
					trace_qs_move_rtg_grp(g_draw_grp[0].task[j], g_draw_grp[0].id, free->id, mret);
					free->task[j] = g_draw_grp[0].task[j];
					free->tgid = g_draw_grp[0].task[j]->tgid;
					put_task_struct(g_draw_grp[0].task[j]);
					g_draw_grp[0].task[j] = NULL;
					moved = true;
				}
			}
			g_draw_grp[0].valid = 0;
			g_draw_grp[0].tgid = 0;
			free->valid = moved;
		}
	}
	ret = plat_sched_set_group_id(p, RTG_DRAW_ID);
	if (is_ui) {
		g_draw_grp[0].ui = p;
		if (g_draw_grp[0].render && g_draw_grp[0].render->tgid != p->tgid)
			g_draw_grp[0].render = NULL;
	} else {
		g_draw_grp[0].render = p;
		if (g_draw_grp[0].ui && g_draw_grp[0].ui->tgid != p->tgid)
			g_draw_grp[0].ui = NULL;
	}
	g_draw_grp[0].valid = 1;
	g_draw_grp[0].tgid = p->tgid;
	new_grp = RTG_DRAW_ID;
unlock:
	spin_unlock(&g_qs_lock);
done:
	trace_qs_set_rtg_grp(p, orig_grp, new_grp, ret);
	if (!ret)
		qs->rtg_set_flag = 1;
}

static void qos_set_callback(struct qievent_struct *data)
{
	struct task_struct *task = data->to;
	u32 qos = data->from_qos.qos;
	struct qs_struct *qs;
	int policy, prio, min_prio, vip;

	get_task_struct(task);
	qs = task_to_qs(data->to);
	if (!qs)
		goto out;

	if (qos & BIT(4)) {
		if (!qs->rt_sched) {
			qs->policy_backup = task->policy;
			qs->prio_backup = task->prio;
			qs->rt_sched = 1;
		}
		if ((qos & 0xf) == 9) {
			min_prio = min(task->prio, data->prio);
			if (task->pid == task->tgid)
				prio = g_ui_prio;
			else if (is_render_thread(task))
				prio = g_render_prio;
			else
				prio = 98;
			update_static_prio(task, prio, -1);
			if (prio > min_prio) {
				trace_qs_skip_rt_sched(task, SCHED_RR, min_prio, prio);
			} else {
				struct sched_param param;

				param.sched_priority = MAX_RT_PRIO - 1 - prio;
				sched_setscheduler_nocheck(task, SCHED_RR | SCHED_RESET_ON_FORK, &param);
				trace_qs_set_rt_sched(task, SCHED_RR, prio);
			}
		} else {
			policy = task->policy;
			prio = task->prio;
			if (fair_policy(policy))
				policy = SCHED_RR;
			update_static_prio(task, prio, -1);
			min_prio = data->prio;
			if (prio > min_prio) {
				struct sched_param param;

				param.sched_priority = MAX_RT_PRIO - 1 - min_prio;
				sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK, &param);
				trace_qs_set_rt_sched(task, policy, min_prio);
			} else {
				trace_qs_skip_rt_sched(task, policy, min_prio, prio);
			}
		}
	} else if (qos & BIT(5)) {
		update_static_prio(task, -1, 5);
		vip = data->vip_prio > 5 ? data->vip_prio : 5;
		set_vip_prio(data->to, vip, false);
		trace_qs_set_vip_sched(data->from, data->to, 5, data->to_qos.qos);
		if (!qs->vip_sched)
			qs->vip_sched = 1;
	}
	set_rtg_grp(data, qs, data->to_qos.qos & 0xf, true);
out:
	put_task_struct(data->to);
}

static void qos_inherit_callback(struct qievent_struct *data)
{
	struct task_struct *task = data->to;
	u32 qos = data->to_qos.qos;
	struct qs_struct *qs;
	int policy, prio, vip;

	get_task_struct(task);
	qs = task_to_qs(task);
	if (unlikely(!qs)) {
		[[clang::__noinline__]] put_task_struct(task);
		return;
	}

	if (qos & BIT(4)) {
		if (!qs->rt_sched) {
			qs->policy_backup = task->policy;
			qs->prio_backup = task->prio;
		}
		policy = task->policy;
		if (fair_policy(policy)) {
			policy = data->from->policy;
			if (fair_policy(policy)) {
				barrier();
				policy = SCHED_RR;
			}
		} else if (data->prio >= task->prio) {
			goto rtg;
		}
		prio = data->prio;
		{
			struct sched_param param;

			param.sched_priority = MAX_RT_PRIO - 1 - prio;
			sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK, &param);
		}
		trace_qs_set_rt_sched(task, policy, prio);
		if (!qs->rt_sched)
			qs->rt_sched = 1;
	} else if (qos & BIT(5)) {
		vip = get_vip_prio(task);
		if (data->vip_prio > vip) {
			set_vip_prio(task, data->vip_prio, false);
			trace_qs_set_vip_sched(data->from, data->to, data->vip_prio, data->to_qos.qos);
		}
		if (!qs->vip_sched)
			qs->vip_sched = 1;
	}
rtg:
	if (!(qos & 0xc0))
		set_rtg_grp(data, qs, qos & 0xf, false);
	put_task_struct(task);
}

static void restore_qos_sched(struct qievent_struct *data, unsigned long action)
{
	struct task_struct *task = data->to;
	u32 qos = data->to_qos.qos;
	struct qs_struct *qs;
	u32 lvl;

	get_task_struct(task);
	qs = task_to_qs(task);
	if (!qs)
		goto out;
	lvl = qos & 0xf;
	if (action != QIEVENT_QOS_UNINHERIT)
		update_static_prio(task, (qs->policy_backup == SCHED_NORMAL || qs->policy_backup == SCHED_BATCH) ? 120 : -1, 0);

	if (lvl <= 5) {
		unsigned int grp;

		restore_rt_sched(task, qs);
		if (qs->vip_sched) {
			restore_vip_prio(task);
			trace_qs_reset_vip_sched(task);
			qs->vip_sched = 0;
		}
		grp = plat_sched_get_group_id(task);
		if (grp && qs->rtg_set_flag) {
			struct draw_rtg_info *d = NULL;
			int i;

			plat_sched_set_group_id(task, 0);
			trace_qs_restore_rtg_grp(task);
			qs->rtg_set_flag = 0;
			for (i = 0; i < ARRAY_SIZE(g_draw_grp); i++) {
				if (g_draw_grp[i].id == grp) {
					d = &g_draw_grp[i];
					break;
				}
			}
			if (d) {
				spin_lock(&g_qs_lock);
				if (task->pid == task->tgid) {
					d->ui = NULL;
					if (!d->render) {
						d->tgid = 0;
						d->valid = 0;
					}
				} else if (is_render_thread(task)) {
					d->render = NULL;
					if (!d->ui) {
						d->tgid = 0;
						d->valid = 0;
					}
				}
				spin_unlock(&g_qs_lock);
			}
		}
	} else {
		if (qos & BIT(4)) {
			int prio = get_task_prio(task);
			int policy = task->policy;

			struct sched_param param;

			param.sched_priority = MAX_RT_PRIO - 1 - prio;
			sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK, &param);
			trace_qs_set_rt_sched(task, policy, prio);
			trace_qs_update_rt_sched(task, task->policy, data->to_qos.qos, prio);
		} else if (qos & BIT(5)) {
			restore_rt_sched(task, qs);
			set_vip_prio(task, data->vip_prio, false);
			trace_qs_set_vip_sched(data->from, data->to, data->vip_prio, data->to_qos.qos);
			if (!qs->vip_sched)
				qs->vip_sched = 1;
		} else {
			goto out;
		}
		set_rtg_grp(data, qs, lvl, false);
	}
out:
	put_task_struct(task);
}

static void qos_reset_callback(struct qievent_struct *data)
{
	struct task_struct *task = data->to;
	u32 qos = data->to_qos.qos;
	struct qs_struct *qs;
	int policy = task->policy;
	int prio;

	get_task_struct(task);
	qs = task_to_qs(task);
	if (unlikely(!qs)) {
		[[clang::__noinline__]] put_task_struct(task);
		return;
	}
	if ((data->from_qos.qos & 0xf) >= 6) {
		if (data->from_qos.qos & BIT(4)) {
			if ((data->from_qos.qos & 0xf) == 9) {
				if (task->pid == task->tgid)
					prio = g_ui_prio;
				else if (is_render_thread(task))
					prio = g_render_prio;
				else
					prio = 98;
			} else {
				prio = task->prio;
			}
			update_static_prio(task, prio, -1);
		}
		if (data->from_qos.qos & BIT(5))
			update_static_prio(task, -1, 5);
	} else {
		update_static_prio(task, 120, 0);
	}

	if ((qos & 0x30) == 0x20) {
		restore_rt_sched(task, qs);
		if (data->vip_prio <= 4) {
			set_vip_prio(data->to, 5, false);
			trace_qs_set_vip_sched(data->from, data->to, 5, data->to_qos.qos);
			if (!qs->vip_sched)
				qs->vip_sched = 1;
		}
	} else {
		if ((data->from_qos.qos & 0xf) == 9) {
			policy = SCHED_RR;
		} else {
			int tprio = task->prio;

			qs->policy_backup = policy;
			qs->prio_backup = tprio;
		}
		prio = get_task_prio(task);
		if (prio < task->prio) {
			struct sched_param param;

			param.sched_priority = MAX_RT_PRIO - 1 - prio;
			sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK, &param);
			trace_qs_set_rt_sched(task, policy, prio);
			if (!qs->rt_sched)
				qs->rt_sched = 1;
		}
	}
	set_rtg_grp(data, qs, qos & 0xf, true);
	put_task_struct(task);
}

static int qievent_callback(struct notifier_block *nb, unsigned long action, void *data)
{
	struct qievent_struct *e = data;
	int ret = 0;

	if (!e)
		return -EINVAL;
	if (unlikely(in_interrupt()))
		goto out;

	if (action == QIEVENT_TASK_EXIT) {
		struct task_struct *p;

		if (!g_qs_enable)
			goto out;
		p = e->to;
		if (!p)
			goto out;
		spin_lock(&g_qs_lock);
		clear_draw_rtg(p);
		spin_unlock(&g_qs_lock);
		goto out;
	}

	if (e->to->flags & PF_EXITING) {
		ret = -1;
		goto out;
	}

	switch (action) {
	case QIEVENT_QOS_SET:
		if (!g_qs_enable)
			break;
		if ((e->from_qos.qos & 0xe) >= 6)
			qos_set_callback(e);
		break;
	case QIEVENT_QOS_UNSET:
		if (!g_qs_enable)
			break;
		if ((e->from_qos.qos & 0xe) <= 5)
			restore_qos_sched(e, QIEVENT_QOS_UNSET);
		else
			qos_reset_callback(e);
		break;
	case QIEVENT_QOS_RESTORE:
	case QIEVENT_QOS_UNINHERIT:
		restore_qos_sched(e, action);
		break;
	case QIEVENT_QOS_INHERIT:
		if (!g_qs_enable)
			break;
		qos_inherit_callback(e);
		break;
	case QIEVENT_QOS_CORRECT:
		if (!g_qs_enable)
			break;
		correct_qos_sched(e);
		break;
	}
out:
	return ret;
}

static struct notifier_block g_qs_nb = {
	.notifier_call = qievent_callback,
};

static struct kobj_attribute sched_enable_attr = __ATTR(sched_enable, SYS_MODE, sched_enable_show, sched_enable_store);
static struct kobj_attribute rt_prio_attr = __ATTR(rt_prio, SYS_MODE, rt_prio_show, rt_prio_store);

static struct attribute *qs_attrs[] = {
	&sched_enable_attr.attr,
	&rt_prio_attr.attr,
	NULL,
};

static struct attribute_group qs_attr_group = {
	.attrs = qs_attrs,
};

static int __init qs_init(void)
{
	int ret;
	struct kobject *qs_kobj;

	g_draw_grp[0].id = 12;
	g_draw_grp[1].id = 15;
	g_draw_grp[2].id = 16;
	g_draw_grp[3].id = 17;

	qs_kobj = kobject_create_and_add("qos_sched", kernel_kobj);
	if (!qs_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(qs_kobj, &qs_attr_group);
	if (ret) {
		kobject_put(qs_kobj);
		pr_err("qs init: failed to create sys node\n");
		return -EINVAL;
	}

	ret = register_trace_android_vh_binder_preset(android_vh_binder_preset, NULL);
	if (ret < 0) {
		pr_err("register hook failed, err:%d\n", ret);
		goto err_exit;
	}

	ret = register_qievent_notifier(&g_qs_nb);
	if (ret < 0) {
		unregister_trace_android_vh_binder_preset(android_vh_binder_preset, NULL);
		pr_err("register qievent failed, err:%d\n", ret);
		goto err_exit;
	}

	ret = sl_init();
	if (ret < 0) {
		unregister_qievent_notifier(&g_qs_nb);
		unregister_trace_android_vh_binder_preset(android_vh_binder_preset, NULL);
		pr_err("speed lock probe failed\n");
		goto err_exit;
	}

	pr_info("qos sched and speed lock probe successfully\n");
	return ret;

err_exit:
	sysfs_remove_group(qs_kobj, &qs_attr_group);
	kobject_put(qs_kobj);
	return ret;
}

module_init(qs_init);

MODULE_AUTHOR("Shaohua Fan <fanshaohua@xiaomi.com>");
MODULE_DESCRIPTION("OS-CPU QOS sched Driver");
MODULE_LICENSE("GPL v2");
