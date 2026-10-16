// SPDX-License-Identifier: GPL-2.0
/*
 * mca_business_misc.c
 *
 * mca misc business driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <mca/common/mca_log.h>
#include "inc/mca_business_misc.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_misc"
#endif

static int business_misc_probe(struct platform_device *pdev)
{
	struct business_misc *misc;
	int ret = 0;

	mca_log_info("probe start\n");

	misc = devm_kzalloc(&pdev->dev, sizeof(*misc), GFP_KERNEL);
	if (!misc)
		return -ENOMEM;

	misc->dev = &pdev->dev;
	platform_set_drvdata(pdev, misc);

	ret = business_votable_init(misc->dev);
	if (ret < 0) {
		mca_log_err("business_votable_init fail\n");
		return -EINVAL;
	}

	mca_log_err("probe end\n");

	return 0;
}


static int business_misc_remove(struct platform_device *pdev)
{
	return 0;
}

static void business_misc_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,business-misc"},
	{},
};

static struct platform_driver business_misc_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "business_misc",
		.of_match_table = match_table,
	},
	.probe = business_misc_probe,
	.remove = business_misc_remove,
	.shutdown = business_misc_shutdown,
};

static int __init business_misc_init(void)
{
	return platform_driver_register(&business_misc_driver);
}
module_init(business_misc_init);

static void __exit business_misc_exit(void)
{
	platform_driver_unregister(&business_misc_driver);
}
module_exit(business_misc_exit);

MODULE_DESCRIPTION("mca business misc");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

