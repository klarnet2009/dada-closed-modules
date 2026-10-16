// SPDX-License-Identifier: GPL-2.0
/*
 *mca_qcom_sysfs.c
 *
 * mca qcom sysfs driver
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
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/version.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/strategy/mca_wireless_revchg.h>
#include <mca/strategy/mca_charger_thermal.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_qcom_sysfs"
#endif

struct mca_qcom_sysfs {
	struct device *dev;
	struct class mca_qcom_class;
	bool support_multi_typec;
};

/* Standard usb_type definitions similar to power_supply_sysfs.c */
static const char * const power_supply_usb_type_text[] = {
	"Unknown", "SDP", "CDP", "DCP", "USB_FLOAT", "HVDCP",
	"HVDCP_3", "HVDCP_3_B", "HVDCP_3P5", "C", "PD", "PD_PPS", "PD_PPS",
	"Unknown", "ACA", "DCP",
};

static const char * const power_supply_wls_type_text[] = {
	"Unknown", "BPP", "EPP", "HPP"
};

static const char *get_usb_type_name(u32 usb_type)
{
	if (usb_type < ARRAY_SIZE(power_supply_usb_type_text))
		return power_supply_usb_type_text[usb_type];

	return "Unknown";
}

static const char *get_wls_type_name(u32 wls_type)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(power_supply_wls_type_text); i++) {
		if (i == wls_type)
			return power_supply_wls_type_text[i];
	}

	return "Unknown";
}

static ssize_t wireless_type_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	int chg_type = 0;

	rc = strategy_class_wireless_ops_get_wls_type(&chg_type);
	if (rc < 0)
		return rc;

#line 92
	mca_log_info("wireless type = %s\n", get_wls_type_name(chg_type));

	return snprintf(buf, PAGE_SIZE, "%s\n", get_wls_type_name(chg_type));
}
static CLASS_ATTR_RO(wireless_type);

static ssize_t real_type_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	int chg_type = 0;

	rc = protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &chg_type);
	if (rc < 0)
		return rc;

#line 108
	mca_log_info("real type = %s\n", get_usb_type_name(chg_type));

	return snprintf(buf, PAGE_SIZE, "%s\n", get_usb_type_name(chg_type));
}
static CLASS_ATTR_RO(real_type);

static ssize_t authentic_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val = 1;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t authentic_store(const struct class *c,
		const struct class_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}
static CLASS_ATTR_RW(authentic);

static ssize_t slave_authentic_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val = 1;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t slave_authentic_store(const struct class *c,
		const struct class_attribute *attr,
		const char *buf, size_t count)
{
	return count;
}
static CLASS_ATTR_RW(slave_authentic);

static ssize_t pd_verifed_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	int val;

	rc = protocol_class_get_adapter_verified(ADAPTER_PROTOCOL_PD, &val);
	if (rc < 0)
		return rc;

#line 156
	mca_log_info("show pd_verifed = %d\n", val);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t pd_verifed_store(const struct class *c,
		const struct class_attribute *attr,
		const char *buf, size_t count)
{
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

#line 171
	mca_log_info("store pd_verifed = %d\n", val);

	rc = protocol_class_set_adapter_verified(ADAPTER_PROTOCOL_PD, val);
	if (rc < 0)
		return rc;

	return count;
}
static CLASS_ATTR_RW(pd_verifed);

static ssize_t quick_charge_type_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int value = 0;
	int online = 0;

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
		STRATEGY_STATUS_TYPE_QC_TYPE, &value);

	if (!value)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_TYPE, &value);

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_QC_TYPE, &value);

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}
static CLASS_ATTR_RO(quick_charge_type);

static ssize_t power_max_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int value = 0;
	int online = 0;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online)
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
			STRATEGY_STATUS_TYPE_POWER_MAX, &value);

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}
static CLASS_ATTR_RO(power_max);

static ssize_t soc_decimal_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int soc_decimal, rate;

	(void)strategy_class_fg_ops_get_soc_decimal(&soc_decimal, &rate);

	return snprintf(buf, PAGE_SIZE, "%d\n", soc_decimal);
}
static CLASS_ATTR_RO(soc_decimal);

static ssize_t soc_decimal_rate_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int soc_decimal, rate;

	(void)strategy_class_fg_ops_get_soc_decimal(&soc_decimal, &rate);

	return snprintf(buf, PAGE_SIZE, "%d\n", rate);
}
static CLASS_ATTR_RO(soc_decimal_rate);

static ssize_t otg_ui_support_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool enable;

	platform_class_buckchg_ops_is_support_cid(MAIN_BUCK_CHARGER, &enable);
	return snprintf(buf, PAGE_SIZE, "%d\n", enable);

}
static CLASS_ATTR_RO(otg_ui_support);

