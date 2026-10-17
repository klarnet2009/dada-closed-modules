// SPDX-License-Identifier: GPL-2.0
/*
 * mca_qcom_subpmic_proxy.c
 *
 * qcom subpmic driver: proxies the MCA buck charger / BC1.2 / QC platform classes to the
 * Qualcomm ADSP charger firmware through the mca_adsp_glink property channel.
 *
 * Reconstructed from the stock mca_qcom_subpmic_proxy.ko (Xiaomi 15 "dada") by reverse engineering; not original source.
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
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>
#ifndef CONFIG_QTI_BATTERY_CHARGER
#define CONFIG_QTI_BATTERY_CHARGER 1
#endif
#include <linux/soc/qcom/battery_charger.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_bc12_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/protocol/protocol_qc_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/qcom/mca_adsp_glink.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "qcom_subpmic"
#endif

/* property ids of the ADSP charger firmware: (type << 16) | id */
#define QCOM_PROP_SHIP_MODE		5
#define QCOM_PROP_SHUTDOWN		6
#define QCOM_PROP_TOO_HOT_LIMIT		7
#define QCOM_PROP_CHG_STATUS		0x2001
#define QCOM_PROP_CHG_TYPE		0x2002
#define QCOM_PROP_ENABLE_CHARGING	0x2003
#define QCOM_PROP_BUCK_FSW		0x2004
#define QCOM_PROP_CHARGE_CURRENT	0x2005
#define QCOM_PROP_TERM_CURRENT		0x2006
#define QCOM_PROP_TERM_VOLT		0x2007
#define QCOM_PROP_INPUT_CURRENT_LIMIT	0x2008
#define QCOM_PROP_INPUT_VOLT_LIMIT	0x2009
#define QCOM_PROP_PRECHG_CURRENT	0x200a
#define QCOM_PROP_PRECHG_VOLT		0x200b
#define QCOM_PROP_VSYS_VOLT		0x200c
#define QCOM_PROP_OTG_BOOST_ENABLE	0x200d
#define QCOM_PROP_OTG_GATE_ENABLE	0x200e
#define QCOM_PROP_BOOST_ENABLE		0x200f
#define QCOM_PROP_BOOST_VOLTAGE		0x2010
#define QCOM_PROP_QC_VOLT		0x2011
#define QCOM_PROP_AICL_ENABLE		0x2012
#define QCOM_PROP_RERUN_AICL		0x2013
#define QCOM_PROP_RESTART_AICL		0x2014
#define QCOM_PROP_AICL_CONT_THD		0x2015
#define QCOM_PROP_OPT_FWS		0x2016
#define QCOM_PROP_ADAPTER_OVERRIDE	0x2017
#define QCOM_PROP_QC3_VOLT		0x2018
#define QCOM_PROP_INPUT_SUSPEND		0x2019
#define QCOM_PROP_OTG_CFG		0x201a
#define QCOM_PROP_CID_CFG		0x201b
#define QCOM_PROP_CP_STATE		0x201c
#define QCOM_PROP_QC_VOLT_CMD		0x201d
#define QCOM_PROP_LPD_SBU1		0x201e
#define QCOM_PROP_LPD_SBU2		0x201f
#define QCOM_PROP_LPD_CC1		0x2020
#define QCOM_PROP_LPD_CC2		0x2021
#define QCOM_PROP_LPD_DP		0x2022
#define QCOM_PROP_LPD_DM		0x2023
#define QCOM_PROP_LPD_CONTROL		0x2024
#define QCOM_PROP_LPD_UART_CONTROL	0x2025
#define QCOM_PROP_PACK_VBAT		0x2026
#define QCOM_PROP_PACK_IBAT		0x2027
#define QCOM_PROP_EU_MODEL		0x2028
#define QCOM_PROP_AICL_STATUS		0x2029
#define QCOM_PROP_PACK_TBAT		0x202a
#define QCOM_PROP_CP_VENDOR_ID		0x202c
#define QCOM_PROP_WLS_VDD_FLAG		0x100c
#define QCOM_PROP_WLS_INPUT_SUSPEND	0x1004
#define QCOM_PROP_WLS_INPUT_CURRENT	0x1003
#define QCOM_PROP_WLS_CURR		0x1007
#define QCOM_PROP_USB_ONLINE		0x20001
#define QCOM_PROP_BUS_VOLT		0x20002
#define QCOM_PROP_USB_SNS_VOLT		0x20003
#define QCOM_PROP_BUS_CURR		0x20004
#define QCOM_PROP_LPD_ENABLE		0x20009
#define QCOM_PROP_LPD_STATUS		0x2000a
#define QCOM_PROP_USB_REAL_TYPE		0x2000c

/* notification ids sent by the ADSP (see qcom_subpmic_notify_cb) */
#define QCOM_NOTIFY_USB_CHANGE		0
#define QCOM_NOTIFY_PLUGIN		2
#define QCOM_NOTIFY_ENABLE_BOOST	3
#define QCOM_NOTIFY_LPD			4
#define QCOM_NOTIFY_PD_STATE		5
#define QCOM_NOTIFY_CC_SHORT_VBUS	6
#define QCOM_NOTIFY_PPS_PTF		7
#define QCOM_NOTIFY_BSINK_SUSPEND	8
#define QCOM_NOTIFY_CP_REVERT		9
#define QCOM_NOTIFY_OTG_CP_CFG		10
#define QCOM_NOTIFY_SBU_LPD		11	/* OS4: new */
#define QCOM_NOTIFY_CID_STS		12	/* OS3: 11 */

/* deferred ADSP notification (allocated in the glink callback, consumed by notify_change_work) */
struct qcom_subpmic_notify_node {
	struct list_head list;
	int type;
	int val;
};

union qcom_otg_data {
	int val;
	struct {
		unsigned int pos : 16;
		unsigned int auth : 16;
	};
};

