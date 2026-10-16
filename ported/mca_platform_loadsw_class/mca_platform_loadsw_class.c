// SPDX-License-Identifier: GPL-2.0
/*
 * mca_platform_loadsw_class.c
 *
 * mca platform load switch class driver
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
#include <linux/string.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "loadsw_class"
#endif
#define MCA_DIR_MAX_LENGTH 20
#define MCA_LOADSW_MAX_NUM	2
#define STRING_MAX_LEN	200

#define platform_loadsw_ops_invalid(data, name) (!data || !data->ops  || !data->ops->name)

struct platform_cp_class_ops_data {
	struct platform_class_loadsw_ops *ops;
	void *data;
};

struct platform_loadsw_dev {
	struct device *dev;
	int loadsw_num;
	const char *loadsw_dir_list[LOADSW_ROLE_MAX];
	struct device *sysfs_dev[LOADSW_ROLE_MAX];
	int loadsw_dev_index[LOADSW_ROLE_MAX];
	bool low_power;
};

static struct platform_loadsw_dev *g_platform_loadsw_dev;

union platform_loadsw_propval {
	unsigned int uintval;
	int intval;
	char strval[STRING_MAX_LEN];
	bool boolval;
};

enum loadsw_attr_list {
	LOADSW_PROP_CHIP_OK,
	LOADSW_PROP_IBAT_LIMIT,
	LOADSW_PROP_LOW_POWER,
};


static struct platform_cp_class_ops_data g_platform_cp_data[LOADSW_ROLE_MAX];

static inline struct platform_cp_class_ops_data *platform_loadsw_class_get_ic_ops(unsigned int role)
{
	if (role >= LOADSW_ROLE_MAX || g_platform_cp_data[role].ops == NULL)
		return NULL;

	return &g_platform_cp_data[role];
}

int platform_class_loadsw_register_ops(unsigned int role, struct platform_class_loadsw_ops *ops, void *data)
{
	if (role >= LOADSW_ROLE_MAX || !data || !ops)
		return -EOPNOTSUPP;

	g_platform_cp_data[role].data = data;
	g_platform_cp_data[role].ops = ops;

	return 0;
}
EXPORT_SYMBOL(platform_class_loadsw_register_ops);

int platform_class_loadsw_get_present(unsigned int role, bool *present)
{
	struct platform_cp_class_ops_data *temp_data = platform_loadsw_class_get_ic_ops(role);

	if (platform_loadsw_ops_invalid(temp_data, loadsw_get_present))
		return -1;

	return temp_data->ops->loadsw_get_present(present, temp_data->data);

}
EXPORT_SYMBOL(platform_class_loadsw_get_present);

int platform_class_loadsw_get_ibat_limit(unsigned int role, int *ibat_limit)
{
	struct platform_cp_class_ops_data *temp_data = platform_loadsw_class_get_ic_ops(role);

	if (platform_loadsw_ops_invalid(temp_data, loadsw_get_ibat_limit))
		return -1;

	return temp_data->ops->loadsw_get_ibat_limit(ibat_limit, temp_data->data);
}
EXPORT_SYMBOL(platform_class_loadsw_get_ibat_limit);

int platform_class_loadsw_set_ibat_limit(unsigned int role, int val)
{
	struct platform_cp_class_ops_data *temp_data = platform_loadsw_class_get_ic_ops(role);

	if (platform_loadsw_ops_invalid(temp_data, loadsw_set_ibat_limit))
		return -1;

	return temp_data->ops->loadsw_set_ibat_limit(val, temp_data->data);
}
EXPORT_SYMBOL(platform_class_loadsw_set_ibat_limit);

int platform_class_loadsw_set_lowpower_mode(unsigned int role, bool mode)
{
	struct platform_cp_class_ops_data *temp_data = platform_loadsw_class_get_ic_ops(role);

	if (platform_loadsw_ops_invalid(temp_data, loadsw_set_lowpower_mode))
		return -1;

	return temp_data->ops->loadsw_set_lowpower_mode(mode, temp_data->data);
}
EXPORT_SYMBOL(platform_class_loadsw_set_lowpower_mode);

int platform_class_loadsw_get_lowpower_mode(unsigned int role, bool *mode)
{
	struct platform_cp_class_ops_data *temp_data = platform_loadsw_class_get_ic_ops(role);

	if (platform_loadsw_ops_invalid(temp_data, loadsw_get_lowpower_mode))
		return -1;

	return temp_data->ops->loadsw_get_lowpower_mode(mode, temp_data->data);
}
EXPORT_SYMBOL(platform_class_loadsw_get_lowpower_mode);

static int platform_loadsw_dev_parse_dt(struct platform_loadsw_dev *loadsw)
{
	struct device_node *node = loadsw->dev->of_node;
	int count = 0;
	int ret = 0;

	if (!node) {
		mca_log_err("device tree info missing\n");
		return -1;
	}
	ret = mca_parse_dts_u32(node, "loadsw-num", &loadsw->loadsw_num, 1);
	if (ret) {
		mca_log_err("get loadsw-num fail\n");
		return ret;
	}

	count = mca_parse_dts_count_strings(node, "loadsw-dir-list", MCA_LOADSW_MAX_NUM, 1);
	mca_log_info("loadsw dir list max count: %d, %d\n", count, loadsw->loadsw_num);

	if (count != loadsw->loadsw_num)
		mca_log_err("loadsw_num can't match loadsw_dir_list count\n");

	for (int i = 0; i < count; i++) {
		ret = mca_parse_dts_string_index(node, "loadsw-dir-list", i, &(loadsw->loadsw_dir_list[i]));
		if (ret < 0) {
			mca_log_err("Unable to read loadsw-dir-list strings[%d]\n", i);
			return ret;
		}
	}

	mca_log_info("%s success\n", __func__);
	return ret;
}

static ssize_t loadsw_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t loadsw_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info loadsw_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(loadsw_sysfs, 0440, LOADSW_PROP_CHIP_OK, chip_ok),
	mca_sysfs_attr_rw(loadsw_sysfs, 0664, LOADSW_PROP_IBAT_LIMIT, ibat_limit),
	mca_sysfs_attr_rw(loadsw_sysfs, 0664, LOADSW_PROP_LOW_POWER, low_power),
};

#define LOADSW_SYSFS_ATTRS_SIZE   ARRAY_SIZE(loadsw_sysfs_field_tbl)

static struct attribute *loadsw_sysfs_attrs[LOADSW_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group loadsw_sysfs_attr_group = {
	.attrs = loadsw_sysfs_attrs,
};

static ssize_t loadsw_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int *loadsw_index;
	union platform_loadsw_propval val;
	ssize_t count = 0;


	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		loadsw_sysfs_field_tbl, LOADSW_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	loadsw_index = (int *)dev_get_drvdata(dev);
	if (!loadsw_index) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	mca_log_info("%s dev_driverdata is %d\n", __func__, *loadsw_index);

	switch (attr_info->sysfs_attr_name) {
	case LOADSW_PROP_CHIP_OK:
		platform_class_loadsw_get_present(*loadsw_index, &(val.boolval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.boolval);
		break;
	case LOADSW_PROP_IBAT_LIMIT:
		platform_class_loadsw_get_ibat_limit(*loadsw_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case LOADSW_PROP_LOW_POWER:
		platform_class_loadsw_get_lowpower_mode(*loadsw_index, &(val.boolval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.boolval);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t loadsw_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	int *loadsw_index;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		loadsw_sysfs_field_tbl, LOADSW_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	loadsw_index = (int *)dev_get_drvdata(dev);
	if (!loadsw_index) {
		mca_log_err("dev_driverdata is null\n");
		return -1;
	}

	mca_log_info("dev_driverdata is: %d, attr: %d, buf: %s\n",
		*loadsw_index, attr_info->sysfs_attr_name, buf);
	switch (attr_info->sysfs_attr_name) {
	case LOADSW_PROP_IBAT_LIMIT:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_class_loadsw_set_ibat_limit(*loadsw_index, val);
		mca_log_info("set ibat limit: %d\n", val);
		break;
	case LOADSW_PROP_LOW_POWER:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_class_loadsw_set_lowpower_mode(*loadsw_index, (bool)val);
		mca_log_info("set low power %d\n", val);
		break;
	default:
		break;
	}

	return count;
}

static const char * const loadsw_dev_list[MCA_LOADSW_MAX_NUM] = {
	[LOADSW_ROLE_MASTER] = "master",
	[LOADSW_ROLE_SLAVE] = "slave",
};

static void loadsw_sysfs_create_group(struct platform_loadsw_dev *loadsw)
{
	const char *loadsw_dev_name;

	mca_sysfs_init_attrs(loadsw_sysfs_attrs, loadsw_sysfs_field_tbl,
		LOADSW_SYSFS_ATTRS_SIZE);
	for (int i = 0; i < loadsw->loadsw_num; i++) {
		if (i > MCA_LOADSW_MAX_NUM) {
			mca_log_err("loadsw sysfsdev out of limit\n");
			return;
		}
		loadsw->sysfs_dev[i] = mca_sysfs_create_group("xm_power", loadsw->loadsw_dir_list[i],
			&loadsw_sysfs_attr_group);
		if (!loadsw->sysfs_dev[i])
			mca_log_err("creat loadsw[%d] sysfs fail\n", i);
	}

	for (int i = 0; i < loadsw->loadsw_num; i++) {
		int j;

		loadsw_dev_name = dev_name(loadsw->sysfs_dev[i]);
		for (j = 0; j < MCA_LOADSW_MAX_NUM; j++) {
			if (strstr(loadsw_dev_name, loadsw_dev_list[j])) {
				loadsw->loadsw_dev_index[j] = j;
				dev_set_drvdata(loadsw->sysfs_dev[i], &loadsw->loadsw_dev_index[j]);
				mca_log_err("success match loadsw_dev_name = %s, loadsw_dev_list[%d]=%s\n",
					loadsw_dev_name, j, loadsw_dev_list[j]);
				break;
			}
		}

		if (j >= MCA_LOADSW_MAX_NUM) {
			dev_set_drvdata(loadsw->sysfs_dev[j], NULL);
			mca_log_err("fail match loadsw_dev_name =%s\n", loadsw_dev_name);
		}
	}

}

static int platform_loadsw_class_probe(struct platform_device *pdev)
{
	struct platform_loadsw_dev *l_dev;
	static int probe_cnt;
	int rc = 0;

	mca_log_err("%s begin cnt %d\n", __func__, ++probe_cnt);
	l_dev = devm_kzalloc(&pdev->dev, sizeof(*l_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;
	l_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, l_dev);
	rc = platform_loadsw_dev_parse_dt(l_dev);
	if (rc < 0) {
		mca_log_err("%s Couldn't parse device tree rc=%d\n", __func__, rc);
		return rc;
	}

	g_platform_loadsw_dev = l_dev;

	loadsw_sysfs_create_group(l_dev);

	mca_log_err("%s success %d\n", __func__, ++probe_cnt);

	return 0;

}

static int platform_loadsw_class_remove(struct platform_device *pdev)
{
	return 0;
}

static void platform_loadsw_class_shutdown(struct platform_device *pdev)
{

}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,platform_loadsw"},
	{},
};


static struct platform_driver platform_loadsw_class_driver = {
	.driver	= {
		.name = "platform_loadsw_class",
		.of_match_table = match_table,
	},
	.probe = platform_loadsw_class_probe,
	.shutdown = platform_loadsw_class_shutdown,
	.remove  = platform_loadsw_class_remove,

};

module_platform_driver(platform_loadsw_class_driver);
MODULE_DESCRIPTION("platform loadsw class");
MODULE_AUTHOR("yinshunan@xiaomi.com");
MODULE_LICENSE("GPL v2");

