// SPDX-License-Identifier: GPL-2.0
/*
 * mca_strategy_fg.c - Xiaomi MCA fuel gauge strategy (Xiaomi 15 "dada", Qualcomm variant).
 *
 * Reconstructed by reverse engineering from the stock mca_strategy_fg_comp.ko; the XRING sources of the same vendor code base
 * (mca_strategy_fg.c) were used as the template.  The stock module is a ThinLTO link of mca_strategy_fg.c, mca_soc_smooth.c
 * and mca_battery_shutdown.c; "#line" directives reproduce the __LINE__ values baked into the log calls.
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
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_voter.h>
#include <mca/qcom/mca_qcom_smem.h>
#include <mca/platform/platform_buckchg_class.h>
#include <mca/platform/platform_loadsw_class.h>
#include <mca/common/mca_charge_mievent.h>
#include <mca/smart_charge/mca_smart_charge.h>
#include <mca/common/mca_workqueue.h>
#include <mca/common/mca_hwid.h>
#include <mca/shared_memory/charger_partition_class.h>
#include <mca/protocol/protocol_class.h>
#include <mca/protocol/protocol_pd_class.h>
#include "strategy_fg.h"

#define MCA_LOG_TAG "mca_strategy_fg"

#define CountryCN 0
#define DEFAULT_DESIGN_CAPACITY 5000

#define MEAN_VOL_NUM 5
#define DISCHARGING_MAX_EMPTY_COUNT 2
#define FAST_CHARGING_MAX_EMPTY_COUNT 10
#define NORMAL_CHARGING_MAX_EMPTY_COUNT 2
#define VABTT_EMPTY_HY 100

void onewire_check_auth_pass(bool *auth_pass)
{
	*auth_pass = true;
}

void onewire_check_chip_ok(bool *chip_ok)
{
	*chip_ok = true;
}

#line 462
void strategy_fg_record_volt_mean(struct strategy_fg *fg)
{
	static int vol_i = 0, voltage_nums[MEAN_VOL_NUM] = {0};
	int sum = 0, cnt_i = 0;
	static int empty_count;
	int empty_count_th = DISCHARGING_MAX_EMPTY_COUNT;
	struct timespec64 ts;
	static int is_zero_speed = 0;

	if (!fg || !fg->fg_init_flag)
		return;
	if (fg->batt_voltage < 1000)
		return;

	voltage_nums[vol_i] = fg->batt_voltage;
#line 473
	mca_log_debug("record_i:%d record_vol [0]= %d, [1]= %d, [2]= %d, [3]= %d, [4]= %d\n",
		vol_i, voltage_nums[0], voltage_nums[1], voltage_nums[2], voltage_nums[3], voltage_nums[4]);
	vol_i = (vol_i + 1) % MEAN_VOL_NUM;

	for (cnt_i = 0; cnt_i < MEAN_VOL_NUM; cnt_i++) {
		if (voltage_nums[cnt_i] == 0)
			break;
		if (voltage_nums[cnt_i] > 0)
			sum += voltage_nums[cnt_i];
	}
	if (cnt_i == 0)
		fg->batt_voltage_mean = 0;
	else
		fg->batt_voltage_mean = sum / cnt_i;
#line 487
	mca_log_info("mean_vol:%d\n", fg->batt_voltage_mean);

	if (fg->chg_status != POWER_SUPPLY_STATUS_CHARGING)
		empty_count_th = DISCHARGING_MAX_EMPTY_COUNT;
	else
		empty_count_th = (fg->fast_charge ? FAST_CHARGING_MAX_EMPTY_COUNT : NORMAL_CHARGING_MAX_EMPTY_COUNT);

	ktime_get_boottime_ts64(&ts);
	if (!is_zero_speed && fg->chg_status == POWER_SUPPLY_STATUS_CHARGING && (u64)ts.tv_sec < 120)
		get_smem_battery_info(&is_zero_speed);
	if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING && is_zero_speed && (u64)ts.tv_sec < 120) {
#line 498
		mca_log_err("chg_status = %d, is_zero_speed = %d, tv_sec = %u, ignore vbatt_empty...\n",
			fg->chg_status, is_zero_speed, (u64)ts.tv_sec);
	} else {
		if (fg->batt_voltage_mean <= fg->vcutoff_sw) {
			empty_count++;
			fg->vbatt_empty = (empty_count > empty_count_th) ? true : false;
		} else if (fg->batt_voltage_mean > (fg->vcutoff_sw + VABTT_EMPTY_HY)) {
			empty_count = 0;
			fg->vbatt_empty = false;
		}
	}

#line 510
	mca_log_info("empty_count_th: %d, vbatt_empty: %d\n", empty_count_th, fg->vbatt_empty);
}

static void strategy_fg_update_batt_dc(struct strategy_fg *fg)
{
	int dc_master;
	int dc_slave;
	int ret;

	if (fg->cfg.design_capacity > 0) {
		fg->batt_dc = fg->cfg.design_capacity * 1000;
		return;
	}

	ret = platform_fg_ops_get_full_design(FG_IC_MASTER, &dc_master);
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE)
		ret |= platform_fg_ops_get_full_design(FG_IC_SLAVE, &dc_slave);
	if (ret)
		return;

	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE)
		fg->batt_dc = dc_master + dc_slave;
	else
		fg->batt_dc = dc_master;
}

static void strategy_fg_update_cycle(struct strategy_fg *fg)
{
	int ret1, ret2;

	if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE) {
		platform_fg_ops_get_cyclecount(FG_IC_MASTER, &fg->batt_cyclecount);
	} else if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL) {
		ret1 = platform_fg_ops_get_cyclecount(FG_IC_SLAVE, &fg->slave_batt_info.cycle_count);
		ret2 = platform_fg_ops_get_cyclecount(FG_IC_MASTER, &fg->master_batt_info.cycle_count);
		if (!(ret1 | ret2))
			fg->batt_info.cycle_count = fg->master_batt_info.cycle_count > fg->slave_batt_info.cycle_count ?
				fg->master_batt_info.cycle_count : fg->slave_batt_info.cycle_count;
	}
}

static void strategy_fg_update_rm(struct strategy_fg *fg)
{
	int ret1, ret2;

	if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE) {
		platform_fg_ops_get_rm(FG_IC_MASTER, &fg->batt_rm);
	} else if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL) {
		ret1 = platform_fg_ops_get_rm(FG_IC_SLAVE, &fg->slave_batt_info.rm);
		ret2 = platform_fg_ops_get_rm(FG_IC_MASTER, &fg->master_batt_info.rm);
		if (!(ret1 | ret2))
			fg->batt_info.rm = fg->master_batt_info.rm + fg->slave_batt_info.rm;
	}
}

void strategy_fg_update_batt_info(struct strategy_fg *fg)
{
	strategy_fg_update_batt_dc(fg);
	strategy_fg_update_cycle(fg);
	strategy_fg_update_rm(fg);
}

#line 3009
int strategy_lossless_rechg_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int ret = 0;
	int idata[3] = { 0 };

	ret = mca_parse_dts_u32(node, "support-lossless-rechg", &(fg->cfg.support_lossless_rechg), 0);
	if (ret < 0) {
#line 3010
		mca_log_err("parse support-lossless-rechg failed %d\n", ret);
		return ret;
	}
	if (fg->cfg.support_lossless_rechg) {
		ret = mca_parse_dts_u32_array(node, "lossless_rechg_cfg", idata, 3);
		if (ret < 0) {
#line 3016
			mca_log_err("parse lossless-rechg_cfg failed %d\n", ret);
			return ret;
		}

		fg->cfg.rechg_cfg.temp_low = idata[0];
		fg->cfg.rechg_cfg.temp_high = idata[1];
		fg->cfg.rechg_cfg.rest_time = idata[2];
	}

	fg->cfg.support_lossless_curr_limit = of_property_read_bool(node, "support-lossless-curr-limit");
	if (fg->cfg.support_lossless_curr_limit)
		ret = mca_parse_dts_u32(node, "lossless_curr_limit", &(fg->cfg.lossless_curr_limit), 0);

	return ret;
}

int strategy_full_design_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	fg->support_full_design_gl = of_property_read_bool(node, "support_full_design_gl");
	if (hwid && fg->support_full_design_gl && hwid->country_version != CountryCN) {
		(void)mca_parse_dts_u32(node, "charge-full-design-gl", &(fg->cfg.design_capacity),
			DEFAULT_DESIGN_CAPACITY);
	} else {
		(void)mca_parse_dts_u32(node, "charge-full-design", &(fg->cfg.design_capacity),
			DEFAULT_DESIGN_CAPACITY);
	}

	return 0;
}

int strategy_fg_ops_is_init_ok(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	return fg->fg_init_flag;
}

int strategy_fg_ops_is_chip_ok(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg->fg_init_flag)
		return 0;

	return !fg->fg_error;
}

int strategy_fg_dual_ops_is_chip_ok(void *data, int fg_index)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (fg_index == FG_IC_MASTER) {
		if (fg->fg_master_init_flag)
			return !fg->fg_master_error;
	} else if (fg_index == FG_IC_SLAVE) {
		if (fg->fg_slave_init_flag)
			return !fg->fg_slave_error;
	}

	return 0;
}

#define TEMP_PARA_MAX_GROUP 5
enum mca_quick_charge_temp_para_ele {
	MCA_FG_TEMP = 0,
	MCA_FG_TEMP_HYS,
	MCA_FG_NORMAL_ITERM,
	MCA_FG_FFC_ITERM,
	MCA_FG_ITERM_PARA_MAX,
};

#line 3085
static int strategy_parse_term_curr_para(struct device_node *node,
	int batt_role, const char *name, struct strategy_fg *fg)
{
	struct term_curr_para *iterm_para = &(fg->cfg.term_curr_data[batt_role]);
	int array_len, row, col, i;
	const char *tmp_string = NULL;
	struct term_curr_data *iterm_data = NULL;

	array_len = mca_parse_dts_count_strings(node, name,
		TEMP_PARA_MAX_GROUP,
		MCA_FG_ITERM_PARA_MAX);
	if (array_len < 0) {
#line 3098
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	iterm_para->iterm_para_size = array_len / MCA_FG_ITERM_PARA_MAX;
	iterm_para->iterm_data = kcalloc(iterm_para->iterm_para_size, sizeof(*iterm_data), GFP_KERNEL);
	if (!iterm_para->iterm_data) {
#line 3105
		mca_log_err("iterm para no mem\n");
		return -ENOMEM;
	}
	iterm_data = iterm_para->iterm_data;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_FG_ITERM_PARA_MAX;
		col = i % MCA_FG_ITERM_PARA_MAX;
		switch (col) {
		case MCA_FG_TEMP:
			if (kstrtoint(tmp_string, 10, &iterm_data[row].batt_temp))
				goto error;
			break;
		case MCA_FG_TEMP_HYS:
			if (kstrtoint(tmp_string, 10, &iterm_data[row].batt_temp_offset))
				goto error;
			break;
		case MCA_FG_NORMAL_ITERM:
			if (kstrtoint(tmp_string, 10, &iterm_data[row].normal_iterm))
				goto error;
			break;
		case MCA_FG_FFC_ITERM:
			if (kstrtoint(tmp_string, 10, &iterm_data[row].ffc_iterm))
				goto error;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < iterm_para->iterm_para_size; i++)
#line 3138
		mca_log_err("iterm para %d %d %d %d\n",
			iterm_data[i].batt_temp, iterm_data[i].batt_temp_offset, iterm_data[i].normal_iterm,
			iterm_data[i].ffc_iterm);

	return 0;
error:
	kfree(iterm_data);
	iterm_data = NULL;
	return -1;
}

enum mca_fg_iterm_para_ele {
	MCA_FG_BATT_ROLE = 0,
	MCA_FG_BATT_PARA_NAME,
	MCA_FG_BATT_PARA_MAX,
};
#define ITERM_PARA_MAX_GROUP 3

#line 3150
static int strategy_term_curr_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int array_len, row, col, i;
	int batt_role = 0;
	const char *tmp_string = NULL;

	array_len = mca_parse_dts_count_strings(node, "iterm_para",
		ITERM_PARA_MAX_GROUP,
		MCA_FG_BATT_PARA_MAX);
	if (array_len < 0) {
#line 3166
		mca_log_err("parse batt_para failed\n");
		return -1;
	}

	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, "iterm_para", i, &tmp_string))
			return -1;

		row = i / MCA_FG_BATT_PARA_MAX;
		col = i % MCA_FG_BATT_PARA_MAX;
		switch (col) {
		case MCA_FG_BATT_ROLE:
			if (kstrtoint(tmp_string, 10, &batt_role))
				return -1;
			if (batt_role > FG_IC_SLAVE)
				i += 1;
			break;
		case MCA_FG_BATT_PARA_NAME:
			if (strategy_parse_term_curr_para(node, batt_role, tmp_string, fg))
				return -1;
			break;
		default:
			break;
		}
	}

	if (!fg->cfg.term_curr_data[FG_IC_MASTER].iterm_data) {
#line 3193
		mca_log_err("parse master iterm para failed\n");
		return -1;
	}

	if (!fg->cfg.term_curr_data[FG_IC_SLAVE].iterm_data) {
#line 3198
		mca_log_err("parse slave iterm para failed\n");
		return -1;
	}

	return 0;
}

#define STRATEGY_FG_REPORT_FULL_RSOC 9700
#define STRATEGY_FG_SOC_PROPORTION 97
#define STRATEGY_FG_SOC_PROPORTION_C 96
#define STRATEGY_FG_DEFAULT_HIGH_TEMP_VTERM 4400

static int strategy_fg_parse_extreme_cold_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int row, col, len;
	int data[FG_EXTREME_COLD_MAX_GROUP * FG_EXTREME_COLD_SIZE] = { 0 };

	len = mca_parse_dts_string_array(node, "extreme_cold_temp_compemsation", data,
		FG_EXTREME_COLD_MAX_GROUP, FG_EXTREME_COLD_SIZE);
	if (len < 0) {
#line 3056
		mca_log_err("parse extreme_cold_temp_compemsation failed\n");
		return -1;
	}

	fg->cfg.extreme_cold_para_size = len / FG_EXTREME_COLD_SIZE;
	for (row = 0; row < fg->cfg.extreme_cold_para_size; row++) {
		col = row * FG_EXTREME_COLD_SIZE + FG_EXTREME_COLD_CYCLE_COUNT_MAX;
		fg->cfg.extreme_cold_para[row].cycle_count_max = data[col];
		col = row * FG_EXTREME_COLD_SIZE + FG_EXTREME_COLD_TEMP_MAX;
		fg->cfg.extreme_cold_para[row].temp_max = data[col];
		col = row * FG_EXTREME_COLD_SIZE + FG_EXTREME_COLD_COMPENSATION;
		fg->cfg.extreme_cold_para[row].compensation = data[col];
#line 3068
		mca_log_err("extreme_cold_para[%d]: %d, %d, %d\n",
			row, fg->cfg.extreme_cold_para[row].cycle_count_max,
			fg->cfg.extreme_cold_para[row].temp_max,
			fg->cfg.extreme_cold_para[row].compensation);
	}

	return 0;
}

#line 3200
static int strategy_fg_parse_dt(struct strategy_fg *fg)
{
	struct device_node *node = fg->dev->of_node;
	int ret = 0;
	int len;
	int i, j;
	int soc_decimal[SOC_DECIMAL_MAX_LEVEL * SOC_DECIMAL_MAX] = { 0 };

	if (!node) {
#line 3214
		mca_log_err("No DT data Failing Probe\n");
		return -EINVAL;
	}

	ret = mca_parse_dts_u32(node, "report-full-rsoc", &(fg->cfg.report_full_raw_soc), STRATEGY_FG_REPORT_FULL_RSOC);
	ret |= mca_parse_dts_u32(node, "soc-proportion", &(fg->cfg.soc_proportion), STRATEGY_FG_SOC_PROPORTION);
	ret |= mca_parse_dts_u32(node, "soc-proportion-c", &(fg->cfg.soc_proportion_c), STRATEGY_FG_SOC_PROPORTION_C);
	ret |= mca_parse_dts_u32(node, "fg_type", &(fg->cfg.fg_type), MCA_FG_TYPE_SINGLE);
	ret |= mca_parse_dts_u32(node, "adapt_power", &(fg->cfg.adapt_power), 0);
	ret |= mca_parse_dts_u32(node, "support_dtpt", &(fg->cfg.support_dtpt), 0);
	(void)mca_parse_dts_u32(node, "cool_threshold", &(fg->cfg.cool_threshold), 150);
	(void)mca_parse_dts_u32(node, "force_full_hys", &(fg->cfg.force_full_hys), 150);
	(void)mca_parse_dts_u32(node, "force_full_high_hys", &(fg->cfg.force_full_high_hys), 150);
	(void)mca_parse_dts_u32(node, "fg_hightemp_vterm", &(fg->cfg.fg_hightemp_vterm), STRATEGY_FG_DEFAULT_HIGH_TEMP_VTERM);
	(void)mca_parse_dts_u32(node, "vterm_delta", &(fg->cfg.vterm_delta), 7);
	(void)mca_parse_dts_u32(node, "support_nvt1000_ota", &(fg->cfg.support_nvt1000_ota), 0);
	(void)mca_parse_dts_u32(node, "support_batt_xvdd_dfx", &(fg->cfg.support_batt_xvdd_dfx), 0);
	ret |= mca_parse_dts_u32(node, "terminated_by_cp", &(fg->cfg.terminated_by_cp), 0);
	ret |= mca_parse_dts_u32(node, "clear_CC_per_hundred", &(fg->cfg.clear_CC_per_hundred), 1);
	fg->cfg.show_model_by_country_version = of_property_read_bool(node, "show-model-by-country-version");
	ret |= mca_parse_dts_string(node, "model-name", &(fg->cfg.model_name));
	ret |= mca_parse_dts_string(node, "model-name-global", &(fg->cfg.model_name_gl));
	ret |= mca_parse_dts_string(node, "model-name-in", &(fg->cfg.model_name_in));
	ret |= mca_parse_dts_u32(node, "fast_force_full_vol_delta", &(fg->cfg.fast_force_full_vol_delta), 15);
	fg->support_normal_force_full = of_property_read_bool(node, "support_normal_force_full");
	if (fg->support_normal_force_full)
		ret |= mca_parse_dts_u32(node, "normal_force_full_hys", &(fg->cfg.normal_force_full_hys), 0);
	if (ret) {
#line 3242
		mca_log_err("strategy fg parse dt failed, ret=%d\n", ret);
	}

	mca_parse_dts_u32(node, "support_fl4p0", &(fg->cfg.support_fl4p0), 0);
	if (fg->cfg.support_fl4p0) {
		len = mca_parse_dts_u32_count(node, "fl4p0_ffc_safe_iterm", SAFE_ITERM_CYCLE_LEVEL, SAFE_ITERM_TEMP_LEVEL);
		if (len < 0) {
#line 3250
			mca_log_err("parse fl4p0_ffc_safe_iterm failed\n");
			return -1;
		}
		ret = mca_parse_dts_u32_array(node, "fl4p0_ffc_safe_iterm", &fg->cfg.ffc_safe_item[0][0], len);
		if (ret < 0)
			return -1;
	}

	len = mca_parse_dts_u32_count(node, "soc_decimal", SOC_DECIMAL_MAX_LEVEL, SOC_DECIMAL_MAX);
	if (len < 0) {
#line 3260
		mca_log_err("parse soc_decimal len failed\n");
		return -1;
	}
	ret = mca_parse_dts_u32_array(node, "soc_decimal", soc_decimal, len);
	if (ret < 0) {
#line 3265
		mca_log_err("parse soc_decimal cfg failed\n");
		return -1;
	}
	fg->cfg.soc_decimal_cnt = len / SOC_DECIMAL_MAX;
	for (i = 0; i < fg->cfg.soc_decimal_cnt; i++) {
		j = SOC_DECIMAL_MAX * i;
		fg->cfg.soc_decimal[i].soc = soc_decimal[j + SOC_DECIMAL_SOC];
		fg->cfg.soc_decimal[i].rate = soc_decimal[j + SOC_DECIMAL_RATE];
	}

	ret = mca_battery_shutdown_parse_dt(fg);
	if (ret < 0) {
#line 3277
		mca_log_err("parse battery shutdown failed %d\n", ret);
		return ret;
	}

	ret = strategy_lossless_rechg_parse_dt(fg);
	if (ret < 0) {
#line 3283
		mca_log_err("parse lossless recharging failed %d\n", ret);
		return 0;
	}

	ret = strategy_full_design_parse_dt(fg);

	ret = strategy_soc_smooth_parse_dt(fg);
	if (ret < 0) {
#line 3295
		mca_log_err("parse soc smooth failed %d\n", ret);
		fg->smooth.suppot_RSOC_0_smooth = 0;
		return 0;
	}

	ret = strategy_fg_parse_extreme_cold_dt(fg);
	if (ret < 0) {
#line 3302
		mca_log_err("parse extreme cold dt failed %d\n", ret);
		return 0;
	}

	fg->cfg.support_base_flip = of_property_read_bool(node, "support-base-flip");
	fg->cfg.base_flip_same = of_property_read_bool(node, "base-flip-same");
	fg->cfg.support_pmic_vterm_dynamics_adjust = of_property_read_bool(node, "support-pmic-vterm-dynamics-adjust");
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
		ret = strategy_term_curr_parse_dt(fg);
		if (ret < 0) {
#line 3312
			mca_log_err("parse iterm failed %d\n", ret);
			return 0;
		}
	}
	fg->cfg.support_pnc = of_property_read_bool(node, "support-pnc");
	fg->cfg.wr_for_sw_cv_wake = of_property_read_bool(node, "wr-for-sw-cv-wake");
	fg->cfg.support_qbg = of_property_read_bool(node, "support-qbg");

	return ret;
}

static int strategy_fg_check_battery_adapt_power(void *data, int *match);

#define WEIGHT_SCALE 10
static int strategy_fg_get_dod_count(void *data, int *value)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int count_level1 = 0, count_level2 = 0, count_level3 = 0, count_lowtemp = 0;
	int slave_count_level1 = 0, slave_count_level2 = 0, slave_count_level3 = 0, slave_count_lowtemp = 0;
	int slave_dod_count;
	int ret = 0;

	if (!fg->support_dod_vcutoff)
		return 0;

	if (fg->fake_dod_count) {
		*value = fg->fake_dod_count;
		return 0;
	}
	ret = platform_fg_ops_get_dod_count(FG_IC_MASTER);
	ret |= platform_fg_ops_get_count_level1(FG_IC_MASTER, &count_level1);
	ret |= platform_fg_ops_get_count_level2(FG_IC_MASTER, &count_level2);
	ret |= platform_fg_ops_get_count_level3(FG_IC_MASTER, &count_level3);
	ret |= platform_fg_ops_get_count_lowtemp(FG_IC_MASTER, &count_lowtemp);

	*value = (fg->weight[WEIGHT_LEVEL_1] * count_level1 +
		fg->weight[WEIGHT_LEVEL_2] * count_level2 +
		fg->weight[WEIGHT_LEVEL_3] * count_level3) / WEIGHT_SCALE;

	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
		ret |= platform_fg_ops_get_dod_count(FG_IC_SLAVE);
		ret |= platform_fg_ops_get_count_level1(FG_IC_SLAVE, &slave_count_level1);
		ret |= platform_fg_ops_get_count_level2(FG_IC_SLAVE, &slave_count_level2);
		ret |= platform_fg_ops_get_count_level3(FG_IC_SLAVE, &slave_count_level3);
		ret |= platform_fg_ops_get_count_lowtemp(FG_IC_SLAVE, &slave_count_lowtemp);
		slave_dod_count = (fg->weight[WEIGHT_LEVEL_1] * slave_count_level1 +
			fg->weight[WEIGHT_LEVEL_2] * slave_count_level2 +
			fg->weight[WEIGHT_LEVEL_3] * slave_count_level3) / WEIGHT_SCALE;
#line 616
		mca_log_info("master_dod_count: %d, slave_dod_count:%d", *value, slave_dod_count);
		*value = max(*value, slave_dod_count);
	}

	return 0;
}

static int strategy_fg_get_parallel_rsoc(struct strategy_fg *fg, int *rsoc)
{
	int master_fcc;
	int slave_fcc;
	int master_weight;
	int slave_weight;
	int ret;

	ret = platform_fg_ops_get_fcc(FG_IC_MASTER, &master_fcc);
	ret |= platform_fg_ops_get_fcc(FG_IC_SLAVE, &slave_fcc);
	if (ret)
		return -1;

	master_weight = master_fcc * 100 / (master_fcc + slave_fcc);
	slave_weight = 100 - master_weight;
	ret = platform_fg_ops_get_rsoc(FG_IC_MASTER, &(fg->master_batt_info.rsoc));
	ret |= platform_fg_ops_get_rsoc(FG_IC_SLAVE, &(fg->slave_batt_info.rsoc));
	if (ret)
		return -1;

	*rsoc = (fg->master_batt_info.rsoc * master_weight + fg->slave_batt_info.rsoc * slave_weight + 50) / 100;
	return 0;
}

#define STRATEGY_FG_ERROR_FAKE_SOC 15
#define STRATEGY_FG_ERROR_FAKE_TEMP 250
#define STRATEGY_FG_ERROR_FAKE_CURR -500000
#define STRATEGY_FG_ERROR_FAKE_VOLT 3700
#define STRATEGY_FG_WORK_INTERVAL_FAST 1000

static int strategy_fg_get_series_rsoc(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_ops_get_rsoc(void *data, int *rsoc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		ret = platform_fg_ops_get_rsoc(FG_IC_MASTER, rsoc);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_rsoc(fg, rsoc);
		break;
	case MCA_FG_TYPE_SERIES:
		ret = strategy_fg_get_series_rsoc(fg);
		break;
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_ops_get_soc(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg)
		return STRATEGY_FG_ERROR_FAKE_SOC;

	if (fg->fake_soc != STRATEGY_FG_FAKE_SOC_NONE)
		return fg->fake_soc;

	if (!fg->fg_init_flag)
		return STRATEGY_FG_ERROR_FAKE_SOC;

	return fg->batt_ui_soc;
}

static int strategy_fg_ops_get_temp(void *data, int *temp)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		*temp = STRATEGY_FG_ERROR_FAKE_TEMP;
	else
		*temp = fg->batt_temperature;

	return 0;
}

static int strategy_fg_ops_get_thermal_temp(void *data, int *temp)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		*temp = STRATEGY_FG_ERROR_FAKE_TEMP;
	else if (fg->cfg.support_pnc)
		*temp = fg->batt_original_temperature;
	else
		*temp = fg->batt_temperature;

	return 0;
}

static int strategy_fg_get_parallel_curr(struct strategy_fg *fg, int *curr)
{
	int ret;

	ret = platform_fg_ops_get_curr(FG_IC_SLAVE, &(fg->slave_batt_info.curr));
	ret |= platform_fg_ops_get_curr(FG_IC_MASTER, &(fg->master_batt_info.curr));
	if (ret)
		return -1;

	*curr = fg->slave_batt_info.curr + fg->master_batt_info.curr;

	return 0;
}

static int strategy_fg_get_series_curr(struct strategy_fg *fg, int *curr)
{
	return 0;
}

static int strategy_fg_ops_get_curr(void *data, int *curr)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag) {
		*curr = STRATEGY_FG_ERROR_FAKE_CURR;
		return 0;
	}

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST) {
		*curr = fg->batt_current;
		return 0;
	}

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		return platform_fg_ops_get_curr(FG_IC_MASTER, curr);
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_curr(fg, curr);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_curr(fg, curr);
	default:
		return -1;
	}

	*curr = fg->batt_current;
	return 0;
}

static int strategy_fg_get_parallel_volt(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_volt(FG_IC_MASTER, &(fg->master_batt_info.volt));
	ret = platform_fg_ops_get_volt(FG_IC_SLAVE, &(fg->slave_batt_info.volt));
	if (ret)
		return -1;

	fg->batt_info.volt = fg->master_batt_info.volt > fg->slave_batt_info.volt ?
		fg->master_batt_info.volt : fg->slave_batt_info.volt;

	return 0;
}

static int strategy_fg_get_series_volt(struct strategy_fg *fg)
{
	return 0;
}

static int strategy_fg_update_batt_volt(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST)
		return 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		ret = platform_fg_ops_get_volt(FG_IC_MASTER, &fg->batt_voltage);
		break;
	case MCA_FG_TYPE_PARALLEL:
		return strategy_fg_get_parallel_volt(fg);
	case MCA_FG_TYPE_SERIES:
		return strategy_fg_get_series_volt(fg);
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_ops_get_volt(void *data, int *volt)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag) {
		*volt = STRATEGY_FG_ERROR_FAKE_VOLT;
		return 0;
	}

	strategy_fg_update_batt_volt(fg);
	*volt = fg->batt_voltage;

	return 0;
}

static int strategy_fg_ops_get_cycle(void *data, int *cycle)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*cycle = fg->batt_cyclecount;

	return 0;
}

static int strategy_fg_ops_get_rm(void *data, int *rm)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*rm = fg->batt_rm;

	return 0;
}

static int strategy_fg_get_volt_mean(void *data, int *vol_mean)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*vol_mean = fg->batt_voltage_mean;

	return 0;
}

#define STRATEGY_FG_SOC_DECIMAL_DEFAULT_RATE 10
static int strategy_fg_ops_get_soc_decimal(void *data, int *soc_decimal, int *rate)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int i;

	if (!data)
		return -1;

	*rate = STRATEGY_FG_SOC_DECIMAL_DEFAULT_RATE;
	for (i = fg->cfg.soc_decimal_cnt - 1; i >= 0; i--) {
		if (fg->batt_ui_soc > fg->cfg.soc_decimal[i].soc) {
			*rate = fg->cfg.soc_decimal[i].rate;
			break;
		}
	}
	/* make sure the decimal growth does not exceed 99 */
	*soc_decimal = fg->batt_ui_soc_decimal * (100 - *rate) / 100 - 1;

	return 0;
}

