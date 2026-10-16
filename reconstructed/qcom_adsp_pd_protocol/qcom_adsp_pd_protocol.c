// SPDX-License-Identifier: GPL-2.0
/*
 * qcom_adsp_pd_protocol.c - pd protocol provider of the Xiaomi MCA stack that talks to the Qualcomm ADSP (pmic_glink)
 *
 * Reconstructed from the stock qcom_adsp_pd_protocol.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
 * __LINE__ values of the log calls are pinned with #line (the stock logs carry them).
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/qcom/mca_adsp_glink.h>
#include <mca/protocol/protocol_class_pd.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_adsp_pd"
#endif

/* property ids of the ADSP battery manager (0x2xxxx = pd/usb group) */
#define ADSP_PD_PROP_VERIFY_PROCESS	0x21002
#define ADSP_PD_PROP_VDM_BASE		0x21003
#define ADSP_PD_PROP_PD_TYPE		0x2000c
#define ADSP_PD_PROP_DATA_ROLE		0x2100c
#define ADSP_PD_PROP_CURRENT_STATE	0x2100d
#define ADSP_PD_PROP_ADAPTER_ID		0x2100e
#define ADSP_PD_PROP_ADAPTER_SVID	0x2100f
#define ADSP_PD_PROP_PD_VERIFED		0x21010
#define ADSP_PD_PROP_PDOS		0x21011
#define ADSP_PD_PROP_GET_VDM_CMD	0x21012
#define ADSP_PD_PROP_PPS_MAX_CUR	0x21013
#define ADSP_PD_PROP_PPS_APDO_MAX	0x21014
#define ADSP_PD_PROP_TYPEC_MODE		0x21015
#define ADSP_PD_PROP_CC_ORIENTATION	0x21016
#define ADSP_PD_PROP_PPS_PDO_SELECT	0x21017
#define ADSP_PD_PROP_FIXED_PD_VOLT	0x21018
#define ADSP_PD_PROP_PPS_STATUS		0x21019
#define ADSP_PD_PROP_PPS_MAX_POWER	0x2101b
#define ADSP_PD_PROP_HAS_DP		0x2101e
#define ADSP_PD_PROP_CID_STATUS		0x2101f
#define ADSP_PD_PROP_OTG_PLUGIN		0x21020
#define ADSP_PD_PROP_CC_TOGGLE		0x21021
#define ADSP_PD_PROP_SNK_SRC_MODE	0x21022
#define ADSP_PD_PROP_CC_STATUS		0x21023
#define ADSP_PD_PROP_CC_SHORT_VBUS	0x21024
#define ADSP_PD_PROP_PPS_PTF		0x21025
#define ADSP_PD_PROP_SUSPEND_SUPPORT	0x21026
#define ADSP_PD_PROP_ZIMI_CYPRESS	0x21027
#define ADSP_PD_PROP_GEAR_SHIFT		0x21028

struct adsp_pd_protocol {
	struct device *dev;
	int pd_active;
	int pd_verifed;
	struct notifier_block event_nb;		/* +0x10 */
	struct notifier_block abnormal_nb;	/* +0x28 */
	int verify_process;			/* +0x40 */
};

static int adsp_pd_protocol_get_pps_max_power(unsigned int *max_power, void *data)
{
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_PPS_MAX_POWER, max_power, 4);
}

static int adsp_pd_protocol_select_pps_pdo(unsigned int vbus_mv, unsigned int ibus_ma, void *data)
{
	int val;

#line 69
	mca_log_info("vbus_mv: %d, ibus_ma: %d", vbus_mv, ibus_ma);
	val = ((vbus_mv / 20) << 16) | (ibus_ma / 50);
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_PPS_PDO_SELECT, &val, 4);
}

static int adsp_pd_protocol_get_pps_ptf(int *pps_ptf, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_PPS_PTF, pps_ptf, 4);
}

