// SPDX-License-Identifier: GPL-2.0
/*
 * mca_vbat_ovp_monitor.c
 *
 * battery ocp software detection and protection driver
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
#include "inc/mca_ibat_ocp_monitor.h"
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_ibat_ocp_mon"
#endif


#define FAST_HEARTBEAT_TIMER_MS 2000
#define NORMAL_HEARTBEAT_TIMER_MS 5000


static int mca_ibat_mon_get_ibat_ocp_flag(struct mca_ibat_ocp_mon_dev *info)
{
	int master_ocp = 0;
	int slave_ocp = 0;
	int flag;
	int master_curr, slave_curr;

	(void)platform_fg_ops_get_curr(FG_IC_MASTER, &master_curr);
	(void)platform_fg_ops_get_curr(FG_IC_SLAVE, &slave_curr);

	if (info->fake_master_ibat_override_ma > 0)
		master_curr = info->fake_master_ibat_override_ma;
	if (info->fake_slave_ibat_override_ma > 0)
		slave_curr = info->fake_slave_ibat_override_ma;

	if(abs(master_curr) > info->ocp_threshold[FG_IC_MASTER]) {
		master_ocp = 1;
	}
	if(abs(slave_curr) > info->ocp_threshold[FG_IC_SLAVE]) {
		slave_ocp = 1;
	}

	flag = (master_ocp << 1) | slave_ocp;
	flag &= 0x03;

	return flag;
}

static int mca_ibat_mon_get_ibat_ocp_status(struct mca_ibat_ocp_mon_dev *info)
{
	int ret, ffc_sts, ocp_flag;
	int usb_online = 0, wireless_online = 0;
	static int ibat_ocp_triggered = 0;

	if (!info)
		return 0;

	/* to do, now only support parallel battery */
	if (info->fg_type != MCA_FG_TYPE_PARALLEL)
		return 0;

	ret = platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &usb_online);
	ret |= platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wireless_online);

	if(ret) {
		return 0;
	} else {
		/* no need to monitor when power absent */
		if (!usb_online && !wireless_online) {
			return 0;
		} else {
			/* a17A: OS4 stock also checks the load switch (absent -> charge dfx event) */
			{
				bool loadsw_present = true;

				(void)platform_class_loadsw_get_present(LOADSW_ROLE_MASTER, &loadsw_present);
				if (!loadsw_present)
					mca_charge_mievent_report(CHARGE_DFX_LOAD_SWITCH_IIC_ERR, NULL, 0);
			}

			ffc_sts = strategy_class_fg_get_fastcharge();
			if (!ffc_sts)
				return 0;

			ocp_flag = mca_ibat_mon_get_ibat_ocp_flag(info);
			mca_log_info("ocp_flag = %d, ibat_ocp_triggered: %d\n", ocp_flag, ibat_ocp_triggered);
			if (!ibat_ocp_triggered && ocp_flag) {
				ibat_ocp_triggered = 1;
				mca_log_err("ibat ocp triggered: ibat_ocp_triggered = %d\n", ibat_ocp_triggered);
				return ocp_flag;
			} else if (ibat_ocp_triggered && !ocp_flag) {
				ibat_ocp_triggered = 0;
				mca_log_info("ibat ocp cleared: ibat_ocp_triggered = %d\n", ibat_ocp_triggered);
				return 0;
			} else if (ibat_ocp_triggered) {
				return ocp_flag;
			}
		}
	}

	return 0;
}

static void mca_ibat_ocp_monitor_workfunc(struct work_struct *work)
{
	struct mca_ibat_ocp_mon_dev *info = container_of(work, struct mca_ibat_ocp_mon_dev, monitor_ibat_ocp_work.work);
	int ibat_ocp_confirmed = 0;
	int interval = NORMAL_HEARTBEAT_TIMER_MS;
	int fake_ibat_ocp = 0;

	mca_log_info("fake_ibat_for_debug = %d\n", info->fake_ibat_for_debug);
	if (info->fake_ibat_for_debug > 13000000)
		fake_ibat_ocp = 1;
	else
		fake_ibat_ocp = 0;

	if (info->fake_ibat_ocp_status != fake_ibat_ocp) {
		info->fake_ibat_ocp_status = fake_ibat_ocp;
		mca_log_err("fake_ibat_ocp = %d\n", fake_ibat_ocp);
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_IBAT_OCP_CHANGE, &info->fake_ibat_ocp_status);
		goto next;
	}

	ibat_ocp_confirmed = mca_ibat_mon_get_ibat_ocp_status(info);

	if (info->batt_ocp_status != ibat_ocp_confirmed) {
		info->batt_ocp_status = ibat_ocp_confirmed;
		mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_IBAT_OCP_CHANGE, &info->batt_ocp_status);
	}

	if (ibat_ocp_confirmed)
		interval = FAST_HEARTBEAT_TIMER_MS;
	else
		interval = NORMAL_HEARTBEAT_TIMER_MS;
next:
	schedule_delayed_work(&info->monitor_ibat_ocp_work, msecs_to_jiffies(interval));
}

static int mca_ibat_ocp_mon_parse_dt(struct mca_ibat_ocp_mon_dev *info)
{
	struct device_node *np = info->dev->of_node;
	int ret = 0;

	if (!np) {
		mca_log_err("device tree info missing\n");
		return -1;
	}

	ret = mca_parse_dts_u32(np, "fg_type", &info->fg_type, MCA_FG_TYPE_SINGLE);
	if (ret)
		mca_log_err("parse fg_type failed\n");

	ret |= mca_parse_dts_u32_array(np, "ocp_threshold", info->ocp_threshold, FG_IC_MAX);
	if (ret) {
		info->ocp_threshold[FG_IC_MASTER] = OCP_THRESHOLD_MAINT;
		info->ocp_threshold[FG_IC_SLAVE] = OCP_THRESHOLD_FLIP;
		mca_log_err("parse ocp_threshold failed, use default value\n");
	}
	mca_log_info("ocp_threshold : %d %d\n", info->ocp_threshold[FG_IC_MASTER], info->ocp_threshold[FG_IC_SLAVE]);

	return ret;
}