static bool strategy_fg_ops_get_charging_done(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	return fg->charging_done;
}

#line 3350
static int strategy_fg_ops_set_charging_done(void *data, bool charging_done)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

#line 3362
	mca_log_info("set charging_done: %d\n", charging_done);
	if (charging_done) {
		mca_vote(fg->en_voter, "term_recharge", true, 0);
		fg->charging_done = true;
		fg->recharging = false;
		fg->lossless_recharge = false;
		fg->near_vterm = false;
		fg->ffc_continue_charge = false;
		if (fg->is_eu_model) {
			if (fg->batt_rsoc < 100) {
				fg->en_smooth_full = true;
#line 3373
				mca_log_info("charging_done start en_smooth_full\n");
			} else {
				fg->en_smooth_full = false;
#line 3376
				mca_log_info("charging_done stop en_smooth_full\n");
			}
			fg->keep_full_flag = false;
		} else {
			fg->keep_full_flag = true;
		}
		if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
			platform_fg_ops_set_co(FG_IC_MASTER, false);
			platform_fg_ops_set_co(FG_IC_SLAVE, false);
			fg->monitor_soc_flag = false;
			fg->first_termination = false;
			fg->fg1_batt_ctr_enabled = false;
			fg->fg2_batt_ctr_enabled = false;
			fg->fg_lock_flag = false;
			fg->base_batt_close = false;
			mca_vote_override(fg->vterm_voter, "para_term", false, 0);
			mca_vote(fg->charge_limit_voter, "losslessRec", false, 0);
			mca_vote(fg->flip_fcc_voter, "losslessRec", false, 0);
			mca_vote(fg->flip_fcc_voter, "base_close_curr", false, 0);
		}

		fg->charging_done_time = ktime_get_boottime_seconds();
#line 3388
		mca_log_info("charging_done_time: %lld\n", fg->charging_done_time);
	} else {
		fg->charging_done = false;
		fg->en_smooth_full = false;
		fg->charging_done_time = 0;
	}

	return 0;
}

#line 2880
static int strategy_fg_ops_get_model_name(void *data, const char **model_name)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	const char *device_name = NULL;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	if (!fg || !fg->fg_init_flag)
		return -1;

	if (!strcmp(fg->cfg.model_name, fg->cfg.model_name_gl)) {
		*model_name = fg->cfg.model_name;
	} else {
		platform_fg_ops_get_device_name(FG_IC_MASTER, &device_name);
		if (device_name && !strncmp(device_name, "BP4U", 4)) {
			*model_name = fg->cfg.model_name;
#line 2896
			mca_log_info("project O3 matched BP4U, set cn model name\n");
		} else if (device_name && (!strncmp(device_name, "BP4V", 4) || !strcmp("5XM31", device_name) ||
			   !strncmp("5@BP", device_name, 4))) {
			*model_name = fg->cfg.model_name_gl;
#line 2900
			mca_log_info("Not cn project, set global model name\n");
		} else {
			*model_name = fg->cfg.model_name;
		}
	}

	if (fg->cfg.show_model_by_country_version) {
		switch (hwid->country_version) {
		case 0:
			*model_name = fg->cfg.model_name;
			break;
		case 1:
			*model_name = fg->cfg.model_name_gl;
			break;
		case 2:
			*model_name = fg->cfg.model_name_in;
			break;
		default:
			*model_name = fg->cfg.model_name_gl;
			break;
		}
	}


	return 0;
}

static int strategy_fg_ops_set_fastcharge(void *data, bool en)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int ret;

	if (!fg || !fg->fg_init_flag)
		return -1;

	ret = platform_fg_ops_set_fastcharge(FG_IC_MASTER, en);
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE)
		ret |= platform_fg_ops_set_fastcharge(FG_IC_SLAVE, en);

	if (!en && fg->cfg.fg_type != MCA_FG_TYPE_SINGLE && mca_is_override_vote_enabled(fg->vterm_voter)) {
		mca_vote_override(fg->vterm_voter, "para_term", false, 0);
#line 3446
		mca_log_err("cancel para_term vote\n");
	}

	return ret;
}

static int strategy_fg_ops_get_authentic(void *data, bool *authentic)
{
	int master_auth = 1;
	int slave_auth = 1;
	int ret;
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	ret = platform_fg_ops_get_authentic(FG_IC_MASTER, &master_auth);
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE)
		ret |= platform_fg_ops_get_authentic(FG_IC_SLAVE, &slave_auth);

	if (ret)
		return ret;

	*authentic = (master_auth && slave_auth);

	return 0;
}

static int strategy_fg_ops_get_dc(void *data, int *dc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*dc = fg->batt_dc;

	return 0;
}

static int strategy_fg_ops_get_fcc(void *data, int *fcc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*fcc = fg->batt_fcc;

	return 0;
}

static int strategy_fg_ops_get_health(void *data, int *health)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*health = fg->batt_health;
	return 0;
}

static int strategy_fg_ops_get_soh(void *data, int *soh)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*soh = fg->batt_soh;
	return 0;
}

static int strategy_fg_ops_get_first_termiation(void *data, int *first_termination)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*first_termination = fg->first_termination;
	return 0;
}

static int strategy_fg_ops_get_pack_vendor_id(void *data, int *pack_vendor_id)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	platform_fg_ops_get_pack_vendor(FG_IC_MASTER, pack_vendor_id);
	return 0;
}

