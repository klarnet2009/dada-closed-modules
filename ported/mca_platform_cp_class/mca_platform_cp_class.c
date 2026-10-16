// SPDX-License-Identifier: GPL-2.0
/*
 * mca_platform_cp_dev.c
 *
 * mca platform charge pump class driver
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
#include <mca/platform/platform_cp_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "cp_class"
#endif
#define MCA_DIR_MAX_LENGTH 20
#define MCA_CP_MAX_NUM	3
#define STRING_MAX_LEN	200

#define platform_cp_ops_invalid(data, name) (!data || !data->ops  || !data->ops->name)

struct platform_cp_class_ops_data {
	struct platform_class_cp_ops *ops;
	void *data;
};

struct platform_cp_dev {
	struct device *dev;
	int cp_num;
	const char *cp_dir_list[MCA_CP_MAX_NUM];
	struct device *sysfs_dev[MCA_CP_MAX_NUM];
	int cp_dev_index[MCA_CP_MAX_NUM];
	int force_fsw;
};

static struct platform_cp_dev *g_platform_cp_dev;

union platform_cp_propval {
	unsigned int uintval;
	int intval;
	char strval[STRING_MAX_LEN];
	bool boolval;
};

enum cp_attr_list {
	CP_PROP_CHIP_OK,
	CP_PROP_VBUS,
	CP_PROP_VUSB,
	CP_PROP_IBUS,
	CP_PROP_BATT_PRESENT,
	CP_PROP_BATT_TEMP,
	CP_PROP_VPACK,
	CP_PROP_OVPGATE,
	CP_PROP_FSW,
	CP_PROP_TDIE,
};

enum platform_cp_attr_list {
	CP_PROP_IBUS_DELTA,
	CP_RORP_IBUS_TOTAL,
	CP_PROP_WORK_MODE,
};

static struct platform_cp_class_ops_data g_platform_cp_data[CP_ROLE_MAX];

static inline struct platform_cp_class_ops_data *platform_cp_class_get_ic_ops(unsigned int role)
{
	if (role >= CP_ROLE_MAX || g_platform_cp_data[role].ops == NULL)
		return NULL;

	return &g_platform_cp_data[role];
}

int platform_class_cp_register_ops(unsigned int role, struct platform_class_cp_ops *ops, void *data)
{
	if (role >= CP_ROLE_MAX || !data || !ops)
		return -EOPNOTSUPP;

	g_platform_cp_data[role].data = data;
	g_platform_cp_data[role].ops = ops;

	return 0;
}
EXPORT_SYMBOL(platform_class_cp_register_ops);

int platform_class_cp_set_charging_enable(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_enable))
		return -1;

	return temp_data->ops->cp_set_enable(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_charging_enable);

int platform_class_cp_get_charging_enabled(unsigned int role, bool *en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_enabled))
		return -1;

	return temp_data->ops->cp_get_enabled(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_charging_enabled);

int platform_class_cp_set_present(unsigned int role, bool present)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_present))
		return -1;

	return temp_data->ops->cp_set_present(present, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_present);

int platform_class_cp_get_vbus_present(unsigned int role, bool *present)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_vbus_present))
		return -1;

	return temp_data->ops->cp_get_vbus_present(present, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_vbus_present);

int platform_class_cp_get_present(unsigned int role, bool *present)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_present))
		return -1;

	return temp_data->ops->cp_get_present(present, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_present);

int platform_class_cp_get_battery_present(unsigned int role, bool *present)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_battery_present))
		return -1;

	return temp_data->ops->cp_get_battery_present(present, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_battery_present);

int platform_class_cp_get_battery_voltage(unsigned int role, int *vbat)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_battery_voltage))
		return -1;

	return temp_data->ops->cp_get_battery_voltage(vbat, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_battery_voltage);

int platform_class_cp_get_battery_current(unsigned int role, int *ibat)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_battery_current))
		return -1;

	return temp_data->ops->cp_get_battery_current(ibat, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_battery_current);

int platform_class_cp_get_battery_temperature(unsigned int role, int *tbat)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_battery_temperature))
		return -1;

	return temp_data->ops->cp_get_battery_temperature(tbat, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_battery_temperature);

int platform_class_cp_get_bus_voltage(unsigned int role, int *vbus)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_bus_voltage))
		return -1;

	return temp_data->ops->cp_get_bus_voltage(vbus, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_bus_voltage);

int platform_class_cp_get_bus_current(unsigned int role, int *ibus)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_bus_current))
		return -1;

	return temp_data->ops->cp_get_bus_current(ibus, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_bus_current);

int platform_class_cp_get_bus_temperature(unsigned int role, int *tbus)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_bus_temperature))
		return -1;

	return temp_data->ops->cp_get_bus_temperature(tbus, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_bus_temperature);

int platform_class_cp_get_die_temperature(unsigned int role, int *tdie)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_die_temperature))
		return -1;

	return temp_data->ops->cp_get_die_temperature(tdie, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_die_temperature);

int platform_class_cp_get_alarm_status(unsigned int role, int *alarm_status)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_alarm_status))
		return -1;

	return temp_data->ops->cp_get_alarm_status(alarm_status, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_alarm_status);

int platform_class_cp_get_fault_status(unsigned int role, int *fault_status)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_fault_status))
		return -1;

	return temp_data->ops->cp_get_fault_status(fault_status, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_fault_status);

int platform_class_cp_get_bus_error_status(unsigned int role, int *error_status)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_bus_error_status))
		return -1;

	return temp_data->ops->cp_get_bus_error_status(error_status, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_bus_error_status);

int platform_class_cp_get_reg_status(unsigned int role, int *reg_status)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_reg_status))
		return -1;

	return temp_data->ops->cp_get_reg_status(reg_status, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_reg_status);

int platform_class_cp_set_mode(unsigned int role, int mode)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_mode))
		return -1;

	return temp_data->ops->cp_set_mode(mode, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_mode);

int platform_class_cp_get_mode(unsigned int role, int *mode)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_mode))
		return -1;

	return temp_data->ops->cp_get_mode(mode, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_mode);

int platform_class_cp_device_init(unsigned int role, int value)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_device_init))
		return -1;

	return temp_data->ops->cp_device_init(value, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_device_init);

int platform_class_cp_enable_adc(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_adc))
		return -1;

	return temp_data->ops->cp_enable_adc(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_adc);

int platform_class_cp_get_bypass_support(unsigned int role, bool *status)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_bypass_support))
		return -1;

	return temp_data->ops->cp_get_bypass_support(status, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_bypass_support);

int platform_class_cp_dump_register(unsigned int role)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_dump_register))
		return -1;

	return temp_data->ops->cp_dump_register(temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_dump_register);

int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_chip_vendor))
		return -1;

	return temp_data->ops->cp_get_chip_vendor(chip_vendor, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_chip_vendor);

int platform_class_cp_get_int_stat(unsigned int role, int channel, bool *result)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_int_stat))
		return -1;

	return temp_data->ops->cp_get_int_stat(channel, result, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_int_stat);

int platform_class_cp_enable_acdrv_manual(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_acdrv_manual))
		return -1;

	return temp_data->ops->cp_enable_acdrv_manual(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_acdrv_manual);

int platform_class_cp_set_adjustadble_timeout(unsigned int role, int value)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_adjustadble_timeout))
		return -1;

	return temp_data->ops->cp_set_adjustadble_timeout(value, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_adjustadble_timeout);

int platform_class_cp_enable_wpcgate(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_wpcgate))
		return -1;

	return temp_data->ops->cp_enable_wpcgate(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_wpcgate);

int platform_class_cp_enable_ovpgate(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_ovpgate))
		return -1;

	return temp_data->ops->cp_enable_ovpgate(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_ovpgate);

int platform_class_cp_enable_ovpgate_with_check(unsigned int role, int type_temp, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_ovpgate_with_check))
		return -1;

	return temp_data->ops->cp_enable_ovpgate_with_check(type_temp, en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_ovpgate_with_check);

int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_ovpgate_status))
		return -1;

	return temp_data->ops->cp_get_ovpgate_status(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_ovpgate_status);

int platform_class_cp_get_probe_ok(unsigned int role)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_probe_status))
		return -1;

	return temp_data->ops->cp_get_probe_status(temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_probe_ok);

int platform_class_cp_get_usb_voltage(unsigned int role, int *vusb)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_usb_voltage))
		return -1;

	return temp_data->ops->cp_get_usb_voltage(vusb, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_usb_voltage);

int platform_class_cp_get_ibus_delta(int *val)
{
	int master_ibus = 0;
	int slave_ibus = 0;
	int ret = 0;

	ret = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &master_ibus);
	if (ret)
		master_ibus = 0;

	ret = platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &slave_ibus);
	if (ret)
		slave_ibus = 0;

	*val = abs(master_ibus - slave_ibus);
	return ret;

}
EXPORT_SYMBOL(platform_class_cp_get_ibus_delta);

int platform_class_cp_get_ibus_total(int *val)
{
	int master_ibus = 0;
	int slave_ibus = 0;
	int ret = 0;

	ret = platform_class_cp_get_bus_current(CP_ROLE_MASTER, &master_ibus);
	if (ret)
		master_ibus = 0;

	ret = platform_class_cp_get_bus_current(CP_ROLE_SLAVE, &slave_ibus);
	if (ret)
		slave_ibus = 0;


	*val = master_ibus + slave_ibus;
	return ret;

}
EXPORT_SYMBOL(platform_class_cp_get_ibus_total);

int platform_class_cp_get_errorhl_stat(unsigned int role, int *stat)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_errorhl_stat))
		return -1;

	return temp_data->ops->cp_get_errorhl_stat(stat, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_errorhl_stat);

int platform_class_cp_enable_busucp(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_enable_busucp))
		return -1;

	return temp_data->ops->cp_enable_busucp(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_enable_busucp);

#line 543 /* keep __LINE__ of the stock logs */
int platform_class_cp_set_fsw(unsigned int role, int fsw)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (g_platform_cp_dev) {
		if (g_platform_cp_dev->force_fsw > 0) {
			mca_log_info("force_fsw has set: %d\n", g_platform_cp_dev->force_fsw);
			return 0;
		} else if (g_platform_cp_dev->force_fsw < 0) {
			g_platform_cp_dev->force_fsw *= -1;
		}
	}

	if (platform_cp_ops_invalid(temp_data, cp_set_fsw))
		return -1;

	return temp_data->ops->cp_set_fsw(fsw, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_fsw);