static int adsp_pd_protocol_set_fixed_pd_volt(unsigned int vbus_mv, void *data)
{
	struct adsp_pd_protocol *info = data;

#line 86
	mca_log_info("vbus_mv: %d", vbus_mv);
	if (info->verify_process) {
#line 88
		mca_log_err("pd verify processing, won't set fixed pd volt\n");
		return 0;
	}
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_FIXED_PD_VOLT, &vbus_mv, 4);
}

static int adsp_pd_protocol_set_gear_shift(int gear_shift, void *data)
{
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_GEAR_SHIFT, &gear_shift, 4);
}

static int adsp_pd_protocol_get_pps_max_cur(unsigned int *max_curr, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_PPS_MAX_CUR, max_curr, 4);
}

static int adsp_pd_protocol_get_pps_status(unsigned int *volt, unsigned int *curr, void *data)
{
	unsigned int val = 0;
	int ret;

	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_PPS_STATUS, &val, 4);
	if (!ret) {
		*curr = (val & 0xffff) * 50;
		*volt = (val >> 16) * 20;
	}
	return ret;
}

static int adsp_pd_protocol_set_pd_active(int pd_active, void *data)
{
	struct adsp_pd_protocol *info = data;

	if (!info)
		return -1;
	info->pd_active = pd_active;
	return 0;
}

static int adsp_pd_protocol_get_pd_active(int *pd_active, void *data)
{
	struct adsp_pd_protocol *info = data;

	if (!info)
		return -1;
	*pd_active = info->pd_active;
	return 0;
}

static int adsp_pd_protocol_get_pps_apdo_max(unsigned int *apdo_max, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_PPS_APDO_MAX, apdo_max, 4);
}

static int adsp_pd_protocol_get_pd_type(int *type, void *data)
{
	struct adsp_pd_protocol *info = data;
	int val = 0;
	int ret;

	if (!info)
		return -1;
	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_PD_TYPE, &val, 4);
	if (ret)
		return ret;
	if (val == 11)
		*type = info->pd_verifed ? 12 : 11;
	else
		*type = 0;
	return 0;
}

static int adsp_pd_protocol_get_typec_mode(int *typec_mode, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_TYPEC_MODE, typec_mode, 4);
}

static int adsp_pd_protocol_get_typec_cc_orientation(int *cc_orientation, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_CC_ORIENTATION, cc_orientation, 4);
}

static int adsp_pd_protocol_get_adapter_id(unsigned int *adapter_id, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_ADAPTER_ID, adapter_id, 4);
}

static int adsp_pd_protocol_get_adapter_svid(unsigned int *adapter_svid, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_ADAPTER_SVID, adapter_svid, 4);
}

static int adsp_pd_protocol_get_has_dp(bool *has_dp, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_HAS_DP, has_dp, 1);
}

static int adsp_pd_protocol_request_vdm_cmd(enum uvdm_state cmd, unsigned int *cdata, unsigned int data_len, void *data)
{
	unsigned int prop = ADSP_PD_PROP_VDM_BASE;
	int i;

	if (!cdata)
		return -1;
#line 245
	mca_log_info("data_len=%d, cmd=%d, data=%d\n", data_len, cmd, cdata[0]);
	switch (cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		break;
	case USBPD_UVDM_CHARGER_VOLTAGE:
		prop += 1;
		break;
	case USBPD_UVDM_CHARGER_TEMP:
		prop += 2;
		break;
	case USBPD_UVDM_SESSION_SEED:
		if (data_len < 4) {
			prop = 0;
			break;
		}
		for (i = 0; i < 4; i++)
			cdata[i] = cpu_to_be32(cdata[i]);
#line 262
		mca_log_info("SESSION_SEED:data = 0x%x 0x%x 0x%x 0x%x\n", cdata[0], cdata[1], cdata[2], cdata[3]);
		prop += 3;
		break;
	case USBPD_UVDM_AUTHENTICATION:
		if (data_len < 4)
			return -1;
		for (i = 0; i < 4; i++)
			cdata[i] = cpu_to_be32(cdata[i]);
#line 270
		mca_log_info("AUTHENTICATION:data =  0x%x 0x%x 0x%x 0x%x\n", cdata[0], cdata[1], cdata[2], cdata[3]);
		prop += 4;
		break;
	case USBPD_UVDM_VERIFIED:
		prop += 5;
		break;
	case USBPD_UVDM_REMOVE_COMPENSATION:
		prop += 6;
		break;
	case USBPD_UVDM_REVERSE_AUTHEN:
		if (data_len < 4)
			return -1;
		for (i = 0; i < 4; i++)
			cdata[i] = cpu_to_be32(cdata[i]);
		for (i = 0; i < 4; i++) {
#line 278
			mca_log_info("REVERSE_AUTHEN:data = %d\n", cdata[i]);
		}
		prop += 7;
		break;
	case USBPD_UVDM_RESET_VSAFE0V:
		prop += 8;
		break;
	case USBPD_UVDM_REQUEST_PD_DR:
		prop += 9;
		break;
	default:
		printk(KERN_INFO "cmd:%d is not support\n", cmd);
		break;
	}
	return mca_adsp_glink_write_prop(prop, cdata, data_len);
}

