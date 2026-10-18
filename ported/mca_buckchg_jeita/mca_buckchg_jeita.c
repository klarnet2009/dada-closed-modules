// SPDX-License-Identifier: GPL-2.0
/*
 *mca_buckchg_jieta.c
 *
 * mca buck charger jeita control driver
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
 *
 */
/*
 * OS4 (HyperOS 4.0) version, reconstructed from the stock mca_buckchg_jeita.ko of HyperOS 4.0 (the XRING template is older):
 * community-only charging source, never flash it, never put it into a ROM build.
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
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/smartchg/smart_chg_class.h>
#include "inc/mca_buckchg_jeita.h"
#include <mca/platform/platform_loadsw_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_hwid.h>
#include "hwid.h"
#include <mca/protocol/protocol_class.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_buckchg_jeita"
#endif

#define MCA_DTPT_MOLECULE_SCALE 8
#define MCA_DTPT_DENOM_SCALE 10

static int mca_buckchg_jeita_get_curr(struct mca_buckchg_jeita_data *jeita_data, int vbat, int *vbat_index)
{
	int i;
	int min_cur = jeita_data->max_current;

	if (!jeita_data->volt_para.size)
		return min_cur;

	for (i = 0;  i < jeita_data->volt_para.size; i++) {
		if (vbat <= jeita_data->volt_para.volt_data[i].voltage) {
			*vbat_index = i;
			return jeita_data->volt_para.volt_data[i].max_current;
		}
		min_cur = min(min_cur, jeita_data->volt_para.volt_data[i].max_current);
	}

	/* can not find para, use min current */
	return min_cur;
}

/* temperature in 0.1 degC -> degC; the stock rounds negative values one step further down */
static inline int mca_buckchg_jeita_temp_to_c(int temp)
{
	if (temp < 0)
		return temp / 10 - 1;
	return temp / 10;
}

#define ABNORMAL_BATT_FV_MAX 4150
#define JEITA_HOT_RECHARGE_VBAT_HYS 150

