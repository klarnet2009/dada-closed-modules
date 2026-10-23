// SPDX-License-Identifier: GPL-2.0
/*
 * mca_basic_wireless.c
 *
 * basic wireless class
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include "inc/mca_basic_wireless.h"
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_adsp_glink.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_wireless_class.h>
#include <mca/platform/platform_wireless_class.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_cp_class.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_hwid.h>
#include <mca/smartchg/smart_chg_class.h>
#include <mca/protocol/protocol_pd_class.h>
//#include "hwid.h"
#include <mca/common/mca_workqueue.h>
#include <mca/strategy/mca_wireless_revchg.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_basic_wireless"
#endif

static void strategy_wireless_rx_fastcharge_work(struct work_struct *work);
static void strategy_wireless_mutex_unlock_work(struct work_struct *work);
static void strategy_wireless_sw_cv_stop(struct strategy_wireless_dev *info);

static struct strategy_wireless_dev *g_basic_wls_info;

static int strategy_wireless_input_suspend(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;
	int ret;

	if (!data)
		return -1;

	ret = platform_class_buckchg_ops_set_wls_hiz(MAIN_BUCK_CHARGER, effective_result);
	if (info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_wls_hiz(AUX_BUCK_CHARGER, effective_result);

#line 104
	mca_log_info("set wls hiz %d ret %d\n", effective_result, ret);

	return ret;
}

static int strategy_wireless_set_rx_vout(struct strategy_wireless_dev *info, int vout)
{
	/* OS4: no higher RX voltage while the bridge is being switched */
	if (info->force_vout_6v || info->in_switch_bridge)
		return platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, 6000);
	return platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, vout);
}

static int strategy_wireless_if_get_input_suspend(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_wireless_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->wls_input_suspend_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->wls_input_suspend_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_wireless_if_set_input_suspend(const char *user,
	char *value, void *data)
{
	int temp_value = 0;
	int suspend;
	struct strategy_wireless_dev *info = data;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

#line 148
	mca_log_err("set_wls_input_suspend: %d\n", temp_value);
	suspend = temp_value != 0;
	(void)mca_vote(info->wls_input_suspend_voter, user, true, suspend);
	(void)mca_vote(info->input_limit_voter, "wls_input_suspend", suspend, 0);	/* OS4: new second vote */

	return 0;
}

static int strategy_wireless_if_get_chg_cur(char *buf, void *data)
{
	const char *client_str;
	int value;
	struct strategy_wireless_dev *info = data;

	if (!buf || !data)
		return -1;

	client_str = mca_get_effective_client(info->charge_limit_voter);
	if (!client_str)
		return -1;
	value = mca_get_effective_result(info->charge_limit_voter);

	scnprintf(buf, MCA_CHARGE_IF_MAX_VALUE_BUFF, "eff_client:%s %d",
		client_str, value);

	return 0;
}

static int strategy_wireless_if_set_chg_cur(const char *user,
	char *value, void *data)
{
	int temp_value = 0;
	struct strategy_wireless_dev *info = data;

	if (!user || !value || !data)
		return -1;

	if (kstrtoint(value, 0, &temp_value))
		return -1;

#line 192
	mca_log_err("set_wls_charge_current_limit: %d\n", temp_value);
	if (temp_value)
		(void)mca_vote(info->charge_limit_voter, user, true, temp_value);
	else
		(void)mca_vote(info->charge_limit_voter, user, false, 0);

	return 0;
}

static struct mca_charge_if_ops g_strategy_wireless_if_ops = {
	.type_name = "wl_buck",
	.set_input_suspend = strategy_wireless_if_set_input_suspend,
	.get_input_suspend = strategy_wireless_if_get_input_suspend,
	.set_charge_current_limit = strategy_wireless_if_set_chg_cur,
	.get_charge_current_limit = strategy_wireless_if_get_chg_cur,
};

static int strategy_wireless_get_sys_op_mode(enum sys_op_mode *sys_mode_type)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;

	if (!sys_mode_type)
		return -1;

	if (info->proc_data.epp)
		*sys_mode_type = SYS_OP_MODE_EPP;
	else
		*sys_mode_type = SYS_OP_MODE_BPP;
#line 220
	mca_log_info("%d\n", *sys_mode_type);
	return 0;
}

static int strategy_wireless_get_compatible_info(struct strategy_wireless_dev *info)
{
	int i;

#line 228
	mca_log_info("uuid is %08x\n", info->proc_data.uuid_value);

	for (i = 0; i < info->uuid_adapter_info->uuid_para_size; i++) {
		if (info->proc_data.uuid_value == info->uuid_adapter_info[i].uuid)
			break;
	}

	if (i < info->uuid_adapter_info->uuid_para_size) {
#line 232
		mca_log_info("tx of xm redmi or zmi, i = %d\n", i);
		return i;
	}

#line 237
	mca_log_info("current adapter not xm or zmi\n");
	return -1;	/* OS4 stock: -1 (the XRING template returned EXTERNAL_ADAPTER_DEFAULT) */
}

#define SMARTCHG_WLS_QUIET_FAN_SPEED 5
#define SMARTCHG_WLS_NORMAL_FAN_SPEED 8
static int strategy_wireless_quiet_sts_func(struct strategy_wireless_dev *info)
{
	int num;

	if (!info->online)
		return -1;

	num = strategy_wireless_get_compatible_info(info);
	if (num == -1 ||
		!info->uuid_adapter_info[num].compatible_info.support_fan) {
#line 253
		mca_log_err("cannot support set fan speed\n");
		return -1;
	}

	info->mutex_lock_sts = true;
	if (info->quiet_sts) {
#line 259
		mca_log_info("enter wireless quiet charge mode, set tx to 0.%d speed\n", SMARTCHG_WLS_QUIET_FAN_SPEED);
		platform_class_wireless_set_tx_fan_speed(WIRELESS_ROLE_MASTER, SMARTCHG_WLS_QUIET_FAN_SPEED);
	} else {
#line 262
		mca_log_info("exit wireless quiet charge mode, set tx to 0.%d speed\n", SMARTCHG_WLS_NORMAL_FAN_SPEED);
		platform_class_wireless_set_tx_fan_speed(WIRELESS_ROLE_MASTER, SMARTCHG_WLS_NORMAL_FAN_SPEED);
	}
	schedule_delayed_work(&info->mutex_unlock_work, msecs_to_jiffies(3000));
	return 0;
}

int strategy_class_wireless_ops_get_adapter_power(struct wls_adapter_power_cap *adapter_power)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;

	if (!info)
		return -1;

	memcpy(adapter_power, &info->proc_data.wireless_power, sizeof(struct wls_adapter_power_cap));

	return 0;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_adapter_power);

int strategy_class_wireless_ops_set_parallel_charge(bool parallel_charge_flag)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;
	int delay_ms;
	int ret = 0;

	if (!info)
		return -1;

	ret = platform_class_wireless_set_parallel_charge(WIRELESS_ROLE_MASTER, parallel_charge_flag);

	if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1665 ||
		info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 ||
		info->project_vendor == WLS_CHIP_VENDOR_SC96281)
		delay_ms = 150 * 1000;
	else
		delay_ms = 4 * 60 * 1000;

	if (parallel_charge_flag) {
		if (info->proc_data.adapter_type >= ADAPTER_XIAOMI_PD_50W)
			schedule_delayed_work(&info->max_power_control_work, msecs_to_jiffies(delay_ms));
	} else
		cancel_delayed_work_sync(&info->max_power_control_work);

	return ret;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_set_parallel_charge);

int strategy_class_wireless_ops_get_wls_type(int *wls_type)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;
	enum sys_op_mode mode_type = SYS_OP_MODE_INVALID;
	int ret = 0;

	if (!info)
		return -1;

	ret = strategy_wireless_get_sys_op_mode(&mode_type);
	if (!ret) {
		switch (mode_type) {
		case SYS_OP_MODE_BPP:
			*wls_type = XM_WLS_CHARGER_TYPE_BPP;
			break;
		case SYS_OP_MODE_EPP:
			*wls_type = XM_WLS_CHARGER_TYPE_EPP;
			break;
		case SYS_OP_MODE_PDDE:
			*wls_type = XM_WLS_CHARGER_TYPE_HPP;
			break;
		default:
			*wls_type = XM_WLS_CHARGER_TYPE_UNKNOWN;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_wls_type);

int strategy_class_wireless_ops_get_adapter_charger_mode(int *cp_charger_mode)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;
	int num;

	if (!info)
		return -1;

	num = strategy_wireless_get_compatible_info(info);
	if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 ||
			info->project_vendor == WLS_CHIP_VENDOR_SC96281) {
		switch (info->proc_data.adapter_type) {
		case ADAPTER_XIAOMI_QC3:
		case ADAPTER_XIAOMI_PD:
		case ADAPTER_ZIMI_CAR_POWER:
		case ADAPTER_XIAOMI_PD_40W:
		case ADAPTER_VOICE_BOX:
			*cp_charger_mode = FORWARD_2_1_CHARGER_MODE;
			break;
		case ADAPTER_XIAOMI_PD_50W:
		case ADAPTER_XIAOMI_PD_60W:
		case ADAPTER_XIAOMI_PD_100W:
			if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 && info->proc_data.uuid_value == 0x0e0e0205)
				*cp_charger_mode = info->proc_data.is_4_1_mode ? FORWARD_4_1_CHARGER_MODE : FORWARD_2_1_CHARGER_MODE;	/* OS4 */
			else if (info->proc_data.is_2_1_mode)
				*cp_charger_mode = FORWARD_2_1_CHARGER_MODE;
			else if(info->support_mode == 2)
				*cp_charger_mode = FORWARD_2_1_CHARGER_MODE;
			else if ((num != -1) &&
					(info->uuid_adapter_info[num].compatible_info.car_mounted ||
					((info->uuid_adapter_info[num].compatible_info.sailboat_tx) && info->project_vendor == WLS_CHIP_VENDOR_SC96281)))
				*cp_charger_mode = FORWARD_2_1_CHARGER_MODE;
			else
				*cp_charger_mode = FORWARD_4_1_CHARGER_MODE;
			break;
		default:
			*cp_charger_mode = FORWARD_4_1_CHARGER_MODE;
			break;
		}
	} else if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1651) {
		switch (info->proc_data.adapter_type) {
		case ADAPTER_XIAOMI_QC3:
		case ADAPTER_XIAOMI_PD:
		case ADAPTER_ZIMI_CAR_POWER:
		case ADAPTER_XIAOMI_PD_40W:
		case ADAPTER_VOICE_BOX:
		case ADAPTER_XIAOMI_PD_50W:
		case ADAPTER_XIAOMI_PD_60W:
		case ADAPTER_XIAOMI_PD_100W:
			*cp_charger_mode = FORWARD_4_1_CHARGER_MODE;
			break;
		default:
			*cp_charger_mode = FORWARD_4_1_CHARGER_MODE;
			break;
		}
	} else
		*cp_charger_mode = FORWARD_4_1_CHARGER_MODE;

	info->proc_data.forward_charger_mode = *cp_charger_mode;
#line 400
	mca_log_info("forward_charger_mode = %d\n", info->proc_data.forward_charger_mode);
	return 0;
}
EXPORT_SYMBOL(strategy_class_wireless_ops_get_adapter_charger_mode);

void strategy_class_wireless_op_get_rx_iout_limit(int *rx_iout_limit_ma)
{
	struct strategy_wireless_dev *info = g_basic_wls_info;

	if (!info)
		return;

	switch (info->proc_data.adapter_type) {
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
		if (info->proc_data.forward_charger_mode == FORWARD_2_1_CHARGER_MODE)
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_20W_2_1;
		else
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_20W_4_1;
		break;
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
		if (info->proc_data.forward_charger_mode == FORWARD_2_1_CHARGER_MODE)
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_30W_2_1;
		else
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_30W_4_1;
		break;
	case ADAPTER_XIAOMI_PD_50W:
		if (info->proc_data.forward_charger_mode == FORWARD_2_1_CHARGER_MODE)
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_50W_2_1;
		else
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_50W_4_1;
		break;
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 ||
				info->project_vendor == WLS_CHIP_VENDOR_SC96281) {
			if (info->proc_data.forward_charger_mode == FORWARD_2_1_CHARGER_MODE)
				*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_50W_2_1;
			else
				*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_80W_4_1;
		} else
			*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_50W_4_1;
		break;
	default:
		*rx_iout_limit_ma = MCA_BASIC_WLS_RX_IOUT_MAX_INVALID;
		break;
	}
#line 449
	mca_log_info("rx_iout_limit: %d\n", *rx_iout_limit_ma);
}
EXPORT_SYMBOL(strategy_class_wireless_op_get_rx_iout_limit);


static int strategy_wireless_msleep(int ms, struct strategy_wireless_dev *info)
{
	int i, count;

	count = ms / 10;

	for (i = 0; i < count; i++) {
		if (!info->online)
			return -1;
		usleep_range(9900, 11000);
	}

	return 0;
}

static void strategy_wireless_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 481
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "xm_wls", true, mA);
	else
		mca_vote(info->input_limit_voter, "xm_wls", false, 0);
}

static void strategy_wireless_fcc_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 491
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en) {
		mca_vote(info->charge_limit_voter, "xm_wls", true, mA);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls", true, mA);
	} else {
		mca_vote(info->charge_limit_voter, "xm_wls", false, 0);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls", false, 0);
	}
}

static void strategy_wireless_otp_fcc_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 504
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en) {
		mca_vote(info->charge_limit_voter, "xm_wls_otp", true, mA);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_otp", true, mA);
	} else {
		mca_vote(info->charge_limit_voter, "xm_wls_otp", false, 0);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_otp", false, 0);
	}
}

static void strategy_wireless_qvalue_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 517
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "xm_wls_qvalue", true, mA);
	else
		mca_vote(info->input_limit_voter, "xm_wls_qvalue", false, 0);
}

static void strategy_wireless_qvalue_fcc_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 527
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en) {
		mca_vote(info->charge_limit_voter, "xm_wls_qvalue", true, mA);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_qvalue", true, mA);
	} else {
		mca_vote(info->charge_limit_voter, "xm_wls_qvalue", false, 0);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_qvalue", false, 0);
	}
}

static void strategy_wireless_soc_fcc_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 540
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en) {
		mca_vote(info->charge_limit_voter, "xm_wls_soc", true, mA);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_soc", true, mA);
	} else {
		mca_vote(info->charge_limit_voter, "xm_wls_soc", false, 0);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_soc", false, 0);
	}
}

static void strategy_wireless_soc_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 553
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "xm_wls_soc", true, mA);
	else
		mca_vote(info->input_limit_voter, "xm_wls_soc", false, 0);
}

static void strategy_wireless_audio_phone_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 563
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "xm_wls_phone", true, mA);
	else
		mca_vote(info->input_limit_voter, "xm_wls_phone", false, 0);
}

static void strategy_wireless_67w_fcc_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 573
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en) {
		mca_vote(info->charge_limit_voter, "xm_wls_67w", true, mA);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_67w", true, mA);
	} else {
		mca_vote(info->charge_limit_voter, "xm_wls_67w", false, 0);
		mca_vote(info->sw_qc_ichg_voter, "xm_wls_67w", false, 0);
	}
}

static void strategy_wireless_67w_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 586
	mca_log_info("en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "xm_wls_67w", true, mA);
	else
		mca_vote(info->input_limit_voter, "xm_wls_67w", false, 0);
}

static void strategy_wireless_stepper_pmic_icl(struct strategy_wireless_dev *info,
	int start_icl, int end_icl, int step_ma, int ms)
{
	int temp_icl = start_icl;

	strategy_wireless_icl_setting(info, true, temp_icl);

	if (start_icl < end_icl) {
		while (temp_icl < end_icl) {
			strategy_wireless_icl_setting(info, true, temp_icl);
			temp_icl += step_ma;
			msleep(ms);
		}
	} else {
		while (temp_icl > end_icl) {
			strategy_wireless_icl_setting(info, true, temp_icl);
			if (temp_icl > step_ma)
				temp_icl -= step_ma;
			else
				temp_icl = 0;
			msleep(ms);
		}
	}

	strategy_wireless_icl_setting(info, true, end_icl);
}

static void strategy_wireless_add_trans_task_to_queue(struct strategy_wireless_dev *info,
	TRANS_DATA_FLAG data_flag, int value)
{
	struct trans_data_lis_node *node = NULL;

	node = kmalloc(sizeof(struct trans_data_lis_node), GFP_ATOMIC);
	if (!node) {
#line 628
		mca_log_err("create node error, return\n");
		return;
	}

#line 632
	mca_log_info("add: data flag: 0x%02x, value: %d\n", data_flag, value);

	spin_lock(&info->list_lock);
	node->data_flag = data_flag;
	node->value = value;
	list_add_tail(&node->lnode, &info->header);
	info->head_cnt++;
	spin_unlock(&info->list_lock);

	wake_up_interruptible(&info->wait_que);
}

static void strategy_wireless_get_qc_enable(struct strategy_wireless_dev *info, bool *enable)
{
	int input_suspend;

	if (!info->online) {
		*enable = false;
		return;
	}

	input_suspend = mca_get_effective_result(info->input_suspend_voter);
	if (info->force_vout_6v || !info->proc_data.epp || !info->proc_data.fc_flag ||
		!info->proc_data.qc_enable || !info->proc_data.pre_fastchg || input_suspend) {
		*enable = false;
#line 687
		mca_log_err("can't enable quick charge, force_vout_6v:%d  epp:%d  fc:%d  qc_enable:%d "
			"pre_fastchg:%d, input_suspned %d\n", info->force_vout_6v, info->proc_data.epp, info->proc_data.fc_flag,
			info->proc_data.qc_enable, info->proc_data.pre_fastchg, input_suspend);
		return;
	}

	// the magnetic adapter at F2 has FCC value set to 3000mA but is expectd to quit quick charge
	if (info->proc_data.adapter_type == ADAPTER_XIAOMI_PD_40W &&
		info->proc_data.current_for_adapter_cmd == ADAPTER_CMD_TYPE_F2) {
		*enable = false;
#line 697
		mca_log_err("can't enable quick charge, magnetic adapter at F2\n");
		return;
	}

	*enable = true;
#line 702
	mca_log_info("can quick charge!\n");
}

static int strategy_wireless_set_input_curr_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	if (!data)
		return -1;

#line 711
	mca_log_info("set wls_icl: %d\n", effective_result);
	return platform_class_buckchg_ops_set_wls_input_curr_lmt(MAIN_BUCK_CHARGER, effective_result);
}

static int strategy_wireless_set_bpp_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && !info->proc_data.epp &&
	((info->proc_data.adapter_type <= ADAPTER_DCP && info->proc_data.adapter_type > ADAPTER_NONE) ||
	info->proc_data.adapter_type == ADAPTER_AUTH_FAILED
	|| info->proc_data.adapter_type == ADAPTER_XIAOMI_PD))
		mca_vote(info->input_limit_voter, "wireless_thermal_bpp", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "wireless_thermal_bpp", false, 0);

	return 0;
}

static int strategy_wireless_set_bppqc2_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.adapter_type == ADAPTER_QC2)
		mca_vote(info->input_limit_voter, "wireless_thermal_bppqc2", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "wireless_thermal_bppqc2", false, 0);

	return 0;
}

static int strategy_wireless_set_bppqc3_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.adapter_type == ADAPTER_QC3)
		mca_vote(info->input_limit_voter, "wireless_thermal_bppqc3", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "wireless_thermal_bppqc3", false, 0);

	return 0;
}

static int strategy_wireless_set_epp_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.epp &&
	(info->proc_data.adapter_type == ADAPTER_PD || info->proc_data.adapter_type == ADAPTER_AUTH_FAILED))
		mca_vote(info->input_limit_voter, "wireless_thermal_epp", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "wireless_thermal_epp", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_20w_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && (info->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3 &&
	info->proc_data.adapter_type <= ADAPTER_ZIMI_CAR_POWER)) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_20w", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_20w", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_30w_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;
	bool auth30;

	if (!data)
		return -1;

	/* OS4: 40 W adapter without magnet TX, or (any other adapter) the special UUID 0x0e0e0205 on a FUDA1661 in 2:1 mode */
	/* stock +0x14..+0x80 ORs both conditions (G13h: was if/else-if, lost the case 40 W + magnet TX + UUID) */
	auth30 = info->proc_data.adapter_type == ADAPTER_XIAOMI_PD_40W && !info->proc_data.magnet_tx_flag;
	if (info->proc_data.uuid_value == 0x0e0e0205 && info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 &&
		!info->proc_data.is_4_1_mode)
		auth30 = true;

	if (effective_result && auth30) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_30w", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_30w", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_50w_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.adapter_type >= ADAPTER_XIAOMI_PD_50W &&
	info->proc_data.wireless_power.max_fcc <= 9200 && info->proc_data.uuid_value != 0x0e0e0205) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_50w", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_50w", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_80w_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.adapter_type >= ADAPTER_XIAOMI_PD_60W &&
	info->proc_data.wireless_power.max_fcc > 9200) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_80w", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_80w", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_voice_box_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.adapter_type == ADAPTER_VOICE_BOX) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_box", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_box", false, 0);

	return 0;
}

static int strategy_wireless_set_auth_magnet_30w_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	if (effective_result && info->proc_data.magnet_tx_flag && info->proc_data.adapter_type == ADAPTER_XIAOMI_PD_40W) {
		mca_vote(info->charge_limit_voter, "wireless_thermal_magnet", true, effective_result);
		mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", true, effective_result);
	} else
		mca_vote(info->charge_limit_voter, "wireless_thermal_magnet", false, 0);

	return 0;
}