static int adsp_pd_protocol_get_vdm_cmd(int *cmd, struct usbpd_vdm_data *vdm_data, void *data)
{
	unsigned int auth[4] = { 0 };
	int ret;

	if (!vdm_data)
		return -1;
	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_GET_VDM_CMD, cmd, 4);
	if (ret)
		return ret;
#line 315
	mca_log_info("current uvdm_state: %d\n", *cmd);
	switch (*cmd) {
	case USBPD_UVDM_CHARGER_VERSION:
		ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_VDM_BASE, &vdm_data->ta_version, 4);
		break;
	case USBPD_UVDM_CHARGER_VOLTAGE:
		ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_VDM_BASE + 1, &vdm_data->ta_voltage, 4);
		break;
	case USBPD_UVDM_CHARGER_TEMP:
		ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_VDM_BASE + 2, &vdm_data->ta_temp, 4);
		break;
	case USBPD_UVDM_AUTHENTICATION:
		ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_VDM_BASE + 4, auth, 16);
		vdm_data->digest[0] = auth[0];
		vdm_data->digest[1] = auth[1];
		vdm_data->digest[2] = auth[2];
		vdm_data->digest[3] = auth[3];
#line 337
		mca_log_info("get auth data[0]=%u, data[1]=%u, data[2]=%u, data[3]=%u", auth[0], auth[1], auth[2], auth[3]);
		break;
	default:
		break;
	}
	return ret;
}

static int adsp_pd_protocol_get_data_role(unsigned char *role, void *data)
{
	int val = 1;
	int ret;

	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_DATA_ROLE, &val, 4);
	*role = val;
#line 355
	mca_log_info("get data_role: %d\n", *role);
	return ret;
}

static int adsp_pd_protocol_get_current_state(char *current_state, int len, void *data)
{
	int val;
	int ret;

	if (len < 64)
		return -1;
	val = 0;
	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_CURRENT_STATE, &val, 4);
	if (ret)
		return ret;
	switch (val) {
	case 0:
		strscpy(current_state, "SRC_Ready", sizeof("SRC_Ready"));
		break;
	case 1:
		strscpy(current_state, "SNK_STARTUP", sizeof("SNK_STARTUP"));
		break;
	case 2:
		strscpy(current_state, "SNK_Ready", sizeof("SNK_Ready"));
		break;
	default:
		strscpy(current_state, "UNKNOWN", sizeof("UNKNOWN"));
		break;
	}
#line 218
	mca_log_info("get current_state: %u => %s\n", val, current_state);
	return 0;
}

static int adsp_pd_protocol_get_pdos(struct pd_pdo *received_pdos, int count, void *data)
{
	int ret, i;

	ret = mca_adsp_glink_read_prop(ADSP_PD_PROP_PDOS, received_pdos, count * 12);
	if (count > 0 && !ret) {
		for (i = 0; i < count; i++) {
#line 166
			mca_log_info("pdo[%d] min_volt: %d, max_volt: %d, max_current: %d\n", i, received_pdos[i].min_volt, received_pdos[i].max_volt, received_pdos[i].max_current);
		}
	}
	return ret;
}

