// SPDX-License-Identifier: GPL-2.0
/*
 * mca_charge_mievent.c
 *
 * mca charge mievent (MiSight) report driver
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

#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_log.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timekeeping.h>
#include <mca/common/mca_event.h>
#include <miev/mievent.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_charge_mievent"
#endif

#define MIEVENT_STRING_MAX_LEN		128
#define MIEVENT_PARA_NAME_MAX_NUM	16
#define MIEVENT_PARA_NAME_MAX_LEN	30

#define DEFAULT_MAX_COUNT		1
#define LEVEL0_MAX_REPORT_COUNT		3
#define LEVEL1_MAX_REPORT_COUNT		5

#define MINOR_TIMER_S			604800
#define CRITICAL_TIMER_S		600
#define BLOCK_TIMER_S			300

enum mievent_upload_type {
	MIEVENT_UPLOAD_TYPE_PLUG = 0,
	MIEVENT_UPLOAD_TYPE_TIME,
};

enum mievent_data_type {
	MIEVENT_DATA_TYPE_INT = 0,
	MIEVENT_DATA_TYPE_STRING,
	MIEVENT_DATA_TYPE_NULL,
};

struct charge_mievent_info {
	int event_code;
	char event_type[30];
	char event_describe[30];
	int upload_type;
	int type_index;
	int data_type;
	int data_count;
	char para_name[MIEVENT_PARA_NAME_MAX_NUM][MIEVENT_PARA_NAME_MAX_LEN];
};

struct mievent_upload_type_plug {
	int max_count;
	int count;
};

struct mievent_upload_type_time {
	time64_t time_last;
	int time_interval;
	int count;
};

#define MIEVENT_CODE_PD_AUTH_FAILED	0x362e412c
#define MIEVENT_CODE_CP_OPEN_FAILED	0x362e412d
#define MIEVENT_CODE_NOT_STANDARD_ADAPTER	0x362e4511
#define MIEVENT_CODE_RP_SHORT_VBUS_DETECTED	0x362e4512
#define MIEVENT_CODE_LPD_DETECTED	0x362e4513
#define MIEVENT_CODE_CP_VBUS_OVP	0x362e4514
#define MIEVENT_CODE_CP_IBUS_OCP	0x362e4515
#define MIEVENT_CODE_CP_VBAT_OVP	0x362e4516
#define MIEVENT_CODE_CP_IBAT_OCP	0x362e4517
#define MIEVENT_CODE_CP_VAC_OVP	0x362e4518
#define MIEVENT_CODE_ANTI_BURN_TRIGGERED	0x362e451c
#define MIEVENT_CODE_SOC_NOT_FULL	0x362e48fa
#define MIEVENT_CODE_SMART_ENDURANCE_TRIGGERED	0x362e48fc
#define MIEVENT_CODE_SMART_NAVIGATION_TRIGGERED	0x362e48fe
#define MIEVENT_CODE_BATTERY_MISSING	0x362e50cb
#define MIEVENT_CODE_CP_TDIE_HOT	0x362e50cc
#define MIEVENT_CODE_VBUS_UVLO	0x362e50cd
#define MIEVENT_CODE_LOW_TEMP_DISCHARGING	0x362e50cf
#define MIEVENT_CODE_HIGH_TEMP_DISCHARGING	0x362e50d0
#define MIEVENT_CODE_DUAL_BATTERY_MISSING	0x362e50d1
#define MIEVENT_CODE_SMART_ENDURANCE_SOC_ERR	0x362e54ba
#define MIEVENT_CODE_SMART_NAVIGATION_SOC_ERR	0x362e54bb
#define MIEVENT_CODE_BATTERY_AUTH_FAIL	0x362e5899
#define MIEVENT_CODE_DUAL_BATTERY_AUTH_FAIL	0x362e589a
#define MIEVENT_CODE_ANTIBURN_ERR	0x362e606b
#define MIEVENT_CODE_WLS_FASTCHG_FAIL	0x362e6839
#define MIEVENT_CODE_WLS_FOD_LOW_POWER	0x362e683a
#define MIEVENT_CODE_WLS_RX_OTP	0x362e6c21
#define MIEVENT_CODE_WLS_RX_OVP	0x362e6c22
#define MIEVENT_CODE_WLS_RX_OCP	0x362e6c23
#define MIEVENT_CODE_WLS_TRX_FOD	0x362e6c24
#define MIEVENT_CODE_WLS_TRX_OCP	0x362e6c25
#define MIEVENT_CODE_WLS_TRX_UVLO	0x362e6c26
#define MIEVENT_CODE_WLS_TRX_IIC_ERR	0x362e7009
#define MIEVENT_CODE_WLS_RX_IIC_ERR	0x362e700c
#define MIEVENT_CODE_LOAD_SWITCH_IIC_ERR	0x362e589b
#define MIEVENT_CODE_WLS_FW_UPGRADE_ERR	0x362e700a
#define MIEVENT_CODE_CHARGE_SLOWLY	0x362e412e
#define MIEVENT_CODE_NON_STANDARD_CHARGER	0x362e412f
#define MIEVENT_CODE_BATTERY_OCD	0x362e73f3
#define MIEVENT_CODE_BATTERY_CUV	0x362e73f4
#define MIEVENT_CODE_BATTERY_SCD	0x362e73f5
#define MIEVENT_CODE_BATTERY_CYCLECOUNT	0x362e48f9
#define MIEVENT_CODE_FG_IIC_ERR	0x362e50c9
#define MIEVENT_CODE_CP_ABSENT	0x362e50ca
#define MIEVENT_CODE_VBATT_SOC_NOT_MATCH	0x362e54b1
#define MIEVENT_CODE_BATTERY_TEMP_HOT	0x362e6069
#define MIEVENT_CODE_BATTERY_TEMP_COLD	0x362e606a
#define MIEVENT_CODE_BATTERY_VOLTAGE_DIFFER	0x362e73f2
#define MIEVENT_CODE_WLS_MAG_CASE_ATTACH	0x362e683b
/* OS4: 13 new plug-type events */
#define MIEVENT_CODE_RM_FCC_ERR	0x362e740b
#define MIEVENT_CODE_RECHG_SOC_ERR	0x362e7404
#define MIEVENT_CODE_NOT_STOP_CHG_ERR	0x362e7405
#define MIEVENT_CODE_CHG_BROKEN	0x362e451d
#define MIEVENT_CODE_BATTERY_COV	0x362e73f6
#define MIEVENT_CODE_BATTERY_OCC	0x362e73f7
#define MIEVENT_CODE_ISP_LEVEL_ONE	0x362e48ff
#define MIEVENT_CODE_ISP_LEVEL_TWO	0x362e4900
#define MIEVENT_CODE_ISP_LEVEL_THREE	0x362e4901
#define MIEVENT_CODE_CSD_FLAG	0x362e4902
#define MIEVENT_CODE_SOC_JUMP	0x362e4ce1
#define MIEVENT_CODE_VBAT_MEAN_SHUTDOWN	0x362e4ce2
#define MIEVENT_CODE_BATTERY_OVER_VOLTAGE	0x362e6452