static int strategy_fg_ops_get_temp_offset_flag(void *data, int *temp_offset_flag)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return -1;

	*temp_offset_flag = fg->temp_offset_flags[FG_IC_MASTER] || fg->temp_offset_flags[FG_IC_SLAVE];
	return 0;
}

#define DFS_CHECK_BATT_AUTH_TIME_SEC 300
#line 2290
static int fg_check_batt_auth(struct strategy_fg *fg)
{
	time64_t time_now;
	static bool checked;

	if (!checked) {
		int param = 0;

		time_now = ktime_get_boottime_seconds();
		if (time_now > DFS_CHECK_BATT_AUTH_TIME_SEC) {
#line 2299
			mca_log_err("check batt_auth: %d, time_now: %lld\n", fg->batt_auth, time_now);
			checked = true;
			if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL) {
				if (!fg->batt_auth) {
					param = 0;
					mca_charge_mievent_report(CHARGE_DFX_BATTERY_AUTH_FAIL_PARALLEL, &param, 1);
				}
#line 2306
				mca_log_err("check slave batt auth: %d, time_now: %lld\n", fg->batt_slave_auth, time_now);
				if (!fg->batt_slave_auth) {
					param = 1;
					mca_charge_mievent_report(CHARGE_DFX_BATTERY_AUTH_FAIL_PARALLEL, &param, 1);
				}
			} else if (!fg->batt_auth) {
				mca_charge_mievent_report(CHARGE_DFX_BATTERY_AUTH_FAIL, NULL, 0);
			}
		}
	}

	return 0;
}

#define STRATEGY_FG_STATUS_BATT_POWER_MATCH	0x12
#define STRATEGY_FG_STATUS_FORCE_FULL_DONE	0x15

static int strategy_fg_get_status(int status, void *value, void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int val = 0;

	if (!value || !data)
		return -1;

	switch (status) {
	case STRATEGY_FG_STATUS_BATT_POWER_MATCH:
		strategy_fg_check_battery_adapt_power(fg, &val);
		val = fg->batt_auth & val;
		break;
	case STRATEGY_FG_STATUS_FORCE_FULL_DONE:
		val = fg->done_force_full;
		break;
	default:
		return -1;
	}

	*(int *)value = val;
	return 0;
}

static void delay_reset_full_flag_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, delay_reset_full_flag_work.work);

	if (!fg || !fg->fg_init_flag)
		return;

	fg->keep_full_flag = false;
}

static void strategy_fg_reset_default_work(struct work_struct *work)
{
	platform_fg_ops_set_co(FG_IC_MASTER, false);
	platform_fg_ops_set_co(FG_IC_SLAVE, false);
}

static void strategy_fg_init_voter(struct strategy_fg *fg)
{
	fg->voter_ok = false;

	fg->en_voter = mca_find_votable("chg_enable");
	if (!fg->en_voter)
		return;
	fg->vterm_voter = mca_find_votable("term_volt");
	if (!fg->vterm_voter)
		return;
	fg->iterm_voter = mca_find_votable("term_curr");
	if (!fg->iterm_voter)
		return;
	fg->charge_limit_voter = mca_find_votable("buck_charge_curr");
	if (!fg->charge_limit_voter)
		return;
	fg->flip_fcc_voter = mca_find_votable("flip_charge_curr");
	if (!fg->flip_fcc_voter)
		return;
	fg->buck_input_voter = mca_find_votable("buck_input");
	if (!fg->buck_input_voter)
		return;

	fg->voter_ok = true;
}

#define STRATEGY_FG_WORK_INTERVAL_DTPT 5000

#line 2600
static void strategy_fg_dtpt_monitor_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, dtpt_monitor_work.work);
	int master_isc_alert = 0, slave_isc_alert = 0, master_soa_alert = 0, slave_soa_alert = 0;
	int interval = STRATEGY_FG_WORK_INTERVAL_DTPT;
	static int last_dtpt_status;
	int dtpt_status = 0;

	platform_fg_ops_get_isc_alert_level(FG_IC_MASTER, &master_isc_alert);
	platform_fg_ops_get_soa_alert_level(FG_IC_MASTER, &master_soa_alert);
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
		platform_fg_ops_get_isc_alert_level(FG_IC_SLAVE, &slave_isc_alert);
		platform_fg_ops_get_soa_alert_level(FG_IC_SLAVE, &slave_soa_alert);
	}

	master_isc_alert = max(master_isc_alert, slave_isc_alert);
	master_soa_alert = max(master_soa_alert, slave_soa_alert);

#line 2617
	mca_log_info("isc_alert = %d, soa_alert = %d\n", master_isc_alert, master_soa_alert);

	if (master_isc_alert == 3 || master_soa_alert)
		dtpt_status = 1;
	else
		dtpt_status = 0;

	if (dtpt_status != last_dtpt_status) {
		last_dtpt_status = dtpt_status;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_BATTERY_DTPT, &dtpt_status);
	}

	schedule_delayed_work(&fg->dtpt_monitor_work, msecs_to_jiffies(interval));
}

static void strategy_fl4p0_calibration_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, fl4p0_calibration_work.work);
	int supplement_energy, charge_energy;

	if (fg->charging_done)
		return;

	platform_fg_ops_get_real_supplement_energy(FG_IC_MASTER, &supplement_energy);
	platform_fg_ops_get_calibration_charge_energy(FG_IC_MASTER, &charge_energy);
#line 2642
	mca_log_info("supplement_energy = %d, charge_energy = %d", supplement_energy, charge_energy);
	if (supplement_energy != 0xffff && supplement_energy < charge_energy &&
	    fg->batt_temperature >= fg->calibration_temp - 50 && fg->batt_temperature <= fg->calibration_temp + 50) {
		schedule_delayed_work(&fg->fl4p0_calibration_work, msecs_to_jiffies(5000));
		return;
	}

#line 2647
	mca_log_info("ok to calibration iterm");
	strategy_class_fg_ops_set_charging_done(true);
}

#define FG_FORCE_REPORT_FULL_TIMES 2

static void strategy_force_report_full_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, force_report_full_work.work);
	static int count;

	if (fg->power_present && fg->fast_charge && !fg->charging_done) {
		if (fg->batt_rsoc == 100) {
			count = 0;
#line 2671
			mca_log_info("fg check report full ok");
			return;
		}

		platform_fg_ops_set_force_report_full(FG_IC_MASTER);
#line 2668
		mca_log_info("fg not report full, send force_report_full cmd again...");
		count++;
		if (count >= FG_FORCE_REPORT_FULL_TIMES) {
			count = 0;
			return;
		}
		schedule_delayed_work(&fg->force_report_full_work, msecs_to_jiffies(8000));
		return;
	}

	count = 0;
}

#define NVT1000_OTA_NEED_FLAG 0xaa5555aa
#define NVT1000_OTA_UPDATE_COUNT 9

static void strategy_nvt1000_ota_monitor_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, nvt1000_ota_monitor_work.work);
	static int update_count;
	unsigned int ota_flag = 0;
	int average_current = 0;
	int flag;
	bool cid_status = false;
	union power_supply_propval pval = { 0 };

	if (fg->batt_psy) {
		power_supply_get_property(fg->batt_psy, POWER_SUPPLY_PROP_STATUS, &pval);
		fg->chg_status = pval.intval;
	}

	if (fg->chg_status == POWER_SUPPLY_STATUS_FULL || fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
#line 2696
		mca_log_err("do not ota when charging\n");
		update_count = 0;
		return;
	}

	platform_fg_ops_get_ota_update_flag(FG_IC_MASTER, &ota_flag);
	if (ota_flag != NVT1000_OTA_NEED_FLAG) {
#line 2703
		mca_log_err("already ota update done\n");
		update_count = 0;
		return;
	}

	platform_fg_ops_get_average_current(FG_IC_MASTER, &average_current);
	protocol_class_pd_get_cid_status(0, &cid_status);
	if (fg->batt_rsoc >= 60 && fg->batt_temperature >= 100 &&
	    average_current >= 100 && average_current <= 1000 && !cid_status)
		update_count++;
	else
		update_count = 0;

#line 2715
	mca_log_err("cid_status = %d, batt_rsoc = %d, batt_temperature = %d, average_current = %d, update_count = %d",
		cid_status, fg->batt_rsoc, fg->batt_temperature, average_current, update_count);

	if (update_count >= NVT1000_OTA_UPDATE_COUNT) {
#line 2719
		mca_log_err("start nvt1000 ota\n");
		flag = 1;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_FG_OTA_PROCESS, &flag);
		platform_fg_ops_ota_update_check(FG_IC_MASTER);
		flag = 0;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_FG_OTA_PROCESS, &flag);
#line 2725
		mca_log_err("end nvt1000 ota\n");
		return;
	}

	schedule_delayed_work(&fg->nvt1000_ota_monitor_work, 5000);
}

struct fg_batt_abnormal_info {
	int ocd_count;
	int ocd_info;
	int hocd_count;
	int hocd_info;
	int cuv_count;
	int cuv_info;
	int hcuv_count;
	int hcuv_info;
	int hscd_count;
	int hscd_info;
};

static void strategy_batt_abnormal_dfx_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, batt_abnormal_dfx_work.work);
	struct fg_batt_abnormal_info info = { 0 };
	int last_ocd_count = 0, last_hocd_count = 0, last_cuv_count = 0, last_hcuv_count = 0, last_hscd_count = 0;
	int param[7] = { 0 };

	platform_fg_ops_get_batt_abnormal_info(FG_IC_MASTER, &info);
	charger_partition_read_ocd_count(&last_ocd_count, &last_hocd_count);
	charger_partition_read_cuv_count(&last_cuv_count, &last_hcuv_count);
	charger_partition_read_hscd_count(&last_hscd_count);
#line 2748
	mca_log_info("last_ocd_count:%d, last_hocd_count:%d, last_cuv_count:%d, last_hcuv_count:%d, last_hscd_count:%d\n",
		last_ocd_count, last_hocd_count, last_cuv_count, last_hcuv_count, last_hscd_count);
#line 2750
	mca_log_info("ocd_count:%d, hocd_count:%d, cuv_count:%d, hcuv_count:%d, hscd_count:%d\n",
		info.ocd_count, info.hocd_count, info.cuv_count, info.hcuv_count, info.hscd_count);

	if (last_ocd_count != info.ocd_count || last_hocd_count != info.hocd_count) {
		param[0] = info.ocd_count;
		param[1] = info.ocd_info;
		param[2] = info.hocd_count;
		param[3] = info.hocd_info;
		param[4] = fg->batt_ui_soc;
		param[5] = fg->batt_voltage;
		param[6] = fg->batt_temperature;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_OCD_COUNT, param, 7);
		charger_partition_write_ocd_count(info.ocd_count, info.hocd_count);
	}

	if (last_cuv_count != info.cuv_count || last_hcuv_count != info.hcuv_count) {
		param[0] = info.cuv_count;
		param[1] = info.cuv_info;
		param[2] = info.hcuv_count;
		param[3] = info.hcuv_info;
		param[4] = fg->batt_ui_soc;
		param[5] = fg->batt_voltage;
		param[6] = fg->batt_temperature;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_CUV_COUNT, param, 7);
		charger_partition_write_cuv_count(info.cuv_count, info.hcuv_count);
	}

	if (last_hscd_count != info.hscd_count) {
		param[0] = info.hscd_count;
		param[1] = info.hscd_info;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_HSCD_COUNT, param, 2);
		charger_partition_write_hcsd_count(info.hscd_count);
	}
}

#define MCA_EVENT_PANEL_SCREEN_STATE_CHANGE 0x40
#define MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE 0x3e
#define NTC_SCALE_BOARDTEMP 100

#line 3840
static int strategy_fg_panel_notifier_cb(struct notifier_block *nb, unsigned long event, void *val)
{
	struct strategy_fg *fg = container_of(nb, struct strategy_fg, panel_nb);
	int status;

	switch (event) {
	case MCA_EVENT_PANEL_SCREEN_STATE_CHANGE:
		status = *(int *)val;
#line 3844
		mca_log_info("update screen_state: %d => %d\n", fg->screen_status, status);
		fg->screen_status = !!status;
		break;
	default:
		break;
	}
	return 0;
}

static int strategy_fg_thermal_notifier_cb(struct notifier_block *nb, unsigned long event, void *val)
{
	struct strategy_fg *fg = container_of(nb, struct strategy_fg, thermal_board_nb);

	switch (event) {
	case MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE:
		fg->thermal_board_temp = *(int *)val / NTC_SCALE_BOARDTEMP;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

#define MEAN_CUR_NUM 5
#line 440
static void strategy_fg_record_curr_mean(struct strategy_fg *fg)
{
	static int current_nums[MEAN_CUR_NUM] = {0};
	static int cur_num = 0;
	int sum = 0;

	if (!fg || !fg->fg_init_flag)
		return;

	current_nums[cur_num] = fg->batt_current;
	cur_num = (cur_num + 1) % MEAN_CUR_NUM;
#line 442
	mca_log_debug("record_i:%d, record_cur [0]= %d, [1]= %d, [2]= %d, [3]= %d, [4]= %d\n",
		cur_num, current_nums[0], current_nums[1], current_nums[2], current_nums[3], current_nums[4]);

	for (int i = 0; i < MEAN_CUR_NUM; i++)
		sum = sum + current_nums[i];

	fg->batt_current_mean = sum / MEAN_CUR_NUM;
#line 449
	mca_log_info("mean_cur:%d\n", fg->batt_current_mean);
}

static int strategy_fg_get_cutoff_voltage(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int cutoff_voltage = 0;
	int ret;

	if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE)
		ret = platform_fg_ops_get_cutoff_voltage(FG_IC_MASTER, &cutoff_voltage);
	else if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
		ret = platform_fg_ops_get_cutoff_voltage(FG_IC_MASTER, &cutoff_voltage);
	}

	return cutoff_voltage;
}

static int strategy_fg_set_cutoff_voltage(void *data, int value)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE)
		platform_fg_ops_set_cutoff_voltage(FG_IC_MASTER, value);
	else {
		platform_fg_ops_set_cutoff_voltage(FG_IC_MASTER, value);
		platform_fg_ops_set_cutoff_voltage(FG_IC_SLAVE, value);
	}

	return 0;
}

#line 710
static int strategy_fg_update_vcuttoff_voltage(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int vcutoff_reg = 0;
	int ret = 0;

	ret = mca_battery_shutdown_update_vcutoff_para(fg);
	if (ret)
		return 0;

	vcutoff_reg = strategy_fg_get_cutoff_voltage(fg);

	if (vcutoff_reg != fg->vcutoff_fw) {
		strategy_fg_set_cutoff_voltage(fg, fg->vcutoff_fw);
#line 717
		mca_log_err("vcutoff_reg:%d, vcutoff_fw %d", vcutoff_reg, fg->vcutoff_fw);
	}
	return 0;
}

#line 722
static int strategy_fg_check_extreme_cold_temp_compensation(struct strategy_fg *fg, int real_temp)
{
	int temp = real_temp;

	if (mca_smartchg_is_extreme_cold_enabled()) {
		for (int i = 0; i < fg->cfg.extreme_cold_para_size; i++) {
			if (fg->batt_cyclecount < fg->cfg.extreme_cold_para[i].cycle_count_max &&
				real_temp < fg->cfg.extreme_cold_para[i].temp_max) {
#line 730
				mca_log_err("cc: %d, real_temp: %d, compensation: %d\n",
					fg->batt_cyclecount, real_temp,
					fg->cfg.extreme_cold_para[i].compensation);
				temp = real_temp - fg->cfg.extreme_cold_para[i].compensation;
				break;
			}
		}
	}

	return temp;
}

static int strategy_fg_get_parallel_error_state(struct strategy_fg *fg)
{
	bool fg_error_master;
	bool fg_error_slave;
	int ret;

	ret = platform_fg_ops_get_error_state(FG_IC_MASTER, &fg_error_master);
	ret |= platform_fg_ops_get_error_state(FG_IC_SLAVE, &fg_error_slave);
	pr_err("%s master %d slave %d\n", __func__, fg_error_master, fg_error_slave);
	if (ret)
		return -1;

	fg->fg_master_error = fg_error_master;
	fg->fg_slave_error = fg_error_slave;
	fg->fg_error = fg_error_master | fg_error_slave;

	return 0;
}