int platform_class_cp_set_default_fsw(unsigned int role)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_default_fsw))
		return -1;

	return temp_data->ops->cp_set_default_fsw(temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_default_fsw);

int platform_class_cp_get_fsw(unsigned int role, int *fsw)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_fsw))
		return -1;

	return temp_data->ops->cp_get_fsw(fsw, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_fsw);

int platform_class_cp_get_fsw_step(unsigned int role, int *fsw_step)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_fsw_step))
		return -1;

	return temp_data->ops->cp_get_fsw_step(fsw_step, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_fsw_step);

int platform_class_cp_get_tdie(unsigned int role, int *tdie)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_tdie))
		return -1;

	return temp_data->ops->cp_get_tdie(tdie, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_tdie);

int platform_class_cp_set_qb(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_qb))
		return -1;

	return temp_data->ops->cp_set_qb(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_qb);

int platform_class_cp_set_rcp(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_rcp))
		return -1;

	return temp_data->ops->cp_set_rcp(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_rcp);

int platform_class_cp_set_pmid2outuvp_th(unsigned int role, int value)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_pmid2outuvp_th))
		return -1;

	return temp_data->ops->cp_set_pmid2outuvp_th(value, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_pmid2outuvp_th);

int platform_class_cp_set_revchg(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_revchg))
		return -1;

	return temp_data->ops->cp_set_revchg(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_revchg);