enum mievent_plug_type {
	PLUG_TYPE_PD_AUTH_FAILED,
	PLUG_TYPE_CP_OPEN_FAILED,
	PLUG_TYPE_NOT_STANDARD_ADAPTER,
	PLUG_TYPE_RP_SHORT_VBUS_DETECTED,
	PLUG_TYPE_LPD_DETECTED,
	PLUG_TYPE_CP_VBUS_OVP,
	PLUG_TYPE_CP_IBUS_OCP,
	PLUG_TYPE_CP_VBAT_OVP,
	PLUG_TYPE_CP_IBAT_OCP,
	PLUG_TYPE_CP_VAC_OVP,
	PLUG_TYPE_ANTI_BURN_TRIGGERED,
	PLUG_TYPE_SOC_NOT_FULL,
	PLUG_TYPE_SMART_ENDURANCE_TRIGGERED,
	PLUG_TYPE_SMART_NAVIGATION_TRIGGERED,
	PLUG_TYPE_BATTERY_MISSING,
	PLUG_TYPE_CP_TDIE_HOT,
	PLUG_TYPE_VBUS_UVLO,
	PLUG_TYPE_LOW_TEMP_DISCHARGING,
	PLUG_TYPE_HIGH_TEMP_DISCHARGING,
	PLUG_TYPE_DUAL_BATTERY_MISSING,
	PLUG_TYPE_SMART_ENDURANCE_SOC_ERR,
	PLUG_TYPE_SMART_NAVIGATION_SOC_ERR,
	PLUG_TYPE_BATTERY_AUTH_FAIL,
	PLUG_TYPE_DUAL_BATTERY_AUTH_FAIL,
	PLUG_TYPE_ANTIBURN_ERR,
	PLUG_TYPE_WLS_FASTCHG_FAIL,
	PLUG_TYPE_WLS_FOD_LOW_POWER,
	PLUG_TYPE_WLS_RX_OTP,
	PLUG_TYPE_WLS_RX_OVP,
	PLUG_TYPE_WLS_RX_OCP,
	PLUG_TYPE_WLS_TRX_FOD,
	PLUG_TYPE_WLS_TRX_OCP,
	PLUG_TYPE_WLS_TRX_UVLO,
	PLUG_TYPE_WLS_TRX_IIC_ERR,
	PLUG_TYPE_WLS_RX_IIC_ERR,
	PLUG_TYPE_LOAD_SWITCH_IIC_ERR,
	PLUG_TYPE_WLS_FW_UPGRADE_ERR,
	PLUG_TYPE_CHARGE_SLOWLY,
	PLUG_TYPE_NON_STANDARD_CHARGER,
	PLUG_TYPE_BATTERY_OCD,
	PLUG_TYPE_BATTERY_CUV,
	PLUG_TYPE_BATTERY_SCD,
	PLUG_TYPE_RM_FCC_ERR,
	PLUG_TYPE_RECHG_SOC_ERR,
	PLUG_TYPE_NOT_STOP_CHG_ERR,
	PLUG_TYPE_CHG_BROKEN,
	PLUG_TYPE_BATTERY_COV,
	PLUG_TYPE_BATTERY_OCC,
	PLUG_TYPE_ISP_LEVEL_ONE,
	PLUG_TYPE_ISP_LEVEL_TWO,
	PLUG_TYPE_ISP_LEVEL_THREE,
	PLUG_TYPE_CSD_FLAG,
	PLUG_TYPE_SOC_JUMP,
	PLUG_TYPE_VBAT_MEAN_SHUTDOWN,
	PLUG_TYPE_BATTERY_OVER_VOLTAGE,
	PLUG_TYPE_MAX_NUM,
};