static int strategy_wireless_sw_set_qc_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	info->sw_qc_ichg = effective_result;
#line 905
	mca_log_info("wireless sw set qc ichg is: %d\n", info->sw_qc_ichg);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		MCA_EVENT_WIRELESS_SW_SET_QC_ICHG, info->sw_qc_ichg);

	return 0;
}

static int strategy_wireless_sw_set_thermal_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;

	if (!data)
		return -1;

	info->sw_thermal_ichg = effective_result;
#line 922
	mca_log_info("wireless sw set thermal ichg is: %d\n", info->sw_thermal_ichg);

	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		MCA_EVENT_WIRELESS_SW_SET_THERMAL_ICHG, info->sw_thermal_ichg);

	return 0;
}

static int strategy_wireless_force_set_vout_6v(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_wireless_dev *info = data;
	if (!data)
		return -1;

	info->force_vout_6v = effective_result;
#line 938
	mca_log_info("wireless force vout 6V is: %d\n", info->force_vout_6v);

	if (info->force_vout_6v)
		return platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, 6000);

	return 0;
}

static int strategy_wireless_set_fastchg_adapter(struct strategy_wireless_dev *info)
{
	int ret = 0;

	switch (info->proc_data.adapter_type) {
	case ADAPTER_QC3:
	case ADAPTER_PD:
		if (!info->proc_data.epp) {
#line 953
			mca_log_info("bpp+ set adapter voltage to 9V\n");
			ret = platform_class_wireless_set_adapter_voltage(WIRELESS_ROLE_MASTER, BPP_PLUS_VOUT);
			if (ret < 0)
#line 956
				mca_log_info("bpp+ set adapter voltage failed!!!\n");
		}
		break;
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
	case ADAPTER_XIAOMI_PD_50W:
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
#line 967
		mca_log_info("EPP+ set adapter voltage to 15V\n");
		ret = platform_class_wireless_set_adapter_voltage(WIRELESS_ROLE_MASTER, EPP_PLUS_VOUT);
		if (ret < 0)
#line 970
			mca_log_info("epp+ set adapter voltage failed!!!\n");
		break;
	default:
#line 973
		mca_log_info("other adapter, don't set adapter voltage\n");
		break;
	}

	return 0;
}

/* OS4 stock export (new vs the template), reconstructed from the disassembly: enable / disable the vbus errorhi + errorlo IRQs of both CPs;
 * only active when the device has support_base_flip.  NOT a byte-identical function (struct layout of ours differs). */
void strategy_wireless_enable_cp_error_irq(bool enable)
{
	int ret;

	if (!g_basic_wls_info)
		return;

	if (g_basic_wls_info->support_base_flip) {
#line 80
		mca_log_info("%s vbus_errorhi and errorlo irq\n", enable ? "enable" : "disable");
		ret = platform_class_cp_enable_vbus_errorhi(CP_ROLE_MASTER, enable);
		ret |= platform_class_cp_enable_vbus_errorlo(CP_ROLE_MASTER, enable);
		ret |= platform_class_cp_enable_vbus_errorhi(CP_ROLE_SLAVE, enable);
		ret |= platform_class_cp_enable_vbus_errorlo(CP_ROLE_SLAVE, enable);
		if (ret)
#line 86
			mca_log_err("enable vbus_errorhi and errorlo irq fail\n");
	}
}
EXPORT_SYMBOL(strategy_wireless_enable_cp_error_irq);

static int strategy_wireless_get_max_power(struct strategy_wireless_dev *info)
{
	int num;
	int max_power;

	num = strategy_wireless_get_compatible_info(info);
	switch (info->proc_data.adapter_type) {
	case ADAPTER_XIAOMI_PD_50W:
		max_power = WLS_SSDEV_POWER_MAX_50W;
		break;
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		if (num != -1) {
			if (info->max_power > WLS_SSDEV_POWER_MAX_50W &&
			!info->uuid_adapter_info[num].compatible_info.sailboat_tx)
				max_power = info->max_power;
			else
				max_power = WLS_SSDEV_POWER_MAX_50W;
		} else
			max_power = info->max_power;
		break;
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
		max_power = WLS_SSDEV_POWER_MAX_30W;
		break;
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
		max_power = WLS_SSDEV_POWER_MAX_20W;
		break;
	default:
		max_power = WLS_SSDEV_POWER_MAX_INVALID;
		break;
	}
	if (max_power >= WLS_SSDEV_POWER_MAX_INVALID && max_power <= WLS_SSDEV_POWER_MAX_80W)
		return max_power;
	else
		return 0;
}

static int strategy_wireless_adapter_handle(struct strategy_wireless_dev *info)
{
	int fcc;

#line 1022
	mca_log_info("xm wls adapter handle:type[0x%02x],epp[%d]\n", info->proc_data.adapter_type, info->proc_data.epp);

	if (!info->proc_data.fc_flag) {
		switch (info->proc_data.adapter_type) {
		case ADAPTER_SDP:
		case ADAPTER_CDP:
		case ADAPTER_DCP:
		case ADAPTER_QC2:
#line 1030
			mca_log_info("[xiaomi]SDP/DCP/CDP/QC2 set iwls 750mA\n");
			info->proc_data.pre_iwls = 750;
			strategy_wireless_icl_setting(info, true, 750);
			break;
		case ADAPTER_AUTH_FAILED:
		case ADAPTER_PD:
		case ADAPTER_QC3:
			if (info->proc_data.epp) {
				if (info->proc_data.tx_max_power == 15) {
					info->proc_data.pre_iwls = 1300;
					strategy_wireless_icl_setting(info, true, 1300);
#line 1041
					mca_log_info("[xiaomi]FAIL/QC3/PD EPP set iwls 1300mA\n");
				} else {
					info->proc_data.pre_iwls = 850;
					strategy_wireless_icl_setting(info, true, 850);
#line 1045
					mca_log_info("[xiaomi]FAIL/QC3/PD EPP set iwls 850mA\n");
				}
			} else {
				info->proc_data.pre_iwls = 750;
				info->proc_data.pre_vout = BPP_DEFAULT_VOUT;
				strategy_wireless_icl_setting(info, true, 750);
#line 1051
				mca_log_info("[xiaomi]FAIL/QC3/PD BPP set iwls 750mA\n");
			}
			break;
		case ADAPTER_XIAOMI_QC3:
		case ADAPTER_XIAOMI_PD:
		case ADAPTER_ZIMI_CAR_POWER:
		case ADAPTER_XIAOMI_PD_40W:
		case ADAPTER_VOICE_BOX:
		case ADAPTER_XIAOMI_PD_50W:
		case ADAPTER_XIAOMI_PD_60W:
		case ADAPTER_XIAOMI_PD_100W:
			if (info->proc_data.tx_max_power == 15) {
				info->proc_data.pre_iwls = 1300;
				info->proc_data.pre_vout = EPP_PLUS_VOUT;
				strategy_wireless_icl_setting(info, true, 1300);
#line 1066
				mca_log_info("[xiaomi]ADPTER_XIAOMI EPP set iwls 1300mA\n");
			} else {
				info->proc_data.pre_iwls = 850;
				info->proc_data.pre_vout = EPP_DEFAULT_VOUT;
				strategy_wireless_icl_setting(info, true, 850);
#line 1071
				mca_log_info("[xiaomi]ADPTER_XIAOMI EPP set iwls 850mA\n");
			}
			break;
		default:
#line 1075
			mca_log_info("[xiaomi]other adapter type\n");
			break;
		}
	} else {
		switch (info->proc_data.adapter_type) {
		case ADAPTER_PD:
		case ADAPTER_QC3:
			msleep(2000);
			strategy_wireless_stepper_pmic_icl(info, 800, 1100, 100, 20);
			info->proc_data.pre_iwls = 1100;
			info->proc_data.pre_vout = BPP_PLUS_VOUT;
			strategy_wireless_icl_setting(info, true, 1100);
#line 1087
			mca_log_info("[xiaomi]QC3/PD BPP FC set iwls 1100mA\n");
			break;
		case ADAPTER_XIAOMI_QC3:
		case ADAPTER_XIAOMI_PD:
		case ADAPTER_ZIMI_CAR_POWER:
			info->proc_data.pre_vout = EPP_PLUS_VOUT;
			fcc = info->project_vendor == WLS_CHIP_VENDOR_SC96281 ? 3800 : 4000;	/* OS4 */
			strategy_wireless_fcc_setting(info, true, fcc);
			info->proc_data.wireless_power.max_fcc = fcc;	/* G52a: stock stores max_fcc before qc_enable */
			info->proc_data.qc_enable = true;
			info->proc_data.qc_type = ADP_ICON_TYPE_FLASH;
#line 1102
			mca_log_info("[xiaomi]20W adapter set fcc as requested\n");
			break;
		case ADAPTER_XIAOMI_PD_40W:
		case ADAPTER_VOICE_BOX:
			info->proc_data.pre_vout = EPP_PLUS_VOUT;
			fcc = info->project_vendor == WLS_CHIP_VENDOR_SC96281 ? 5400 : 6000;	/* OS4 */
			strategy_wireless_fcc_setting(info, true, fcc);
			info->proc_data.wireless_power.max_fcc = fcc;	/* G52a: stock stores max_fcc before qc_enable */
			info->proc_data.qc_enable = true;
			info->proc_data.qc_type = ADP_ICON_TYPE_SUPER;
#line 1116
			mca_log_info("[xiaomi]30W adapter set fcc as requested\n");
			break;
		case ADAPTER_XIAOMI_PD_60W:
		case ADAPTER_XIAOMI_PD_100W:
			info->proc_data.pre_vout = EPP_PLUS_VOUT;
			info->proc_data.qc_enable = true;
			if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 && info->proc_data.is_4_1_mode && info->max_power > 50) {
				strategy_wireless_fcc_setting(info, true, 13000);
				info->proc_data.wireless_power.max_fcc = 13000;
			} else {
				strategy_wireless_fcc_setting(info, true, 9200);
				info->proc_data.wireless_power.max_fcc = 9200;
			}
			info->proc_data.qc_type = ADP_ICON_TYPE_SUPER;
#line 1130
			mca_log_info("[xiaomi]60W adapter set fcc as requested\n");
			break;
		case ADAPTER_XIAOMI_PD_50W:
			info->proc_data.pre_vout = EPP_PLUS_VOUT;
			strategy_wireless_fcc_setting(info, true, 9200);
			info->proc_data.wireless_power.max_fcc = 9200;
			info->proc_data.qc_type = ADP_ICON_TYPE_SUPER;
#line 1137
			mca_log_info("[xiaomi]40~50W adapter set fcc as requested\n");
			info->proc_data.qc_enable = true;
			break;
		default:
#line 1141
			mca_log_info("[xiaomi]other adapter type\n");
			break;
		}
	}

	schedule_delayed_work(&info->wireless_loop_work, msecs_to_jiffies(1000));

	return 0;
}

static void strategy_wireless_update_soc_to_tx(struct strategy_wireless_dev *info)
{
#ifdef CONFIG_FACTORY_BUILD
	return;
#endif

	u8 data[3] = {0};
	u8 data_length = 3;

	if (!info->online)
		return;

	if (info->proc_data.adapter_type < ADAPTER_XIAOMI_QC3)
		return;

	info->mutex_lock_sts = true;

	info->proc_data.batt_soc = strategy_class_fg_ops_get_soc();
	data[0] = 0x00;
	data[1] = 0x05;
	data[2] = (u8)(info->proc_data.batt_soc);

	(void)platform_class_wireless_send_transparent_data(WIRELESS_ROLE_MASTER, data, data_length);

	schedule_delayed_work(&info->mutex_unlock_work, msecs_to_jiffies(2000));
}

static void strategy_wireless_get_target_vout_iwls(struct strategy_wireless_dev *info)
{
	/* OS4 stock: the input_suspend voter forces the RX to 6 V; the XIAOMI adapters always use 11 V */
	int input_suspend = mca_get_effective_result(info->input_suspend_voter);

	switch (info->proc_data.adapter_type) {
	case ADAPTER_QC2:
		info->proc_data.target_vout = BPP_QC2_VOUT;
		info->proc_data.target_iwls = 750;
		break;
	case ADAPTER_PD:
	case ADAPTER_QC3:
		if (info->proc_data.epp) {
			info->proc_data.target_vout = EPP_DEFAULT_VOUT;
			if (info->proc_data.tx_max_power == 15)
				info->proc_data.target_iwls = 1300;
			else
				info->proc_data.target_iwls = 850;
		} else if (info->proc_data.fc_flag) {
			info->proc_data.target_vout = BPP_PLUS_VOUT;
			info->proc_data.target_iwls = 1100;
		} else {
			info->proc_data.target_vout = BPP_DEFAULT_VOUT;
			if (info->proc_data.dev_auth)
				info->proc_data.target_iwls = 300;
			else
				info->proc_data.target_iwls = 750;
		}
		break;
	case ADAPTER_AUTH_FAILED:
		if (info->proc_data.epp) {
			info->proc_data.target_vout = EPP_DEFAULT_VOUT;
			if (info->proc_data.tx_max_power == 15)
				info->proc_data.target_iwls = 1300;
			else
				info->proc_data.target_iwls = 850;
		} else {
			info->proc_data.target_vout = BPP_DEFAULT_VOUT;
			if (info->proc_data.high_start_soc == true)
				info->proc_data.target_iwls = 300;
			else
				info->proc_data.target_iwls = 750;
		}
		break;
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
	case ADAPTER_XIAOMI_PD_50W:
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		if (info->proc_data.fc_flag || info->proc_data.tx_max_power == 15)
			info->proc_data.target_iwls = 1300;
		else
			info->proc_data.target_iwls = 850;
		break;
	default:
		if (info->proc_data.epp) {
			info->proc_data.target_vout = EPP_DEFAULT_VOUT;
			if (info->proc_data.tx_max_power == 15)
				info->proc_data.target_iwls = 1300;
			else
				info->proc_data.target_iwls = 850;
		} else {
			info->proc_data.target_vout = BPP_DEFAULT_VOUT;
			info->proc_data.target_iwls = 750;
		}
		break;
	}

	if (input_suspend)
		info->proc_data.target_vout = BPP_DEFAULT_VOUT;

#line 1255
	mca_log_info("initial target vout: %u, target iwls: %u, input_suspend %d\n", info->proc_data.target_vout, info->proc_data.target_iwls, input_suspend);
}

static int strategy_wireless_get_chgr_stage(struct strategy_wireless_dev *info, enum XM_WLS_CHGR_STAGE *chg_stage)
{
	bool charging_done;

	if ((chg_stage == NULL) || (!info))
		return -EINVAL;

	charging_done = strategy_class_fg_ops_get_charging_done();

	switch (*chg_stage) {
	case WLS_NORMAL_MODE:
		if (info->proc_data.batt_soc == 100) {
			*chg_stage = WLS_TAPER_MODE;
#line 1271
			mca_log_info("change normal to taper mode\n");
		}
		break;
	case WLS_TAPER_MODE:
		if (info->proc_data.batt_soc == 100 &&
			info->proc_data.chgr_status == POWER_SUPPLY_CHARGING_STATUS_FULL) {
			*chg_stage = WLS_FULL_MODE;
			//xm_wireless_set_ble_mac();
#line 1279
			mca_log_info("change taper to full mode\n");
		} else if (info->proc_data.batt_soc < 99) {
			*chg_stage = WLS_NORMAL_MODE;
#line 1282
			mca_log_info("change full to normal mode\n");
		}
		break;
	case WLS_FULL_MODE:
		if (!charging_done && info->proc_data.chgr_status == POWER_SUPPLY_CHARGING_STATUS_CHARGING) {
			*chg_stage = WLS_RECHG_MODE;
#line 1288
			mca_log_info("change full to recharge mode\n");
		}
		break;
	case WLS_RECHG_MODE:
		if (info->proc_data.chgr_status == POWER_SUPPLY_CHARGING_STATUS_FULL) {
			*chg_stage = WLS_FULL_MODE;
#line 1294
			mca_log_info("change recharge to full mode\n");
		}
		break;
	default:
#line 1298
		mca_log_info("error chgr stage!\n");
		break;
	}

	return 0;
}

static void strategy_wireless_get_batt_chgr_status(struct strategy_wireless_dev *info, int *chg_status)
{
	int rc = 0;
	union power_supply_propval pval = {0, };

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy) {
#line 1312
		mca_log_err("batt_psy is not find, return\n");
		return;
	}

	rc = power_supply_get_property(info->batt_psy, POWER_SUPPLY_PROP_STATUS, &pval);
	if (rc < 0)
#line 1318
		mca_log_err("Couldn't read battery status, rc=%d\n", rc);

	*chg_status = pval.intval;
}

static int strategy_wireless_send_frequency_request(struct strategy_wireless_dev *info, u8 frequence_khz)
{
	u8 send_value[4] = {0x00, 0x28, 0xd3, 0};
	int ret = 0;

	if (!info->online)
		return -1;

	if (frequence_khz < SUPER_TX_FREQUENCY_MIN_KHZ || frequence_khz > SUPER_TX_FREQUENCY_MAX_KHZ)
		return -EINVAL;

	info->mutex_lock_sts = true;
	send_value[3] = frequence_khz;

	ret = platform_class_wireless_send_transparent_data(WIRELESS_ROLE_MASTER, send_value, ARRAY_SIZE(send_value));
#line 1338
	mca_log_info("%d\n", frequence_khz);
	schedule_delayed_work(&info->mutex_unlock_work, msecs_to_jiffies(2000));

	return ret;
}

static int strategy_wireless_send_vout_range_request(struct strategy_wireless_dev *info, int max_voltage_mv)
{
	u8 send_value[4] = {0x00, 0x28, 0xd6, 0};
	u8 req_voltage_mv;
	int ret = 0;

	if (!info->online)
		return -1;

	if (max_voltage_mv < SUPER_TX_VOUT_MIN_MV || max_voltage_mv > SUPER_TX_VOUT_MAX_MV)
		return -EINVAL;

	info->mutex_lock_sts = true;

	req_voltage_mv = (max_voltage_mv - SUPER_TX_VOUT_MIN_MV)/500;
	send_value[3] = req_voltage_mv;

	ret = platform_class_wireless_send_transparent_data(WIRELESS_ROLE_MASTER, send_value, ARRAY_SIZE(send_value));
#line 1362
	mca_log_info("%d\n", max_voltage_mv);

	schedule_delayed_work(&info->mutex_unlock_work, msecs_to_jiffies(2000));

	return ret;
}

static int strategy_wireless_transparent_success(struct strategy_wireless_dev *info)
{
	int ret = 0;
	const struct mca_hwid *hwid;

	info->proc_data.set_tx_voltage_cnt = 0;	/* G52a: stock stores the counter before it calls mca_get_hwid_info() */
	hwid = mca_get_hwid_info();

	switch (info->proc_data.current_for_tx_cmd) {
	case TX_CMD_TYPE_FREQUENCE:
		info->proc_data.current_for_tx_cmd = TX_CMD_TYPE_VOLTAGE;
		if(info->support_mode == 2)
#line 1379
			mca_log_info("voltage_range not set 32V\n");
		else {
			/* stock +0x10a8: 20 V on platform_version 3 with TX uuid 0x09010a01, else 32 V (G13h: was always 32 V) */
			info->proc_data.tx_voltage = (hwid && hwid->platform_version == 3 && info->proc_data.uuid_value == 0x09010a01) ? 20000 : 32000;
			strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_VOUT_RANGE, info->proc_data.tx_voltage);
#line 1386
			mca_log_info("voltage_range 32V\n");
		}
		break;
	case TX_CMD_TYPE_VOLTAGE:
		/* OS4 stock: CP 2:1 mode for TX uuid 0x09010a01 at hwid platform_version 3, and for the 30 W magnet TX (uuid 0x0e0e0205) on
		 * FUDA1661 while ss_voltage < 6700; every other TX runs 4:1 (the template had `if (0)` = always 4:1) */
		info->proc_data.is_2_1_mode = false;
		if ((hwid && hwid->platform_version == 3 && info->proc_data.uuid_value == 0x09010a01) ||
			(info->proc_data.uuid_value == 0x0e0e0205 && info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 &&
				info->proc_data.ss_voltage < 6700)) {
			info->proc_data.is_2_1_mode = true;
			info->proc_data.is_4_1_mode = false;
		} else {
			info->proc_data.is_4_1_mode = true;
		}
		info->proc_data.pre_fastchg = true;
		ret = strategy_wireless_adapter_handle(info);
		schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
		info->proc_data.current_for_tx_cmd = TX_CMD_TYPE_NONE;
#line 1404
		mca_log_info("limit success\n");
		break;
	default:
		break;
	}

	return ret;
}

/* OS4 stock (inlined in process_irq, after a successful TX frequency step): a low Q value of a low-inductance / magnet TX
 * (tx_q <= tx_q2 of its kind, if that kind's support_q switch is set) lowers the RX voltage to 11 V (6 V at full stage) */