int platform_class_cp_set_busovp(unsigned int role, int value)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_busovp))
		return -1;

	return temp_data->ops->cp_set_busovp(value, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_busovp);

int platform_class_cp_set_pmid2out_uvp_dis(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_pmid2out_uvp_dis))
		return -1;

	return temp_data->ops->cp_set_pmid2out_uvp_dis(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_pmid2out_uvp_dis);

int platform_class_cp_get_pmid2out_uvp_dis(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_get_pmid2out_uvp_dis))
		return -1;

	return temp_data->ops->cp_get_pmid2out_uvp_dis(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_get_pmid2out_uvp_dis);

int platform_class_cp_set_manual_revchg_mode(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_manual_revchg_mode))
		return -1;

	return temp_data->ops->cp_set_manual_revchg_mode(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_manual_revchg_mode);

int platform_class_cp_set_cp_reverse_mode(unsigned int role, bool en)
{
	struct platform_cp_class_ops_data *temp_data = platform_cp_class_get_ic_ops(role);

	if (platform_cp_ops_invalid(temp_data, cp_set_cp_reverse_mode))
		return -1;

	return temp_data->ops->cp_set_cp_reverse_mode(en, temp_data->data);
}
EXPORT_SYMBOL(platform_class_cp_set_cp_reverse_mode);

