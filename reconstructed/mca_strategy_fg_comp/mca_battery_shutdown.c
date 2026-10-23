// SPDX-License-Identifier: GPL-2.0
/*
 * mca_battery_shutdown.c - battery cutoff voltage / shutdown delay handling of the Xiaomi MCA fuel gauge strategy.
 * Reconstructed from the stock mca_strategy_fg_comp.ko, XRING mca_battery_shutdown.c as template.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_hwid.h>
#include <mca/shared_memory/charger_partition_class.h>
#include "strategy_fg.h"

#define MCA_LOG_TAG "mca_battery_shutdown"

#define CountryCN 0

#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_FW 3050
#define	MCA_BATTERY_SHUTDOWN_VCUTTOFF_SHUTDOWN_DELAY 3050
#define	MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW 2900
#define MCA_BATTERY_SHUTDOWN_VCUTTOFF_MIN 2500
#define MCA_BATTERY_SHUTDOWN_VOLTAGE_HYST 50
#define MCA_HEAVY_LOAD_DISCHARGING_CURRENT 2000000
#define MCA_HEAVY_LOAD_DISCHARGING_COUNT 2

enum shutdown_strategy_type {
	SHUTDOWN_STRATEGY_BATTERY_HEALTH,
	SHUTDOWN_STRATEGY_BATTERY_SPEC,
	SHUTDOWN_STRATEGY_MAX,
};

static int mca_battery_shutdown_parse_vcutoff_para(struct device_node *node,
	const char *name, struct mca_battery_shutdown_vcutoff_para_info *vcutoff_info)
{
	struct mca_battery_shutdown_vcutoff_para *vcutoff_para;
	int array_len, row, col, i;
	const char *tmp_string = NULL;

	if (strcmp(name, "null") == 0) {
#line 71
		mca_log_info("no need parse volt para\n");
		return 0;
	}
	array_len = mca_parse_dts_count_strings(node, name,
		VCUTOFF_PARA_MAX_GROUP,
		MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_MAX);
	if (array_len < 0) {
#line 78
		mca_log_err("parse %s failed\n", name);
		return -1;
	}

	vcutoff_info->vcutoff_para_size = array_len / MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_MAX;
	vcutoff_info->vcutoff_para = kcalloc(vcutoff_info->vcutoff_para_size, sizeof(*vcutoff_para), GFP_KERNEL);
	if (!vcutoff_info->vcutoff_para) {
#line 85
		mca_log_err("vcutoff para no mem\n");
		return -ENOMEM;
	}
	vcutoff_para = vcutoff_info->vcutoff_para;
	for (i = 0; i < array_len; i++) {
		if (mca_parse_dts_string_index(node, name, i, &tmp_string))
			return -1;

		row = i / MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_MAX;
		col = i % MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_MAX;

		switch (col) {
		case MCA_BATTERY_SHUTDOWN_RANGE_UPPER:
			if (kstrtoint(tmp_string, 10, &vcutoff_para[row].range_upper))
				goto error;
			break;
		case MCA_BATTERY_SHUTDOWN_VCUTOFF_FOR_FW:
			if (kstrtoint(tmp_string, 10, &vcutoff_para[row].vcutoff_fw))
				goto error;
			break;
		case MCA_BATTERY_SHUTDOWN_VCUTOFF_FOR_SHUTDOWN_DELAY:
			if (kstrtoint(tmp_string, 10, &vcutoff_para[row].vcutoff_shutdown_delay))
				goto error;
			break;
		case MCA_BATTERY_SHUTDOWN_VCUTOFF_FOR_SW:
			if (kstrtoint(tmp_string, 10, &vcutoff_para[row].vcutoff_sw))
				goto error;
			break;
		default:
			break;
		}
	}

	return 0;
error:
	kfree(vcutoff_para);
	vcutoff_para = NULL;
	return -1;
}

int mca_battery_shutdown_parse_dt(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	struct device_node *node = fg->dev->of_node;
	int rc;
	struct mca_battery_shutdown_para_info *batt_spec_para_info = &(fg->batt_spec_para);
	struct mca_battery_shutdown_para_info *batt_health_para_info = &(fg->batt_health_para);
	int array_len, row, col;
	const char *tmp_string = NULL;
	struct mca_battery_shutdown_temp_para_info *temp_info;
	const struct mca_hwid *hwid = mca_get_hwid_info();
	const char *device_name = NULL;
	bool is_gl_cc = false;
	bool is_gl_dod = false;
	int i;

	rc = mca_parse_dts_u32(node, "vcutoff_shutdown_delay",
		&fg->vcutoff_shutdown_delay, MCA_BATTERY_SHUTDOWN_VCUTTOFF_SHUTDOWN_DELAY);
	rc |= mca_parse_dts_u32(node, "vcutoff_sw",
		&fg->vcutoff_sw, MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW);
	rc |= mca_parse_dts_u32(node, "vcutoff_fw",
		&fg->vcutoff_fw, MCA_BATTERY_SHUTDOWN_VCUTTOFF_FW);
	if (rc) {
#line 148
		mca_log_err(" normal vcutoff parse failed ret=%d\n", rc);
		return rc;
	}

	fg->support_cc_vcutoff = of_property_read_bool(node, "support-cc-vcutoff");
	fg->support_cc_vcutoff_gl = of_property_read_bool(node, "support-cc-vcutoff-global");
	if (hwid && fg->support_cc_vcutoff_gl && hwid->country_version != CountryCN)
		is_gl_cc = true;
	fg->notsupport_dod_vcutoff_global = of_property_read_bool(node, "notsupport-dod-vcutoff-global");
	if (hwid && fg->notsupport_dod_vcutoff_global && hwid->country_version != CountryCN)
		fg->support_cc_vcutoff = false;
	fg->support_cc_unique_cn_nvt = of_property_read_bool(node, "support-cc-unique-cn-nvt");
	if (fg->support_cc_unique_cn_nvt) {
		if (hwid->country_version == CountryCN) {
			int retry = 1;

			do {
				platform_fg_ops_get_device_name(FG_IC_MASTER, &device_name);
				msleep(100);
#line 168
				mca_log_err("retry get device name:%d\n", retry);
			} while (!device_name && retry++ != 20);
			if (!device_name) {
#line 172
				mca_log_err("get device name  fail\n");
				return -1;
			}
#line 175
			mca_log_err("CN device name: %s\n", device_name);
			if (strcmp("3XM31", device_name))
				fg->support_cc_unique_cn_nvt = false;
		} else {
			fg->support_cc_unique_cn_nvt = false;
#line 182
			mca_log_err("GL device no need check\n");
		}
	}
#line 184
	mca_log_err("is nvt unique scheme %d\n", fg->support_cc_unique_cn_nvt);

	if (fg->support_cc_vcutoff) {
		array_len = mca_parse_dts_count_strings(node,
			is_gl_cc ? "cc_vcutoff_cfg_gl" :
			(fg->support_cc_unique_cn_nvt ? "cc_vcutoff_cfg_cn_nvt" : "cc_vcutoff_cfg"),
			VCUTOFF_PARA_MAX_GROUP,
			MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX);
		if (array_len < 0) {
#line 200
			mca_log_err("parse cc_vcutoff_cfg failed\n");
			return -1;
		}

		batt_health_para_info->temp_para_size = array_len / MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
		batt_health_para_info->temp_info = kcalloc(batt_health_para_info->temp_para_size, sizeof(*temp_info), GFP_KERNEL);
		if (!batt_health_para_info->temp_info) {
#line 207
			mca_log_err("vcutoff_para_info->temp_para no mem\n");
			return -ENOMEM;
		}

		temp_info = batt_health_para_info->temp_info;
		for (i = 0; i < array_len; i++) {
			if (is_gl_cc) {
				if (mca_parse_dts_string_index(node, "cc_vcutoff_cfg_gl", i, &tmp_string)) {
#line 215
					mca_log_err("gl cc config is not ready, check pls!\n");
					return -1;
				}
			} else if (fg->support_cc_unique_cn_nvt) {
				if (mca_parse_dts_string_index(node, "cc_vcutoff_cfg_cn_nvt", i, &tmp_string))
					return -1;
			} else {
				if (mca_parse_dts_string_index(node, "cc_vcutoff_cfg", i, &tmp_string))
					return -1;
			}
			row = i / MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
			col = i % MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
			switch (col) {
			case MCA_BATTERY_SHUTDOWN_TEMP_LOW:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_low))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_TEMP_HIGH:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_high))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_LOW_TEMP_HYS:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.low_temp_hysteresis))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_HIGH_TEMP_HYS:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.high_temp_hysteresis))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_NAME:
				if (mca_battery_shutdown_parse_vcutoff_para(node, tmp_string,
					&temp_info[row].vcutoff_info))
					goto error;
				break;
			default:
				break;
			}
		}
	}

	fg->support_dod_vcutoff = of_property_read_bool(node, "support-dod-vcutoff");
	fg->support_dod_vcutoff_gl = of_property_read_bool(node, "support-dod-vcutoff-global");
	fg->support_dod_vcutoff_v2 = of_property_read_bool(node, "support-dod-vcutoff-v2");
	if (hwid && fg->support_dod_vcutoff_gl && hwid->country_version != CountryCN)
		is_gl_dod = true;
	if (hwid && fg->notsupport_dod_vcutoff_global && hwid->country_version != CountryCN)
		fg->support_dod_vcutoff = false;
	if (fg->support_dod_vcutoff || fg->support_dod_vcutoff_v2) {
		if (is_gl_dod) {
			array_len = mca_parse_dts_count_strings(node, "dod_vcutoff_cfg_gl",
				BATTERY_TEMP_PARA_MAX_GROUP,
				MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX);
#line 269
			mca_log_err("parse dod_vcutoff_cfg-gl, %d\n", array_len);
		} else {
			array_len = mca_parse_dts_count_strings(node, "dod_vcutoff_cfg",
				BATTERY_TEMP_PARA_MAX_GROUP,
				MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX);
		}
		if (array_len < 0) {
#line 275
			mca_log_err("parse dod_vcutoff_cfg failed\n");
			return -1;
		}
		batt_spec_para_info->temp_para_size = array_len / MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
		batt_spec_para_info->temp_info = kcalloc(batt_spec_para_info->temp_para_size, sizeof(*temp_info), GFP_KERNEL);
		if (!batt_spec_para_info->temp_info) {
#line 281
			mca_log_err("batt_para_info->temp_para no mem\n");
			return -ENOMEM;
		}

		temp_info = batt_spec_para_info->temp_info;
		for (i = 0; i < array_len; i++) {
			if (is_gl_dod) {
				if (mca_parse_dts_string_index(node, "dod_vcutoff_cfg_gl", i, &tmp_string)) {
#line 289
					mca_log_err("gl dod config is not ready, check pls!\n");
					return -1;
				}
			} else {
				if (mca_parse_dts_string_index(node, "dod_vcutoff_cfg", i, &tmp_string))
					return -1;
			}

			row = i / MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
			col = i % MCA_BATTERY_SHUTDOWN_TEMP_PARA_MAX;
			switch (col) {
			case MCA_BATTERY_SHUTDOWN_TEMP_LOW:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_low))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_TEMP_HIGH:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.temp_high))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_LOW_TEMP_HYS:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.low_temp_hysteresis))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_HIGH_TEMP_HYS:
				if (kstrtoint(tmp_string, 10, &temp_info[row].temp_para.high_temp_hysteresis))
					goto error;
				break;
			case MCA_BATTERY_SHUTDOWN_VCUTOFF_PARA_NAME:
				if (mca_battery_shutdown_parse_vcutoff_para(node, tmp_string,
					&temp_info[row].vcutoff_info))
					goto error;
				break;
			default:
				break;
			}
		}

		if (is_gl_dod) {
			mca_parse_dts_u32_array(node, "weight-global", fg->weight, WEIGHT_LEVEL_MAX);
		} else if (fg->support_cc_unique_cn_nvt) {
			mca_parse_dts_u32_array(node, "weight_cn_nvt", fg->weight, WEIGHT_LEVEL_MAX);
		} else {
			if (batt_spec_para_info->temp_para_size < 1) {
#line 333
				mca_log_err("temp_para_size is 0, cannot access temp_info[0]\n");
				kfree(temp_info);
				return -1;
			}
			batt_spec_para_info->total_gear = batt_spec_para_info->temp_info[0].vcutoff_info.vcutoff_para_size;
			batt_spec_para_info->init_update_siloss = 1;
			mca_parse_dts_u32_array(node, "weight", fg->weight, WEIGHT_LEVEL_MAX);
		}
	}

	fg->support_vpack_low_shutdown = of_property_read_bool(node, "support-vpack-low-shutdown");
	fg->disable_rollback_shutdown_volt = of_property_read_bool(node, "disable-rollback-shutdown-volt");
	return 0;

error:
	kfree(temp_info);
	temp_info = NULL;
	return -1;
}

#define ROLLBACK_SHUTDOWN_VOLT_MAX 3000
int mca_battery_shutdown_update_cc_para(void *data, int *vcutoff_fw, int *vcutoff_shutdown_delay, int *vcutoff_sw)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	static int last_temp_index;
	struct mca_battery_shutdown_vcutoff_para_info *vcutoff_info;
	struct mca_battery_shutdown_temp_para temp_para;
	int cur_temp_index = -1, cur_vcuttoff_index = -1;
	int lock = 0, i, temp;

	if (!fg->support_cc_vcutoff)
		return 0;

	temp = fg->batt_temperature / 10;
	for (i = 0; i < fg->batt_health_para.temp_para_size; i++) {
		if (temp < fg->batt_health_para.temp_info[i].temp_para.temp_high &&
		    temp >= fg->batt_health_para.temp_info[i].temp_para.temp_low) {
			cur_temp_index = i;
			break;
		}
	}

	if (cur_temp_index < 0 || cur_temp_index >= fg->batt_health_para.temp_para_size) {
#line 373
		mca_log_err("temp over range invalid\n");
		return -1;
	}

	temp_para = fg->batt_health_para.temp_info[fg->batt_health_para.cur_temp_index].temp_para;
	if (cur_temp_index > fg->batt_health_para.cur_temp_index) {
		if (temp >= temp_para.temp_high + temp_para.high_temp_hysteresis)
			fg->batt_health_para.cur_temp_index = cur_temp_index;
	} else if (cur_temp_index < fg->batt_health_para.cur_temp_index) {
		if (temp < temp_para.temp_low - temp_para.low_temp_hysteresis)
			fg->batt_health_para.cur_temp_index = cur_temp_index;
	}

	vcutoff_info = &fg->batt_health_para.temp_info[fg->batt_health_para.cur_temp_index].vcutoff_info;
	if (fg->support_dod_vcutoff_v2) {
		if (fg->batt_spec_para.init_update_siloss)
			last_temp_index = fg->batt_health_para.cur_temp_index;
		if (fg->batt_rsoc <= 9 && fg->batt_health_para.cur_temp_index > last_temp_index) {
			vcutoff_info = &fg->batt_health_para.temp_info[last_temp_index].vcutoff_info;
			lock = 1;
		} else {
			last_temp_index = fg->batt_health_para.cur_temp_index;
		}
	}

	for (i = 0; i < vcutoff_info->vcutoff_para_size; i++) {
		if (fg->batt_cyclecount <= vcutoff_info->vcutoff_para[i].range_upper) {
			cur_vcuttoff_index = i;
			break;
		}
	}

	if (cur_vcuttoff_index < 0 || cur_vcuttoff_index >= vcutoff_info->vcutoff_para_size) {
#line 408
		mca_log_err("cyclecount over range invalid\n");
		return -1;
	}

	*vcutoff_fw = vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_fw;
	*vcutoff_shutdown_delay = vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_shutdown_delay;
	*vcutoff_sw = vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_sw;
#line 423
	mca_log_info("BatteryHealth: T_idx(curr/eff/last)=(%d/%d/%d) V(idx/fw/delay/sw)=(%d/%d/%d/%d) Lock=%d\n",
		     cur_temp_index, fg->batt_health_para.cur_temp_index, last_temp_index, cur_vcuttoff_index,
		     *vcutoff_fw, *vcutoff_shutdown_delay, *vcutoff_sw, lock);
	return 0;
}

void strategy_update_vcoutoff_v2_work(struct work_struct *work)
{
	struct strategy_fg *fg = container_of(work, struct strategy_fg, update_vcoutoff_v2_work.work);
	static int cyclecount;
	static int last_vcuttoff_index;
	static int last_temp_index;
	static time64_t iaver_time;
	static bool iaver_time_flag;
	int vcutoff_fw[SHUTDOWN_STRATEGY_MAX] = {0};
	int vcutoff_shutdown_delay[SHUTDOWN_STRATEGY_MAX] = {0};
	int vcutoff_sw[SHUTDOWN_STRATEGY_MAX] = {0};
	struct mca_battery_shutdown_vcutoff_para_info *vcutoff_info;
	struct mca_battery_shutdown_temp_para temp_para;
	int vsiloss = 0, iaver = 0;
	int cur_temp_index = -1, cur_vcuttoff_index, eff_index, v_rt, delta_v, best, dist, d, i, j;
	int curr_v_index = -1;
	int temp = fg->batt_temperature;
	int msecs = 30000, lock = 0, allow_up = 0;

	if (!fg->support_dod_vcutoff_v2 || temp > 599)
		return;

	mutex_lock(&fg->temp_index_lock);
	mca_battery_shutdown_update_cc_para(fg, &vcutoff_fw[0], &vcutoff_shutdown_delay[0], &vcutoff_sw[0]);

	if (!fg->support_dod_vcutoff_v2)
		goto update_result;

	if (!fg->power_present)
		fg->batt_spec_para.i_aver_gear = 0;

	if (cyclecount == fg->batt_cyclecount && !fg->batt_spec_para.init_update_siloss) {
		cur_vcuttoff_index = fg->batt_spec_para.cur_vcuttoff_index;
	} else {
		cyclecount = fg->batt_cyclecount;
		if (fg->batt_spec_para.temp_para_size <= 3) {
#line 588
			mca_log_err("temp_para_size %d is too small for index %d\n", fg->batt_spec_para.temp_para_size, 3);
			goto out;
		}
		vcutoff_info = &fg->batt_spec_para.temp_info[3].vcutoff_info;
		for (i = 0; i < vcutoff_info->vcutoff_para_size; i++) {
			if (fg->dod_count < vcutoff_info->vcutoff_para[i].range_upper)
				break;
		}
		if (i >= vcutoff_info->vcutoff_para_size) {
#line 601
			mca_log_err("dod_count over range invalid, current:%d\n", -1);
			goto out;
		}
		cur_vcuttoff_index = i;
		v_rt = vcutoff_info->vcutoff_para[i].vcutoff_fw;
		platform_fg_ops_get_vsiloss(0, &vsiloss);
		delta_v = (vsiloss + v_rt) / 2 - v_rt;
#line 607
		mca_log_info("V_Rt:%d, V_siloss:%d, delta_v:%d", v_rt, vsiloss, delta_v);
		fg->batt_spec_para.cur_vcuttoff_index = cur_vcuttoff_index;
		if ((unsigned int)delta_v <= 100) {
			int avg = (vsiloss + v_rt) / 2;

			best = cur_vcuttoff_index;
			dist = abs(vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_fw - avg);
			for (j = cur_vcuttoff_index + 1; j < vcutoff_info->vcutoff_para_size; j++) {
				d = abs(vcutoff_info->vcutoff_para[j].vcutoff_fw - avg);
				if (d < dist) {
					best = j;
					dist = d;
				}
			}
			fg->batt_spec_para.siloss_gear = best - cur_vcuttoff_index;
		}
	}

	eff_index = min(fg->batt_spec_para.siloss_gear + cur_vcuttoff_index, fg->batt_spec_para.total_gear - 1);

	temp /= 10;
	for (i = 0; i < fg->batt_spec_para.temp_para_size; i++) {
#line 627
		mca_log_info("temp_para_size:%d, temp: %d, high: %d, low: %d\n", fg->batt_spec_para.temp_para_size, temp,
			     fg->batt_spec_para.temp_info[i].temp_para.temp_high, fg->batt_spec_para.temp_info[i].temp_para.temp_low);
		if (temp < fg->batt_spec_para.temp_info[i].temp_para.temp_high &&
		    temp >= fg->batt_spec_para.temp_info[i].temp_para.temp_low) {
			cur_temp_index = i;
			break;
		}
	}

	if (cur_temp_index < 0 || cur_temp_index >= fg->batt_spec_para.temp_para_size) {
#line 637
		mca_log_err("temp over range invalid, current:%d\n", cur_temp_index);
		goto out;
	}

	temp_para = fg->batt_spec_para.temp_info[fg->batt_spec_para.cur_temp_index].temp_para;
	if (cur_temp_index > fg->batt_spec_para.cur_temp_index) {
		if (temp >= temp_para.temp_high + temp_para.high_temp_hysteresis)
			fg->batt_spec_para.cur_temp_index = cur_temp_index;
	} else if (cur_temp_index < fg->batt_spec_para.cur_temp_index) {
		if (temp < temp_para.temp_low - temp_para.low_temp_hysteresis)
			fg->batt_spec_para.cur_temp_index = cur_temp_index;
	}

	vcutoff_info = &fg->batt_spec_para.temp_info[fg->batt_spec_para.cur_temp_index].vcutoff_info;
	if (fg->batt_spec_para.init_update_siloss) {
		fg->batt_spec_para.init_update_siloss = 0;
		last_vcuttoff_index = eff_index;
		last_temp_index = fg->batt_spec_para.cur_temp_index;
	}

	platform_fg_ops_get_iaver(0, &iaver);
	if (iaver >= -300 && iaver <= 0) {
		if (!iaver_time_flag) {
			iaver_time_flag = true;
			iaver_time = ktime_get_boottime();
		}
		if (iaver_time_flag && ktime_get_boottime() - iaver_time >= 64800LL * NSEC_PER_SEC) {
#line 668
			mca_log_info("allow i_aver up gear");
			allow_up = 1;
		}
	} else {
		iaver_time_flag = false;
	}

	if (fg->batt_rsoc >= 51) {
		lock = 0;
		last_temp_index = fg->batt_spec_para.cur_temp_index;
		msecs = 600000;
	} else if (fg->batt_rsoc >= 10) {
		if (allow_up && !fg->batt_spec_para.i_aver_gear) {
			fg->batt_spec_para.i_aver_gear = 1;
			eff_index = min(eff_index + 1, fg->batt_spec_para.total_gear - 1);
		}
		lock = 0;
		last_temp_index = fg->batt_spec_para.cur_temp_index;
	} else {
		eff_index = last_vcuttoff_index;
		if (fg->batt_spec_para.cur_temp_index > last_temp_index) {
			lock = 1;
			vcutoff_info = &fg->batt_spec_para.temp_info[last_temp_index].vcutoff_info;
		} else {
			lock = 0;
			last_temp_index = fg->batt_spec_para.cur_temp_index;
		}
	}
	curr_v_index = eff_index;
	vcutoff_fw[1] = vcutoff_info->vcutoff_para[eff_index].vcutoff_fw;
	vcutoff_shutdown_delay[1] = vcutoff_info->vcutoff_para[eff_index].vcutoff_shutdown_delay;
	vcutoff_sw[1] = vcutoff_info->vcutoff_para[eff_index].vcutoff_sw;
	last_vcuttoff_index = eff_index;

update_result:
	fg->vcutoff_fw = max(vcutoff_fw[0], vcutoff_fw[1]);
	fg->vcutoff_shutdown_delay = max(vcutoff_shutdown_delay[0], vcutoff_shutdown_delay[1]);
	fg->vcutoff_sw = max(vcutoff_sw[0], vcutoff_sw[1]);
#line 705
	mca_log_info("T_idx(curr/eff/last)=(%d/%d/%d) V_idx(curr/eff/last)=(%d/%d/%d) V(fw/delay/sw)=(%d/%d/%d)\n",
		     cur_temp_index, fg->batt_spec_para.cur_temp_index, last_temp_index, curr_v_index,
		     fg->batt_spec_para.cur_vcuttoff_index, last_vcuttoff_index, fg->vcutoff_fw,
		     fg->vcutoff_shutdown_delay, fg->vcutoff_sw);
#line 711
	mca_log_info("Gear(silos/iaver)=(%d/%d) Iaver(val/flag/en)=(%d/%d/%d) Lock=%d\n", fg->batt_spec_para.siloss_gear,
		     fg->batt_spec_para.i_aver_gear, iaver, iaver_time_flag, allow_up, lock);
	mutex_unlock(&fg->temp_index_lock);
	if (!fg->power_present)
		schedule_delayed_work(&fg->update_vcoutoff_v2_work, msecs_to_jiffies(msecs));	/* G52a: stock loads system_wq after msecs_to_jiffies */
	return;