static ssize_t cid_status_show(const struct class *c,
					const struct class_attribute *attr, char *buf)
{
	bool online = false;
	int rc;

	rc = protocol_class_pd_get_cid_status(TYPEC_PORT_0, &online);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", online);
}
static CLASS_ATTR_RO(cid_status);

static ssize_t cc_toggle_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;
	rc = protocol_class_pd_set_cc_toggle(TYPEC_PORT_0, val);
	if (rc < 0)
		return rc;

	return count;

}

static ssize_t cc_toggle_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool cc_toggle = false;
	int rc;

	rc = protocol_class_pd_get_cc_toggle(TYPEC_PORT_0, &cc_toggle);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", cc_toggle);

}
static CLASS_ATTR_RW(cc_toggle);

static ssize_t dam_ovpgate_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int rc;
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	rc = platform_class_cp_enable_ovpgate(CP_ROLE_MASTER, val);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t dam_ovpgate_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool en;
	int rc;

	rc = platform_class_cp_get_ovpgate_status(CP_ROLE_MASTER, &en);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", en);
}
static CLASS_ATTR_RW(dam_ovpgate);

static ssize_t wireless_chip_fw_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int val;
	int rc;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = mca_wireless_rev_update_fw_version(val);
	if (rc < 0)
		return rc;

	return count;

}

static ssize_t wireless_chip_fw_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	char wls_fw_data[10] = { 0 };

	rc = platform_class_wireless_get_fw_version(WIRELESS_ROLE_MASTER, wls_fw_data);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%s\n", wls_fw_data);

}
static CLASS_ATTR_RW(wireless_chip_fw);

static ssize_t reverse_chg_state_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = mca_wireless_rev_get_reverse_chg_state(&val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(reverse_chg_state);

static ssize_t reverse_chg_mode_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int val;
	int rc;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = mca_wireless_rev_enable_reverse_charge(val);
#line 418
	mca_log_err("zxy store reverse_chg_mode = %d\n", val);
	rc = mca_wireless_rev_set_user_reverse_chg(val);
	if (rc < 0)
		return rc;

	return count;

}

static ssize_t reverse_chg_mode_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool val = false;
	int rc;

	rc = mca_wireless_rev_get_reverse_chg(&val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RW(reverse_chg_mode);

static ssize_t magnetic_case_flag_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool val = false;
	int rc;

	rc = platform_class_wireless_get_hall_gpio_status(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(magnetic_case_flag);

static ssize_t tx_adapter_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = platform_class_wireless_get_tx_adapter(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(tx_adapter);

static ssize_t rx_vout_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(rx_vout);

static ssize_t rx_vrect_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = platform_class_wireless_get_vrect(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(rx_vrect);

static ssize_t rx_iout_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(rx_iout);

static ssize_t rx_ss_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val;
	int rc;

	rc = platform_class_wireless_get_ss_voltage(WIRELESS_ROLE_MASTER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RO(rx_ss);

static ssize_t shipmode_count_reset_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int rc;
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = platform_class_buckchg_ops_set_ship_mode(MAIN_BUCK_CHARGER, !!val);

	if (rc < 0)
		return rc;

	return count;
}

static ssize_t shipmode_count_reset_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	bool val;

	rc = platform_class_buckchg_ops_get_ship_mode(MAIN_BUCK_CHARGER, &val);
	if (rc < 0)
		return rc;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}
static CLASS_ATTR_RW(shipmode_count_reset);

static ssize_t has_dp_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool has_dp = false;
	int rc;

	rc = protocol_class_pd_get_has_dp(TYPEC_PORT_0, &has_dp);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", has_dp);

}
static CLASS_ATTR_RO(has_dp);

static ssize_t wls_thermal_remove_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int val;
	int rc;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = mca_set_wls_charger_thermal_remove(val);
#line 588
	mca_log_err("zxy store wls_thermal_remove = %d\n", val);
	if (rc < 0)
		return rc;

	return count;

}

static ssize_t wls_thermal_remove_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	bool val = false;
	int rc;

	rc = mca_get_wls_charger_thermal_remove(&val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);

}
static CLASS_ATTR_RW(wls_thermal_remove);

static ssize_t usb_real_type_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	int chg_type = 0;

	rc = protocol_class_get_adapter_type(5, &chg_type);
	if (rc < 0)
		return rc;

#line 255
	mca_log_info("usb real type = %s\n", get_usb_type_name(chg_type));

	return snprintf(buf, PAGE_SIZE, "%s\n", get_usb_type_name(chg_type));
}
static CLASS_ATTR_RO(usb_real_type);

static ssize_t pmic_ibat_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int rc;
	int val = 0;

	rc = platform_class_buckchg_ops_get_pack_ibat(MAIN_BUCK_CHARGER, &val);
	if (rc < 0)
		return rc;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}
