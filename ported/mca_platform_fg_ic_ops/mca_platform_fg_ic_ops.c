// SPDX-License-Identifier: GPL-2.0
/*
 * mca_platform_fg_ic_ops.c
 *
 * mca platform fuelgauge ic operation driver
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
#include "platform_fg_ic_ops_os4.h"
#include <mca/common/mca_log.h>

#define platform_fg_ops_invalid(data, name) (!data || !data->ops || !data->ops->name)

struct fuelguage_info {
	void *data;
	struct fuelguage_ic_ops *ops;
};
static struct fuelguage_info g_fg_ic_ops[FG_IC_MAX];

static inline struct fuelguage_info *platform_get_fg_ic_ops(unsigned int ic_role)
{
	if (ic_role >= FG_IC_MAX || g_fg_ic_ops[ic_role].ops == NULL)
		return NULL;

	return &g_fg_ic_ops[ic_role];
}

int platform_fg_ic_ops_register(unsigned int ic_role, void *data, struct fuelguage_ic_ops *platform_fg_ops)
{
	if (ic_role >= FG_IC_MAX || data == NULL || platform_fg_ops  == NULL)
		return -EOPNOTSUPP;

	g_fg_ic_ops[ic_role].data = data;
	g_fg_ic_ops[ic_role].ops = platform_fg_ops;

	return 0;
}
EXPORT_SYMBOL(platform_fg_ic_ops_register);

int platform_fg_ops_probe_ok(unsigned int ic_role, bool *ok)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_probe_ok))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_probe_ok(temp_info->data, ok);
}
EXPORT_SYMBOL(platform_fg_ops_probe_ok);

int platform_fg_ops_get_batt_info(unsigned int ic_role, void *info)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_batt_info))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_batt_info(temp_info->data, info);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_info);

void platform_fg_ops_get_batt_abnormal_info(unsigned int ic_role, void *info)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_batt_abnormal_info))
		return;

	temp_info->ops->fg_ic_get_batt_abnormal_info(temp_info->data, info);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_abnormal_info);

int platform_fg_ops_get_soc(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_soc))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_soc(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_get_soc);

int platform_fg_ops_get_rsoc(unsigned int ic_role, int *rsoc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_rsoc))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_rsoc(temp_info->data, rsoc);
}
EXPORT_SYMBOL(platform_fg_ops_get_rsoc);

int platform_fg_ops_get_curr(unsigned int ic_role, int *curr)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_curr))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_curr(temp_info->data, curr);
}
EXPORT_SYMBOL(platform_fg_ops_get_curr);

int platform_fg_ops_set_verify_digest(unsigned int ic_role, char *buf)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_verify_digest))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_verify_digest(temp_info->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_set_verify_digest);

int platform_fg_ops_get_verify_digest(unsigned int ic_role, char *buf)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_verify_digest))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_verify_digest(temp_info->data, buf);
}
EXPORT_SYMBOL(platform_fg_ops_get_verify_digest);

int platform_fg_ops_set_authentic(unsigned int ic_role, int value)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_authentic))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_authentic(temp_info->data, value);
}
EXPORT_SYMBOL(platform_fg_ops_set_authentic);

int platform_fg_ops_get_authentic(unsigned int ic_role, int *value)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_authentic))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_authentic(temp_info->data, value);
}
EXPORT_SYMBOL(platform_fg_ops_get_authentic);

int platform_fg_ops_get_error_state(unsigned int ic_role, bool *error)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_error_state))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_error_state(temp_info->data, error);
}
EXPORT_SYMBOL(platform_fg_ops_get_error_state);

int platform_fg_ops_get_volt(unsigned int ic_role, int *volt)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_volt))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_volt(temp_info->data, volt);
}
EXPORT_SYMBOL(platform_fg_ops_get_volt);

int platform_fg_ops_set_temp(unsigned int ic_role, int temp)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_temp))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_temp(temp_info->data, temp);
}
EXPORT_SYMBOL(platform_fg_ops_set_temp);

int platform_fg_ops_get_temp(unsigned int ic_role, int *temp)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_temp))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_temp(temp_info->data, temp);
}
EXPORT_SYMBOL(platform_fg_ops_get_temp);

int platform_fg_ops_get_original_temp(unsigned int ic_role, int *original_temp)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_original_temp))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_original_temp(temp_info->data, original_temp);
}
EXPORT_SYMBOL(platform_fg_ops_get_original_temp);

int platform_fg_ops_set_iterm(unsigned int ic_role, int curr)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_iterm))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_iterm(temp_info->data, curr);
}
EXPORT_SYMBOL(platform_fg_ops_set_iterm);

int platform_fg_ops_get_charge_status(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_charge_status))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_charge_status(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_get_charge_status);

int platform_fg_ops_get_rm(unsigned int ic_role, int *rm)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_rm))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_rm(temp_info->data, rm);
}
EXPORT_SYMBOL(platform_fg_ops_get_rm);

int platform_fg_ops_get_isc_alert_level(unsigned int ic_role, int *level)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_isc_alert_level))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_isc_alert_level(temp_info->data, level);
}
EXPORT_SYMBOL(platform_fg_ops_get_isc_alert_level);

int platform_fg_ops_get_soa_alert_level(unsigned int ic_role, int *level)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_soa_alert_level))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_soa_alert_level(temp_info->data, level);
}
EXPORT_SYMBOL(platform_fg_ops_get_soa_alert_level);

int platform_fg_ops_get_fastcharge(unsigned int ic_role, int *ffc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fastcharge))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fastcharge(temp_info->data, ffc);
}
EXPORT_SYMBOL(platform_fg_ops_get_fastcharge);

int platform_fg_ops_set_fastcharge(unsigned int ic_role, bool en)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_fastcharge))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_fastcharge(temp_info->data, en);
}
EXPORT_SYMBOL(platform_fg_ops_set_fastcharge);

int platform_fg_ops_get_chg_vol(unsigned int ic_role, int *volt)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_chg_vol))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_chg_vol(temp_info->data, volt);
}
EXPORT_SYMBOL(platform_fg_ops_get_chg_vol);

int platform_fg_ops_get_chip_ok(unsigned int ic_role, int *ok)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_chip_ok))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_chip_ok(temp_info->data, ok);
}
EXPORT_SYMBOL(platform_fg_ops_get_chip_ok);

int platform_fg_ops_get_cyclecount(unsigned int ic_role, int *cc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_cyclecount))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_cyclecount(temp_info->data, cc);
}
EXPORT_SYMBOL(platform_fg_ops_get_cyclecount);

int platform_fg_ops_get_tte(unsigned int ic_role, int *tte)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_tte))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_tte(temp_info->data, tte);
}
EXPORT_SYMBOL(platform_fg_ops_get_tte);

int platform_fg_ops_get_ttf(unsigned int ic_role, int *ttf)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_ttf))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_ttf(temp_info->data, ttf);
}
EXPORT_SYMBOL(platform_fg_ops_get_ttf);

int platform_fg_ops_get_fcc(unsigned int ic_role, int *fcc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fcc))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fcc(temp_info->data, fcc);
}
EXPORT_SYMBOL(platform_fg_ops_get_fcc);

int platform_fg_ops_get_fcc_default(unsigned int ic_role, bool *fcc_default)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fcc_default))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fcc_default(temp_info->data, fcc_default);
}
EXPORT_SYMBOL(platform_fg_ops_get_fcc_default);

int platform_fg_ops_get_full_design(unsigned int ic_role, int *dc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_full_design))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_full_design(temp_info->data, dc);
}
EXPORT_SYMBOL(platform_fg_ops_get_full_design);

int platform_fg_ops_get_decimal_rate(unsigned int ic_role, int *rate)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_decimal_rate))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_decimal_rate(temp_info->data, rate);
}
EXPORT_SYMBOL(platform_fg_ops_get_decimal_rate);

int platform_fg_ops_get_decimal(unsigned int ic_role, int *decimal)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_decimal))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_decimal(temp_info->data, decimal);
}
EXPORT_SYMBOL(platform_fg_ops_get_decimal);

int platform_fg_ops_get_soh(unsigned int ic_role, int *soh)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_soh))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_soh(temp_info->data, soh);
}
EXPORT_SYMBOL(platform_fg_ops_get_soh);

int platform_fg_ops_get_temp_max(unsigned int ic_role, int *temp_max)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_temp_max))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_temp_max(temp_info->data, temp_max);
}
EXPORT_SYMBOL(platform_fg_ops_get_temp_max);

int platform_fg_ops_get_time_ot(unsigned int ic_role, int *time_ot)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_time_ot))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_time_ot(temp_info->data, time_ot);
}
EXPORT_SYMBOL(platform_fg_ops_get_time_ot);

int platform_fg_ops_get_batt_cell_info(unsigned int ic_role, const char **name)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_batt_cell_info))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_batt_cell_info(temp_info->data, name);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_cell_info);

int platform_fg_ops_get_cutoff_voltage(unsigned int ic_role, int *volt)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_cutoff_voltage))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_cutoff_voltage(temp_info->data, volt);
}
EXPORT_SYMBOL(platform_fg_ops_get_cutoff_voltage);

int platform_fg_ops_set_cutoff_voltage(unsigned int ic_role, int value)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_cutoff_voltage))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_cutoff_voltage(temp_info->data, value);
}
EXPORT_SYMBOL(platform_fg_ops_set_cutoff_voltage);

int platform_fg_ops_get_dod_count(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_dod_count))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_dod_count(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_get_dod_count);

int platform_fg_ops_get_count_level1(unsigned int ic_role, int *count)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_count_level1))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_count_level1(temp_info->data, count);
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level1);

int platform_fg_ops_get_count_level2(unsigned int ic_role, int *count)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_count_level1))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_count_level2(temp_info->data, count);
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level2);

int platform_fg_ops_get_count_level3(unsigned int ic_role, int *count)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_count_level3))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_count_level3(temp_info->data, count);
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level3);

int platform_fg_ops_get_vsiloss(unsigned int ic_role, int *vsiloss)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_vsiloss))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_vsiloss(temp_info->data, vsiloss);
}
EXPORT_SYMBOL(platform_fg_ops_get_vsiloss);

int platform_fg_ops_get_iaver(unsigned int ic_role, int *iaver)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_iaver))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_iaver(temp_info->data, iaver);
}
EXPORT_SYMBOL(platform_fg_ops_get_iaver);

int platform_fg_ops_get_count_lowtemp(unsigned int ic_role, int *count)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_count_lowtemp))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_count_lowtemp(temp_info->data, count);
}
EXPORT_SYMBOL(platform_fg_ops_get_count_lowtemp);

int platform_fg_ops_set_clear_count_data(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_clear_count_data))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_clear_count_data(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_set_clear_count_data);

int platform_fg_ops_get_adapt_power(unsigned int ic_role, int *adapt_power)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_adapt_power))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_adapt_power(temp_info->data, adapt_power);
}
EXPORT_SYMBOL(platform_fg_ops_get_adapt_power);

int platform_fg_ops_get_aged_flag(unsigned int ic_role, int *flag)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_aged_flag))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_aged_flag(temp_info->data, flag);
}
EXPORT_SYMBOL(platform_fg_ops_get_aged_flag);

int platform_fg_ops_get_raw_soc(unsigned int ic_role, int *raw_soc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_raw_soc))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_raw_soc(temp_info->data, raw_soc);
}
EXPORT_SYMBOL(platform_fg_ops_get_raw_soc);

int platform_fg_ops_get_real_supplement_energy(unsigned int ic_role, int *supplement_energy)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_real_supplement_energy))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_real_supplement_energy(temp_info->data, supplement_energy);
}
EXPORT_SYMBOL(platform_fg_ops_get_real_supplement_energy);

int platform_fg_ops_get_calibration_ffc_iterm(unsigned int ic_role, int *iterm)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_calibration_ffc_iterm))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_calibration_ffc_iterm(temp_info->data, iterm);
}
EXPORT_SYMBOL(platform_fg_ops_get_calibration_ffc_iterm);

int platform_fg_ops_get_calibration_charge_energy(unsigned int ic_role, int *charge_energy)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_calibration_charge_energy))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_calibration_charge_energy(temp_info->data, charge_energy);
}
EXPORT_SYMBOL(platform_fg_ops_get_calibration_charge_energy);

void platform_fg_ops_fl4p0_enable_check(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_fl4p0_enable_check))
		return;

	temp_info->ops->fg_ic_fl4p0_enable_check(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_fl4p0_enable_check);

void platform_fg_ops_update_fw(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_update_fw))
		return;

	temp_info->ops->fg_ic_update_fw(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_update_fw);

int platform_fg_ops_get_device_name(unsigned int ic_role, const char **name)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_device_name))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_device_name(temp_info->data, name);
}
EXPORT_SYMBOL(platform_fg_ops_get_device_name);

int platform_fg_ops_get_temp_min(unsigned int ic_role, int *temp_min)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_temp_min))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_temp_min(temp_info->data, temp_min);
}
EXPORT_SYMBOL(platform_fg_ops_get_temp_min);

void platform_fg_ops_set_force_report_full(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_force_report_full))
		return;

	temp_info->ops->fg_ic_set_force_report_full(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_set_force_report_full);

int platform_fg_ops_get_fc(unsigned int ic_role, bool *fc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fc))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fc(temp_info->data, fc);
}
EXPORT_SYMBOL(platform_fg_ops_get_fc);

int platform_fg_ops_set_co(unsigned int ic_role, bool value)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_co))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_co(temp_info->data, value);
}
EXPORT_SYMBOL(platform_fg_ops_set_co);

void platform_fg_ops_get_ui_soh(unsigned int ic_role, int *ui_soh)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_ui_soh))
		return;

	temp_info->ops->fg_ic_get_ui_soh(temp_info->data, ui_soh);
}
EXPORT_SYMBOL(platform_fg_ops_get_ui_soh);

void platform_fg_ops_qbg_send_chg_data(unsigned int ic_role, void *chg_data)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_qbg_send_chg_data))
		return;

	temp_info->ops->fg_ic_qbg_send_chg_data(temp_info->data, chg_data);
}
EXPORT_SYMBOL(platform_fg_ops_qbg_send_chg_data);

void platform_fg_ops_get_pack_vendor(unsigned int ic_role, int *pack_vendor_id)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_pack_vendor))
		return;

	temp_info->ops->fg_ic_get_pack_vendor(temp_info->data, pack_vendor_id);
}
EXPORT_SYMBOL(platform_fg_ops_get_pack_vendor);

unsigned long platform_fg_ops_get_calc_rvalue(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_calc_rvalue))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_calc_rvalue(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_get_calc_rvalue);

void platform_fg_ops_get_average_current(unsigned int ic_role, int *avg_curr)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_average_current))
		return;

	temp_info->ops->fg_ic_get_average_current(temp_info->data, avg_curr);
}
EXPORT_SYMBOL(platform_fg_ops_get_average_current);

void platform_fg_ops_get_ota_update_flag(unsigned int ic_role, unsigned int *flag)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_ota_update_flag))
		return;

	temp_info->ops->fg_ic_get_ota_update_flag(temp_info->data, flag);
}
EXPORT_SYMBOL(platform_fg_ops_get_ota_update_flag);

void platform_fg_ops_ota_update_check(unsigned int ic_role)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_ota_update_check))
		return;

	temp_info->ops->fg_ic_ota_update_check(temp_info->data);
}
EXPORT_SYMBOL(platform_fg_ops_ota_update_check);

int platform_fg_ops_get_manufacturing_date(unsigned int ic_role, unsigned char *manufacturing_date)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_manufacturing_date))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_manufacturing_date(temp_info->data, manufacturing_date);
}
EXPORT_SYMBOL(platform_fg_ops_get_manufacturing_date);

void platform_fg_ops_set_first_usage_date(unsigned int ic_role, const char *first_usage_date)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_first_usage_date))
		return;

	temp_info->ops->fg_ic_set_first_usage_date(temp_info->data, first_usage_date);
}
EXPORT_SYMBOL(platform_fg_ops_set_first_usage_date);

int platform_fg_ops_get_first_usage_date(unsigned int ic_role, unsigned char *first_usage_date)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_first_usage_date))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_first_usage_date(temp_info->data, first_usage_date);
}
EXPORT_SYMBOL(platform_fg_ops_get_first_usage_date);

int platform_fg_ops_get_co_status(unsigned int ic_role, int *co_status)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_read_one_co))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_read_one_co(temp_info->data, co_status);
}
EXPORT_SYMBOL(platform_fg_ops_get_co_status);

int platform_fg_ops_get_fg_vendor(unsigned int ic_role, int *fg_vendor)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fg_vendor))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fg_vendor(temp_info->data, fg_vendor);
}
EXPORT_SYMBOL(platform_fg_ops_get_fg_vendor);

int platform_fg_ops_get_csd_flag(unsigned int ic_role, int *csd_flag)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_csd_flag))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_csd_flag(temp_info->data, csd_flag);
}
EXPORT_SYMBOL(platform_fg_ops_get_csd_flag);

int platform_fg_ops_get_batt_qmax(unsigned int ic_role, int *qmax)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_batt_qmax))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_batt_qmax(temp_info->data, qmax);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_qmax);

int platform_fg_ops_get_fast_mode_times(unsigned int ic_role, int *fast_mode_times)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_fast_mode_times))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_fast_mode_times(temp_info->data, fast_mode_times);
}
EXPORT_SYMBOL(platform_fg_ops_get_fast_mode_times);

int platform_fg_ops_set_fast_mode_times(unsigned int ic_role, int fast_mode_times)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_fast_mode_times))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_fast_mode_times(temp_info->data, fast_mode_times);
}
EXPORT_SYMBOL(platform_fg_ops_set_fast_mode_times);

int platform_fg_ops_get_cc_coulomb_integral(unsigned int ic_role, int *cc)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_cc_coulomb_integral))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_cc_coulomb_integral(temp_info->data, cc);
}
EXPORT_SYMBOL(platform_fg_ops_get_cc_coulomb_integral);

int platform_fg_ops_get_batt_sn(unsigned int ic_role, char *batt_sn)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_get_batt_sn))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_get_batt_sn(temp_info->data, batt_sn);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_sn);

int platform_fg_ops_set_dynamic_fv(unsigned int ic_role, int fv)
{
	struct fuelguage_info *temp_info = platform_get_fg_ic_ops(ic_role);

	if (platform_fg_ops_invalid(temp_info, fg_ic_set_dynamic_fv))
		return -EOPNOTSUPP;

	return temp_info->ops->fg_ic_set_dynamic_fv(temp_info->data, fv);
}
EXPORT_SYMBOL(platform_fg_ops_set_dynamic_fv);

static struct platform_driver platform_fg_ops_driver = {
	.driver	= {
		.name = "platform_fg_ops",
	},
};

module_platform_driver(platform_fg_ops_driver);
MODULE_DESCRIPTION("platform fg ops");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