static ssize_t ibat_ocp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t ibat_ocp_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);


struct mca_sysfs_attr_info ibat_ocp_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(ibat_ocp_sysfs, 0664, FAKE_IBAT_FOR_DEBUG, fake_ibat_for_debug),
	mca_sysfs_attr_rw(ibat_ocp_sysfs, 0664, FAKE_MASTER_IBAT_OVERRIDE, fake_master_ibat_override),
	mca_sysfs_attr_rw(ibat_ocp_sysfs, 0664, FAKE_SLAVE_IBAT_OVERRIDE, fake_slave_ibat_override),
};

#define IBAT_OVP_SYSFS_ATTRS_SIZE   ARRAY_SIZE(ibat_ocp_sysfs_field_tbl)

static struct attribute *ibat_ocp_sysfs_attrs[IBAT_OVP_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group ibat_ocp_sysfs_attr_group = {
	.attrs = ibat_ocp_sysfs_attrs,
};

static ssize_t ibat_ocp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct mca_ibat_ocp_mon_dev *info = dev_get_drvdata(dev);
	int ibat, ibat_override;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		ibat_ocp_sysfs_field_tbl, IBAT_OVP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	switch (attr_info->sysfs_attr_name) {
	case FAKE_IBAT_FOR_DEBUG:
		ibat = info->fake_ibat_for_debug;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", ibat);
		break;
	case FAKE_MASTER_IBAT_OVERRIDE:
		ibat_override = info->fake_master_ibat_override_ma;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", ibat_override);
		break;
	case FAKE_SLAVE_IBAT_OVERRIDE:
		ibat_override = info->fake_slave_ibat_override_ma;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", ibat_override);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t ibat_ocp_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct mca_ibat_ocp_mon_dev *info = dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		ibat_ocp_sysfs_field_tbl, IBAT_OVP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case FAKE_IBAT_FOR_DEBUG:
		info->fake_ibat_for_debug = val;
		mca_log_err("set the %d fake vbat for debug = %d\n", attr_info->sysfs_attr_name, val);

		cancel_delayed_work(&info->monitor_ibat_ocp_work);
		schedule_delayed_work(&info->monitor_ibat_ocp_work, 0);
		break;
	case FAKE_MASTER_IBAT_OVERRIDE:
		info->fake_master_ibat_override_ma = val;
		mca_log_err("set the %d fake vbat override = %d\n", attr_info->sysfs_attr_name, val);

		cancel_delayed_work(&info->monitor_ibat_ocp_work);
		schedule_delayed_work(&info->monitor_ibat_ocp_work, 0);
		break;
	case FAKE_SLAVE_IBAT_OVERRIDE:
		info->fake_slave_ibat_override_ma = val;
		mca_log_err("set the %d fake vbat override = %d\n", attr_info->sysfs_attr_name, val);

		cancel_delayed_work(&info->monitor_ibat_ocp_work);
		schedule_delayed_work(&info->monitor_ibat_ocp_work, 0);
		break;
	default:
		break;
	}

	return count;
}

static int ibat_ocp_mon_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(ibat_ocp_sysfs_attrs, ibat_ocp_sysfs_field_tbl,
		IBAT_OVP_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "ibat_ocp",
	 dev, &ibat_ocp_sysfs_attr_group);
}

static int mca_ibat_ocp_mon_probe(struct platform_device *pdev)
{
	struct mca_ibat_ocp_mon_dev *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	ret = mca_ibat_ocp_mon_parse_dt(info);
	if (ret) {
		mca_log_err("parse dt faile\n");
		return -1;
	}

	info->batt_ocp_status = 0;
	info->fake_ibat_ocp_status = 0;
	info->fake_ibat_for_debug = -22;
	info->fake_master_ibat_override_ma = -22;
	info->fake_slave_ibat_override_ma = -22;
	INIT_DELAYED_WORK(&info->monitor_ibat_ocp_work, mca_ibat_ocp_monitor_workfunc);
	schedule_delayed_work(&info->monitor_ibat_ocp_work, msecs_to_jiffies(NORMAL_HEARTBEAT_TIMER_MS));
	ibat_ocp_mon_sysfs_create_group(info->dev);
	mca_log_err("%s success\n", __func__);
	return 0;
}

static int mca_ibat_ocp_mon_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_ibat_ocp_mon_shutdown(struct platform_device *pdev)
{

}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,ibat_ocp_monitor"},
	{},
};

static struct platform_driver mca_ibat_ocp_mon_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_ibat_ocp_monitor",
		.of_match_table = match_table,
	},
	.probe = mca_ibat_ocp_mon_probe,
	.remove = mca_ibat_ocp_mon_remove,
	.shutdown = mca_ibat_ocp_mon_shutdown,
};

static int __init mca_ibat_ocp_mon_init(void)
{
	return platform_driver_register(&mca_ibat_ocp_mon_driver);
}
module_init(mca_ibat_ocp_mon_init);

static void __exit mca_ibat_ocp_mon_exit(void)
{
	platform_driver_unregister(&mca_ibat_ocp_mon_driver);
}
module_exit(mca_ibat_ocp_mon_exit);

MODULE_DESCRIPTION("battery ocp software protection");
MODULE_AUTHOR("yinshunan@xiaomi.com");
MODULE_LICENSE("GPL v2");