static int strategy_fg_get_error_state(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		platform_fg_ops_get_error_state(FG_IC_MASTER, &fg->fg_error);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_get_parallel_error_state(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

static int strategy_fg_update_fw(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		platform_fg_ops_update_fw(FG_IC_MASTER);
		break;
	case MCA_FG_TYPE_PARALLEL:
		platform_fg_ops_update_fw(FG_IC_MASTER);
		platform_fg_ops_update_fw(FG_IC_SLAVE);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

#define EXTREME_HIGH_DEGREE 1000

static int strategy_fg_get_parallel_temp(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_temp(FG_IC_SLAVE, &(fg->slave_batt_info.temp));
	ret |= platform_fg_ops_get_temp(FG_IC_MASTER, &(fg->master_batt_info.temp));
	pr_err("%s master %d slave %d\n", __func__, fg->master_batt_info.temp, fg->slave_batt_info.temp);
	if (ret)
		return -1;

	if (fg->master_batt_info.temp <= fg->cfg.cool_threshold ||
	    fg->slave_batt_info.temp <= fg->cfg.cool_threshold)
		fg->batt_info.temp = min(fg->master_batt_info.temp, fg->slave_batt_info.temp);
	else if (min(fg->master_batt_info.temp, fg->slave_batt_info.temp) > EXTREME_HIGH_DEGREE)
		fg->batt_info.temp = min(fg->master_batt_info.temp, fg->slave_batt_info.temp);
	else
		fg->batt_info.temp = max(fg->master_batt_info.temp, fg->slave_batt_info.temp);

	return 0;
}

static int strategy_fg_get_parallel_original_temp(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_original_temp(FG_IC_SLAVE, &(fg->slave_batt_info.original_temp));
	ret |= platform_fg_ops_get_original_temp(FG_IC_MASTER, &(fg->master_batt_info.original_temp));
	pr_info("%s master %d slave %d\n", __func__, fg->master_batt_info.original_temp, fg->slave_batt_info.original_temp);
	if (ret)
		return -1;

	if (fg->master_batt_info.original_temp <= fg->cfg.cool_threshold ||
	    fg->slave_batt_info.original_temp <= fg->cfg.cool_threshold)
		fg->batt_info.original_temp = min(fg->master_batt_info.original_temp, fg->slave_batt_info.original_temp);
	else if (min(fg->master_batt_info.original_temp, fg->slave_batt_info.original_temp) > EXTREME_HIGH_DEGREE)
		fg->batt_info.original_temp = min(fg->master_batt_info.original_temp, fg->slave_batt_info.original_temp);
	else
		fg->batt_info.original_temp = max(fg->master_batt_info.original_temp, fg->slave_batt_info.original_temp);

	return 0;
}

static int strategy_fg_get_parallel_cyclecount(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_cyclecount(FG_IC_SLAVE, &(fg->slave_batt_info.cycle_count));
	ret |= platform_fg_ops_get_cyclecount(FG_IC_MASTER, &(fg->master_batt_info.cycle_count));
	if (ret)
		return -1;

	fg->batt_info.cycle_count = fg->master_batt_info.cycle_count > fg->slave_batt_info.cycle_count ?
		fg->master_batt_info.cycle_count : fg->slave_batt_info.cycle_count;

	return 0;
}

static int strategy_fg_get_parallel_soh(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_soh(FG_IC_SLAVE, &(fg->slave_batt_info.soh));
	ret |= platform_fg_ops_get_soh(FG_IC_MASTER, &(fg->master_batt_info.soh));
	if (ret)
		return -1;

	fg->batt_info.soh = fg->master_batt_info.soh > fg->slave_batt_info.soh ?
		fg->master_batt_info.soh : fg->slave_batt_info.soh;

	return 0;
}

static int strategy_fg_get_parallel_rm(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_rm(FG_IC_SLAVE, &(fg->slave_batt_info.rm));
	ret |= platform_fg_ops_get_rm(FG_IC_MASTER, &(fg->master_batt_info.rm));
	if (ret)
		return -1;

	fg->batt_info.rm = fg->master_batt_info.rm + fg->slave_batt_info.rm;

	return 0;
}

static int strategy_fg_get_parallel_fcc(struct strategy_fg *fg)
{
	int ret;

	ret = platform_fg_ops_get_fcc(FG_IC_SLAVE, &(fg->slave_batt_info.fcc));
	ret |= platform_fg_ops_get_fcc(FG_IC_MASTER, &(fg->master_batt_info.fcc));
	if (ret)
		return -1;

	fg->batt_info.fcc = fg->master_batt_info.fcc + fg->slave_batt_info.fcc;

	return 0;
}

#line 1060
static int strategy_fg_get_parallel_batt_info(struct strategy_fg *fg)
{
	int ret = 0;
	int i = 0;
	struct fg_batt_info batt_info;

	ret = strategy_fg_get_parallel_curr(fg, &(fg->batt_info.curr));
	ret |= strategy_fg_get_parallel_temp(fg);
	ret |= strategy_fg_get_parallel_original_temp(fg);
	ret |= strategy_fg_get_parallel_rsoc(fg, &(fg->batt_info.rsoc));
	ret |= strategy_fg_get_parallel_volt(fg);
	ret |= strategy_fg_get_parallel_cyclecount(fg);
	ret |= strategy_fg_get_parallel_soh(fg);
	ret |= strategy_fg_get_parallel_rm(fg);
	ret |= strategy_fg_get_parallel_fcc(fg);

	for (i = 0; i <= fg->cfg.fg_type; i++) {
		batt_info = i ? fg->slave_batt_info : fg->master_batt_info;
#line 1092
		mca_log_err("fg_index[%d], CURRENT:%d RSOC:%d VOLTAGE:%d TEMP:%d ORIGINAL_TEMP:%d\n",
			i, batt_info.curr, batt_info.rsoc, batt_info.volt, batt_info.temp, batt_info.original_temp);
#line 1094
		mca_log_info("fg_index[%d], CYCLECOUNT:%d S:%d RM:%d F:%d\n",
			i, batt_info.cycle_count, batt_info.soh, batt_info.rm, batt_info.fcc);
	}

	return ret;
}

static int strategy_fg_get_batt_info(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		ret = platform_fg_ops_get_batt_info(FG_IC_MASTER, &fg->batt_info);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_get_parallel_batt_info(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

static int strategy_fg_set_clear_count_data(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		platform_fg_ops_set_clear_count_data(FG_IC_MASTER);
		break;
	case MCA_FG_TYPE_PARALLEL:
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

#line 1150
static void strategy_fg_report_battery_status_changed(struct strategy_fg *fg)
{
	static int last_soc, last_temp;
	int batt_temp = fg->batt_temperature / 10;

	if (fg->batt_ui_soc != last_soc || batt_temp != last_temp) {
#line 1151
		mca_log_err("ui_soc: %d => %d, temp: %d => %d\n",
			last_soc, fg->batt_ui_soc, last_temp, batt_temp);
		last_soc = fg->batt_ui_soc;
		last_temp = batt_temp;
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
			MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	}
}

#define VBAT_DIFF_THRESHOLD 500
#define VBAT_DIFF_CHECK_START_SEC 180
#line 1990
static void dfx_fg_check_vbat_diff(struct strategy_fg *fg)
{
	static bool check_started;
	int diff;
	int vbat[2];
	time64_t time_now;

	if (!check_started) {
		time_now = ktime_get_boottime_seconds();
		if (time_now < VBAT_DIFF_CHECK_START_SEC)
#line 1997
			mca_log_err("time_now: %lld\n", time_now);
		else
			check_started = true;
		return;
	}

	diff = fg->master_batt_info.volt > fg->slave_batt_info.volt ?
		fg->master_batt_info.volt - fg->slave_batt_info.volt :
		fg->slave_batt_info.volt - fg->master_batt_info.volt;
#line 2005
	mca_log_err("batt volt diff = %d\n", diff);
	if (diff > VBAT_DIFF_THRESHOLD) {
		vbat[0] = fg->master_batt_info.volt;
		vbat[1] = fg->slave_batt_info.volt;
		mca_charge_mievent_report(CHARGE_DFX_BATT_VBAT_DIFF, vbat, 2);
	} else {
		mca_charge_mievent_set_state(MIEVENT_STATE_END, CHARGE_DFX_BATT_VBAT_DIFF);
	}
}

static int strategy_fg_get_fast_charge(struct strategy_fg *fg)
{
	int ffc = 0;
	int slave_ffc = 0;

	if (!fg || !fg->fg_init_flag)
		return 0;

	platform_fg_ops_get_fastcharge(FG_IC_MASTER, &ffc);
	if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
		platform_fg_ops_get_fastcharge(FG_IC_SLAVE, &slave_ffc);
		ffc = (ffc && slave_ffc);
	}

	return ffc;
}

static int strategy_fg_ops_get_fastcharge(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;

	if (!fg || !fg->fg_init_flag)
		return 0;

	if (fg->update_interval == STRATEGY_FG_WORK_INTERVAL_FAST)
		return fg->fast_charge;

	return strategy_fg_get_fast_charge(fg);
}

#line 2180
static int mca_strategy_update_fg_info(struct strategy_fg *fg)
{
	static bool master_i2c_error;
	int ret;

	strategy_fg_get_error_state(fg);
	if (fg->fg_error) {
		if ((fg->cfg.support_base_flip || fg->cfg.base_flip_same) && fg->fg_master_error && !fg->fg_slave_error)
			master_i2c_error = true;
		mca_charge_mievent_report(CHARGE_DFX_FG_IIC_ERR, &fg->batt_ui_soc, 1);
		fg->batt_ui_soc = STRATEGY_FG_ERROR_FAKE_SOC;
		fg->batt_temperature = STRATEGY_FG_ERROR_FAKE_TEMP;
		strategy_fg_report_battery_status_changed(fg);
		return -1;
	}

	if (master_i2c_error == true && fg->power_present) {
#line 2193
		mca_log_err("last master i2c error and slave i2c is ok, now dual i2c is ok, so set exit lowpower \n");
		platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MAIN, false);
		master_i2c_error = false;
	}

	ret = strategy_fg_get_batt_info(fg);
	if (ret)
		return ret;

	if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL)
		dfx_fg_check_vbat_diff(fg);

	if (fg->cyclecount_hundred != fg->batt_cyclecount / 100 &&
		fg->batt_cyclecount % 100 == 0) {
		fg->cyclecount_hundred = fg->batt_cyclecount / 100;
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_CYCLECOUNT, &fg->batt_cyclecount, 1);
		if (fg->cfg.clear_CC_per_hundred) {
#line 2211
			mca_log_err("clear_CC_per_hundred: %d\n", fg->cyclecount_hundred);
			strategy_fg_set_clear_count_data(fg);
		}
	}

	fg->batt_current = fg->batt_info.curr;
	fg->batt_voltage = fg->batt_info.volt;
	fg->batt_temperature = strategy_fg_check_extreme_cold_temp_compensation(fg, fg->batt_info.temp);
	fg->batt_original_temperature = fg->batt_info.original_temp;
	fg->batt_rsoc = fg->batt_info.rsoc;
	fg->batt_rm = fg->batt_info.rm;
	fg->batt_fcc = fg->batt_info.fcc;
	fg->batt_cyclecount = fg->batt_info.cycle_count;
	fg->batt_soh = fg->batt_info.soh;


	if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL) {
		int master_weight = fg->master_batt_info.fcc * 100 / (fg->master_batt_info.fcc + fg->slave_batt_info.fcc);
		int slave_weight = 100 - master_weight;
		int master_raw_soc = fg->master_batt_info.rm * 10 / (fg->master_batt_info.fcc / 1000);
		int slave_raw_soc = fg->slave_batt_info.rm * 10 / (fg->slave_batt_info.fcc / 1000);
		fg->batt_raw_soc = (master_raw_soc * master_weight + slave_raw_soc * slave_weight + 50) / 100;
#line 2231
		mca_log_info("master_rate:%d, slave_rate:%d, master_raw_soc:%d, slave_raw_soc:%d\n",
			master_weight, slave_weight, master_raw_soc, slave_raw_soc);
	} else {
		fg->batt_raw_soc = ((fg->batt_rm / 1000) * 10000) / (fg->batt_fcc / 1000);
	}

	strategy_fg_get_dod_count(fg, &fg->dod_count);
	fg->fast_charge = strategy_fg_get_fast_charge(fg);

	return 0;
}

#define BATTERY_TEMP_HOT_THRESHOLD 550
#define BATTERY_TEMP_COLD_THRESHOLD -100

enum mievent_battery_temp_abnormal_ele {
	MIEVENT_BATTERY_TEMP_PARAM_TBAT,
	MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX,
	MIEVENT_BATTERY_TEMP_PARAM_TBAT_MIN = MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX,
	MIEVENT_BATTERY_TEMP_PARAM_IS_CHARGING,
	MIEVENT_BATTERY_TEMP_PARAM_TBOARD,
	MIEVENT_BATTERY_TEMP_PARAM_MAX,
};

#line 2260
static int fg_abnormal_temp_notify_mievent(struct strategy_fg *fg)
{
	int param[MIEVENT_BATTERY_TEMP_PARAM_MAX] = {0};

	if (fg->batt_temperature <= BATTERY_TEMP_HOT_THRESHOLD &&
		fg->batt_temperature >= BATTERY_TEMP_COLD_THRESHOLD) {
#line 2271
		mca_log_info("battery temp normal ignore");
		return 0;
	}

	param[MIEVENT_BATTERY_TEMP_PARAM_TBAT] = fg->batt_temperature;
	param[MIEVENT_BATTERY_TEMP_PARAM_IS_CHARGING] = (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) ? 1 : 0;
	param[MIEVENT_BATTERY_TEMP_PARAM_TBOARD] = fg->thermal_board_temp;
	if (fg->batt_temperature < BATTERY_TEMP_COLD_THRESHOLD) {
		platform_fg_ops_get_temp_min(FG_IC_MASTER, &param[MIEVENT_BATTERY_TEMP_PARAM_TBAT_MIN]);
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_TEMP_COLD, param, MIEVENT_BATTERY_TEMP_PARAM_MAX);
	} else if (fg->batt_temperature > BATTERY_TEMP_HOT_THRESHOLD) {
		platform_fg_ops_get_temp_max(FG_IC_MASTER, &param[MIEVENT_BATTERY_TEMP_PARAM_TBAT_MAX]);
		mca_charge_mievent_report(CHARGE_DFX_BATTERY_TEMP_HOT, param, MIEVENT_BATTERY_TEMP_PARAM_MAX);
	}

	return 0;
}

#define BATT_OVERHEAT_THRESHOLD	580
#define BATT_WARM_THRESHOLD	480
#define BATT_COOL_THRESHOLD	150
#define BATT_COLD_THRESHOLD	0

static int fg_update_batt_health(struct strategy_fg *fg)
{
	int temp = fg->batt_temperature;
	int health = POWER_SUPPLY_HEALTH_GOOD;

	if (temp >= BATT_OVERHEAT_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (temp >= BATT_WARM_THRESHOLD && temp < BATT_OVERHEAT_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_WARM;
	else if (temp >= BATT_COOL_THRESHOLD && temp < BATT_WARM_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_GOOD;
	else if (temp >= BATT_COLD_THRESHOLD && temp < BATT_COOL_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_COOL;
	else if (temp < BATT_COLD_THRESHOLD)
		health = POWER_SUPPLY_HEALTH_COLD;

	if (health != fg->batt_health) {
		fg->batt_health = health;
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGE_STATUS, MCA_EVENT_BATTERY_HEALTH_CHANGE, NULL);
	}
	return 0;
}

#line 1180
static void strategy_fg_report_batt_ov(struct strategy_fg *fg)
{
	unsigned int chg_status;
	int vterm;

	if (!fg->cfg.wr_for_sw_cv_wake)
		return;

	chg_status = 0;
	if (fg->wls_online)
		return;

	if (!fg->input_suppend_voter)
		fg->input_suppend_voter = mca_find_votable("input_suspend");
	if (fg->input_suppend_voter && mca_get_client_vote(fg->input_suppend_voter, "wire_qc"))
		return;

	platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chg_status);
	if (chg_status < 5 || chg_status == 8) {
		vterm = mca_get_effective_result(fg->vterm_voter);
		if (fg->batt_voltage >= vterm - 10) {
#line 1190
			mca_log_err("batt_voltage: %d, fg_ov_voltage: %d\n", fg->batt_voltage, vterm - 10);
			mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, 0x4b, 1);
		}
	}
}

#line 2360
static int fg_update_status(struct strategy_fg *fg, bool prohibit_jump)
{
	static bool last_fg_error;
	int ret;
	union power_supply_propval pval = { 0 };

	if (fg->batt_psy) {
		power_supply_get_property(fg->batt_psy, POWER_SUPPLY_PROP_STATUS, &pval);
		fg->chg_status = pval.intval;
	} else {
		fg->batt_psy = power_supply_get_by_name("battery");
	}

	ret = mca_strategy_update_fg_info(fg);
	if (fg->fg_error != last_fg_error) {
#line 2367
		mca_log_err("fg_error:%d\n", fg->fg_error);
		last_fg_error = fg->fg_error;
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BMD, 0x50, fg->fg_error);
	}
	if (ret || fg->fg_error) {
#line 2373
		mca_log_err("get batt info fail\n");
		return -1;
	}
	fg_update_batt_health(fg);
	strategy_fg_record_volt_mean(fg);
	strategy_fg_record_curr_mean(fg);
	strategy_fg_update_vcuttoff_voltage(fg);
	strategy_fg_update_fw(fg);
	strategy_fg_report_batt_ov(fg);

	fg->batt_ui_soc = fg_ui_soc_smooth(fg, prohibit_jump);
	strategy_fg_report_battery_status_changed(fg);
	(void)platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER, &fg->pvbat);
	(void)platform_class_buckchg_ops_get_pack_ibat(MAIN_BUCK_CHARGER, &fg->pibat);
	(void)platform_class_buckchg_ops_get_pack_tbat(MAIN_BUCK_CHARGER, &fg->ptbat);

	fg_abnormal_temp_notify_mievent(fg);

#line 2392
	mca_log_err("UISOC:%d CURRENT:%d RSOC:%d VOLTAGE:%d TEMP:%d PVBAT:%d PIBAT:%d PTBAT:%d HEALTH:%d\n",
		fg->batt_ui_soc, fg->batt_current, fg->batt_rsoc, fg->batt_voltage, fg->batt_temperature, fg->pvbat, fg->pibat, fg->ptbat, fg->batt_health);
#line 2394
	mca_log_info("CC:%d S:%d RM:%d F:%d RAWSOC:%d, DODCOUNT:%d\n",
		fg->batt_cyclecount, fg->batt_soh, fg->batt_rm, fg->batt_fcc, fg->batt_raw_soc, fg->dod_count);

	return 0;
}

#define MCA_QUICK_CHG_STS_CHARGING 1
#define MCA_QUICK_CHG_STS_NO_CHARGING 3
#define STRATEGY_STATUS_TYPE_CHARGING 2
#define MCA_BATT_CHGR_STATUS_TERMINATION 5
#define STRATEGY_FG_DEFAULT_ITERM 250
#define STRATEGY_FG_DEFAULT_VTERM 4400