struct qcom_subpmic {
	struct device *dev;
	struct delayed_work update_usb_type_work;
	struct work_struct notify_change_work;
	bool is_enable_shipmode;
	struct notifier_block ship_mode_nb;
	struct notifier_block shutdown_nb;
	u8 reserved1[0x110];
	struct delayed_work sync_cfg_work;
	struct list_head notify_list;
	spinlock_t notify_lock;
	bool support_2s_charging;
	bool support_dual_panel;
	bool support_multi_bc12;
	bool support_cid;
	u32 cid_gpio_int;
	u8 reserved2[0x34];
	u32 otg_boost_src;
	u32 gpio_chip_type;
	u32 vdd_boost_en;
	u32 otg_ovp_en;
	int usb_online;
	int usb_real_type;
	int glink_down;
	int init_ok;
	int lpd_status;
	int cc_short_vbus;
	int pps_ptf;
	union qcom_otg_data otg_data;
	u8 bsink_suspend;
	u8 eu_model;
	int sbu_lpd;	/* OS4: new (+0x314), cid_status moved to +0x318, kzalloc 0x320 */
	int cid_status;
};

static RAW_NOTIFIER_HEAD(hboost_notifier);

int register_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_register(&hboost_notifier, nb);
}
EXPORT_SYMBOL(register_hboost_event_notifier);

int unregister_hboost_event_notifier(struct notifier_block *nb)
{
	return raw_notifier_chain_unregister(&hboost_notifier, nb);
}
EXPORT_SYMBOL(unregister_hboost_event_notifier);

int qti_battery_charger_get_prop(const char *name, enum battery_charger_prop prop_id, int *val)
{
	if (!val)
		return -EINVAL;

	switch (prop_id) {
	case BATTERY_RESISTANCE:
		*val = 100000;
		break;
	default:
		*val = 0;
		break;
	}

	return 0;
}
EXPORT_SYMBOL(qti_battery_charger_get_prop);

static int qcom_subpmic_get_usb_real_type(int *type, void *data)
{
	struct qcom_subpmic *chip = data;
	int rc;

	rc = mca_adsp_glink_read_prop(QCOM_PROP_USB_REAL_TYPE, type, sizeof(*type));
	if (rc)
		*type = chip->usb_real_type;

	return 0;
}

static int qcom_subpmic_set_qc_volt(void *data, int volt)
{
	struct qcom_subpmic *chip = data;
	int set_volt = volt;
	int vbus = 0;
	int step_mv, step_ms, tol, diff, target, abs_diff;
	unsigned int timeout_ms;
	ktime_t start;
	int rc;

	start = ktime_get_boottime();
	step_mv = chip->usb_real_type == 8 ? 20 : 200;
	step_ms = chip->usb_real_type == 8 ? 5 : 50;
	if (volt < 5000 || volt > 12000) {
#line 584
		mca_log_err("invalid qc voltage: %d\n", volt);
		return -1;
	}

	tol = chip->usb_real_type == 8 ? 500 : 800;
	mca_adsp_glink_read_prop(QCOM_PROP_BUS_VOLT, &vbus, sizeof(vbus));
	vbus /= 1000;
	diff = volt - vbus;
	target = (diff > 0 ? -tol : tol) + volt;
#line 593
	mca_log_info("real_type: %d, volt: %d, vbus: %d\n", chip->usb_real_type, volt, vbus, diff, target);
	rc = mca_adsp_glink_write_prop(QCOM_PROP_QC_VOLT, &set_volt, sizeof(set_volt));
	if (rc)
		return rc;

	if (chip->usb_real_type == 5)
		return 0;
	if (set_volt == 5000)
		return 0;

	abs_diff = abs(diff);
	if (abs_diff < step_mv)
		return 0;

	timeout_ms = abs_diff / step_mv * step_ms;
	while (1) {
		msleep(50);
		mca_adsp_glink_read_prop(QCOM_PROP_BUS_VOLT, &vbus, sizeof(vbus));
		vbus /= 1000;
#line 605
		mca_log_info("vbus: %d\n", vbus);
		if (vbus < 4000 || !chip->usb_online) {
#line 607
			mca_log_err("usb removed, break\n");
			return 0;
		}

		if (diff > 0 && vbus > target)
			return 0;
		if (diff < 0 && vbus < target)
			return 0;

		if (ktime_to_ms(ktime_sub(ktime_get_boottime(), start)) > timeout_ms) {
#line 616
			mca_log_info("wait time out, break\n");
			return 0;
		}
	}
}

static int qcom_subpmic_set_qc_volt_cmd(void *data, int hvdcp_cmd)
{
#line 626
	mca_log_info("set_qc_volt_cmd: %d", hvdcp_cmd);
	return mca_adsp_glink_write_prop(QCOM_PROP_QC_VOLT_CMD, &hvdcp_cmd, sizeof(hvdcp_cmd));
}

static int qcom_subpmic_get_bus_volt(void *data, int *bus_volt)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_BUS_VOLT, bus_volt, sizeof(*bus_volt));
}