static void strategy_wireless_q_value_reduce_voltage_func(struct strategy_wireless_dev *info, int num)
{
	int tx_q2 = 0;

	if (info->proc_data.current_for_tx_cmd != TX_CMD_TYPE_VOLTAGE || num == -1)
		return;

	if (info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_50W])
		tx_q2 = info->tx_q2[ADAPTER_LOW_INDUCTANCE_TX_50W];
	else if (info->uuid_adapter_info[num].compatible_info.low_inductance_80w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_80W])
		tx_q2 = info->tx_q2[ADAPTER_LOW_INDUCTANCE_TX_80W];
	else if (info->uuid_adapter_info[num].compatible_info.magnet_30w_tx && info->support_q[ADAPTER_MAGNET_TX_30W])
		tx_q2 = info->tx_q2[ADAPTER_MAGNET_TX_30W];

	if (info->proc_data.tx_q > tx_q2)
		return;

	if (info->proc_data.chgr_stage == WLS_FULL_MODE)
		strategy_wireless_set_rx_vout(info, BPP_DEFAULT_VOUT);
	else
		strategy_wireless_set_rx_vout(info, EPP_DEFAULT_VOUT);
#line 1429
	mca_log_info("q_value=%d < tx_q2=%d reduce voltage\n", info->proc_data.tx_q, tx_q2);
}

static int strategy_wireless_cmd_type_f5_func(struct strategy_wireless_dev *info, bool en)
{
	int ret = 0;
	struct mca_votable *qcchg_disable_voter = NULL;

	qcchg_disable_voter = mca_find_votable("wls_quick_chg_disable");
	if (qcchg_disable_voter)
		mca_vote(qcchg_disable_voter, "basic_wls_set", true, !!en);

	mca_vote(info->force_vout_6v_voter, "cmd_type", true, !!en);

	if (!en) {
		if (info->proc_data.chgr_stage == WLS_FULL_MODE)
			ret = strategy_wireless_set_rx_vout(info, BPP_DEFAULT_VOUT);
		else
			ret = strategy_wireless_set_rx_vout(info, EPP_DEFAULT_VOUT);
	}

	return ret;
}

static int strategy_wireless_help_fcc_change(struct strategy_wireless_dev *info, u8 *receive_data)
{
	int ret = 0;
	int i;
	int tx_adapter = info->proc_data.adapter_type;
	int icl_setting, fcc_setting;
	int force_6v_result = 0;

	if ((receive_data[2] == info->proc_data.current_for_adapter_cmd) ||
		(receive_data[2] < ADAPTER_CMD_TYPE_F0 || receive_data[2] > ADAPTER_CMD_TYPE_F5))
		return ret;
#line 1466
	mca_log_info("receive data:0x%x\n", receive_data[2]);

	for (i = 0; i < ADAPTER_CMD_TYPE_MAX; i++) {
		if (info->cmd_type_info[i].receive_data == receive_data[2])
			break;
	}

	if (i == ADAPTER_CMD_TYPE_MAX) {
#line 1474
		mca_log_err("cannot find cmd_type_%02x\n", receive_data[2]);
		return -1;
	}

	info->proc_data.current_for_adapter_cmd = receive_data[2];
#line 1479
	mca_log_info("current for adapter cmd:0x%x\n", info->proc_data.current_for_adapter_cmd);

	icl_setting = info->cmd_type_info[i].cmd_fx[tx_adapter].icl_setting;
	fcc_setting = info->cmd_type_info[i].cmd_fx[tx_adapter].fcc_setting;

	force_6v_result = mca_get_client_vote(info->force_vout_6v_voter, "cmd_type");

	//set icl and fcc
	switch (info->proc_data.current_for_adapter_cmd) {
	case ADAPTER_CMD_TYPE_F0:
		if (force_6v_result)
			strategy_wireless_cmd_type_f5_func(info, false);
		strategy_wireless_67w_fcc_setting(info, false, 0);
		strategy_wireless_67w_icl_setting(info, false, 0);
		break;
	case ADAPTER_CMD_TYPE_F5:
		strategy_wireless_cmd_type_f5_func(info, true);
		if (icl_setting)
			strategy_wireless_67w_icl_setting(info, true, icl_setting);
		if (fcc_setting)
			strategy_wireless_67w_fcc_setting(info, true, fcc_setting);
		break;
	default:
		if (force_6v_result)
			strategy_wireless_cmd_type_f5_func(info, false);
		if (icl_setting)
			strategy_wireless_67w_icl_setting(info, true, icl_setting);
		else
			strategy_wireless_67w_icl_setting(info, false, 0);
		if (fcc_setting)
			strategy_wireless_67w_fcc_setting(info, true, fcc_setting);
		break;
	}

	if (info->proc_data.adapter_type == ADAPTER_XIAOMI_PD_40W &&
		info->proc_data.current_for_adapter_cmd == ADAPTER_CMD_TYPE_F2) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			MCA_EVENT_WIRELESS_MAGNETIC_TX_F2, 0);
	}

	return ret;
}

static int strategy_wireless_transparent_fail(struct strategy_wireless_dev *info)
{
	int ret = 0;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (info->proc_data.set_tx_voltage_cnt++ < TRANS_FAIL_RETRY_MAX_CNT) {
		switch (info->proc_data.current_for_tx_cmd) {
		case TX_CMD_TYPE_FREQUENCE:
			strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_FREQUENCE, info->proc_data.tx_frequency);
#line 1531
			mca_log_info("frequency_range failed\n");
			break;
		case TX_CMD_TYPE_VOLTAGE:
			if (hwid && hwid->platform_version == 3 && info->proc_data.uuid_value == 0x09010a01)
				info->proc_data.tx_voltage = 20000;	/* OS4 */
			else if (info->proc_data.adapter_type > ADAPTER_XIAOMI_PD_50W)
				info->proc_data.tx_voltage = 34000;
			else
				info->proc_data.tx_voltage = 32000;
			strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_VOUT_RANGE, info->proc_data.tx_voltage);
#line 1541
			mca_log_info("voltage_range failed\n");
			break;
		default:
			break;
		}
	} else {
		info->proc_data.current_for_tx_cmd = TX_CMD_TYPE_NONE;
		if ((!info->proc_data.first_drawload) && (!info->proc_data.is_4_1_mode)) {
			info->proc_data.is_2_1_mode = true;
			info->proc_data.first_drawload = true;
			info->proc_data.pre_fastchg = true;
			info->proc_data.tx_voltage = 20000;
			ret = strategy_wireless_set_rx_vout(info, 8000);	//dts
			ret = strategy_wireless_adapter_handle(info);
			strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_VOUT_RANGE, info->proc_data.tx_voltage);
			schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
#line 1557
			mca_log_info("voltage_range 20V,is_2_1_mode :%d\n", info->proc_data.is_2_1_mode);
		}
	}

	return ret;
}

static void strategy_wireless_epp_plate_tx_work(struct strategy_wireless_dev *info)
{
	bool vout_change = false;
	int vout = 0;

#line 1569
	mca_log_info("enter epp plate tx work\n");

	//if(pDeviceContext->xm_power_supply_properties[XM_POWER_SUPPLY_PROP_WLSCHARGE_CONTROL_LIMIT] != 0)
	//{
	//	info->proc_data.target_vout = EPP_DEFAULT_VOUT;
	//	if(xm_wireless_cfg.cp_vendor == WLS_CP_LN8282){
	//		info->proc_data.target_iwls = 1800;
	//	}else
	//		info->proc_data.target_iwls = 850;
	//	info->proc_data.fc_flag = 0;
	//	TraceInfo(PmicWPP_device, "epp plate, thermal != 0, keep inov");
	//}

	if (info->proc_data.chgr_stage == WLS_FULL_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 250;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 250);
	}

	if (info->proc_data.chgr_stage == WLS_RECHG_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 550;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 550);
	}

	if (info->proc_data.target_vout != info->proc_data.pre_vout) {
#line 1597
		mca_log_info("epp plate, set new vout: %u, pre vout: %u\n",
			info->proc_data.target_vout, info->proc_data.pre_vout);

		if (info->proc_data.target_vout == EPP_PLUS_VOUT) {
			strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
			info->proc_data.qc_enable = true;
		} else if (info->proc_data.target_vout == EPP_DEFAULT_VOUT) {
			(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout);
			while (vout > EPP_DEFAULT_VOUT) {
				vout = vout - 1000;
				strategy_wireless_set_rx_vout(info, vout);
				msleep(200);
			}
			strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
			info->proc_data.qc_enable = false;
		}
		info->proc_data.pre_vout = info->proc_data.target_vout;
		vout_change = true;
	}

	if ((info->proc_data.target_iwls != info->proc_data.pre_iwls) || vout_change) {
#line 1618
		mca_log_info("epp plate, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}
}

static void strategy_wireless_epp_train_tx_work(struct strategy_wireless_dev *info)
{
	bool vout_change = false;
	int vout = 0, battery_temp = 0;
	(void)strategy_class_fg_ops_get_temperature(&battery_temp);
#line 1630
	mca_log_info("enter epp train tx work, BatteryTemperature=%d\n", battery_temp);
	battery_temp /= 10;
	if (battery_temp >= 38) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		if (battery_temp <= 40)
			info->proc_data.target_iwls = 700;
		else
			info->proc_data.target_iwls = 500;
		info->proc_data.fc_flag = 0;
		strategy_wireless_fcc_setting(info, true, 1990);
	}

	if (info->proc_data.chgr_stage == WLS_FULL_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 250;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 250);
	}

	if (info->proc_data.chgr_stage == WLS_RECHG_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 550;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 550);
	}

	if (info->proc_data.target_vout != info->proc_data.pre_vout) {
#line 1657
		mca_log_info("epp train, set new vout: %u, pre vout: %u\n",
			info->proc_data.target_vout, info->proc_data.pre_vout);

		if (info->proc_data.target_vout == EPP_PLUS_VOUT) {
			strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
			info->proc_data.qc_enable = true;
		} else if (info->proc_data.target_vout == EPP_DEFAULT_VOUT) {
			(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout);
			while (vout > EPP_DEFAULT_VOUT) {
				vout = vout - 1000;
				strategy_wireless_set_rx_vout(info, vout);
				msleep(200);
			}
			strategy_wireless_set_rx_vout(info, vout);
			info->proc_data.qc_enable = false;
		}
		info->proc_data.pre_vout = info->proc_data.target_vout;
		vout_change = true;
	}

	if ((info->proc_data.target_iwls != info->proc_data.pre_iwls) || vout_change) {
#line 1678
		mca_log_info("epp train, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}
}

static void strategy_wireless_qc2_adapter_work(struct strategy_wireless_dev *info)
{
#line 1687
	mca_log_info("enter qc2 adapter work\n");

	if (info->proc_data.batt_soc >= 95)
		info->proc_data.target_iwls = 500;
	if (info->proc_data.batt_soc >= 99)
		info->proc_data.target_iwls = 350;
	if (info->proc_data.chgr_stage == WLS_FULL_MODE)
		info->proc_data.target_iwls = 200;

	if (info->proc_data.target_vout != info->proc_data.pre_vout) {
#line 1697
		mca_log_info("qc2 adapter, set new vout: %u, pre vout: %u\n",
			info->proc_data.target_vout, info->proc_data.pre_vout);
		strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
		info->proc_data.pre_vout = info->proc_data.target_vout;
	}

	if (info->proc_data.target_iwls != info->proc_data.pre_iwls) {
#line 1704
		mca_log_info("qc2 adapter, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}
}

static void strategy_wireless_bpp_plus_work(struct strategy_wireless_dev *info)
{
#line 1713
	mca_log_info("enter bpp plus work\n");

	if (info->proc_data.batt_soc >= 95) {
		info->proc_data.target_vout = BPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 750;
	}
	if (info->proc_data.chgr_stage == WLS_FULL_MODE)
		info->proc_data.target_iwls = 250;

	if (info->proc_data.target_vout != info->proc_data.pre_vout) {
#line 1723
		mca_log_info("bpp plus, set new vout: %u, pre vout: %u\n",
					info->proc_data.target_vout, info->proc_data.pre_vout);
		strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
		info->proc_data.pre_vout = info->proc_data.target_vout;
	}

	if (info->proc_data.target_iwls != info->proc_data.pre_iwls) {
#line 1730
		mca_log_info("bpp plus, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}
}

static void strategy_wireless_epp_standard_work(struct strategy_wireless_dev *info)
{
#line 1739
	mca_log_info("enter standard epp work\n");

	if (info->proc_data.chgr_stage == WLS_FULL_MODE)
		info->proc_data.target_iwls = 250;

	if (info->proc_data.chgr_stage == WLS_RECHG_MODE)
		info->proc_data.target_iwls = 550;

	if (info->proc_data.target_iwls != info->proc_data.pre_iwls) {
#line 1748
		mca_log_info("standard epp, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}

}

static void strategy_wireless_epp_plus_work(struct strategy_wireless_dev *info)
{
	int quick_sts = 0;

#line 1760
	mca_log_info("enter epp plus work\n");
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_QC_START_FLAG, &quick_sts);
	if (info->proc_data.chgr_stage == WLS_FULL_MODE) {
		info->proc_data.target_vout = 6000;
		info->proc_data.target_iwls = 300;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
	}

	if (info->proc_data.chgr_stage == WLS_RECHG_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 550;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_stepper_pmic_icl(info, info->proc_data.pre_iwls, info->proc_data.target_iwls, 100, 50);
	}

	/* OS4 stock: no RX vout / ICL change while the quick-wireless (CP) charge is running */
	if (info->proc_data.target_vout != info->proc_data.pre_vout && !quick_sts) {
#line 1778
		mca_log_info("epp plus, set new vout: %u, pre vout: %u\n",
			info->proc_data.target_vout, info->proc_data.pre_vout);
		strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
		msleep(200);
		info->proc_data.pre_vout = info->proc_data.target_vout;
	}

	if (info->proc_data.target_iwls != info->proc_data.pre_iwls && !quick_sts) {
#line 1786
		mca_log_info("epp plus, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}

	if (info->proc_data.batt_soc >= 70 && info->proc_data.high_soc_fcc_set == false) {
		info->proc_data.high_soc_fcc_set = true;
		strategy_wireless_fcc_setting(info, true, info->soc_limit_fcc);
	}
}

static void strategy_wireless_epp_musical_box_work(struct strategy_wireless_dev *info)
{
	bool vout_change = false;
	int vout = 0, battery_temp = 0;
	(void)strategy_class_fg_ops_get_temperature(&battery_temp);
#line 1803
	mca_log_info("enter epp musical box work, BatteryTemperature=%d\n", battery_temp);
	battery_temp /= 10;

	if (battery_temp >= 39) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 850;
		info->proc_data.fc_flag = 0;
		if (battery_temp >= 44)
			strategy_wireless_fcc_setting(info, true, 1000);
		else if (battery_temp == 43)
			strategy_wireless_fcc_setting(info, true, 1100);
		else if (battery_temp == 42)
			strategy_wireless_fcc_setting(info, true, 1300);
		else if (battery_temp == 41)
			strategy_wireless_fcc_setting(info, true, 1500);
		else if (battery_temp == 40)
			strategy_wireless_fcc_setting(info, true, 1700);
		else if (battery_temp == 39)
			strategy_wireless_fcc_setting(info, true, 1900);
	} else if (battery_temp == 38)
		strategy_wireless_fcc_setting(info, true, 2100);
	else if (battery_temp == 37)
		strategy_wireless_fcc_setting(info, true, 2300);
	else if (battery_temp == 36)
		strategy_wireless_fcc_setting(info, true, 2500);
	else if (battery_temp <= 35)
		strategy_wireless_fcc_setting(info, true, 6000);

	if (info->proc_data.chgr_stage == WLS_FULL_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 250;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 250);
	}

	if (info->proc_data.chgr_stage == WLS_RECHG_MODE) {
		info->proc_data.target_vout = EPP_DEFAULT_VOUT;
		info->proc_data.target_iwls = 550;
		if (info->proc_data.target_iwls != info->proc_data.pre_iwls)
			strategy_wireless_icl_setting(info, true, 550);
	}

	if (info->proc_data.target_vout != info->proc_data.pre_vout) {
#line 1846
		mca_log_info("epp musical box, set new vout: %u, pre vout: %u\n",
			info->proc_data.target_vout, info->proc_data.pre_vout);

		if (info->proc_data.target_vout == EPP_PLUS_VOUT) {
			strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
			info->proc_data.qc_enable = true;
		} else if (info->proc_data.target_vout == EPP_DEFAULT_VOUT) {
			(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout);
			while (vout > EPP_DEFAULT_VOUT) {
				vout = vout - 1000;
				strategy_wireless_set_rx_vout(info, vout);
				msleep(200);
			}
			strategy_wireless_set_rx_vout(info, info->proc_data.target_vout);
			info->proc_data.qc_enable = false;
		}
		info->proc_data.pre_vout = info->proc_data.target_vout;
		vout_change = true;
	}

	if ((info->proc_data.target_iwls != info->proc_data.pre_iwls) || vout_change) {
#line 1867
		mca_log_info("epp musical box, set new iwls: %u, pre iwls: %u\n",
			info->proc_data.target_iwls, info->proc_data.pre_iwls);
		strategy_wireless_icl_setting(info, true, info->proc_data.target_iwls);
		info->proc_data.pre_iwls = info->proc_data.target_iwls;
	}
}

static int strategy_wireless_charge_loop_work(struct strategy_wireless_dev *info)
{
	int num;

	num = strategy_wireless_get_compatible_info(info);
	strategy_wireless_get_target_vout_iwls(info);

	if ((num != -1) && (info->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3)) {
		if (info->uuid_adapter_info[num].compatible_info.plate_tx) {
			strategy_wireless_epp_plate_tx_work(info);
			return 0;
		} else if (info->uuid_adapter_info[num].compatible_info.train_tx) {
			strategy_wireless_epp_train_tx_work(info);
			return 0;
		}
	}

	switch (info->proc_data.adapter_type) {
	case ADAPTER_QC2:
		strategy_wireless_qc2_adapter_work(info);
		break;
	case ADAPTER_PD:
	case ADAPTER_QC3:
		if (info->proc_data.epp)
			strategy_wireless_epp_standard_work(info);
		else
			strategy_wireless_bpp_plus_work(info);
		break;
	case ADAPTER_AUTH_FAILED:
		if (info->proc_data.epp)
			strategy_wireless_epp_standard_work(info);
		break;
	case ADAPTER_XIAOMI_QC3:
	case ADAPTER_XIAOMI_PD:
	case ADAPTER_ZIMI_CAR_POWER:
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_XIAOMI_PD_50W:
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		strategy_wireless_epp_plus_work(info);
		break;
	case ADAPTER_VOICE_BOX:
		strategy_wireless_epp_musical_box_work(info);
		break;
	default:
		break;
	}

	return 0;
}

static void strategy_wireless_get_charging_info(struct strategy_wireless_dev *info)
{
	int vout = 0, vrect = 0, iout = 0, iwls = 0, icl = 0, buck_fcc = 0;
	int pmic_status = MCA_BATT_CHGR_STATUS_CHARGING_DISABLED;
	bool chg_en = false;
	int enable_quickchg = 0;
	u8 brg_status = 0;

	(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &vout);
	(void)platform_class_wireless_get_vrect(WIRELESS_ROLE_MASTER, &vrect);
	(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &iout);
	(void)platform_class_buckchg_ops_get_wls_curr(MAIN_BUCK_CHARGER, &iwls);
	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &pmic_status);
	(void)platform_class_wireless_get_rx_brg_status(WIRELESS_ROLE_MASTER, &brg_status);
	info->proc_data.batt_soc = strategy_class_fg_ops_get_soc();
	strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_SOC, 0);
	strategy_wireless_get_batt_chgr_status(info, &info->proc_data.chgr_status);
	strategy_wireless_get_chgr_stage(info, &info->proc_data.chgr_stage);
	(void)strategy_class_fg_ops_get_voltage(&info->proc_data.vbat_cell_mv);
	icl = mca_get_effective_result(info->input_limit_voter);
	buck_fcc = mca_get_effective_result(info->charge_limit_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_QC_START_FLAG, &enable_quickchg);
	if (info->proc_data.epp && info->proc_data.fc_flag && (!info->proc_data.start_vout_check_flag)) {
		++info->proc_data.start_wls_loop_cnt;
		if (enable_quickchg)
			info->proc_data.start_vout_check_flag = true;
		else if (info->proc_data.start_wls_loop_cnt >= START_WLS_LOOP_CHECK_CNT) {
			info->proc_data.start_vout_check_flag = true;
#line 1956
			mca_log_info("after epp fastcharge, no enter quick_chg mode, set vout 11V\n");
			if (!info->proc_data.magnet_tx_flag || !info->audio_phone_sts) {
				strategy_wireless_set_rx_vout(info, EPP_DEFAULT_VOUT);
				info->proc_data.pre_vout = EPP_DEFAULT_VOUT;
			}
		}
	}

	if (!enable_quickchg)
	{
		if (info->support_mode == 2)
			strategy_wireless_soc_icl_setting(info, true, iout + 450);
		else
			strategy_wireless_soc_icl_setting(info, true, iout + 250);
	} else
		strategy_wireless_soc_icl_setting(info, false, 0);

#line 1973
	mca_log_err("wireless loop: vout:%d, vrect:%d, iout:%d, iwls:%d, vbat_cell_mv:%d, soc:%d, tx_adapter:%d\n",
				vout, vrect, iout, iwls, info->proc_data.vbat_cell_mv, info->proc_data.batt_soc, info->proc_data.adapter_type);
#line 1975
	mca_log_err("wireless loop: icl:%d, buck_fcc:%d, chg_en:%d, charging_status:%d, pmic_status:%d, stage:%d, brg_status %d\n",
				icl, buck_fcc, chg_en, info->proc_data.chgr_status, pmic_status, info->proc_data.chgr_stage, brg_status);
}

static void strategy_wireless_enable_vdd(struct strategy_wireless_dev *info, bool enable)
{
	int ret = 0;
	int src_enable = 0;
	bool otg_plugin;
	EN_SRC en_boost = WIRELESS_EN_BOOST;

#line 1986
	mca_log_info("enable_vdd: %d\n", enable);

	src_enable = (en_boost << 16) | (info->wls_vdd_src << 8) | enable;

	protocol_class_pd_get_otg_plugin_status(TYPEC_PORT_0, &otg_plugin);
	if (enable) {
		info->proc_data.boost_wireless_vdd = true;
		(void)platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);
	} else {
		if (!otg_plugin && info->proc_data.boost_wireless_vdd == true)
			(void)platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, src_enable);
		info->proc_data.boost_wireless_vdd = false;
	}
	ret = platform_class_buckchg_ops_set_wls_vdd_flag(MAIN_BUCK_CHARGER, info->proc_data.boost_wireless_vdd);
	if (ret) {
#line 2001
		mca_log_err("set wireless vdd boost fail, retry\n");
		schedule_delayed_work(&info->set_vdd_flag_work, msecs_to_jiffies(500));
	}

	msleep(20);
}

static int strategy_wireless_phone_sts_change(struct strategy_wireless_dev *info)
{
	int ret = 0;

	if (info->online && info->proc_data.magnet_tx_flag) {
		if (info->audio_phone_sts) {
			ret = strategy_wireless_set_rx_vout(info, 6000);
			if (ret < 0)
#line 2016
				mca_log_info("work around for audio phone state set rx voltage failed!!!\n");
			strategy_wireless_audio_phone_icl_setting(info, true, info->phone_icl);
		} else if (info->proc_data.chgr_stage != WLS_FULL_MODE) {
			ret = strategy_wireless_set_rx_vout(info, 11000);
			if (ret < 0)
#line 2021
				mca_log_info("work around for audio phone state reset rx voltage failed!!!\n");
			strategy_wireless_audio_phone_icl_setting(info, false, 0);
		}	// else stay at 6V since charge stage is full
	}

	if (info->online && info->proc_data.auth_data < AUTH_STATUS_UUID_OK) {
		if (info->audio_phone_sts) {
			ret = strategy_wireless_set_rx_vout(info, info->phone_vol);
			if (ret < 0)
#line 2030
				mca_log_info("work around for audio phone state set rx voltage failed!!!\n");
			strategy_wireless_audio_phone_icl_setting(info, true, info->offstd_phone_icl);
		} else {
			if (info->proc_data.epp)
				ret = strategy_wireless_set_rx_vout(info, 11000);
			else
				ret = strategy_wireless_set_rx_vout(info, 6000);
			if (ret < 0)
#line 2038
				mca_log_info("work around for audio phone state reset rx voltage failed!!!\n");
			strategy_wireless_audio_phone_icl_setting(info, false, 0);
		}
	}

#line 2043
	mca_log_info("work around for audio phone state %d\n", info->audio_phone_sts);

	return ret;
}

static void strategy_wireless_update_quick_charge_type(struct strategy_wireless_dev *info)
{
	if (info->batt_cold || info->batt_overhot) {
		info->proc_data.qc_type = ADP_ICON_TYPE_NORMAL;
		return;
	}

	info->proc_data.wireless_power.max_power = strategy_wireless_get_max_power(info);
	if (info->proc_data.wireless_power.max_power >= MCA_WLS_CHG_SUPER_CHG_POWER)
		info->proc_data.qc_type = ADP_ICON_TYPE_SUPER;
	else if (info->proc_data.wireless_power.max_power >= MCA_WLS_CHG_FLASH_CHG_POWER)
		info->proc_data.qc_type = ADP_ICON_TYPE_FLASH;
	else
		info->proc_data.qc_type = ADP_ICON_TYPE_NORMAL;
}

static noinline void strategy_wireless_pre_authen_process(struct strategy_wireless_dev *info)
{
	int ret = 0;
	int len = 0;
	int tx_adapter = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;

	ret = platform_class_wireless_get_tx_adapter_by_i2c(WIRELESS_ROLE_MASTER, &tx_adapter);
	if (ret) {
#line 2074
		mca_log_info("pre authen get tx adapter fail\n");
		return;
	}

	if (tx_adapter) {
#line 2079
		mca_log_info("get tx adapter: %d\n", tx_adapter);
		info->proc_data.adapter_type_first = tx_adapter;
		info->proc_data.adapter_type = tx_adapter;
	} else {
#line 2083
		mca_log_info("adapter_type is default, return\n");
		info->proc_data.adapter_type = ADAPTER_SDP;
	}

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_TX_ADAPTER=%d", info->proc_data.adapter_type);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	if (info->proc_data.adapter_type == ADAPTER_SDP)
		return;

	strategy_wireless_update_quick_charge_type(info);
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_QUICK_CHARGE_TYPE=%d", info->proc_data.qc_type);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void strategy_wireless_power_good_on(struct strategy_wireless_dev *info)
{
	bool fw_update = false;
	int len = 0, ret = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };
	int wls_input_suspend;
	int power_good_flag = 0;
	const struct mca_hwid *hwid = mca_get_hwid_info();

#line 2114
	mca_log_err("wireless power_good_on\n");

	msleep(50);
	ret = platform_class_wireless_check_i2c_is_ok(WIRELESS_ROLE_MASTER);
	if (ret) {
#line 2119
		mca_log_err("check rx i2c error, retry\n");
		ret = platform_class_wireless_check_i2c_is_ok(WIRELESS_ROLE_MASTER);
		platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &power_good_flag);
		if (ret && power_good_flag) {
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_IIC_ERR, NULL, 0);
#line 2124
			mca_log_err("wireless Rx I2c check fail\n");
		}
	}
	platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER, false);
	(void)platform_class_wireless_get_project_vendor(WIRELESS_ROLE_MASTER, &info->project_vendor);
	(void)platform_class_cp_get_chip_vendor(0, &info->cp_vendor);
	if (info->cp_vendor == SC8541_VENDOR)
		platform_class_cp_enable_ovpgate_with_check(0, WLS_CHG, false);
	(void)platform_class_wireless_get_magnetic_case_flag(WIRELESS_ROLE_MASTER, &info->proc_data.magnetic_case_flag);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, MCA_EVENT_WIRELESS_MAGNETIC_CASE_INT, info->proc_data.magnetic_case_flag);
	info->wait_for_reverse_test = false;

	mca_vote(info->chg_enable_voter, "online", true, 1);
	mca_vote(info->charge_limit_voter, "volt_thermal_limit", false, 0);

	strategy_wireless_icl_setting(info, true, 0);
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(150));
	schedule_delayed_work(&info->process_irq_work, msecs_to_jiffies(0));
	schedule_delayed_work(&info->trans_data_work, msecs_to_jiffies(0));

	cancel_delayed_work_sync(&info->renegociation_work);

	wls_input_suspend = mca_get_effective_result(info->wls_input_suspend_voter);
	if (!wls_input_suspend)
		platform_class_buckchg_ops_set_wls_hiz(MAIN_BUCK_CHARGER, false);
	platform_class_buckchg_ops_set_hiz(MAIN_BUCK_CHARGER, true);

	if (hwid && hwid->platform_version == 5)
		schedule_delayed_work(&info->get_type_advance_work, msecs_to_jiffies(800));
	else
		strategy_wireless_pre_authen_process(info);

	info->proc_data.batt_soc = strategy_class_fg_ops_get_soc();
	if (info->proc_data.batt_soc >= 70) {
		info->proc_data.high_soc_fcc_set = true;
		strategy_wireless_soc_fcc_setting(info, true, 6000);	//temp, depend on dts
	}

	/* OS4 stock: at connect the CP vbus error IRQs are switched off again (no-op without support_base_flip) */
	strategy_wireless_enable_cp_error_irq(false);

	//check fw_version
	platform_class_wireless_check_firmware_state(WIRELESS_ROLE_MASTER, &fw_update);
	if (fw_update)
		schedule_delayed_work(&info->wls_fw_state_work, msecs_to_jiffies(10000));
	else {
		mca_wireless_rev_set_firmware_state(FIRMWARE_NO_UPDATE);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_NO_UPDATE);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}
}

