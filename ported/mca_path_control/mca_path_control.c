//SPDX-License-Identifier: GPL-2.0
/*
 * mca_path_control.c
 *
 * path control driver
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
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>

#include "inc/mca_path_control.h"
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/hardware/hw_path_control.h>
#include <mca/strategy/strategy_wireless_class.h>


#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_path_control"
#endif

static struct mca_path_control *g_info;

static int mca_path_control_usb_in(struct mca_path_control *info, bool enable, int *temp_value)
{
	if (enable)
		info->condition_value |= PATH_CONTROL_USB;
	else
		info->condition_value &= ~PATH_CONTROL_USB;

	*temp_value = info->condition_value;
	mca_log_info("path's value is %d\n", *temp_value);
	return 0;
}

static int mca_path_control_wireless_in(struct mca_path_control *info, bool enable, int *temp_value)
{
	if (enable)
		info->condition_value |= PATH_CONTROL_WLS;
	else
		info->condition_value &= ~PATH_CONTROL_WLS;

	*temp_value = info->condition_value;
	mca_log_info("path's value is %d\n", *temp_value);

	return 0;
}

static int mca_path_control_wireless_rev(struct mca_path_control *info, bool enable, int *temp_value)
{
	if (enable)
		info->condition_value |= PATH_CONTROL_WLS_REV;
	else
		info->condition_value &= ~PATH_CONTROL_WLS_REV;

	*temp_value = info->condition_value;
	mca_log_info("path's value is %d\n", *temp_value);
	return 0;
}

static int mca_path_control_otg_in(struct mca_path_control *info, bool enable, int *temp_value)
{
	if (enable)
		info->condition_value |= PATH_CONTROL_OTG;
	else
		info->condition_value &= ~PATH_CONTROL_OTG;

	*temp_value = info->condition_value;
	mca_log_info("value is %d\n", *temp_value);

	return 0;
}

static int mca_path_control_wireless_vdd(struct mca_path_control *info, bool enable, int *temp_value)
{
	if (enable)
		info->condition_value |= PATH_CONTROL_VDD;
	else
		info->condition_value &= ~PATH_CONTROL_VDD;

	*temp_value = info->condition_value;
	mca_log_info("path's value is %d\n", *temp_value);
	return 0;
}

static int mca_path_control_handle_func(struct mca_path_control *info, int value)
{
	int i, j, l;
	struct gate_enable_info *temp_enable_cfg = NULL;
	int temp_enable_cfg_size = 0;
	int temp_role = -1, temp_enable = 0;

	for (i = 0; i < PATH_CONDITION_MAX_GROUP; i++) {
		if (info->control_path[i].condition == value)
			break;
	}
	if (i == PATH_CONDITION_MAX_GROUP) {
		mca_log_err("this condition is not exit\n");
		return -1;
	}
	mca_log_info("condition is %d\n", info->control_path[i].condition);

	for (j = 0; j < info->control_path[i].boost_cfg_num; j++) {
		for (l = 0; l < BOOST_TYPE_MAX; l++) {
			if (info->control_path[i].boost_cfg[j].boost_type[l] == -1)
				continue;
			if (info->control_path[i].boost_cfg[j].boost_type[l] != info->boost_src_default[l])
				break;
		}
		if (l == BOOST_TYPE_MAX)
			break;
	}
	if (j == info->control_path[i].boost_cfg_num) {
		mca_log_err("this boost combination is not exit\n");
		return -1;
	}
	mca_log_info("boost combination index is %d\n", j);

	temp_enable_cfg = info->control_path[i].boost_cfg[j].gate_enable_info;
	temp_enable_cfg_size = info->control_path[i].boost_cfg[j].gate_enable_para_size;
	for (i = 0; i < temp_enable_cfg_size; i++) {
		temp_role = temp_enable_cfg[i].control_gate_role;
		temp_enable = temp_enable_cfg[i].control_gate_enable;
		mca_log_info("gate[%d] ready to enable[%d]\n", temp_role, temp_enable);

		if (temp_enable != info->gate_sts[temp_role] &&
			info->control_scheme[temp_role].control_enable_func != NULL) {
			info->control_scheme[temp_role].control_enable_func(temp_role, temp_enable);
			info->gate_sts[temp_role] = temp_enable;
		}
	}

	return 0;
}

int mca_path_control_enable_gate(CONTROL_SRC src, bool enable)
{
	struct mca_path_control *info = g_info;
	int value;
	int ret = 0;

	mca_log_info("src: %d, enable: %d\n", src, enable);

	mutex_lock(&info->enable_handling_lock);

	switch (src) {
	case PATH_CONTROL_USB:
		ret = mca_path_control_usb_in(info, enable, &value);
		break;
	case PATH_CONTROL_WLS:
		ret = mca_path_control_wireless_in(info, enable, &value);
		break;
	case PATH_CONTROL_WLS_REV:
		ret = mca_path_control_wireless_rev(info, enable, &value);
		break;
	case PATH_CONTROL_OTG:
		ret = mca_path_control_otg_in(info, enable, &value);
		break;
	case PATH_CONTROL_VDD:
		ret = mca_path_control_wireless_vdd(info, enable, &value);
		break;
	default:
		break;
	}

	if (ret)
		goto exit;

	ret = mca_path_control_handle_func(info, value);

exit:
	mutex_unlock(&info->enable_handling_lock);
	return ret;
}
EXPORT_SYMBOL(mca_path_control_enable_gate);


static int mac_path_control_gpio_scheme_func(int role, bool enable)
{
	struct mca_path_control *info = g_info;
	int ret = 0;
	int control_gpio = 0;
	int gpio_enable_val = 0;

	for (int i = 0; i < MAX_ROLE; i++) {
		if (info->control_scheme[i].role != role)
			continue;
		control_gpio = info->control_scheme[i].gpio;

		ret = gpio_direction_output(control_gpio, enable);
		if (ret)
			mca_log_err("set direction for enable-gate-gpio[%d] failed\n", control_gpio);

		gpio_enable_val = gpio_get_value(control_gpio);
		mca_log_info("enable-gate-gpio[%d] val is :%d\n", control_gpio, gpio_enable_val);
		break;
	}
	return ret;
}

static int mac_path_control_pmic_register_scheme_func(int role, bool enable)
{
	return 0;
}

static int mac_path_control_cp_chip_scheme_func(int role, bool enable)
{
	struct mca_path_control *info = g_info;
	int ret = 0;
	int chip_role = 0;

	for (int i = 0; i < MAX_ROLE; i++) {
		if (info->control_scheme[i].role != role)
			continue;

		chip_role = info->control_scheme[i].cp_chip_role;
		switch (role) {
		case OVPGATE_ROLE:
			mca_log_info("set ovpgate: %d\n", enable);
			platform_class_cp_enable_ovpgate(chip_role, enable);
			break;
		case WPCGATE_ROLE:
			mca_log_info("set wpcgate: %d\n", enable);
			break;
		default:
			mca_log_err("this switch cp cannot control, exit\n");
			break;
		}
		break;
	}

	return ret;
}


static int mca_path_control_parse_cp_chip_scheme(struct device_node *node,
	const char *name, struct path_control_scheme_cfg *scheme_data)
{
	(void)mca_parse_dts_u32(node, name, &scheme_data->cp_chip_role, 0);
	return 0;
}

static int mca_path_control_parse_pmic_register_scheme(struct device_node *node,
	const char *name, struct path_control_scheme_cfg *scheme_data)
{
	(void)mca_parse_dts_u32(node, name, &scheme_data->reg, 0);
	return 0;
}

static int mca_path_control_parse_gpio_scheme(struct device_node *node,
	const char *name, struct path_control_scheme_cfg *scheme_data)
{
	int ret = 0;

	scheme_data->gpio = of_get_named_gpio(node, name, 0);
	if (!gpio_is_valid(scheme_data->gpio)) {
		mca_log_err("failed to parse gpio_scheme\n");
		return -1;
	}
	if (gpio_is_valid(scheme_data->gpio)) {
		ret = gpio_request(scheme_data->gpio, name);
		if (ret) {
			mca_log_err("request enable-gate-gpio[%d] failed\n", scheme_data->gpio);
			goto fail_scheme_gpio;
		}

		ret = gpio_direction_output(scheme_data->gpio, 0);
		if (ret) {
			mca_log_err("set direction for enable-gate-gpio[%d] failed\n", scheme_data->gpio);
			goto fail_scheme_gpio;
		}
	} else {
		mca_log_err("gpio[%d] not provided\n", scheme_data->gpio);
		goto fail_scheme_gpio;
	}

	return ret;
fail_scheme_gpio:
	gpio_free(scheme_data->gpio);
	return -1;
}

static int mca_path_control_parse_process_way(struct device_node *node,
	const char *name, struct path_control_scheme_cfg *scheme_data)
{
	int ret = 0;

	if (strcmp(name, "null") == 0) {
		mca_log_info("no need parse scheme para\n");
		return 0;
	}

	switch (scheme_data->scheme) {
	case GPIO_SCHEME:
		ret = mca_path_control_parse_gpio_scheme(node, name, scheme_data);
		if (!ret)
			scheme_data->control_enable_func = mac_path_control_gpio_scheme_func;
		break;
	case PMIC_REGISTER_SCHEME:
		ret = mca_path_control_parse_pmic_register_scheme(node, name, scheme_data);
		if (!ret)
			scheme_data->control_enable_func = mac_path_control_pmic_register_scheme_func;
		break;
	case CP_CHIP_SCHEME:
		ret = mca_path_control_parse_cp_chip_scheme(node, name, scheme_data);
		if (!ret)
			scheme_data->control_enable_func = mac_path_control_cp_chip_scheme_func;
		break;
	default:
		break;
	}
	return ret;
}

static int mca_path_control_parse_role(struct mca_path_control *info)
{
	struct device_node *node = info->dev->of_node;
	int ret = 0;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "control_scheme",
		MAX_ROLE,
		ITEM_MAX);
	if (array_len < 0) {
		mca_log_err("parse control_scheme failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "control_scheme", i, &tmp_string))
			return -1;

		row = i / ITEM_MAX;
		col = i % ITEM_MAX;
		mca_log_debug("[%d]control scheme %s\n", i, tmp_string);
		switch (col) {
		case GATE_ROLE:
			if (kstrtoint(tmp_string, 10, &info->control_scheme[row].role))
				return -1;
			break;
		case CONTROL_SCHEME:
			if (kstrtoint(tmp_string, 10, &info->control_scheme[row].scheme))
				return -1;
			break;
		case PROCESS_WAY:
			ret = mca_path_control_parse_process_way(node, tmp_string, &info->control_scheme[row]);
			break;
		default:
			break;
		}
	}
	return ret;
}

static int mca_path_control_parse_condition_process(struct device_node *node,
	const char *name, struct path_control_boost_para *boost_cfg)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	struct gate_enable_info *temp_enable_info = NULL;

	if (strcmp(name, "null") == 0) {
		mca_log_info("no need parse process_action para\n");
		return 0;
	}

	array_len = mca_parse_dts_count_strings(node, name,
		MAX_ROLE,
		CONTROL_GATE_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	boost_cfg->gate_enable_para_size = array_len / CONTROL_GATE_PARA_MAX;
	boost_cfg->gate_enable_info = kcalloc(boost_cfg->gate_enable_para_size,
		sizeof(*temp_enable_info), GFP_KERNEL);
	if (!boost_cfg->gate_enable_info) {
		mca_log_err("gate_enable para no mem\n");
		return -ENOMEM;
	}

	temp_enable_info = boost_cfg->gate_enable_info;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			goto error;

		row = i / CONTROL_GATE_PARA_MAX;
		col = i % CONTROL_GATE_PARA_MAX;
		mca_log_debug("[%d]control_gate_para %s\n", i, tmp_string);
		switch (col) {
		case CONTROL_GATE_PARA_GATE:
			if (kstrtoint(tmp_string, 10, &temp_enable_info[row].control_gate_role))
				goto error;
			break;
		case CONTROL_GATE_PARA_ENABLE:
			if (kstrtoint(tmp_string, 10, &temp_enable_info[row].control_gate_enable))
				goto error;
			break;
		default:
			break;
		}
	}

	return 0;
error:
	kfree(temp_enable_info);
	temp_enable_info = NULL;
	return -1;
}

static int mca_path_control_parse_condition_path(struct device_node *node,
	const char *name, struct path_control_condition_cfg *control_cfg)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	int temp_type;
	struct path_control_boost_para *boost_cfg = NULL;

	if (strcmp(name, "null") == 0) {
		mca_log_info("no need parse process para\n");
		return 0;
	}

	array_len = mca_parse_dts_count_strings(node, name,
		PATH_PROCESS_MAX_GROUP,
		PROCESS_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	control_cfg->boost_cfg_num = array_len / PROCESS_PARA_MAX;
	control_cfg->boost_cfg = kcalloc(control_cfg->boost_cfg_num, sizeof(*boost_cfg), GFP_KERNEL);
	if (!control_cfg->boost_cfg) {
		mca_log_err("boost cfg no mem\n");
		return -ENOMEM;
	}

	boost_cfg = control_cfg->boost_cfg;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			goto error;

		row = i / PROCESS_PARA_MAX;
		col = i % PROCESS_PARA_MAX;
		mca_log_debug("[%d]process para %s\n", i, tmp_string);
		if (col == PROCESS_METHOD) {
			if (mca_path_control_parse_condition_process(node, tmp_string,
				&boost_cfg[row]))
				goto error;
			continue;
		}
		switch (col % PATH_PROCESS_BOOST_PARA_MAX) {
		case BOOST_TYPE:
			if (kstrtoint(tmp_string, 10, &temp_type) && temp_type >= BOOST_TYPE_MAX)
				goto error;
			break;
		case BOOST_SOURCE:
			if (kstrtoint(tmp_string, 10, &boost_cfg[row].boost_type[temp_type]))
				goto error;
			break;
		default:
			break;
		}
	}

	return 0;
error:
	kfree(boost_cfg);
	boost_cfg = NULL;
	return -1;
}

static int mca_path_control_parse_condition(struct mca_path_control *info)
{
	struct device_node *node = info->dev->of_node;
	int ret = 0;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "path_condition",
		PATH_CONDITION_MAX_GROUP,
		CONTROL_PARA_MAX);
	if (array_len < 0) {
		mca_log_err("parse path_condition failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "path_condition", i, &tmp_string))
			return -1;

		row = i / CONTROL_PARA_MAX;
		col = i % CONTROL_PARA_MAX;
		mca_log_debug("[%d]path condition %s\n", i, tmp_string);
		switch (col) {
		case CONTROL_CONDITION:
			if (kstrtoint(tmp_string, 10, &info->control_path[row].condition))
				return -1;
			break;
		case CONTROL_PATH:
			ret = mca_path_control_parse_condition_path(node, tmp_string, &info->control_path[row]);
			break;
		default:
			break;
		}
	}
	return ret;
}

/*
static int mca_path_control_parse_pinctrl(struct mca_path_control *info)
{
	struct device_node *node = info->dev->of_node;
	int ret = 0;
	int array_len, i;
	const char *tmp_string = NULL;

	array_len = of_property_count_strings(node, "pinctrl-names");
	if (array_len < 0) {
		mca_log_err("parse pinctrl-names failed\n");
		return -1;
	}

	info->pinctrl = devm_pinctrl_get(info->dev);
	if (IS_ERR_OR_NULL(info->pinctrl)) {
		mca_log_err("failed to get pinctrl\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "pinctrl-names", i, &tmp_string))
			return -1;
		mca_log_info("[%d]pinctrl-names %s\n", i, tmp_string);
		info->pinctrl_stat = pinctrl_lookup_state(info->pinctrl, tmp_string);
		if (IS_ERR_OR_NULL(info->pinctrl_stat))
			mca_log_info("failed to parse [%s] pinctrl_stat\n", tmp_string);
		else {
			ret = pinctrl_select_state(info->pinctrl, info->pinctrl_stat);
			if (ret)
				mca_log_err("failed to select [%s] pinctrl_stat\n", tmp_string);
		}
	}
	return ret;
}
*/