#line 1215
static int mca_strategy_check_sigle_termination(struct strategy_fg *fg)
{
	static int count;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	int soft_iterm;
	int soft_vterm;
	int quick_charge_status = 0;
	int chgr_status = 0;
	int online = 0;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online) {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	} else {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	}

	if (fg->voter_ok) {
		iterm = mca_get_effective_result(fg->iterm_voter);
		vterm = mca_get_effective_result(fg->vterm_voter);
	}
	soft_iterm = iterm >= 1000 ? (iterm * 110 / 100) : (iterm * 115 / 100);
	soft_vterm = vterm - fg->cfg.vterm_delta;

	if (!fg->power_present || quick_charge_status == MCA_QUICK_CHG_STS_CHARGING ||
		fg->charging_done || vterm <= fg->cfg.fg_hightemp_vterm)
		return 0;

	platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_status);
	if (chgr_status == MCA_BATT_CHGR_STATUS_TERMINATION) {
		if (fg->cfg.support_pmic_vterm_dynamics_adjust) {
			mca_vote_override(fg->vterm_voter, "pmic_temp_term", false, 0);
			mca_vote_override(fg->buck_input_voter, "pmic_temp_term", false, 0);
		}
		strategy_class_fg_ops_set_charging_done(true);
#line 1235
		mca_log_err("vterm: %d, iterm: %d, vbat: %d, ibat: %d, pvbat: %d, chgr_status: %d, charging_done: %d\n",
			vterm, iterm, fg->batt_voltage, fg->batt_current, fg->pvbat, chgr_status, fg->charging_done);
	} else if (-fg->batt_current / 1000 < soft_iterm && fg->batt_voltage >= soft_vterm) {
		fg->near_vterm = true;
		if (++count >= 5) {
			count = 0;
			if (fg->cfg.support_pmic_vterm_dynamics_adjust) {
				mca_vote_override(fg->vterm_voter, "pmic_temp_term", false, 0);
				mca_vote_override(fg->buck_input_voter, "pmic_temp_term", false, 0);
			}
			strategy_class_fg_ops_set_charging_done(true);
		}
#line 1247
		mca_log_err("soft_vterm: %d, soft_iterm: %d, vbat: %d, ibat: %d, pvbat: %d, count: %d, charging_done: %d\n",
			soft_vterm, soft_iterm, fg->batt_voltage, fg->batt_current, fg->pvbat, count, fg->charging_done);
	} else {
		fg->near_vterm = false;
		count = 0;
	}

	return 0;
}

#define MCA_EVENT_QUICK_CHARGE_PMIC_TERM 0x3d

#line 1700
static void mca_strategy_parallel_force_fw_report_full(struct strategy_fg *fg, int fg_index)
{
	int force_iterm, force_vterm;
	int count = 6;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	int quick_charge_status = MCA_QUICK_CHG_STS_NO_CHARGING;
	struct fg_batt_info batt_info;
	static int force_full_count[2] = {0, 0};

	if (!fg->power_present || !mca_get_effective_result(fg->en_voter))
		return;

	batt_info = fg_index == FG_IC_MASTER ? fg->master_batt_info : fg->slave_batt_info;



	if (fg->lossless_recharge && (fg->cfg.support_base_flip || fg->cfg.base_flip_same))
		count = 2;

	if (fg->voter_ok) {
		iterm = fg->dual_iterm[fg_index];
		vterm = mca_get_client_vote(fg->vterm_voter, "jeita");
	}

	bool fast = fg->fast_charge;

	if (fg->fg1_batt_ctr_enabled || fg->fg2_batt_ctr_enabled) {
		force_iterm = iterm * 130 / 100;
		force_vterm = vterm - 30;
	} else if (fg->cfg.base_flip_same) {
		force_vterm = vterm - 15;
		if (fast)
			force_iterm = iterm * 115 / 100;
		else
			force_iterm = iterm + 30;
	} else if (fast) {
		force_iterm = iterm * 115 / 100;
		force_vterm = vterm - 30;
	} else {
		force_vterm = vterm - 25;
		if (fg->wls_online) {
			if (fg->batt_rsoc >= 98)
				force_iterm = iterm + 33;
			else
				force_iterm = iterm + (!fg_index ? 33 : 12);
		} else {
			if (fg->batt_temperature <= 30)
				force_iterm = iterm + 12 + (!fg_index ? 1 : 0);
			else
				force_iterm = iterm + (!fg_index ? 33 : 12);
		}
	}


#line 1708
	mca_log_info("fg_index[%d], vterm: %d, iterm: %d, force_iterm: %d, force_vterm: %d, start_force_full: %d\n",
		fg_index, vterm, iterm, force_iterm, force_vterm, fg->dual_force_full[fg_index]);

	if (fg->power_present && (batt_info.temp < 480) && !fg->charging_done &&
	    -batt_info.curr / 1000 < force_iterm && batt_info.volt > force_vterm && !fg->dual_force_full[fg_index]) {
		force_full_count[fg_index]++;
#line 1714
		mca_log_err("force_full_count[%d] =  %d", fg_index, force_full_count[fg_index]);
		if (force_full_count[fg_index] >= count) {
			if (!fg->wls_online && fg->cfg.support_base_flip && !fg->fg1_batt_ctr_enabled &&
			    !fg->fg2_batt_ctr_enabled && !fg_index) {
				mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
				if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING) {
					mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
						MCA_EVENT_QUICK_CHARGE_PMIC_TERM, fg->dual_iterm[1]);
					force_full_count[0] = 0;
					fg->base_batt_close = true;
#line 1725
					mca_log_err("switch to pmic termination\n");
					return;
				}
			}

			platform_fg_ops_set_force_report_full(fg_index);
			force_full_count[fg_index] = 0;
			fg->dual_force_full[fg_index] = true;
#line 1733
			mca_log_err("start report full");
			if (fg->self_equal_flag[fg_index]) {
				fg->self_equal_count[fg_index]++;
				if (fg->self_equal_count[fg_index] >= fg->self_equal_count_max) {
					fg->self_equal_count[fg_index] = false;
					fg->self_equal_flag[fg_index] = false;
				}
#line 1741
				mca_log_err("start equal report full, self_equal_count[fg_index]: %d", fg_index, fg->self_equal_count[fg_index]);
			} else {
				fg->self_equal_count[fg_index] = false;
			}
		}
	} else {
		force_full_count[fg_index] = 0;
		fg->dual_force_full[fg_index] = false;
	}
}

#line 1800
static void mca_strategy_start_recharging(struct strategy_fg *fg)
{
#line 1808
	mca_log_err("start to recharging\n");
	strategy_class_fg_ops_set_charging_done(false);
	strategy_class_fg_set_fastcharge(false);
	fg->recharging = true;
	mca_vote(fg->en_voter, "term_recharge", false, 0);

	mca_mod_delayed_work(&fg->monitor_work, msecs_to_jiffies(1000));
}

#line 1850
static int mca_strategy_get_ffc_safe_item(struct strategy_fg *fg)
{
	int temp_index = 0, cycle_index = 0;

	if (fg->batt_cyclecount <= 100)
		cycle_index = 0;
	else if (fg->batt_cyclecount <= 300)
		cycle_index = 1;
	else if (fg->batt_cyclecount <= 500)
		cycle_index = 2;
	else if (fg->batt_cyclecount <= 800)
		cycle_index = 3;
	else
		cycle_index = 4;

	if (fg->batt_temperature < 200)
		temp_index = 0;
	else if (fg->batt_temperature < 250)
		temp_index = 1;
	else if (fg->batt_temperature < 300)
		temp_index = 2;
	else if (fg->batt_temperature < 350)
		temp_index = 3;
	else if (fg->batt_temperature < 400)
		temp_index = 4;
	else if (fg->batt_temperature < 450)
		temp_index = 5;
	else
		temp_index = 6;

	return fg->cfg.ffc_safe_item[cycle_index][temp_index];
}


static int mca_strategy_get_fl4p0_iterm(struct strategy_fg *fg)
{
	int iterm;
	int safe_item = 0;
	int calibration_ffc_iterm = 0;

	safe_item = mca_strategy_get_ffc_safe_item(fg);
	platform_fg_ops_get_calibration_ffc_iterm(FG_IC_MASTER, &calibration_ffc_iterm);
	iterm = max(safe_item, calibration_ffc_iterm);

#line 1881
	mca_log_info("safe_item: %d, calibration_ffc_iterm: %d, iterm: %d\n", safe_item, calibration_ffc_iterm, iterm);

	return iterm;
}

static ssize_t strategy_fg_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t strategy_fg_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static struct mca_sysfs_attr_info strategy_fg_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_MAIN_AUTH, authentic),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_SLAVE_AUTH, slave_authentic),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_BATT_INDEX, verify_slave_flag),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_VERIFY_DIGEST, verify_digest),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_BATT_POWER_MATCH, batt_power_match),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_FAST_CHARGE, fast_charge),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SOC_DECIMAL, soc_decimal),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SOC_DECIMAL_RATE, soc_decimal_rate),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_FAKE_SOC, fake_soc),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_FAKE_TEMP, fake_temp),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_BATTERY_NUM, battery_num),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_UPDATE_PERIOD, update_period),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_PACK_VOLTAGE, pack_vbat),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_DOD_COUNT, dod_count),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_ENABLE_ROLLBACK, enable_rollback),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SOH_NEW, soh_new),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_MASTER_SELF_EQUAL_COUNT, master_self_equal_count),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_SLAVE_SELF_EQUAL_COUNT, slave_self_equal_count),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_SELF_EQUAL_MAX_COUNT, self_equal_max_count),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_PACK_TBAT, pack_tbat),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_RAW_SOC, raw_soc),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_CALC_RVALUE, calc_rvalue),
	mca_sysfs_attr_ro(strategy_fg_sysfs, 0440, FG_PROP_MANUFACTURING_DATE, manufacturing_date),
	mca_sysfs_attr_rw(strategy_fg_sysfs, 0664, FG_PROP_FIRST_USAGE_DATE, first_usage_date),
};

#define FG_ATTRS_SIZE ARRAY_SIZE(strategy_fg_sysfs_field_tbl)

static struct attribute *strategy_fg_sysfs_attrs[FG_ATTRS_SIZE + 1];

static const struct attribute_group strategy_fg_sysfs_attr_group = {
	.attrs = strategy_fg_sysfs_attrs,
};

static int strategy_fg_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(strategy_fg_sysfs_attrs, strategy_fg_sysfs_field_tbl, FG_ATTRS_SIZE);
	return mca_sysfs_create_link_group("fuelgauge", "strategy_fg", dev, &strategy_fg_sysfs_attr_group);
}

static void strategy_fg_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("fuelgauge", "strategy_fg", dev, &strategy_fg_sysfs_attr_group);
}

#line 3410
static int strategy_fg_check_battery_adapt_power(void *data, int *match)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int ret = 0;
	int adapt_power = 0, adapt_power_slave = 0;

	if (fg->fake_bap_match >= 0) {
		*match = fg->fake_bap_match;
	} else {
		if (fg->cfg.fg_type == MCA_FG_TYPE_SINGLE) {
			ret = platform_fg_ops_get_adapt_power(FG_IC_MASTER, &adapt_power);
		} else {
			ret = platform_fg_ops_get_adapt_power(FG_IC_MASTER, &adapt_power);
			ret |= platform_fg_ops_get_adapt_power(FG_IC_SLAVE, &adapt_power_slave);
			if (fg->cfg.support_base_flip) {
				if (adapt_power == 55 && adapt_power_slave == 15)
					adapt_power = 67;
			} else if (fg->cfg.base_flip_same) {
				if (adapt_power == 33 && adapt_power_slave == 33)
					adapt_power = 67;
			} else {
				if (adapt_power != adapt_power_slave)
					adapt_power = -1;
			}
		}
#line 3423
		mca_log_info("check battery adapt_power: %d, read value %d\n", fg->cfg.adapt_power, adapt_power);
		*match = fg->cfg.adapt_power == adapt_power;
	}
#line 3426
	mca_log_info("%s: fake_bap_match: %d, match %d\n", __func__, fg->fake_bap_match, *match);

	return ret;
}

static void strategy_fg_ops_get_single_soh_new(struct strategy_fg *fg, int *soh_new)
{
	int master_ui_soh = 0;

	platform_fg_ops_get_ui_soh(FG_IC_MASTER, &master_ui_soh);
	*soh_new = master_ui_soh;
}

#line 640
static void strateg_fg_ops_get_parallel_soh_new(struct strategy_fg *fg, int *soh_new)
{
	int master_ui_soh = 0, slave_ui_soh = 0;
	int master_cap = 0, slave_cap = 0;

	platform_fg_ops_get_ui_soh(FG_IC_MASTER, &master_ui_soh);
	platform_fg_ops_get_ui_soh(FG_IC_SLAVE, &slave_ui_soh);
	platform_fg_ops_get_full_design(FG_IC_MASTER, &master_cap);
	platform_fg_ops_get_full_design(FG_IC_SLAVE, &slave_cap);
	master_cap /= 1000;
	slave_cap /= 1000;

	if (master_cap < 1 || master_cap > 10000)
		master_cap = 0;
	if (slave_cap < 1 || slave_cap > 10000)
		slave_cap = 0;
	if (!master_cap && !slave_cap) {
		master_cap = 3920;
		slave_cap = 1235;
	}

	*soh_new = (master_ui_soh * master_cap + slave_ui_soh * slave_cap) / (master_cap + slave_cap);
#line 651
	mca_log_info("master_cap=%d, slave_cap=%d, master_ui_soh=%d, slave_ui_soh=%d, soh_new=%d\n",
		master_cap, slave_cap, master_ui_soh, slave_ui_soh, *soh_new);
}

static int strategy_fg_get_soh_new(struct strategy_fg *fg, int *soh_new)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		strategy_fg_ops_get_single_soh_new(fg, soh_new);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strateg_fg_ops_get_parallel_soh_new(fg, soh_new);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

#line 670
static unsigned long strateg_fg_ops_get_parallel_calc_rvalue(struct strategy_fg *fg)
{
	unsigned long master_rvalue, slave_rvalue, rvalue;

	master_rvalue = platform_fg_ops_get_calc_rvalue(FG_IC_MASTER);
	slave_rvalue = platform_fg_ops_get_calc_rvalue(FG_IC_SLAVE);

	rvalue = (master_rvalue > slave_rvalue) ? master_rvalue : slave_rvalue;
#line 680
	mca_log_info("rvalue=%d, master_rvalue=%d, slave_rvalue=%d\n",
		rvalue, master_rvalue, slave_rvalue);

	return rvalue;
}

static unsigned long strategy_fg_get_calc_rvalue(struct strategy_fg *fg)
{
	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		return platform_fg_ops_get_calc_rvalue(FG_IC_MASTER);
	case MCA_FG_TYPE_PARALLEL:
		return strateg_fg_ops_get_parallel_calc_rvalue(fg);
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}
	return 0;
}

#define FG_DATE_LEN 8

#line 2020
static void strategy_fg_get_parallel_manufacturing_date(struct strategy_fg *fg, unsigned char *manufacturing_date)
{
	unsigned char date[FG_IC_MAX][FG_DATE_LEN] = {0};
	bool master_empty = true, slave_empty = true;
	bool choose_slave = false;
	int i;

	platform_fg_ops_get_manufacturing_date(FG_IC_MASTER, date[FG_IC_MASTER]);
	platform_fg_ops_get_manufacturing_date(FG_IC_SLAVE, date[FG_IC_SLAVE]);
#line 2027
	mca_log_err("get parallel_manufacturing_date fail: fg_master is %s fg_slave is %s\n", date[FG_IC_MASTER], date[FG_IC_SLAVE]);

	for (i = 0; i < FG_DATE_LEN; i++) {
		if (date[FG_IC_MASTER][i])
			master_empty = false;
		if (date[FG_IC_SLAVE][i])
			slave_empty = false;
	}
	if (master_empty || slave_empty) {
		if (master_empty && slave_empty)
			memset(manufacturing_date, 0, FG_DATE_LEN);
		else if (master_empty)
			memcpy(manufacturing_date, date[FG_IC_SLAVE], FG_DATE_LEN);
		else
			memcpy(manufacturing_date, date[FG_IC_MASTER], FG_DATE_LEN);
		return;
	}

	for (i = 3; i < FG_DATE_LEN; i++) {
		if (date[FG_IC_MASTER][i] != date[FG_IC_SLAVE][i]) {
			choose_slave = date[FG_IC_MASTER][i] >= date[FG_IC_SLAVE][i];
#line 2046
			mca_log_info(" Warning: inconsistent fg_master is %s fg_slave is %s, choose %d\n", date[FG_IC_MASTER], date[FG_IC_SLAVE], choose_slave);
			break;
		}
	}
	memcpy(manufacturing_date, date[choose_slave], FG_DATE_LEN);
}

static int mca_strategy_get_manufacturing_date(struct strategy_fg *fg, unsigned char *manufacturing_date)
{
	if (!fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		platform_fg_ops_get_manufacturing_date(FG_IC_MASTER, manufacturing_date);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_get_parallel_manufacturing_date(fg, manufacturing_date);
		break;
	default:
		return -1;
	}
	return 0;
}