static int qcom_subpmic_parse_dt(struct qcom_subpmic *chip)
{
	struct device_node *node = chip->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	chip->support_2s_charging = of_property_read_bool(node, "mi,support-2s-charging");
	chip->support_dual_panel = of_property_read_bool(node, "mi,support-dual-panel");
	chip->support_multi_bc12 = of_property_read_bool(node, "mi,support-multi-bc12");
	chip->support_cid = of_property_read_bool(node, "mi,support-cid");
	mca_parse_dts_u32(node, "otg_boost_src", &chip->otg_boost_src, 2);
	mca_parse_dts_u32(node, "gpio_chip_type", &chip->gpio_chip_type, 1);
	mca_parse_dts_u32(node, "vdd_boost_en", &chip->vdd_boost_en, -1);
	mca_parse_dts_u32(node, "otg_ovp_en", &chip->otg_ovp_en, -1);

	if (!hwid)
		return -ENOMEM;

	if (hwid->platform_version == 1 &&
	    (hwid->build_version == 0 || (hwid->build_version == 1 && hwid->minor_version == 0))) {
		chip->otg_boost_src = 2;
#line 125
		mca_log_err("O2 p1.1 change otg boost from external to haptic\n");
	}

	if (chip->support_cid) {
		mca_parse_dts_u32(node, "cid_gpio_int", &chip->cid_gpio_int, 0);
#line 130
		mca_log_err("dts sync cid_gpio_int= %#x\n", chip->cid_gpio_int);
	}

#line 134
	mca_log_err("dts sync otg_cfg_src= %d,gpio_chip_type=%d, vdd_boost_gpio=%d, ovp_en_gpio=%d\n",
		chip->otg_boost_src, chip->gpio_chip_type, chip->vdd_boost_en, chip->otg_ovp_en);

	return 0;
}

static void qcom_subpmic_update_usb_type_work(struct work_struct *work)
{
	struct qcom_subpmic *chip = container_of(work, struct qcom_subpmic, update_usb_type_work.work);
	int real_type = 0;
	int pd_active = 0;
	int usb_online = 0;
	int rc;

	if (chip->support_multi_bc12) {
		protocol_class_pd_get_pd_active(1, &pd_active);
		if (pd_active)
			return;
	}

	rc = mca_adsp_glink_read_prop(QCOM_PROP_USB_ONLINE, &usb_online, sizeof(usb_online));
	if (rc < 0) {
#line 159
		mca_log_err("Failed to read usb_online rc=%d\n", rc);
		goto retry;
	}

	if (!chip->init_ok) {
		mca_event_block_notify(4, 0x2a, NULL);
		chip->init_ok = 1;
	}

	if (chip->usb_online != usb_online) {
#line 170
		mca_log_err("usb online: %d\n", usb_online);
		chip->usb_online = usb_online;
		if (usb_online) {
			protocol_class_pd_set_pd_active(0, 1);
			mca_event_block_notify(0, 1, NULL);
		} else {
			protocol_class_pd_set_pd_active(0, 0);
			mca_event_block_notify(0, 0, NULL);
			pm_relax(chip->dev);
		}
	}

	rc = mca_adsp_glink_read_prop(QCOM_PROP_USB_REAL_TYPE, &real_type, sizeof(real_type));
	if (rc < 0) {
#line 187
		mca_log_err("Failed to read USB_ADAP_TYPE rc=%d\n", rc);
		goto retry;
	}

	if (chip->usb_real_type != real_type) {
#line 193
		mca_log_err("usb real_type: %d\n", real_type);
		chip->usb_real_type = real_type;
	} else if (!chip->glink_down) {
		return;
	}

	chip->glink_down = 0;
	mca_event_block_notify(1, 5, &chip->usb_real_type);
	return;

retry:
	queue_delayed_work(system_wq, &chip->update_usb_type_work, msecs_to_jiffies(2000));
}

static void qcom_subpmic_sync_cfg_work(struct work_struct *work)
{
	struct qcom_subpmic *chip = container_of(work, struct qcom_subpmic, sync_cfg_work.work);
	static int count;
	bool cp_present = false;
	int rc;

	rc = platform_class_cp_get_present(0, &cp_present);
	if (rc) {
#line 214
		mca_log_err("Failed to get cp state, rc=%d\n", rc);
		goto retry;
	}

	rc = mca_adsp_glink_write_prop(QCOM_PROP_CP_STATE, &cp_present, sizeof(cp_present));
	if (rc) {
#line 219
		mca_log_err("Failed to send cp state, rc=%d\n", rc);
		goto retry;
	}
#line 222
	mca_log_err("Success to send cp state, rc=%d\n", rc);

	rc = mca_adsp_glink_write_prop(QCOM_PROP_OTG_CFG, &chip->otg_boost_src, 16);
	if (rc) {
#line 229
		mca_log_err("Failed to send otg cfg, rc=%d\n", rc);
		goto retry;
	}
#line 232
	mca_log_err("Success to send otg cfg, rc=%d\n", rc);

	if (chip->support_cid) {
		rc = mca_adsp_glink_write_prop(QCOM_PROP_CID_CFG, &chip->cid_gpio_int, sizeof(chip->cid_gpio_int));
		if (rc) {
#line 241
			mca_log_err("Failed to send cid cfg, rc=%d\n", rc);
			goto retry;
		}
#line 244
		mca_log_err("Success to send cid cfg, rc=%d\n", rc);

		rc = mca_adsp_glink_write_prop(QCOM_PROP_EU_MODEL, &chip->eu_model, sizeof(chip->eu_model));
		if (rc) {
#line 250
			mca_log_err("Failed to send eu model, rc=%d\n", rc);
			goto retry;
		}
#line 253
		mca_log_err("Success to send eu model, rc=%d\n", rc);
	}
	return;

retry:
	if (count++ < 60)
		queue_delayed_work(system_wq, &chip->sync_cfg_work, msecs_to_jiffies(1000));
}

