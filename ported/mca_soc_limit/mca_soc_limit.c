
// SPDX-License-Identifier: GPL-2.0
/*
 * mca_soc_limit.c
 *
 * soc limit driver
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
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/strategy/strategy_fg_class.h>
void soc_limit_process(bool enable, int soc_limit_thre);
#include "mca_soc_limit.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_soc_limit"
#endif

static struct soc_limit_info *global_soc_limit_info;

void soc_limit_process(bool enable, int soc_limit_thre)
{
	struct soc_limit_info *info = global_soc_limit_info;

	if (!info)
		return;

	if (enable) {
		info->curr_soc = strategy_class_fg_ops_get_soc();
		if (info->curr_soc > soc_limit_thre) {
			info->soc_limit_enable = true;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_SOC_LIMIT, &info->soc_limit_enable);
			mca_log_info("soc_limit_thre = %d", soc_limit_thre);
		}
	} else {
		info->soc_limit_enable = false;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_SOC_LIMIT, &info->soc_limit_enable);
		mca_log_info("disable soc limit");
	}
}
EXPORT_SYMBOL(soc_limit_process);

static int soc_limit_probe(struct platform_device *pdev)
{
	struct soc_limit_info *info;
	static int probe_cnt;

	mca_log_info("probe_cnt = %d", ++probe_cnt);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	global_soc_limit_info = info;

	mca_log_info("done\n");
	return 0;
}

static int soc_limit_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id match_table[] = {
	{.compatible = "xiaomi,soc_limit"},
	{},
};

static struct platform_driver soc_limit_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "soc_limit",
		.of_match_table = match_table,
	},
	.probe = soc_limit_probe,
	.remove = soc_limit_remove,
};

static int __init soc_limit_init(void)
{
	return platform_driver_register(&soc_limit_driver);
}
module_init(soc_limit_init);

static void __exit soc_limit_exit(void)
{
	platform_driver_unregister(&soc_limit_driver);
}
module_exit(soc_limit_exit);

MODULE_DESCRIPTION("soc limit driver");
MODULE_AUTHOR("liuzhengqing@xiaomi.com");
MODULE_LICENSE("GPL v2");