#line 705 /* keep __LINE__ of the stock logs */
static int platform_cp_dev_parse_dt(struct platform_cp_dev *cp)
{
	struct device_node *node = cp->dev->of_node;
	int count = 0;
	int ret = 0;
	int i;

	if (!node) {
		mca_log_err("device tree info missing\n");
		return -1;
	}
	ret = mca_parse_dts_u32(node, "cp-num", &cp->cp_num, 1);
	if (ret) {
		mca_log_err("get cp-num fail\n");
		return ret;
	}

	count = mca_parse_dts_count_strings(node, "cp-dir-list", MCA_CP_MAX_NUM, 1);
	mca_log_err("cp dir list max count: %d, %d\n", count, cp->cp_num);

	if (count != cp->cp_num)
		mca_log_err("cp_num can't match cp_dir_list count\n");

	for (i = 0; i < count; i++) {
		ret = mca_parse_dts_string_index(node, "cp-dir-list", i, &(cp->cp_dir_list[i]));
		if (ret < 0) {
			mca_log_err("Unable to read cp-dir-list strings[%d]\n", i);
			return ret;
		}
	}

	mca_log_info("%s success\n", __func__);
	return ret;

}

static ssize_t cp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t cp_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info cp_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_CHIP_OK, chip_ok),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_VBUS, vbus),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_VUSB, vusb),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_IBUS, ibus),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_BATT_PRESENT, batt_present),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_BATT_TEMP, batt_temp),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_VPACK, vpack),
	mca_sysfs_attr_rw(cp_sysfs, 0664, CP_PROP_OVPGATE, ovpgate),
	mca_sysfs_attr_rw(cp_sysfs, 0664, CP_PROP_FSW, fsw),
	mca_sysfs_attr_ro(cp_sysfs, 0440, CP_PROP_TDIE, tdie),
};

#define CP_SYSFS_ATTRS_SIZE   ARRAY_SIZE(cp_sysfs_field_tbl)