static void mca_buckchg_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int vterm = 0, iterm = 0, chg_curr = 0;
	int hys_affect = 0;
	int data_change = 0;
	int vbat = 0;
	unsigned int adapter_type = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	static int last_type;
	static int is_in_dynamic_iterm_temp_range;
	int vbat_index = -1;
	int weak_iterm = 0, weak_applied = 0;

	if (!info->jeita_para.jeita_data || !info->voter_ok) {
#line 367
		mca_log_err("jeita data not ready\n");
		return;
	}

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 373
		mca_log_err("get battery temp failed\n");
		return;
	}

	temp = mca_buckchg_jeita_temp_to_c(temp);
	fastcharge_mode = strategy_class_fg_get_fastcharge();
	ret = strategy_class_fg_ops_get_voltage(&vbat);
	if (ret) {
#line 382
		mca_log_err("get battery volt failed\n");
		return;
	}

	if (!fastcharge_mode) {
		for (i = 0; i < info->jeita_para.size; i++) {
			cur_jeita_data = info->jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->jeita_para.size) {
#line 393
			mca_log_err("can not find jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->jeita_para.fcc_size; i++) {
			cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->jeita_para.fcc_size) {
#line 403
			mca_log_err("can not find jeita fcc para\n");
			return;
		}
	}

	/* select cur jeita_data */
	if (info->proc_data.cur_jeita_index == -1 || info->baacfg_update) {
		jeita_data = cur_jeita_data;
		last_chg_curr = mca_buckchg_jeita_get_curr(cur_jeita_data, vbat, &vbat_index);
	} else {
		if (info->proc_data.cur_jeita_index == i || fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (fastcharge_mode)
				jeita_data = info->jeita_para.jeita_ffc_data + info->proc_data.cur_jeita_index;
			else
				jeita_data = info->jeita_para.jeita_data + info->proc_data.cur_jeita_index;

#line 421
			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				jeita_data->temp_low - jeita_data->low_temp_hys,
				jeita_data->temp_high + jeita_data->high_temp_hys, temp);
			if (i > info->proc_data.cur_jeita_index) {
				if (info->proc_data.cur_jeita_index == 0 && mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp < jeita_data->temp_high + jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp >= jeita_data->temp_low - jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}

	/* select whether vote */
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_JEITA,
		STRATEGY_STATUS_TYPE_JEITA_IS_DYNAMIC_ITERM_TEMP_RANGE, &is_in_dynamic_iterm_temp_range);
	info->proc_data.max_chg_curr = mca_buckchg_jeita_get_curr(jeita_data, vbat, &vbat_index);
	if (info->baacfg_update) {
		data_change = 1;
		info->baacfg_update = false;
#line 448
		mca_log_info("BAA config data update\n");
	} else if (info->proc_data.cur_jeita_index != i && !hys_affect) {	//jeita_index change
		data_change = 1;
#line 451
		mca_log_info("jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->proc_data.max_chg_curr;
	} else if (info->support_dynamic_iterm && info->last_temp != temp && is_in_dynamic_iterm_temp_range) {
		data_change = 1;
#line 456
		mca_log_info("temp %d ->%d dynamic iterm change\n", info->last_temp, temp);
	} else {	//jeita_index not change, check vbat_thre
		if (info->proc_data.max_chg_curr > last_chg_curr) {
			if (vbat_index != -1 &&
				vbat < jeita_data->volt_para.volt_data[vbat_index].voltage -
				((unsigned int)(i - 1) < 3 ? info->vbat_low_cold_hyst : info->vbat_low_hyst)) {
				data_change = 1;
#line 466
				mca_log_info("vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d, vbat:%d\n",
					info->proc_data.max_chg_curr, last_chg_curr, vbat_index, vbat);
				last_chg_curr = info->proc_data.max_chg_curr;
			}
		} else if (info->proc_data.max_chg_curr < last_chg_curr) {
			if (vbat_index != -1 &&
				vbat > jeita_data->volt_para.volt_data[vbat_index - 1].voltage) {
				data_change = 1;
#line 474
				mca_log_info("vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
					info->proc_data.max_chg_curr, last_chg_curr, vbat_index);
				last_chg_curr = info->proc_data.max_chg_curr;
			}
		}
	}
	info->last_temp = temp;

	vterm = jeita_data->vterm;
	iterm = jeita_data->iterm;
	if (info->support_weak_chg_vterm) {
		protocol_class_get_adapter_type(ADAPTER_PROTOCOL_BC12, &adapter_type);
		if (adapter_type != last_type) {
			data_change = 1;
			last_type = adapter_type;
		}
		if (adapter_type - 1 <= 3 && jeita_data->iterm > info->weak_chg_iterm) {
			weak_iterm = info->weak_chg_iterm;
			vterm = jeita_data->vterm - info->weak_chg_delta_vterm;
			weak_applied = 1;
		}
	}

	info->vfloat = vterm;
#line 507
	mca_log_info("current vfloat from batt spec is %d", vterm);
	if (vterm > ABNORMAL_BATT_FV_MAX) {
		vterm -= info->smartchg_data.delta_fv;
		mca_vote(info->en_voter, "jeita-hot", true, 1);
		if (vterm != info->vterm) {
			mca_vote(info->vterm_voter, "jeita", true, vterm);
			info->vterm = vterm;
		}
	} else {
		if (vbat >= vterm - info->jeita_hot_termination_hyst)
			mca_vote(info->en_voter, "jeita-hot", true, 0);
		else if (vbat < vterm - JEITA_HOT_RECHARGE_VBAT_HYS)
			mca_vote(info->en_voter, "jeita-hot", true, 1);
	}

	if (info->dtpt_status)
		chg_curr = info->proc_data.max_chg_curr * MCA_DTPT_MOLECULE_SCALE / MCA_DTPT_DENOM_SCALE;
	else
		chg_curr = info->proc_data.max_chg_curr;

#line 532
	mca_log_info("cur index %d/%d last_chg_curr %d chg_curr %d/%d fastcharge %d jeita_vterm %d jeita_iterm %d vterm %d delta_fv %d hys_affect %d\n",
		i, info->proc_data.cur_jeita_index, last_chg_curr, info->proc_data.max_chg_curr, chg_curr, fastcharge_mode,
		jeita_data->vterm, jeita_data->iterm, vterm, info->smartchg_data.delta_fv, hys_affect);

	last_fastcharge_mode = fastcharge_mode;
	if (chg_curr)
		mca_vote(info->en_voter, "jeita", true, 1);
	else
		mca_vote(info->en_voter, "jeita", true, 0);

	if (data_change) {
		if (!hys_affect)
			info->proc_data.cur_jeita_index = i;
		mca_vote(info->fcc_voter, "jeita", true, chg_curr);
		(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_JEITA,
			STRATEGY_STATUS_TYPE_JEITA_DYNAMIC_ITERM, &iterm);
		if (weak_applied && weak_iterm < iterm)
			iterm = weak_iterm;
#line 549
		mca_log_info("data_change voted iterm: %d\n", iterm);
		mca_vote(info->vterm_voter, "jeita", true, vterm);
		if (info->base_flip_same)
			mca_rerun_election(info->vterm_voter);
		if (!info->support_base_flip && !info->base_flip_same)
			mca_vote(info->iterm_voter, "jeita", true, iterm);
	}

	if (!fastcharge_mode && info->base_flip_same) {
		info->cold_zone = info->proc_data.cur_jeita_index == 1;
#line 558
		mca_log_info("cur index %d/%d last_chg_curr %d cold_zone %d\n",
			i, info->proc_data.cur_jeita_index, last_chg_curr, info->cold_zone);
	}
}

static void mca_buckchg_base_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int now_curr = 0, chg_curr = 0, effective_curr;
	int hys_affect = 0, data_change = 0;
	int vbat = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	static bool runswocp;
	static int base_over_curr_count;
	int vbat_index = -1;

	if (!info->base_jeita_para.jeita_data || !info->voter_ok) {
#line 94
		mca_log_err("jeita data not ready\n");
		return;
	}
	effective_curr = mca_get_effective_result(info->fcc_voter);

	ret = platform_fg_ops_get_temp(FG_IC_MASTER, &temp);
	ret |= platform_fg_ops_get_curr(FG_IC_MASTER, &now_curr);
	if (ret) {
#line 102
		mca_log_err("get battery temp or currfailed\n");
		return;
	}

	temp = mca_buckchg_jeita_temp_to_c(temp);
	fastcharge_mode = strategy_class_fg_get_fastcharge();
#line 108
	mca_log_err("fastcharge_mode is %d\n", fastcharge_mode);
	platform_fg_ops_get_volt(FG_IC_MASTER, &vbat);
#line 111
	mca_log_info("vbat is %d\n", vbat);

	if (!fastcharge_mode) {
		for (i = 0; i < info->base_jeita_para.size; i++) {
			cur_jeita_data = info->base_jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->base_jeita_para.size) {
#line 120
			mca_log_err("can not find flip jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->base_jeita_para.fcc_size; i++) {
			cur_jeita_data = info->base_jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->base_jeita_para.fcc_size) {
#line 130
			mca_log_err("can not find flip jeita fcc para\n");
			return;
		}
	}

	if (info->base_proc_data.cur_jeita_index == -1) {
		jeita_data = cur_jeita_data;
		last_chg_curr = mca_buckchg_jeita_get_curr(jeita_data, vbat, &vbat_index);
	} else {
		if (info->base_proc_data.cur_jeita_index == i || fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (!fastcharge_mode)
				jeita_data = info->base_jeita_para.jeita_data + info->base_proc_data.cur_jeita_index;
			else
				jeita_data = info->base_jeita_para.jeita_ffc_data + info->base_proc_data.cur_jeita_index;

#line 147
			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				jeita_data->temp_low - jeita_data->low_temp_hys,
				jeita_data->temp_high + jeita_data->high_temp_hys, temp);
			if (i > info->base_proc_data.cur_jeita_index) {
				if (info->base_proc_data.cur_jeita_index == 0 && mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp < jeita_data->temp_high + jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp >= jeita_data->temp_low - jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}

	info->base_proc_data.max_chg_curr = mca_buckchg_jeita_get_curr(jeita_data, vbat, &vbat_index);
	if (info->base_proc_data.cur_jeita_index != i && !hys_affect) {
		data_change = 1;
#line 170
		mca_log_info("jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->base_proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->base_proc_data.max_chg_curr;
	} else {
		if (info->base_proc_data.max_chg_curr > last_chg_curr) {
			if (vbat_index != -1 &&
				vbat < jeita_data->volt_para.volt_data[vbat_index].voltage - info->base_vbat_low_hyst) {
				data_change = 1;
#line 178
				mca_log_info("vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d, vbat:%d\n",
					info->base_proc_data.max_chg_curr, last_chg_curr, vbat_index, vbat);
				last_chg_curr = info->base_proc_data.max_chg_curr;
			}
		} else if (info->base_proc_data.max_chg_curr < last_chg_curr) {
			if (vbat_index != -1 &&
				vbat > jeita_data->volt_para.volt_data[vbat_index - 1].voltage) {
				data_change = 1;
#line 186
				mca_log_info("vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
					info->base_proc_data.max_chg_curr, last_chg_curr, vbat_index);
				last_chg_curr = info->base_proc_data.max_chg_curr;
			}
		}
	}

	chg_curr = info->base_proc_data.max_chg_curr;
#line 194
	mca_log_info("cur index %d/%d max_chg_curr %d chg_curr %d hys_affect %d, now_curr %d, effective_curr %d\n",
		i, info->base_proc_data.cur_jeita_index, info->base_proc_data.max_chg_curr, chg_curr, hys_affect, now_curr, effective_curr);

	last_fastcharge_mode = fastcharge_mode;
	if (!hys_affect && data_change)
		info->base_proc_data.cur_jeita_index = i;

	if (effective_curr != 0 && now_curr / 1000 < -chg_curr)
		base_over_curr_count++;
	else
		base_over_curr_count = 0;
	if (base_over_curr_count >= 4 && effective_curr != 0) {
		mca_vote(info->fcc_voter, "swocp", true, effective_curr - 100);
		base_over_curr_count = 0;
		runswocp = true;
#line 212
		mca_log_err("reduce fcc %d by swocp\n", effective_curr - 100);
	}
	if (now_curr / 1000 > -chg_curr + 500 && runswocp) {
		runswocp = false;
		mca_vote(info->fcc_voter, "swocp", false, 0);
#line 218
		mca_log_err("disable swocp\n");
	}
}

static void mca_buckchg_flip_jeita_update(struct mca_buckchg_jeita_dev *info)
{
	int temp = 0;
	int i, ret;
	struct mca_buckchg_jeita_data *jeita_data, *cur_jeita_data;
	int fastcharge_mode = 0;
	int chg_curr = 0;
	int hys_affect = 0, data_change = 0;
	int vbat = 0;
	static int last_fastcharge_mode;
	static int last_chg_curr;
	int vbat_index = -1;

	if (!info->flip_jeita_para.jeita_data || !info->voter_ok) {
#line 237
		mca_log_err("jeita data not ready\n");
		return;
	}

	ret = platform_fg_ops_get_temp(FG_IC_SLAVE, &temp);
	if (ret) {
#line 243
		mca_log_err("get battery temp failed\n");
		return;
	}

	temp = mca_buckchg_jeita_temp_to_c(temp);
	fastcharge_mode = strategy_class_fg_get_fastcharge();
#line 249
	mca_log_err("fastcharge_mode is %d\n", fastcharge_mode);
	platform_fg_ops_get_volt(FG_IC_SLAVE, &vbat);
#line 252
	mca_log_info("vbat is %d\n", vbat);

	if (!fastcharge_mode) {
		for (i = 0; i < info->flip_jeita_para.size; i++) {
			cur_jeita_data = info->flip_jeita_para.jeita_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->flip_jeita_para.size) {
#line 261
			mca_log_err("can not find flip jeita para\n");
			return;
		}
	} else {
		for (i = 0; i < info->flip_jeita_para.fcc_size; i++) {
			cur_jeita_data = info->flip_jeita_para.jeita_ffc_data + i;
			if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
				break;
		}
		if (i == info->flip_jeita_para.fcc_size) {
#line 271
			mca_log_err("can not find flip jeita fcc para\n");
			return;
		}
	}

	if (info->flip_proc_data.cur_jeita_index == -1) {
		jeita_data = cur_jeita_data;
		last_chg_curr = mca_buckchg_jeita_get_curr(jeita_data, vbat, &vbat_index);
	} else {
		if (info->flip_proc_data.cur_jeita_index == i || fastcharge_mode != last_fastcharge_mode)
			jeita_data = cur_jeita_data;
		else {
			if (!fastcharge_mode)
				jeita_data = info->flip_jeita_para.jeita_data + info->flip_proc_data.cur_jeita_index;
			else
				jeita_data = info->flip_jeita_para.jeita_ffc_data + info->flip_proc_data.cur_jeita_index;

#line 288
			mca_log_info("temp_low: %d, temp_high: %d, temp: %d\n",
				jeita_data->temp_low - jeita_data->low_temp_hys,
				jeita_data->temp_high + jeita_data->high_temp_hys, temp);
			if (i > info->flip_proc_data.cur_jeita_index) {
				if (info->flip_proc_data.cur_jeita_index == 0 && mca_smartchg_is_extreme_cold_enabled())
					jeita_data = cur_jeita_data;
				else if (temp < jeita_data->temp_high + jeita_data->high_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			} else {
				if (temp >= jeita_data->temp_low - jeita_data->low_temp_hys)
					hys_affect = 1;
				else
					jeita_data = cur_jeita_data;
			}
		}
	}

	info->flip_proc_data.max_chg_curr = mca_buckchg_jeita_get_curr(jeita_data, vbat, &vbat_index);
	if (info->flip_proc_data.cur_jeita_index != i && !hys_affect) {
		data_change = 1;
#line 311
		mca_log_info("jeita_index: cur_jeita_index:%d, i:%d, hys_affect:%d\n",
			info->flip_proc_data.cur_jeita_index, i, hys_affect);
		last_chg_curr = info->flip_proc_data.max_chg_curr;
	} else {
		if (info->flip_proc_data.max_chg_curr > last_chg_curr) {
			if (vbat_index != -1 &&
				vbat < jeita_data->volt_para.volt_data[vbat_index].voltage - info->flip_vbat_low_hyst) {
				data_change = 1;
#line 319
				mca_log_info("vbat_thre[l->s]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d, vbat:%d\n",
					info->flip_proc_data.max_chg_curr, last_chg_curr, vbat_index, vbat);
				last_chg_curr = info->flip_proc_data.max_chg_curr;
			}
		} else if (info->flip_proc_data.max_chg_curr < last_chg_curr) {
			if (vbat_index != -1 &&
				vbat > jeita_data->volt_para.volt_data[vbat_index - 1].voltage) {
				data_change = 1;
#line 327
				mca_log_info("vbat_thre[s->l]: max_chg_curr:%d, last_chg_curr:%d, vbat_index:%d\n",
					info->flip_proc_data.max_chg_curr, last_chg_curr, vbat_index);
				last_chg_curr = info->flip_proc_data.max_chg_curr;
			}
		}
	}

	chg_curr = info->flip_proc_data.max_chg_curr;
#line 336
	mca_log_info("cur index %d/%d max_chg_curr %d chg_curr %d hys_affect %d\n",
		i, info->flip_proc_data.cur_jeita_index, info->flip_proc_data.max_chg_curr, chg_curr, hys_affect);

	last_fastcharge_mode = fastcharge_mode;
	if (data_change) {
		if (!hys_affect)
			info->flip_proc_data.cur_jeita_index = i;
		mca_vote(info->flip_fcc_voter, "jeita", true, chg_curr);
	}
}

static int mca_buckchg_jeita_flip_charge_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	if (!data)
		return -1;

#line 569
	mca_log_err("set flip limit current %d\n", effective_result);

	return platform_class_loadsw_set_ibat_limit(LOADSW_ROLE_MASTER, effective_result);
}

static void mca_buckchg_jeita_init_voter(struct mca_buckchg_jeita_dev *info)
{
	info->flip_fcc_voter = mca_create_votable("flip_charge_curr", MCA_VOTE_MIN,
		mca_buckchg_jeita_flip_charge_limit, MCA_BUCKCHG_JEITA_FLIP_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->flip_fcc_voter))
		return;

	info->en_voter = mca_find_votable("chg_enable");
	if (!info->en_voter)
		return;
	info->fcc_voter = mca_find_votable("buck_charge_curr");
	if (!info->fcc_voter)
		return;
	info->vterm_voter = mca_find_votable("term_volt");
	if (!info->vterm_voter)
		return;
	info->iterm_voter = mca_find_votable("term_curr");
	if (!info->iterm_voter)
		return;

	info->voter_ok = 1;
}

static void mca_buckchg_jeita_monitor_work(struct work_struct *work)
{
	int interval = MCA_BUCKCHG_JEITA_UPDATE_NORMAL_INTERVAL;
	struct mca_buckchg_jeita_dev *info = container_of(work,
					struct mca_buckchg_jeita_dev, monitor_work.work);

	if (!info->voter_ok)
		mca_buckchg_jeita_init_voter(info);

	if (!info->voter_ok) {
		interval = MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL;
#line 608
		mca_log_info("voter not ok\n");
		goto out;
	}

	if (!info->online) {
#line 613
		mca_log_info("adapter is plugout\n");
		return;
	}
	mca_buckchg_jeita_update(info);
	if (info->support_base_flip) {
		mca_buckchg_base_jeita_update(info);
		mca_buckchg_flip_jeita_update(info);
	}
out:
	schedule_delayed_work(&info->monitor_work, msecs_to_jiffies(interval));
}

static int mca_buckchg_parse_volt_para(struct device_node *node,
	const char *name, struct mca_buckchg_jeita_volt_para *volt_para)
{
	struct mca_buckchg_jeita_volt_data *volt_data;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	if (strcmp(name, "null") == 0) {
#line 633
		mca_log_info("no need parse volt para\n");
		return 0;
	}
	array_len = mca_parse_dts_count_strings(node, name,
		JEITA_VOLT_DATA_MAX_GROUP,
		BUCKCHG_VOLTAGE_PARA_MAX);
	if (array_len < 0) {
#line 640
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	volt_para->size = array_len / BUCKCHG_VOLTAGE_PARA_MAX;
	volt_para->volt_data = kcalloc(volt_para->size, sizeof(*volt_data), GFP_KERNEL);
	if (!volt_para->volt_data) {
#line 647
		mca_log_err("volt para no mem\n");
		return -ENOMEM;
	}
	volt_data = volt_para->volt_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / BUCKCHG_VOLTAGE_PARA_MAX;
		col = i % BUCKCHG_VOLTAGE_PARA_MAX;

		switch (col) {
		case BUCKCHG_VOLTAGE_TH:
			if (kstrtoint(tmp_string, 10, &volt_data[row].voltage))
				goto error;
			break;
		case BUCKCHG_CURRENT_MAX:
			if (kstrtoint(tmp_string, 10, &volt_data[row].max_current))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < volt_para->size; i++)
#line 673
		mca_log_debug("volt_para %d %d\n", volt_data[i].voltage, volt_data[i].max_current);

	return 0;
error:
	kfree(volt_data);
	volt_data = NULL;
	return -1;
}

static int mca_buckchg_jeita_parse_para(struct device_node *node,
	const char *name, struct mca_buckchg_jeita_para *jeita_info)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	char ffc_name[50];
	struct mca_buckchg_jeita_data *jeita_data, *jeita_ffc_data;

	array_len = mca_parse_dts_count_strings(node, name,
		JEITA_DATA_MAX_GROUP,
		JEITA_TEMP_PARA_MAX);
	if (array_len < 0) {
#line 835
		mca_log_err("parse jeita failed\n");
		return -1;
	}

	jeita_info->size = array_len / JEITA_TEMP_PARA_MAX;
	jeita_info->jeita_data = kcalloc(jeita_info->size, sizeof(*jeita_info->jeita_data), GFP_KERNEL);
	if (!jeita_info->jeita_data) {
#line 842
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	jeita_data = jeita_info->jeita_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / JEITA_TEMP_PARA_MAX;
		col = i % JEITA_TEMP_PARA_MAX;
		switch (col) {
		case JEITA_TEMP_PARA_TEMP_LOW:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].temp_low))
				goto error;
			break;
		case JEITA_TEMP_PARA_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].temp_high))
				goto error;
			break;
		case JEITA_TEMP_PARA_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].low_temp_hys))
				goto error;
			break;
		case JEITA_TEMP_PARA_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].high_temp_hys))
				goto error;
			break;
		case JEITA_TEMP_PARA_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].max_current))
				goto error;
			break;
		case JEITA_TEMP_PARA_VTERM:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].vterm))
				goto error;
			break;
		case JEITA_TEMP_PARA_ITERM:
			if (kstrtoint(tmp_string, 10, &jeita_data[row].iterm))
				goto error;
			break;
		case JEITA_TEMP_PARA_VOLT_PARA_NAME:
			if (mca_buckchg_parse_volt_para(node, tmp_string,
				&jeita_data[row].volt_para))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < jeita_info->size; i++)