static void  strategy_wireless_reset_charge_para(struct strategy_wireless_dev *info)
{
	int usb_online = 0;

#line 2198
	mca_log_info("stop charging\n");
	info->sw_cv_running = false;
	info->vbat_ov_count = 0;
	memset(&info->proc_data, 0, sizeof(info->proc_data));
	memset(&info->wls_node, 0, sizeof(info->wls_node));

	/* OS4 stock: only when no USB is online the charger is disabled ("online" vote 0), the FCC is limited to 100 mA and
	 * the "vbat_ovp" disable vote is released; with USB online all three votes stay untouched */
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_STATUS_TYPE_ONLINE, &usb_online);
#line 2204
	mca_log_info("usb_online=%d\n", usb_online);
	if (!usb_online) {
		mca_vote(info->chg_enable_voter, "online", true, 0);
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", true, 100);
		mca_vote(info->chg_enable_voter, "vbat_ovp", false, 1);
	}
	mca_vote(info->input_limit_voter, "thermal_phone", false, 0);
}

static void strategy_wireless_reset_trans_list(struct strategy_wireless_dev *info)
{
	struct trans_data_lis_node *node, *tmp;
	while (!list_empty(&info->header)) {
		spin_lock(&info->list_lock); /* G31a: stock drains the whole list under one lock hold (list_for_each_entry_safe) */
		list_for_each_entry_safe(node, tmp, &info->header, lnode) {
			list_del(&node->lnode);
			kfree(node);
			info->head_cnt--;
		}
		spin_unlock(&info->list_lock);
	}
#line 2191
	mca_log_info("success reset trans list\n");
}

static void strategy_wireless_power_good_off(struct strategy_wireless_dev *info)
{
	int usb_input_suspend;
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

#line 2220
	mca_log_err("wireless power_good_off\n");

	strategy_wireless_cmd_type_f5_func(info, false);
	strategy_wireless_sw_cv_stop(info);
	info->mutex_lock_sts = false;
	wake_up_interruptible(&info->wait_que);
	wake_up_interruptible(&info->wait_que_irq);

	cancel_delayed_work_sync(&info->trans_data_work);
	strategy_wireless_reset_trans_list(info);
	cancel_delayed_work_sync(&info->process_irq_work);
	cancel_delayed_work_sync(&info->max_power_control_work);
	cancel_delayed_work_sync(&info->monitor_work);
	cancel_delayed_work_sync(&info->update_wireless_thermal_work);
	cancel_delayed_work_sync(&info->rx_fastcharge_work);
	cancel_delayed_work_sync(&info->rx_alarm_work);
	cancel_delayed_work_sync(&info->wireless_loop_work);
	cancel_delayed_work_sync(&info->renegociation_work);
	cancel_delayed_work_sync(&info->wls_drawload_work);
	cancel_delayed_work_sync(&info->get_type_advance_work);

	platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER, true);
	if (info->cp_vendor == SC8541_VENDOR)
		platform_class_cp_enable_ovpgate_with_check(0, WLS_CHG, true);
	usb_input_suspend = mca_get_effective_result(info->input_suspend_voter);
	if (!usb_input_suspend)
		platform_class_buckchg_ops_set_hiz(MAIN_BUCK_CHARGER, false);
	platform_class_buckchg_ops_set_wls_hiz(MAIN_BUCK_CHARGER, true);

	strategy_wireless_enable_vdd(info, false);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_WLS_CAR_ADAPTER=%d", 0);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_LOW_INDUCTANCE_OFFSET=%d", 0);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	mca_vote(info->sw_thermal_ichg_voter, "wireless_thermal", false, 0);
	mca_vote(info->input_limit_voter, "weak_epp15w_tx", false, 0);
	mca_vote(info->force_vout_6v_voter, "soc_limit", false, 0);
	strategy_wireless_icl_setting(info, false, 0);
	strategy_wireless_fcc_setting(info, false, 0);
	strategy_wireless_otp_fcc_setting(info, false, 0);
	strategy_wireless_soc_fcc_setting(info, false, 0);
	strategy_wireless_soc_icl_setting(info, false, 0);
	strategy_wireless_67w_fcc_setting(info, false, 0);
	strategy_wireless_67w_icl_setting(info, false, 0);
	strategy_wireless_qvalue_fcc_setting(info, false, 0);
	strategy_wireless_qvalue_icl_setting(info, false, 0);
	strategy_wireless_audio_phone_icl_setting(info, false, 0);
	(void)strategy_class_fg_set_fastcharge(false);
	strategy_wireless_enable_cp_error_irq(true);

	strategy_wireless_reset_charge_para(info);
}

static void strategy_wireless_process_online_change(int value, struct strategy_wireless_dev *info)
{
	if (value == info->online)
		return;

	info->online = value;
	if (value)
		strategy_wireless_power_good_on(info);
	else
		strategy_wireless_power_good_off(info);
}

static void strategy_wireless_chg_handler(struct strategy_wireless_dev *info)
{
	int val_length, h_len, l_len;
	u8 rcv_value[128];
	u8 rcv_val = 0;
	u8 err_code = 0;
	u8 dfx_code = 64;	/* G43a: stock initialises [sp+0xc8] to 0x40 (process_irq 0x6100/0x613c) */
	int ret = 0;
	int num = -1;	/* stock: per-call value (G13h: was a static initialised to 10); recomputed by get_compatible_info where the stock does */
	int tx_q2 = 0;
	int len = 0;
	int dfx_data[2] = { 0 };
	int interval = 500;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;
	WLS_DEBUG_SET_FOD_TYPE type = WLS_DEBUG_SET_FOD_NONE;

#line 2311
	mca_log_info("wireless chg handler, int_index = %d\n", info->proc_data.int_flag);
	switch (info->proc_data.int_flag) {
	case RX_INT_POWER_ON:
#line 2314
		mca_log_info("RX_INT_POWER_ON\n");
		ret = platform_class_wireless_get_rx_power_mode(WIRELESS_ROLE_MASTER, &info->proc_data.epp);
		if (ret)
#line 2317
			mca_log_info("get rx power mode failed\n");
#line 2318
		mca_log_info("epp : %d\n", info->proc_data.epp);
		break;
	case RX_INT_LDO_ON:
#line 2321
		mca_log_info("RX_INT_LDO_ON!\n");
		cancel_delayed_work_sync(&info->rx_fastcharge_work);
		platform_class_buckchg_ops_set_aicl_enable(MAIN_BUCK_CHARGER, false);

		ret = platform_class_wireless_get_ss_voltage(WIRELESS_ROLE_MASTER, &info->proc_data.ss_voltage);
		if (ret)
#line 2327
			mca_log_info("get ss_voltage failed in LDO_ON!\n");

		ret = platform_class_wireless_get_rx_power_mode(WIRELESS_ROLE_MASTER, &info->proc_data.epp);
		if (ret)
#line 2331
			mca_log_info("get rx power mode failed\n");
		else if (!info->proc_data.epp) {
			(void)platform_class_wireless_get_rsv_eppmode_fail(WIRELESS_ROLE_MASTER, &info->proc_data.rsv_eppmode_fail);
#line 2334
			mca_log_info("get value in LDO_ON! eppmode %d rsv01DA = 0x%04x\n", info->proc_data.epp, info->proc_data.rsv_eppmode_fail);
		}

			schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
		ret = platform_class_wireless_get_tx_max_power(WIRELESS_ROLE_MASTER, &info->proc_data.tx_max_power);
		if (ret)
#line 2340
			mca_log_info("get tx max power failed\n");

		if (info->proc_data.epp) {
			strategy_wireless_icl_setting(info, true, 200);
			schedule_delayed_work(&info->wls_drawload_work, msecs_to_jiffies(300));
		} else {
			if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1651)
				strategy_wireless_stepper_pmic_icl(info, 250, 750, 100, 20);
			else if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1665 ||
						info->project_vendor == WLS_CHIP_VENDOR_FUDA1661) {
				strategy_wireless_icl_setting(info, true, 250);
				schedule_delayed_work(&info->wls_drawload_work, msecs_to_jiffies(300));
			} else
				strategy_wireless_stepper_pmic_icl(info, 250, 750, 100, 20);
		}
		break;
	case RX_INT_AUTHEN_FINISH:
#line 2357
		mca_log_err("RX_INT_AUTHEN_FINISH\n");
		platform_class_wireless_get_auth_value(WIRELESS_ROLE_MASTER, &info->proc_data.auth_data);
		platform_class_wireless_get_tx_uuid(WIRELESS_ROLE_MASTER, info->proc_data.uuid);
		info->proc_data.uuid_value |= (info->proc_data.uuid[0] << 24 | info->proc_data.uuid[1] << 16 |
										info->proc_data.uuid[2] << 8 | info->proc_data.uuid[3]);
#line 2362
		mca_log_err("uuid_value is 0x%08x\n", info->proc_data.uuid_value);
		platform_class_wireless_get_tx_adapter(WIRELESS_ROLE_MASTER, &info->proc_data.adapter_type);

		if (info->proc_data.auth_data != AUTH_STATUS_FAILED) {
			if (info->proc_data.auth_data >= AUTH_STATUS_UUID_OK) {
				platform_class_wireless_get_debug_fod_type(WIRELESS_ROLE_MASTER, &type);
				if (type != WLS_DEBUG_SET_FOD_NONE)
					platform_class_wireless_set_debug_fod_params(WIRELESS_ROLE_MASTER);
				else
					platform_class_wireless_set_fod_params(WIRELESS_ROLE_MASTER, info->proc_data.auth_data);
			}

			if (!info->proc_data.epp)
#line 2375
				mca_log_info("bpp tx hw id: 0x%x, 0x%x\n", info->proc_data.uuid[0], info->proc_data.uuid[1]);
			else
				num = strategy_wireless_get_compatible_info(info);

			if (info->proc_data.uuid[2] & 0x80) {
				info->proc_data.magnet_tx_flag = true;
				(void)mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL, MCA_EVENT_WIRELESS_MAGNETIC_TX_INT, 1);	/* stock +0xb28 */
			}
#line 2383
			mca_log_info("uuid2: 0x%x, magnet_tx_flag: %d\n", info->proc_data.uuid[2], info->proc_data.magnet_tx_flag);

			if (num != -1) {
				if (info->uuid_adapter_info[num].compatible_info.car_mounted && (info->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3)) {
					info->wls_node.wls_car_adapter = 1;
					len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
						"POWER_SUPPLY_WLS_CAR_ADAPTER=%d", info->wls_node.wls_car_adapter);
					event_data.event = event;
					event_data.event_len = len;
					mca_event_report_uevent(&event_data);
				}

				if (info->uuid_adapter_info[num].compatible_info.musical_box)
					info->proc_data.adapter_type = ADAPTER_VOICE_BOX;

				if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 &&
				info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx)
				{
					info->wls_node.low_inductance_offset = info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx;
					len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
						"POWER_SUPPLY_LOW_INDUCTANCE_OFFSET=%d", info->wls_node.low_inductance_offset);
					event_data.event = event;
					event_data.event_len = len;
					mca_event_report_uevent(&event_data);
				}

			}

			if (num != -1) {
				/* OS4 stock: one support_q[] switch per TX kind (50 W / 80 W low inductance, 30 W magnet) */
				if (((info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_50W]) ||
				(info->uuid_adapter_info[num].compatible_info.low_inductance_80w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_80W]) ||
				(info->uuid_adapter_info[num].compatible_info.magnet_30w_tx && info->support_q[ADAPTER_MAGNET_TX_30W])) && info->support_q_value) {
#line 2414
					mca_log_info("send_tx_q_value_request\n");
					strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_QVALUE, 0);
				}
			}

			strategy_wireless_update_quick_charge_type(info);
			if (info->proc_data.adapter_type_first != info->proc_data.adapter_type) {
				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_TX_ADAPTER=%d", info->proc_data.adapter_type);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);

				len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_QUICK_CHARGE_TYPE=%d", info->proc_data.qc_type);
				event_data.event = event;
				event_data.event_len = len;
				mca_event_report_uevent(&event_data);

				schedule_delayed_work(&info->report_soc_decimal_work, msecs_to_jiffies(0));
			}

			if (info->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3)
				strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_SOC, 0);

			strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_FAN_SPEED, 0);

			interval = info->head_cnt > 0 ? info->head_cnt * 500 : 500;
			if (info->proc_data.adapter_type >= ADAPTER_XIAOMI_PD_50W)
				schedule_delayed_work(&info->renegociation_work, msecs_to_jiffies(interval));
			else {
				strategy_wireless_adapter_handle(info);
				schedule_delayed_work(&info->rx_fastcharge_work, msecs_to_jiffies(interval));
			}
			/* stock +0xd74..+0xe24: weak EPP 15 W TX (uuid 0x09010903 and tx_max_power 15) */
			if (info->proc_data.uuid_value == 0x09010903 && info->proc_data.tx_max_power == 15) {
				strategy_wireless_stepper_pmic_icl(info, 1300, 850, 100, 500);
				mca_vote(info->input_limit_voter, "weak_epp15w_tx", true, 850);
				if (!info->online) {
					strategy_wireless_icl_setting(info, false, 0);
					mca_vote(info->input_limit_voter, "weak_epp15w_tx", false, 0);
				}
			}
		} else {
#line 2460
			mca_log_info("authen failed!\n");
			strategy_wireless_adapter_handle(info);
			schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
			info->proc_data.fc_done = true;
			if (info->support_hall && info->proc_data.magnetic_case_flag)
				strategy_wireless_phone_sts_change(info);
		}
		break;
	case RX_INT_RENEGO_DONE:
