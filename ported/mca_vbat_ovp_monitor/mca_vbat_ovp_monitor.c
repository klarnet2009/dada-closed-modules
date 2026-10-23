// SPDX-License-Identifier: GPL-2.0
/*
 * mca_vbat_ovp_monitor.c
 *
 * battery ovp software detection and protection driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <mca/common/mca_parse_dts.h>
#include "mca_vbat_ovp_monitor.h"
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_vbat_ovp_mon"
#endif


#define FAST_HEARTBEAT_TIMER_MS 10000
#define NORMAL_HEARTBEAT_TIMER_MS 60000

static int mca_vbat_mon_get_vbat_ovp_status(struct mca_vbat_ovp_mon_dev *info)
{
	int ret;
	int vbat_now_mv, temp;
	int usb_online = 0, wireless_online = 0, vbat_ovp_threshold_mv;
	int ffc_sts;
	static int vbat_ovp_triggered = 0;

	if (!info)
		return 0;

	/* parallel fg to do, now only support single 1S/2S(also have single fg) battery */
	if (info->fg_type > MCA_FG_TYPE_SINGLE)
		return 0;

	(void)strategy_class_fg_ops_get_temperature(&temp);
	/* no need to monitor vbat ovp when batt temp >= 48 degree*/
	if (temp >= 480)
		return 0;
	(void)strategy_class_fg_ops_get_voltage(&vbat_now_mv);
	ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb_online);
	ret |= platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wireless_online);

	if(ret) {
		return 0;
	} else {
		/* no need to monitor when power absent */
		if (!usb_online && !wireless_online) {
			return 0;
		} else {
			ffc_sts = strategy_class_fg_get_fastcharge();
			if (ffc_sts)
				vbat_ovp_threshold_mv = info->vbat_ovp_thr_ffr_mv;
			else
				vbat_ovp_threshold_mv = info->vbat_ovp_thr_nor_mv;
			if (info->fake_vbat_override_mv > 0)
				vbat_now_mv = info->fake_vbat_override_mv;
			mca_log_info("vbat_now_mv = %d, vbat_ovp_triggered: %d\n", vbat_now_mv, vbat_ovp_triggered);
			if (!vbat_ovp_triggered && vbat_now_mv > vbat_ovp_threshold_mv + info->vbat_ovp_thr_hys_mv) {
				vbat_ovp_triggered = 1;
				mca_log_err("vbat ovp triggered: vbat_ovp_triggered = %d\n", vbat_ovp_triggered);
				return 1;
			} else if (vbat_ovp_triggered && vbat_now_mv <= vbat_ovp_threshold_mv - info->vbat_ovp_recharge_delta_mv) {
				vbat_ovp_triggered = 0;
				mca_log_info("vbat ovp cleared: vbat_ovp_triggered = %d\n", vbat_ovp_triggered);
				return 0;
			} else if (vbat_ovp_triggered) {
				return 1;
			}
		}
	}

	return 0;
}

static void mca_vbat_ovp_monitor_workfunc(struct work_struct *work)
{
	struct mca_vbat_ovp_mon_dev *info = container_of(work, struct mca_vbat_ovp_mon_dev, monitor_vbat_ovp_work.work);
	int vbat_ovp_confirmed = 0;
	int interval = NORMAL_HEARTBEAT_TIMER_MS;
	int fake_vbat_ovp = 0;

	mca_log_info("fake_vbat_for_debug = %d\n", info->fake_vbat_for_debug);
	if (info->fake_vbat_for_debug > 4600)
		fake_vbat_ovp = 1;
	else
		fake_vbat_ovp = 0;

	if (info->fake_vbat_ovp_status != fake_vbat_ovp) {
		info->fake_vbat_ovp_status = fake_vbat_ovp;
		mca_log_err("fake_vbat_ovp = %d\n", fake_vbat_ovp);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_VBAT_OVP_CHANGE, &info->fake_vbat_ovp_status);
		goto next;
	}

	vbat_ovp_confirmed = mca_vbat_mon_get_vbat_ovp_status(info);

	if (info->batt_ovp_status != vbat_ovp_confirmed) {
		info->batt_ovp_status = vbat_ovp_confirmed;
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_VBAT_OVP_CHANGE, &info->batt_ovp_status);
	}

	if (vbat_ovp_confirmed)
		interval = FAST_HEARTBEAT_TIMER_MS;
	else
		interval = NORMAL_HEARTBEAT_TIMER_MS;
next:
	schedule_delayed_work(&info->monitor_vbat_ovp_work, msecs_to_jiffies(interval));
}