static int adsp_pd_protocol_set_verify_process(int verify_process, void *data)
{
	struct adsp_pd_protocol *info = data;

	info->verify_process = verify_process;
#line 143
	mca_log_err("verify_process: %d", verify_process);
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_VERIFY_PROCESS, &verify_process, 4);
}

static int adsp_pd_protocol_get_verify_process(int *verify_process, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_VERIFY_PROCESS, verify_process, 4);
}

static int adsp_pd_protocol_set_pd_verifed(int pd_verifed, void *data)
{
	struct adsp_pd_protocol *info = data;

#line 364
	mca_log_err("set pd_verifed: %d\n", pd_verifed);
	info->pd_verifed = pd_verifed;
	mca_adsp_glink_write_prop(ADSP_PD_PROP_PD_VERIFED, &pd_verifed, 4);
	return 0;
}

static int adsp_pd_protocol_get_pd_verifed(int *pd_verifed, void *data)
{
	struct adsp_pd_protocol *info = data;

#line 376
	mca_log_info("get verifed %d\n", info->pd_verifed);
	*pd_verifed = info->pd_verifed;
	return 0;
}

static int adsp_pd_protocol_get_cid_status(bool *status, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_CID_STATUS, status, 1);
}

static int adsp_pd_protocol_get_otg_plugin_status(bool *status, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_OTG_PLUGIN, status, 1);
}

static int adsp_protocol_set_cc_toggle(bool en, void *data)
{
#line 443
	mca_log_info("set cc toggle: 0x%2x\n", en);
	return mca_adsp_glink_write_prop(ADSP_PD_PROP_CC_TOGGLE, &en, 1);
}

static int adsp_protocol_get_cc_toggle(bool *en, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_CC_TOGGLE, en, 1);
}

static int adsp_pd_protocol_get_snk_src_mode(int *snk_src_mode, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_SNK_SRC_MODE, snk_src_mode, 4);
}

static int adsp_protocol_get_cc_status(bool *status, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_CC_STATUS, status, 1);
}

static int adsp_protocol_get_cc_short_vbus(int *cc_short_vbus, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_CC_SHORT_VBUS, cc_short_vbus, 4);
}

static int adsp_pd_protocol_get_suspend_support_status(bool *pdsuspendsupported, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_SUSPEND_SUPPORT, pdsuspendsupported, 1);
}

static int adsp_pd_protocol_get_zimi_cypress_flag(int *zimi_cypress, void *data)
{
	return mca_adsp_glink_read_prop(ADSP_PD_PROP_ZIMI_CYPRESS, zimi_cypress, 4);
}

