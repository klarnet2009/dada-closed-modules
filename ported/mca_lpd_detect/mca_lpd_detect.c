// SPDX-License-Identifier: GPL-2.0
/*
 * mca_lpd_detect.c
 *
 * usb liquited detection driver
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
#include <linux/iio/consumer.h>
#include <linux/platform_device.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>

#include "inc/mca_lpd_detect.h"
#include <mca/platform/platform_buckchg_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_lpd_detect"
#endif

static struct mca_lpd_dev *g_lpd;

int lpd_is_charging_limit(void)
{
	return g_lpd->lpd_charging;
}
EXPORT_SYMBOL(lpd_is_charging_limit);

static ssize_t lpd_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t lpd_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info lpd_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_EN, enable),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_STATUS, lpd_status),
	mca_sysfs_attr_rw(lpd_sysfs, 0664, LPD_PROP_SBU1, sbu1),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_SBU2, sbu2),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_CC1, cc1),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_CC2, cc2),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_DP, dp),
	mca_sysfs_attr_ro(lpd_sysfs, 0440, LPD_PROP_DP, dm),
	mca_sysfs_attr_rw(lpd_sysfs, 0664, LPD_PROP_CHARGING, lpd_charging),
	mca_sysfs_attr_rw(lpd_sysfs, 0664, LPD_PROP_CONTROL, lpd_control),
	mca_sysfs_attr_rw(lpd_sysfs, 0664, LPD_PROP_UART_CONTROL, uart_control),
};

#define LPD_SYSFS_ATTRS_SIZE   ARRAY_SIZE(lpd_sysfs_field_tbl)

static struct attribute *lpd_sysfs_attrs[LPD_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group lpd_sysfs_attr_group = {
	.attrs = lpd_sysfs_attrs,
};

static ssize_t lpd_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct mca_lpd_dev *lpd = dev_get_drvdata(dev);
	int temp = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		lpd_sysfs_field_tbl, LPD_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case LPD_PROP_EN:
		(void)platform_class_buckchg_ops_get_lpd_enable(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_STATUS:
		(void)platform_class_buckchg_ops_get_lpd_status(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_SBU1:
		(void)platform_class_buckchg_ops_get_lpd_sbu1(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_SBU2:
		(void)platform_class_buckchg_ops_get_lpd_sbu2(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_CC1:
		(void)platform_class_buckchg_ops_get_lpd_cc1(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_CC2:
		(void)platform_class_buckchg_ops_get_lpd_cc2(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_DP:
		(void)platform_class_buckchg_ops_get_lpd_dp(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_DM:
		(void)platform_class_buckchg_ops_get_lpd_dm(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_CHARGING:
		temp = lpd->lpd_charging;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_CONTROL:
		(void)platform_class_buckchg_ops_get_lpd_control(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case LPD_PROP_UART_CONTROL:
		(void)platform_class_buckchg_ops_get_lpd_uart_control(MAIN_BUCK_CHARGER, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t lpd_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct mca_lpd_dev *lpd = dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		lpd_sysfs_field_tbl, LPD_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!lpd) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case LPD_PROP_SBU1:
		(void)platform_class_buckchg_ops_set_lpd_sbu1(MAIN_BUCK_CHARGER, val);
		break;
	case LPD_PROP_CHARGING:
		lpd->lpd_charging = val;
		break;
	case LPD_PROP_CONTROL:
		(void)platform_class_buckchg_ops_set_lpd_control(MAIN_BUCK_CHARGER, val);
		break;
	case LPD_PROP_UART_CONTROL:
		(void)platform_class_buckchg_ops_set_lpd_uart_control(MAIN_BUCK_CHARGER, val);
		break;
	default:
		break;
	}

	mca_log_info("set the %d ntc = %d\n", attr_info->sysfs_attr_name, val);

	return count;
}

static int lpd_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(lpd_sysfs_attrs, lpd_sysfs_field_tbl,
		LPD_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "lpd",
	 dev, &lpd_sysfs_attr_group);
}

static void lpd_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_5, "lpd",
		dev, &lpd_sysfs_attr_group);
}

static int mca_lpd_detect_probe(struct platform_device *pdev)
{
	struct mca_lpd_dev *lpd;

	lpd = devm_kzalloc(&pdev->dev, sizeof(*lpd), GFP_KERNEL);
	if (!lpd)
		return -ENOMEM;

	lpd->dev = &pdev->dev;
	platform_set_drvdata(pdev, lpd);

	lpd->lpd_charging = 0;
	g_lpd = lpd;
	lpd_sysfs_create_group(lpd->dev);
	mca_log_err("%s success\n", __func__);
	return 0;
}

static int mca_lpd_detect_remove(struct platform_device *pdev)
{
	lpd_sysfs_remove_group(&pdev->dev);
	return 0;
}

static void mca_lpd_detect_shutdown(struct platform_device *pdev)
{

}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,lpd_detect"},
	{},
};

static struct platform_driver mca_lpd_detect_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_lpd_detect",
		.of_match_table = match_table,
	},
	.probe = mca_lpd_detect_probe,
	.remove = mca_lpd_detect_remove,
	.shutdown = mca_lpd_detect_shutdown,
};

static int __init mca_lpd_detect_init(void)
{
	return platform_driver_register(&mca_lpd_detect_driver);
}
module_init(mca_lpd_detect_init);

static void __exit mca_lpd_detect_exit(void)
{
	platform_driver_unregister(&mca_lpd_detect_driver);
}
module_exit(mca_lpd_detect_exit);

MODULE_DESCRIPTION("usb moisture detection");
MODULE_AUTHOR("zhouhongfei@xiaomi.com");
MODULE_LICENSE("GPL v2");