static int mca_path_control_parse_dt(struct mca_path_control *info)
{
	int ret = 0;

	//ret = mca_path_control_parse_pinctrl(info);
	ret |= mca_path_control_parse_role(info);
	ret |= mca_path_control_parse_condition(info);

	return ret;
}

static ssize_t path_control_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t path_control_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info path_control_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(path_control_sysfs, 0664, USB_IN_PATH_CONTROL, usb_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_IN_PATH_CONTROL, wls_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_REV_PATH_CONTROL, wls_rev),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, OTG_IN_PATH_CONTROL, otg_in),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, WLS_VDD_PATH_CONTROL, wls_vdd),
	mca_sysfs_attr_rw(path_control_sysfs, 0664, TOTAL_PATH_CONTROL, control_value),
};

#define PATH_CONTROL_SYSFS_ATTRS_SIZE   ARRAY_SIZE(path_control_sysfs_field_tbl)

static struct attribute *path_control_sysfs_attrs[PATH_CONTROL_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group path_control_sysfs_attr_group = {
	.attrs = path_control_sysfs_attrs,
};

static ssize_t path_control_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct mca_path_control *info = dev_get_drvdata(dev);

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		path_control_sysfs_field_tbl, PATH_CONTROL_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("dev_driverdata is null\n");
		return -1;
	}

	switch (attr_info->sysfs_attr_name) {
	case USB_IN_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", (info->condition_value & PATH_CONTROL_USB));
		break;
	case WLS_IN_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", (info->condition_value & PATH_CONTROL_WLS));
		break;
	case WLS_REV_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", (info->condition_value & PATH_CONTROL_WLS_REV));
		break;
	case OTG_IN_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", (info->condition_value & PATH_CONTROL_OTG));
		break;
	case WLS_VDD_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", (info->condition_value & PATH_CONTROL_VDD));
		break;
	case TOTAL_PATH_CONTROL:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->condition_value);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t path_control_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct mca_path_control *info = dev_get_drvdata(dev);
	int val;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		path_control_sysfs_field_tbl, PATH_CONTROL_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("dev_driverdata is null\n");
		return -1;
	}

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case USB_IN_PATH_CONTROL:
		mca_path_control_enable_gate(PATH_CONTROL_USB, !!val);
		break;
	case WLS_IN_PATH_CONTROL:
		mca_path_control_enable_gate(PATH_CONTROL_WLS, !!val);
		break;
	case WLS_REV_PATH_CONTROL:
		mca_path_control_enable_gate(PATH_CONTROL_WLS_REV, !!val);
		break;
	case OTG_IN_PATH_CONTROL:
		mca_path_control_enable_gate(PATH_CONTROL_OTG, !!val);
		break;
	case WLS_VDD_PATH_CONTROL:
		mca_path_control_enable_gate(PATH_CONTROL_VDD, !!val);
		break;
	case TOTAL_PATH_CONTROL:
		mca_path_control_handle_func(info, val);
		break;
	default:
		break;
	}

	mca_log_info("set %d, enable = %d\n", attr_info->sysfs_attr_name, val);

	return count;
}