static int mca_vbat_ovp_mon_parse_dt(struct mca_vbat_ovp_mon_dev *info)
{
	struct device_node *np = info->dev->of_node;
	int ret = 0;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!np) {
		mca_log_err("device tree info missing\n");
		return -1;
	}

	info->support_global_fv = of_property_read_bool(np, "support_global_fv");
	ret = mca_parse_dts_u32(np, "vbat_ovp_threshold_ffc",
		&info->vbat_ovp_threshold_ffc_mv, VBAT_OVP_THR_FFC_DEFAULT_MV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_threshold_normal",
		&info->vbat_ovp_threshold_normal_mv, VBAT_OVP_THR_NORMAL_DEFAULT_UV);
	(void)mca_parse_dts_u32(np, "vbat_ovp_threshold_ffc_gl",
		&info->vbat_ovp_threshold_ffc_gl_mv, VBAT_OVP_THR_FFC_GL_DEFAULT_MV);
	(void)mca_parse_dts_u32(np, "vbat_ovp_threshold_normal_gl",
		&info->vbat_ovp_threshold_normal_gl_mv, VBAT_OVP_THR_NORMAL_GL_DEFAULT_UV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_threshold_hys",
		&info->vbat_ovp_thr_hys_mv, VBAT_OVP_THR_DEFAULT_HYS_MV);
	ret |= mca_parse_dts_u32(np, "vbat_ovp_recharge_delta",
		&info->vbat_ovp_recharge_delta_mv, VBAT_OVP_RECHARGE_DELTA_MV);
	ret |= mca_parse_dts_u32(np, "fg_type", &info->fg_type, MCA_FG_TYPE_SINGLE);

	if (hwid && info->support_global_fv && hwid->country_version != CountryCN) {
		info->vbat_ovp_thr_ffr_mv = info->vbat_ovp_threshold_ffc_gl_mv;
		info->vbat_ovp_thr_nor_mv = info->vbat_ovp_threshold_normal_gl_mv;
	} else {
		info->vbat_ovp_thr_ffr_mv = info->vbat_ovp_threshold_ffc_mv;
		info->vbat_ovp_thr_nor_mv = info->vbat_ovp_threshold_normal_mv;
	}

	return ret;
}

static ssize_t vbat_ovp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t vbat_ovp_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);


struct mca_sysfs_attr_info vbat_ovp_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(vbat_ovp_sysfs, 0664, FAKE_VBAT_FOR_DEBUG, fake_vbat_for_debug),
	mca_sysfs_attr_rw(vbat_ovp_sysfs, 0664, FAKE_VBAT_OVERRIDE, fake_vbat_override),
};

#define VBAT_OVP_SYSFS_ATTRS_SIZE   ARRAY_SIZE(vbat_ovp_sysfs_field_tbl)

static struct attribute *vbat_ovp_sysfs_attrs[VBAT_OVP_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group vbat_ovp_sysfs_attr_group = {
	.attrs = vbat_ovp_sysfs_attrs,
};

static ssize_t vbat_ovp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct mca_vbat_ovp_mon_dev *info = dev_get_drvdata(dev);
	int vbat, vbat_override;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		vbat_ovp_sysfs_field_tbl, VBAT_OVP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	switch (attr_info->sysfs_attr_name) {
	case FAKE_VBAT_FOR_DEBUG:
		vbat = info->fake_vbat_for_debug;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", vbat);
		break;
	case FAKE_VBAT_OVERRIDE:
		vbat_override = info->fake_vbat_override_mv;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", vbat_override);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t vbat_ovp_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct mca_vbat_ovp_mon_dev *info = dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		vbat_ovp_sysfs_field_tbl, VBAT_OVP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case FAKE_VBAT_FOR_DEBUG:
		info->fake_vbat_for_debug = val;
		mca_log_err("set the %d fake vbat for debug = %d\n", attr_info->sysfs_attr_name, val);

		cancel_delayed_work(&info->monitor_vbat_ovp_work);
		schedule_delayed_work(&info->monitor_vbat_ovp_work, 0);
		break;
	case FAKE_VBAT_OVERRIDE:
		info->fake_vbat_override_mv = val;
		mca_log_err("set the %d fake vbat override = %d\n", attr_info->sysfs_attr_name, val);

		cancel_delayed_work(&info->monitor_vbat_ovp_work);
		schedule_delayed_work(&info->monitor_vbat_ovp_work, 0);
		break;
	default:
		break;
	}

	return count;
}

static int vbat_ovp_mon_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(vbat_ovp_sysfs_attrs, vbat_ovp_sysfs_field_tbl,
		VBAT_OVP_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "vbat_ovp",
	 dev, &vbat_ovp_sysfs_attr_group);
}

static int mca_vbat_ovp_mon_probe(struct platform_device *pdev)
{
	struct mca_vbat_ovp_mon_dev *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	ret = mca_vbat_ovp_mon_parse_dt(info);
	if (ret) {
		mca_log_err("parse dt faile\n");
		return -1;
	}

	info->batt_ovp_status = 0;
	info->fake_vbat_ovp_status = 0;
	info->fake_vbat_for_debug = -22;
	info->fake_vbat_override_mv = -22;
	INIT_DELAYED_WORK(&info->monitor_vbat_ovp_work, mca_vbat_ovp_monitor_workfunc);
	schedule_delayed_work(&info->monitor_vbat_ovp_work, msecs_to_jiffies(NORMAL_HEARTBEAT_TIMER_MS));
	vbat_ovp_mon_sysfs_create_group(info->dev);
	mca_log_err("%s success\n", __func__);
	return 0;
}

static int mca_vbat_ovp_mon_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_vbat_ovp_mon_shutdown(struct platform_device *pdev)
{

}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,vbat_ovp_monitor"},
	{},
};

static struct platform_driver mca_vbat_ovp_mon_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_vbat_ovp_monitor",
		.of_match_table = match_table,
	},
	.probe = mca_vbat_ovp_mon_probe,
	.remove = mca_vbat_ovp_mon_remove,
	.shutdown = mca_vbat_ovp_mon_shutdown,
};

static int __init mca_vbat_ovp_mon_init(void)
{
	return platform_driver_register(&mca_vbat_ovp_mon_driver);
}
module_init(mca_vbat_ovp_mon_init);

static void __exit mca_vbat_ovp_mon_exit(void)
{
	platform_driver_unregister(&mca_vbat_ovp_mon_driver);
}
module_exit(mca_vbat_ovp_mon_exit);

MODULE_DESCRIPTION("battery ovp software protection");
MODULE_AUTHOR("jiangfei1@xiaomi.com");
MODULE_LICENSE("GPL v2");