enum mievent_time_type {
	TIME_TYPE_BATTERY_CYCLECOUNT,
	TIME_TYPE_FG_IIC_ERR,
	TIME_TYPE_CP_ABSENT,
	TIME_TYPE_VBATT_SOC_NOT_MATCH,
	TIME_TYPE_BATTERY_TEMP_HOT,
	TIME_TYPE_BATTERY_TEMP_COLD,
	TIME_TYPE_BATTERY_VOLTAGE_DIFFER,
	TIME_TYPE_WLS_MAG_CASE_ATTACH,
	TIME_TYPE_MAX_NUM,
};

/* table positions: plug events first, then the time events (the shared header still has the OS3 numbering) */
#define MIEVENT_EVENT_NUM	(PLUG_TYPE_MAX_NUM + TIME_TYPE_MAX_NUM)

static struct charge_mievent_info g_charge_mievent_info[MIEVENT_EVENT_NUM] = {
	/* charger mievent upload type for plug*/
	{
		.event_code = MIEVENT_CODE_PD_AUTH_FAILED,
		.event_type = "chgErrInfo",
		.event_describe = "PdAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_PD_AUTH_FAILED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"adapterId"},
	},
	{
		.event_code = MIEVENT_CODE_CP_OPEN_FAILED,
		.event_type = "chgErrInfo",
		.event_describe = "CpEnFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_OPEN_FAILED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"cpId"},
	},
	{
		.event_code = MIEVENT_CODE_NOT_STANDARD_ADAPTER,
		.event_type = "chgErrInfo",
		.event_describe = "NoneStandartChg",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_NOT_STANDARD_ADAPTER,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_RP_SHORT_VBUS_DETECTED,
		.event_type = "chgErrInfo",
		.event_describe = "CorrosionDischarge",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_RP_SHORT_VBUS_DETECTED,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_LPD_DETECTED,
		.event_type = "chgErrInfo",
		.event_describe = "LpdDetected",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_LPD_DETECTED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"lpdFlag"},
	},
	{
		.event_code = MIEVENT_CODE_CP_VBUS_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVbusOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VBUS_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_CP_IBUS_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "CpIbusOcp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_IBUS_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_CP_VBAT_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVbatOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VBAT_OVP,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"vbat", "vbatMax"},
	},
	{
		.event_code = MIEVENT_CODE_CP_IBAT_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "CpIbatOcp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_IBAT_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_CP_VAC_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "CpVacOvp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_VAC_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_ANTI_BURN_TRIGGERED,
		.event_type = "chgStatInfo",
		.event_describe = "AntiBurnTirg",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ANTI_BURN_TRIGGERED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"tconn"},
	},
	{
		.event_code = MIEVENT_CODE_SOC_NOT_FULL,
		.event_type = "chgErrInfo",
		.event_describe = "SocNotFull",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SOC_NOT_FULL,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = {"vbat", "soc", "rsoc"},
	},
	{
		.event_code = MIEVENT_CODE_SMART_ENDURANCE_TRIGGERED,
		.event_type = "chgStatInfo",
		.event_describe = "SmartEnduraTrig",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_ENDURANCE_TRIGGERED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"soc"},
	},
	{
		.event_code = MIEVENT_CODE_SMART_NAVIGATION_TRIGGERED,
		.event_type = "chgStatInfo",
		.event_describe = "SmartNaviTrig",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_NAVIGATION_TRIGGERED,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"soc"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_MISSING,
		.event_type = "chgErrInfo",
		.event_describe = "BattLinkerAbsent",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_MISSING,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_CP_TDIE_HOT,
		.event_type = "chgStatInfo",
		.event_describe = "CpTdieHot",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CP_TDIE_HOT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"masterTdie", "slaveTdie"},
	},
	{
		.event_code = MIEVENT_CODE_VBUS_UVLO,
		.event_type = "chgErrInfo",
		.event_describe = "VbusUvlo",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_VBUS_UVLO,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = {"vbat", "vbus", "aicl"},
	},
	{
		.event_code = MIEVENT_CODE_LOW_TEMP_DISCHARGING,
		.event_type = "chgErrInfo",
		.event_describe = "NotChgInLowTemp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_LOW_TEMP_DISCHARGING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"tbat"},
	},
	{
		.event_code = MIEVENT_CODE_HIGH_TEMP_DISCHARGING,
		.event_type = "chgErrInfo",
		.event_describe = "NotChgInHighTemp",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_HIGH_TEMP_DISCHARGING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"tbat"},
	},
	{
		.event_code = MIEVENT_CODE_DUAL_BATTERY_MISSING,
		.event_type = "chgErrInfo",
		.event_describe = "DualBattLinkerAbsent",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_DUAL_BATTERY_MISSING,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"status"},
	},
	{
		.event_code = MIEVENT_CODE_SMART_ENDURANCE_SOC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "SmartEnduraSocErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_ENDURANCE_SOC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"soc"},
	},
	{
		.event_code = MIEVENT_CODE_SMART_NAVIGATION_SOC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "SmartNaviSocErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SMART_NAVIGATION_SOC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"soc"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_AUTH_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "BattAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_AUTH_FAIL,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_DUAL_BATTERY_AUTH_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "ChgBattAuthFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_DUAL_BATTERY_AUTH_FAIL,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"status"},
	},
	{
		.event_code = MIEVENT_CODE_ANTIBURN_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "AntiFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ANTIBURN_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_FASTCHG_FAIL,
		.event_type = "chgErrInfo",
		.event_describe = "WlsFastChgFail",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FASTCHG_FAIL,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_FOD_LOW_POWER,
		.event_type = "chgErrInfo",
		.event_describe = "WlsQLow",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FOD_LOW_POWER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"chgQbase", "chgQreal"},
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OTP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOTP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OTP,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"chgRxtemp"},
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OVP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOVP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OVP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxOCP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_FOD,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxFod",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_FOD,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_OCP,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxOCP",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_OCP,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_UVLO,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxUVLO",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_UVLO,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_TRX_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "WlsTrxI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_TRX_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_RX_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "WlsRxI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_RX_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_LOAD_SWITCH_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "LoadSwitchI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_LOAD_SWITCH_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_WLS_FW_UPGRADE_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "WlsFwUpgradeErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_WLS_FW_UPGRADE_ERR,
		.data_type = MIEVENT_DATA_TYPE_STRING,
		.data_count = 1,
		.para_name = {"errReason"},
	},
	{
		.event_code = MIEVENT_CODE_CHARGE_SLOWLY,
		.event_type = "chgStatInfo",
		.event_describe = "ChargeSlowly",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CHARGE_SLOWLY,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 5,
		.para_name = {"maxPower", "initialRm", "endRm", "initialSoc", "endSoc"},
	},
	{
		.event_code = MIEVENT_CODE_NON_STANDARD_CHARGER,
		.event_type = "chgStatInfo",
		.event_describe = "NonStandardCharger",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_NON_STANDARD_CHARGER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"chgType"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_OCD,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryOcd",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_OCD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 7,
		.para_name = {"ocdCount", "ocdLastCycle", "hocdCount", "hocdLastCycle", "uiSoc", "voltage", "temp"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_CUV,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryCuv",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_CUV,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 7,
		.para_name = {"cuvCount", "cuvLastCycle", "hcuvCount", "hcuvLastCycle", "uiSoc", "voltage", "temp"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_SCD,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryScd",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_SCD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"hscdCount", "hscdLastCycle"},
	},
	{
		.event_code = MIEVENT_CODE_RM_FCC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "rmFccErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_RM_FCC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 8,
		.para_name = {"rsoc", "uiSoc", "vbat", "temp", "rm", "lastRm", "fcc", "lastFcc"},
	},
	{
		.event_code = MIEVENT_CODE_RECHG_SOC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "RechgSocErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_RECHG_SOC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = {"soc", "vbat", "tbat", "cycleCnt"},
	},
	{
		.event_code = MIEVENT_CODE_NOT_STOP_CHG_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "NotStopChgErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_NOT_STOP_CHG_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = {"current", "vbat", "tbat", "cycleCnt"},
	},
	{
		.event_code = MIEVENT_CODE_CHG_BROKEN,
		.event_type = "chgErrInfo",
		.event_describe = "ChgBroken",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CHG_BROKEN,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_COV,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryCov",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_COV,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 8,
		.para_name = {"covCount", "covLastCycle", "hcovCount", "hcovLastCycle", "uiSoc", "voltage", "current", "temp"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_OCC,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryOcc",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_OCC,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 8,
		.para_name = {"occCount", "occLastCycle", "hoccCount", "hoccLastCycle", "uiSoc", "voltage", "current", "temp"},
	},
	{
		.event_code = MIEVENT_CODE_ISP_LEVEL_ONE,
		.event_type = "chgStatInfo",
		.event_describe = "IspLevelOne",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ISP_LEVEL_ONE,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"ispLevel"},
	},
	{
		.event_code = MIEVENT_CODE_ISP_LEVEL_TWO,
		.event_type = "chgStatInfo",
		.event_describe = "IspLevelTwo",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ISP_LEVEL_TWO,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"ispLevel"},
	},
	{
		.event_code = MIEVENT_CODE_ISP_LEVEL_THREE,
		.event_type = "chgStatInfo",
		.event_describe = "IspLevelThree",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_ISP_LEVEL_THREE,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"ispLevel"},
	},
	{
		.event_code = MIEVENT_CODE_CSD_FLAG,
		.event_type = "chgErrInfo",
		.event_describe = "csdFlag",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_CSD_FLAG,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"csdFlag"},
	},
	{
		.event_code = MIEVENT_CODE_SOC_JUMP,
		.event_type = "chgErrInfo",
		.event_describe = "socJump",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_SOC_JUMP,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 10,
		.para_name = {"preUIsoc", "preRsoc", "uisoc", "rsoc", "temp", "vbat", "online", "current", "fcc", "qmax"},
	},
	{
		.event_code = MIEVENT_CODE_VBAT_MEAN_SHUTDOWN,
		.event_type = "chgStatInfo",
		.event_describe = "vbatMeanShutdown",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_VBAT_MEAN_SHUTDOWN,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = {"vbat_mean", "current", "temp"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_OVER_VOLTAGE,
		.event_type = "chgErrInfo",
		.event_describe = "BatteryOverVoltage",
		.upload_type = MIEVENT_UPLOAD_TYPE_PLUG,
		.type_index = PLUG_TYPE_BATTERY_OVER_VOLTAGE,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = {"targerVoltage", "vbat", "cycleCnt", "chgMode"},
	},
	/* charger mievent upload type for time*/
	{
		.event_code = MIEVENT_CODE_BATTERY_CYCLECOUNT,
		.event_type = "chgStatInfo",
		.event_describe = "chgBattCycle",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_CYCLECOUNT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"cycleCnt"},
	},
	{
		.event_code = MIEVENT_CODE_FG_IIC_ERR,
		.event_type = "chgErrInfo",
		.event_describe = "FgI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_FG_IIC_ERR,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 1,
		.para_name = {"soc"},
	},
	{
		.event_code = MIEVENT_CODE_CP_ABSENT,
		.event_type = "chgErrInfo",
		.event_describe = "CpI2cErr",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_CP_ABSENT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"masterOk", "slaveOk"},
	},
	{
		.event_code = MIEVENT_CODE_VBATT_SOC_NOT_MATCH,
		.event_type = "chgErrInfo",
		.event_describe = "VbatSocNotMatch",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_VBATT_SOC_NOT_MATCH,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 3,
		.para_name = {"vbat", "soc", "cycleCnt"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_TEMP_HOT,
		.event_type = "chgStatInfo",
		.event_describe = "TbatHot",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_TEMP_HOT,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = {"tbat", "tbatMax", "isCharging", "tboard"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_TEMP_COLD,
		.event_type = "chgStatInfo",
		.event_describe = "TbatCold",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_TEMP_COLD,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 4,
		.para_name = {"tbat", "tbatMin", "isCharging", "tboard"},
	},
	{
		.event_code = MIEVENT_CODE_BATTERY_VOLTAGE_DIFFER,
		.event_type = "chgErrInfo",
		.event_describe = "DualVbatDiff",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_BATTERY_VOLTAGE_DIFFER,
		.data_type = MIEVENT_DATA_TYPE_INT,
		.data_count = 2,
		.para_name = {"chgBaseBattVol", "chgFlipBattVol"},
	},
	{
		.event_code = MIEVENT_CODE_WLS_MAG_CASE_ATTACH,
		.event_type = "chgStatInfo",
		.event_describe = "WlsMagCaseAttach",
		.upload_type = MIEVENT_UPLOAD_TYPE_TIME,
		.type_index = TIME_TYPE_WLS_MAG_CASE_ATTACH,
		.data_type = MIEVENT_DATA_TYPE_NULL,
		.data_count = 0,
		.para_name = {""},
	},
};

static struct mievent_upload_type_plug g_upload_type_plug_info[PLUG_TYPE_MAX_NUM] = {
	{DEFAULT_MAX_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{LEVEL0_MAX_REPORT_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{LEVEL1_MAX_REPORT_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
	{DEFAULT_MAX_COUNT, 0},
};

static struct mievent_upload_type_time g_upload_type_time_info[TIME_TYPE_MAX_NUM] = {
	{0, MINOR_TIMER_S, 0},
	{0, CRITICAL_TIMER_S, 0},
	{0, CRITICAL_TIMER_S, 0},
	{0, BLOCK_TIMER_S, 0},
	{0, BLOCK_TIMER_S, 0},
	{0, BLOCK_TIMER_S, 0},
	{0, BLOCK_TIMER_S, 0},
	{0, MINOR_TIMER_S, 0},
};

void mca_charge_mievent_report(int event_index, void *data, int size)
{
	struct charge_mievent_info *mievent;
	struct misight_mievent *event = NULL;
	int g_index = 0;
	bool is_report = false;
	time64_t time_now;
	int *param;
	char **string;
	struct mca_event_notify_data n_data = { 0 };

	if (event_index >= MIEVENT_EVENT_NUM || event_index < 0) {
#line 754
		mca_log_err("event_index[%d] invalid", event_index);
		return;
	}

	mievent = &g_charge_mievent_info[event_index];
	if (size != mievent->data_count) {
#line 760
		mca_log_err("data_type is int param size invalid\n");
		return;
	} else {
		if (size && !data) {
#line 764
			mca_log_err("data_type is int param null invalid\n");
			return;
		}
	}

	if (!strcmp(mievent->event_type, "chgErrInfo") &&
		mievent->event_code != MIEVENT_CODE_NOT_STANDARD_ADAPTER)
#line 770
		__mca_log_err("[ARCH-TF-CHARGER]" "[" MCA_LOG_TAG "]%s:%d %s %s\n", __func__, __LINE__,
			mievent->event_type, mievent->event_describe);

	g_index = mievent->type_index;
	if (mievent->upload_type == MIEVENT_UPLOAD_TYPE_PLUG) {
		if (g_upload_type_plug_info[g_index].count < g_upload_type_plug_info[g_index].max_count) {
			is_report = true;
#line 776
			mca_log_err("event_code[%d][%s] must report fault\n", mievent->event_code, mievent->event_describe);
		}
		g_upload_type_plug_info[g_index].count++;
	} else {
		time_now = ktime_get_boottime_seconds();
		if (!g_upload_type_time_info[g_index].count ||
			time_now - g_upload_type_time_info[g_index].time_last >=
			g_upload_type_time_info[g_index].time_interval) {
			is_report = true;
#line 784
			mca_log_err("event_code[%d] must report fault\n", mievent->event_code, mievent->event_describe);
			g_upload_type_time_info[g_index].time_last = time_now;
		}
		g_upload_type_time_info[g_index].count++;
	}

	if (is_report) {
		n_data.event = "MCA_LOG_FULL_EVENT";
		n_data.event_len = 18;
		mca_event_report_uevent(&n_data);

		event = cdev_tevent_alloc(mievent->event_code);
		if (!event) {
#line 797
			mca_log_err("cdev_tevent_alloc failed");
			return;
		}

		cdev_tevent_add_str(event, mievent->event_type, mievent->event_describe);
#line 802
		mca_log_err("[%d] [%s] [%s]\n", mievent->event_code, mievent->event_type, mievent->event_describe);
		switch (mievent->data_type) {
		case MIEVENT_DATA_TYPE_INT:
			param = (int *)data;
			for (int i = 0; i < mievent->data_count; i++) {
				cdev_tevent_add_int(event, mievent->para_name[i], param[i]);
#line 808
				mca_log_err("[%s] [%d]\n", mievent->para_name[i], param[i]);
			}
			break;
		case MIEVENT_DATA_TYPE_STRING:
			string = (char **)data;
			for (int i = 0; i < mievent->data_count; i++) {
				if (strlen(string[i]) >= MIEVENT_STRING_MAX_LEN) {
#line 815
					mca_log_err("[%s] [%s] String Len overflow\n", mievent->para_name[i], string[i]);
					continue;
				}
				cdev_tevent_add_str(event, mievent->para_name[i], string[i]);
#line 819
				mca_log_err("[%s] [%s]\n", mievent->para_name[i], string[i]);
			}
			break;
		case MIEVENT_DATA_TYPE_NULL:
			break;
		default:
			break;
		}

		cdev_tevent_write(event);
		cdev_tevent_destroy(event);
	}
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_report);

void mca_charge_mievent_set_state(enum charge_mievent_state_ele state, int value)
{
	struct charge_mievent_info *mievent;

	switch (state) {
	case MIEVENT_STATE_PLUG:
		if (value) {
#line 844
			mca_log_info("don't plug out can't reset fault status");
			return;
		}
		for (int i = 0; i < PLUG_TYPE_MAX_NUM; i++)
			g_upload_type_plug_info[i].count = 0;
		break;
	case MIEVENT_STATE_END:
		if (value >= MIEVENT_EVENT_NUM || value < 0) {
#line 852
			mca_log_err("event_index[%d] invalid", value);
			return;
		}
		mievent = &g_charge_mievent_info[value];
		if (mievent->upload_type == MIEVENT_UPLOAD_TYPE_PLUG)
			g_upload_type_plug_info[mievent->type_index].count = 0;
		else if (mievent->upload_type == MIEVENT_UPLOAD_TYPE_TIME) {
			g_upload_type_time_info[mievent->type_index].time_last = 0;
			g_upload_type_time_info[mievent->type_index].count = 0;
		}
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(mca_charge_mievent_set_state);

MODULE_DESCRIPTION("mca charge mievent");
MODULE_AUTHOR("lvxiaofeng@xiaomi.com");
MODULE_LICENSE("GPL v2");