#line 2469
		mca_log_info("RX_INT_RENEGO_DONE\n");
		cancel_delayed_work_sync(&info->renegociation_work);
		strategy_wireless_rx_fastcharge_work(&info->rx_fastcharge_work.work);
		break;
	case RX_INT_RENEGO_FAIL:
#line 2474
		mca_log_info("RX_INT_RENEGO_FAIL\n");
		cancel_delayed_work_sync(&info->renegociation_work);
		schedule_delayed_work(&info->renegociation_work, msecs_to_jiffies(0));
		break;
	case RX_INT_FAST_CHARGE:
#line 2479
		mca_log_err("RX_INT_FAST_CHARGE\n");
		platform_class_wireless_get_rx_fastcharge_status(WIRELESS_ROLE_MASTER, &info->proc_data.fc_flag);
		info->wls_node.wls_fc_flag = info->proc_data.fc_flag;
#line 2482
		mca_log_err("fast chg success: %d\n", info->proc_data.fc_flag);
		cancel_delayed_work_sync(&info->rx_fastcharge_work);
		num = strategy_wireless_get_compatible_info(info);	/* stock +0x528 */

		if (info->proc_data.fc_flag) {
			if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1665 ||
					info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 ||
					info->project_vendor == WLS_CHIP_VENDOR_SC96281) {
				if (info->proc_data.adapter_type < ADAPTER_XIAOMI_PD_50W ||
					((num != -1) && info->uuid_adapter_info[num].compatible_info.car_mounted && info->proc_data.uuid_value != 0x0e0e0205) ||
					info->support_mode == 2) {
					info->proc_data.pre_fastchg = true;
					strategy_wireless_adapter_handle(info);
					strategy_wireless_set_rx_vout(info, 8000);
					schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
				} else {
					info->proc_data.current_for_tx_cmd = TX_CMD_TYPE_FREQUENCE;
					info->proc_data.tx_frequency = 137;
					strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_FREQUENCE, info->proc_data.tx_frequency);
#line 2505
					mca_log_info("frequency_range 137kHz\n");
				}
			} else {
				info->proc_data.pre_fastchg = true;
				strategy_wireless_adapter_handle(info);
				schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
			}

			(void)platform_class_buckchg_ops_get_otg_boost_src(MAIN_BUCK_CHARGER, &info->otg_boost_src);
			if (info->proc_data.epp && info->otg_boost_src == info->wls_vdd_src) {
				strategy_wireless_enable_vdd(info, true);
				if (info->project_vendor == WLS_CHIP_VENDOR_SC96281)
					platform_class_wireless_enable_vsys_ctrl(WIRELESS_ROLE_MASTER, true);
			}
		} else if (info->proc_data.adapter_type >= ADAPTER_XIAOMI_PD_40W &&
					info->proc_data.set_fastcharge_vout_cnt < FAST_CHARGE_RETRY_MAX_CNT)
			schedule_delayed_work(&info->rx_fastcharge_work, msecs_to_jiffies(0));
		else {
			strategy_wireless_adapter_handle(info);
			schedule_delayed_work(&info->update_wireless_thermal_work, msecs_to_jiffies(0));
		}

		if (info->proc_data.fc_flag || info->proc_data.set_fastcharge_vout_cnt >= FAST_CHARGE_RETRY_MAX_CNT) {
			info->proc_data.fc_done = true;
			if (info->support_hall && info->proc_data.magnetic_case_flag)
				strategy_wireless_phone_sts_change(info);
		}
		break;
	case RX_INT_TRANSPARENT_SUCCESS:
#line 2534
		mca_log_info("RX_INT_TRANSPARENT_SUCCESS\n");
		platform_class_wireless_receive_transparent_data(WIRELESS_ROLE_MASTER, rcv_value, ARRAY_SIZE(rcv_value), &val_length);
		cancel_delayed_work_sync(&info->mutex_unlock_work);
		strategy_wireless_mutex_unlock_work(&info->mutex_unlock_work.work);
		h_len = (val_length & 0xF0) >> 4;
		l_len = val_length & 0x0F;
#line 2540
		mca_log_info("h_len = %d, l_len = %d\n", h_len, l_len);
		num = strategy_wireless_get_compatible_info(info);	/* stock +0x3ec */
		if (l_len == 1) {
			switch (rcv_value[0]) {
			case 0x28:
				rcv_val = rcv_value[3] & 0x3C;
				if (rcv_val == 0x3C) {
					(void)strategy_wireless_transparent_success(info);
					strategy_wireless_q_value_reduce_voltage_func(info, num);
				} else
					(void)strategy_wireless_transparent_fail(info);
				break;
			case 0x05:
				(void)strategy_wireless_help_fcc_change(info, rcv_value);
				break;
			}
		} else if (l_len == 4) {
			if (rcv_value[4] == 0x62) {
				info->proc_data.tx_q = rcv_value[5];
#line 2561
				mca_log_info("tx_q = %d, rcv_value[5] = %d\n", info->proc_data.tx_q, rcv_value[5]);
				if (num != -1) {
					/* G31a: stock +0x834..+0x8a0: the kind flag AND the support_q switch of that kind, three kinds (50 W, 80 W, 30 W magnet), else 0 */
					if (info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_50W]) tx_q2 = info->tx_q2[ADAPTER_LOW_INDUCTANCE_TX_50W];
					else if (info->uuid_adapter_info[num].compatible_info.low_inductance_80w_tx && info->support_q[ADAPTER_LOW_INDUCTANCE_TX_80W]) tx_q2 = info->tx_q2[ADAPTER_LOW_INDUCTANCE_TX_80W];
					else if (info->uuid_adapter_info[num].compatible_info.magnet_30w_tx && info->support_q[ADAPTER_MAGNET_TX_30W]) tx_q2 = info->tx_q2[ADAPTER_MAGNET_TX_30W];

					if (rcv_value[5] <= tx_q2) {
						strategy_wireless_qvalue_fcc_setting(info, true, MCA_WLS_CHG_TX_QLIMIT_FCC_5W);
						strategy_wireless_qvalue_icl_setting(info, true, MCA_WLS_CHG_TX_QLIMIT_ICL_5W);
#line 2573
						mca_log_info("Q value is smaller than Q2, set power to 5W\n");
						dfx_data[0] = tx_q2;
						dfx_data[1] = rcv_value[5];
						mca_charge_mievent_report(CHARGE_DFX_WLS_FOD_LOW_POWER, dfx_data, 2);
					}
				}
			}
		}
		break;
	case RX_INT_TRANSPARENT_FAIL:
#line 2583
		mca_log_info("RX_INT_TRANSPARENT_FAIL\n");
		platform_class_wireless_receive_transparent_data(WIRELESS_ROLE_MASTER, rcv_value, ARRAY_SIZE(rcv_value), &val_length);
		cancel_delayed_work_sync(&info->mutex_unlock_work);
		strategy_wireless_mutex_unlock_work(&info->mutex_unlock_work.work);
		h_len = (val_length & 0xF0) >> 4;
		l_len = val_length & 0x0F;
#line 2589
		mca_log_info("h_len = %d, l_len = %d\n", h_len, l_len);
		if (rcv_value[0] == 0x28 && info->proc_data.current_for_tx_cmd != TX_CMD_TYPE_NONE)
			(void)strategy_wireless_transparent_fail(info);
		break;
	case RX_INT_RPP:
#line 2594
		mca_log_info("RX_INT_RPP\n");
		break;
	case RX_INT_OOB_GOOD:
#line 2597
		mca_log_info("RX_INT_OOB_GOOD\n");
		break;
	case RX_INT_OCP_OTP_ALARM:
#line 2600
		mca_log_info("RX_INT_OCP_OTP_ALARM\n");
		schedule_delayed_work(&info->rx_alarm_work, msecs_to_jiffies(500));
		break;
	case RX_INT_POWER_OFF:
		(void)platform_class_wireless_get_poweroff_err_code(WIRELESS_ROLE_MASTER, &err_code);
#line 2605
		mca_log_err("RX_INT_POWER_OFF, err_code:0x%02x\n", err_code);
		switch (err_code) {
		case 3:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OTP, &info->rx_temp, 1);
			break;
		case 4:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OVP, NULL, 0);
			break;
		case 5:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OCP, NULL, 0);
			break;
		default:
			break;
		}
		break;
	case RX_INT_FACTORY_TEST:
#ifdef CONFIG_FACTORY_BUILD
		(void)platform_class_wireless_receive_test_cmd(WIRELESS_ROLE_MASTER, rcv_value, &val_length);
		mca_log_err("RX_INT_FACTORY_TEST, %d, 0x%x, 0x%x, 0x%x\n", val_length, rcv_value[0], rcv_value[1], rcv_value[2]);

		if (rcv_value[0] == FACTORY_TEST_CMD) {
			if (rcv_value[1] == FACTORY_TEST_CMD_REVERSE_REQ)
				info->wait_for_reverse_test = true;
			(void)platform_class_wireless_process_factory_cmd(WIRELESS_ROLE_MASTER, rcv_value[1]);
		}
#endif
		break;
	case RX_INT_ERR_CODE:
		(void)platform_class_wireless_get_rx_err_code(WIRELESS_ROLE_MASTER, &err_code, &dfx_code);
#line 2634
		mca_log_err("RX_INT_ERR_CODE, err_code:0x%02x, dfx_code:%d\n", err_code, dfx_code);
		/* OS4 stock: the dfx code of the RX is itself the mievent id 27 (OTP, with rx_temp), 28 (OVP), 29 (OCP) */
		switch (dfx_code) {
		case CHARGE_DFX_WLS_RX_OTP:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OTP, &info->rx_temp, 1);
			break;
		case CHARGE_DFX_WLS_RX_OVP:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OVP, NULL, 0);
			break;
		case CHARGE_DFX_WLS_RX_OCP:
			mca_charge_mievent_report(CHARGE_DFX_WLS_RX_OCP, NULL, 0);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	info->proc_data.int_flag = 0;
}

/*
 * G13e: the OS4 stock queues every set bit of the INT_CHANGE value as a node (struct rx_int_lis_node) and handles the nodes in
 * process_irq_work() (wait_event on wait_que_irq); chg_handler is inlined into process_irq (stock: separate 5724-byte function).
 */
static noinline int strategy_wireless_process_irq(struct strategy_wireless_dev *info)
{
	struct rx_int_lis_node *node, *tmp;

	while (!list_empty(&info->header_irq)) {
		spin_lock(&info->list_lock_irq);
		list_for_each_entry_safe(node, tmp, &info->header_irq, node) {
			list_del(&node->node);
			spin_unlock(&info->list_lock_irq);
#line 2816
			mca_log_info("cur_node: data_flag: %d\n", node->data_flag);
			info->proc_data.int_flag = node->data_flag;
			strategy_wireless_chg_handler(info);
			spin_lock(&info->list_lock_irq);
			kfree(node);
			info->head_cnt_irq--;
		}
		spin_unlock(&info->list_lock_irq);
	}

	return !info->online;
}

static void strategy_wireless_process_irq_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work, struct strategy_wireless_dev, process_irq_work.work);

	while (info->online)
		wait_event_interruptible(info->wait_que_irq, strategy_wireless_process_irq(info));
}

static void strategy_wireless_add_irq_queue(struct strategy_wireless_dev *info, int flag)
{
	struct rx_int_lis_node *node = kmalloc(sizeof(*node), GFP_ATOMIC);

	if (!node) {
#line 2780
		mca_log_err("create node error, return\n");
		return;
	}
#line 2783
	mca_log_info("add: data flag: 0x%x\n", flag);
	spin_lock(&info->list_lock_irq);
	node->data_flag = flag;	/* G52a: stock stores the flag after the spin_lock (kept in a callee-saved register across log + lock) */
	list_add_tail(&node->node, &info->header_irq);
	info->head_cnt_irq++;
	spin_unlock(&info->list_lock_irq);
}

void strategy_wireless_split_irq_index(struct strategy_wireless_dev *info, int index)
{
	int i;

	for (i = 0; index > 0; i++, index >>= 1) {
		if (index & 1)
			strategy_wireless_add_irq_queue(info, 1 << i);
	}
	wake_up_interruptible(&info->wait_que_irq);
}

static void strategy_wireless_change_cp_mode_workfunc(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work, struct strategy_wireless_dev, change_cp_mode_work.work);

#line 3318
	mca_log_info("wireless changing CP work mode\n");
	info->proc_data.is_2_1_mode = true;
	info->proc_data.is_4_1_mode = false;
	strategy_wireless_fcc_setting(info, true, 6000);
	info->proc_data.wireless_power.max_fcc = 6000;
	schedule_delayed_work(&info->update_wireless_thermal_work, 0);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, MCA_EVENT_CP_NEW_MODE, 0);
}

static void strategy_wireless_get_adapter_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work, struct strategy_wireless_dev, get_type_advance_work.work);

	strategy_wireless_pre_authen_process(info);
}

/*
 * G13e: stock rodata object strategy_wireless_buckchg_base_flip_sw_cv_workfunc.sw_cv_volt_delta_map [16 bytes] = {1000, 7}, {0, 5} (G13h: was {0, 2}, stock bytes e8030000 07000000 00000000 05000000).
 * Recovered from the OS4 disassembly; ibat_ma holds microamps in this member (stock divides by 1000 for the log and the table).
 */
static void strategy_wireless_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work, struct strategy_wireless_dev, base_flip_sw_cv_work.work);
	static const int sw_cv_volt_delta_map[][2] = {
		{ 1000, 7 },
		{ 0, 5 }
	};
	int interval = 5000;
	int vterm_jeita = mca_get_client_vote(info->vterm_voter, "jeita");
	int iterm = info->parallel_iterm;
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int first_termination = 0;
	int fastcharge, vterm, actual_vterm, compensa_fv, vadj, fv_step, volt_delta, vbat, ibat, i;

	fastcharge = strategy_class_fg_get_fastcharge();
	strategy_class_fg_get_first_termination(&first_termination);
	if (fastcharge) {
		compensa_fv = 25;
		vadj = -4;
	} else {
		compensa_fv = info->pmic_fv_compensation;
		vadj = -2;
	}
	vterm = first_termination ? mca_get_effective_result(info->vterm_voter) : vterm_jeita;
	actual_vterm = vterm + compensa_fv;
	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat_cell_mv);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat_ma);
	vbat = info->proc_data.vbat_cell_mv;
	ibat = -info->proc_data.ibat_ma / 1000;
	fv_step = fastcharge ? -10 : -5;
#line 3372
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, iterm: %d, fcc: %d, actual_vterm %d, pmic_fv_compensa %d, compensa_fv %d\n",
		vbat, ibat, vterm_jeita, iterm, fcc, actual_vterm, info->pmic_fv_compensation, compensa_fv);

	volt_delta = sw_cv_volt_delta_map[0][1];
	for (i = 0; i < ARRAY_SIZE(sw_cv_volt_delta_map); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (vbat >= vterm_jeita - volt_delta) {
		interval = 2000;
		if (ibat - 50 > iterm) {
			int val;

			if (fcc - ibat >= 100)
				val = ibat / 50 * 50;
			else
				val = fcc - 50;

			mca_vote(info->charge_limit_voter, "sw_cv", true, val > 500 ? val : 500);
		}
	}

	if (vbat >= vterm_jeita + vadj) {
#line 3398
		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n", info->vbat_ov_count);
		info->vbat_ov_count++;
		platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, actual_vterm + min(info->vbat_ov_count, 5) * fv_step);
	}

	mca_queue_delayed_work(&info->base_flip_sw_cv_work, msecs_to_jiffies(interval));
}

static void strategy_wireless_process_batt_btb_change(int value, struct strategy_wireless_dev *info)
{
#line 2663
	mca_log_info("value: %d\n", value);
	if (value) {
		mca_vote(info->charge_limit_voter, "batt_miss", true, info->batt_type == 0 ? MCA_WLS_CHG_BATT_MISS_FCC : 1000);
#ifndef CONFIG_FACTORY_BUILD
		mca_vote(info->vterm_voter, "batt_miss", true, MCA_WLS_CHG_BATT_MISS_FV);
#endif
	} else {
		mca_vote(info->charge_limit_voter, "batt_miss", false, MCA_WLS_CHG_CURRENT_DEFAULT_VALUE);
		mca_vote(info->vterm_voter, "batt_miss", false, MCA_WLS_CHG_VTERM_DEFAULT_VALUE);
	}
}

static const struct
{
	int fcc_value;
	int icl_value;
} soc_limit_stepper_table[] = {
	{0, 0},
	{1990, 850},
	{1400, 750},
	{1100, 650},
	{900, 550},
	{700, 450},
	{500, 350},
	{300, 350},
	{200, 350},
	{100, 250},
	{0, 250},
};

#define SOC_LIMIT_MAX_STEP 10
static void strategy_wireless_process_soc_limit_change(int value, struct strategy_wireless_dev *info)
{
	static u8 cur_step;

	if (value)
#line 2702
		mca_log_info("SOC limit triggered\n");
	else
#line 2704
		mca_log_info("SOC limit released\n");

	if (cur_step > SOC_LIMIT_MAX_STEP)
		cur_step = SOC_LIMIT_MAX_STEP;

	if (value && cur_step < SOC_LIMIT_MAX_STEP)
		cur_step += 1;
	else if (!value && cur_step > 0)
		cur_step -= 1;

	if (cur_step == 0) {
		mca_vote(info->charge_limit_voter, "soc_limit", false, 0);
		mca_vote(info->input_limit_voter, "soc_limit", false, 0);
		mca_vote(info->force_vout_6v_voter, "soc_limit", false, 0);	/* OS4: new */
	} else {
		mca_vote(info->charge_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].fcc_value);
		mca_vote(info->input_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].icl_value);
		if (cur_step != SOC_LIMIT_MAX_STEP) {
			mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
			schedule_delayed_work(&info->soc_limit_stepper_work, msecs_to_jiffies(4000));
		} else {
			mca_vote(info->force_vout_6v_voter, "soc_limit", true, 1);	/* OS4: new */
			mca_vote(info->chg_enable_voter, "soc_limit", true, 0);
		}
	}
#line 2729
	mca_log_info("cur_step = %d\n", cur_step);
}

static void strategy_wireless_process_battery_health_change(struct strategy_wireless_dev *info)
{
	union power_supply_propval pval = {0,};
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy) {
#line 2741
		mca_log_err("batt_psy is not find, return\n");
		return;
	}

	power_supply_get_property(info->batt_psy, POWER_SUPPLY_PROP_HEALTH, &pval);
	if (pval.intval == POWER_SUPPLY_HEALTH_COLD)
		info->batt_cold = 1;
	else if (pval.intval == POWER_SUPPLY_HEALTH_OVERHEAT)
		info->batt_overhot = 1;
	else {
		info->batt_cold = 0;
		info->batt_overhot = 0;
	}

	if (info->online) {
		strategy_wireless_update_quick_charge_type(info);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				"POWER_SUPPLY_QUICK_CHARGE_TYPE=%d", info->proc_data.qc_type);
		event_data.event = event;
		event_data.event_len = len;
		mca_event_report_uevent(&event_data);
	}
}

static void strategy_wireless_process_pmic_init_done(struct strategy_wireless_dev *info)
{
#line 2767
	mca_log_info("deal with pmic_init_done\n");
	mca_rerun_election(info->input_limit_voter);
}

static int strategy_wireless_process_event(int event, int value, void *data)
{
	struct strategy_wireless_dev *info = data;
	bool if_qc_enable = false;
	int ret = 0;
	struct mca_votable *qcchg_disable_voter = NULL;

	if (!data)
		return -1;

#line 2850
	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		qcchg_disable_voter = mca_find_votable("wls_quick_chg_disable");
		if (qcchg_disable_voter)
			mca_vote(qcchg_disable_voter, "basic_wls_set", true, !value);
		strategy_wireless_process_online_change(value, info);
		break;
	case MCA_EVENT_WIRELESS_INT_CHANGE:
		strategy_wireless_split_irq_index(info, value);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		strategy_wireless_process_batt_btb_change(value, info);
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
#line 2866
		mca_log_info("receive batt_auth event, value %d\n", value);
		mca_vote(info->charge_limit_voter, "batt_auth", false, 0);
		if (info->online)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_BATT_EMERG:	/* GLOBAL OS4.0.0.7 event 85: FCC voter (+0x78 = charge_limit_voter), constant 500 (0x1f4, units as the other FCC votes: mA, inferred) */
		mca_vote(info->charge_limit_voter, "batt_emerg", true, 500);
		break;
	case MCA_EVENT_PMIC_INIT_DONE:
		strategy_wireless_process_pmic_init_done(info);
		break;
	case MCA_EVENT_WIRELESS_MAGNETIC_CASE_INT:
		if (value)
			mca_charge_mievent_report(CHARGE_DFX_WLS_MAGNETIC_CASE_ATTACH, NULL, 0);
		else
			mca_charge_mievent_set_state(MIEVENT_STATE_END, CHARGE_DFX_WLS_MAGNETIC_CASE_ATTACH);
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
#line 2872
		mca_log_info("adsp crash, stop charging\n");
		break;
	case MCA_EVENT_CHARGE_RESTORE:
#line 2875
		mca_log_info("adsp restore, enable charging\n");
		strategy_wireless_get_qc_enable(info, &if_qc_enable);
		if (if_qc_enable && (info->otg_boost_src == info->wls_vdd_src))
			strategy_wireless_enable_vdd(info, true);
		else if (!info->online) {
			info->proc_data.boost_wireless_vdd = false;
			ret = platform_class_buckchg_ops_set_wls_vdd_flag(MAIN_BUCK_CHARGER, info->proc_data.boost_wireless_vdd);
			if (ret) {
#line 2883
				mca_log_err("set wireless vdd boost fail, retry\n");
				schedule_delayed_work(&info->set_vdd_flag_work, msecs_to_jiffies(500));
			}
		}
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value)
			mca_vote(info->chg_enable_voter, "vbat_ovp", true, 0);
		else
			mca_vote(info->chg_enable_voter, "vbat_ovp", false, 1);
		break;
	case MCA_EVENT_WIRELESS_THERMAL_PHONE_FLAG:
#line 2895
		mca_log_info("thermal_phone_flag: %d\n", value);
		info->thermal_phone_flag = value;
		break;
	case MCA_EVENT_BATTERY_HEALTH_CHANGE:
#line 2899
		mca_log_info("battery health change\n");
		strategy_wireless_process_battery_health_change(info);
		break;
	case MCA_EVENT_BATTERY_TOTAL_ITERM:	/* stock process_event +0x384 (G13h: was missing) */
#line 2912
		mca_log_info("parallel_iterm: %d\n", value);
		info->parallel_iterm = value;
		break;
	default:
		break;
	}

	return 0;
}

static void strategy_wireless_check_rx_alarm(struct strategy_wireless_dev *info, bool *ocp_flag, bool *otp_flag, bool *otp_recovery)
{
	int rx_iout = 0;

	(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
	if (info->proc_data.forward_charger_mode == FORWARD_2_1_CHARGER_MODE)
		*ocp_flag = (rx_iout >= info->rx_max_iout[CHG_MODE_DIV2]);
	else
		*ocp_flag = (rx_iout >= info->rx_max_iout[CHG_MODE_DIV4]);

	(void)platform_class_wireless_get_temp(WIRELESS_ROLE_MASTER, &info->rx_temp);
	if(info->project_vendor == WLS_CHIP_VENDOR_SC96281){
		*otp_flag = (info->rx_temp >= MCA_WLS_CHG_RX_MAX_TEMP_SC96281);
		*otp_recovery = (info->rx_temp <= MCA_WLS_CHG_RX_MAX_TEMP_SC96281 - MCA_WLS_CHG_RX_OTP_DELTA_TEMP);
	}else{
		*otp_flag = (info->rx_temp >= MCA_WLS_CHG_RX_MAX_TEMP);
		*otp_recovery = (info->rx_temp <= MCA_WLS_CHG_RX_MAX_TEMP - MCA_WLS_CHG_RX_OTP_DELTA_TEMP);
	}
}

static void strategy_wireless_start_renego(struct strategy_wireless_dev *info)
{
	u8 max_power = 0;

	switch (info->proc_data.adapter_type) {
	case ADAPTER_XIAOMI_PD_40W:
	case ADAPTER_VOICE_BOX:
		max_power = 20;
		break;
	case ADAPTER_XIAOMI_PD_50W:
		/* OS4 stock: 25 only for SC96281 with the special UUID 0x09010a01 */
		if (info->project_vendor == WLS_CHIP_VENDOR_SC96281 && info->proc_data.uuid_value == 0x09010a01)
			max_power = 25;
		else
			max_power = 20;
		break;
	case ADAPTER_XIAOMI_PD_60W:
	case ADAPTER_XIAOMI_PD_100W:
		max_power = 25;
		break;
	default:
	break;
	}

	if (max_power > 0)
		(void)platform_class_wireless_do_renego(WIRELESS_ROLE_MASTER, max_power);
}

static int strategy_wireless_send_tx_q_value_request(struct strategy_wireless_dev *info)
{
	int ret = 0;
	u8 send_value = 0;
	int num;

	if (!info->online)
		return -1;

	info->mutex_lock_sts = true;

	num = strategy_wireless_get_compatible_info(info);
	if (num != -1) {
		if (info->uuid_adapter_info[num].compatible_info.low_inductance_50w_tx)
			send_value = info->tx_q1[0];
		if (info->uuid_adapter_info[num].compatible_info.low_inductance_80w_tx)
			send_value = info->tx_q1[1];
		if (info->uuid_adapter_info[num].compatible_info.magnet_30w_tx)	/* OS4: new */
			send_value = info->tx_q1[ADAPTER_MAGNET_TX_30W];
	}
	ret = platform_class_wireless_send_tx_q_value(WIRELESS_ROLE_MASTER, send_value);

	schedule_delayed_work(&info->mutex_unlock_work, msecs_to_jiffies(2000));
	return ret;
}

static void strategy_wireless_process_batt_btb_change(int value, struct strategy_wireless_dev *info);

static int strategy_wireless_find_voter(struct strategy_wireless_dev *info)
{
	info->charge_limit_voter = mca_find_votable("buck_charge_curr");
	if (!info->charge_limit_voter)
		goto find_voter_error;

	info->chg_enable_voter = mca_find_votable("chg_enable");
	if (!info->chg_enable_voter)
		goto find_voter_error;

	info->vterm_voter = mca_find_votable("term_volt");
	if (!info->vterm_voter)
		goto find_voter_error;

	info->iterm_voter = mca_find_votable("term_curr");
	if (!info->iterm_voter)
		goto find_voter_error;

	info->input_suspend_voter = mca_find_votable("input_suspend");
	if (!info->input_suspend_voter)
		goto find_voter_error;

	/* OS4 stock (inlined here): assume "battery missing" until the btb event clears it */
	strategy_wireless_process_batt_btb_change(1, info);
	return 0;

find_voter_error:
#line 3023
	mca_log_err("find voter fail, wait for it\n");
	return -1;
}

/* OS4 stock: the quick-wireless charge status is a parameter (the static of monitor_work is passed in);
 * the FFC decision uses info->fastcharge_mode (read once at the top of monitor_work) and info->proc_data.vbat_cell_mv */
static int strategy_wireless_enable_fast_charge_mode(struct strategy_wireless_dev *info, int quick_charge_status)
{
	int batt_temp, rx_vout_mv = 0, ffc_iterm = 0, normal_vterm = 0;
	int chgr_stat = CHARGER_STATUS_CHARGING_DISABLED;
	int effective_icl, effective_fcc;
	int ret = 0;
	static int fastchg_cnt;
	static int charge_done_cnt;

	if (info->proc_data.adapter_type > ADAPTER_AUTH_FAILED) {
		batt_temp = 0;

		if (info->batt_type == 0)
			(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_JEITA,
					STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM, &ffc_iterm);
		else if (info->support_base_flip)
			ffc_iterm = info->parallel_iterm;
		else
			ffc_iterm = mca_get_effective_result(info->iterm_voter);
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_JEITA,
				STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM, &normal_vterm);
		effective_icl = mca_get_effective_result(info->input_limit_voter);
		effective_fcc = mca_get_effective_result(info->charge_limit_voter);

		(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout_mv);
		if (rx_vout_mv <= BPP_DEFAULT_VOUT || rx_vout_mv >= EPP_PLUS_VOUT)
			rx_vout_mv = EPP_DEFAULT_VOUT;

		(void)strategy_class_fg_ops_get_temperature(&batt_temp);
		batt_temp /= 10;

		(void)strategy_class_fg_ops_get_voltage(&info->proc_data.vbat_cell_mv);
		strategy_wireless_get_chgr_stage(info, &info->proc_data.chgr_stage);
#line 3061
		mca_log_info("batt_temp:%d, fastcharge_mode:%d, ffc_iterm:%d, effective_icl:%d, effective_fcc:%d, vbat_cell_mv:%d, rx_vout_mv:%d\n",
					batt_temp, info->fastcharge_mode, ffc_iterm, effective_icl, effective_fcc, info->proc_data.vbat_cell_mv, rx_vout_mv);

		if (info->proc_data.chgr_stage == WLS_FULL_MODE || info->sw_cv_running)
			return ret;

		if (info->proc_data.chgr_stage == WLS_RECHG_MODE) {
			if (info->fastcharge_mode) {
				strategy_class_fg_set_fastcharge(false);
#line 3070
				mca_log_info("chgr_state is %d, exit ffc mode\n", info->proc_data.chgr_stage);
				return ret;
			}
#line 3073
			mca_log_info("chgr_state is %d, keep normal mode\n", info->proc_data.chgr_stage);
			return ret;
		}

		/* charge-done debounce (OS4): ffc_iterm >= 2101 mA, no base flip, quick charge not running: 4 loops -> leave FFC */
		if (quick_charge_status != MCA_QUICK_CHG_STS_CHARGING && ffc_iterm >= 2101 && !info->support_base_flip) {
			if (info->fastcharge_mode) {
				charge_done_cnt++;
				if (charge_done_cnt >= 4) {
					charge_done_cnt = 0;
					strategy_class_fg_set_fastcharge(false);
#line 3083
					mca_log_info("quick_charge_status is %d, exit ffc mode\n", quick_charge_status);
					return ret;
				}
#line 3086
				mca_log_info("charge_done_cnt < thre, continue\n");
			} else {
				charge_done_cnt = 0;
#line 3089
				mca_log_info("quick_charge_status is %d, keep normal mode\n", quick_charge_status);
				return ret;
			}
		} else {
			charge_done_cnt = 0;
		}

		if (normal_vterm && (info->proc_data.vbat_cell_mv >= normal_vterm - info->smartchg_data.delta_fv - CHARGE_SW_CV_VBAT_ALARM_DELTA) &&
			(effective_fcc < ffc_iterm || effective_icl * rx_vout_mv / info->proc_data.vbat_cell_mv < ffc_iterm)) {
			if (info->fastcharge_mode) {
				fastchg_cnt++;
				if (fastchg_cnt > MCA_WLS_CHG_ENABLE_FASTCHG_CNT) {
					fastchg_cnt = 0;
					strategy_class_fg_set_fastcharge(false);
#line 3103
					mca_log_info("vcell is high and max fcc is low, exit ffc mode to avoid vcell over voltage\n");
					return ret;
				}
#line 3106
				mca_log_info("fastchg_cnt < thre, continue\n");
			} else {
				fastchg_cnt = 0;
#line 3109
				mca_log_info("vcell is high and max fcc is low, keep normal mode to avoid vcell over voltage\n");
				return ret;
			}
		} else {
#line 3113
			mca_log_info("vcell is low or max_fcc is high, clean fastchg_cnt\n");
			fastchg_cnt = 0;
		}

		if (batt_temp < info->ffc_temp_low || batt_temp >= info->ffc_temp_high) {
			if (info->fastcharge_mode) {
				strategy_class_fg_set_fastcharge(false);
#line 3120
				mca_log_info("temp is over, wireless buck charger disable fast charge mode\n");
				return ret;
			}
#line 3123
			mca_log_info("temp is over, wireless buck charger keep normal charge mode\n");
			return ret;
		}

		(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_stat);
		if (normal_vterm && (info->proc_data.vbat_cell_mv < normal_vterm - info->smartchg_data.delta_fv - 2 * CHARGE_SW_CV_VBAT_ALARM_DELTA) &&
			!info->fastcharge_mode && chgr_stat != CHARGER_STATUS_TAPER) {
			strategy_class_fg_set_fastcharge(true);
#line 3129
			mca_log_info("wireless buck charger enable fast charge mode\n");
		}
	}
	return ret;
}

static void strategy_wireless_sw_cv_start(struct strategy_wireless_dev *info)
{
	info->sw_cv_running = true;
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	/* OS4: the base-flip hardware uses its own sw_cv worker */
	mca_queue_delayed_work(info->support_base_flip ? &info->base_flip_sw_cv_work : &info->sw_cv_work,
		msecs_to_jiffies(CHARGE_SW_CV_WORK_FAST_INTERVAL));
}

static void strategy_wireless_sw_cv_stop(struct strategy_wireless_dev *info)
{
#line 3147
	mca_log_info("chgr_stage: %d, stop sw_cv_work\n", info->proc_data.chgr_stage);
	cancel_delayed_work(&info->sw_cv_work);
	cancel_delayed_work(&info->base_flip_sw_cv_work);
	mca_vote(info->charge_limit_voter, "sw_cv", false, 0);
	info->sw_cv_running = false;
	info->vbat_ov_count = 0;
}

/* OS4 stock (inlined helpers of monitor_work, log lines 471 / 664..672 of the stock source) */
static void strategy_wireless_bridge_icl_setting(struct strategy_wireless_dev *info, bool en, int mA)
{
#line 471
	mca_log_info("bridge_sw en: %d, ma: %u\n", en, mA);

	if (en)
		mca_vote(info->input_limit_voter, "sw_bridge", true, mA);
	else
		mca_vote(info->input_limit_voter, "sw_bridge", false, 0);
}

/* only on hwid platform_version 5: bridge_status 0 = FULL bridge (stock log text), then the RX is set to 6 V / 300 mA ICL and the
 * bridge is switched to HALF as soon as rx_vout <= 6500 mV and rx_iout <= 400 mA (15 tries, 300 ms apart) */
static void strategy_wireless_switch_to_half_bridge(struct strategy_wireless_dev *info)
{
	u8 brg_status = 0;
	int rx_vout = 0, rx_iout = 0;
	unsigned int cnt = 15;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!hwid || hwid->platform_version != 5)
		return;

	(void)platform_class_wireless_get_rx_brg_status(WIRELESS_ROLE_MASTER, &brg_status);
	if (brg_status) {
#line 672
		mca_log_err("bridge_sw no need switch, bridge is HALF\n");
		return;
	}

	info->in_switch_bridge = 1;
	(void)platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, 6000);
	strategy_wireless_bridge_icl_setting(info, true, 300);
	do {
		msleep(300);
		(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout);
		(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
		if (rx_vout <= 6500 && rx_iout <= 400) {
			(void)platform_class_wireless_switch_bridge(WIRELESS_ROLE_MASTER, 0);
			break;
		}
#line 664
		mca_log_err("bridge_sw wait, rx_vout %d, rx_iout %d \n", rx_vout, rx_iout);
	} while (--cnt);

	info->in_switch_bridge = 0;
	msleep(20);
	(void)platform_class_wireless_get_rx_brg_status(WIRELESS_ROLE_MASTER, &brg_status);
	strategy_wireless_bridge_icl_setting(info, false, 0);
#line 670	/* G14k: manual pin: pb_pin3 cannot match this UTF-8 string against the latin-1 rodata dump (stock 670, was 671 by drift) */
	mca_log_err("bridge_sw switch bridge over, bridge_state %d 【0】is FULL Bridge\n", brg_status);
}

static void strategy_wireless_monitor_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, monitor_work.work);
	int interval;
	static int quick_charge_status = MCA_QUICK_CHG_STS_CHARGE_FAILED;
	static int last_quick_enable = -1;
	int pre_quick_status;
	bool if_qc_enable = false;
	int vterm = 0;
	int input_suspend = 0, chg_en = 0;
	int chgr_stat = 0;

#line 3168
	mca_log_info("wireless monitor work\n");
	if (!info->thermal_phone_flag)
		(void)mca_vote(info->input_limit_voter, "thermal_phone", false, 0);

	strategy_wireless_get_qc_enable(info, &if_qc_enable);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, MCA_EVENT_CHARGE_ACTION, if_qc_enable);
	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_stat);

	/* OS4 stock: the quick-charge enable flag was unchanged and is off: try the FULL -> HALF bridge switch (platform_version 5) */
	if (last_quick_enable == if_qc_enable) {
		if (!if_qc_enable)
			strategy_wireless_switch_to_half_bridge(info);
	} else {
		last_quick_enable = if_qc_enable;
	}

	info->fastcharge_mode = strategy_class_fg_get_fastcharge();
	pre_quick_status = quick_charge_status;
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
				STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING && pre_quick_status == MCA_QUICK_CHG_STS_CHARGING)
		interval = 5000;
	else
		interval = ((info->fastcharge_mode != 0) & (chgr_stat == CHARGER_STATUS_TAPER)) ? 6000 : 10000;

	/* OS4: 30 W "magnet" TX (UUID 0x0e0e0205) on FUDA1661: 13 A FCC while the 4:1 CP charge runs (back to 2:1 after 15000 jiffies), else 6 A */
	if (info->proc_data.uuid_value == 0x0e0e0205 && info->project_vendor == WLS_CHIP_VENDOR_FUDA1661) {
		if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING && info->proc_data.is_4_1_mode) {
			strategy_wireless_fcc_setting(info, true, 13000);
			info->proc_data.wireless_power.max_fcc = 13000;
			queue_delayed_work(system_wq, &info->change_cp_mode_work, 15000);
		} else {
			cancel_delayed_work(&info->change_cp_mode_work);
			strategy_wireless_fcc_setting(info, true, 6000);
			info->proc_data.wireless_power.max_fcc = 6000;
		}
	}

	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING)
		goto out;

	//WA: slove pmic adnormal status
	input_suspend = mca_get_effective_result(info->wls_input_suspend_voter);
	chg_en = mca_get_effective_result(info->chg_enable_voter);
	if (!input_suspend && info->proc_data.chgr_stage != WLS_FULL_MODE &&
		chg_en && chgr_stat == CHARGER_STATUS_CHARGING_DISABLED) {
		platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, false);
		platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, true);
#line 3218
		mca_log_err("recover wireless buck charging\n");
	}

	strategy_wireless_enable_fast_charge_mode(info, quick_charge_status);

	if (info->proc_data.chgr_stage == WLS_FULL_MODE)
		strategy_wireless_sw_cv_stop(info);

	if (info->support_base_flip)
		vterm = mca_get_client_vote(info->vterm_voter, "jeita");
	else
		vterm = mca_get_effective_result(info->vterm_voter);
	if (info->sw_cv_running) {
		if (!chg_en || info->proc_data.vbat_cell_mv <= vterm - 20) {
#line 3237
			mca_log_err("vbat: %d, vterm: %d, chg_en: %d, stop sw_cv_work\n", info->proc_data.vbat_cell_mv, vterm, chg_en);
			strategy_wireless_sw_cv_stop(info);
		}
	} else if (info->proc_data.chgr_stage != WLS_FULL_MODE &&
		vterm >= MCA_WLS_CHG_VTERM_LOW_TH && info->proc_data.vbat_cell_mv >= vterm - CHARGE_SW_CV_VBAT_ALARM_DELTA &&
		pre_quick_status != MCA_QUICK_CHG_STS_CHARGING) {
#line 3234
		mca_log_err("vbat: %d, vterm: %d, start sw_cv_work\n", info->proc_data.vbat_cell_mv, vterm);
		strategy_wireless_sw_cv_start(info);
	}

out:
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(interval));
}

#define FCC_STEP 50
#define FV_STEP 5
static void strategy_wireless_buckchg_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, sw_cv_work.work);
	static int sw_cv_volt_delta_map[][2] = {
		{1000, 5},
		{0, 2}
	};
	int interval = CHARGE_SW_CV_WORK_NORMAL_INTERVAL;
	int vbat, ibat;
	int vterm = mca_get_effective_result(info->vterm_voter);
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int volt_delta = sw_cv_volt_delta_map[0][1];
	int quick_sts = 3;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat_cell_mv);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat_ma);
	vbat = info->proc_data.vbat_cell_mv;
	ibat = -info->proc_data.ibat_ma / 1000;
#line 3276
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, iterm: %d, fcc: %d\n",
		vbat, ibat, vterm, iterm, fcc);

	for (int i = 0; i < sizeof(sw_cv_volt_delta_map) / sizeof(sw_cv_volt_delta_map[0]); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (vbat >= vterm - volt_delta) {
		interval = CHARGE_SW_CV_WORK_FAST_INTERVAL;
		if (ibat - FCC_STEP > iterm) {
			if (fcc - ibat >= 2 * FCC_STEP)
				mca_vote(info->charge_limit_voter, "sw_cv", true, ibat / FCC_STEP * FCC_STEP);
			else
				mca_vote(info->charge_limit_voter, "sw_cv", true, fcc - FCC_STEP);
		}
	} else {
		interval = CHARGE_SW_CV_WORK_NORMAL_INTERVAL;
	}

	/* OS4: the quick-wireless charge status decides whether a battery overvoltage only switches the fast charge to normal */
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS, STRATEGY_STATUS_TYPE_CHARGING, &quick_sts);

	if (vbat >= vterm) {
		if (hwid && hwid->platform_version == 3 && quick_sts != 1 &&
		    (quick_sts == 2 || hwid->country_version == 1)) {
#line 3304
			mca_log_err("WARNING: batt ov, fc switch normal\n");
		} else {
#line 3306
			mca_log_err("WARNING: batt ov, reduce fv\n");
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, vterm + info->pmic_fv_compensation - FV_STEP);
		}
	}

	mca_queue_delayed_work(&info->sw_cv_work, msecs_to_jiffies(interval));
}

static void strategy_wireless_find_voter_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, find_voter_work.work);
	int ret = 0;

	ret = strategy_wireless_find_voter(info);
	if (ret) {
#line 3415
		mca_log_err("find voter fail\n");
		schedule_delayed_work(&info->find_voter_work, msecs_to_jiffies(500));
	}
}