#line 892
		mca_log_err("jeita para %d %d %d %d %d %d %d %d\n",
			jeita_data[i].temp_low, jeita_data[i].temp_high, jeita_data[i].low_temp_hys,
			jeita_data[i].high_temp_hys, jeita_data[i].max_current, jeita_data[i].vterm,
			jeita_data[i].iterm, jeita_data[i].volt_para.size);

	snprintf(ffc_name, sizeof(ffc_name), "%s", name);
	snprintf(ffc_name + strlen(ffc_name), sizeof(ffc_name) - strlen(ffc_name), "_ffc");
#line 899
	mca_log_err("new name:%s\n", ffc_name);
	array_len = mca_parse_dts_count_strings(node, ffc_name,
		JEITA_DATA_MAX_GROUP,
		JEITA_TEMP_PARA_MAX);
	if (array_len < 0) {
#line 904
		mca_log_err("parse jeita para ffc failed\n");
		return -1;
	}

	jeita_info->fcc_size = array_len / JEITA_TEMP_PARA_MAX;

	jeita_info->jeita_ffc_data = kcalloc(jeita_info->fcc_size, sizeof(*jeita_info->jeita_ffc_data), GFP_KERNEL);
	if (!jeita_info->jeita_ffc_data) {
#line 912
		mca_log_err("temp para no mem\n");
		return -ENOMEM;
	}
	jeita_ffc_data = jeita_info->jeita_ffc_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, ffc_name, i, &tmp_string))
			return -1;

		row = i / JEITA_TEMP_PARA_MAX;
		col = i % JEITA_TEMP_PARA_MAX;
		switch (col) {
		case JEITA_TEMP_PARA_TEMP_LOW:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].temp_low))
				goto error1;
			break;
		case JEITA_TEMP_PARA_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].temp_high))
				goto error1;
			break;
		case JEITA_TEMP_PARA_LOW_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].low_temp_hys))
				goto error1;
			break;
		case JEITA_TEMP_PARA_HIGH_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].high_temp_hys))
				goto error1;
			break;
		case JEITA_TEMP_PARA_MAX_CURRENT:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].max_current))
				goto error1;
			break;
		case JEITA_TEMP_PARA_VTERM:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].vterm))
				goto error1;
			break;
		case JEITA_TEMP_PARA_ITERM:
			if (kstrtoint(tmp_string, 10, &jeita_ffc_data[row].iterm))
				goto error1;
			break;
		case JEITA_TEMP_PARA_VOLT_PARA_NAME:
			if (mca_buckchg_parse_volt_para(node, tmp_string,
				&jeita_ffc_data[row].volt_para))
				goto error1;
			break;
		default:
			break;
		}
	}
	for (i = 0; i < jeita_info->fcc_size; i++)
