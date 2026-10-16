// SPDX-License-Identifier: GPL-2.0
/*
 * mca_protocol_class.c
 *
 * mca protocol class driver
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
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <mca/protocol/protocol_class.h>

#define adapter_protocol_invalid_ops(data, name) (!data || !data->ops || !data->ops->name)

struct adapter_protocol_class_data {
	struct adapter_protocol_class_ops *ops;
	void *data;
};
static struct adapter_protocol_class_data g_protocol_class_data[ADAPTER_PROTOCOL_MAX];

static struct adapter_protocol_class_data *protocol_class_get_protocol_data(unsigned int protocol)
{
	if (protocol >= ADAPTER_PROTOCOL_MAX)
		return NULL;

	return &g_protocol_class_data[protocol];
}

int protocol_class_register_ops(unsigned int protocol, struct adapter_protocol_class_ops *ops, void *data)
{
	if (protocol >= ADAPTER_PROTOCOL_MAX || !ops)
		return -1;

	g_protocol_class_data[protocol].ops = ops;
	g_protocol_class_data[protocol].data = data;

	return 0;
}
EXPORT_SYMBOL(protocol_class_register_ops);

int protocol_class_set_adapter_verified(unsigned int protocol, int verified)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, set_adapter_verified))
		return -1;

	return temp_data->ops->set_adapter_verified(temp_data->data, verified);
}
EXPORT_SYMBOL(protocol_class_set_adapter_verified);

int protocol_class_get_adapter_verified(unsigned int protocol, int *verified)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_verified))
		return -1;

	return temp_data->ops->get_adapter_verified(temp_data->data, verified);
}
EXPORT_SYMBOL(protocol_class_get_adapter_verified);

int protocol_class_get_adapter_max_power(unsigned int protocol, unsigned int *max_power)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_max_power))
		return -1;

	return temp_data->ops->get_adapter_max_power(temp_data->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_max_power);

int protocol_class_get_adapter_pwr_max_power(unsigned int protocol, unsigned int *max_power)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_pwr_max_power))
		return -1;

	return temp_data->ops->get_adapter_pwr_max_power(temp_data->data, max_power);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pwr_max_power);

int protocol_class_det_adapter_type(unsigned int protocol, int en)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, adapter_det_en))
		return -1;

	return temp_data->ops->adapter_det_en(temp_data->data, en);
}
EXPORT_SYMBOL(protocol_class_det_adapter_type);

int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_type))
		return -1;

	return temp_data->ops->get_adapter_type(temp_data->data, (int *)value);
}
EXPORT_SYMBOL(protocol_class_get_adapter_type);

int protocol_class_get_adapter_power_cap(unsigned int protocol, struct adapter_power_cap_info *cap)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_pwr_cap))
		return -1;

	return temp_data->ops->get_adapter_pwr_cap(temp_data->data, cap);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_cap);


int protocol_class_set_adapter_volt_and_curr(unsigned int protocol, int volt, int curr)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, set_adapter_volt_and_curr))
		return -1;

	return temp_data->ops->set_adapter_volt_and_curr(temp_data->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_set_adapter_volt_and_curr);

int protocol_class_get_adapter_volt_and_curr(unsigned int protocol, int *volt, int *curr)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_volt_and_curr))
		return -1;

	return temp_data->ops->get_adapter_volt_and_curr(temp_data->data, volt, curr);
}
EXPORT_SYMBOL(protocol_class_get_adapter_volt_and_curr);

int protocol_class_get_adapter_pps_ptf(unsigned int protocol, int *pps_ptf)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_pps_ptf))
		return -1;

	return temp_data->ops->get_adapter_pps_ptf(temp_data->data, pps_ptf);
}
EXPORT_SYMBOL(protocol_class_get_adapter_pps_ptf);

int protocol_class_get_adapter_info(unsigned int protocol, struct adapter_vendor_info *info)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_info))
		return -1;

	return temp_data->ops->get_adapter_info(temp_data->data, info);
}
EXPORT_SYMBOL(protocol_class_get_adapter_info);

int protocol_class_get_adapter_power_curve(unsigned int protocol, struct adapter_power_curve *pwr_curve)
{
	struct adapter_protocol_class_data *temp_data = protocol_class_get_protocol_data(protocol);

	if (adapter_protocol_invalid_ops(temp_data, get_adapter_power_curve))
		return -1;

	return temp_data->ops->get_adapter_power_curve(temp_data->data, pwr_curve);
}
EXPORT_SYMBOL(protocol_class_get_adapter_power_curve);

static struct platform_driver protocol_class_driver = {
	.driver	= {
		.name = "protocol_class",
	},
};

module_platform_driver(protocol_class_driver);

MODULE_DESCRIPTION("mca protocol class");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