static void strategy_wireless_update_soc_decimal_work(struct work_struct *work)
{
	//struct strategy_wireless_dev *info = container_of(work,
	//	struct strategy_wireless_dev, report_soc_decimal_work.work);
	int len = 0;
	int soc_decimal, rate;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data;

#line 3431
	mca_log_info("wireless update soc decimal work\n");

	(void)strategy_class_fg_ops_get_soc_decimal(&soc_decimal, &rate);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_SOC_DECIMAL=%d", soc_decimal);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);

	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
		"POWER_SUPPLY_SOC_DECIMAL_RATE=%d", rate);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void strategy_wireless_renegociation_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, renegociation_work.work);

	strategy_wireless_start_renego(info);

	if (info->proc_data.renegociation_cnt < 3) {
		schedule_delayed_work(&info->renegociation_work, msecs_to_jiffies(1500));
		++info->proc_data.renegociation_cnt;
	}
#line 3459
	mca_log_info("wireless renegociation work\n");
}

static void strategy_wireless_loop_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, wireless_loop_work.work);

#line 3467
	mca_log_info("wireless loop work\n");

	(void)strategy_wireless_get_charging_info(info);
	(void)strategy_wireless_charge_loop_work(info);

	schedule_delayed_work(&info->wireless_loop_work, msecs_to_jiffies(10000));
}

static void strategy_wireless_rx_fastcharge_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, rx_fastcharge_work.work);

	strategy_wireless_set_fastchg_adapter(info);
	if ((info->proc_data.adapter_type == ADAPTER_XIAOMI_QC3 ||
			info->proc_data.adapter_type == ADAPTER_XIAOMI_PD ||
			info->proc_data.adapter_type == ADAPTER_ZIMI_CAR_POWER) &&
			(info->proc_data.batt_soc >= 95))
		return;

	if (info->proc_data.set_fastcharge_vout_cnt < FAST_CHARGE_RETRY_MAX_CNT &&
		info->proc_data.adapter_type >= ADAPTER_XIAOMI_QC3) {
		schedule_delayed_work(&info->rx_fastcharge_work, msecs_to_jiffies(3500));
		++info->proc_data.set_fastcharge_vout_cnt;
	} else if (info->proc_data.set_fastcharge_vout_cnt >= FAST_CHARGE_RETRY_MAX_CNT) {
#line 3492
		mca_log_err("fastcharge retry failed\n");
		mca_charge_mievent_report(CHARGE_DFX_WLS_FASTCHG_FAIL, NULL, 0);
	}

#line 3496
	mca_log_info("wireless rx fastcharge work\n");
}

static void strategy_wireless_drawload_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, wls_drawload_work.work);

	int chgr_stat;
	int cnt = 0;
	int rx_vout = 0, rx_iout = 0, iwls = 0;

	(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_stat);

	while (cnt++ <= 50) {
		(void)platform_class_buckchg_ops_get_wls_curr(MAIN_BUCK_CHARGER, &iwls);
		(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout);
		(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
		(void)platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_stat);
#line 3515
		mca_log_info("BPP drawload, iwls:%d, chgr_sts:%d, iout:%d, vout:%d\n", iwls, chgr_stat, rx_iout, rx_vout);
		if (chgr_stat == CHARGER_STATUS_FULLON || chgr_stat == CHARGER_STATUS_TAPER)
			break;
		if (strategy_wireless_msleep(50, info))
			return;
	}

	msleep(100);
	if (info->proc_data.epp) {
		if (info->proc_data.tx_max_power == 15)
			strategy_wireless_stepper_pmic_icl(info, 200, 1300, 100, 20);
		else
			strategy_wireless_stepper_pmic_icl(info, 200, 800, 100, 20);
	} else
		strategy_wireless_stepper_pmic_icl(info, 250, 750, 100, 20);

#line 3531
	mca_log_info("wireless drawload work\n");
}

static void strategy_wireless_rx_alarm_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, rx_alarm_work.work);

	bool ocp_flag = false, otp_flag = false, otp_recovery = false;
	int current_fcc_val = 0;
	int quick_charge_status = MCA_QUICK_CHG_STS_CHARGE_FAILED;

	strategy_wireless_check_rx_alarm(info, &ocp_flag, &otp_flag, &otp_recovery);
#line 3544
	mca_log_info("ocp_otp alarm ocp_flag:%d otp_flag:%d\n", ocp_flag, otp_flag);
	if ((ocp_flag == false) && (otp_flag == false) && (otp_recovery == true))
	{
		strategy_wireless_otp_fcc_setting(info, false, 0);
		return;
	}

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING)
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			STRATEGY_STATUS_TYPE_QC_IBAT_MAX, &current_fcc_val);
	else
		current_fcc_val = mca_get_effective_result(info->charge_limit_voter);

	if (ocp_flag) {
		if (current_fcc_val - 100 > 0)
			strategy_wireless_fcc_setting(info, true, (current_fcc_val - 100));
	}
	if (otp_flag) {
		if (current_fcc_val - 500 > 0)
			strategy_wireless_otp_fcc_setting(info, true, (current_fcc_val - 500));
	}

	schedule_delayed_work(&info->rx_alarm_work, msecs_to_jiffies(4000));
}

static void strategy_wireless_max_power_control_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info =  container_of(work,
		struct strategy_wireless_dev, max_power_control_work.work);
	int num;

	num = strategy_wireless_get_compatible_info(info);
	if ((!info->online) || (info->proc_data.adapter_type < ADAPTER_XIAOMI_PD_50W))
		return;

	if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1661 &&
			info->proc_data.adapter_type > ADAPTER_XIAOMI_PD_50W) {
		if (num == -1)
			return;
		else if (info->uuid_adapter_info[num].compatible_info.standard_tx) {
#line 3585	/* G14k: stock: two log sites merged (phi{3585|3590}); the FUDA1661 80 W branch logs line 3585, the 50 W branch 3590 */
			mca_log_info("max_power_control\n");
			strategy_wireless_fcc_setting(info, true, MCA_WLS_CHG_REDUCE_FCC_VALUE_80W_MA);
		}
	} else if (info->project_vendor == WLS_CHIP_VENDOR_FUDA1651 ||
			info->project_vendor == WLS_CHIP_VENDOR_SC96281) {
#line 3590
		mca_log_info("max_power_control\n");
		strategy_wireless_fcc_setting(info, true, MCA_WLS_CHG_REDUCE_FCC_VALUE_50W_MA);
	}
}

static void strategy_wireless_fw_state_work(struct work_struct *work)
{
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

#line 3601
	mca_log_info("wireless fw_state work\n");

	mca_wireless_rev_set_firmware_state(FIRMWARE_NEED_UPDATE);
	len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_WLS_FW_STATE=%d", FIRMWARE_NEED_UPDATE);
	event_data.event = event;
	event_data.event_len = len;
	mca_event_report_uevent(&event_data);
}

static void strategy_wireless_set_vdd_flag_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, set_vdd_flag_work.work);
	int ret = 0;

	ret = platform_class_buckchg_ops_set_wls_vdd_flag(MAIN_BUCK_CHARGER, info->proc_data.boost_wireless_vdd);
	if (ret) {
#line 3619
		mca_log_err("set wireless vdd boost fail, retry\n");
		schedule_delayed_work(&info->set_vdd_flag_work, msecs_to_jiffies(500));
	}
}

static void strategy_wireless_soc_limit_stepper_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, soc_limit_stepper_work.work);

	strategy_wireless_process_soc_limit_change(info->soc_limit, info);
}

static void strategy_wireless_update_wireless_thermal_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, update_wireless_thermal_work.work);

	if (info->online)
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_THERMAL,
		MCA_EVENT_WIRELESS_EPP_MODE, 0);
}

static void strategy_wireless_mutex_unlock_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, mutex_unlock_work.work);

	if (info->mutex_lock_sts) {
		info->mutex_lock_sts = false;
		wake_up_interruptible(&info->wait_que);
	}
}

static void strategy_wireless_process_trans_func(struct strategy_wireless_dev *info,
	struct trans_data_lis_node *node)
{
	switch (node->data_flag){
	case TRANS_DATA_FLAG_SOC:
		strategy_wireless_update_soc_to_tx(info);
		break;
	case TRANS_DATA_FLAG_QVALUE:
		strategy_wireless_send_tx_q_value_request(info);
		break;
	case TRANS_DATA_FLAG_FAN_SPEED:
		strategy_wireless_quiet_sts_func(info);
		break;
	case TRANS_DATA_FLAG_VOUT_RANGE:
#line 3667
		mca_log_info("trans data value: %d\n", node->value);
		strategy_wireless_send_vout_range_request(info, node->value);
		break;
	case TRANS_DATA_FLAG_FREQUENCE:
#line 3671
		mca_log_info("trans data value: %d\n", node->value);
		strategy_wireless_send_frequency_request(info, node->value);
		break;
	default:
#line 3675
		mca_log_err("not support this type\n");
		break;
	}
}

static int strategy_wireless_process_trans(struct strategy_wireless_dev *info)
{
	struct trans_data_lis_node *cur_node, *temp_node;

	while (!list_empty(&info->header) && !info->mutex_lock_sts) {
		spin_lock(&info->list_lock);
		list_for_each_entry_safe(cur_node, temp_node, &info->header, lnode) {
			if (info->mutex_lock_sts)
				break;
			list_del(&cur_node->lnode);
			spin_unlock(&info->list_lock);

#line 3692
			mca_log_info("cur_node: data_flag: %d, value: %d\n", cur_node->data_flag, cur_node->value);
			strategy_wireless_process_trans_func(info, cur_node);

			spin_lock(&info->list_lock);
			kfree(cur_node);
			info->head_cnt--;
		}
		spin_unlock(&info->list_lock);
	}

	if (!info->online)
		return 1;

	return 0;
}

static void strategy_wireless_trans_data_work(struct work_struct *work)
{
	struct strategy_wireless_dev *info = container_of(work,
		struct strategy_wireless_dev, trans_data_work.work);

	while (info->online)
		wait_event_interruptible(info->wait_que,
			(strategy_wireless_process_trans(info)));
}

static int strategy_wireless_parse_cmd_type_para(struct device_node *node,
	const char *name, struct adapter_cmd_type_info *cmd_type_info)
{
	int array_len, i, j, len_temp;
	int idata[ADAPTER_MAX * ADAPTER_CMD_TYPE_SETTING_MAX] = { 0 };

	array_len = mca_parse_dts_u32_count(node, name, ADAPTER_MAX,
		ADAPTER_CMD_TYPE_SETTING_MAX);
	if (array_len < 0) {
#line 3727
		mca_log_err("parse %s failed", name);
		return -1;
	}

	if (mca_parse_dts_u32_array(node, name, idata, array_len)) {
#line 3732
		mca_log_err("parse %s data failed", name);
		return -1;
	}

	len_temp = array_len / ADAPTER_CMD_TYPE_SETTING_MAX;
	cmd_type_info->cmd_fx = kcalloc(len_temp, sizeof(struct adapter_cmd_type_fx), GFP_KERNEL);

	for (i = 0; i < len_temp; i++) {
		j = ADAPTER_CMD_TYPE_SETTING_MAX * i;
		cmd_type_info->cmd_fx[i].icl_setting = idata[j + ADAPTER_CMD_TYPE_SETTING_ICL];
		cmd_type_info->cmd_fx[i].fcc_setting = idata[j + ADAPTER_CMD_TYPE_SETTING_FCC];
#line 3743
		mca_log_debug("[%d] icl_setting: %d; fcc_setting: %d\n",
			i, cmd_type_info->cmd_fx[i].icl_setting, cmd_type_info->cmd_fx[i].fcc_setting);
	}
	return 0;

}

static int strategy_wireless_parse_adapter_cmd_type_info(struct strategy_wireless_dev *info)
{
	struct device_node *node = info->dev->of_node;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "adapter_cmd_type_para",
		ADAPTER_CMD_TYPE_MAX,
		ADAPTER_CMD_TYPE_FX_MAX);
	if (array_len < 0) {
#line 3760
		mca_log_err("parse adapter_cmd_type_para failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "adapter_cmd_type_para", i, &tmp_string))
			return -1;

		row = i / ADAPTER_CMD_TYPE_FX_MAX;
		col = i % ADAPTER_CMD_TYPE_FX_MAX;
#line 3770
		mca_log_debug("[%d]adapter_cmd_type_para %s\n", i, tmp_string);
		switch (col) {
		case ADAPTER_CMD_TYPE_FX:
			if (kstrtou8(tmp_string, 16, &info->cmd_type_info[row].receive_data))
				return -1;
			break;
		case ADAPTER_CMD_TYPE_FX_PARA:
			if (strategy_wireless_parse_cmd_type_para(node, tmp_string, &info->cmd_type_info[row]))
				return -1;
			break;
		default:
			break;
		}
	}
	return 0;
}

static int strategy_wireless_parse_adapter_para(struct device_node *node,
	const char *name, struct wireless_compatible_info *compatible_info)
{
	int array_len, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, name,
		ADAPTER_PARA_MAX_GROUP,
		MCA_BASIC_WLS_COMPATIBLE_PARA_MAX);
	if (array_len < 0) {
#line 3797
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		col = i % MCA_BASIC_WLS_COMPATIBLE_PARA_MAX;
#line 3806
		mca_log_debug("[%d]uuid_adapter para %s\n", i, tmp_string);
		switch (col) {
		case MCA_BASIC_WLS_STANDARD:
			if (kstrtoint(tmp_string, 2, &compatible_info->standard_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_MUSICAL:
			if (kstrtoint(tmp_string, 2, &compatible_info->musical_box))
				return -1;
			break;
		case MCA_BASIC_WLS_PLATE:
			if (kstrtoint(tmp_string, 2, &compatible_info->plate_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_CAR:
			if (kstrtoint(tmp_string, 2, &compatible_info->car_mounted))
				return -1;
			break;
		case MCA_BASIC_WLS_TRAIN:
			if (kstrtoint(tmp_string, 2, &compatible_info->train_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_REDMI_30W:
			if (kstrtoint(tmp_string, 2, &compatible_info->redmi_30w_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_SAILBOAT:
			if (kstrtoint(tmp_string, 2, &compatible_info->sailboat_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_LOWINDUCTANCE_50W:
			if (kstrtoint(tmp_string, 2, &compatible_info->low_inductance_50w_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_LOWINDUCTANCE_80W:
			if (kstrtoint(tmp_string, 2, &compatible_info->low_inductance_80w_tx))
				return -1;
			break;
		case MCA_BASIC_WLS_SUPPORT_FAN:
			if (kstrtoint(tmp_string, 2, &compatible_info->support_fan))
				return -1;
			break;
		case MCA_BASIC_WLS_MAGNET_30W_TX:
			if (kstrtoint(tmp_string, 2, &compatible_info->magnet_30w_tx))
				return -1;
			break;
		default:
			break;
		}
	}
	return 0;
}

static int strategy_wireless_parse_adapter_info(struct strategy_wireless_dev *info)
{
	struct device_node *node = info->dev->of_node;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "adapter_para",
		UUID_PARA_MAX_GROUP,
		MCA_BASIC_WLS_UUID_PARA_MAX);
	if (array_len < 0) {
#line 3869
		mca_log_err("parse adapter_para failed\n");
		return -1;
	}

	info->uuid_adapter_info->uuid_para_size = array_len / MCA_BASIC_WLS_UUID_PARA_MAX;

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "adapter_para", i, &tmp_string))
			return -1;

		row = i / MCA_BASIC_WLS_UUID_PARA_MAX;
		col = i % MCA_BASIC_WLS_UUID_PARA_MAX;
#line 3881
		mca_log_debug("[%d]wls_adapter para %s\n", i, tmp_string);
		switch (col) {
		case MCA_BASIC_WLS_UUID_NAME:
			if (kstrtoint(tmp_string, 16, &info->uuid_adapter_info[row].uuid))
				return -1;
			break;
		case MCA_BASIC_WLS_UUID_PARA:
			if (strategy_wireless_parse_adapter_para(node, tmp_string, &info->uuid_adapter_info[row].compatible_info))
				return -1;
			break;
		default:
			break;
		}
	}
	return 0;
}

static int strategy_wireless_get_status(int status, void *value, void *data)
{
	struct strategy_wireless_dev *info = (struct strategy_wireless_dev *)data;
	int *cur_val = (int *)value;
	bool enable = false;

	if (!info || !value)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_INIT_OK:	/* stock jump table +0x14c, case 1 -> +0x78 (G13h: was missing) */
		*cur_val = info->init_ok;
		break;
	case STRATEGY_STATUS_TYPE_ONLINE:
		platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, cur_val);
		break;
	case STRATEGY_STATUS_TYPE_QC_TYPE:
		*cur_val = info->proc_data.qc_type;
		break;
	case STRATEGY_STATUS_TYPE_QC_ENABLE:
		strategy_wireless_get_qc_enable(info, &enable);
		*cur_val = (int)enable;
		break;
	case STRATEGY_STATUS_TYPE_POWER_MAX:
		*cur_val = info->proc_data.wireless_power.max_power;
		break;
	case STRATEGY_STATUS_TYPE_REV_TEST:
		*cur_val = info->wait_for_reverse_test;
		break;
	case STRATEGY_STATUS_TYPE_WLS_MAGNET_LIMIT:
		*cur_val = info->online && info->proc_data.magnet_tx_flag && info->proc_data.magnetic_case_flag;
		break;
	default:
		return -1;
	}

	return 0;
}

/* OS4 stock: a separate static function (inlined by the compiler); tx_q1/tx_q2 are only taken for adapter types with support_q != 0 */
static void strategy_wireless_parse_q_vaule(struct strategy_wireless_dev *info)
{
	struct device_node *node = info->dev->of_node; int ret;	/* G52a: stock re-reads info->dev->of_node (not the caller's cached node) */
	u32 support_q_tx[ADAPTER_TYPE_MAX] = { 0 };
	u8 *tx_q1_buf = NULL;
	u32 *tx_q2_buf = NULL;
	int count, i;

	ret = mca_parse_dts_u32_array(node, "support_q_tx", support_q_tx, ADAPTER_TYPE_MAX);
	if (ret) {
		info->support_q[0] = 1;	/* defaults 1, 1, 0 as in the stock code */
		info->support_q[1] = 1;
		info->support_q[2] = 0;
	} else
		memcpy(info->support_q, support_q_tx, sizeof(support_q_tx));
#line 3954
	mca_log_err("support_q %d %d %d\n", info->support_q[0], info->support_q[1], info->support_q[2]);

	for (i = 0; i < ADAPTER_TYPE_MAX; i++)
		info->tx_q1[i] = MCA_WLS_CHG_DEFAULT_TX_Q1;
	count = of_property_count_elems_of_size(node, "tx_q1", sizeof(u8));
	if (count >= 1) {
		tx_q1_buf = kzalloc(count, GFP_KERNEL);	/* G13o: stock __kmalloc flags 0xdc0 = zeroed */
		if (!tx_q1_buf) {
#line 3962
			mca_log_err("alloc memory for tx_q1 failed\n");
			goto q_out;
		}
		if (!mca_parse_dts_u8_array(node, "tx_q1", tx_q1_buf, count)) {
			for (i = 0; i < count; i++)
				if (info->support_q[i])
					info->tx_q1[i] = tx_q1_buf[i];
		}
	}
#line 3975
	mca_log_err("tx_q1 0x%02x 0x%02x 0x%02x, len %d\n", info->tx_q1[0], info->tx_q1[1], info->tx_q1[2], count);

	for (i = 0; i < ADAPTER_TYPE_MAX; i++)
		info->tx_q2[i] = MCA_WLS_CHG_DEFAULT_TX_Q2;
	count = of_property_count_elems_of_size(node, "tx_q2", sizeof(u32));
	if (count >= 1) {
		tx_q2_buf = kzalloc(count * sizeof(u32), GFP_KERNEL);	/* G13o: stock zeroed */
		if (!tx_q2_buf) {
#line 3983
			mca_log_err("alloc memory for tx_q2 failed\n");
			goto q_out;
		}
		if (!mca_parse_dts_u32_array(node, "tx_q2", tx_q2_buf, count)) {
			for (i = 0; i < count; i++)
				if (info->support_q[i])
					info->tx_q2[i] = tx_q2_buf[i];
		}
	}
#line 3996
	mca_log_err("tx_q2 %d %d %d, len %d\n", info->tx_q2[0], info->tx_q2[1], info->tx_q2[2], count);
q_out:
	kfree(tx_q2_buf);
	kfree(tx_q1_buf);
}

static int strategy_wireless_parse_dt(struct strategy_wireless_dev *info)
{
	struct device_node *node = info->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int ret = 0;
	u32 idata[CHG_MODE_MAX] = { 0 };

	if (!node) {
#line 4012
		mca_log_err("device tree node missing\n");
		return -EINVAL;
	}
	if (!hwid)
		return -ENOMEM;

	(void)mca_parse_dts_u32(node, "support_q_value", &info->support_q_value, 0);
	if (info->support_q_value)
		strategy_wireless_parse_q_vaule(info);
	(void)mca_parse_dts_u32(node, "batt_type", &info->batt_type, 0);
	(void)mca_parse_dts_u32(node, "max_power", &info->max_power, 50);
	(void)mca_parse_dts_u32(node, "support_mode", &info->support_mode, 4);
	(void)mca_parse_dts_u32(node, "support_multi_buck",
		&info->support_multi_buck, MCA_WLS_SUPPORT_MULTI_BUCK);
	(void)mca_parse_dts_u32(node, "wls_vdd_src", &info->wls_vdd_src,
		EXTERNAL_BOOST);
	if (hwid->platform_version == 1 &&
	    (hwid->build_version == 0 || (hwid->build_version == 1 && hwid->minor_version == 0))) {
		info->wls_vdd_src = EXTERNAL_BOOST;
#line 4033
		mca_log_err("O2 p1.1 change wls vdd boost from external to haptic\n");
	}
	(void)mca_parse_dts_u32(node, "phone_icl", &info->phone_icl, 200);
	(void)mca_parse_dts_u32(node, "offstd_phone_icl", &info->offstd_phone_icl, 200);
	(void)mca_parse_dts_u32(node, "phone_vol", &info->phone_vol, 6000);

	(void)mca_parse_dts_u32(node, "ffc_temp_low", &info->ffc_temp_low, ALLOW_FFC_TEMP_LOW_THR);
	(void)mca_parse_dts_u32(node, "ffc_temp_high", &info->ffc_temp_high, ALLOW_FFC_TEMP_HIGH_THR);
	(void)mca_parse_dts_u32(node, "support-hall", &info->support_hall, 0);
	(void)mca_parse_dts_u32(node, "pmic_fv_compensation", &info->pmic_fv_compensation, 0);
	(void)mca_parse_dts_u32(node, "soc_limit_fcc", &info->soc_limit_fcc, 6000);
	info->support_base_flip = of_find_property(node, "support-base-flip", NULL) != NULL;
	ret = mca_parse_dts_u32_array(node, "rx_max_iout", idata, CHG_MODE_MAX);
	if (ret) {
		info->rx_max_iout[CHG_MODE_DIV2] = MCA_WLS_CHG_DEFAULT_DIV2_RX_MAX_IOUT;
		info->rx_max_iout[CHG_MODE_DIV4] = MCA_WLS_CHG_DEFAULT_DIV4_RX_MAX_IOUT;
	} else
		memcpy(info->rx_max_iout, idata, sizeof(idata));

#line 4052
	mca_log_debug("rx_max_iout %d %d\n", info->rx_max_iout[CHG_MODE_DIV2],
		info->rx_max_iout[CHG_MODE_DIV2]);

	ret = strategy_wireless_parse_adapter_info(info);
	if (ret)
#line 4057
		mca_log_err("parst dts adapter info failed\n");

	ret = strategy_wireless_parse_adapter_cmd_type_info(info);
	if (ret)
#line 4061
		mca_log_err("parst dts adapter cmd type failed\n");

	return ret;
}

#ifdef CONFIG_SYSFS
static ssize_t strategy_wireless_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t strategy_wireless_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);

static int strategy_wireless_debug_process_data(const char *buf, struct strategy_wireless_dev *info)
{
	char *pchar = NULL;
	int args[50] = {0};
	u8 count = 0;
	char *buf_tmp;
	char *str;
	int ret = 0;

	/* OS4 stock: heap copy of exactly strlen(buf) bytes, NOT NUL-terminated (stock bug, reproduced: strsep may read past the end) */
	buf_tmp = kmalloc(strlen(buf), GFP_KERNEL);
	if (!buf_tmp) {
#line 4083
		mca_log_err("malloc failed\n");
		return -ENOMEM;
	}
	str = buf_tmp;
#line 4087
	mca_log_info("buf length is %d\n", strlen(buf));
	memcpy(buf_tmp, buf, strlen(buf));
	pchar = strsep(&str, " ");
	while (pchar != NULL) {
		if (kstrtoint(pchar, 10, &args[count]))
			return -1;
#line 4094
		mca_log_info("args[%d]: %d\n", count, args[count]);
		pchar = strsep(&str, " ");
		++count;
	}

	switch (args[0]) {
	case DEBUG_SET_FCC:
#line 4101
		mca_log_info("wls debug set fcc\n");
		info->debug_qc_ichg = args[1];
		if (args[1] == 100000)
			mca_vote_override(info->charge_limit_voter, "wls_debug", false, 0);
		else
			mca_vote_override(info->charge_limit_voter, "wls_debug", true, args[1]);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
		MCA_EVENT_WIRELESS_WLS_DEBUG, info->debug_qc_ichg);
		break;
	case DEBUG_SET_ICL:
#line 4111
		mca_log_info("wls debug set icl\n");
		if (args[1] == 100000)
			mca_vote_override(info->input_limit_voter, "wls_debug", false, 0);
		else
			mca_vote_override(info->input_limit_voter, "wls_debug", true, args[1]);
		break;
	case DEBUG_SET_ONE_EPP_FOD:
#line 4118
		mca_log_info("wls debug set one epp fod\n");
		if (count == 4)
			platform_class_wireless_set_debug_fod(WIRELESS_ROLE_MASTER, args, count);
		break;
	case DEBUG_SET_ALL_EPP_FOD:
#line 4123
		mca_log_info("wls debug set all epp fod\n");
		if (count == 25)
			platform_class_wireless_set_debug_fod(WIRELESS_ROLE_MASTER, args, count);
		break;
	case DEBUG_SET_ALL_FOD:
#line 4128
		mca_log_info("wls debug set all fod\n");
		if (count % 2 == 0 && count >= 12)
			platform_class_wireless_set_debug_fod(WIRELESS_ROLE_MASTER, args, count);
		break;
	case DEBUG_SET_VOUT:
#line 4133
		mca_log_info("wls debug set vout\n");
		if (args[1] > 0 && args[1] <= 18000) {
			ret = platform_class_wireless_set_vout(WIRELESS_ROLE_MASTER, args[1]);
			if (ret < 0)
#line 4137
				mca_log_info("wls debug set adapter voltage failed!!!\n");
		} else
#line 4139
			mca_log_info("wls debug set error vout\n");
		break;
	default:
		break;
	}
	kfree(buf_tmp);
	return 0;
}

static struct mca_sysfs_attr_info strategy_wireless_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_DEBUG, wls_debug),
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_CAR_ADAPTER, wls_car_adapter),
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_FC_FLAG, wls_fc_flag),
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_LOW_INDUCTANCE_OFFSET, low_inductance_offset),
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_SET_RX_SLEEP, set_rx_sleep),
	mca_sysfs_attr_rw(strategy_wireless_sysfs, 0664, MCA_BASIC_CHG_WLS_AUDIO_PHONE_STS, audio_phone_sts),
};