static void qcom_subpmic_notify_change_work(struct work_struct *work)
{
	struct qcom_subpmic *chip = container_of(work, struct qcom_subpmic, notify_change_work);
	struct qcom_subpmic_notify_node *node, *tmp;

	while (!list_empty(&chip->notify_list)) {
		spin_lock(&chip->notify_lock);
		list_for_each_entry_safe(node, tmp, &chip->notify_list, list) {
			list_del(&node->list);
			spin_unlock(&chip->notify_lock);
			switch (node->type) {
			case QCOM_NOTIFY_PLUGIN:
				mca_event_block_notify(1, 6, NULL);
				break;
			case QCOM_NOTIFY_ENABLE_BOOST: {
				union qcom_otg_data *od = &chip->otg_data;
				int val;

				mca_event_block_notify(4, 0x2f, &node->val);
				val = node->val;
				if (chip->otg_boost_src) {
					bool ovp_en = val == 0;

#line 367
					mca_log_err("enable_ovpgate: %d\n", ovp_en);
					platform_class_cp_enable_ovpgate_with_check(0, 0, ovp_en);
					if (val == 0) {
						od->auth = 0x100;
						mca_event_block_notify(4, 0x2e, od);
						od->val = 0;
					}
				} else {
					if (val == 0)
						od->val = 0;
					mca_event_block_notify(4, 0x2e, od);
				}
				break;
			}
			case QCOM_NOTIFY_PD_STATE:
				mca_event_block_notify(8, 0x42, NULL);
				break;
			default:
				break;
			}
			kfree(node);	/* OS4: the node is freed now (OS3 leaked it) */
			spin_lock(&chip->notify_lock);
		}
		spin_unlock(&chip->notify_lock);
	}
}

static int qcom_subpmic_ship_mode(struct notifier_block *nb, unsigned long action, void *data)
{
	struct qcom_subpmic *chip = container_of(nb, struct qcom_subpmic, ship_mode_nb);
	unsigned long cmd = action;
	int chip_vendor = -1;
	int rc;

	if (action == SYS_DOWN || action == SYS_POWER_OFF) {
		if (chip->is_enable_shipmode) {
#line 309
			mca_log_info("set adsp shipmode\n");
			platform_class_cp_get_chip_vendor(0, &chip_vendor);
#line 311
			mca_log_info("cp type: %d\n", chip_vendor);
			if (chip_vendor != 0 && chip_vendor != 4)
				platform_class_cp_enable_ovpgate(0, false);
			rc = mca_adsp_glink_write_prop(QCOM_PROP_SHIP_MODE, &cmd, sizeof(cmd));
			if (rc < 0)
				pr_emerg("Failed to write ship mode: %d\n", rc);
		}
	}

	return 0;
}

static int qcom_subpmic_shutdown_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	unsigned long cmd = action;
	int rc;

	if (action == SYS_DOWN || action == SYS_POWER_OFF) {
#line 328
		mca_log_info("start adsp shutdown\n");
		rc = mca_adsp_glink_write_prop(QCOM_PROP_SHUTDOWN, &cmd, sizeof(cmd));
		if (rc < 0)
			pr_emerg("Failed to write shutdown cmd to adsp: %d\n", rc);
	}

	return 0;
}

static int qcom_subpmic_bc12_det_en(int en, void *data)
{
	return 0;
}

static int qcom_subpmic_is_init_ok(void *data)
{
	struct qcom_subpmic *chip = data;

	if (!chip)
		return -1;

	return chip->init_ok ? chip->init_ok : -1;
}

static int qcom_subpmic_get_online(void *data, int *online)
{
	struct qcom_subpmic *chip = data;

	*online = chip->usb_online;
	return 0;
}

static int qcom_subpmic_is_charge_done(void *data, bool *charge_done)
{
	int status = 0;
	int rc;

	rc = mca_adsp_glink_read_prop(QCOM_PROP_CHG_STATUS, &status, sizeof(status));
	if (rc < 0)
		return rc;

	*charge_done = status == 5;
	return 0;
}

static int qcom_subpmic_get_input_current_limit(void *data, int *curr)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_INPUT_CURRENT_LIMIT, curr, sizeof(*curr));
}

static int qcom_subpmic_get_bus_curr(void *data, int *bus_curr)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_BUS_CURR, bus_curr, sizeof(*bus_curr));
}

static int qcom_subpmic_get_usb_sns_volt(void *data, int *bus_volt)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_USB_SNS_VOLT, bus_volt, sizeof(*bus_volt));
}

static int qcom_subpmic_get_vsys_volt(void *data, int *vsys_min)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_VSYS_VOLT, vsys_min, sizeof(*vsys_min));
}

static int qcom_subpmic_get_chg_status(void *data, int *chg_status)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_CHG_STATUS, chg_status, sizeof(*chg_status));
}

static int qcom_subpmic_get_chg_type(void *data, int *chg_type)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_CHG_TYPE, chg_type, sizeof(*chg_type));
}

static int qcom_subpmic_get_term_current(void *data, int *term_curr)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_TERM_CURRENT, term_curr, sizeof(*term_curr));
}

static int qcom_subpmic_get_term_volt(void *data, int *term_volt)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_TERM_VOLT, term_volt, sizeof(*term_volt));
}

static int qcom_subpmic_get_wls_curr(void *data, int *wls_curr)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_WLS_CURR, wls_curr, sizeof(*wls_curr));
}

static int qcom_subpmic_set_input_suspend(void *data, bool suspend)
{
#line 658
	mca_log_info("set input suspend %d\n", suspend);
	return mca_adsp_glink_write_prop(QCOM_PROP_INPUT_SUSPEND, &suspend, sizeof(suspend));
}

static int qcom_subpmic_set_wireless_input_suspend(void *data, bool suspend)
{
#line 664
	mca_log_info("set wls input suspend %d\n", suspend);
	return mca_adsp_glink_write_prop(QCOM_PROP_WLS_INPUT_SUSPEND, &suspend, sizeof(suspend));
}

static int qcom_subpmic_set_input_current_limit(void *data, int curr)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_INPUT_CURRENT_LIMIT, &curr, sizeof(curr));
}

static int qcom_subpmic_set_wls_input_current_limit(void *data, int curr)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_WLS_INPUT_CURRENT, &curr, sizeof(curr));
}

static int qcom_subpmic_set_input_volt_limit(void *data, int volt)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_INPUT_VOLT_LIMIT, &volt, sizeof(volt));
}

