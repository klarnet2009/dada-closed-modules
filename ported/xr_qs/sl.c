// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * speedlock (sl) part of the Xiaomi QOS sched driver (OS4). Reconstructed from the stock binary.
 */

#define pr_fmt(fmt) "sl: " fmt

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/rwsem.h>
#include <trace/hooks/dtask.h>

#include "xr_qs.h"
#include "trace_xr_qs.h"

#define SYS_MODE	0640

static u32 g_sl_enable;
static u32 g_sl_prio = 5;

static void android_vh_rwsem_lock_released(void *unused, struct rw_semaphore *sem,
					   unsigned long settime_jiffies)
{
	struct qs_struct *qs;

	if (likely(!settime_jiffies)) {
		qs = task_to_qs(current);
		if (qs && qs->speedlock_flag) {
			restore_vip_prio(current);
			trace_sl_reset(current, (unsigned long)sem);
			qs->speedlock_flag = 0;
		}
	}
}

static void android_vh_rwsem_read_wait_start(void *unused, struct rw_semaphore *sem)
{
	struct qs_struct *qs;

	if (!g_sl_enable)
		return;
	if (is_qos_task(current))
		return;
	if (get_proc_static_qos_lvl(current) < 7)
		return;

	qs = task_to_qs(current);
	if (!qs)
		return;
	set_vip_prio(current, g_sl_prio, false);
	trace_sl_set(current, g_sl_prio, (unsigned long)sem);
	qs->speedlock_flag = 1;
}

static ssize_t sl_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	u32 enable = 0;

	if (kstrtouint(buf, 0, &enable)) {
		pr_err("speedlock store: parse enable failed\n");
		return -EINVAL;
	}
	g_sl_enable = !!enable;
	return count;
}

static ssize_t sl_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "enable:\t%d\n", g_sl_enable);
}

static ssize_t sl_prio_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	u32 prio = 0;

	if (kstrtouint(buf, 0, &prio)) {
		pr_err("sl prio store: parse value failed\n");
		return -EINVAL;
	}
	if (prio == 0 || prio >= MAX_RT_PRIO) {
		pr_err("sl prio store: invalid value[%u]\n", prio);
		return -EINVAL;
	}
	g_sl_prio = prio;
	return count;
}

static ssize_t sl_prio_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "sl prio:\t%d\n", g_sl_prio);
}

static struct kobj_attribute sl_enable_attr = __ATTR(sl_enable, SYS_MODE, sl_enable_show, sl_enable_store);
static struct kobj_attribute sl_prio_attr = __ATTR(sl_prio, SYS_MODE, sl_prio_show, sl_prio_store);

static struct attribute *sl_attrs[] = {
	&sl_enable_attr.attr,
	&sl_prio_attr.attr,
	NULL,
};

static struct attribute_group sl_attr_group = {
	.attrs = sl_attrs,
};

int sl_init(void)
{
	int ret;
	struct kobject *sl_kobj;

	ret = register_trace_android_vh_record_rwsem_lock_starttime(android_vh_rwsem_lock_released, NULL);
	if (ret < 0)
		return ret;
	ret = register_trace_android_vh_rwsem_read_wait_start(android_vh_rwsem_read_wait_start, NULL);
	if (ret < 0)
		goto unreg_starttime;

	sl_kobj = kobject_create_and_add("speedlock", kernel_kobj);
	if (!sl_kobj) {
		ret = -ENOMEM;
		goto unreg_wait;
	}
	ret = sysfs_create_group(sl_kobj, &sl_attr_group);
	if (ret) {
		kobject_put(sl_kobj);
		pr_err("speedlock init: failed to create sys node\n");
		goto unreg_wait;
	}
	return ret;

unreg_wait:
	unregister_trace_android_vh_rwsem_read_wait_start(android_vh_rwsem_read_wait_start, NULL);
unreg_starttime:
	unregister_trace_android_vh_record_rwsem_lock_starttime(android_vh_rwsem_lock_released, NULL);
	return ret;
}
