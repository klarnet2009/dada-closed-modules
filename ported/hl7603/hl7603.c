// SPDX-License-Identifier: GPL-2.0
/*
 * hl7603.c
 *
 * boost bypass ic driver
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
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include "inc/hl7603.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "boost_hl7603"
#endif

#define VOUT_THRSHOLD_DEFAULT 3400

static int hl7603_set_voltage_threshold(struct boost_bypass_dev *info, u32 vout_threshold);

#ifdef CONFIG_DEBUG_FS
enum boost_attr_list {
	BOOST_PROP_VOUT_THRESHOLD,
	BOOST_PROP_CHIP_ID,
	BOOST_PROP_ADDRESS,
	BOOST_PROP_COUNT,
	BOOST_PROP_DATA,
	BOOST_PROP_MAX,
};

static ssize_t boost_debugfs_show(void *priv_data, char *buf)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct boost_bypass_dev *dev_data = (struct boost_bypass_dev *)attr_data->private;
	u8 val = 0;
	ssize_t count = 0;
	char read_buf[MAX_LENGTH_BYTE] = {'\0'};
	int i, max_count;

	if (!dev_data || !attr_info) {
#line 61
		mca_log_err("null pointer show\n");
		return count;
	}
	switch (attr_info->debugfs_attr_name) {
	case BOOST_PROP_VOUT_THRESHOLD:
		val = i2c_smbus_read_byte_data(dev_data->client, VOUT_REG);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case BOOST_PROP_CHIP_ID:
		val = i2c_smbus_read_byte_data(dev_data->client, CHIP_ID_REG);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case BOOST_PROP_ADDRESS:
		count = scnprintf(buf, PAGE_SIZE, "%02x\n", dev_data->address);
		break;
	case BOOST_PROP_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", dev_data->count);
		break;
	case BOOST_PROP_DATA:
		if (dev_data->count < HL7603_MAX_REGS - dev_data->address)
			max_count = dev_data->count;
		else
			max_count = HL7603_MAX_REGS - dev_data->address;
		for (i = 0; i < max_count; i++) {
			val = i2c_smbus_read_byte_data(dev_data->client, dev_data->address + i);
			count += scnprintf(read_buf, MAX_LENGTH_BYTE, "%02x: %02x\n",
				dev_data->address + i, val);
			strcat(buf, read_buf);
		}
		break;
	default:
		break;
	}
	return count;
}

static ssize_t boost_debugfs_store(void *priv_data, const char *buf, size_t count)
{
	struct mca_debugfs_attr_data *attr_data = (struct mca_debugfs_attr_data *)priv_data;
	struct mca_debugfs_attr_info *attr_info = attr_data->attr_info;
	struct boost_bypass_dev *dev_data = (struct boost_bypass_dev *)attr_data->private;
	int val = 0;
	int ret = 0;

	if (!dev_data || !attr_info) {
#line 102
		mca_log_err("null pointer store\n");
		return count;
	}

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

#line 108
	mca_log_info("write attr[%d]: %d\n", attr_info->debugfs_attr_name, val);
	switch (attr_info->debugfs_attr_name) {
	case BOOST_PROP_VOUT_THRESHOLD:
		ret = hl7603_set_voltage_threshold(dev_data, val);
		if (ret < 0)
#line 114
			mca_log_err("BOOST_PROP_VOUT_THRESHOLD store fail\n");
		break;
	case BOOST_PROP_ADDRESS:
		dev_data->address = val;
		break;
	case BOOST_PROP_COUNT:
		dev_data->count = val;
		break;
	case BOOST_PROP_DATA:
		if (dev_data->address < HL7603_MAX_REGS)
			i2c_smbus_write_byte_data(dev_data->client, dev_data->address, val);
		break;
	default:
		break;
	}

	return count;
}

struct mca_debugfs_attr_info boost_debugfs_field_tbl[] = {
	mca_debugfs_attr(boost_debugfs, 0664, BOOST_PROP_VOUT_THRESHOLD, vout),
	mca_debugfs_attr(boost_debugfs, 0444, BOOST_PROP_CHIP_ID, chip_id),
	mca_debugfs_attr(boost_debugfs, 0664, BOOST_PROP_ADDRESS, address),
	mca_debugfs_attr(boost_debugfs, 0664, BOOST_PROP_COUNT, count),
	mca_debugfs_attr(boost_debugfs, 0664, BOOST_PROP_DATA, data),
};

#define BOOST_DEBUGFS_ATTRS_SIZE   ARRAY_SIZE(boost_debugfs_field_tbl)
#endif

static int hl7603_parse_dt(struct boost_bypass_dev *info)
{
	struct device_node *np = info->dev->of_node;

	if (!np) {
#line 149
		mca_log_err("device tree info missing\n");
		return -1;
	}
	mca_parse_dts_u32(np, "vout_threshold", &(info->vout_threshold), VOUT_THRSHOLD_DEFAULT);
	info->suport_hbm = of_property_read_bool(np, "support_hbm");
	if (info->suport_hbm)
		mca_parse_dts_u32(np, "hbm_vout_threshold", &(info->hbm_vout_threshold), VOUT_THRSHOLD_DEFAULT);

	return 0;
}

static int hl7603_set_voltage_threshold(struct boost_bypass_dev *info, u32 vout_threshold)
{
	u8 val = 0;
	int ret;

	if ((vout_threshold > VOUT_REG_MAX) || vout_threshold < VOUT_REG_BASE) {
#line 166
		mca_log_err("vout_threshold no valid %d", info->vout_threshold);
		return -1;
	}
	val = (vout_threshold - VOUT_REG_BASE) / VOUT_REG_STEP;

	ret = i2c_smbus_write_byte_data(info->client, VOUT_REG, val);
	if (ret < 0) {
#line 173
		mca_log_err("i2c read reg 0x%02X faild\n", VOUT_REG);
		return ret;
	}
	val = i2c_smbus_read_byte_data(info->client, VOUT_REG);
#line 177
	mca_log_info("i2c read reg [0x%02X]=[%d]\n", VOUT_REG, val);

	return ret;
}

static int hl7603_panel_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *val)
{
	struct boost_bypass_dev *info = container_of(nb, struct boost_bypass_dev, panel_nb);

	switch (event) {
	case MCA_EVENT_PANEL_HBM_STATE_CHANGE:
		if (info->suport_hbm)
			hl7603_set_voltage_threshold(info,
				*(int *)val ? info->hbm_vout_threshold : info->vout_threshold);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int hl7603_probe(struct i2c_client *client)
{
	int ret;
	struct boost_bypass_dev *info;

#line 205
	mca_log_info("%s start probe\n", __func__);
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = &client->dev;
	i2c_set_clientdata(client, info);
	ret = hl7603_parse_dt(info);
	ret |= hl7603_set_voltage_threshold(info, info->vout_threshold);
	if (ret) {
#line 216
		mca_log_err("failed to init hl7603\n");
		return ret;
	}
#ifdef CONFIG_DEBUG_FS
	mca_debugfs_create_group("hl7603", boost_debugfs_field_tbl, BOOST_DEBUGFS_ATTRS_SIZE, info);
#endif

	info->panel_nb.notifier_call = hl7603_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &info->panel_nb);

	info->count = 1;
#line 228
	mca_log_err("probe success\n");
	return 0;
}

static void hl7603_remove(struct i2c_client *client)
{
	struct boost_bypass_dev *info = i2c_get_clientdata(client);

	mca_event_block_notify_unregister(MCA_EVENT_TYPE_PANEL, &info->panel_nb);
	devm_kfree(&client->dev, info);
}

static const struct of_device_id hl7603_of_match[] = {
	{ .compatible = "hl7603"},
	{},
};

MODULE_DEVICE_TABLE(of, hl7603_of_match);

static struct i2c_driver hl7603_driver = {
	.driver = {
		.name = "hl7603_boost_bypass",
		.of_match_table = hl7603_of_match,
	},
	.probe = hl7603_probe,
	.remove = hl7603_remove,
};
module_i2c_driver(hl7603_driver);

MODULE_AUTHOR("lvxiaofeng <lvxiaofeng@xiaomi.com>");
MODULE_DESCRIPTION("hl7603 boot_bypass driver");
MODULE_LICENSE("GPL v2");
