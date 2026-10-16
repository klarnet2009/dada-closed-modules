/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_battery_psy.h
 *
 * mca battery psy driver
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
#ifndef __MCA_BATTERY_PSY_H__
#define __MCA_BATTERY_PSY_H__

#include <mca/common/mca_voter.h>

#define BATT_OVERHEAT_THRESHOLD	580
#define BATT_WARM_THRESHOLD	480
#define BATT_COOL_THRESHOLD	150
#define BATT_COLD_THRESHOLD	0

struct batt_psy_info {
	struct device *dev;
	struct power_supply_desc batt_psy_desc;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *wls_psy;
	int present;
	int chg_status;
	int fake_power;
	struct mca_votable *input_suppend_voter;
};

struct batt_psy_info *business_battery_psy_init(struct device *dev);
void business_battery_psy_deinit(struct batt_psy_info *info);
/*void business_battery_psy_event_process(struct batt_psy_info *info);*/

#endif /* __BATTERY_PSY_H__ */

