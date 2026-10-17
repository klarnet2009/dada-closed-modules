// SPDX-License-Identifier: GPL-2.0
/*
 * mca_platform_buckchg_class.c
 *
 * mca platform buck charger class driver
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
#include <linux/platform_device.h>
#include <mca/platform/platform_buckchg_class.h>

struct platform_class_buckchg_data {
	void *data;
	struct platform_class_buckchg_ops *ops;
};

#define platform_class_buckchg_invalid_ops(data, name) (!data || !(data->ops) || !(data->ops->name))

static struct platform_class_buckchg_data platform_buckchg_ops_data[MAX_BUCK_CHARGER];

static struct platform_class_buckchg_data *platform_class_buckchg_get_ops_data(unsigned int role)
{
	if (role >= MAX_BUCK_CHARGER)
		return NULL;

	return &platform_buckchg_ops_data[role];
}

int platform_class_buckchg_ops_register(unsigned int role, void *data, struct platform_class_buckchg_ops *ops)
{
	if (role >= MAX_BUCK_CHARGER)
		return -1;

	platform_buckchg_ops_data[role].data = data;
	platform_buckchg_ops_data[role].ops = ops;

	return 0;
}
EXPORT_SYMBOL(platform_class_buckchg_ops_register);

int platform_class_buckchg_ops_enable_hvdcp(unsigned int role, int en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, enable_hvdcp))
		return -1;

	return temp_data->ops->enable_hvdcp(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_enable_hvdcp);

int platform_class_buckchg_ops_get_online(unsigned int role, int *online)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_online))
		return -1;

	return temp_data->ops->get_online(temp_data->data, online);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_online);

int platform_class_buckchg_ops_is_charge_done(unsigned int role, bool *charge_done)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, is_charge_done))
		return -1;

	return temp_data->ops->is_charge_done(temp_data->data, charge_done);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_is_charge_done);

int platform_class_buckchg_ops_get_hiz_status(unsigned int role, int *hz)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_hiz_status))
		return -1;

	return temp_data->ops->get_hiz_status(temp_data->data, hz);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_hiz_status);

int platform_class_buckchg_ops_get_input_volt_lmt(unsigned int role, int *mV)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_input_volt_lmt))
		return -1;

	return temp_data->ops->get_input_volt_lmt(temp_data->data, mV);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_input_volt_lmt);

int platform_class_buckchg_ops_get_input_curr_lmt(unsigned int role, int *mA)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_input_curr_lmt))
		return -1;

	return temp_data->ops->get_input_curr_lmt(temp_data->data, mA);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_input_curr_lmt);

int platform_class_buckchg_ops_get_bus_curr(unsigned int role, int *bus_curr)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_bus_curr))
		return -1;

	return temp_data->ops->get_bus_curr(temp_data->data, bus_curr);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_bus_curr);

int platform_class_buckchg_ops_get_bus_volt(unsigned int role, int *bus_volt)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_bus_volt))
		return -1;

	return temp_data->ops->get_bus_volt(temp_data->data, bus_volt);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_bus_volt);

int platform_class_buckchg_ops_get_usb_sns_volt(unsigned int role, int *bus_volt)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_bus_volt))
		return -1;

	return temp_data->ops->get_usb_sns_volt(temp_data->data, bus_volt);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_usb_sns_volt);

int platform_class_buckchg_ops_get_ac_volt(unsigned int role, int *mV)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_ac_volt))
		return -1;

	return temp_data->ops->get_ac_volt(temp_data->data, mV);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_ac_volt);

int platform_class_buckchg_ops_get_batt_volt_sns(unsigned int role, int *sns)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_batt_volt_sns))
		return -1;

	return temp_data->ops->get_batt_volt_sns(temp_data->data, sns);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_batt_volt_sns);

int platform_class_buckchg_ops_get_batt_volt(unsigned int role, int *mV)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_batt_volt))
		return -1;

	return temp_data->ops->get_batt_volt(temp_data->data, mV);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_batt_volt);

int platform_class_buckchg_ops_get_batt_curr(unsigned int role, int *mA)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_batt_curr))
		return -1;

	return temp_data->ops->get_batt_curr(temp_data->data, mA);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_batt_curr);

int platform_class_buckchg_ops_get_sys_volt(unsigned int role, int *vsys_min)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_sys_volt))
		return -1;

	return temp_data->ops->get_sys_volt(temp_data->data, vsys_min);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_sys_volt);

int platform_class_buckchg_ops_get_bus_tsns(unsigned int role, int *sns)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_bus_tsns))
		return -1;

	return temp_data->ops->get_bus_tsns(temp_data->data, sns);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_bus_tsns);

int platform_class_buckchg_ops_get_batt_tsns(unsigned int role, int *sns)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_batt_tsns))
		return -1;

	return temp_data->ops->get_batt_tsns(temp_data->data, sns);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_batt_tsns);

int platform_class_buckchg_ops_get_die_temp(unsigned int role, int *temp)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_die_temp))
		return -1;

	return temp_data->ops->get_die_temp(temp_data->data, temp);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_die_temp);

int platform_class_buckchg_ops_get_batt_id(unsigned int role, int *id)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_batt_id))
		return -1;

	return temp_data->ops->get_batt_id(temp_data->data, id);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_batt_id);

int platform_class_buckchg_ops_get_chg_status(unsigned int role, int *chg_status)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_chg_status))
		return -1;

	return temp_data->ops->get_chg_status(temp_data->data, chg_status);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_chg_status);

int platform_class_buckchg_ops_get_chg_type(unsigned int role, int *chg_type)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_chg_type))
		return -1;

	return temp_data->ops->get_chg_type(temp_data->data, chg_type);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_chg_type);

int platform_class_buckchg_ops_get_term_curr(unsigned int role, int *term_curr)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_term_curr))
		return -1;

	return temp_data->ops->get_term_curr(temp_data->data, term_curr);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_term_curr);

int platform_class_buckchg_ops_get_term_volt(unsigned int role, int *term_volt)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_term_volt))
		return -1;

	return temp_data->ops->get_term_volt(temp_data->data, term_volt);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_term_volt);

int platform_class_buckchg_ops_get_wls_curr(unsigned int role, int *wls_curr)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_wls_curr))
		return -1;

	return temp_data->ops->get_wls_curr(temp_data->data, wls_curr);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_wls_curr);

int platform_class_buckchg_ops_set_hiz(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_hiz))
		return -1;

	return temp_data->ops->set_hiz(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_hiz);

int platform_class_buckchg_ops_set_wls_hiz(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_wls_hiz))
		return -1;

	return temp_data->ops->set_wls_hiz(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_wls_hiz);

int platform_class_buckchg_ops_set_input_curr_lmt(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_input_curr_lmt))
		return -1;

	return temp_data->ops->set_input_curr_lmt(temp_data->data, ma);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_input_curr_lmt);

int platform_class_buckchg_ops_set_wls_input_curr_lmt(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_wls_input_curr_lmt))
		return -1;

	return temp_data->ops->set_wls_input_curr_lmt(temp_data->data, ma);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_wls_input_curr_lmt);

int platform_class_buckchg_ops_set_input_volt_lmt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_input_volt_lmt))
		return -1;

	return temp_data->ops->set_input_volt_lmt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_input_volt_lmt);

int platform_class_buckchg_ops_set_ichg(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_ichg))
		return -1;

	return temp_data->ops->set_ichg(temp_data->data, ma);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_ichg);

int platform_class_buckchg_ops_set_chg(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_chg))
		return -1;

	return temp_data->ops->set_chg(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_chg);

int platform_class_buckchg_ops_set_buck_fsw(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_buck_fsw))
		return -1;

	return temp_data->ops->set_buck_fsw(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_buck_fsw);

int platform_class_buckchg_ops_set_otg_curr(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_otg_curr))
		return -1;

	return temp_data->ops->set_otg_curr(temp_data->data, ma);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_otg_curr);

int platform_class_buckchg_ops_set_otg_volt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_otg_volt))
		return -1;

	return temp_data->ops->set_otg_volt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_otg_volt);

int platform_class_buckchg_ops_set_term(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_term))
		return -1;

	return temp_data->ops->set_term(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_term);

int platform_class_buckchg_ops_set_term_curr(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_term_curr))
		return -1;

	return temp_data->ops->set_term_curr(temp_data->data, ma);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_term_curr);

int platform_class_buckchg_ops_set_term_volt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_term_volt))
		return -1;

	return temp_data->ops->set_term_volt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_term_volt);

int platform_class_buckchg_ops_adc_enable(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, adc_enable))
		return -1;

	return temp_data->ops->adc_enable(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_adc_enable);

int platform_class_buckchg_ops_set_prechg_volt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_prechg_volt))
		return -1;

	return temp_data->ops->set_prechg_volt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_prechg_volt);

int platform_class_buckchg_ops_set_prechg_curr(unsigned int role, int ma)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_prechg_curr))
		return -1;

	return temp_data->ops->set_prechg_curr(temp_data->data, ma);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_prechg_curr);

int platform_class_buckchg_ops_force_dpdm(unsigned int role, int en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, force_dpdm))
		return -1;

	return temp_data->ops->force_dpdm(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_force_dpdm);

int platform_class_buckchg_ops_request_dpdm(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, request_dpdm))
		return -1;

	return temp_data->ops->request_dpdm(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_request_dpdm);

int platform_class_buckchg_ops_set_wd_timeout(unsigned int role, int ms)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_wd_timeout))
		return -1;

	return temp_data->ops->set_wd_timeout(temp_data->data, ms);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_wd_timeout);

int platform_class_buckchg_ops_kick_wd(unsigned int role)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, kick_wd))
		return -1;

	return temp_data->ops->kick_wd(temp_data->data);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_kick_wd);

int platform_class_buckchg_ops_set_qc_volt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_qc_volt))
		return -1;

	return temp_data->ops->set_qc_volt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_qc_volt);

int platform_class_buckchg_ops_set_usb_aicl_cont_thd(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_usb_aicl_cont_thd))
		return -1;

	return temp_data->ops->set_usb_aicl_cont_thd(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_usb_aicl_cont_thd);

int platform_class_buckchg_ops_get_usb_aicl_cont_thd(unsigned int role, int *mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_usb_aicl_cont_thd))
		return -1;

	return temp_data->ops->get_usb_aicl_cont_thd(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_usb_aicl_cont_thd);

int platform_class_buckchg_ops_set_opt_fws(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_opt_fws))
		return -1;

	return temp_data->ops->set_opt_fws(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_opt_fws);

int platform_class_buckchg_ops_usb_adapter_allow_override(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, usb_adapter_allow_override))
		return -1;

	return temp_data->ops->usb_adapter_allow_override(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_usb_adapter_allow_override);

int platform_class_buckchg_ops_set_qc3_volt(unsigned int role, int mv)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_qc3_volt))
		return -1;

	return temp_data->ops->set_qc3_volt(temp_data->data, mv);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_qc3_volt);

int platform_class_buckchg_ops_get_otg_boost_src(unsigned int role, int *otg_boost_src)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_otg_boost_src))
		return -1;

	return temp_data->ops->get_otg_boost_src(temp_data->data, otg_boost_src);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_otg_boost_src);

int platform_class_buckchg_ops_get_otg_boost_enable_status(unsigned int role, int *otg_boost_enable_sts)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_otg_boost_enable_status))
		return -1;

	return temp_data->ops->get_otg_boost_enable_status(temp_data->data, otg_boost_enable_sts);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_otg_boost_enable_status);

int platform_class_buckchg_ops_get_otg_gate_enable_status(unsigned int role, int *otg_gate_enable_sts)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_otg_gate_enable_status))
		return -1;

	return temp_data->ops->get_otg_gate_enable_status(temp_data->data, otg_gate_enable_sts);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_otg_gate_enable_status);

int platform_class_buckchg_ops_set_boost_enable(unsigned int role, int src_enable)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_boost_enable))
		return -1;

	return temp_data->ops->set_boost_enable(temp_data->data, src_enable);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_boost_enable);

int platform_class_buckchg_ops_set_boost_voltage(unsigned int role, int src_value)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_boost_voltage))
		return -1;

	return temp_data->ops->set_boost_voltage(temp_data->data, src_value);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_boost_voltage);

int platform_class_buckchg_ops_set_aicl_enable(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_aicl_enable))
		return -1;

	return temp_data->ops->set_aicl_enable(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_aicl_enable);

int platform_class_buckchg_ops_set_rerun_aicl(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_rerun_aicl))
		return -1;

	return temp_data->ops->set_rerun_aicl(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_rerun_aicl);

int platform_class_buckchg_ops_is_support_cid(unsigned int role, bool *en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, is_support_cid))
		return -1;

	return temp_data->ops->is_support_cid(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_is_support_cid);

int platform_class_buckchg_ops_set_ship_mode(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_ship_mode))
		return -1;

	return temp_data->ops->set_ship_mode(temp_data->data, en);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_ship_mode);

int platform_class_buckchg_ops_get_ship_mode(unsigned int role, bool *en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_ship_mode))
		return -1;

	return temp_data->ops->get_ship_mode(temp_data->data, en);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_ship_mode);

int platform_class_buckchg_ops_set_wls_vdd_flag(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_wls_vdd_flag))
		return -1;

	return temp_data->ops->set_wls_vdd_flag(temp_data->data, en);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_wls_vdd_flag);
int platform_class_buckchg_ops_get_lpd_enable(unsigned int role, int *lpd_en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_enable))
		return -1;

	return temp_data->ops->get_lpd_enable(temp_data->data, lpd_en);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_enable);

int platform_class_buckchg_ops_get_lpd_status(unsigned int role, int *lpd_status)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_status))
		return -1;

	return temp_data->ops->get_lpd_status(temp_data->data, lpd_status);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_status);

int platform_class_buckchg_ops_get_lpd_sbu1(unsigned int role, int *lpd_sbu1)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_sbu1))
		return -1;

	return temp_data->ops->get_lpd_sbu1(temp_data->data, lpd_sbu1);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_sbu1);

int platform_class_buckchg_ops_get_lpd_sbu2(unsigned int role, int *lpd_sbu2)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_sbu2))
		return -1;

	return temp_data->ops->get_lpd_sbu2(temp_data->data, lpd_sbu2);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_sbu2);

int platform_class_buckchg_ops_get_lpd_cc1(unsigned int role, int *lpd_cc1)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_cc1))
		return -1;

	return temp_data->ops->get_lpd_cc1(temp_data->data, lpd_cc1);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_cc1);

int platform_class_buckchg_ops_get_lpd_cc2(unsigned int role, int *lpd_cc2)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_cc2))
		return -1;

	return temp_data->ops->get_lpd_cc2(temp_data->data, lpd_cc2);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_cc2);

int platform_class_buckchg_ops_get_lpd_dp(unsigned int role, int *lpd_dp)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_dp))
		return -1;

	return temp_data->ops->get_lpd_dp(temp_data->data, lpd_dp);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_dp);

int platform_class_buckchg_ops_get_lpd_dm(unsigned int role, int *lpd_dm)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_dm))
		return -1;

	return temp_data->ops->get_lpd_dm(temp_data->data, lpd_dm);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_dm);

int platform_class_buckchg_ops_set_lpd_sbu1(unsigned int role, int lpd_sbu1)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_lpd_sbu1))
		return -1;

	return temp_data->ops->set_lpd_sbu1(temp_data->data, lpd_sbu1);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_lpd_sbu1);

int platform_class_buckchg_ops_set_lpd_control(unsigned int role, int lpd_control)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_lpd_control))
		return -1;

	return temp_data->ops->set_lpd_control(temp_data->data, lpd_control);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_lpd_control);

int platform_class_buckchg_ops_get_lpd_control(unsigned int role, int *lpd_control)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_control))
		return -1;

	return temp_data->ops->get_lpd_control(temp_data->data, lpd_control);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_control);

int platform_class_buckchg_ops_set_lpd_uart_control(unsigned int role, int lpd_uart_control)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_lpd_uart_control))
		return -1;

	return temp_data->ops->set_lpd_uart_control(temp_data->data, lpd_uart_control);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_lpd_uart_control);

int platform_class_buckchg_ops_get_lpd_uart_control(unsigned int role, int *lpd_uart_control)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_lpd_uart_control))
		return -1;

	return temp_data->ops->get_lpd_uart_control(temp_data->data, lpd_uart_control);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_lpd_uart_control);

int platform_class_buckchg_ops_get_pack_vbat(unsigned int role, int *pvbat)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_pack_vbat))
		return -1;

	return temp_data->ops->get_pack_vbat(temp_data->data, pvbat);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_vbat);

int platform_class_buckchg_ops_set_eu_model(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_eu_model))
		return -1;

	return temp_data->ops->set_eu_model(temp_data->data, en);

}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_eu_model);

int platform_class_buckchg_ops_is_init_ok(unsigned int role)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, is_init_ok))
		return -1;

	return temp_data->ops->is_init_ok(temp_data->data);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_is_init_ok);

int platform_class_buckchg_ops_set_otg(unsigned int role, bool otg_en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_otg))
		return -1;

	return temp_data->ops->set_otg(temp_data->data, otg_en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_otg);

int platform_class_buckchg_ops_get_pack_ibat(unsigned int role, int *pibat)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_pack_ibat))
		return -1;

	return temp_data->ops->get_pack_ibat(temp_data->data, pibat);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_ibat);

int platform_class_buckchg_ops_get_pack_tbat(unsigned int role, int *ptbat)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_pack_tbat))
		return -1;

	return temp_data->ops->get_pack_tbat(temp_data->data, ptbat);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_pack_tbat);

int platform_class_buckchg_ops_get_aicl_status(unsigned int role, int *aicl_status)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, get_aicl_status))
		return -1;

	return temp_data->ops->get_aicl_status(temp_data->data, aicl_status);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_get_aicl_status);

int platform_class_buckchg_ops_set_restart_aicl(unsigned int role, bool en)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_restart_aicl))
		return -1;

	return temp_data->ops->set_restart_aicl(temp_data->data, en);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_restart_aicl);

int platform_class_buckchg_ops_set_too_hot_limit(unsigned int role, int limit)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_too_hot_limit))
		return -1;

	return temp_data->ops->set_too_hot_limit(temp_data->data, limit);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_too_hot_limit);

/* OS4 addition (op #83 of struct platform_class_buckchg_ops, offset 656); prototype local, the stubs_g5 header only has the op */
int platform_class_buckchg_ops_set_cp_vendor_id(unsigned int role, int *val)
{
	struct platform_class_buckchg_data *temp_data = platform_class_buckchg_get_ops_data(role);

	if (platform_class_buckchg_invalid_ops(temp_data, set_cp_vendor_id))
		return -1;

	return temp_data->ops->set_cp_vendor_id(temp_data->data, val);
}
EXPORT_SYMBOL(platform_class_buckchg_ops_set_cp_vendor_id);

static struct platform_driver platform_class_buckchg_driver = {
	.driver	= {
		.name = "platform_class_buckchg",
	},
};

module_platform_driver(platform_class_buckchg_driver);
MODULE_DESCRIPTION("platform buckchg class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