out:
	mutex_unlock(&fg->temp_index_lock);
}

int mca_battery_shutdown_update_vcutoff_para(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int vcutoff_fw[SHUTDOWN_STRATEGY_MAX] = {0};
	int vcutoff_shutdown_delay[SHUTDOWN_STRATEGY_MAX] = {0};
	int vcutoff_sw[SHUTDOWN_STRATEGY_MAX] = {0};
	int vcutoff_shutdown_volt = 0, index = 0;
	int temp = fg->batt_temperature / 10;

	if (!fg->support_cc_vcutoff && !fg->support_dod_vcutoff)
		return -1;

	mutex_lock(&fg->temp_index_lock);
	/* OS4: the health (CC) strategy is a separate function; its error does not abort the update */
	mca_battery_shutdown_update_cc_para(fg, vcutoff_fw, vcutoff_shutdown_delay, vcutoff_sw);

	if (fg->support_dod_vcutoff) {
		int cur_temp_index = -1;
		int cur_vcuttoff_index = -1;
		struct mca_battery_shutdown_vcutoff_para_info *vcutoff_info;
		struct mca_battery_shutdown_temp_para temp_para;

		for (int i = 0; i < fg->batt_spec_para.temp_para_size; i++) {
#line 455
			mca_log_err("temp_para_size:%d, temp: %d, high: %d, low: %d\n", fg->batt_spec_para.temp_para_size, temp,
				fg->batt_spec_para.temp_info[i].temp_para.temp_high, fg->batt_spec_para.temp_info[i].temp_para.temp_low);
			if (temp < fg->batt_spec_para.temp_info[i].temp_para.temp_high &&
				temp >= fg->batt_spec_para.temp_info[i].temp_para.temp_low) {
				cur_temp_index = i;
				break;
			}
		}

		if (cur_temp_index < 0 || cur_temp_index >= fg->batt_spec_para.temp_para_size) {
#line 465
			mca_log_err("temp over range invalid, current:%d\n", cur_temp_index);
			mutex_unlock(&fg->temp_index_lock);
			return -1;
		}

		temp_para = fg->batt_spec_para.temp_info[fg->batt_spec_para.cur_temp_index].temp_para;
		if (cur_temp_index > fg->batt_spec_para.cur_temp_index) {
			if (temp >= temp_para.temp_high + temp_para.high_temp_hysteresis)
				fg->batt_spec_para.cur_temp_index = cur_temp_index;
		} else if (cur_temp_index < fg->batt_spec_para.cur_temp_index) {
			if (temp < temp_para.temp_low - temp_para.low_temp_hysteresis)
				fg->batt_spec_para.cur_temp_index = cur_temp_index;
		}

		vcutoff_info = &(fg->batt_spec_para.temp_info[fg->batt_spec_para.cur_temp_index].vcutoff_info);
		for (int i = 0; i < vcutoff_info->vcutoff_para_size; i++) {
			if (fg->dod_count < vcutoff_info->vcutoff_para[i].range_upper) {
				cur_vcuttoff_index = i;
				break;
			}
		}

		if (cur_vcuttoff_index < 0 || cur_vcuttoff_index > vcutoff_info->vcutoff_para_size) {
#line 488
			mca_log_err("dod_count over range invalid\n");
			mutex_unlock(&fg->temp_index_lock);
			return -1;
		}

		vcutoff_fw[SHUTDOWN_STRATEGY_BATTERY_SPEC] =
			vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_fw;
		vcutoff_shutdown_delay[SHUTDOWN_STRATEGY_BATTERY_SPEC] =
			vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_shutdown_delay;
		vcutoff_sw[SHUTDOWN_STRATEGY_BATTERY_SPEC] =
			vcutoff_info->vcutoff_para[cur_vcuttoff_index].vcutoff_sw;

#line 500
		mca_log_info("BatterySpec:cur_temp_index[%d], last_temp_index[%d] cur_vcuttoff_index[%d]\n",
			cur_temp_index, fg->batt_spec_para.cur_temp_index, cur_vcuttoff_index);
	}


	for (int i = 0; i < SHUTDOWN_STRATEGY_MAX; i++) {
		if (vcutoff_shutdown_volt < vcutoff_shutdown_delay[i]) {
			vcutoff_shutdown_volt = vcutoff_shutdown_delay[i];
			index = i;
		}
#line 510
		mca_log_info("Max-Strategy[%d], vcutoff_fw %d, vcutoff_shutdown_delay %d vcutoff_sw %d\n",
		i, vcutoff_fw[i], vcutoff_shutdown_delay[i], vcutoff_sw[i]);
	}


	if (index  < 0 || index >= SHUTDOWN_STRATEGY_MAX) {
		mca_log_info("Strategy Optimla Select Invalid\n");
		mutex_unlock(&fg->temp_index_lock);
		return 0;
	}

	if (!fg->disable_rollback_shutdown_volt && fg->enable_rollback) {
		if (vcutoff_shutdown_delay[index] < ROLLBACK_SHUTDOWN_VOLT_MAX) {
			fg->vcutoff_fw = MCA_BATTERY_SHUTDOWN_VCUTTOFF_FW;
			fg->vcutoff_shutdown_delay = MCA_BATTERY_SHUTDOWN_VCUTTOFF_SHUTDOWN_DELAY;
			fg->vcutoff_sw = MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW;
#line 526
			mca_log_info("[Enable_Rollback][%d] vcutoff_fw[%d], vcutoff_shutdown_delay[%d], vcutoff_sw[%d]\n",
				index, fg->vcutoff_fw, fg->vcutoff_shutdown_delay, fg->vcutoff_sw);
			mutex_unlock(&fg->temp_index_lock);
			return 0;
		}
	}

	fg->vcutoff_fw = vcutoff_fw[index] > MCA_BATTERY_SHUTDOWN_VCUTTOFF_MIN ?
		vcutoff_fw[index] : MCA_BATTERY_SHUTDOWN_VCUTTOFF_FW;
	fg->vcutoff_shutdown_delay = vcutoff_shutdown_delay[index] > MCA_BATTERY_SHUTDOWN_VCUTTOFF_MIN ?
		vcutoff_shutdown_delay[index] : MCA_BATTERY_SHUTDOWN_VCUTTOFF_SHUTDOWN_DELAY;
	if (!fg->cfg.support_base_flip)
		fg->vcutoff_sw = vcutoff_sw[index] >= MCA_BATTERY_SHUTDOWN_VCUTTOFF_MIN ?
			vcutoff_sw[index] : MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW;
	else
		fg->vcutoff_sw = vcutoff_sw[index] < MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW ?
			vcutoff_sw[index] : MCA_BATTERY_SHUTDOWN_VCUTTOFF_SW;
#line 544
	mca_log_info("Strategy[%d]:vcutoff_fw[%d], vcutoff_shutdown_delay[%d], vcutoff_sw[%d]\n",
		index, fg->vcutoff_fw, fg->vcutoff_shutdown_delay, fg->vcutoff_sw);
	mutex_unlock(&fg->temp_index_lock);
	return 0;
}