#line 961
		mca_log_err("jeita ffc para %d %d %d %d %d %d %d %d\n",
			jeita_ffc_data[i].temp_low, jeita_ffc_data[i].temp_high, jeita_ffc_data[i].low_temp_hys,
			jeita_ffc_data[i].high_temp_hys, jeita_ffc_data[i].max_current, jeita_ffc_data[i].vterm,
			jeita_ffc_data[i].iterm, jeita_ffc_data[i].volt_para.size);
	return 0;
error1:
	kfree(jeita_ffc_data);
	jeita_ffc_data = NULL;
error:
	kfree(jeita_data);
	jeita_data = NULL;
	return -1;

}

static int mca_buckchg_parse_dynamic_iterm_range(struct device_node *node,
	const char *name, struct mca_dynamic_iterm_para *para)
{
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, name,
		10, DYNAMIC_ITERM_RANGE_MAX);
	if (array_len < 0) {
#line 693
		mca_log_info("no dynamic iterm range: %s\n", name);
		return -1;
	}

	para->size = array_len / DYNAMIC_ITERM_RANGE_MAX;
	para->range = kcalloc(para->size, sizeof(*para->range), GFP_KERNEL);
	if (!para->range)
		return -ENOMEM;

	for (i = 0; i < para->size * DYNAMIC_ITERM_RANGE_MAX; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			goto error;

		row = i / DYNAMIC_ITERM_RANGE_MAX;
		col = i % DYNAMIC_ITERM_RANGE_MAX;
		switch (col) {
		case DYNAMIC_ITERM_TEMP_LOW:
			if (kstrtoint(tmp_string, 10, &para->range[row].temp_low))
				goto error;
			break;
		case DYNAMIC_ITERM_TEMP_HIGH:
			if (kstrtoint(tmp_string, 10, &para->range[row].temp_high))
				goto error;
			break;
		case DYNAMIC_ITERM_K:
			if (kstrtoint(tmp_string, 10, &para->range[row].k))
				goto error;
			break;
		case DYNAMIC_ITERM_B:
			if (kstrtoint(tmp_string, 10, &para->range[row].b))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < para->size; i++)
#line 732
		mca_log_info("dynamic iterm range %d: %d-%d, k=%d, b=%d\n", i,
			para->range[i].temp_low, para->range[i].temp_high, para->range[i].k, para->range[i].b);

	return 0;
error:
	kfree(para->range);
	para->range = NULL;
	return -1;
}