static void strategy_fg_get_parallel_first_usage_date(struct strategy_fg *fg, unsigned char *first_usage_date)
{
	unsigned char date[FG_IC_MAX][FG_DATE_LEN] = {0};
	bool master_valid, slave_valid;
	int i;

	platform_fg_ops_get_first_usage_date(FG_IC_MASTER, date[FG_IC_MASTER]);
	platform_fg_ops_get_first_usage_date(FG_IC_SLAVE, date[FG_IC_SLAVE]);
	master_valid = memcmp(date[FG_IC_MASTER], "99999999", FG_DATE_LEN) != 0;
	slave_valid = memcmp(date[FG_IC_SLAVE], "99999999", FG_DATE_LEN) != 0;

	if (!master_valid && !slave_valid) {
		memcpy(first_usage_date, "99999999", FG_DATE_LEN);
		return;
	}
	if (!master_valid) {
		memcpy(first_usage_date, date[FG_IC_SLAVE], FG_DATE_LEN);
		return;
	}
	if (!slave_valid) {
		memcpy(first_usage_date, date[FG_IC_MASTER], FG_DATE_LEN);
		return;
	}

	for (i = 0; i < FG_DATE_LEN; i++) {
		if (date[FG_IC_MASTER][i] != date[FG_IC_SLAVE][i])
			break;
	}
	if (i == FG_DATE_LEN || date[FG_IC_MASTER][i] > date[FG_IC_SLAVE][i])
		memcpy(first_usage_date, date[FG_IC_MASTER], FG_DATE_LEN);
	else
		memcpy(first_usage_date, date[FG_IC_SLAVE], FG_DATE_LEN);
}

static int mca_strategy_get_first_usage_date(struct strategy_fg *fg, unsigned char *first_usage_date)
{
	if (!fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		platform_fg_ops_get_first_usage_date(FG_IC_MASTER, first_usage_date);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_get_parallel_first_usage_date(fg, first_usage_date);
		break;
	default:
		return -1;
	}
	return 0;
}

#line 395
static void strategy_fg_set_fake_soc(struct strategy_fg *fg, int val)
{
#line 400
	mca_log_err("set fake_soc: %d\n", val);
	if (val == STRATEGY_FG_FAKE_SOC_NONE || (val >= 0 && val <= 100)) {
		if (val != fg->fake_soc) {
			fg->fake_soc = val;
			cancel_delayed_work_sync(&fg->monitor_work);
			mca_queue_delayed_work(&fg->monitor_work, 0);
			mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO, MCA_EVENT_BATTERY_STS_CHANGE, NULL);
		}
	}
}

static void strategy_fg_set_fake_temp(struct strategy_fg *fg, int val)
{
#line 415
	mca_log_err("set fake_temp: %d\n", val);
	if (val != fg->fake_temp) {
		fg->fake_temp = val;
		platform_fg_ops_set_temp(FG_IC_MASTER, val);
		if (fg->cfg.fg_type == MCA_FG_TYPE_PARALLEL)
			platform_fg_ops_set_temp(FG_IC_SLAVE, val);

		cancel_delayed_work_sync(&fg->monitor_work);
		mca_queue_delayed_work(&fg->monitor_work, 0);
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO, MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	}
}

#define MCA_EVENT_FG_ERROR 0x43
#define STRATEGY_EVENT_DUAL_TERM_CURR 0x14
#define STRATEGY_STATUS_TYPE_FFC_FCC 5
#define STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM 0xe
#define STRATEGY_STATUS_TYPE_DEVICE_POWER_MAX 0x14
#define MCA_FG_UPDATE_INTERVAL_FAST 1000

static void strategy_fg_reset_co_to_default(struct strategy_fg *fg)
{
	platform_fg_ops_set_co(FG_IC_MASTER, false);
	platform_fg_ops_set_co(FG_IC_SLAVE, false);
	fg->monitor_soc_flag = false;
	fg->first_termination = false;
	fg->fg1_batt_ctr_enabled = false;
	fg->fg2_batt_ctr_enabled = false;
	fg->fg_lock_flag = false;
	fg->base_batt_close = false;
	mca_vote_override(fg->vterm_voter, "para_term", false, 0);
	mca_vote(fg->charge_limit_voter, "losslessRec", false, 0);
	mca_vote(fg->flip_fcc_voter, "losslessRec", false, 0);
	mca_vote(fg->flip_fcc_voter, "base_close_curr", false, 0);
}

#line 1270
static int mca_strategy_parallel_first_termination(struct strategy_fg *fg)
{
	int ret = 0;
	int chgr_status = 0;
	int rsoc_master, rsoc_slave, tem_soc_delt;
	static int soc_delt;
	bool fc_master = false, fc_slave = false;

	ret = platform_class_buckchg_ops_get_chg_status(MAIN_BUCK_CHARGER, &chgr_status);
	if (ret) {
#line 1288
		mca_log_err("get chg status fail");
		return ret;
	}
	ret |= platform_fg_ops_get_rsoc(FG_IC_MASTER, &rsoc_master);
	ret |= platform_fg_ops_get_rsoc(FG_IC_SLAVE, &rsoc_slave);
#line 1293
	mca_log_err("fg_master_rsoc:%d, fg_slave_rsoc:%d", rsoc_master, rsoc_slave);
	/* monitor battery to avoid locking */
	if (fg->fg1_batt_ctr_enabled || fg->fg2_batt_ctr_enabled || fg->fg_lock_flag) {
		tem_soc_delt = abs(rsoc_master - rsoc_slave);
		if ((tem_soc_delt > 5) && !fg->fg_lock_flag) {
			if (!fg->monitor_soc_flag) {
				soc_delt = tem_soc_delt;
				fg->monitor_soc_flag = true;
			} else {
				if (soc_delt < tem_soc_delt - 1) {
					strategy_fg_reset_co_to_default(fg);
					fg->fg_lock_flag = true;
					fg->monitor_soc_flag = false;
				} else {
					fg->fg_lock_flag = false;
				}
			}
		} else if (fg->fg_lock_flag) {
			if (tem_soc_delt <= 4)
				fg->fg_lock_flag = false;
		} else {
			fg->monitor_soc_flag = false;
		}
	}

	ret |= platform_fg_ops_get_fc(FG_IC_MASTER, &fc_master);
	ret |= platform_fg_ops_get_fc(FG_IC_SLAVE, &fc_slave);
#line 1328
	mca_log_err("fc_master:%d, fc_slave:%d", fc_master, fc_slave);
	if (!fg->fg1_batt_ctr_enabled && !fg->fg2_batt_ctr_enabled) {
		if (fg->lossless_recharge) {
			if (fg->dual_force_full[FG_IC_MASTER]) {
				ret |= platform_fg_ops_set_co(FG_IC_MASTER, true);
				fg->fg1_batt_ctr_enabled = true;
				fg->first_termination = false;
#line 1335
				mca_log_err("FG_MASTER lossless_recharge_full is 1");
			} else if (fg->dual_force_full[FG_IC_SLAVE]) {
				ret |= platform_fg_ops_set_co(FG_IC_SLAVE, true);
				fg->fg2_batt_ctr_enabled = true;
				fg->first_termination = false;
#line 1340
				mca_log_err("FG_SLAVE lossless_recharge_full is 1");
			}
		} else {
			if (fc_master == true && !fg->fg_lock_flag) {
				ret |= platform_fg_ops_set_co(FG_IC_MASTER, true);
				fg->fg1_batt_ctr_enabled = true;
				fg->first_termination = true;
#line 1347
				mca_log_err("FG_MASTER FC is 1");
			} else if (fc_slave == true && !fg->fg_lock_flag) {
				ret |= platform_fg_ops_set_co(FG_IC_SLAVE, true);
				fg->fg2_batt_ctr_enabled = true;
				fg->first_termination = true;
#line 1352
				mca_log_err("FG_SLAVE FC is 1");
			}
		}
	}

	return ret;
}

#line 1370
static int strategy_fg_get_term_current_new(struct strategy_fg *fg, int fg_index)
{
	static int last_temp_index[FG_IC_MAX] = {-1, -1};
	struct term_curr_para *term_curr_data = &fg->cfg.term_curr_data[fg_index];
	int temp = (fg_index ? fg->slave_batt_info.temp : fg->master_batt_info.temp) / 10;
	int temp_index, i;
	bool temp_offset_flag = false;

	for (i = term_curr_data->iterm_para_size - 1; i >= 0; i--) {
		if (term_curr_data->iterm_data[i].batt_temp <= temp)
			break;
	}
	temp_index = i < 0 ? 0 : i;
#line 1382
	mca_log_info("fg[%d] temp_index %d, last_index %d", fg_index, temp_index, last_temp_index[fg_index]);
	if (last_temp_index[fg_index] != -1 && temp_index < last_temp_index[fg_index]) {
		struct term_curr_data *last = &term_curr_data->iterm_data[last_temp_index[fg_index]];

		if (temp >= last->batt_temp - last->batt_temp_offset) {
			temp_offset_flag = true;
			temp_index = last_temp_index[fg_index];
		}
	}
	last_temp_index[fg_index] = temp_index;
	fg->temp_offset_flags[fg_index] = temp_offset_flag;
#line 1392
	mca_log_info("temp_offset_flag[%d]: %d", fg_index, temp_offset_flag);

	if (fg->fast_charge)
		return term_curr_data->iterm_data[temp_index].ffc_iterm;
	return term_curr_data->iterm_data[temp_index].normal_iterm;
}

#line 1430
static int strategy_fg_check_parallel_termination(struct strategy_fg *fg)
{
	static bool last_first_termination;
	int ret = 0;
	int vterm = 0;
	int vterm_target = 0;
	int final_term_curr = 0;
	int dual_term_curr;
	static int last_term[FG_IC_MAX] = {187, 61};
	bool fc_master = false;
	int i;

	vterm = mca_get_effective_result(fg->vterm_voter);
	if (fg->power_present && !fg->charging_done && vterm >= STRATEGY_FG_DEFAULT_VTERM) {
		/* the first termination */
		ret = mca_strategy_parallel_first_termination(fg);
	}
	/* adjust vterm after first_termination */
	if (fg->cfg.support_base_flip) {
		if (last_first_termination != fg->first_termination) {
			last_first_termination = fg->first_termination;
			if (last_first_termination == true) {
				int add = 0;

				if (fg->fg1_batt_ctr_enabled) {
					if (!fg->wls_online)
						add = fg->slave_batt_info.temp > 399 ? 20 : 0;
				} else if (fg->fg2_batt_ctr_enabled) {
					if (fg->wls_online) {
						if (fg->master_batt_info.temp < 300)
							add = 20;
						else
							add = fg->master_batt_info.temp > 399 ? 60 : 30;
					} else {
						add = fg->master_batt_info.temp > 399 ? 65 : 50;
					}
				}
				if (!fg->fast_charge)
					add = 0;
				vterm_target = vterm + add;
#line 1444
				mca_log_err("last_first_termination:vterm_target:%d", vterm_target);
				if (vterm_target != vterm || fg->base_batt_close)
					mca_vote_override(fg->vterm_voter, "para_term", true, vterm_target);
			} else {
#line 1448
				mca_log_err("last_first_termination: clean para_term");
				mca_vote_override(fg->vterm_voter, "para_term", false, 0);
			}
		}
	}

	/* setting dual battery term current */
	for (i = 0; i < FG_IC_MAX; i++) {
		fg->dual_iterm[i] = strategy_fg_get_term_current_new(fg, i);
#line 1457
		mca_log_err("fg_index: %d, last_term: %d, dual_iterm :%d\n", i, last_term[i], fg->dual_iterm[i]);
		if (last_term[i] != fg->dual_iterm[i])
			last_term[i] = fg->dual_iterm[i];
	}

	if (fg->fg2_batt_ctr_enabled) {
		final_term_curr = fg->fg1_batt_ctr_enabled ? 0 : last_term[FG_IC_MASTER];
		dual_term_curr = final_term_curr;
	} else if (fg->fg1_batt_ctr_enabled) {
		final_term_curr = last_term[FG_IC_SLAVE];
		dual_term_curr = final_term_curr;
	} else {
		dual_term_curr = fg->dual_iterm[FG_IC_MASTER] + fg->dual_iterm[FG_IC_SLAVE];
		final_term_curr = last_term[FG_IC_SLAVE];
		if (final_term_curr == 0)
			final_term_curr = last_term[FG_IC_MASTER];
	}
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, STRATEGY_EVENT_DUAL_TERM_CURR, dual_term_curr);
	mca_strategy_func_process(STRATEGY_FUNC_TYPE_BASIC_WIRELESS, STRATEGY_EVENT_DUAL_TERM_CURR, dual_term_curr);

	if (fg->cfg.support_base_flip) {
		ret |= platform_fg_ops_get_fc(FG_IC_MASTER, &fc_master);
		if (fg->fast_charge && fg->batt_temperature > 400 && fg->fg2_batt_ctr_enabled && !fc_master)
			mca_vote(fg->iterm_voter, "high_temp_para", true, 200);
		else
			mca_vote(fg->iterm_voter, "high_temp_para", false, 0);
	}

#line 1487
	mca_log_info("final_term_curr:%d", final_term_curr);
	if (final_term_curr != fg->last_final_term_curr) {
		mca_vote(fg->iterm_voter, "para_term", true, final_term_curr);
		fg->last_final_term_curr = final_term_curr;
	}

	mca_strategy_check_sigle_termination(fg);
	return 0;
}

static int mca_strategy_check_termination(struct strategy_fg *fg)
{
	int ret = 0;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		ret = mca_strategy_check_sigle_termination(fg);
		break;
	case MCA_FG_TYPE_PARALLEL:
		ret = strategy_fg_check_parallel_termination(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

#line 1550
static void mca_strategy_single_force_fw_report_full(struct strategy_fg *fg)
{
	int force_iterm, force_vterm;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;
	static int force_full_count;
	static time64_t start_force_full_time;
	int quick_charge_jeita_iterm = 0;
	int quick_charge_status = MCA_QUICK_CHG_STS_NO_CHARGING;
	int count_threshold;
	int online;
	time64_t cur_time;

	if (!fg->power_present || fg->batt_rsoc < 90 || !(fg->fast_charge || fg->support_normal_force_full))
		goto reset;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
		STRATEGY_STATUS_TYPE_ONLINE, &online);
	if (online) {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
			STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	} else {
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	}

	if (fg->voter_ok) {
		iterm = mca_get_effective_result(fg->iterm_voter);
		vterm = mca_get_effective_result(fg->vterm_voter);
	}

	if (vterm <= 4200) {
		if (fg->charging_done) {
			force_full_count = 0;
			fg->start_force_full = false;
			fg->done_force_full = false;
			start_force_full_time = 0;
		}
#line 1565
		mca_log_info("vterm=%d, return for vetrm abormal", vterm);
		return;
	}

	if (!fg->cfg.terminated_by_cp) {
		if (quick_charge_status == MCA_QUICK_CHG_STS_CHARGING)
			return;
		if (fg->fast_charge) {
			force_iterm = iterm * (iterm > 999 ? 115 : 120) / 100;
			force_vterm = vterm - 15;
		} else {
			force_iterm = iterm + fg->cfg.normal_force_full_hys;
			force_vterm = vterm - 15;
		}
	} else {
		if (fg->fast_charge && quick_charge_status == MCA_QUICK_CHG_STS_CHARGING) {
#line 1580
			mca_log_info("wire quick charge force full state");
			if (online) {
				mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_WIRELESS,
					STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM, &quick_charge_jeita_iterm);
			} else {
				mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE,
					STRATEGY_STATUS_TYPE_JEITA_FFC_ITERM, &quick_charge_jeita_iterm);
			}
			force_iterm = max(quick_charge_jeita_iterm * (quick_charge_jeita_iterm > 3000 ? 13 : 24) / 100, 500) +
				quick_charge_jeita_iterm;
			force_vterm = vterm - fg->cfg.fast_force_full_vol_delta;
		} else if (fg->fast_charge) {
#line 1596
			mca_log_info("PMIC FFC force full state");
			force_iterm = iterm * (iterm > 3000 ? 113 : 124) / 100;
			force_vterm = vterm - fg->cfg.fast_force_full_vol_delta;
		} else {
#line 1604
			mca_log_info("wire normal charge force full state");
			force_iterm = iterm + 120;
			force_vterm = vterm - 15;
		}
	}

	count_threshold = fg->fast_charge ? 3 : 2;
#line 1613
	mca_log_info("terminated_by_cp = %d, force_iterm = %d, force_vterm = %d, start_force_full = %d",
		fg->cfg.terminated_by_cp, force_iterm, force_vterm, fg->start_force_full);
#line 1614
	mca_log_info("count_threshold = %d, fg->fast_charge = %d", count_threshold, fg->fast_charge);

	if (fg->charging_done)
		goto reset;

	if (-fg->batt_current / 1000 < force_iterm && fg->batt_voltage > force_vterm && !fg->start_force_full) {
		force_full_count++;
#line 1619
		mca_log_info("force_full_count = %d", force_full_count);
		if (force_full_count >= count_threshold) {
			platform_fg_ops_set_force_report_full(FG_IC_MASTER);
			force_full_count = 0;
			fg->start_force_full = true;
			start_force_full_time = ktime_get_boottime_seconds();
#line 1625
			mca_log_info("force fw report full start_force_full_time = %d", start_force_full_time);
			schedule_delayed_work(&fg->force_report_full_work, msecs_to_jiffies(8000));
		}
	} else {
		force_full_count = 0;
		if (start_force_full_time) {
			cur_time = ktime_get_boottime_seconds() - start_force_full_time;
#line 1632
			mca_log_info("force fw report full cur_time = %d", cur_time);
			if (cur_time >= 7) {
				start_force_full_time = 0;
				fg->done_force_full = true;
			}
		}
	}
	return;

reset:
	force_full_count = 0;
	fg->start_force_full = false;
	fg->done_force_full = false;
	start_force_full_time = 0;
}

