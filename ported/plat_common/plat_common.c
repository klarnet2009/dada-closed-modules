// SPDX-License-Identifier: GPL-2.0
/*
 * plat_common: OS-CPU platform hook layer (hook table + wrappers) of the Xiaomi performance stack.
 * Reconstructed from the HyperOS 4.0 stock binary (no public source).
 */
#include "plat_common.h"

static DECLARE_RWSEM(g_table_rwsem);
struct plat_hook_entry g_plat_hook_table[PLAT_HOOK_MAX];
static int plat_cgroup_ta_id = -1;
static int plat_cgroup_fg_id = -1;
static int plat_cgroup_bg_id = -1;
static int plat_cgroup_au_id = -1;
static unsigned int plat_critical_rt_prio = 96;
static unsigned int plat_sched_relaxed;

int register_plat_func(u32 cmd, void *func GKPAD(fcrcJZ207qkvL4))
{
	unsigned int idx = cmd & 0xff;

	if (idx >= PLAT_HOOK_MAX || !func)
		return -EINVAL;
	down_write(&g_table_rwsem);
	if (g_plat_hook_table[idx].func)
		pr_err("perf_actuator: register %u conflict\n", cmd);
	g_plat_hook_table[idx].cmd = cmd;
	g_plat_hook_table[idx].func = func;
	up_write(&g_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(register_plat_func);

int unregister_plat_func(u32 cmd GKPAD(fcrcgXkH1zKZ4u))
{
	unsigned int idx = cmd & 0xff;

	if (idx >= PLAT_HOOK_MAX)
		return -EINVAL;
	down_write(&g_table_rwsem);
	if (!(g_plat_hook_table[idx].func && g_plat_hook_table[idx].cmd == cmd))
		pr_err("perf_actuator: unregister %u conflict\n", cmd);
	g_plat_hook_table[idx].cmd = 0;
	g_plat_hook_table[idx].func = NULL;
	up_write(&g_table_rwsem);
	return 0;
}
EXPORT_SYMBOL_GPL(unregister_plat_func);

#include "plat_wrappers.h"

void plat_init_sched_cgroup_id(struct cgroup_subsys_state *css GKPAD(fcrc63JozGA6nR))
{
	const char *name;

	if (css->cgroup->level != 1)
		return;

	rcu_read_lock();
	name = css->cgroup->kn->name;
	if (!strcmp(name, "top-app"))
		plat_cgroup_ta_id = css->id;
	else if (!strcmp(name, "foreground"))
		plat_cgroup_fg_id = css->id;
	else if (!strcmp(name, "background"))
		plat_cgroup_bg_id = css->id;
	else if (!strcmp(name, "audio"))
		plat_cgroup_au_id = css->id;
	rcu_read_unlock();

	pr_info("Identified sched cgroup: level=%d, name=%s, id=%d(ta=%d fg=%d bg=%d au=%d)\n",
		css->cgroup->level, css->cgroup->kn->name, css->id,
		plat_cgroup_ta_id, plat_cgroup_fg_id, plat_cgroup_bg_id, plat_cgroup_au_id);
}
EXPORT_SYMBOL_GPL(plat_init_sched_cgroup_id);

bool plat_is_relaxed_rt(struct task_struct *p GKPAD(fcrc16J7PgIzCi))
{
	struct cgroup_subsys_state *css;
	int id;

	if (!plat_sched_relaxed)
		return false;
	if (p->prio > 99 || p->prio > plat_critical_rt_prio)
		return false;
	if (plist_node_empty(&p->pushable_tasks))
		return false;

	rcu_read_lock();
	css = rcu_dereference(p->cgroups) ? task_css(p, cpu_cgrp_id) : NULL;
	id = css ? css->id : -1;
	rcu_read_unlock();

	return id != -1 && id == plat_cgroup_au_id;
}
EXPORT_SYMBOL_GPL(plat_is_relaxed_rt);

static void plat_fix_timer_slack_fork(void *unused, struct task_struct *p)
{
	if (!p->default_timer_slack_ns)
		p->default_timer_slack_ns = 50000;
	if (!p->timer_slack_ns && !(p->policy == SCHED_FIFO || p->policy == SCHED_RR || p->policy == SCHED_DEADLINE))
		p->timer_slack_ns = p->default_timer_slack_ns;
}

static int __init plat_common_init(void)
{
	/* global OS4.0.0.7: 120 = sizeof(plat_task_struct), 1024 = its budget; both are constants in the stock code (guess for the names) */
	pr_info("plat_task_struct size: %zu/%d bytes\n", (size_t)120, 1024);
	register_trace_android_rvh_finish_prio_fork(plat_fix_timer_slack_fork, NULL);
	return 0;
}

static void __exit plat_common_exit(void)
{
}

module_init(plat_common_init);
module_exit(plat_common_exit);
module_param_named(sched_relaxed, plat_sched_relaxed, uint, 0644);
module_param_named(critical_rt_prio, plat_critical_rt_prio, uint, 0644);
MODULE_DESCRIPTION("OS-CPU Platform Hook");
MODULE_LICENSE("GPL v2");
