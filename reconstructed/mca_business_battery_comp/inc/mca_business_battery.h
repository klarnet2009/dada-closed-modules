/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_business_battery.h
 *
 * mca battery business driver
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
#ifndef __BATTERY_CORE_H__
#define __BATTERY_CORE_H__

struct batt_dt_props {
	int test;
	// u32 resistance_id;
};

struct business_battery {
	struct device *dev;
	struct batt_dt_props dt;
	struct notifier_block batt_info_nb;

	struct batt_psy_info *batt_psy_info;
	struct batt_feature_info *batt_feature;
	struct work_struct event_process_work;
};

#endif /* __BATTERY_CORE_H__ */