static bool mca_battery_shutdown_heavy_load_check(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	static int heavy_load_count;

	if (fg->batt_voltage_mean < (fg->vcutoff_shutdown_delay + MCA_BATTERY_SHUTDOWN_VOLTAGE_HYST) &&
		fg->batt_current > MCA_HEAVY_LOAD_DISCHARGING_CURRENT)
		heavy_load_count++;
	else
		heavy_load_count = 0;

#line 731
	mca_log_info("heavy_load_count: %d\n", heavy_load_count);

	if (heavy_load_count > MCA_HEAVY_LOAD_DISCHARGING_COUNT)
		return true;
	else
		return false;
}

static bool mca_shutdown_due_to_vpack_low_of_vpack_inside_fg_absent(void *data)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int vpack_vol_now_mv = 0, chip_ok = 0;
	static int count = 0;

	if (!fg->support_vpack_low_shutdown)
		return false;

	vpack_vol_now_mv = fg->pvbat;

	chip_ok = strategy_class_fg_is_chip_ok();
#line 753
	mca_log_info("chip_ok:%d vpack_vol_now_mv:%d",
			chip_ok, vpack_vol_now_mv);
	if (!chip_ok && count < 10 && vpack_vol_now_mv <= 3000) {
		count++;
	} else {
		count = 0;
	}

	if (count >= 10)
		return true;
	else
		return false;
}

