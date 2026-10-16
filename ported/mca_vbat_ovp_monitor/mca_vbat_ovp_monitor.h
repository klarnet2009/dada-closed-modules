/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_vbat_ovp_monitor.h
 *
 * battery ovp software detection and protection driver
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

#ifndef __VBAT_OVP_MONITOR_H__
#define __VBAT_OVP_MONITOR_H__


#define VBAT_OVP_THR_FFC_DEFAULT_MV 4580
#define VBAT_OVP_THR_NORMAL_DEFAULT_UV 4530
#define VBAT_OVP_THR_FFC_GL_DEFAULT_MV 4530
#define VBAT_OVP_THR_NORMAL_GL_DEFAULT_UV 4480
#define VBAT_OVP_THR_DEFAULT_HYS_MV 15
#define VBAT_OVP_RECHARGE_DELTA_MV 50

enum strategy_fg_type {
	MCA_FG_TYPE_SINGLE = 0,
	MCA_FG_TYPE_SINGLE_SERIES,   /* single fuelgauge, two cells in series */
	MCA_FG_TYPE_SINGLE_NUM_MAX = MCA_FG_TYPE_SINGLE_SERIES,
	MCA_FG_TYPE_PARALLEL,
	MCA_FG_TYPE_SERIES,
	MCA_FG_TYPE_MAX = MCA_FG_TYPE_SERIES,
};

enum vbat_ovp_mon_attr_list {
	FAKE_VBAT_FOR_DEBUG,
	FAKE_VBAT_OVERRIDE,
	FAKE_VBAT_MON_ATTR_MAX,
};

struct mca_vbat_ovp_mon_dev {
	struct device *dev;
	struct delayed_work monitor_vbat_ovp_work;
	int vbat_ovp_thr_ffr_mv;
	int vbat_ovp_thr_nor_mv;
	int vbat_ovp_threshold_ffc_mv;
	int vbat_ovp_threshold_normal_mv;
	int vbat_ovp_threshold_ffc_gl_mv;
	int vbat_ovp_threshold_normal_gl_mv;
	int vbat_ovp_thr_hys_mv;
	int vbat_ovp_recharge_delta_mv;
	int fake_vbat_for_debug;
	int fake_vbat_override_mv;
	int fg_type;
	int fake_vbat_ovp_status;
	int batt_ovp_status;
	bool support_global_fv;
};

#endif /* __VBAT_OVP_MONITOR_H__ */
