// SPDX-License-Identifier: GPL-2.0
/*
 * mca_business_battery.c
 *
 * mca battery business driver
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
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <mca/strategy/strategy_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include "inc/mca_business_battery.h"
#include "inc/mca_battery_psy.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_business_battery"
#endif

static struct business_battery *g_mca_business_battery;

/*
static void battery_event_process_work(struct work_struct *work)
{
	struct business_battery *battery = container_of(work,
			struct business_battery, event_process_work);

	business_battery_psy_event_process(battery->batt_psy_info);
}
*/
static int business_battery_event_process(struct notifier_block *nb,
	unsigned long event, void *data)
{
	struct business_battery *battery = container_of(nb, struct business_battery, batt_info_nb);

	switch (event) {
	case MCA_EVENT_BATTERY_STS_CHANGE:
		if (!battery || !battery->batt_psy_info ||!battery->batt_psy_info->batt_psy)
			return 0;
		power_supply_changed(battery->batt_psy_info->batt_psy);
		break;
	case MCA_EVENT_BATTERY_FAKE_POWER:
		battery->batt_psy_info->fake_power = *(int *)data;
		mca_log_err("reviced fake_power event %d ", battery->batt_psy_info->fake_power);
		break;
	default:
		break;
	}

	return 0;
}

static int business_battery_parse_dt(struct business_battery *battery)
{
	struct device_node *node = battery->dev->of_node;

	(void)mca_parse_dts_u32(node, "battery-core-test", &battery->dt.test, 0);
	// mca_parse_dts_u32(node, "battery-resistance-id", &battery->dt.resistance_id, 0);

	return 0;
}

static int business_battery_probe(struct platform_device *pdev)
{
	struct business_battery *battery;
	static int probe_cnt;
	int rc = 0;

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);

	battery = devm_kzalloc(&pdev->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->dev = &pdev->dev;
	platform_set_drvdata(pdev, battery);

	rc = business_battery_parse_dt(battery);
	if (rc < 0) {
		mca_log_err("Couldn't parse device tree rc=%d\n", rc);
		return rc;
	}

	battery->batt_psy_info = business_battery_psy_init(battery->dev);
	if (!battery->batt_psy_info) {
		mca_log_err("Couldn't init battery psy\n");
		return -1;
	}

	//INIT_WORK(&battery->event_process_work, battery_event_process_work);
	battery->batt_info_nb.notifier_call = business_battery_event_process;
	rc = mca_event_block_notify_register(MCA_EVENT_TYPE_BATTERY_INFO,
		&battery->batt_info_nb);
	if (rc) {
		rc = -EPROBE_DEFER;
		mca_log_err("register notify failed\n");
		goto error;
	}

	g_mca_business_battery = battery;
	// business_battery_sysfs_create_files();

	mca_log_err("probe ok");

	return 0;

error:
	business_battery_psy_deinit(battery->batt_psy_info);
	return rc;
}

static int business_battery_remove(struct platform_device *pdev)
{
	//struct business_battery *battery = platform_get_drvdata(pdev);

	//cancel_work_sync(&battery->event_process_work);

	return 0;
}

static void business_battery_shutdown(struct platform_device *pdev)
{
	//struct business_battery *battery = platform_get_drvdata(pdev);

	//cancel_work_sync(&battery->event_process_work);
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,business_battery"},
	{},
};

static struct platform_driver business_battery_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "business_battery",
		.of_match_table = match_table,
	},
	.probe = business_battery_probe,
	.remove = business_battery_remove,
	.shutdown = business_battery_shutdown,
};

static int __init business_battery_init(void)
{
	return platform_driver_register(&business_battery_driver);
}
module_init(business_battery_init);

static void __exit business_battery_exit(void)
{
	platform_driver_unregister(&business_battery_driver);
}
module_exit(business_battery_exit);

MODULE_DESCRIPTION("business battery core");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