int mca_battery_shutdown_check_shutdown_delay(void *data, int *ui_soc)
{
	struct strategy_fg *fg = (struct strategy_fg *)data;
	int soc = *ui_soc;
	int charging_status = POWER_SUPPLY_STATUS_UNKNOWN;
	static bool shutdown_dealy, last_shutdown_delay;
	static bool shutdown_delay_cancel;
	int len;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	union power_supply_propval pval = { 0, };
	struct mca_event_notify_data n_data = { 0 };
	bool heavy_load_off = false;

	if (fg->batt_psy) {
		power_supply_get_property(fg->batt_psy, POWER_SUPPLY_PROP_STATUS, &pval);
		charging_status = pval.intval;
	}

	if (mca_shutdown_due_to_vpack_low_of_vpack_inside_fg_absent(fg)
		&& charging_status != POWER_SUPPLY_STATUS_CHARGING) {
		shutdown_dealy = true;
		shutdown_delay_cancel = false;
		soc = 1;
		goto report_shutdown;
	}

	if (soc <= 1) {
		heavy_load_off = mca_battery_shutdown_heavy_load_check(fg);

		if ((fg->batt_voltage_mean <= fg->vcutoff_shutdown_delay || heavy_load_off) &&
			charging_status != POWER_SUPPLY_STATUS_CHARGING) {
			shutdown_dealy = true;
			shutdown_delay_cancel = false;
			soc = 1;
		} else if (charging_status == POWER_SUPPLY_STATUS_CHARGING) {
			if (shutdown_dealy) {
				shutdown_dealy = false;
				shutdown_delay_cancel = true;
			}
			soc = 1;
		} else {
			shutdown_dealy = false;
			soc = 1;
		}
	} else {
		shutdown_dealy = false;
		shutdown_delay_cancel = false;
	}

report_shutdown:
#line 817
	mca_log_info("ui_soc: %d, soc: %d, last_shutdown_delay: %d, shutdown_dealy: %d\n",
		*ui_soc, soc, last_shutdown_delay, shutdown_dealy);

	*ui_soc = soc;

	if (last_shutdown_delay != shutdown_dealy && fg->fake_soc == STRATEGY_FG_FAKE_SOC_NONE) {
#line 823
		mca_log_err("shutdown_dealy changed: %d => %d\n", last_shutdown_delay, shutdown_dealy);
		len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
			"POWER_SUPPLY_SHUTDOWN_DELAY=%u\n", shutdown_dealy);
		n_data.event = event;
		n_data.event_len = len;
		mca_event_report_uevent(&n_data);
		last_shutdown_delay = shutdown_dealy;
	}

	return 0;
}