static int mca_buckchg_parse_dynamic_iterm_by_name(struct device_node *node,
	struct mca_dynamic_iterm_para *para)
{
	int i, array_len;
	const char *tmp_string = NULL;
	const char *range_name = NULL;
	const char *name = "dynamic_iterm_para";

	if (!para)
		return -1;
	int mode;

	array_len = mca_parse_dts_count_strings(node, name,
		DYNAMIC_ITERM_SINGLE_PARA_MAX, DYNAMIC_ITERM_SINGLE_PARA_MAX);
	if (array_len < 0) {
#line 755
		mca_log_info("no %s config\n", name);
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;
		switch (i) {
		case DYNAMIC_ITERM_SINGLE_MODE:
			mode = DYNAMIC_ITERM_MODE_BOTH;
			if (kstrtoint(tmp_string, 10, &mode))
				return -1;
			para->enable_mode = mode;
			break;
		case DYNAMIC_ITERM_SINGLE_RANGE_NAME:
			range_name = tmp_string;
			break;
		default:
			break;
		}
	}

	if (!range_name)
		return -1;

	return mca_buckchg_parse_dynamic_iterm_range(node, range_name, para);
}

static int mca_buckchg_get_dynamic_iterm(struct mca_dynamic_iterm_para *para,
	struct mca_buckchg_jeita_para *jeita_para, int temp, int fastcharge)
{
	int i;
	int iterm = -1;

	if (!para || !jeita_para)
		return -1;

	if (fastcharge) {
		if (para->enable_mode == DYNAMIC_ITERM_MODE_NORMAL)
			return -1;
	} else {
		if (para->enable_mode == DYNAMIC_ITERM_MODE_FFC)
			return -1;
	}

