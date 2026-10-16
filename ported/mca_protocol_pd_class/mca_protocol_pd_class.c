// SPDX-License-Identifier: GPL-2.0
/*
 * mca_protocol_pd_class.c
 *
 * mca pd protocol class driver
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
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_parse_dts.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "protocol_pd_class"
#endif

#define protocol_class_pd_invalid_ops(temp, name) (!temp || !temp->ops || !temp->ops->name)
#define MCA_PPS_MAX_VOL_10V 10000

enum mca_apdo_max {
	MCA_APDO_MAX_30W = 30,
	MCA_APDO_MAX_33W = 33,
	MCA_APDO_MAX_40W = 40,
	MCA_APDO_MAX_50W = 50,
	MCA_APDO_MAX_55W = 55,
	MCA_APDO_MAX_65W = 65,
	MCA_APDO_MAX_67W = 67,
	MCA_APDO_MAX_90W = 90,
	MCA_APDO_MAX_100W = 100,
	MCA_APDO_MAX_120W = 120,
	MCA_APDO_MAX_INVALID = 67,
};

struct protocol_class_pd_ops_data {
	void *data;
	struct protocol_class_pd_ops *ops;
};

static struct protocol_class_pd_ops_data g_protocol_pd_data[TYPEC_PORT_MAX];
static unsigned int g_cur_port;
static struct notifier_block g_pd_class_ntf;
static int device_max_power;

static int protocol_class_pps_get_pwr_cap(void *data, struct adapter_power_cap_info *pwr_cap);

static struct protocol_class_pd_ops_data *protocol_class_pd_get_ops_data(unsigned int port_num)
{
	if (port_num >= TYPEC_PORT_MAX || !g_protocol_pd_data[port_num].ops)
		return NULL;

	return &g_protocol_pd_data[port_num];
}

int protocol_class_pd_get_pps_max_power(unsigned int port_num, unsigned int *max_power)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_pps_get_max_power))
		return -1;

	return temp_data->ops->protocol_pd_pps_get_max_power(max_power, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_max_power);

int protocol_class_pd_set_pps_pdo_select(unsigned int port_num, int volt, int curr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_pps_pdo_select))
		return -1;

	return temp_data->ops->protocol_pd_pps_pdo_select(volt, curr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_pdo_select);

int protocol_class_pd_get_pps_ptf(unsigned int port_num, int *pps_ptf)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_ptf))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_ptf(pps_ptf, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_ptf);

int protocol_class_pd_set_fixed_volt(unsigned int port_num, int volt)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_fixed_pdo_set_vol))
		return -1;

	return temp_data->ops->protocol_pd_fixed_pdo_set_vol(volt, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_fixed_volt);

int protocol_class_pd_set_gear_shift(unsigned int port_num, int gear_shift)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_gear_shift))
		return -1;

	return temp_data->ops->protocol_pd_set_gear_shift(gear_shift, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_gear_shift);

int protocol_class_pd_set_pps_max_cur(unsigned int port_num, unsigned int curr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pps_max_cur))
		return -1;

	return temp_data->ops->protocol_pd_set_pps_max_cur(curr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_max_cur);

int protocol_class_pd_get_pps_max_cur(unsigned int port_num, unsigned int *curr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_max_cur))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_max_cur(curr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_max_cur);

int protocol_class_pd_get_pps_status(unsigned int port_num, int *volt, int *curr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_status))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_status(volt, curr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_status);

int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pd_active))
		return -1;

	return temp_data->ops->protocol_pd_set_pd_active(pd_active, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pd_active);

int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pd_active))
		return -1;

	return temp_data->ops->protocol_pd_get_pd_active(pd_active, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pd_active);

int protocol_class_pd_set_pps_min_volt(unsigned int port_num, unsigned int volt)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pps_min_volt))
		return -1;

	return temp_data->ops->protocol_pd_set_pps_min_volt(volt, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_min_volt);

int protocol_class_pd_get_pps_min_volt(unsigned int port_num, unsigned int *volt)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_min_volt))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_min_volt(volt, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_min_volt);

int protocol_class_pd_set_pps_max_volt(unsigned int port_num, unsigned int volt)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pps_max_volt))
		return -1;

	return temp_data->ops->protocol_pd_set_pps_max_volt(volt, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_max_volt);

int protocol_class_pd_get_pps_max_volt(unsigned int port_num, unsigned int *volt)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_max_volt))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_max_volt(volt, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_max_volt);

int protocol_class_pd_set_pps_apdo_max(unsigned int port_num, unsigned int apdo_max)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pps_apdo_max))
		return -1;

	return temp_data->ops->protocol_pd_set_pps_apdo_max(apdo_max, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pps_apdo_max);

int protocol_class_pd_get_pps_apdo_max(unsigned int port_num, unsigned int *apdo_max)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pps_apdo_max))
		return -1;

	return temp_data->ops->protocol_pd_get_pps_apdo_max(apdo_max, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pps_apdo_max);

int protocol_class_pd_get_pd_type(unsigned int port_num, int *type)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pd_type))
		return -1;

	return temp_data->ops->protocol_pd_get_pd_type(type, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pd_type);

int protocol_class_pd_set_typec_mode(unsigned int port_num, int typec_mode)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_typec_mode))
		return -1;

	return temp_data->ops->protocol_pd_set_typec_mode(typec_mode, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_typec_mode);

int protocol_class_pd_get_typec_mode(unsigned int port_num, int *typec_mode)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_typec_mode))
		return -1;

	return temp_data->ops->protocol_pd_get_typec_mode(typec_mode, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_typec_mode);

int protocol_class_pd_get_typec_cc_orientation(unsigned int port_num, int *cc_orientation)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_typec_cc_orientation))
		return -1;

	return temp_data->ops->protocol_pd_get_typec_cc_orientation(cc_orientation, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_typec_cc_orientation);

int protocol_class_pd_set_typec_cc_orientation(unsigned int port_num, int cc_orientation)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_typec_cc_orientation))
		return -1;

	return temp_data->ops->protocol_pd_set_typec_cc_orientation(cc_orientation, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_typec_cc_orientation);

int protocol_class_pd_set_pd_in_hard_reset(unsigned int port_num, int in_hard_reset)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_in_hard_reset))
		return -1;

	return temp_data->ops->protocol_pd_set_in_hard_reset(in_hard_reset, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pd_in_hard_reset);

int protocol_class_pd_get_pd_in_hard_reset(unsigned int port_num, int *in_hard_reset)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_in_hard_reset))
		return -1;

	return temp_data->ops->protocol_pd_get_in_hard_reset(in_hard_reset, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pd_in_hard_reset);

int protocol_class_pd_set_usb_suspend_supported(unsigned int port_num, int supported)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_usb_suspend_supported))
		return -1;

	return temp_data->ops->protocol_pd_set_usb_suspend_supported(supported, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_usb_suspend_supported);

int protocol_class_pd_get_usb_suspend_supported(unsigned int port_num, int *supported)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_usb_suspend_supported))
		return -1;

	return temp_data->ops->protocol_pd_get_usb_suspend_supported(supported, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_usb_suspend_supported);

int protocol_class_pd_set_pd_typec_accessory_mode(unsigned int port_num, int typec_accessory_mode)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pd_typec_accessory_mode))
		return -1;

	return temp_data->ops->protocol_pd_set_pd_typec_accessory_mode(typec_accessory_mode,
		temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pd_typec_accessory_mode);

int protocol_class_pd_get_pd_typec_accessory_mode(unsigned int port_num, int *typec_accessory_mode)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pd_typec_accessory_mode))
		return -1;

	return temp_data->ops->protocol_pd_get_pd_typec_accessory_mode(typec_accessory_mode,
		temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pd_typec_accessory_mode);

int protocol_class_pd_get_cap(unsigned int port_num, int cap_type, struct adapter_power_cap *tacap)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_cap))
		return -1;

	return temp_data->ops->protocol_pd_get_cap(cap_type, tacap, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cap);

int protocol_class_pd_get_adapter_id(unsigned int port_num, unsigned int *adapter_id)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_adapter_id))
		return -1;

	return temp_data->ops->protocol_pd_get_adapter_id(adapter_id, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_adapter_id);

int protocol_class_pd_get_adapter_svid(unsigned int port_num, unsigned int *adapter_svid)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_adapter_svid))
		return -1;

	return temp_data->ops->protocol_pd_get_adapter_svid(adapter_svid, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_adapter_svid);

int protocol_class_pd_request_vdm_cmd(unsigned int port_num, enum uvdm_state cmd,
	unsigned int *data, unsigned int data_len)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_request_vdm_cmd))
		return -1;

	return temp_data->ops->protocol_pd_request_vdm_cmd(cmd, data, data_len, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_request_vdm_cmd);

int protocol_class_pd_get_vdm_cmd(unsigned int port_num, int *cmd, struct usbpd_vdm_data *vdm_data)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_vdm_cmd))
		return -1;

	return temp_data->ops->protocol_pd_get_vdm_cmd(cmd, vdm_data, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_vdm_cmd);

int protocol_class_pd_get_power_role(unsigned int port_num, unsigned char *pr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_power_role))
		return -1;

	return temp_data->ops->protocol_pd_get_power_role(pr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_power_role);

int protocol_class_pd_get_data_role(unsigned int port_num, unsigned char *pr)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_data_role))
		return -1;

	return temp_data->ops->protocol_pd_get_data_role(pr, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_data_role);

int protocol_class_pd_get_current_state(unsigned int port_num, char *current_state, int len)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_current_state))
		return -1;

	return temp_data->ops->protocol_pd_get_current_state(current_state, len, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_current_state);

int protocol_class_pd_get_pdos(unsigned int port_num, struct pd_pdo *pdos, int count)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pdos))
		return -1;

	return temp_data->ops->protocol_pd_get_pdos(pdos, count, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pdos);

int protocol_class_pd_set_verify_process(unsigned int port_num, int verify_process)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_verify_process))
		return -1;

	return temp_data->ops->protocol_pd_set_verify_process(verify_process, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_verify_process);

int protocol_class_pd_get_verify_process(unsigned int port_num, int *verify_process)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_verify_process))
		return -1;

	return temp_data->ops->protocol_pd_get_verify_process(verify_process, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_verify_process);

int protocol_class_pd_set_pd_verifed(unsigned int port_num, int pd_verifed)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_pd_verifed))
		return -1;

	return temp_data->ops->protocol_pd_set_pd_verifed(pd_verifed, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_set_pd_verifed);

int protocol_class_pd_get_pd_verifed(unsigned int port_num, int *pd_verifed)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_pd_verifed))
		return -1;

	return temp_data->ops->protocol_pd_get_pd_verifed(pd_verifed, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_pd_verifed);

int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_has_dp))
		return -1;

	return temp_data->ops->protocol_pd_get_has_dp(has_dp, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_has_dp);

int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_cid_status))
		return -1;

	return temp_data->ops->protocol_pd_get_cid_status(status, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cid_status);

int protocol_class_pd_get_otg_plugin_status(unsigned int port_num, bool *status)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_otg_plugin_status))
		return -1;

	return temp_data->ops->protocol_pd_get_otg_plugin_status(status, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_otg_plugin_status);

int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_set_cc_toggle))
		return -1;

	return temp_data->ops->protocol_pd_set_cc_toggle(en, temp_data->data);

}
EXPORT_SYMBOL(protocol_class_pd_set_cc_toggle);

int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_cc_toggle))
		return -1;

	return temp_data->ops->protocol_pd_get_cc_toggle(en, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cc_toggle);

int protocol_class_pd_get_snk_src_mode(unsigned int port_num, int *snk_src_mode)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_snk_src_mode))
		return -1;

	return temp_data->ops->protocol_pd_get_snk_src_mode(snk_src_mode, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_snk_src_mode);

int protocol_class_pd_get_cc_status(unsigned int port_num, bool *status)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_cc_status))
		return -1;

	return temp_data->ops->protocol_pd_get_cc_status(status, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cc_status);

int protocol_class_pd_get_cc_short_vbus(unsigned int port_num, int *cc_short_vbus)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_cc_short_vbus))
		return -1;

	return temp_data->ops->protocol_pd_get_cc_short_vbus(cc_short_vbus, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_cc_short_vbus);

int protocol_class_pd_get_suspend_support_status(unsigned int port_num, bool *pdsuspendsupported)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_suspend_support_status))
		return -1;

	return temp_data->ops->protocol_pd_get_suspend_support_status(pdsuspendsupported, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_suspend_support_status);

int protocol_class_pd_get_zimi_cypress_flag(unsigned int port_num, int *zimi_cypress_flag)
{
	struct protocol_class_pd_ops_data *temp_data = protocol_class_pd_get_ops_data(port_num);

	if (protocol_class_pd_invalid_ops(temp_data, protocol_pd_get_zimi_cypress_flag))
		return -1;

	return temp_data->ops->protocol_pd_get_zimi_cypress_flag(zimi_cypress_flag, temp_data->data);
}
EXPORT_SYMBOL(protocol_class_pd_get_zimi_cypress_flag);

int protocol_class_pd_get_port_num(void)
{
	return g_cur_port;
}
EXPORT_SYMBOL(protocol_class_pd_get_port_num);

int protocol_class_pd_register_ops(unsigned int port_num, struct protocol_class_pd_ops *ops, void *data)
{
	if (port_num >= TYPEC_PORT_MAX || !ops)
		return -1;

	g_protocol_pd_data[port_num].ops = ops;
	g_protocol_pd_data[port_num].data = data;

	return 0;
}
EXPORT_SYMBOL(protocol_class_pd_register_ops);

static int protocol_class_pd_set_adapter_verified(void *data, int verified)
{
	return protocol_class_pd_set_pd_verifed(g_cur_port, verified);
}

static int protocol_class_pd_get_adapter_verified(void *data, int *verified)
{
	return protocol_class_pd_get_pd_verifed(g_cur_port, verified);
}

static int protocol_class_pd_get_adapter_type(void *data, int *type)
{
	return protocol_class_pd_get_pd_type(g_cur_port, type);
}

static int protocol_class_pd_get_max_power(void *data, unsigned int *max_power)
{
	return 0;
}

static int protocol_class_pd_get_pwr_cap(void *data, struct adapter_power_cap_info *pwr_cap)
{
	struct pd_pdo pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };
	int i, ret;

	ret = protocol_class_pd_get_pdos(g_cur_port, pdos, PROTOCOL_PD_MAX_PDO_NUMS);
	if (ret)
		return -1;

	pwr_cap->nums = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		if (pdos[i].max_volt != 0 && pdos[i].max_volt == pdos[i].min_volt) {
			pwr_cap->cap[pwr_cap->nums].max_voltage = pdos[i].max_volt;
			pwr_cap->cap[pwr_cap->nums].min_voltage = pdos[i].min_volt;
			pwr_cap->cap[pwr_cap->nums].max_current = pdos[i].max_current;
			pwr_cap->cap[pwr_cap->nums].max_power = pdos[i].max_volt * pdos[i].max_current / 1000;
			pwr_cap->nums++;
			mca_log_info("pwr_cap[%d] vmin: %d, vmax: %d, imax: %d\n",
				i, pdos[i].min_volt, pdos[i].max_volt, pdos[i].max_current);
		}
	}

	if (pwr_cap->nums == 0)
		return -1;

	return 0;
}

static int protocol_class_pd_get_adapter_info(void *data, struct adapter_vendor_info *info)
{
	return 0;
}

static int protocol_class_pps_adapter_get_adv_current(struct adapter_power_cap_info *pwr_cap, int max_volt, int *curr)
{
	int i = 0;

	if (pwr_cap == NULL || pwr_cap->nums == 0)
		return -1;

	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		if (max_volt >= pwr_cap->cap[i].min_voltage &&
			max_volt <= pwr_cap->cap[i].max_voltage) {
				*curr = pwr_cap->cap[i].max_current;
				break;
		}
	}

	return 0;
}

static int protocol_class_pps_adapter_get_max_power(void *data, unsigned int *max_power)
{
	struct adapter_power_cap_info pwr_cap = {0};
	int type = XM_CHARGER_TYPE_UNKNOW;
	int power = 0;
	int adv_curr = 0;
	int i = 0;

	*max_power = 0;
	protocol_class_pd_get_pd_type(g_cur_port, &type);
	if (type != XM_CHARGER_TYPE_PPS && type != XM_CHARGER_TYPE_PD_VERIFY)
		return 0;

	protocol_class_pps_get_pwr_cap(NULL, &pwr_cap);
	for (i = 0; i < pwr_cap.nums; i++)
		power = max(power, pwr_cap.cap[i].max_power / 1000);

	protocol_class_pps_adapter_get_adv_current(&pwr_cap, MCA_PPS_MAX_VOL_10V, &adv_curr);
	mca_log_info("device_max_power: %d, max_power: %d, adv_curr: %d\n",
		device_max_power, power, adv_curr);

	// 96 is for 110V AC voltage when used in some foreigns
	if ((power >= 96 && adv_curr == 4800) || (power >= 96 && adv_curr == 5000))
		power = MCA_APDO_MAX_100W;
	else if (power >= 96 && power < 130)
		power = MCA_APDO_MAX_120W;
	else if (power >= 70 && power < 96)
		power = MCA_APDO_MAX_90W;
	else if (power >= 66 && power < 70)
		power = MCA_APDO_MAX_67W;
	else if (power >= 60 && power < 66)
		power = MCA_APDO_MAX_65W;
	else if (power >= 55 && power < 60)
		power = MCA_APDO_MAX_55W;
	else if (power >= 50 && power < 55)
		power = MCA_APDO_MAX_50W;
	else // other such as 40W, we do not show the animaton below 50w
		power = MCA_APDO_MAX_33W;

	*max_power = min(power, device_max_power);
	return 0;
}

static int protocol_class_pps_get_pwr_cap(void *data, struct adapter_power_cap_info *pwr_cap)
{
	struct pd_pdo pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };
	int i, ret;

	ret = protocol_class_pd_get_pdos(g_cur_port, pdos, PROTOCOL_PD_MAX_PDO_NUMS);
	if (ret)
		return -1;

	pwr_cap->nums = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		if (pdos[i].max_volt == pdos[i].min_volt)
			continue;

		pwr_cap->cap[pwr_cap->nums].max_voltage = pdos[i].max_volt;
		pwr_cap->cap[pwr_cap->nums].min_voltage = pdos[i].min_volt;
		pwr_cap->cap[pwr_cap->nums].max_current = pdos[i].max_current;
		pwr_cap->cap[pwr_cap->nums].max_power = pdos[i].max_volt * pdos[i].max_current / 1000;
		pwr_cap->nums++;
	}

	if (pwr_cap->nums == 0)
		return -1;

	return 0;
}

static int protocol_class_pps_set_volt_and_curr(void *data, int volt, int curr)
{
	return protocol_class_pd_set_pps_pdo_select(g_cur_port, volt, curr);
}

static int protocol_class_pps_get_pps_ptf(void *data, int *pps_ptf)
{
	return protocol_class_pd_get_pps_ptf(g_cur_port, pps_ptf);
}

static int protocol_class_pps_get_volt_and_curr(void *data, int *volt, int *curr)
{
	return protocol_class_pd_get_pps_status(g_cur_port, volt, curr);
}

struct adapter_protocol_class_ops g_protocol_pd_ops = {
	.set_adapter_verified = protocol_class_pd_set_adapter_verified,
	.get_adapter_verified = protocol_class_pd_get_adapter_verified,
	.get_adapter_type = protocol_class_pd_get_adapter_type,
	.get_adapter_max_power = protocol_class_pd_get_max_power,
	.get_adapter_pwr_cap = protocol_class_pd_get_pwr_cap,
	.set_adapter_volt_and_curr = protocol_class_pps_set_volt_and_curr,
	.get_adapter_pps_ptf = protocol_class_pps_get_pps_ptf,
	.get_adapter_info = protocol_class_pd_get_adapter_info,
};

struct adapter_protocol_class_ops g_protocol_pps_ops = {
	.get_adapter_max_power = protocol_class_pps_adapter_get_max_power,
	.get_adapter_pwr_cap = protocol_class_pps_get_pwr_cap,
	.set_adapter_volt_and_curr = protocol_class_pps_set_volt_and_curr,
	.get_adapter_volt_and_curr = protocol_class_pps_get_volt_and_curr,
	.get_adapter_pps_ptf = protocol_class_pps_get_pps_ptf,
	.get_adapter_info = protocol_class_pd_get_adapter_info,
};

static unsigned int protocol_pd_update_port_num(void)
{
	int snk_src_mode = TYPEC_UNATTACH;
	int i;


	for (i = 0; i < TYPEC_PORT_MAX; i++) {
		protocol_class_pd_get_snk_src_mode(i, &snk_src_mode);
		if (snk_src_mode >= TYPEC_SNK_MODE && snk_src_mode <= TYPEC_AUDIO_ACCESS_MODE)
			return i;
	}

	return TYPEC_PORT_0;
}

static int protocol_pd_notify_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	g_cur_port = protocol_pd_update_port_num();
	mca_log_info("cur active port %d\n", g_cur_port);

	return NOTIFY_OK;
}

enum pd_attr_list {
	PD_PROP_CC_ORIENTATION,
	PD_PROP_APDO_MAX,
	PD_PROP_HAS_DP,
	PD_PROP_CID_STATUS,
	PD_PROP_OTG_UI_SUPPORT,
	PD_PROP_CC_TOGGLE,
	PD_PROP_CC_SHORT_VBUS,
	PD_PROP_PPS_PTF,
	PD_PROP_TYPEC_MODE,
};

static ssize_t pd_class_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buff);
static ssize_t pd_class_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info pd_class_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_CC_ORIENTATION, cc_orientation),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_APDO_MAX, apdo_max),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_HAS_DP, has_dp),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_CID_STATUS, cid_status),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_OTG_UI_SUPPORT, otg_ui_support),
	mca_sysfs_attr_rw(pd_class_sysfs, 0640, PD_PROP_CC_TOGGLE, cc_toggle),
	mca_sysfs_attr_rw(pd_class_sysfs, 0640, PD_PROP_PPS_PTF, pps_ptf),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_CC_SHORT_VBUS, cc_short_vbus),
	mca_sysfs_attr_ro(pd_class_sysfs, 0440, PD_PROP_TYPEC_MODE, typec_mode),
};

#define PD_SYSFS_ATTRS_SIZE   ARRAY_SIZE(pd_class_sysfs_field_tbl)

static ssize_t pd_class_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;
	int type = XM_CHARGER_TYPE_UNKNOW;
	ssize_t count = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		pd_class_sysfs_field_tbl, PD_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case PD_PROP_CC_ORIENTATION:
		protocol_class_pd_get_typec_cc_orientation(g_cur_port, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		mca_log_info("cur typec port = %d, cc_orientation = %d\n", g_cur_port, val);
		break;
	case PD_PROP_APDO_MAX:
		protocol_class_pps_adapter_get_max_power(NULL, (unsigned int *)(&val));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		mca_log_err("read apdo_max: %d\n", val);
		break;
	case PD_PROP_HAS_DP:
		protocol_class_pd_get_has_dp(g_cur_port, (bool *)(&val));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		mca_log_err("read has_dp: %d\n", val);
		break;
	case PD_PROP_CID_STATUS:
		protocol_class_pd_get_cid_status(g_cur_port, (bool *)(&val));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case PD_PROP_OTG_UI_SUPPORT:
		val = 1;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case PD_PROP_CC_TOGGLE:
		protocol_class_pd_get_cc_toggle(g_cur_port, (bool *)(&val));
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case PD_PROP_CC_SHORT_VBUS:
		protocol_class_pd_get_cc_short_vbus(g_cur_port, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case PD_PROP_PPS_PTF:
		protocol_class_pd_get_pd_type(g_cur_port, &type);
		if (type == XM_CHARGER_TYPE_PPS || type == XM_CHARGER_TYPE_PD_VERIFY) {
			protocol_class_pd_get_pps_ptf(g_cur_port, &val);
		}
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		mca_log_err("read buf: %d\n", val);
		break;
	case PD_PROP_TYPEC_MODE:
		protocol_class_pd_get_typec_mode(g_cur_port, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	default:
		break;
	}
	return count;
}

static ssize_t pd_class_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		pd_class_sysfs_field_tbl, PD_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case PD_PROP_CC_TOGGLE:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		protocol_class_pd_set_cc_toggle(g_cur_port, val);
		break;
	case PD_PROP_PPS_PTF:
		break;
	default:
		break;
	}
	return count;
}

static int pd_sysfs_create_files(void)
{
	return mca_sysfs_create_files(SYSFS_DEV_3, pd_class_sysfs_field_tbl, PD_SYSFS_ATTRS_SIZE);
}

static int protocol_pd_class_probe(struct platform_device *pdev)
{
	int ret;

	ret = protocol_class_register_ops(ADAPTER_PROTOCOL_PD, &g_protocol_pd_ops, NULL);
	if (ret)
		return ret;
	ret = protocol_class_register_ops(ADAPTER_PROTOCOL_PPS, &g_protocol_pps_ops, NULL);
	if (ret)
		return ret;

	mca_parse_dts_u32(pdev->dev.of_node, "max_power", &device_max_power, 0);

	g_pd_class_ntf.notifier_call = protocol_pd_notify_cb;
	ret = mca_event_block_notify_register(MCA_EVENT_TYPE_TYPEC_PORT_STATUS, &g_pd_class_ntf);
	if (ret)
		return -EPROBE_DEFER;

	g_cur_port = 0;

	pd_sysfs_create_files();
	mca_log_err("probe ok\n");
	return ret;
}

static int protocol_pd_class_remove(struct platform_device *pdev)
{
	return 0;
}

static void protocol_pd_class_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,protocol_pd"},
	{},
};

static struct platform_driver protocol_pd_class_driver = {
	.driver	= {
		.name = "protocol_pd_class",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = protocol_pd_class_probe,
	.remove = protocol_pd_class_remove,
	.shutdown = protocol_pd_class_shutdown,
};

static int __init protocol_pd_class_init(void)
{
	return platform_driver_register(&protocol_pd_class_driver);
}
module_init(protocol_pd_class_init);

static void __exit protocol_pd_class_exit(void)
{
	platform_driver_unregister(&protocol_pd_class_driver);
}
module_exit(protocol_pd_class_exit);


MODULE_DESCRIPTION("protocol pd class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