static int qcom_subpmic_set_charge_current(void *data, int curr)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_CHARGE_CURRENT, &curr, sizeof(curr));
}

static int qcom_subpmic_enable_charging(void *data, bool en)
{
#line 470
	mca_log_info("enable charging: %d\n", en);
	return mca_adsp_glink_write_prop(QCOM_PROP_ENABLE_CHARGING, &en, sizeof(en));
}

static int qcom_subpmic_set_buck_fsw(void *data, int fsw)
{
#line 476
	mca_log_info("set buck fsw: %d\n", fsw);
	return mca_adsp_glink_write_prop(QCOM_PROP_BUCK_FSW, &fsw, sizeof(fsw));
}

static int qcom_subpmic_set_term_current(void *data, int curr)
{
#line 487
	mca_log_info("set iterm: %d\n", curr);
	return mca_adsp_glink_write_prop(QCOM_PROP_TERM_CURRENT, &curr, sizeof(curr));
}

static int qcom_subpmic_set_term_volt(void *data, int volt)
{
#line 498
	mca_log_info("set vterm: %d\n", volt);
	return mca_adsp_glink_write_prop(QCOM_PROP_TERM_VOLT, &volt, sizeof(volt));
}

static int qcom_subpmic_set_prechg_volt(void *data, int volt)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_PRECHG_VOLT, &volt, sizeof(volt));
}

static int qcom_subpmic_set_prechg_current(void *data, int curr)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_PRECHG_CURRENT, &curr, sizeof(curr));
}

static int qcom_subpmic_set_usb_aicl_cont_thd(void *data, int thd)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_AICL_CONT_THD, &thd, sizeof(thd));
}

static int qcom_subpmic_get_usb_aicl_cont_thd(void *data, int *thd)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_AICL_CONT_THD, thd, sizeof(*thd));
}

static int qcom_subpmic_set_opt_fws(void *data, int fws)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_OPT_FWS, &fws, sizeof(fws));
}

static int qcom_subpmic_usb_adapter_allow_override(void *data, bool allow)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_ADAPTER_OVERRIDE, &allow, sizeof(allow));
}

static int qcom_subpmic_set_qc3_volt(void *data, int volt)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_QC3_VOLT, &volt, sizeof(volt));
}

static int qcom_subpmic_get_otg_boost_src(void *data, int *otg_boost_src)
{
	struct qcom_subpmic *chip = data;

	*otg_boost_src = chip->otg_boost_src;
#line 673
	mca_log_info("get otg_boost_src: %d\n", *otg_boost_src);
	return chip->otg_boost_src;
}

static int qcom_subpmic_get_otg_gate_enable_status(void *data, int *otg_gate_enable_status)
{
#line 685
	mca_log_info("get otg plugin status\n");
	return mca_adsp_glink_read_prop(QCOM_PROP_OTG_GATE_ENABLE, otg_gate_enable_status, sizeof(*otg_gate_enable_status));
}

static int qcom_subpmic_get_otg_boost_enable_status(void *data, int *otg_boost_enable_sts)
{
#line 679
	mca_log_info("get otg enable status\n");
	return mca_adsp_glink_read_prop(QCOM_PROP_OTG_BOOST_ENABLE, otg_boost_enable_sts, sizeof(*otg_boost_enable_sts));
}

static int qcom_subpmic_set_boost_enable(void *data, int src_enable)
{
#line 691
	mca_log_info("set otg enable: 0x%2x\n", src_enable);
	return mca_adsp_glink_write_prop(QCOM_PROP_BOOST_ENABLE, &src_enable, sizeof(src_enable));
}

static int qcom_subpmic_set_boost_voltage(void *data, int src_voltage)
{
#line 697
	mca_log_info("set boost voltage: 0x%2x\n", src_voltage);
	return mca_adsp_glink_write_prop(QCOM_PROP_BOOST_VOLTAGE, &src_voltage, sizeof(src_voltage));
}

static int qcom_subpmic_set_aicl_enable(void *data, bool en)
{
#line 703
	mca_log_info("set aicl enable: %d\n", en);
	return mca_adsp_glink_write_prop(QCOM_PROP_AICL_ENABLE, &en, sizeof(en));
}

static int qcom_subpmic_set_rerun_aicl(void *data, bool en)
{
#line 709
	mca_log_info("set rerun aicl: %d\n", en);
	return mca_adsp_glink_write_prop(QCOM_PROP_RERUN_AICL, &en, sizeof(en));
}

static int qcom_subpmic_set_restart_aicl(void *data, bool en)
{
#line 715
	mca_log_info("set restart aicl: %d\n", en);
	return mca_adsp_glink_write_prop(QCOM_PROP_RESTART_AICL, &en, sizeof(en));
}

static int qcom_subpmic_is_support_cid(void *data, bool *support_cid)
{
	struct qcom_subpmic *chip = data;

	*support_cid = chip->support_cid;
	return 0;
}

static int qcom_subpmic_set_ship_mode(void *data, bool en)
{
	struct qcom_subpmic *chip = data;

	chip->is_enable_shipmode = en;
#line 285
	mca_log_err("is_enable_shipmode = %d\n", en);
	return 0;
}

static int qcom_subpmic_get_ship_mode(void *data, bool *en)
{
	struct qcom_subpmic *chip = data;

	*en = chip->is_enable_shipmode;
	return 0;
}

static int qcom_subpmic_set_wls_vdd_flag(void *data, bool en)
{
#line 721
	mca_log_info("set wls vdd flag: %d\n", en);
	return mca_adsp_glink_write_prop(QCOM_PROP_WLS_VDD_FLAG, &en, sizeof(en));
}

static int qcom_subpmic_get_lpd_enable(void *data, int *lpd_en)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_ENABLE, lpd_en, sizeof(*lpd_en));
}

