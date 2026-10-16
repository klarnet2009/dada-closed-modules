// SPDX-License-Identifier: GPL-2.0
/*
 *mca_strategy_class.c
 *
 * mca strategy class driver
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
#include <linux/platform_device.h>
#include <mca/strategy/strategy_class.h>

struct mca_strategy_func_data {
	mca_strategy_func func;
	mca_strategy_get_status get_func;
	mca_strategy_set_config set_config;
	void *data;
};

static struct mca_strategy_func_data g_mca_stg_func[STRATEGY_FUNC_TYPE_MAX];

static struct mca_strategy_func_data *mca_strategy_get_func_data(unsigned int type)
{
	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return NULL;

	return &g_mca_stg_func[type];
}

int mca_strategy_func_get_status(int type, int status, void *value)
{
	struct mca_strategy_func_data *temp_data = mca_strategy_get_func_data(type);

	if (!temp_data || !temp_data->get_func)
		return -1;

	return temp_data->get_func(status, value, temp_data->data);
}
EXPORT_SYMBOL(mca_strategy_func_get_status);

int mca_strategy_func_process(unsigned int type, int event, int value)
{
	struct mca_strategy_func_data *temp_data = mca_strategy_get_func_data(type);

	if (!temp_data || !temp_data->func)
		return -1;

	return temp_data->func(event, value, temp_data->data);
}
EXPORT_SYMBOL(mca_strategy_func_process);

int mca_strategy_func_set_config(int type, int conifg, int value)
{
	struct mca_strategy_func_data *temp_data = mca_strategy_get_func_data(type);

	if (!temp_data || !temp_data->set_config)
		return -1;

	return temp_data->set_config(conifg, value, temp_data->data);
}
EXPORT_SYMBOL(mca_strategy_func_set_config);

int mca_strategy_ops_register(unsigned int type, mca_strategy_func func,
	mca_strategy_get_status get_func, mca_strategy_set_config set_config, void *data)
{
	if (type >= STRATEGY_FUNC_TYPE_MAX)
		return -1;

	g_mca_stg_func[type].func = func;
	g_mca_stg_func[type].get_func = get_func;
	g_mca_stg_func[type].set_config = set_config;
	g_mca_stg_func[type].data = data;
	return 0;
}
EXPORT_SYMBOL(mca_strategy_ops_register);

static struct platform_driver mca_strategy_class_driver = {
	.driver	= {
		.name = "mca_strategy_class",
	},
};


module_platform_driver(mca_strategy_class_driver);


MODULE_DESCRIPTION("mca strategy class");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