#line 1760
static void strategy_fg_check_parallel_force_fw_report_full(struct strategy_fg *fg)
{
	int soh_diff, cycle_diff;

	if (fg->master_batt_info.cycle_count % 50 == 0 || fg->slave_batt_info.cycle_count % 50 == 0) {
		soh_diff = fg->master_batt_info.soh - fg->slave_batt_info.soh;
		cycle_diff = fg->master_batt_info.cycle_count - fg->slave_batt_info.cycle_count;

		if (abs(soh_diff) >= 5) {
			if (soh_diff < 0)
				fg->self_equal_flag[0] = true;
			else
				fg->self_equal_flag[1] = true;
		} else if (abs(cycle_diff) >= 10) {
			if (cycle_diff > 0)
				fg->self_equal_flag[0] = true;
			else
				fg->self_equal_flag[1] = true;
		}
#line 1775
		mca_log_info("soh_diff: %d, cycle_diff: %d", soh_diff, cycle_diff);
	}

	mca_strategy_parallel_force_fw_report_full(fg, FG_IC_MASTER);
	mca_strategy_parallel_force_fw_report_full(fg, FG_IC_SLAVE);
}

static int mca_strategy_force_fw_report_full(struct strategy_fg *fg)
{
	int ret = 0;

	if (!fg || !fg->fg_init_flag)
		return -1;

	switch (fg->cfg.fg_type) {
	case MCA_FG_TYPE_SINGLE:
		mca_strategy_single_force_fw_report_full(fg);
		break;
	case MCA_FG_TYPE_PARALLEL:
		strategy_fg_check_parallel_force_fw_report_full(fg);
		break;
	case MCA_FG_TYPE_SERIES:
		break;
	default:
		return -1;
	}

	return ret;
}

#define EU_RECHARGER_SOC 95
#line 1815
static int mca_strategy_check_recharge(struct strategy_fg *fg)
{
	if (!fg || !fg->power_present || !fg->charging_done || fg->recharging)
		return 0;

	if (!fg->is_eu_model) {
		if (fg->batt_raw_soc <= fg->cfg.report_full_raw_soc) {
			mca_strategy_start_recharging(fg);
#line 1826
			mca_log_err("ok to recharging, raw_soc: %d\n", fg->batt_raw_soc);
		}
	} else {
		if (fg->batt_ui_soc < EU_RECHARGER_SOC) {
			mca_strategy_start_recharging(fg);
#line 1831
			mca_log_err("eu start recharging ui_soc: %d rsoc:%d\n", fg->batt_ui_soc, fg->batt_rsoc);
		}
	}

	return 0;
}

#line 1895
static void mca_strategy_check_fl4p0_status(struct strategy_fg *fg)
{
	static int count;
	int real_supplement_energy;
	int iterm = STRATEGY_FG_DEFAULT_ITERM;
	int vterm = STRATEGY_FG_DEFAULT_VTERM;

	if (fg->fast_charge)
		mca_vote(fg->iterm_voter, "calibration", true, mca_strategy_get_ffc_safe_item(fg));
	else
		mca_vote(fg->iterm_voter, "calibration", false, 0);

	if (!fg || fg->charging_done || fg->recharging || !fg->voter_ok || !fg->fast_charge || fg->ffc_continue_charge)
		return;

	iterm = mca_strategy_get_fl4p0_iterm(fg);
	vterm = mca_get_effective_result(fg->vterm_voter);

	if (-fg->batt_current / 1000 < iterm && vterm >= STRATEGY_FG_DEFAULT_VTERM &&
	    (fg->batt_voltage >= vterm - 11) && !fg->ffc_continue_charge) {
		if (count++ >= 2) {
			count = 0;
			fg->ffc_continue_charge = true;
		}
#line 1910
		mca_log_info("vterm: %d, iterm: %d, vbat: %d, ibat: %d", vterm, iterm, fg->batt_voltage, fg->batt_current);
	} else {
		count = 0;
	}

	if (fg->ffc_continue_charge) {
		fg->calibration_temp = fg->batt_temperature;
		platform_fg_ops_get_real_supplement_energy(FG_IC_MASTER, &real_supplement_energy);
		if (real_supplement_energy == 0xFFFF) {
#line 1919
			mca_log_info("ok to calibration iterm");
			strategy_class_fg_ops_set_charging_done(true);
		} else {
#line 1922
			mca_log_info("continue calibration");
			schedule_delayed_work(&fg->fl4p0_calibration_work, 0);
		}
	}
}

#define EU_LOSSES_START_CYCLECOUNT 300
#line 1940
static int mca_strategy_check_lossless_recharge(struct strategy_fg *fg)
{
	static ktime_t last_time;
	ktime_t time_now = ktime_get();
	ktime_t time_diff = 0;
	int batt_temp = 0;

	if (!fg->charging_done) {
		last_time = time_now;
		return 0;
	}

	if (fg->is_eu_model) {
		if (fg->batt_cyclecount <= EU_LOSSES_START_CYCLECOUNT) {
			fg->lossless_recharge = false;
#line 1944
			mca_log_info("eu model CycleCount Too Low Disable\n");
			return 0;
		}
	}

	if (!fg->input_suppend_voter)
		fg->input_suppend_voter = mca_find_votable("input_suspend");
	if (fg->input_suppend_voter && mca_get_effective_result(fg->input_suppend_voter)) {
#line 1952
		mca_log_info("set input_suspend, skip check_lossless_recharge \n");
		return 0;
	}

	time_diff = ktime_to_ms(ktime_sub(time_now, last_time));
	time_diff /= 1000;
#line 1958
	mca_log_info("time_now: %ld, last_time: %ld, time_diff: %ld\n", time_now, last_time, time_diff);

	(void)strategy_class_fg_ops_get_temperature(&batt_temp);
	batt_temp /= 10;

	if (fg->charging_done && fg->fast_charge &&
	    batt_temp >= fg->cfg.rechg_cfg.temp_low && batt_temp <= fg->cfg.rechg_cfg.temp_high &&
	    time_diff >= fg->cfg.rechg_cfg.rest_time) {
		fg->lossless_recharge = true;
		if (fg->cfg.support_base_flip || fg->cfg.base_flip_same) {
			mca_vote(fg->charge_limit_voter, "losslessRec", true, 600);
			mca_vote(fg->flip_fcc_voter, "losslessRec", true, 100);
		} else if (fg->cfg.support_lossless_curr_limit) {
			mca_vote(fg->charge_limit_voter, "losslessRec", true, fg->cfg.lossless_curr_limit);
		}
	}
#line 1975
	mca_log_info("charging_done: %d, fast_charge: %d, temp: %d, lossless_recharge: %d\n",
			fg->charging_done, fg->fast_charge, batt_temp, fg->lossless_recharge);

	if (fg->lossless_recharge) {
		mca_strategy_start_recharging(fg);
#line 1979
		mca_log_err("ok to lossless_recharge\n");
	}

	return 0;
}

#define MCA_EVENT_ONEWIRE_AUTH_PASS MCA_EVENT_BATT_AUTH_PASS

#line 2465
static int mca_strategy_check_onewire_batt_auth(struct strategy_fg *fg)
{
	if (fg->batt_auth)
		return 0;

	platform_fg_ops_set_authentic(FG_IC_MASTER, 1);
	fg->batt_auth = 1;
	mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_BATT_AUTH_PASS, NULL);
#line 2475
	mca_log_err("onewire batt auth pass\n");
	return 0;
}

#line 3880
static unsigned int charge_get_batt_rm_threshold(void)
{
	static int device_power_max;
	int rm_thr;

	if (!device_power_max)
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE, STRATEGY_STATUS_TYPE_DEVICE_POWER_MAX, &device_power_max);

	if (device_power_max > 67000)
		rm_thr = 420;
	else
		rm_thr = 200;
	if (device_power_max > 100000)
		rm_thr = 500;
#line 3889
	mca_log_info("device_power_max:%d, batt_rm_thr:%d\n", device_power_max, rm_thr);
	return rm_thr;
}

#line 3900
static void charge_slowly_monitor_func(struct strategy_fg *fg)
{
	static int now_rm, now_soc;
	static time64_t nowtime, nowplugintime;
	int power_max = 0;
	int param[5] = {0};
	int rm_thr;

#line 3905
	mca_log_err("realtype:%d, soc:%d, rm:%d\n", fg->real_type, fg->batt_ui_soc, now_rm);
#line 3906
	mca_log_err("time:%d, timeplugin:%d plugin:%d\n", nowtime, nowplugintime, fg->online_time);
	nowplugintime = ktime_get_boottime_seconds();

	if (fg->real_type == XM_CHARGER_TYPE_PD_VERIFY) {
		if (fg->batt_ui_soc <= 80) {
			fg->online_time = ktime_get_boottime_seconds();
			protocol_class_get_adapter_max_power(ADAPTER_PROTOCOL_PPS, &power_max);
			rm_thr = charge_get_batt_rm_threshold();
#line 3926
			mca_log_err("powermax:%d designcapacity:%d curr_batt_rm_thr:%d\n", power_max, fg->batt_dc, rm_thr);
			if (power_max >= 33 && fg->batt_dc >= rm_thr * 1000) {
				if (!fg->xmpps_initialtime) {
					fg->xmpps_initialtime = ktime_get_boottime_seconds();
					fg->initial_rm = fg->batt_rm / 1000;
					fg->initial_soc = fg->batt_ui_soc;
				}
				nowtime = ktime_get_boottime_seconds();
				now_rm = fg->batt_rm / 1000;
				now_soc = fg->batt_ui_soc;
				if (nowtime - fg->xmpps_initialtime >= 600) {
					if (now_soc - fg->initial_soc < rm_thr * 100 / (fg->batt_dc / 1000 - 100)) {
#line 3940
						mca_log_err("ntime:%d, initialtime:%d\n", nowtime, fg->xmpps_initialtime);
#line 3941
						mca_log_err("nrm:%d, initialrm:%d\n", now_rm, fg->initial_rm);
#line 3942
						mca_log_err("report dfx\n");
						param[0] = power_max;
						param[1] = fg->initial_rm;
						param[2] = now_rm;
						param[3] = fg->initial_soc;
						param[4] = now_soc;
						mca_charge_mievent_report(CHARGE_DFX_XMPPS_CHARGE_SLOW, param, 5);
					}
					fg->initial_rm = now_rm;
					fg->initial_soc = now_soc;
					fg->xmpps_initialtime = nowtime;
				}
			}
			return;
		}
	} else if (fg->real_type >= 1 && fg->real_type <= 11 && !fg->nonxmpps_flag &&
		   nowplugintime - fg->online_time >= 61) {
		param[0] = fg->real_type;
		mca_charge_mievent_report(CHARGE_DFX_NONXMPPS_CHARGE, param, 1);
		fg->nonxmpps_flag = true;
#line 3914
		mca_log_err("report dfx\n");
	}

	fg->xmpps_initialtime = 0;
	nowtime = 0;
	now_rm = 0;
	nowplugintime = 0;
}

static void strategy_fg_adjust_interval(struct strategy_fg *fg)
{
	if (fg->update_period != -1) {
		fg->update_interval = fg->update_period;
		return;
	}

	if (fg->near_vterm) {
		fg->update_interval = MCA_FG_UPDATE_INTERVAL_FAST;
	} else if (fg->chg_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (fg->fast_charge)
			fg->update_interval = MCA_FG_UPDATE_INTERVAL_FAST;
		else
			fg->update_interval = 5000;
	} else {
		fg->update_interval = fg->screen_status ? 10000 : 30000;
		if (fg->batt_voltage_mean < fg->vcutoff_shutdown_delay + 200)
			fg->update_interval = fg->batt_temperature < 0 ? 2000 : 5000;
		else if (fg->batt_temperature < 0)
			fg->update_interval = 5000;
	}

	if (fg->batt_voltage < fg->vcutoff_sw + 50)
		fg->update_interval = MCA_FG_UPDATE_INTERVAL_FAST;
}

static void strategy_fg_qbg_send_chg_data(struct strategy_fg *fg)
{
	int final_fcc = 0;
	int quick_charge_status = 0;

	mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE, STRATEGY_STATUS_TYPE_CHARGING, &quick_charge_status);
	if (quick_charge_status)
		mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_QUICK_CHARGE, STRATEGY_STATUS_TYPE_FFC_FCC, &final_fcc);
	else if (fg->charge_limit_voter)
		final_fcc = mca_get_effective_result(fg->charge_limit_voter);
	platform_fg_ops_qbg_send_chg_data(FG_IC_MASTER, fg->chg_status, final_fcc);
}

#line 2480
static void strategy_fg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, monitor_work.work);
	const struct mca_hwid *hwid = mca_get_hwid_info();
	int ret;
	bool prohibit_jump = fg->fg_init_flag;
	int flag;
	bool master_ok = false, slave_ok = false;
	int param[3] = {0};
	time64_t cur_time;

	if (!fg->fg_init_flag) {
		flag = platform_fg_ops_probe_ok(FG_IC_MASTER, &master_ok);
		fg->fg_master_init_flag = master_ok;
#line 2519
		mca_log_err("master flag: %d\n", flag);
		if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
			flag |= platform_fg_ops_probe_ok(FG_IC_SLAVE, &slave_ok);
			fg->fg_slave_init_flag = slave_ok;
		}
#line 2524
		mca_log_err("master and slave flag: %d\n", flag);
		if (flag) {
			fg_check_batt_auth(fg);
			mca_queue_delayed_work(&fg->monitor_work, msecs_to_jiffies(1000));
			return;
		}
		prohibit_jump = false;
		strategy_fg_update_batt_info(fg);
		if (fg->cfg.fg_type != MCA_FG_TYPE_SINGLE)
			fg->fg_init_flag = master_ok && slave_ok;
		else
			fg->fg_init_flag = master_ok;
		if (fg->cfg.support_base_flip || fg->cfg.base_flip_same)
			schedule_delayed_work(&fg->reset_default_co_work, 0);
		if (hwid && hwid->country_version == CountryCN && fg->fg_init_flag && fg->cfg.support_fl4p0)
			platform_fg_ops_fl4p0_enable_check(FG_IC_MASTER);
	}

	if (fg->resume_update_soc) {
		prohibit_jump = false;
		fg->resume_update_soc = false;
	}

	if (fg->boot_update_soc) {
		fg->boot_update_soc = false;
#line 2551
		mca_log_info("boot_update_soc update is_eu_model done\n");
		prohibit_jump = false;
	}

	if (!fg->voter_ok)
		strategy_fg_init_voter(fg);

	ret = fg_update_status(fg, prohibit_jump);
	if (fg->cfg.support_qbg) {
		mca_strategy_check_onewire_batt_auth(fg);
		strategy_fg_qbg_send_chg_data(fg);
	}

	if (fg->charging_done_time) {
		cur_time = ktime_get_boottime_seconds() - fg->charging_done_time;
		if (cur_time >= 100) {
			if ((fg->batt_ui_soc != 100 || fg->is_eu_model) && fg->batt_rsoc != 100) {
				param[0] = fg->batt_voltage;
				param[1] = fg->batt_ui_soc;
				param[2] = fg->batt_rsoc;
				mca_charge_mievent_report(CHARGE_DFX_SOC_NOT_FULL, param, 3);
			}
			fg->charging_done_time = 0;
#line 2575
			mca_log_info("check report dfx soc_not_full curr_time=%d\n", cur_time);
		}
	}

	mca_strategy_check_termination(fg);
	mca_strategy_force_fw_report_full(fg);
	mca_strategy_check_recharge(fg);
	if (hwid && hwid->country_version == CountryCN && fg->cfg.support_fl4p0)
		mca_strategy_check_fl4p0_status(fg);
	if (fg->cfg.support_lossless_rechg)
		mca_strategy_check_lossless_recharge(fg);
	fg_check_batt_auth(fg);
	charge_slowly_monitor_func(fg);

	strategy_fg_adjust_interval(fg);
	mca_queue_delayed_work(&fg->monitor_work, msecs_to_jiffies(fg->update_interval));
}