static struct attribute *cp_sysfs_attrs[CP_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group cp_sysfs_attr_group = {
	.attrs = cp_sysfs_attrs,
};

static ssize_t cp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int *cp_index;
	union platform_cp_propval val;
	ssize_t count = 0;


	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		cp_sysfs_field_tbl, CP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	cp_index = (int *)dev_get_drvdata(dev);
	if (!cp_index) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}
	mca_log_err("%s dev_driverdata is %d\n", __func__, *cp_index);

	switch (attr_info->sysfs_attr_name) {
	case CP_PROP_CHIP_OK:
		platform_class_cp_get_present(*cp_index, &(val.boolval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.boolval);
		break;
	case CP_PROP_VBUS:
		platform_class_cp_enable_adc(*cp_index, true);
		platform_class_cp_get_bus_voltage(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case CP_PROP_VUSB:
		platform_class_cp_enable_adc(*cp_index, true);
		platform_class_cp_get_usb_voltage(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case CP_PROP_IBUS:
		platform_class_cp_enable_adc(*cp_index, true);
		platform_class_cp_get_bus_current(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case CP_PROP_BATT_PRESENT:
		platform_class_cp_get_battery_present(*cp_index, (bool *)&(val.uintval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.uintval);
		break;
	case CP_PROP_BATT_TEMP:
		platform_class_cp_get_battery_temperature(*cp_index, &(val.uintval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.uintval);
		break;
	case CP_PROP_VPACK:
		platform_class_cp_enable_adc(*cp_index, true);
		platform_class_cp_get_battery_voltage(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case CP_PROP_OVPGATE:
		platform_class_cp_get_ovpgate_status(*cp_index, &(val.boolval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.boolval);
		break;
	case CP_PROP_FSW:
		platform_class_cp_get_fsw(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	case CP_PROP_TDIE:
		platform_class_cp_get_tdie(*cp_index, &(val.intval));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val.intval);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t cp_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	int *cp_index;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		cp_sysfs_field_tbl, CP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	cp_index = (int *)dev_get_drvdata(dev);
	if (!cp_index) {
		mca_log_err("dev_driverdata is null\n");
		return -1;
	}

	mca_log_err("dev_driverdata is: %d, attr: %d, buf: %s\n",
		*cp_index, attr_info->sysfs_attr_name, buf);
	switch (attr_info->sysfs_attr_name) {
	case CP_PROP_OVPGATE:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_class_cp_enable_ovpgate(*cp_index, val);
		break;
	case CP_PROP_FSW:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		if (g_platform_cp_dev && val >= 0) {
			g_platform_cp_dev->force_fsw = -val;
			platform_class_cp_set_fsw(*cp_index, val);
		}
		break;
	default:
		break;
	}

	return count;
}

static const char * const cp_dev_list[MCA_CP_MAX_NUM] = {
	[CP_ROLE_MASTER] = "master",
	[CP_ROLE_SLAVE] = "slave",
	[CP_ROLE_THIRD] = "third",
};

#line 885 /* keep __LINE__ of the stock logs */
static void cp_sysfs_create_group(struct platform_cp_dev *cp)
{
	const char *cp_dev_name;
	int i;

	mca_sysfs_init_attrs(cp_sysfs_attrs, cp_sysfs_field_tbl,
		CP_SYSFS_ATTRS_SIZE);
	for (i = 0; i < cp->cp_num; i++) {
		if (i > MCA_CP_MAX_NUM) {
			mca_log_err("cp sysfsdev out of limit\n");
			return;
		}
		cp->sysfs_dev[i] = mca_sysfs_create_group("xm_power", cp->cp_dir_list[i],
			&cp_sysfs_attr_group);
		if (!cp->sysfs_dev[i])
			mca_log_err("creat cp[%d] sysfs fail\n", i);
	}

	for (i = 0; i < cp->cp_num; i++) {
		int j;

		cp_dev_name = dev_name(cp->sysfs_dev[i]);
		for (j = 0; j < MCA_CP_MAX_NUM; j++) {
			if (strstr(cp_dev_name, cp_dev_list[j])) {
				cp->cp_dev_index[j] = j;
				dev_set_drvdata(cp->sysfs_dev[i], &cp->cp_dev_index[j]);
				mca_log_err("success match cp_dev_name = %s, cp_dev_list[%d]=%s\n",
					cp_dev_name, j, cp_dev_list[j]);
				break;
			}
		}

		if (j >= MCA_CP_MAX_NUM) {
			dev_set_drvdata(cp->sysfs_dev[j], NULL);
			mca_log_err("fail match cp_dev_name =%s\n", cp_dev_name);
		}
	}

}

static ssize_t platform_cp_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t platform_cp_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info platform_cp_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(platform_cp_sysfs, 0440, CP_PROP_IBUS_DELTA, ibus_delta),
	mca_sysfs_attr_ro(platform_cp_sysfs, 0440, CP_RORP_IBUS_TOTAL, ibus_total),
	mca_sysfs_attr_rw(platform_cp_sysfs, 0664, CP_PROP_WORK_MODE, cp_mode),
};

#define PLATFORM_CP_SYSFS_ATTRS_SIZE   ARRAY_SIZE(platform_cp_sysfs_field_tbl)

static struct attribute *platform_cp_sysfs_attrs[PLATFORM_CP_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group platform_cp_sysfs_attr_group = {
	.attrs = platform_cp_sysfs_attrs,
};

static ssize_t platform_cp_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;
	ssize_t count = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		platform_cp_sysfs_field_tbl, PLATFORM_CP_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;


	switch (attr_info->sysfs_attr_name) {
	case CP_PROP_IBUS_DELTA:
		platform_class_cp_get_ibus_delta(&val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case CP_RORP_IBUS_TOTAL:
		platform_class_cp_get_ibus_total(&val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case CP_PROP_WORK_MODE:
		break;
	default:
		break;
	}
	return count;
}

static ssize_t platform_cp_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static int platform_cp_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(platform_cp_sysfs_attrs, platform_cp_sysfs_field_tbl,
		PLATFORM_CP_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "chargerpump",
	 dev, &platform_cp_sysfs_attr_group);
}


static int platform_cp_class_probe(struct platform_device *pdev)
{
	struct platform_cp_dev *l_dev;
	static int probe_cnt;
	int rc = 0;

	mca_log_err("%s begin cnt %d\n", __func__, ++probe_cnt);
	l_dev = devm_kzalloc(&pdev->dev, sizeof(*l_dev), GFP_KERNEL);
	if (!l_dev)
		return -ENOMEM;
	l_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, l_dev);
	rc = platform_cp_dev_parse_dt(l_dev);
	if (rc < 0) {
		mca_log_err("%s Couldn't parse device tree rc=%d\n", __func__, rc);
		return rc;
	}

	g_platform_cp_dev = l_dev;

	cp_sysfs_create_group(l_dev);
	platform_cp_sysfs_create_group(l_dev->dev);
	mca_log_err("%s success %d\n", __func__, ++probe_cnt);

	return 0;

}

static int platform_cp_class_remove(struct platform_device *pdev)
{
	return 0;
}

static void platform_cp_class_shutdown(struct platform_device *pdev)
{

}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,platform_cp"},
	{},
};


static struct platform_driver platform_cp_class_driver = {
	.driver	= {
		.name = "platform_cp_class",
		.of_match_table = match_table,
	},
	.probe = platform_cp_class_probe,
	.shutdown = platform_cp_class_shutdown,
	.remove  = platform_cp_class_remove,

};

module_platform_driver(platform_cp_class_driver);
MODULE_DESCRIPTION("platform cp class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

