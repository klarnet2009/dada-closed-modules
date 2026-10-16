// SPDX-License-Identifier: GPL-2.0
/*
 * mca_pd_auth.c
 *
 * pd adapter private auth driver
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
#include <mca/protocol/protocol_pd_class.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/platform/platform_buckchg_class.h>
#include "pd_auth.h"
#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "pd_auth"
#endif

#ifdef CONFIG_SYSFS
static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info strategy_pd_auth_sysfs_field_tbl[] = {
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_NAME, name),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_REQUEST_VDM_CMD, request_vdm_cmd),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_CURRENT_STATE, current_state),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_ADAPTER_ID, adapter_id),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_ADAPTER_SVID, adapter_svid),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_VERIFY_PROCESS, verify_process),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_USBPD_VERIFIED, verified),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_CURRENT_PR, current_pr),
	mca_sysfs_attr_ro(strategy_pd_auth_sysfs, 0440, PD_AUTH_IS_PD_ADAPTER, is_pd_adapter),
	mca_sysfs_attr_rw(strategy_pd_auth_sysfs, 0664, PD_AUTH_USBPD_DATA_ROLE, data_role),
};

#define PD_AUTH_ATTRS_SIZE   ARRAY_SIZE(strategy_pd_auth_sysfs_field_tbl)

static struct attribute *strategy_pd_auth_sysfs_attrs[PD_AUTH_ATTRS_SIZE + 1];

static const struct attribute_group strategy_pd_auth_sysfs_attr_group = {
	.attrs = strategy_pd_auth_sysfs_attrs,
};

static int strategy_pd_auth_get_vdm_cmd(char *buf, int active_port)
{
	int i;
	int cmd;
	int rc;
	char data[PD_AUTH_UVDM_AUTH_DATA_LEN] = { 0 };
	char str_buf[PD_AUTH_UVDM_AUTH_STR_LEN] = { 0 };
	struct usbpd_vdm_data vdm_data;

	rc = protocol_class_pd_get_vdm_cmd(active_port, &cmd, &vdm_data);
	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		return snprintf(buf, PAGE_SIZE, "%d,%x\n", cmd, vdm_data.ta_version);
	case USBPD_UVDM_CHARGER_TEMP:
		return snprintf(buf, PAGE_SIZE, "%d,%d\n", cmd, vdm_data.ta_temp);
	case USBPD_UVDM_CHARGER_VOLTAGE:
		return snprintf(buf, PAGE_SIZE, "%d,%d\n", cmd, vdm_data.ta_voltage);
	case USBPD_UVDM_SESSION_SEED:
	case USBPD_UVDM_CONNECT:
	case USBPD_UVDM_DISCONNECT:
	case USBPD_UVDM_VERIFIED:
	case USBPD_UVDM_REMOVE_COMPENSATION:
	case USBPD_UVDM_NAN_ACK:
		return snprintf(buf, PAGE_SIZE, "%d,Null\n", cmd);
	case USBPD_UVDM_REVERSE_AUTHEN:
		return snprintf(buf, PAGE_SIZE, "%d,%d", cmd, vdm_data.reauth);
	case USBPD_UVDM_AUTHENTICATION:
		if (rc) {
			mca_log_err("get auth data failed, rc = %d\n", rc);
			return snprintf(buf, PAGE_SIZE, "%d,Null\n", cmd + USBPD_UVDM_CMD_INIT);
		}
		for (i = 0; i < USBPD_UVDM_SS_LEN; i++) {
			memset(data, 0, sizeof(data));
			snprintf(data, sizeof(data), "%08lx", vdm_data.digest[i]);
			strlcat(str_buf, data, sizeof(str_buf));
		}
		return snprintf(buf, PAGE_SIZE, "%d,%s\n", cmd, str_buf);
	default:
		if ((cmd >= USBPD_UVDM_CMD_INIT && cmd <= USBPD_UVDM_CMD_INIT + USBPD_UVDM_CONNECT) ||
			(cmd >= USBPD_UVDM_CMD_NAK && cmd <= USBPD_UVDM_CMD_NAK + USBPD_UVDM_CONNECT))
			return snprintf(buf, PAGE_SIZE, "%d,Null\n", cmd);
		mca_log_err("feedbak cmd:%d is not support\n", cmd);
		break;
	}

	return snprintf(buf, PAGE_SIZE, "%d,%s\n", cmd, str_buf);
}

static int strategy_pd_auth_is_pd_adapter(char *buf, int active_port)
{
	struct pd_pdo received_pdos[PROTOCOL_PD_MAX_PDO_NUMS] = { 0 };

	protocol_class_pd_get_pdos(active_port, received_pdos, PROTOCOL_PD_MAX_PDO_NUMS);

	if (received_pdos[1].max_volt)
		return snprintf(buf, PAGE_SIZE, "true\n");

	return snprintf(buf, PAGE_SIZE, "false\n");
}

static ssize_t strategy_pd_auth_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int len;
	unsigned int adapter_id = 0;
	unsigned int adapter_svid = 0;
	int verify_process = 0;
	int verifed = 0;
	int active_port = protocol_class_pd_get_port_num();
	unsigned char role = 0;
	struct mca_sysfs_attr_info *attr_info;
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	char current_state[PROTOCOL_PD_MAX_STRING_LEN] = { 0 };

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_pd_auth_sysfs_field_tbl, PD_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_NAME:
		len = 0;
		break;
	case PD_AUTH_REQUEST_VDM_CMD:
		len = strategy_pd_auth_get_vdm_cmd(buf, active_port);
		break;
	case PD_AUTH_CURRENT_STATE:
		(void)protocol_class_pd_get_current_state(active_port,
			current_state, PROTOCOL_PD_MAX_STRING_LEN);
		len = snprintf(buf, PAGE_SIZE, "%s\n", current_state);
		break;
	case PD_AUTH_ADAPTER_ID:
		(void)protocol_class_pd_get_adapter_id(active_port, &adapter_id);
		len = snprintf(buf, PAGE_SIZE, "%08x\n", adapter_id);
		break;
	case PD_AUTH_ADAPTER_SVID:
		(void)protocol_class_pd_get_adapter_svid(active_port, &adapter_svid);
		len = snprintf(buf, PAGE_SIZE, "%04x\n", adapter_svid);
		break;
	case PD_AUTH_VERIFY_PROCESS:
		(void)protocol_class_pd_get_verify_process(active_port, &verify_process);
		len = snprintf(buf, PAGE_SIZE, "%d\n", verify_process);
		break;
	case PD_AUTH_USBPD_VERIFIED:
		protocol_class_pd_get_pd_verifed(active_port, &verifed);
		len = snprintf(buf, PAGE_SIZE, "%d\n", verifed);
		break;
	case PD_AUTH_CURRENT_PR:
		if (protocol_class_pd_get_power_role(active_port, &role))
			len = snprintf(buf, PAGE_SIZE, "none\n");
		if (role == PD_ROLE_SINK_FOR_ADAPTER)
			len = snprintf(buf, PAGE_SIZE, "sink\n");
		else if (role == PD_ROLE_SOURCE_FOR_ADAPTER)
			len = snprintf(buf, PAGE_SIZE, "source\n");
		break;
	case PD_AUTH_IS_PD_ADAPTER:
		len = strategy_pd_auth_is_pd_adapter(buf, active_port);
		break;
	case PD_AUTH_USBPD_DATA_ROLE:
		protocol_class_pd_get_data_role(active_port, &role);
		if (role == XM_REQUEST_PD_DR_UFP)
			len = snprintf(buf, PAGE_SIZE, "ufp\n");
		else if (role == XM_REQUEST_PD_DR_DFP)
			len = snprintf(buf, PAGE_SIZE, "dfp\n");
		else
			len = snprintf(buf, PAGE_SIZE, "unknown\n");
		break;
	default:
		len = 0;
		break;
	}

	return len;
}

static int strategy_pd_auth_string2hex(char *str, unsigned char *out, unsigned int *outlen)
{
	char *p = str;
	char high = 0, low = 0;
	int tmplen = strlen(p), cnt = 0;

	tmplen = strlen(p);
	while (cnt < (tmplen / 2)) {
		high = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;
		low = (*(++p) > '9' && ((*p <= 'F') || (*p <= 'f'))) ? *(p) - 48 - 7 : *(p) - 48;
		out[cnt] = ((high & 0x0f) << 4 | (low & 0x0f));
		p++;
		cnt++;
	}
	if (tmplen % 2 != 0)
		out[cnt] = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;

	if (outlen != NULL)
		*outlen = tmplen / 2 + tmplen % 2;

	return tmplen / 2 + tmplen % 2;
}

static int strategy_pd_auth_set_vdm_cmd(const char *buf, int active_port)
{
	int cmd, ret;
	unsigned char buffer[PROTOCOL_PD_MAX_STRING_LEN];
	unsigned char data[PD_AUTH_VDM_CMD_HEX_DATA_LEN] = { 0 };
	unsigned int count;

	ret = sscanf(buf, "%d,%s\n", &cmd, buffer);
	if (!ret)
		return ret;

	mca_log_info("buf:%s cmd:%d, buffer:%s\n", buf, cmd, buffer);

	strategy_pd_auth_string2hex(buffer, data, &count);
	(void)protocol_class_pd_request_vdm_cmd(active_port, cmd, (unsigned int *)data, count);

	return 0;
}

#define XM_ADAPTER_SVID 0x2717
static void strategy_pd_auth_fail_report_dfx(void)
{
	int active_port = protocol_class_pd_get_port_num();
	unsigned int adapter_id = 0;
	unsigned int adapter_svid = 0;
	int online = 0;

	(void)platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (!online)
		return;

	(void)protocol_class_pd_get_adapter_svid(active_port, &adapter_svid);
	if (adapter_svid != XM_ADAPTER_SVID)
		return;

	(void)protocol_class_pd_get_adapter_id(active_port, &adapter_id);
	mca_charge_mievent_report(CHARGE_DFX_PD_AUTH_FAILED, &adapter_id, 1);
}

static ssize_t strategy_pd_auth_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct pd_auth_strategy *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int value = 0;
	int verify_process_end;
	int active_port = protocol_class_pd_get_port_num();

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_pd_auth_sysfs_field_tbl, PD_AUTH_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case PD_AUTH_REQUEST_VDM_CMD:
		if (strategy_pd_auth_set_vdm_cmd(buf, active_port))
			return -1;
		break;
	case PD_AUTH_VERIFY_PROCESS:
		if (sscanf(buf, "%d\n", &value) != 1) {
			mca_log_err("verify process value invalid %s\n", buf);
			return -EINVAL;
		}
		(void)protocol_class_pd_set_verify_process(active_port, value);
		verify_process_end = !value;
		if (!value) {
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
				MCA_EVENT_CHARGE_VERIFY_PROCESS_END, &verify_process_end);
		}
		break;
	case PD_AUTH_USBPD_VERIFIED:
		if (sscanf(buf, "%d\n", &value) != 1) {
			mca_log_err("verified value invalid %s\n", buf);
			return -EINVAL;
		}
		mca_log_info("set pd verified %d\n", value);
		(void)protocol_class_pd_set_pd_verifed(active_port, value);
		if (value) {
			info->pd_verified_type = XM_CHARGER_TYPE_PD_VERIFY;
			mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_TYPE,
				MCA_EVENT_CHARGE_TYPE_CHANGE, &info->pd_verified_type);
		}
		else
			strategy_pd_auth_fail_report_dfx();
		break;
	case PD_AUTH_USBPD_DATA_ROLE:
		mca_log_info("set data_role: %s\n", buf);
		if (strncmp(buf, "ufp", 3) == 0)
			value = XM_REQUEST_PD_DR_UFP;
		else if (strncmp(buf, "dfp", 3) == 0)
			value = XM_REQUEST_PD_DR_DFP;
		else
			return -EINVAL;
		protocol_class_pd_request_vdm_cmd(TYPEC_PORT_0, USBPD_UVDM_REQUEST_PD_DR, &value, sizeof(value));
		break;
	default:
		break;
	}

	return count;
}

static int strategy_pd_auth_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(strategy_pd_auth_sysfs_attrs, strategy_pd_auth_sysfs_field_tbl,
		PD_AUTH_ATTRS_SIZE);
	return mca_sysfs_create_link_group("typec", "strategy_pd_auth",
		dev, &strategy_pd_auth_sysfs_attr_group);
}

static void strategy_pd_auth_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("typec", "strategy_pd_auth",
		dev, &strategy_pd_auth_sysfs_attr_group);
}

#else
static inline int strategy_pd_auth_create_group(struct device *dev)
{
}

static void strategy_pd_auth_remove_group(struct device *dev)
{
}
#endif /* CONFIG_SYSFS */