#line 3640
static int strategy_fg_process_event(int event, int value, void *data)
{
	struct strategy_fg *info = data;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	int ret = 0;

	if (!data) {
		ret = -1;
		goto out;
	}

	unsigned int ota_flag = 0;

#line 3659
	mca_log_info("receive event %d, value %d", event, value);
	switch (event) {
	case MCA_EVENT_DEBUG_DOUBLE85:
	case MCA_EVENT_DEBUG_REMOVE_TEMP_LIMIT:
		if (value) {
			strategy_fg_set_fake_soc(info, 55);
			strategy_fg_set_fake_temp(info, 500);
		} else {
			strategy_fg_set_fake_soc(info, STRATEGY_FG_FAKE_SOC_NONE);
			strategy_fg_set_fake_temp(info, STRATEGY_FG_FAKE_TEMP_NONE);
		}
		break;
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_WIRELESS_DISCONNECT:
		if (event == MCA_EVENT_USB_DISCONNECT) {
			info->usb_online = false;
		} else {
			bool usb_online = info->usb_online;

			info->wls_online = false;
			if (usb_online) {
#line 3693
				mca_log_info("wireless disocnnect usb present goto end\n");
				break;
			}
		}
		if (!info->is_eu_model) {
			mca_vote(info->en_voter, "term_recharge", false, 0);
			info->charging_done = false;
		} else if (info->batt_ui_soc < 100) {
			mca_vote(info->en_voter, "term_recharge", false, 0);
			info->charging_done = false;
#line 3706
			mca_log_info("usb disonnect clear charging_done\n");
		}
		mca_vote(info->iterm_voter, "calibration", false, 0);
		mca_vote(info->charge_limit_voter, "losslessRec", false, 0);
		info->recharging = false;
		info->lossless_recharge = false;
		info->power_present = false;
		info->en_smooth_full = false;
		info->charging_done_time = 0;
		info->near_vterm = false;
		info->last_final_term_curr = 0;
#line 3718
		mca_log_info("plugout keep_full_flag = %d\n", info->keep_full_flag);
		if (info->keep_full_flag)
			schedule_delayed_work(&info->delay_reset_full_flag_work, msecs_to_jiffies(60 * 1000));
		if (info->cfg.support_dtpt)
			cancel_delayed_work_sync(&info->dtpt_monitor_work);
		if (info->cfg.fg_type != MCA_FG_TYPE_SINGLE) {
			cancel_delayed_work_sync(&info->reset_default_co_work);
			schedule_delayed_work(&info->reset_default_co_work, 0);
			info->monitor_soc_flag = false;
			info->first_termination = false;
			info->fg1_batt_ctr_enabled = false;
			info->fg2_batt_ctr_enabled = false;
			info->fg_lock_flag = false;
			info->base_batt_close = false;
			mca_vote_override(info->vterm_voter, "para_term", false, 0);
			mca_vote(info->charge_limit_voter, "losslessRec", false, 0);
			mca_vote(info->flip_fcc_voter, "losslessRec", false, 0);
			mca_vote(info->flip_fcc_voter, "base_close_curr", false, 0);
		}
		if (info->cfg.support_nvt1000_ota) {
			platform_fg_ops_get_ota_update_flag(FG_IC_MASTER, &ota_flag);
			if (ota_flag == 0xaa5555aa) {
				cancel_delayed_work_sync(&info->nvt1000_ota_monitor_work);
				schedule_delayed_work(&info->nvt1000_ota_monitor_work, 5000);
			}
		}
		info->xmpps_initialtime = 0;
		info->nonxmpps_flag = false;
		break;
	case MCA_EVENT_USB_CONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		if (event == MCA_EVENT_USB_CONNECT)
			info->usb_online = true;
		else
			info->wls_online = true;
		info->power_present = true;
		info->plugin_time = ktime_get_boottime_seconds();
		info->online_time = ktime_get_boottime_seconds();
		if (info->is_eu_model) {
			if (info->batt_ui_soc < 100) {
				mca_vote(info->en_voter, "term_recharge", false, 0);
				info->charging_done = false;
#line 3674
				mca_log_info("usb connect clear charging_done\n");
			}
		}
#line 3678
		mca_log_info("plugin keep_full_flag: %d, plugin_time: %lld\n", info->keep_full_flag, info->plugin_time);
		mca_mod_delayed_work(&info->monitor_work, msecs_to_jiffies(1000));
		cancel_delayed_work_sync(&info->delay_reset_full_flag_work);
		if (info->cfg.support_dtpt)
			schedule_delayed_work(&info->dtpt_monitor_work, msecs_to_jiffies(5000));
		if (info->cfg.support_nvt1000_ota)
			cancel_delayed_work_sync(&info->nvt1000_ota_monitor_work);
		break;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		info->real_type = value;
		break;
	case MCA_EVENT_IS_EU_MODEL:
		if (hwid && hwid->country_version == CountryCN) {
#line 3744
			mca_log_info("country_version = CN\n");
			break;
		}
		if (info->is_eu_model != value) {
			info->is_eu_model = value ? true : false;
#line 3750
			mca_log_info("is_eu_model[%d] update soc\n", info->is_eu_model);
		}
		break;
	case MCA_EVENT_FG_ERROR:
		info->fg_error = 1;
		fg_update_status(info, true);
		break;
	case MCA_EVENT_DEBUG_MEMORY_TEST:
		if (value)
			strategy_fg_set_fake_temp(info, 250);
		else
			strategy_fg_set_fake_temp(info, STRATEGY_FG_FAKE_TEMP_NONE);
		break;
	default:
		break;
	}

out:
	return ret;
}

static ssize_t strategy_fg_sysfs_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	struct mca_sysfs_attr_info *attr_info;
	struct strategy_fg *info = dev_get_drvdata(dev);
	int val = 0;
	int temp = 0;
	unsigned char date[64] = {0};
	unsigned long rvalue;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_fg_sysfs_field_tbl, FG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_PROP_MAIN_AUTH:
		platform_fg_ops_get_authentic(FG_IC_MASTER, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_SLAVE_AUTH:
		platform_fg_ops_get_authentic(FG_IC_SLAVE, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_BATT_INDEX:
		count = scnprintf(buf, PAGE_SIZE, "%x\n", info->batt_index);
		break;
	case FG_PROP_VERIFY_DIGEST:
		count = platform_fg_ops_get_verify_digest(info->batt_index, (char *)buf);
		break;
	case FG_PROP_BATT_POWER_MATCH:
		strategy_fg_check_battery_adapt_power(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%x\n", val);
		break;
	case FG_PROP_FAST_CHARGE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fast_charge);
		break;
	case FG_PROP_SOC_DECIMAL:
		strategy_fg_ops_get_soc_decimal(info, &val, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_SOC_DECIMAL_RATE:
		strategy_fg_ops_get_soc_decimal(info, &val, &temp);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", temp);
		break;
	case FG_PROP_FAKE_SOC:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fake_soc);
		break;
	case FG_PROP_FAKE_TEMP:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->fake_temp);
		break;
	case FG_PROP_BATTERY_NUM:
		if (info->cfg.fg_type == MCA_FG_TYPE_PARALLEL)
			count = scnprintf(buf, PAGE_SIZE, "%d\n", info->cfg.fg_type);
		else
			count = scnprintf(buf, PAGE_SIZE, "%d\n", 0);
		break;
	case FG_PROP_UPDATE_PERIOD:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->update_period);
		break;
	case FG_PROP_PACK_VOLTAGE:
		platform_class_buckchg_ops_get_pack_vbat(MAIN_BUCK_CHARGER, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_DOD_COUNT:
		strategy_fg_get_dod_count(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_ENABLE_ROLLBACK:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->enable_rollback);
		break;
	case FG_PROP_SOH_NEW:
		strategy_fg_get_soh_new(info, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_MASTER_SELF_EQUAL_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->self_equal_count[FG_IC_MASTER]);
		break;
	case FG_PROP_SLAVE_SELF_EQUAL_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->self_equal_count[FG_IC_SLAVE]);
		break;
	case FG_PROP_SELF_EQUAL_MAX_COUNT:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->self_equal_count_max);
		break;
	case FG_PROP_PACK_TBAT:
		platform_class_buckchg_ops_get_pack_tbat(MAIN_BUCK_CHARGER, &val);
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case FG_PROP_RAW_SOC:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->batt_raw_soc);
		break;
	case FG_PROP_CALC_RVALUE:
		rvalue = strategy_fg_get_calc_rvalue(info);
		count = scnprintf(buf, PAGE_SIZE, "%lu\n", rvalue);
		break;
	case FG_PROP_MANUFACTURING_DATE:
		mca_strategy_get_manufacturing_date(info, date);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", date);
		break;
	case FG_PROP_FIRST_USAGE_DATE:
		mca_strategy_get_first_usage_date(info, date);
		count = scnprintf(buf, PAGE_SIZE, "%s\n", date);
		break;
	default:
		break;
	}

	return count;
}

#line 215
static ssize_t strategy_fg_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct strategy_fg *info = dev_get_drvdata(dev);
	struct mca_sysfs_attr_info *attr_info;
	int val;
	int match = 0;

	if (!info)
		return -1;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		strategy_fg_sysfs_field_tbl, FG_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case FG_PROP_MAIN_AUTH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_fg_ops_set_authentic(FG_IC_MASTER, val);
		info->batt_auth = val;
		strategy_fg_check_battery_adapt_power(info, &match);
		if (val && info->cfg.fg_type == MCA_FG_TYPE_SINGLE && match)
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_BATT_AUTH_PASS, NULL);
		break;
	case FG_PROP_SLAVE_AUTH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		platform_fg_ops_set_authentic(FG_IC_SLAVE, val);
		info->batt_slave_auth = val;
		strategy_fg_check_battery_adapt_power(info, &match);
		if (info->batt_auth && info->batt_slave_auth && match)
			mca_event_block_notify(MCA_EVENT_TYPE_HW_INFO, MCA_EVENT_BATT_AUTH_PASS, NULL);
		break;
	case FG_PROP_BATT_INDEX:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		info->batt_index = val;
		break;
	case FG_PROP_VERIFY_DIGEST:
		platform_fg_ops_set_verify_digest(info->batt_index, (char *)buf);
		break;
	case FG_PROP_BATT_POWER_MATCH:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		info->fake_bap_match = val;
#line 231
		mca_log_info("set fake_bap_match: %d\n", val);
		break;
	case FG_PROP_FAKE_SOC:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		strategy_fg_set_fake_soc(info, val);
		break;
	case FG_PROP_FAKE_TEMP:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
		strategy_fg_set_fake_temp(info, val);
		break;
	case FG_PROP_UPDATE_PERIOD:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
#line 246
		mca_log_err("set fg update period: %d\n", val);
		if (val != info->update_period) {
			info->update_period = val;
			cancel_delayed_work_sync(&info->monitor_work);
			mca_queue_delayed_work(&info->monitor_work, 0);

			mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
					MCA_EVENT_BATTERY_STS_CHANGE, NULL);
		}
		break;
	case FG_PROP_DOD_COUNT:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
#line 259
		mca_log_err("set fake_dod_count: %d\n", val);
		info->fake_dod_count = val;
		break;
	case FG_PROP_ENABLE_ROLLBACK:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
#line 265
		mca_log_err("set enable_rollback: %d\n", val);
		info->enable_rollback = !!val;
		break;
	case FG_PROP_SELF_EQUAL_MAX_COUNT:
		if (kstrtoint(buf, 10, &val))
			return -EINVAL;
#line 271
		mca_log_err("set self_equal_count_max: %d\n", val);
		info->self_equal_count_max = val;
		break;
	case FG_PROP_FIRST_USAGE_DATE:
		if (!info->fg_init_flag)
			break;
		{
			int fg_type = info->cfg.fg_type;

			switch (fg_type) {
			case MCA_FG_TYPE_PARALLEL:
				platform_fg_ops_set_first_usage_date(FG_IC_MASTER, buf);
				fallthrough;
			case MCA_FG_TYPE_SINGLE:
				platform_fg_ops_set_first_usage_date(fg_type, buf);
				break;
			default:
				break;
			}
		}
		break;
	default:
		break;
	}

	return count;
}

static struct strategy_fg_class_ops g_strategy_fg_ops = {
	.strategy_fg_is_init_ok = strategy_fg_ops_is_init_ok,
	.strategy_fg_is_chip_ok = strategy_fg_ops_is_chip_ok,
	.strategy_fg_dual_is_chip_ok = strategy_fg_dual_ops_is_chip_ok,
	.strategy_fg_get_rsoc = strategy_fg_ops_get_rsoc,
	.strategy_fg_get_soc = strategy_fg_ops_get_soc,
	.strategy_fg_get_temp = strategy_fg_ops_get_temp,
	.strategy_fg_get_thermal_temp = strategy_fg_ops_get_thermal_temp,
	.strategy_fg_get_current = strategy_fg_ops_get_curr,
	.strategy_fg_get_voltage = strategy_fg_ops_get_volt,
	.strategy_fg_get_cycle = strategy_fg_ops_get_cycle,
	.strategy_fg_get_rm = strategy_fg_ops_get_rm,
	.strategy_fg_get_voltage_mean = strategy_fg_get_volt_mean,
	.strategy_fg_get_soc_decimal_info = strategy_fg_ops_get_soc_decimal,
	.strategy_fg_get_charging_done = strategy_fg_ops_get_charging_done,
	.strategy_fg_set_charging_done = strategy_fg_ops_set_charging_done,
	.strategy_fg_get_model_name = strategy_fg_ops_get_model_name,
	.strategy_fg_set_fastcharge = strategy_fg_ops_set_fastcharge,
	.strategy_fg_get_fastcharge = strategy_fg_ops_get_fastcharge,
	.strategy_fg_get_authentic = strategy_fg_ops_get_authentic,
	.strategy_fg_get_dc = strategy_fg_ops_get_dc,
	.strategy_fg_get_fcc = strategy_fg_ops_get_fcc,
	.strategy_fg_get_health = strategy_fg_ops_get_health,
	.strategy_fg_get_soh = strategy_fg_ops_get_soh,
	.strategy_fg_get_first_termiation = strategy_fg_ops_get_first_termiation,
	.strategy_fg_get_pack_vendor_id = strategy_fg_ops_get_pack_vendor_id,
	.strategy_fg_get_temp_offset_flag = strategy_fg_ops_get_temp_offset_flag,
};

#line 3958
static int strategy_fg_probe(struct platform_device *pdev)
{
	struct strategy_fg *fg;
	int ret = 0;
	const struct mca_hwid *hwid = mca_get_hwid_info();

#line 3962
	mca_log_info("%s probe_begin\n", __func__);
	fg = devm_kzalloc(&pdev->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg) {
#line 3965
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	fg->dev = &pdev->dev;
	platform_set_drvdata(pdev, fg);
	fg->batt_ui_soc = 15;
	ret = strategy_fg_parse_dt(fg);
	if (ret)
		return ret;

	fg->batt_psy = power_supply_get_by_name("battery");
	if (hwid && hwid->country_version) {
		charger_partition_get_eu_model(&fg->is_eu_model);
		if (fg->is_eu_model)
			fg->boot_update_soc = true;
	}
#line 3982
	mca_log_info("probe is_eu_model[%d]\n", fg->is_eu_model);
	INIT_DELAYED_WORK(&fg->monitor_work, strategy_fg_monitor_workfunc);
	mca_queue_delayed_work(&fg->monitor_work, 0);
	ret = strategy_class_fg_ops_register(fg, &g_strategy_fg_ops);
	strategy_fg_sysfs_create_group(fg->dev);
	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_FG,
		strategy_fg_process_event, strategy_fg_get_status, NULL, fg);
	INIT_DELAYED_WORK(&fg->delay_reset_full_flag_work, delay_reset_full_flag_work);
	INIT_DELAYED_WORK(&fg->reset_default_co_work, strategy_fg_reset_default_work);
	strategy_fg_init_voter(fg);
	INIT_DELAYED_WORK(&fg->dtpt_monitor_work, strategy_fg_dtpt_monitor_work);
	INIT_DELAYED_WORK(&fg->fl4p0_calibration_work, strategy_fl4p0_calibration_work);
	INIT_DELAYED_WORK(&fg->force_report_full_work, strategy_force_report_full_work);
	INIT_DELAYED_WORK(&fg->nvt1000_ota_monitor_work, strategy_nvt1000_ota_monitor_work);
	INIT_DELAYED_WORK(&fg->batt_abnormal_dfx_work, strategy_batt_abnormal_dfx_work);
	fg->fake_bap_match = STRATEGY_FG_FAKE_BAP_MATCH_NONE;
	fg->fake_soc = STRATEGY_FG_FAKE_SOC_NONE;
	fg->fake_temp = STRATEGY_FG_FAKE_TEMP_NONE;
	fg->update_period = -1;
	fg->self_equal_count_max = 15;
	fg->batt_health = POWER_SUPPLY_HEALTH_GOOD;
	fg->last_final_term_curr = 0;
	fg->panel_nb.notifier_call = strategy_fg_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &fg->panel_nb);
	fg->thermal_board_nb.notifier_call = strategy_fg_thermal_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP, &fg->thermal_board_nb);

	if (fg->cfg.support_nvt1000_ota)
		schedule_delayed_work(&fg->nvt1000_ota_monitor_work, 5000);
	if (fg->cfg.support_batt_xvdd_dfx)
		schedule_delayed_work(&fg->batt_abnormal_dfx_work, 17500);

#line 4016
	mca_log_info("%s probe end\n", __func__);
	return ret;
}

static int strategy_fg_remove(struct platform_device *pdev)
{
	strategy_fg_sysfs_remove_group(&pdev->dev);
	return 0;
}

static int strategy_fg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct strategy_fg *fg = (struct strategy_fg *)platform_get_drvdata(pdev);
	ktime_t time_now = ktime_get_boottime();

	if (!fg)
		return -1;

	if (ktime_ms_delta(time_now, fg->suspend_time) > 3000)
		fg->suspend_time = time_now;

	cancel_delayed_work_sync(&fg->monitor_work);

	return 0;
}

static int strategy_fg_resume(struct platform_device *pdev)
{
	struct strategy_fg *fg = (struct strategy_fg *)platform_get_drvdata(pdev);
	int interval = 10000;

	if (!fg)
		return -1;

	if (ktime_ms_delta(ktime_get_boottime(), fg->suspend_time) > 3000) {
		interval = 0;
		fg->resume_update_soc = true;
	} else {
		fg->resume_update_soc = false;
	}

	mca_queue_delayed_work(&fg->monitor_work, msecs_to_jiffies(interval));
	return 0;
}

static void strategy_fg_shutdown(struct platform_device *pdev)
{
	struct strategy_fg *fg = (struct strategy_fg *)platform_get_drvdata(pdev);

#line 4065
	mca_log_err("battery shutdown, soc: %d, rsoc: %d, vbat: %d, vbat_mean: %d, empty: %d\n",
		fg->batt_ui_soc, fg->batt_rsoc, fg->batt_voltage, fg->batt_voltage_mean, fg->vbatt_empty);
}

static const struct of_device_id match_table[] = {
	{.compatible = "xiaomi,strategy_fg"},
	{},
};

static struct platform_driver strategy_fg_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "strategy_fg",
		.of_match_table = match_table,
	},
	.probe = strategy_fg_probe,
	.remove = strategy_fg_remove,
	.suspend = strategy_fg_suspend,
	.resume = strategy_fg_resume,
	.shutdown = strategy_fg_shutdown,
};

static int __init strategy_fg_init(void)
{
	return platform_driver_register(&strategy_fg_driver);
}
module_init(strategy_fg_init);

static void __exit strategy_fg_exit(void)
{
	platform_driver_unregister(&strategy_fg_driver);
}
module_exit(strategy_fg_exit);

MODULE_DESCRIPTION("Strategy Fuel Gauge");
MODULE_AUTHOR("liweiwei9@xiaomi.com");
MODULE_LICENSE("GPL v2");