static int mca_path_control_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(path_control_sysfs_attrs, path_control_sysfs_field_tbl,
		PATH_CONTROL_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group(SYSFS_DEV_5, "path_control",
		dev, &path_control_sysfs_attr_group);
}

static void mca_path_control_update_status_work(struct work_struct *work)
{
	struct mca_path_control *info =  container_of(work,
		struct mca_path_control, update_condition_work.work);
	int ret = 0;

	mca_log_info("update condition status work\n");

	ret = platform_class_buckchg_ops_get_otg_boost_src(MAIN_BUCK_CHARGER, &info->boost_src_default[OTG_BOOST_TYPE]);
	ret |= mca_wireless_rev_get_rev_boost_default(&info->boost_src_default[WLS_REV_BOOST_TYPE]);
	mca_log_info("otg_boost_src is %d, wls_rev_boost_default is %d\n",
				info->boost_src_default[OTG_BOOST_TYPE], info->boost_src_default[WLS_REV_BOOST_TYPE]);
	ret |= platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &info->usb_online);
	ret |= platform_class_buckchg_ops_get_otg_boost_enable_status(MAIN_BUCK_CHARGER, &info->otg_boost_enable_sts);
	ret |= platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &info->wireless_online);
	ret |= mca_wireless_rev_get_reverse_chg(&info->wireless_rev_en);
	//TODO:add get wireless_vdd_en
	mca_log_info("usb_online: %d, wireless_online: %d, otg_boost_enable_sts: %d, wireless_rev_en: %d\n",
				info->usb_online, info->wireless_online, info->otg_boost_enable_sts, info->wireless_rev_en);
	if (info->usb_online)
		info->condition_value |= PATH_CONTROL_USB;
	else
		info->condition_value &= ~PATH_CONTROL_USB;
	if (info->wireless_online)
		info->condition_value |= PATH_CONTROL_WLS;
	else
		info->condition_value &= ~PATH_CONTROL_WLS;
	if (info->wireless_rev_en)
		info->condition_value |= PATH_CONTROL_WLS_REV;
	else
		info->condition_value &= ~PATH_CONTROL_WLS_REV;
	if (info->otg_boost_enable_sts == OTG_ENABLE_SEQUENCE || info->otg_boost_enable_sts == OTG_ENABLE)
		info->condition_value |= PATH_CONTROL_OTG;
	else
		info->condition_value &= ~PATH_CONTROL_OTG;
	if (info->wireless_vdd_en)
		info->condition_value |= PATH_CONTROL_VDD;
	else
		info->condition_value &= ~PATH_CONTROL_VDD;
}