static int qcom_subpmic_get_lpd_status(void *data, int *lpd_bliquid)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_STATUS, lpd_bliquid, sizeof(*lpd_bliquid));
}

static int qcom_subpmic_get_lpd_sbu1(void *data, int *lpd_sbu1)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_SBU1, lpd_sbu1, sizeof(*lpd_sbu1));
}

static int qcom_subpmic_get_lpd_sbu2(void *data, int *lpd_sbu2)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_SBU2, lpd_sbu2, sizeof(*lpd_sbu2));
}

static int qcom_subpmic_get_lpd_cc1(void *data, int *lpd_cc1)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_CC1, lpd_cc1, sizeof(*lpd_cc1));
}

static int qcom_subpmic_get_lpd_cc2(void *data, int *lpd_cc2)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_CC2, lpd_cc2, sizeof(*lpd_cc2));
}

static int qcom_subpmic_get_lpd_dp(void *data, int *lpd_dp)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_DP, lpd_dp, sizeof(*lpd_dp));
}

static int qcom_subpmic_get_lpd_dm(void *data, int *lpd_dm)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_DM, lpd_dm, sizeof(*lpd_dm));
}

static int qcom_subpmic_set_lpd_sbu1(void *data, int lpd_sbu1)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_LPD_SBU1, &lpd_sbu1, sizeof(lpd_sbu1));
}

static int qcom_subpmic_set_lpd_control(void *data, int lpd_control)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_LPD_CONTROL, &lpd_control, sizeof(lpd_control));
}

static int qcom_subpmic_get_lpd_control(void *data, int *lpd_control)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_CONTROL, lpd_control, sizeof(*lpd_control));
}

static int qcom_subpmic_set_lpd_uart_control(void *data, int lpd_uart_control)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_LPD_UART_CONTROL, &lpd_uart_control, sizeof(lpd_uart_control));
}

static int qcom_subpmic_get_lpd_uart_control(void *data, int *lpd_uart_control)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_LPD_UART_CONTROL, lpd_uart_control, sizeof(*lpd_uart_control));
}

static int qcom_subpmic_get_pack_vbat(void *data, int *pvbat)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_PACK_VBAT, pvbat, sizeof(*pvbat));
}

static int qcom_subpmic_get_pack_ibat(void *data, int *pibat)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_PACK_IBAT, pibat, sizeof(*pibat));
}

static int qcom_subpmic_set_eu_model(void *data, bool en)
{
	struct qcom_subpmic *chip = data;
	bool eu_model = en;
	int rc;

	rc = mca_adsp_glink_write_prop(QCOM_PROP_EU_MODEL, &eu_model, sizeof(eu_model));
	if (rc < 0)
#line 272
		mca_log_err("Failed to write eu model to adsp: %d\n", rc);
	chip->eu_model = eu_model;
#line 274
	mca_log_info("set_eu_model = %d\n", eu_model);
	return 0;
}

static int qcom_subpmic_get_aicl_status(void *data, int *aicl_status)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_AICL_STATUS, aicl_status, sizeof(*aicl_status));
}

static int qcom_subpmic_set_too_hot_limit(void *data, int limit)
{
	return mca_adsp_glink_write_prop(QCOM_PROP_TOO_HOT_LIMIT, &limit, sizeof(limit));
}

static int qcom_subpmic_get_pack_tbat(void *data, int *ptbat)
{
	return mca_adsp_glink_read_prop(QCOM_PROP_PACK_TBAT, ptbat, sizeof(*ptbat));
}

static int qcom_subpmic_set_cp_vendor_id(void *data, int *cp_vendor_id)
{
#line 559
	mca_log_info("set_cp_vendor_id = %d\n", *cp_vendor_id);
	return mca_adsp_glink_write_prop(QCOM_PROP_CP_VENDOR_ID, cp_vendor_id, 8);
}

