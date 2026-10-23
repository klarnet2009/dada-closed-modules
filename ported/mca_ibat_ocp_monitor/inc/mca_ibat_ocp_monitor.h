/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_ibat_ocp_monitor.h
 *
 * battery icp software detection and protection driver
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

#ifndef __IBAT_OCP_MONITOR_H__
#define __IBAT_OCP_MONITOR_H__

#define OCP_THRESHOLD_MAINT	9280000
#define OCP_THRESHOLD_FLIP	3540000

enum strategy_fg_type {
	MCA_FG_TYPE_SINGLE = 0,
	MCA_FG_TYPE_SINGLE_NUM_MAX = MCA_FG_TYPE_SINGLE,
	MCA_FG_TYPE_PARALLEL,
	MCA_FG_TYPE_SERIES,
	MCA_FG_TYPE_MAX = MCA_FG_TYPE_SERIES,
};

enum ibat_ocp_mon_attr_list {
	FAKE_IBAT_FOR_DEBUG,
	FAKE_MASTER_IBAT_OVERRIDE,
	FAKE_SLAVE_IBAT_OVERRIDE,
	FAKE_IBAT_MON_ATTR_MAX,
};

struct mca_ibat_ocp_mon_dev {
	struct device *dev;
	struct delayed_work monitor_ibat_ocp_work;
	int fake_ibat_for_debug;
	int fake_master_ibat_override_ma;
	int fake_slave_ibat_override_ma;
	int fg_type;
	int fake_ibat_ocp_status;
	int batt_ocp_status;
	int ocp_threshold[2];
};

#endif /* __IBAT_OCP_MONITOR_H__ */