	if (!para->range)
		return -1;

	for (i = 0; i < para->size; i++) {
		if (temp >= para->range[i].temp_low && temp < para->range[i].temp_high) {
			iterm = para->range[i].k * temp + para->range[i].b;
			if (iterm < 0)
				iterm = 0;
#line 807
			mca_log_info("dynamic iterm: temp=%d, k=%d, b=%d, iterm=%d\n",
				temp, para->range[i].k, para->range[i].b, iterm);
			return iterm;
		}
	}

	return -1;
}

static void mca_buckchg_jeita_parse_dt_override(struct mca_buckchg_jeita_dev *info)
{
	struct device_node *node;
	u32 weak_cfg[2] = { 0 };
	int ret;

	node = of_get_child_by_name(info->dev->of_node, "buckchg_jeita_override");
	if (!node) {
#line 1085
		mca_log_info("child node 'buckchg_jeita_override' not found, using parent config\n");
		return;
	}
#line 1089
	mca_log_info("applying buckchg_jeita override from child node\n");

	mca_parse_dts_u32(node, "vbat_high_hyst", &(info->vbat_high_hyst), info->vbat_high_hyst);
	mca_parse_dts_u32(node, "vbat_low_hyst", &(info->vbat_low_hyst), info->vbat_low_hyst);
	mca_parse_dts_u32(node, "vbat_low_cold_hyst", &(info->vbat_low_cold_hyst), info->vbat_low_cold_hyst);
	mca_parse_dts_u32(node, "jeita_hot_termination_hyst", &(info->jeita_hot_termination_hyst),
		info->jeita_hot_termination_hyst);
	mca_parse_dts_u32(node, "support_weak_chg_vterm", &(info->support_weak_chg_vterm), 0);
	if (info->support_weak_chg_vterm) {
		ret = mca_parse_dts_u32_array(node, "weak_chg_vterm_cfg", weak_cfg, 2);
		if (ret < 0) {
#line 1102
			mca_log_err("parse weak_chg_vterm_cfg failed %d\n", ret);
			info->support_weak_chg_vterm = 0;
		} else {
			info->weak_chg_delta_vterm = weak_cfg[0];
			info->weak_chg_iterm = weak_cfg[1];
#line 1107
			mca_log_info("weak_chg_vterm: delta_vterm=%d, iterm=%d\n",
				info->weak_chg_delta_vterm, info->weak_chg_iterm);
		}
	}

	if (of_property_read_bool(node, "jeita_para")) {
		if (!mca_buckchg_jeita_parse_para(node, "jeita_para", &info->jeita_para))
#line 1114
			mca_log_info("overridden jeita_para\n");
	}

	if (!mca_buckchg_parse_dynamic_iterm_by_name(node, &info->dynamic_iterm)) {
		info->support_dynamic_iterm = true;
#line 1120
		mca_log_info("dynamic iterm support enabled by override\n");
	}
#line 1123
	mca_log_info("buckchg_jeita override parse completed\n");
}

static int mca_buckchg_jeita_parse_dt(struct mca_buckchg_jeita_dev *info)
{
	struct mca_buckchg_jeita_para *jeita_info = &info->jeita_para;
	struct mca_buckchg_jeita_para *base_jeita_info = &info->base_jeita_para;
	struct mca_buckchg_jeita_para *flip_jeita_info = &info->flip_jeita_para;
	struct device_node *node = info->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	const char *dev_name = NULL;
	const char *cell_name = NULL;
	u32 weak_cfg[2] = { 0 };
	int ret;

	mca_parse_dts_u32(node, "vbat_high_hyst", &(info->vbat_high_hyst), MCA_BUCKCHG_JEITA_VBAT_HIGH_HYST);
	mca_parse_dts_u32(node, "vbat_low_hyst", &(info->vbat_low_hyst), MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "vbat_low_cold_hyst", &(info->vbat_low_cold_hyst), MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "base_vbat_low_hyst", &(info->base_vbat_low_hyst), MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "flip_vbat_low_hyst", &(info->flip_vbat_low_hyst), MCA_BUCKCHG_JEITA_VBAT_LOW_HYST);
	mca_parse_dts_u32(node, "jeita_hot_termination_hyst", &(info->jeita_hot_termination_hyst), 0);
	info->has_gbl_batt_para = of_property_read_bool(node, "has-global-batt-para");
	if (hwid && info->has_gbl_batt_para && hwid->country_version != CountryCN)
		node = of_find_node_by_name(NULL, "mca_buckchg_jeita_gbl_para");
	if (!node) {
#line 998
		mca_log_err("node in null\n");
		return -1;
	}

	mca_parse_dts_u32(node, "support_weak_chg_vterm", &(info->support_weak_chg_vterm), 0);
	if (info->support_weak_chg_vterm) {
		ret = mca_parse_dts_u32_array(node, "weak_chg_vterm_cfg", weak_cfg, 2);
		if (ret < 0) {
#line 1008
			mca_log_err("parse weak_chg_vterm_cfg failed %d\n", ret);
			info->support_weak_chg_vterm = 0;
		} else {
			info->weak_chg_delta_vterm = weak_cfg[0];
			info->weak_chg_iterm = weak_cfg[1];
#line 1013
			mca_log_info("weak_chg_vterm: delta_vterm=%d, iterm=%d\n",
				info->weak_chg_delta_vterm, info->weak_chg_iterm);
		}
	}

	info->has_tmp_batt_para = of_property_read_bool(node, "has-tmp-batt-para");
	if (info->has_tmp_batt_para) {
		platform_fg_ops_get_device_name(FG_IC_MASTER, &dev_name);
		if (!dev_name) {
#line 1022
			mca_log_err("get device name fail, wait for it\n");
			ret = -EINVAL;
			goto out;
		}
#line 1025
		mca_log_err("project O9 tmp test: device name: %s\n", dev_name);
		if (!strcmp("2@BP", dev_name)) {
			node = of_find_node_by_name(NULL, "mca_buckchg_jeita_tmp_para");
			if (!node) {
#line 1030
				mca_log_err("has-tmp-batt-para node in null\n");
				ret = -1;
				goto out;
			}
		}
	}

	info->has_second_batt_para = of_property_read_bool(node, "has-second-batt-para");
	if (info->has_second_batt_para) {
		if (hwid && !mca_is_warsaw_p2_before(hwid)) {
#line 1039
			mca_log_err("not warsaw p2 before or yanan p1 before, skip second batt para\n");
			info->has_second_batt_para = 0;
		} else {
			platform_fg_ops_get_batt_cell_info(FG_IC_MASTER, &cell_name);
			if (!cell_name) {
#line 1044
				mca_log_err("get main cell name fail\n");
				ret = -EINVAL;
				goto out;
			}
#line 1047
			mca_log_err("main_cell_name %s\n", cell_name);
			if (!strcmp("coslight", cell_name)) {
				node = of_find_node_by_name(NULL, "mca_buckchg_jeita_second_para");
				if (!node) {
#line 1052
					mca_log_err("has-second-batt-para node in null\n");
					ret = -1;
					goto out;
				}
			}
		}
	}

	ret = mca_buckchg_jeita_parse_para(node, "jeita_para", jeita_info);
	info->support_base_flip = of_property_read_bool(node, "support-base-flip");
	info->base_flip_same = of_property_read_bool(node, "base-flip-same");
	if (info->support_base_flip) {
#line 1063
		mca_log_err("support base flip, start config buckchg jeita\n");
		mca_buckchg_jeita_parse_para(node, "base_jeita_para", base_jeita_info);
		ret = mca_buckchg_jeita_parse_para(node, "flip_jeita_para", flip_jeita_info);
	}

	if (!mca_buckchg_parse_dynamic_iterm_by_name(node, &info->dynamic_iterm)) {
		info->support_dynamic_iterm = true;
#line 1070
		mca_log_info("dynamic iterm support enabled\n");
	}
out:
	return ret;
}