static CLASS_ATTR_RO(pmic_ibat);

static ssize_t hall_phone_case_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int val = 0;

	(void)platform_class_wireless_get_phone_case_category(WIRELESS_ROLE_MASTER, &val);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t hall_phone_case_store(const struct class *c,
		const struct class_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int rc;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	rc = platform_class_wireless_set_phone_case_category(WIRELESS_ROLE_MASTER, val);
	if (rc < 0)
		return rc;

#line 624
	mca_log_err("store hall_phone_case = %d\n", val);

	return count;
}
static CLASS_ATTR_RW(hall_phone_case);

static ssize_t soh_show(const struct class *c,
		const struct class_attribute *attr, char *buf)
{
	int soh = 0;

	(void)strategy_class_fg_get_soh(&soh);

	return snprintf(buf, PAGE_SIZE, "%d\n", soh);
}
static CLASS_ATTR_RO(soh);

static struct attribute *mca_qcom_sysfs_attrs[] = {
	&class_attr_real_type.attr,
	&class_attr_wireless_type.attr,
	&class_attr_authentic.attr,
	&class_attr_slave_authentic.attr,
	&class_attr_pd_verifed.attr,
	&class_attr_quick_charge_type.attr,
	&class_attr_power_max.attr,
	&class_attr_soc_decimal.attr,
	&class_attr_soc_decimal_rate.attr,
	&class_attr_usb_real_type.attr,
	&class_attr_otg_ui_support.attr,
	&class_attr_cid_status.attr,
	&class_attr_cc_toggle.attr,
	&class_attr_has_dp.attr,
	&class_attr_dam_ovpgate.attr,
	&class_attr_pmic_ibat.attr,
	&class_attr_wireless_chip_fw.attr,
	&class_attr_reverse_chg_mode.attr,
	&class_attr_reverse_chg_state.attr,
	&class_attr_magnetic_case_flag.attr,
	&class_attr_tx_adapter.attr,
	&class_attr_rx_vout.attr,
	&class_attr_rx_vrect.attr,
	&class_attr_rx_iout.attr,
	&class_attr_rx_ss.attr,
	&class_attr_shipmode_count_reset.attr,
	&class_attr_wls_thermal_remove.attr,
	&class_attr_hall_phone_case.attr,
	&class_attr_soh.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mca_qcom_sysfs);

static int qcom_sysfs_parse_dt(struct mca_qcom_sysfs *o1_dev)
{
	struct device_node *node = o1_dev->dev->of_node;

	o1_dev->support_multi_typec = of_property_read_bool(node, "mi,support_multi_typec");
	return 0;

}

static int mca_qcom_sysfs_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct mca_qcom_sysfs *qcom_sysfs;

#line 696
	mca_log_info("probe begin\n");
	qcom_sysfs = devm_kzalloc(&pdev->dev, sizeof(*qcom_sysfs), GFP_KERNEL);
	if (!qcom_sysfs)
		return -ENOMEM;

	qcom_sysfs->dev = &pdev->dev;
	rc = qcom_sysfs_parse_dt(qcom_sysfs);
	if (rc) {
		mca_log_err("%s parse dt fail\n", __func__);
		return rc;
	}
	platform_set_drvdata(pdev, qcom_sysfs);
	qcom_sysfs->mca_qcom_class.name = "qcom-battery";
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 9))
	qcom_sysfs->mca_qcom_class.owner = THIS_MODULE;
#endif
	qcom_sysfs->mca_qcom_class.class_groups = mca_qcom_sysfs_groups;
	rc = class_register(&qcom_sysfs->mca_qcom_class);
	if (rc < 0) {
#line 715
		mca_log_err("class reg failed %d\n", rc);
		return rc;
	}

#line 719
	mca_log_err("probe ok\n");
	return 0;
}

static int mca_qcom_sysfs_remove(struct platform_device *pdev)
{
	struct mca_qcom_sysfs *qcom_sysfs = platform_get_drvdata(pdev);

	class_unregister(&qcom_sysfs->mca_qcom_class);

	return 0;
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,qcom_sysfs"},
	{},
};

static struct platform_driver mca_qcom_sysfs_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "mca_qcom_sysfs",
		.of_match_table = match_table,
	},
	.probe = mca_qcom_sysfs_probe,
	.remove = mca_qcom_sysfs_remove,
};

static int __init mca_qcom_sysfs_init(void)
{
	return platform_driver_register(&mca_qcom_sysfs_driver);
}
module_init(mca_qcom_sysfs_init);

static void __exit mca_qcom_sysfs_exit(void)
{
	platform_driver_unregister(&mca_qcom_sysfs_driver);
}
module_exit(mca_qcom_sysfs_exit);


MODULE_DESCRIPTION("mca qcom sysfs");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

