// SPDX-License-Identifier: GPL-2.0
/*
 * mca_platform_bc12_class.c
 *
 * mca bc 12 class driver
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
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <mca/common/mca_log.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/platform/platform_bc12_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "platform_bc12_class"
#endif

struct platform_class_bc12_ops_data {
	struct platform_bc12_class_ops *ops;
	void *data;
};

struct platform_class_bc12_info {
	struct device *dev;
	bool support_multi_bc12;
};

static struct platform_class_bc12_ops_data g_bc12_ops_data[BC12_MAX_ROLE];

int platform_bc12_class_ops_register(unsigned int role,
	struct platform_bc12_class_ops *ops, void *data)
{
	if (!ops || role >= BC12_MAX_ROLE)
		return -1;

	g_bc12_ops_data[role].data = data;
	g_bc12_ops_data[role].ops = ops;

	return 0;
}
EXPORT_SYMBOL(platform_bc12_class_ops_register);

static struct platform_class_bc12_ops_data *platform_class_bc12_get_ops_data(unsigned int role)
{
	if (role >= BC12_MAX_ROLE)
		return NULL;

	return &g_bc12_ops_data[role];
}

static int platform_class_bc12_ops_det_en(unsigned int role, int en)
{
	struct platform_class_bc12_ops_data *temp_data = platform_class_bc12_get_ops_data(role);

	if (!temp_data || !temp_data->ops || !temp_data->ops->bc12_det_en)
		return -1;

	return temp_data->ops->bc12_det_en(en, temp_data->data);
}

static int platform_class_bc12_ops_get_chg_type(unsigned int role, int *value)
{
	struct platform_class_bc12_ops_data *temp_data = platform_class_bc12_get_ops_data(role);

	if (!temp_data || !temp_data->ops || !temp_data->ops->get_charge_type)
		return -1;

	return temp_data->ops->get_charge_type(value, temp_data->data);
}

static unsigned int platform_class_bc12_get_role(struct platform_class_bc12_info *info)
{
	int pd_active = 0;

	if (!info->support_multi_bc12)
		return BC12_MAIN_ROLE;

	(void)protocol_class_pd_get_pd_active(TYPEC_PORT_1, &pd_active);
	if (pd_active)
		return BC12_AUX_ROLE;

	return BC12_MAIN_ROLE;
}

static int platform_class_bc12_det_en(void *data, int en)
{
	struct platform_class_bc12_info *info = (struct platform_class_bc12_info *)data;
	unsigned int role;

	if (!data)
		return -1;

	role = platform_class_bc12_get_role(info);

	return platform_class_bc12_ops_det_en(role, en);
}

static int platform_class_bc12_get_real_type(void *data, int *value)
{
	struct platform_class_bc12_info *info = (struct platform_class_bc12_info *)data;
	unsigned int role;

	if (!data)
		return -1;

	role = platform_class_bc12_get_role(info);

	return platform_class_bc12_ops_get_chg_type(role, value);
}

static int platform_class_bc12_parse_dt(struct platform_class_bc12_info *info)
{
	struct device_node *node = info->dev->of_node;

	info->support_multi_bc12 = of_property_read_bool(node, "support-multi-bc12");

	return 0;
}

static struct adapter_protocol_class_ops g_bc12_ops = {
	.adapter_det_en = platform_class_bc12_det_en,
	.get_adapter_type = platform_class_bc12_get_real_type,
};

static int platform_class_bc12_probe(struct platform_device *pdev)
{
	struct platform_class_bc12_info *platform_class_bc12;

	platform_class_bc12 = devm_kzalloc(&pdev->dev, sizeof(*platform_class_bc12), GFP_KERNEL);
	if (!platform_class_bc12)
		return -ENOMEM;

	platform_class_bc12->dev = &pdev->dev;
	platform_class_bc12_parse_dt(platform_class_bc12);
	(void)protocol_class_register_ops(ADAPTER_PROTOCOL_BC12,
		&g_bc12_ops, platform_class_bc12);

	platform_set_drvdata(pdev, platform_class_bc12);
#line 157
	mca_log_err("probe ok\n");
	return 0;
}

static int platform_class_bc12_remove(struct platform_device *pdev)
{
	return 0;
}

static void platform_class_bc12_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,bc12_class"},
	{},
};

static struct platform_driver platform_bc12_class_driver = {
	.driver	= {
		.owner = THIS_MODULE,
		.name = "platform_bc12_class",
		.of_match_table = match_table,
	},
	.probe = platform_class_bc12_probe,
	.remove = platform_class_bc12_remove,
	.shutdown = platform_class_bc12_shutdown,
};

static int __init platform_class_bc12_init(void)
{
	return platform_driver_register(&platform_bc12_class_driver);
}
module_init(platform_class_bc12_init);

static void __exit platform_class_bc12_exit(void)
{
	platform_driver_unregister(&platform_bc12_class_driver);
}
module_exit(platform_class_bc12_exit);


MODULE_DESCRIPTION("mca protocol class");
MODULE_AUTHOR("liyuze@xiaomi.com");
MODULE_LICENSE("GPL v2");