static int mca_path_control_probe(struct platform_device *pdev)
{
	struct mca_path_control *info;
	static int probe_cnt;
	int ret = 0;

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	for (int i = 0; i < MAX_ROLE; i++)
		info->control_scheme[i].control_enable_func = NULL;
	info->gate_sts = kcalloc(MAX_ROLE, sizeof(int), GFP_KERNEL);
	memset(info->gate_sts, 0, MAX_ROLE);

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);

	mutex_init(&info->enable_handling_lock);

	ret = mca_path_control_parse_dt(info);
	if (ret) {
		mca_log_err("parst dts failed\n");
		return ret;
	}

	/* only obtained once when powering on, and judged based on condition_value later. */
	INIT_DELAYED_WORK(&info->update_condition_work, mca_path_control_update_status_work);
	schedule_delayed_work(&info->update_condition_work, msecs_to_jiffies(10000));

	g_info = info;
	ret = mca_path_control_sysfs_create_group(info->dev);

	mca_log_err("probe %s\n", ret == -EPROBE_DEFER ? "Over probe cnt max" : "OK");
	return ret;
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,path_control"},
	{},
};

static int mca_path_control_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_path_control_shutdown(struct platform_device *pdev)
{

}

static struct platform_driver mca_path_control_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_path_control",
		.of_match_table = match_table,
	},
	.probe = mca_path_control_probe,
	.remove = mca_path_control_remove,
	.shutdown = mca_path_control_shutdown,
};

static int __init mca_path_control_init(void)
{
	return platform_driver_register(&mca_path_control_driver);
}
module_init(mca_path_control_init);

static void __exit mca_path_control_exit(void)
{
	platform_driver_unregister(&mca_path_control_driver);
}
module_exit(mca_path_control_exit);

MODULE_DESCRIPTION("MCA Path Control");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