static int mca_buckchg_jeita_process_event(int event, int value, void *data)
{
	struct mca_buckchg_jeita_dev *info = data;

	if (!info)
		return -1;

#line 1135
	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		info->online = 1;
		info->proc_data.cur_jeita_index = -1;
		info->proc_data.max_chg_curr = 0;
		info->base_proc_data.cur_jeita_index = -1;
		info->base_proc_data.max_chg_curr = 0;
		info->flip_proc_data.cur_jeita_index = -1;
		info->flip_proc_data.max_chg_curr = 0;
		cancel_delayed_work_sync(&info->monitor_work);
		schedule_delayed_work(&info->monitor_work, 0);
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->online = 0;
		cancel_delayed_work_sync(&info->monitor_work);
		break;
	case MCA_EVENT_BATTERY_DTPT:
		info->dtpt_status = value;
		break;
	default:
		break;
	}

	return 0;
}

static int mca_strategy_buckchg_jeita_get_normal_vterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_normal_vterm = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1172
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.size; i++) {
		cur_jeita_data = info->jeita_para.jeita_data + i;
		if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.size) {
#line 1183
		mca_log_err("can not find jeita para\n");
		return 0;
	}

	cur_normal_vterm = cur_jeita_data->vterm;

	return cur_normal_vterm;
}

static int mca_strategy_buckchg_jeita_get_ffc_iterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_ffc_itrem = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1200
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.fcc_size; i++) {
		cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
		if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.fcc_size) {
#line 1211
		mca_log_err("can not find jeita fcc para\n");
		return 0;
	}

	cur_ffc_itrem = cur_jeita_data->iterm;

	return cur_ffc_itrem;
}

static int mca_strategy_buckchg_jeita_get_ffc_vterm(struct mca_buckchg_jeita_dev *info)
{
	int cur_ffc_vterm = 0, temp = 0;
	int ret, i;
	struct mca_buckchg_jeita_data *cur_jeita_data;

	ret = strategy_class_fg_ops_get_temperature(&temp);
	if (ret) {
#line 1228
		mca_log_err("get battery temp failed\n");
		return 0;
	}
	temp /= 10;

	for (i = 0; i < info->jeita_para.fcc_size; i++) {
		cur_jeita_data = info->jeita_para.jeita_ffc_data + i;
		if (temp < cur_jeita_data->temp_high && temp >= cur_jeita_data->temp_low)
			break;
	}
	if (i == info->jeita_para.fcc_size) {
#line 1239
		mca_log_err("can not find jeita fcc para\n");
		return 0;
	}

	cur_ffc_vterm = cur_jeita_data->vterm;

	return cur_ffc_vterm;
}

static int mca_strategy_buckchg_jeita_get_status(int status, void *value, void *data)
{
	struct mca_buckchg_jeita_dev *info = (struct mca_buckchg_jeita_dev *)data;
	int *cur_val = (int *)value;
	int temp = 0, fastcharge, ret, i;

	if (!info || !value)
		return -1;

	switch (status) {
	case STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM:
		*cur_val = mca_strategy_buckchg_jeita_get_ffc_iterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_NORMAL_VTERM:
		*cur_val = mca_strategy_buckchg_jeita_get_normal_vterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_FFC_VTERM:
		*cur_val = mca_strategy_buckchg_jeita_get_ffc_vterm(info);
		break;
	case STRATEGY_STATUS_TYPE_JEITA_COLD_ZONE:
		*cur_val = info->cold_zone;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_VFLOAT:
		*cur_val = info->vfloat;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_HIGH_TEMP_RANGE:
		*cur_val = 0;
#line 1279
		mca_log_info("jeita high temp range check: cur_jeita_index=%d, size=%d\n",
			info->proc_data.cur_jeita_index, info->jeita_para.size);
		if (info->proc_data.cur_jeita_index >= 0 &&
			info->proc_data.cur_jeita_index >= info->jeita_para.size - 2)
			*cur_val = 1;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_DYNAMIC_ITERM:
		if (!info->support_dynamic_iterm)
			return 0;
		fastcharge = strategy_class_fg_get_fastcharge();
		ret = strategy_class_fg_ops_get_temperature(&temp);
		if (ret)
			return 0;
		temp /= 10;
		ret = mca_buckchg_get_dynamic_iterm(&info->dynamic_iterm, &info->jeita_para, temp, fastcharge);
		if (ret >= 0)
			*cur_val = ret;
		break;
	case STRATEGY_STATUS_TYPE_JEITA_IS_DYNAMIC_ITERM_TEMP_RANGE:
		*cur_val = 0;
		ret = strategy_class_fg_ops_get_temperature(&temp);
		if (ret)
			return 0;
		temp /= 10;
		if (!info->dynamic_iterm.range)
			return 0;
		for (i = 0; i < info->dynamic_iterm.size; i++) {
			if (temp >= info->dynamic_iterm.range[i].temp_low &&
				temp < info->dynamic_iterm.range[i].temp_high)
				*cur_val = 1;
		}
		break;
	default:
		return -1;
	}

	return 0;
}