static int strategy_pd_auth_probe(struct platform_device *pdev)
{
	struct pd_auth_strategy *pd_auth;

	mca_log_info("probe begin\n");
	pd_auth = devm_kzalloc(&pdev->dev, sizeof(*pd_auth), GFP_KERNEL);
	if (!pd_auth) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	pd_auth->dev = &pdev->dev;
	pd_auth->pd_verified_type = 0;
	platform_set_drvdata(pdev, pd_auth);
	strategy_pd_auth_create_group(pd_auth->dev);
	mca_log_err("probe end\n");

	return 0;
}

static int strategy_pd_auth_remove(struct platform_device *pdev)
{
	strategy_pd_auth_remove_group(&pdev->dev);
	return 0;
}

static void strategy_pd_auth_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,strategy_pd_auth"},
	{},
};

static struct platform_driver strategy_pd_auth_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "strategy_pd_auth",
		.of_match_table = match_table,
	},
	.probe = strategy_pd_auth_probe,
	.remove = strategy_pd_auth_remove,
	.shutdown = strategy_pd_auth_shutdown,
};

static int __init strategy_pd_auth_init(void)
{
	return platform_driver_register(&strategy_pd_auth_driver);
}
module_init(strategy_pd_auth_init);

static void __exit strategy_pd_auth_exit(void)
{
	platform_driver_unregister(&strategy_pd_auth_driver);
}
module_exit(strategy_pd_auth_exit);

MODULE_DESCRIPTION("Xiaomi Sub Pmic Protocol");
MODULE_AUTHOR("yinshunan@xiaomi.com");
MODULE_LICENSE("GPL v2");