struct protocol_class_pd_ops g_adsp_pd_ops = {
	.protocol_pd_pps_get_max_power = adsp_pd_protocol_get_pps_max_power,
	.protocol_pd_pps_pdo_select = adsp_pd_protocol_select_pps_pdo,
	.protocol_pd_get_pps_ptf = adsp_pd_protocol_get_pps_ptf,
	.protocol_pd_fixed_pdo_set_vol = adsp_pd_protocol_set_fixed_pd_volt,
	.protocol_pd_set_gear_shift = adsp_pd_protocol_set_gear_shift,
	.protocol_pd_get_pps_max_cur = adsp_pd_protocol_get_pps_max_cur,
	.protocol_pd_get_pps_status = adsp_pd_protocol_get_pps_status,
	.protocol_pd_set_pd_active = adsp_pd_protocol_set_pd_active,
	.protocol_pd_get_pd_active = adsp_pd_protocol_get_pd_active,
	.protocol_pd_get_pps_apdo_max = adsp_pd_protocol_get_pps_apdo_max,
	.protocol_pd_get_pd_type = adsp_pd_protocol_get_pd_type,
	.protocol_pd_get_typec_mode = adsp_pd_protocol_get_typec_mode,
	.protocol_pd_get_typec_cc_orientation = adsp_pd_protocol_get_typec_cc_orientation,
	.protocol_pd_get_adapter_id = adsp_pd_protocol_get_adapter_id,
	.protocol_pd_get_adapter_svid = adsp_pd_protocol_get_adapter_svid,
	.protocol_pd_get_has_dp = adsp_pd_protocol_get_has_dp,
	.protocol_pd_request_vdm_cmd = adsp_pd_protocol_request_vdm_cmd,
	.protocol_pd_get_vdm_cmd = adsp_pd_protocol_get_vdm_cmd,
	.protocol_pd_get_data_role = adsp_pd_protocol_get_data_role,
	.protocol_pd_get_current_state = adsp_pd_protocol_get_current_state,
	.protocol_pd_get_pdos = adsp_pd_protocol_get_pdos,
	.protocol_pd_set_verify_process = adsp_pd_protocol_set_verify_process,
	.protocol_pd_get_verify_process = adsp_pd_protocol_get_verify_process,
	.protocol_pd_set_pd_verifed = adsp_pd_protocol_set_pd_verifed,
	.protocol_pd_get_pd_verifed = adsp_pd_protocol_get_pd_verifed,
	.protocol_pd_get_cid_status = adsp_pd_protocol_get_cid_status,
	.protocol_pd_get_otg_plugin_status = adsp_pd_protocol_get_otg_plugin_status,
	.protocol_pd_set_cc_toggle = adsp_protocol_set_cc_toggle,
	.protocol_pd_get_cc_toggle = adsp_protocol_get_cc_toggle,
	.protocol_pd_get_snk_src_mode = adsp_pd_protocol_get_snk_src_mode,
	.protocol_pd_get_cc_status = adsp_protocol_get_cc_status,
	.protocol_pd_get_cc_short_vbus = adsp_protocol_get_cc_short_vbus,
	.protocol_pd_get_suspend_support_status = adsp_pd_protocol_get_suspend_support_status,
	.protocol_pd_get_zimi_cypress_flag = adsp_pd_protocol_get_zimi_cypress_flag,
};

static int adsp_pd_protocol_event_cb(struct notifier_block *nb, unsigned long action, void *data);
static int adsp_pd_protocol_abnormal_cb(struct notifier_block *nb, unsigned long action, void *data);

static int adsp_pd_protocol_probe(struct platform_device *pdev)
{
	struct adsp_pd_protocol *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	info->dev = &pdev->dev;
	ret = protocol_class_pd_register_ops(0, &g_adsp_pd_ops, info);
	if (ret) {
#line 555
		mca_log_err("register ops fail\n");
		return ret;
	}
	info->event_nb.notifier_call = adsp_pd_protocol_event_cb;
	mca_event_block_notify_register(0, &info->event_nb);
	info->abnormal_nb.notifier_call = adsp_pd_protocol_abnormal_cb;
	mca_event_block_notify_register(5, &info->abnormal_nb);
#line 565
	mca_log_err("probe ok\n");
	return ret;
}

static int adsp_pd_protocol_remove(struct platform_device *pdev)
{
	return 0;
}

static void adsp_pd_protocol_shutdown(struct platform_device *pdev)
{
}

static int adsp_pd_protocol_event_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	struct adsp_pd_protocol *info = container_of(nb, struct adsp_pd_protocol, event_nb);

	if (action == 0) {
		info->pd_verifed = 0;
		info->verify_process = 0;
	}
	return 0;
}

static int adsp_pd_protocol_abnormal_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	struct adsp_pd_protocol *info = container_of(nb, struct adsp_pd_protocol, abnormal_nb);

	if (action == 50)
		info->pd_verifed = 0;
	return 0;
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,adsp_pd_protocol" },
	{ },
};

static struct platform_driver adsp_pd_protocol_driver = {
	.probe = adsp_pd_protocol_probe,
	.remove = adsp_pd_protocol_remove,
	.shutdown = adsp_pd_protocol_shutdown,
	.driver = {
		.name = "adsp_pd_protocol",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
};
module_platform_driver(adsp_pd_protocol_driver);

MODULE_DESCRIPTION("qcom pd protocol");
MODULE_AUTHOR("lvchen@xiaomi.com");
MODULE_LICENSE("GPL v2");
