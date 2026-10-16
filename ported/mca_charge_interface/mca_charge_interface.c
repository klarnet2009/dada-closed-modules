// SPDX-License-Identifier: GPL-2.0
/*
 * mca_charge_interface.c
 *
 * mca charger interface driver
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/common/mca_event.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charge_if"
#endif

#define MCA_CHARGE_IF_MAX_RW_BUFF 256

enum mca_charge_if_sysfs_type {
	MCA_CHRAGE_IF_SYSFS_BEGIN = 0,
	MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND = MCA_CHRAGE_IF_SYSFS_BEGIN,
	MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE,
	MCA_CHARGE_IF_SYSFS_IIN_LIMIT,
	MCA_CHRAGE_IF_SYSFS_ICHG_LIMIT,
	MCA_CHRAGE_IF_SYSFS_POWER_LIMIT,
	MCA_CHRAGE_IF_SYSFS_SUSPEND_STATUS,
	MCA_CHRAGE_IF_SYSFS_SHIPMODE,
	MCA_CHRAGE_IF_SYSFS_END,
};

struct mca_charge_if_dev {
	struct device *dev;
	struct mca_charge_if_ops *ops;
};

static const char * const g_mca_charge_if_op_type_table[MCA_CHARGE_IF_CHG_TYPE_END] = {
	[MCA_CHARGE_IF_CHG_TYPE_BUCK] = "buck",
	[MCA_CHARGE_IF_CHG_TYPE_MAIN_BUCK] = "main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_AUX_BUCK] = "aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_QC] = "quick",
	[MCA_CHARGE_IF_CHG_TYPE_QC_MAIN_PATH] = "quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_QC_AUX_PATH] = "quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV1] = "div1",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV2] = "div2",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV4] = "div4",
	[MCA_CHARGE_IF_CHG_TYPE_WL_BUCK] = "wl_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_MAIN_BUCK] = "wl_main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_AUX_BUCK] = "wl_aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC] = "wl_quick",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_MAIN_PATH] = "wl_quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_AUX_PATH] = "wl_quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV1] = "wl_div1",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV2] = "wl_div2",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV4] = "wl_div4",
	[MCA_CHARGE_IF_CHG_TYPE_ALL] = "all",
};

static struct mca_charge_if_ops *g_mca_charge_if_ops[MCA_CHARGE_IF_CHG_TYPE_END];

static int mca_charge_if_get_op_type(const char *type_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_mca_charge_if_op_type_table); i++) {
		if (!strcmp(type_name, g_mca_charge_if_op_type_table[i]))
			return i;
	}

	mca_log_err("fail to find type_name %s\n", type_name);
	return -1;
}

#ifdef CONFIG_SYSFS
static struct mca_charge_if_ops *mca_charge_if_get_ops(unsigned int type)
{
	return g_mca_charge_if_ops[type];
}

static const char *mca_charge_if_get_type_name(unsigned int type)
{
	if (type >= MCA_CHARGE_IF_CHG_TYPE_ALL)
		return "ivaild type";

	return g_mca_charge_if_op_type_table[type];
}

static int mca_charge_if_op_set(char *user, unsigned int type,
	unsigned int sysfs_type, char *value)
{
	struct mca_charge_if_ops *if_ops = mca_charge_if_get_ops(type);
	int ret = 0;
	unsigned int uint_value = 0;

	if (!if_ops)
		return 0;

	switch (sysfs_type) {
	case MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND:
		mca_log_err("set_input_suspend user: %s, value: %s\n", user, value);
		if (if_ops->set_input_suspend)
			ret = if_ops->set_input_suspend(user, value, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE:
		(void)kstrtouint(value, 0, &uint_value);
		if (if_ops->set_charge_enable)
			ret = if_ops->set_charge_enable(user, uint_value, if_ops->data);
		break;
	case MCA_CHARGE_IF_SYSFS_IIN_LIMIT:
		if (if_ops->set_input_current_limit)
			ret = if_ops->set_input_current_limit(user, value, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_ICHG_LIMIT:
		if (if_ops->set_charge_current_limit)
			ret = if_ops->set_charge_current_limit(user, value, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_POWER_LIMIT:
		(void)kstrtouint(value, 0, &uint_value);
		if (if_ops->set_charge_power_limit)
			ret = if_ops->set_charge_power_limit(user, uint_value, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_SHIPMODE:
		(void)kstrtouint(value, 10, &uint_value);
		if (if_ops->set_ship_mode_en)
			ret = if_ops->set_ship_mode_en(user, uint_value, if_ops->data);
		break;
	default:
		break;
	}

	return ret;
}

static int mca_charge_if_common_sysfs_set(char *user, unsigned int type,
	unsigned int sysfs_type, char *value)
{
	int begin, end;
	int ret = 0;

	mca_log_info("user %s set node %d type %d value %s\n",
		user, sysfs_type, type, value);

	if (type == MCA_CHARGE_IF_CHG_TYPE_ALL) {
		begin = 0;
		end = MCA_CHARGE_IF_CHG_TYPE_ALL;
	} else {
		begin = type;
		end = type + 1;
	}

	for (; begin < end; begin++)
		ret |= mca_charge_if_op_set(user, begin, sysfs_type, value);

	if (sysfs_type == MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND)
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO, MCA_EVENT_BATTERY_STS_CHANGE, NULL);

	return ret;
}

static int mca_charge_if_op_get(unsigned int sys_type, int chg_type, char *buf)
{
	struct mca_charge_if_ops *if_ops = NULL;
	int ret = -1;
	char value_buf[MCA_CHARGE_IF_MAX_VALUE_BUFF] = { 0 };

	if_ops = mca_charge_if_get_ops(chg_type);
	if (!if_ops)
		return -1;

	switch (sys_type) {
	case MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND:
		if (if_ops->get_input_suspend)
			ret = if_ops->get_input_suspend(value_buf, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE:
		if (if_ops->get_charge_enable)
			ret = if_ops->get_charge_enable(value_buf, if_ops->data);
		break;
	case MCA_CHARGE_IF_SYSFS_IIN_LIMIT:
		if (if_ops->get_input_current_limit)
			ret = if_ops->get_input_current_limit(value_buf, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_ICHG_LIMIT:
		if (if_ops->get_charge_current_limit)
			ret = if_ops->get_charge_current_limit(value_buf, if_ops->data);
		break;
	case MCA_CHRAGE_IF_SYSFS_POWER_LIMIT:
		if (if_ops->get_charge_power_limit)
			ret = if_ops->get_charge_power_limit(value_buf, if_ops->data);
		break;
	default:
		ret = -1;
		break;
	}

	if (ret)
		return ret;

	scnprintf(buf, MCA_CHARGE_IF_MAX_RW_BUFF, "%s %s\n",
		mca_charge_if_get_type_name(chg_type), value_buf);

	return 0;
}

static int mca_charge_suspend_state_show(bool *suspend_sts)
{
	int ret = 0;
	char rd_buf[MCA_CHARGE_IF_MAX_RW_BUFF] = { 0 };
	char last_chars[MCA_CHARGE_IF_CHG_TYPE_ALL];

	for (int i = 0; i < MCA_CHARGE_IF_CHG_TYPE_ALL; i++) {
		last_chars[i] = '\0';
		ret = mca_charge_if_op_get(MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND, i, rd_buf);
		if (ret != 0 || strlen(rd_buf) == 0) {
			last_chars[i] = '0';
			continue;
		}
		size_t last_char_index = strlen(rd_buf) - 2;

		last_chars[i] = rd_buf[last_char_index];
		memset(rd_buf, 0, MCA_CHARGE_IF_MAX_RW_BUFF);
	}

	*suspend_sts = false;
	for (int j = 0; j < MCA_CHARGE_IF_CHG_TYPE_ALL; j++) {
		if (last_chars[j] != '0') {
			*suspend_sts = true;
			break;
		}
	}
	mca_log_info("suspend sts = %d", *suspend_sts);
	return ret;
}

static int mca_charge_ship_mode_show(bool *pdata)
{
	struct mca_charge_if_ops *if_ops = mca_charge_if_get_ops(MCA_CHARGE_IF_CHG_TYPE_BUCK);
	int ret = 0;

	if (if_ops->get_ship_mode_status)
		ret = if_ops->get_ship_mode_status(pdata, if_ops->data);

	return ret;
}

static ssize_t mca_charge_if_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t mca_charge_if_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info mca_charge_if_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_INPUT_SUSPEND, input_suspend),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_CHARGE_ENABLE, charge_enable),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHARGE_IF_SYSFS_IIN_LIMIT, iin_limit),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_ICHG_LIMIT, ichg_limit),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_POWER_LIMIT, power_limit),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_SUSPEND_STATUS, suspend_status),
	mca_sysfs_attr_rw(mca_charge_if_sysfs, 0640, MCA_CHRAGE_IF_SYSFS_SHIPMODE, shipmode_count_reset),
};

#define MCA_CHARGE_IF_SYSFS_ATTRS_SIZE  ARRAY_SIZE(mca_charge_if_sysfs_field_tbl)

static struct attribute *mca_charge_if_sysfs_attrs[MCA_CHARGE_IF_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group mca_charge_if_sysfs_attr_group = {
	.attrs = mca_charge_if_sysfs_attrs,
};

static ssize_t mca_charge_if_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *info = NULL;
	char rd_buf[MCA_CHARGE_IF_MAX_RW_BUFF] = { 0 };
	int i;
	int ret;
	bool val = false;

	info = mca_sysfs_lookup_attr(attr->attr.name,
		mca_charge_if_sysfs_field_tbl, MCA_CHARGE_IF_SYSFS_ATTRS_SIZE);
	if (!info)
		return -EINVAL;

	if (info->sysfs_attr_name == MCA_CHRAGE_IF_SYSFS_SUSPEND_STATUS) {
		mca_charge_suspend_state_show(&val);
		return snprintf(buf, PAGE_SIZE, "%d\n", val);
	} else if (info->sysfs_attr_name == MCA_CHRAGE_IF_SYSFS_SHIPMODE) {
		mca_charge_ship_mode_show(&val);
		return snprintf(buf, PAGE_SIZE, "%d\n", val);
	}

	for (i = 0; i < MCA_CHARGE_IF_CHG_TYPE_ALL; i++) {
		ret = mca_charge_if_op_get(info->sysfs_attr_name, i, rd_buf);
		if (ret)
			continue;
		strncat(buf, rd_buf, strlen(rd_buf));
		memset(rd_buf, 0, MCA_CHARGE_IF_MAX_RW_BUFF);
	}

	return strlen(buf);
}

static ssize_t mca_charge_if_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *info = NULL;
	char user_name[MCA_CHARGE_IF_MAX_RW_BUFF] = { 0 };
	char type_name[MCA_CHARGE_IF_MAX_RW_BUFF] = { 0 };
	char value_str[MCA_CHARGE_IF_MAX_RW_BUFF] = { 0 };
	int type;

	info = mca_sysfs_lookup_attr(attr->attr.name,
		mca_charge_if_sysfs_field_tbl, MCA_CHARGE_IF_SYSFS_ATTRS_SIZE);
	if (!info)
		return -EINVAL;
	if (info->sysfs_attr_name == MCA_CHRAGE_IF_SYSFS_SHIPMODE) {
		strcpy(value_str, buf);
		strcpy(type_name, "buck");
		strcpy(user_name, "micharger");
	} else {
		/* reserve 2 bytes to prevent buffer overflow */
		if (count >= (MCA_CHARGE_IF_MAX_RW_BUFF - 2)) {
			mca_log_err("input too long\n");
			return -EINVAL;
		}

		/* 3: the fields of "user type value" */
		if (sscanf(buf, "%s %s %s", user_name, type_name, value_str) != 3) {
			mca_log_err("unable to parse input:%s\n", buf);
			return -EINVAL;
		}
	}

	type = mca_charge_if_get_op_type(type_name);
	if (type < 0)
		return -EINVAL;

	mca_charge_if_common_sysfs_set(user_name, type, info->sysfs_attr_name, value_str);

	return count;
}