static void moisture_detection_uevent(int moisture_sts)
{
	char uevent_buf[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data n_data = { 0 };

#line 851
	mca_log_info("moisture detection uevent notify\n");
	n_data.event_len = snprintf(uevent_buf, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_MOISTURE_DET_STS=%d", moisture_sts);
	n_data.event = uevent_buf;
	mca_event_report_uevent(&n_data);
}

static void cc_short_vbus_uevent(int cc_short_vbus)
{
	char uevent_buf[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data n_data = { 0 };

#line 866
	mca_log_info("cc short vbus uevent notify\n");
	n_data.event_len = snprintf(uevent_buf, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_CC_SHORT_VBUS=%d", cc_short_vbus);
	n_data.event = uevent_buf;
	mca_event_report_uevent(&n_data);
}

static void qcom_subpmic_glink_down_cb(void *priv)
{
	struct qcom_subpmic *chip = priv;

#line 828
	mca_log_info("glink down\n");
	chip->glink_down = 1;
	mca_event_block_notify(5, 0x32, NULL);
}

static void qcom_subpmic_sync_cb(void *priv)
{
	struct qcom_subpmic *chip = priv;

#line 838
	mca_log_info("glink up\n");
	mca_event_block_notify(5, 0x33, NULL);
	queue_delayed_work(system_wq, &chip->update_usb_type_work, 0);
	queue_delayed_work(system_wq, &chip->sync_cfg_work, 0);
}

static void qcom_subpmic_notify_cb(int notify_id, void *data, int len, void *priv)
{
	struct qcom_subpmic *chip = priv;
	struct qcom_subpmic_notify_node *node;
	int *val = data;

	if (!chip)
		return;

	pm_wakeup_dev_event(chip->dev, 1000, true);

	switch (notify_id) {
	case QCOM_NOTIFY_USB_CHANGE:
		queue_delayed_work(system_wq, &chip->update_usb_type_work, 0);
		break;
	case QCOM_NOTIFY_PLUGIN:
	case QCOM_NOTIFY_ENABLE_BOOST:
	case QCOM_NOTIFY_PD_STATE:
		node = kmalloc(sizeof(*node), GFP_ATOMIC);
		if (!node)
			return;
		node->type = notify_id;
		if (notify_id == QCOM_NOTIFY_ENABLE_BOOST) {
			node->val = *val;
#line 900
			mca_log_err("recv enable boost notify: %d\n", *val);
		}
		spin_lock(&chip->notify_lock);
		list_add_tail(&node->list, &chip->notify_list);
		spin_unlock(&chip->notify_lock);
		schedule_work(&chip->notify_change_work);
		break;
	case QCOM_NOTIFY_LPD:
		chip->lpd_status = *val;
#line 916
		mca_log_err("recv adsp report lpd status: %d\n", chip->lpd_status);
		moisture_detection_uevent(chip->lpd_status);
		mca_event_block_notify(4, 0x29, &chip->lpd_status);
		break;
	case QCOM_NOTIFY_CC_SHORT_VBUS:
		chip->cc_short_vbus = *val;
#line 922
		mca_log_err("recv cc short vbus notify: %d\n", chip->cc_short_vbus);
		cc_short_vbus_uevent(chip->cc_short_vbus);
		mca_event_block_notify(4, 0x2b, &chip->cc_short_vbus);
		break;
	case QCOM_NOTIFY_PPS_PTF:
		chip->pps_ptf = *val;
#line 928
		mca_log_err("recv pps ptf notify: %d\n", chip->pps_ptf);
		mca_strategy_func_process(1, 0x37, chip->pps_ptf);
		mca_strategy_func_process(0, 0x37, chip->pps_ptf);
		break;
	case QCOM_NOTIFY_BSINK_SUSPEND:
		if (!chip->eu_model)
			break;
#line 909
		mca_log_err("recv bsinkpowersuspend change\n");
		chip->bsink_suspend = *(u8 *)data;
		mca_event_block_notify(1, 0x45, &chip->bsink_suspend);
		break;
	case QCOM_NOTIFY_CP_REVERT:
		chip->otg_data.val = *val;
#line 934
		mca_log_info("recv cp revert notify");
		mca_event_block_notify(4, 0x2e, &chip->otg_data);
		break;
	case QCOM_NOTIFY_OTG_CP_CFG:
#line 938
		mca_log_info("recv otg cp config notify: %d", *val);
		platform_class_cp_set_revchg(0, *val != 0);
		break;
	case QCOM_NOTIFY_SBU_LPD:
		chip->sbu_lpd = *val;
#line 943
		mca_log_info("recv sbu_lpd: %d", *val);
		mca_event_block_notify(4, 0x53, &chip->sbu_lpd);
		break;
	case QCOM_NOTIFY_CID_STS:
		chip->cid_status = *val;
#line 948
		mca_log_info("recv cid_sts: %d", *val);
		mca_event_block_notify(4, 0x30, &chip->cid_status);
		break;
	default:
		break;
	}
}

static struct platform_class_buckchg_ops g_qcom_buckchg_ops = {
	.is_init_ok = qcom_subpmic_is_init_ok,
	.get_online = qcom_subpmic_get_online,
	.is_charge_done = qcom_subpmic_is_charge_done,
	.get_input_curr_lmt = qcom_subpmic_get_input_current_limit,
	.get_bus_curr = qcom_subpmic_get_bus_curr,
	.get_bus_volt = qcom_subpmic_get_bus_volt,
	.get_usb_sns_volt = qcom_subpmic_get_usb_sns_volt,
	.get_sys_volt = qcom_subpmic_get_vsys_volt,
	.get_chg_status = qcom_subpmic_get_chg_status,
	.get_chg_type = qcom_subpmic_get_chg_type,
	.get_term_curr = qcom_subpmic_get_term_current,
	.get_term_volt = qcom_subpmic_get_term_volt,
	.get_wls_curr = qcom_subpmic_get_wls_curr,
	.set_hiz = qcom_subpmic_set_input_suspend,
	.set_wls_hiz = qcom_subpmic_set_wireless_input_suspend,
	.set_input_curr_lmt = qcom_subpmic_set_input_current_limit,
	.set_wls_input_curr_lmt = qcom_subpmic_set_wls_input_current_limit,
	.set_input_volt_lmt = qcom_subpmic_set_input_volt_limit,
	.set_ichg = qcom_subpmic_set_charge_current,
	.set_chg = qcom_subpmic_enable_charging,
	.set_buck_fsw = qcom_subpmic_set_buck_fsw,
	.set_term_curr = qcom_subpmic_set_term_current,
	.set_term_volt = qcom_subpmic_set_term_volt,
	.set_prechg_volt = qcom_subpmic_set_prechg_volt,
	.set_prechg_curr = qcom_subpmic_set_prechg_current,
	.set_qc_volt = qcom_subpmic_set_qc_volt,
	.set_usb_aicl_cont_thd = qcom_subpmic_set_usb_aicl_cont_thd,
	.get_usb_aicl_cont_thd = qcom_subpmic_get_usb_aicl_cont_thd,
	.set_opt_fws = qcom_subpmic_set_opt_fws,
	.usb_adapter_allow_override = qcom_subpmic_usb_adapter_allow_override,
	.set_qc3_volt = qcom_subpmic_set_qc3_volt,
	.get_otg_boost_src = qcom_subpmic_get_otg_boost_src,
	.get_otg_gate_enable_status = qcom_subpmic_get_otg_gate_enable_status,
	.get_otg_boost_enable_status = qcom_subpmic_get_otg_boost_enable_status,
	.set_boost_enable = qcom_subpmic_set_boost_enable,
	.set_boost_voltage = qcom_subpmic_set_boost_voltage,
	.set_aicl_enable = qcom_subpmic_set_aicl_enable,
	.set_rerun_aicl = qcom_subpmic_set_rerun_aicl,
	.set_restart_aicl = qcom_subpmic_set_restart_aicl,
	.is_support_cid = qcom_subpmic_is_support_cid,
	.set_ship_mode = qcom_subpmic_set_ship_mode,
	.get_ship_mode = qcom_subpmic_get_ship_mode,
	.set_wls_vdd_flag = qcom_subpmic_set_wls_vdd_flag,
	.get_lpd_enable = qcom_subpmic_get_lpd_enable,
	.get_lpd_status = qcom_subpmic_get_lpd_status,
	.get_lpd_sbu1 = qcom_subpmic_get_lpd_sbu1,
	.get_lpd_sbu2 = qcom_subpmic_get_lpd_sbu2,
	.get_lpd_cc1 = qcom_subpmic_get_lpd_cc1,
	.get_lpd_cc2 = qcom_subpmic_get_lpd_cc2,
	.get_lpd_dp = qcom_subpmic_get_lpd_dp,
	.get_lpd_dm = qcom_subpmic_get_lpd_dm,
	.set_lpd_sbu1 = qcom_subpmic_set_lpd_sbu1,
	.set_lpd_control = qcom_subpmic_set_lpd_control,
	.get_lpd_control = qcom_subpmic_get_lpd_control,
	.set_lpd_uart_control = qcom_subpmic_set_lpd_uart_control,
	.get_lpd_uart_control = qcom_subpmic_get_lpd_uart_control,
	.get_pack_vbat = qcom_subpmic_get_pack_vbat,
	.get_pack_ibat = qcom_subpmic_get_pack_ibat,
	.set_eu_model = qcom_subpmic_set_eu_model,
	.get_aicl_status = qcom_subpmic_get_aicl_status,
	.set_too_hot_limit = qcom_subpmic_set_too_hot_limit,
	.get_pack_tbat = qcom_subpmic_get_pack_tbat,
	.set_cp_vendor_id = qcom_subpmic_set_cp_vendor_id,
};

static struct platform_bc12_class_ops g_qcom_pmic_bc12_ops = {
	.bc12_det_en = qcom_subpmic_bc12_det_en,
	.get_charge_type = qcom_subpmic_get_usb_real_type,
};

struct protocol_class_qc_ops g_adsp_qc_ops = {
	.protocol_qc_get_qc_type = qcom_subpmic_get_usb_real_type,
	.protocol_qc_set_volt = qcom_subpmic_set_qc_volt,
	.protocol_qc_set_volt_cmd = qcom_subpmic_set_qc_volt_cmd,
};

static struct mca_adsp_call_back g_qcom_subpmic_glink_cb = {
	.glink_down_cb = qcom_subpmic_glink_down_cb,
	.sync_data_cb = qcom_subpmic_sync_cb,
	.notify_cb = qcom_subpmic_notify_cb,
};

static int qcom_subpmic_probe(struct platform_device *pdev)
{
	struct qcom_subpmic *chip;
	int ret;

#line 1057
	mca_log_info("probe start\n");

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	ret = qcom_subpmic_parse_dt(chip);
	if (ret) {
#line 1066
		mca_log_err("%s parse dt fail\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&chip->notify_list);
	spin_lock_init(&chip->notify_lock);
	INIT_DELAYED_WORK(&chip->update_usb_type_work, qcom_subpmic_update_usb_type_work);
	INIT_DELAYED_WORK(&chip->sync_cfg_work, qcom_subpmic_sync_cfg_work);
	INIT_WORK(&chip->notify_change_work, qcom_subpmic_notify_change_work);

	chip->ship_mode_nb.notifier_call = qcom_subpmic_ship_mode;
	chip->ship_mode_nb.priority = 255;
	register_reboot_notifier(&chip->ship_mode_nb);
	chip->shutdown_nb.notifier_call = qcom_subpmic_shutdown_cb;
	chip->shutdown_nb.priority = 255;
	register_reboot_notifier(&chip->shutdown_nb);

	device_init_wakeup(chip->dev, true);
	platform_set_drvdata(pdev, chip);

	ret = platform_bc12_class_ops_register(0, &g_qcom_pmic_bc12_ops, chip);
	if (ret) {
#line 1092
		mca_log_err("%s register protocol ops fail\n", __func__);
		goto err;
	}

	ret = protocol_class_qc_register_ops(0, &g_adsp_qc_ops, chip);
	if (ret) {
#line 1097
		mca_log_err("%s register qc ops fail\n", __func__);
		goto err;
	}

	ret = platform_class_buckchg_ops_register(0, chip, &g_qcom_buckchg_ops);
	if (ret) {
#line 1103
		mca_log_err("%s register buckchg ops fail\n", __func__);
		goto err;
	}

	ret = mca_adsp_glink_resister_ops(&g_qcom_subpmic_glink_cb, chip);
	if (ret) {
#line 1108
		mca_log_err("%s register glink ops fail\n", __func__);
		goto err;
	}

	queue_delayed_work(system_wq, &chip->update_usb_type_work, 0);
	queue_delayed_work(system_wq, &chip->sync_cfg_work, msecs_to_jiffies(100));
#line 1114
	mca_log_err("probe ok\n");
	return 0;

err:
	unregister_reboot_notifier(&chip->ship_mode_nb);
	unregister_reboot_notifier(&chip->shutdown_nb);
	return ret;
}

static int qcom_subpmic_remove(struct platform_device *pdev)
{
	return 0;
}

static void qcom_subpmic_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{ .compatible = "mca,qcom_subpmic" },
	{},
};

static struct platform_driver qcom_subpmic_driver = {
	.driver = {
		.name = "qcom_subpmic",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = qcom_subpmic_probe,
	.remove = qcom_subpmic_remove,
	.shutdown = qcom_subpmic_shutdown,
};
module_platform_driver(qcom_subpmic_driver);

MODULE_DESCRIPTION("qcom subpmic  driver");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
