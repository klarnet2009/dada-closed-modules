// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock millet_oem_cgroup.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cgroup.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/sched.h>

/* table of the hooks this module installs; layout is struct cgroup_subsys (can_attach/cancel_attach at +0x48/+0x50) */
struct cgroup_subsys oem_cgroup_hook;

int millet_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct task_struct *task;
	const struct cred *cred = current_cred();
	const struct cred *tcred __maybe_unused;

	cgroup_taskset_for_each(task, css, tset) {
		tcred = __task_cred(task);
		if (task == current)
			continue;
		if (cred->euid.val == 1000)
			continue;
		if (capable(CAP_SYS_ADMIN))
			continue;
		printk(KERN_ERR "millet-oem_cgroup: Permission problem\n");
		return 1;
	}
	return 0;
}

static int freezer_can_attach(struct cgroup_taskset *tset)
{
	if (oem_cgroup_hook.can_attach)
		return oem_cgroup_hook.can_attach(tset);
	return 0;
}

static void freezer_cancel_attach(struct cgroup_taskset *tset)
{
	if (oem_cgroup_hook.cancel_attach)
		oem_cgroup_hook.cancel_attach(tset);
}

static int __init oem_cgrp_init(void)
{
	printk(KERN_ERR "millet-oem_cgroup: enter oem_cgrp_init func!\n");
	oem_cgroup_hook.can_attach = millet_can_attach;
	freezer_cgrp_subsys.can_attach = freezer_can_attach;
	freezer_cgrp_subsys.cancel_attach = freezer_cancel_attach;
	return 0;
}
module_init(oem_cgrp_init);

MODULE_LICENSE("GPL");