static int mca_charge_if_sysfs_create_group(struct mca_charge_if_dev *info)
{
	mca_sysfs_init_attrs(mca_charge_if_sysfs_attrs,
		mca_charge_if_sysfs_field_tbl, MCA_CHARGE_IF_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_1, "charge_interface",
		info->dev, &mca_charge_if_sysfs_attr_group);
}

static void mca_charge_if_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group(SYSFS_DEV_1, "charge_interface", dev,
		&mca_charge_if_sysfs_attr_group);
}
#else
static inline int mca_charge_if_sysfs_create_group(struct mca_charge_if_dev *info)
{
	return 0;
}

static inline void mca_charge_if_sysfs_remove_group(struct device *dev)
{
}
#endif /* CONFIG_SYSFS */

int mca_charge_if_ops_register(struct mca_charge_if_ops *ops)
{
	int type;

	if (!ops || !ops->type_name)
		return -1;

	type = mca_charge_if_get_op_type(ops->type_name);
	if (type < 0) {
		mca_log_err("type is invalid %s\n", ops->type_name);
		return -1;
	}

	g_mca_charge_if_ops[type] = ops;

	return 0;
}
EXPORT_SYMBOL(mca_charge_if_ops_register);

static int mca_charge_if_probe(struct platform_device *pdev)
{
	struct mca_charge_if_dev *info = NULL;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	if (mca_charge_if_sysfs_create_group(info)) {
		mca_log_err("create sysfs failed\n");
		return -1;
	}

	return 0;
}

static int mca_charge_if_remove(struct platform_device *pdev)
{
	mca_charge_if_sysfs_remove_group(&pdev->dev);

	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,charge_interface" },
	{ },
};

static struct platform_driver charge_interface_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_charge_interface",
		.of_match_table = match_table,
	},
	.probe = mca_charge_if_probe,
	.remove = mca_charge_if_remove,
};

static int __init mca_charge_interface_init(void)
{
	return platform_driver_register(&charge_interface_driver);
}
module_init(mca_charge_interface_init);

static void __exit mca_charge_interface_exit(void)
{
	platform_driver_unregister(&charge_interface_driver);
}
module_exit(mca_charge_interface_exit);

MODULE_DESCRIPTION("xiaomi charge interface driver");
MODULE_AUTHOR("lipeng43@xiaomi.com");
MODULE_LICENSE("GPL v2");