static int mca_jeita_smartchg_delta_fv_callback(void *data, int effective_result)
{
	struct mca_buckchg_jeita_dev *info = (struct mca_buckchg_jeita_dev *)data;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!data)
		return -1;

	/* the stock compares platform_version == 3 and country_version == 1 (names of the values unknown) */
	if (hwid && hwid->platform_version == 3 && hwid->country_version == 1)
		effective_result = min(effective_result, 20);

	info->smartchg_data.delta_fv = effective_result;
#line 1329
	mca_log_err("effective_result: %d\n", effective_result);

	return 0;
}

static int mca_buckchg_jeita_smartchg_baa_update_jeita_data(
	struct mca_buckchg_jeita_data *jeita_data, struct smart_batt_jeita_term_para *para)
{
	int max_volt_idx = jeita_data->volt_para.size - 1;

#line 1339
	mca_log_info("jeita_term_para idx: %d, temp: %d:%d => %d:%d, vterm: %d => %d, iterm: %d => %d\n",
		para->t_range.idx, para->t_range.min, para->t_range.max,
		jeita_data->temp_low, jeita_data->temp_high,
		jeita_data->vterm, para->vterm,
		jeita_data->iterm, para->iterm);

	jeita_data->vterm = para->vterm;
	jeita_data->iterm = para->iterm;
	if (jeita_data->volt_para.size > 0) {
#line 1348
		mca_log_info("volt_data[%d].voltage: %d => %d\n",
			max_volt_idx, jeita_data->volt_para.volt_data[max_volt_idx].voltage, jeita_data->vterm);
		jeita_data->volt_para.volt_data[max_volt_idx].voltage = jeita_data->vterm;
	}

	return 0;
}

static int mca_buckchg_jeita_smartchg_update_baa_para(void *data, char *baa_para, int ffc_size, int normal_size)
{
	struct mca_buckchg_jeita_dev *info = (struct mca_buckchg_jeita_dev *)data;
	struct smart_batt_jeita_term_para *jeita_term_para = NULL;
	int offset = 0;
	int index = 0;

	if (!data || !baa_para) {
#line 1364
		mca_log_err("data or baa_para is NULL\n");
		return -1;
	}

	// update ffc jeita para
	jeita_term_para = (struct smart_batt_jeita_term_para *)baa_para;
	for (int i = 0; i < ffc_size; i++) {
		index = jeita_term_para[i].t_range.idx;
		if (index < 0 || index >= info->jeita_para.fcc_size) {
#line 1373
			mca_log_err("jeita_para.fcc_size: %d, invalid idx: %d\n", info->jeita_para.fcc_size, index);
		} else {
			mca_buckchg_jeita_smartchg_baa_update_jeita_data(
				&(info->jeita_para.jeita_ffc_data[index]), &(jeita_term_para[i]));
		}
	}

	// update normal jeita para
	offset = ffc_size * sizeof(struct smart_batt_jeita_term_para);
	jeita_term_para = (struct smart_batt_jeita_term_para *)(baa_para + offset);
	for (int i = 0; i < normal_size; i++) {
		index = jeita_term_para[i].t_range.idx;
		if (index < 0 || index >= info->jeita_para.size) {
#line 1386
			mca_log_err("jeita_para.size: %d, invalid idx: %d\n", info->jeita_para.size, index);
		} else {
			mca_buckchg_jeita_smartchg_baa_update_jeita_data(
				&(info->jeita_para.jeita_data[index]), &(jeita_term_para[i]));
		}
	}

	info->baacfg_update = true;
	return 0;
}

static struct mca_smartchg_if_ops g_jeita_smartchg_if_ops = {
	.type = MCA_SMARTCHG_IF_CHG_TYPE_JEITA,
	.data = NULL,
	.set_delta_fv = mca_jeita_smartchg_delta_fv_callback,
	.set_delta_ichg = NULL,
	.update_baa_para = mca_buckchg_jeita_smartchg_update_baa_para,
};

static int mca_buckchg_jeita_probe(struct platform_device *pdev)
{
	struct mca_buckchg_jeita_dev *info;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	static int parse_dt_cnt;
	int ret;
	int online = 0, wireless_online = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	ret = mca_buckchg_jeita_parse_dt(info);
	if (ret) {
#line 1421
		mca_log_err("parse dts failed, ret: %d\n", ret);
		if (ret != -1) {
			dev_err(&pdev->dev, "%s buckchg_jeita parse_dt_cnt = %d\n", __func__, ++parse_dt_cnt);
			msleep(100);
			return parse_dt_cnt > 49 ? -1 : -EPROBE_DEFER;
		}
	}
	if (hwid && (!strcmp(hwid->product_name, "donghai") || !strcmp(hwid->product_name, "yanan")))
		mca_buckchg_jeita_parse_dt_override(info);
	INIT_DELAYED_WORK(&info->monitor_work, mca_buckchg_jeita_monitor_work);
	mca_buckchg_jeita_init_voter(info);
	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_JEITA,
		mca_buckchg_jeita_process_event, mca_strategy_buckchg_jeita_get_status, NULL, info);
	g_jeita_smartchg_if_ops.data = info;
	(void)mca_smartchg_if_ops_register(&g_jeita_smartchg_if_ops);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &wireless_online);

	if (online || wireless_online) {
		info->online = 1;
		schedule_delayed_work(&info->monitor_work, 0);
		return 0;
	}

	if (!info->voter_ok)
		schedule_delayed_work(&info->monitor_work,
			msecs_to_jiffies(MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL));
#line 1458
	mca_log_err("probe ok\n");
	return 0;
}


static int mca_buckchg_jeita_remove(struct platform_device *pdev)
{
	return 0;
}

static void mca_buckchg_jeita_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,buckchg_jeita"},
	{},
};

static struct platform_driver mca_buckchg_jeita_driver = {
	.driver	= {
		.name = "mca_buckchg_jeita",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = mca_buckchg_jeita_probe,
	.remove = mca_buckchg_jeita_remove,
	.shutdown = mca_buckchg_jeita_shutdown,
};

static int __init mca_buckchg_jeita_init(void)
{
	return platform_driver_register(&mca_buckchg_jeita_driver);
}
module_init(mca_buckchg_jeita_init);

static void __exit mca_buckchg_jeita_exit(void)
{
	platform_driver_unregister(&mca_buckchg_jeita_driver);
}
module_exit(mca_buckchg_jeita_exit);

MODULE_DESCRIPTION("buckchg jeita");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