#define MCA_BASIC_CHG_ATTRS_SIZE ARRAY_SIZE(strategy_wireless_sysfs_field_tbl)

static struct attribute *strategy_wireless_sysfs_attrs[MCA_BASIC_CHG_ATTRS_SIZE + 1];

static const struct attribute_group strategy_wireless_sysfs_attr_group = {
	.attrs = strategy_wireless_sysfs_attrs,
};

static ssize_t strategy_wireless_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct strategy_wireless_dev *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int len = 0;
	int rx_vout = 0, rx_vrect = 0, rx_iout = 0;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_wireless_sysfs_field_tbl, MCA_BASIC_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_BASIC_CHG_WLS_DEBUG:
		(void)platform_class_wireless_get_vout(WIRELESS_ROLE_MASTER, &rx_vout);
		(void)platform_class_wireless_get_vrect(WIRELESS_ROLE_MASTER, &rx_vrect);
		(void)platform_class_wireless_get_iout(WIRELESS_ROLE_MASTER, &rx_iout);
		len = snprintf(buf, PAGE_SIZE, "vout=%d, vrect=%d, iout=%d\n",
			rx_vout, rx_vrect, rx_iout);
		break;
	case MCA_BASIC_CHG_WLS_CAR_ADAPTER:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->wls_node.wls_car_adapter);
		break;
	case MCA_BASIC_CHG_WLS_FC_FLAG:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->wls_node.wls_fc_flag);
		break;
	case MCA_BASIC_CHG_WLS_LOW_INDUCTANCE_OFFSET:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->wls_node.low_inductance_offset);
		break;
	case MCA_BASIC_CHG_WLS_AUDIO_PHONE_STS:
		len = snprintf(buf, PAGE_SIZE, "%d\n", info->audio_phone_sts);
		break;
	default:
		len = 0;
		break;
	}

	return len;
}

static ssize_t strategy_wireless_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct strategy_wireless_dev *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_wireless_sysfs_field_tbl, MCA_BASIC_CHG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_BASIC_CHG_WLS_DEBUG:
		strategy_wireless_debug_process_data(buf, info);
		break;
	case MCA_BASIC_CHG_WLS_SET_RX_SLEEP:
		if (sscanf(buf, "%d\n", &info->wls_node.set_rx_sleep) != 1)
			return -1;
#line 4230
		mca_log_info("set rx sleep by user:%d", info->wls_node.set_rx_sleep);
		platform_class_wireless_set_enable_mode(WIRELESS_ROLE_MASTER, (!info->wls_node.set_rx_sleep));
		break;
	case MCA_BASIC_CHG_WLS_AUDIO_PHONE_STS:
		(void)sscanf(buf, "%d", &info->audio_phone_sts);
#line 4235
		mca_log_info("set audio phone status:%d", info->audio_phone_sts);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			MCA_EVENT_WIRELESS_AUDIO_PHONE_STS, info->audio_phone_sts);
		if (info->support_hall && info->proc_data.magnetic_case_flag && info->proc_data.fc_done)
			strategy_wireless_phone_sts_change(info);
		break;
	default:
		break;
	}

	return count;
}

static int strategy_wireless_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(strategy_wireless_sysfs_attrs, strategy_wireless_sysfs_field_tbl,
		MCA_BASIC_CHG_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "wls_basic_charge",
		dev, &strategy_wireless_sysfs_attr_group);
}

static void strategy_wireless_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "wls_basic_charge",
		dev, &strategy_wireless_sysfs_attr_group);
}

#else

static inline int strategy_wireless_create_group(struct device *dev)
{
}

static void strategy_wireless_remove_group(struct device *dev)
{
}

#endif /* CONFIG_SYSFS */

static void strategy_wireless_init_work(struct strategy_wireless_dev *info)
{
	INIT_DELAYED_WORK(&info->trans_data_work, strategy_wireless_trans_data_work);
	INIT_DELAYED_WORK(&info->process_irq_work, strategy_wireless_process_irq_work);
	INIT_DELAYED_WORK(&info->monitor_work, strategy_wireless_monitor_work);
	INIT_DELAYED_WORK(&info->find_voter_work, strategy_wireless_find_voter_work);
	INIT_DELAYED_WORK(&info->report_soc_decimal_work, strategy_wireless_update_soc_decimal_work);
	INIT_DELAYED_WORK(&info->renegociation_work, strategy_wireless_renegociation_work);
	INIT_DELAYED_WORK(&info->wireless_loop_work, strategy_wireless_loop_work);
	INIT_DELAYED_WORK(&info->rx_fastcharge_work, strategy_wireless_rx_fastcharge_work);
	INIT_DELAYED_WORK(&info->wls_drawload_work, strategy_wireless_drawload_work);
	INIT_DELAYED_WORK(&info->rx_alarm_work, strategy_wireless_rx_alarm_work);
	INIT_DELAYED_WORK(&info->max_power_control_work, strategy_wireless_max_power_control_work);
	INIT_DELAYED_WORK(&info->wls_fw_state_work, strategy_wireless_fw_state_work);
	INIT_DELAYED_WORK(&info->set_vdd_flag_work, strategy_wireless_set_vdd_flag_work);
	INIT_DELAYED_WORK(&info->soc_limit_stepper_work, strategy_wireless_soc_limit_stepper_work);
	INIT_DELAYED_WORK(&info->update_wireless_thermal_work, strategy_wireless_update_wireless_thermal_work);
	INIT_DELAYED_WORK(&info->mutex_unlock_work, strategy_wireless_mutex_unlock_work);
	INIT_DELAYED_WORK(&info->sw_cv_work, strategy_wireless_buckchg_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->change_cp_mode_work, strategy_wireless_change_cp_mode_workfunc);
	INIT_DELAYED_WORK(&info->base_flip_sw_cv_work, strategy_wireless_buckchg_base_flip_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->get_type_advance_work, strategy_wireless_get_adapter_work);
}

static int strategy_wireless_init_voter(struct strategy_wireless_dev *info)
{
	info->input_limit_voter = mca_create_votable("wireless_buck_input", MCA_VOTE_MIN,
		strategy_wireless_set_input_curr_limit, 1100, info);
	if (IS_ERR(info->input_limit_voter))
		return -1;
	info->bpp_in_voter = mca_create_votable("wireless_bpp_in", MCA_VOTE_MIN,
		strategy_wireless_set_bpp_input, 1300, info);
	if (IS_ERR(info->bpp_in_voter))
		return -1;
	info->bppqc2_in_voter = mca_create_votable("wireless_bppqc2_in", MCA_VOTE_MIN,
		strategy_wireless_set_bppqc2_input, 1300, info);
	if (IS_ERR(info->bppqc2_in_voter))
		return -1;
	info->bppqc3_in_voter = mca_create_votable("wireless_bppqc3_in", MCA_VOTE_MIN,
		strategy_wireless_set_bppqc3_input, 1300, info);
	if (IS_ERR(info->bppqc3_in_voter))
		return -1;
	info->epp_in_voter = mca_create_votable("wireless_epp_in", MCA_VOTE_MIN,
		strategy_wireless_set_epp_input, 1300, info);
	if (IS_ERR(info->epp_in_voter))
		return -1;
	info->auth_20w_voter = mca_create_votable("wireless_auth_20w", MCA_VOTE_MIN,
		strategy_wireless_set_auth_20w_ichg, 15600, info);
	if (IS_ERR(info->auth_20w_voter))
		return -1;
	info->auth_30w_voter = mca_create_votable("wireless_auth_30w", MCA_VOTE_MIN,
		strategy_wireless_set_auth_30w_ichg, 15600, info);
	if (IS_ERR(info->auth_30w_voter))
		return -1;
	info->auth_50w_voter = mca_create_votable("wireless_auth_50w", MCA_VOTE_MIN,
		strategy_wireless_set_auth_50w_ichg, 15600, info);
	if (IS_ERR(info->auth_50w_voter))
		return -1;
	info->auth_80w_voter = mca_create_votable("wireless_auth_80w", MCA_VOTE_MIN,
		strategy_wireless_set_auth_80w_ichg, 15600, info);
	if (IS_ERR(info->auth_80w_voter))
		return -1;
	info->auth_voice_box_voter = mca_create_votable("wireless_auth_voice_box", MCA_VOTE_MIN,
		strategy_wireless_set_auth_voice_box_ichg, 15600, info);
	if (IS_ERR(info->auth_voice_box_voter))
		return -1;
	info->auth_magnet_30w_voter = mca_create_votable("wireless_auth_magnet_30w", MCA_VOTE_MIN,
		strategy_wireless_set_auth_magnet_30w_ichg, 15600, info);
	if (IS_ERR(info->auth_magnet_30w_voter))
		return -1;
	info->sw_qc_ichg_voter = mca_create_votable("wireless_sw_qc_ich", MCA_VOTE_MIN,
		strategy_wireless_sw_set_qc_ichg, 15600, info);
	if (IS_ERR(info->sw_qc_ichg_voter))
		return -1;
	info->sw_thermal_ichg_voter = mca_create_votable("wireless_sw_thermal_ich", MCA_VOTE_MIN,
		strategy_wireless_sw_set_thermal_ichg, 15600, info);
	if (IS_ERR(info->sw_thermal_ichg_voter))
		return -1;
	info->wls_input_suspend_voter = mca_create_votable("wireless_input_suspend", MCA_VOTE_OR,
		strategy_wireless_input_suspend, MCA_WLS_CHARGE_INPUT_SUSPEND, info);
	if (IS_ERR(info->wls_input_suspend_voter))
		return -1;
	info->force_vout_6v_voter = mca_create_votable("wireless_force_vout_6v", MCA_VOTE_OR,
		strategy_wireless_force_set_vout_6v, 0, info);
	return 0;
}

static int strategy_wireless_soc_limit_sts_callback(void *data, int effective_result)
{
	struct strategy_wireless_dev *info = (struct strategy_wireless_dev *)data;

	if (!data)
		return -1;

	info->soc_limit = effective_result;

#line 4370
	mca_log_info("effective_result: %d\n", effective_result);

	if (effective_result && info->online)
		strategy_wireless_process_soc_limit_change(effective_result, info);
	else {
		cancel_delayed_work_sync(&info->soc_limit_stepper_work);
		strategy_wireless_process_soc_limit_change(0, info);
	}

	return 0;
}

static int strategy_wireless_quiet_sts_callback(void *data, int effective_result)
{
	struct strategy_wireless_dev *info = (struct strategy_wireless_dev *)data;
	int ret = 0;

	if (!data)
		return -1;

	info->quiet_sts = effective_result;

#line 4392
	mca_log_info("effective_result: %d\n", effective_result);

	if (!info->online)
#line 4395
		mca_log_info("wireless is not online, keep sts wait for it\n");
	else
		strategy_wireless_add_trans_task_to_queue(info, TRANS_DATA_FLAG_FAN_SPEED, 0);

	return ret;
}

static int strategy_wireless_smartchg_delta_fv_callback(void *data, int effective_result)
{
	struct strategy_wireless_dev *info = (struct strategy_wireless_dev *)data;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!data)
		return -1;

#line 4410
	mca_log_err("effective_result: %d\n", effective_result);
	/* OS4: platform_version 3 / country_version 1 limit delta_fv to 20 */
	if (hwid && hwid->platform_version == 3 && hwid->country_version == 1)
		effective_result = min(effective_result, 20);
	info->smartchg_data.delta_fv = effective_result;
#line 4416
	mca_log_err("effective_result: %d\n", effective_result);
	return 0;
}

static struct mca_smartchg_if_ops g_basic_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_WL_BUCK,
	.data = NULL,
	.set_soc_limit_sts = strategy_wireless_soc_limit_sts_callback,
	.set_wls_quiet_sts = strategy_wireless_quiet_sts_callback,
	.set_delta_fv = strategy_wireless_smartchg_delta_fv_callback,
};

static int basic_wireless_class_probe(struct platform_device *pdev)
{
	int ret = 0;
	static int probe_cnt;
	struct strategy_wireless_dev *info;
	int wls_online = 0;

#line 4435
	mca_log_info("probe start probe_cnt: %d\n", ++probe_cnt);

	if (strategy_class_fg_ops_is_init_ok() <= 0) {
#line 4438
		mca_log_info("fg is not ready, wait for it\n");
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->batt_psy = power_supply_get_by_name("battery");
	if (!info->batt_psy)
#line 4448
		mca_log_err("batt_psy is not ready, wait for it\n");

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	device_init_wakeup(info->dev, true);
	ret = strategy_wireless_parse_dt(info);
	if (ret) {
#line 4455
		mca_log_err("parst dts failed\n");
		return ret;
	}

	INIT_LIST_HEAD(&info->header);
	spin_lock_init(&info->list_lock);
	init_waitqueue_head(&info->wait_que);
	INIT_LIST_HEAD(&info->header_irq);
	spin_lock_init(&info->list_lock_irq);	/* G52a: stock initialises the irq-list lock before the waitqueue */
	init_waitqueue_head(&info->wait_que_irq);
	strategy_wireless_init_work(info);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		strategy_wireless_process_event, strategy_wireless_get_status, NULL, info);

	ret = strategy_wireless_init_voter(info);
	if (ret) {
#line 4472
		mca_log_err("init voter err\n");
		return -1;
	}
	schedule_delayed_work(&info->find_voter_work, msecs_to_jiffies(0));

	strategy_wireless_create_group(info->dev);

	g_basic_wls_info = info;
	g_strategy_wireless_if_ops.data = info;
	(void)mca_charge_if_ops_register(&g_strategy_wireless_if_ops);
	g_basic_smartchg_if_ops.data = info;
	(void)mca_smartchg_if_ops_register(&g_basic_smartchg_if_ops);

	platform_class_wireless_is_present(WIRELESS_ROLE_MASTER, &wls_online);
	if (wls_online) {
#line 4487
		mca_log_err("avoid missing wls_online event in probe\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_WIRELESS_CONNECT, NULL);
	}

	/* stock probe +0x137c/+0x1380 (G13h: init_ok was never set, INIT_OK get_status would always report 0) */
	info->vbat_ov_count = 0;
	info->init_ok = 1;
#line 4492
	mca_log_err("probe End\n");

	return ret;
}

static int basic_wireless_class_remove(struct platform_device *pdev)
{
	strategy_wireless_remove_group(&pdev->dev);
	return 0;
}

static void basic_wireless_class_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,basic_wireless"},
	{},
};

static struct platform_driver basic_wireless_class_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "basic_wireless_class",
		.of_match_table = match_table,
	},
	.probe = basic_wireless_class_probe,
	.remove = basic_wireless_class_remove,
	.shutdown = basic_wireless_class_shutdown,
};

static int __init basic_wireless_class_init(void)
{
	return platform_driver_register(&basic_wireless_class_driver);
}
module_init(basic_wireless_class_init);

static void __exit basic_wireless_class_exit(void)
{
	platform_driver_unregister(&basic_wireless_class_driver);
}
module_exit(basic_wireless_class_exit);

MODULE_DESCRIPTION("Basic Wireless Class");
MODULE_AUTHOR("wuliyang@xiaomi.com");
MODULE_LICENSE("GPL v2");
