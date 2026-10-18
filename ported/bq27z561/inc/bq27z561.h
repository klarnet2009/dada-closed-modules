/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bq27z51.h
 *
 * fuelgauge reg
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
#ifndef __BQ27Z561_H
#define __BQ27Z561_H

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <asm/atomic.h>

enum print_reason {
	PR_INTERRUPT	= BIT(0),
	PR_REGISTER	 = BIT(1),
	PR_OEM		= BIT(2),
	PR_DEBUG	= BIT(3),
};

enum fg_cell_vendor {
	FG_CELL_NAME_A = 65,
	FG_CELL_NAME_B,
	FG_CELL_NAME_C,
	FG_CELL_NAME_D,
	FG_CELL_NAME_E,
	FG_CELL_NAME_F,
	FG_CELL_NAME_G,
	FG_CELL_NAME_H,
	FG_CELL_NAME_I,
	FG_CELL_NAME_J,
	FG_CELL_NAME_L = 76,
};


enum fg_pack_vendor {
	PACK_SUPPLIER_BYD = 0,
	PACK_SUPPLIER_COSLIGHT,
	PACK_SUPPLIER_SUNWODA,
	PACK_SUPPLIER_NVT,
	PACK_SUPPLIER_SCUD,
	PACK_SUPPLIER_TWS,
	PACK_SUPPLIER_LISHEN,
	PACK_SUPPLIER_DESAY,
};

#ifdef CONFIG_FACTORY_BUILD
#define PROBE_CNT_MAX	30
#else
#define PROBE_CNT_MAX	50
#endif

#define	INVALID_REG_ADDR	0xFF

#define FG_FLAGS_CO			BIT(2)
#define FG_FLAGS_FD				BIT(4)
#define	FG_FLAGS_FC				BIT(5)
#define	FG_FLAGS_DSG				BIT(6)
#define FG_FLAGS_RCA				BIT(9)
#define FG_FLAGS_FASTCHAGE			BIT(5)

#define BATTERY_DIGEST_LEN 32
#define BATTERY_RANDOM_LEN 64
#define BATTERY_UNSEAL_KEY_LEN 4

#define DEFUALT_FULL_DESIGN		5000

#define BQ_REPORT_FULL_SOC	9800
#define BQ_CHARGE_FULL_SOC	9750
#define BQ_RECHARGE_SOC		9800
#define BQ_DEFUALT_FULL_SOC		100

#define BATT_NORMAL_H_THRESHOLD		350

#define TI_TERM_CURRENT_L		1100
#define TI_TERM_CURRENT_H		1232

#define NFG_TERM_CURRENT_L		1237
#define NFG_TERM_CURRENT_H		1458

#define BQ27Z561_DEFUALT_TERM		-200
#define BQ27Z561_DEFUALT_FFC_TERM	-680
#define BQ27Z561_DEFUALT_RECHARGE_VOL	4380

#define NO_FFC_OVER_FV	(4464 * 1000)
#define STEP_FV	(16 * 1000)
#define BATT_WARM_THRESHOLD		480

#define PD_CHG_UPDATE_DELAY_US	20	/*20 sec*/
#define BQ_I2C_FAILED_SOC	-107
#define BQ_I2C_FAILED_TEMP	-307
#define DEBUG_CAPATICY		15
#define BMS_FG_VERIFY		"BMS_FG_VERIFY"
#define CC_CV_STEP		"CC_CV_STEP"
#define FCCMAIN_FG_CHARGE_FULL_VOTER		"FCCMAIN_FG_CHARGE_FULL_VOTER"
#define OVER_FV_VOTER	"OVER_FV_VOTER"
#define SMART_BATTERY_FV	"SMART_BATTERY_FV"

#define BQ_PACK_MAXIUM_VOLTAGE_FOR_PMIC			4490
#define BQ_MAXIUM_VOLTAGE_FOR_CELL			4480
#define BQ_PACK_MAXIUM_VOLTAGE_FOR_PMIC_SAFETY	4477

#define FG_ERROR_CHECK_MAX_TIMES 3
#define FG_ERROR_FAKE_TEMP 250
#define FG_FAKE_TEMP_NONE -999

enum bq_fg_reg_idx {
	BQ_FG_REG_CTRL = 0,
	BQ_FG_REG_TEMP,		/* Battery Temperature */
	BQ_FG_REG_VOLT,		/* Battery Voltage */
	BQ_FG_REG_CN,		/* Current Now */
	BQ_FG_REG_AVER_CURRENT,		/* Average Current */
	BQ_FG_REG_BATT_STATUS,	/* BatteryStatus */
	BQ_FG_REG_PACK_BATT,	/* stock BTF name; reg 0x0e from the stock table (what it reads: unverified) */
	BQ_FG_REG_ORIGINAL_TEMP,	/* stock BTF name; reg 0x1e */
	BQ_FG_REG_TTE,		/* Time to Empty */
	BQ_FG_REG_TTF,		/* Time to Full */
	BQ_FG_REG_FCC,		/* Full Charge Capacity */
	BQ_FG_REG_RM,		/* Remaining Capacity */
	BQ_FG_REG_CC,		/* Cycle Count */
	BQ_FG_REG_SOC,		/* Relative State of Charge */
	BQ_FG_REG_SOH,		/* State of Health */
	BQ_FG_REG_CHG_VOL,	/* Charging Voltage*/
	BQ_FG_REG_CHG_CUR,	/* Charging Current*/
	BQ_FG_REG_DC,		/* Design Capacity */
	BQ_FG_REG_ALT_MAC,	/* AltManufactureAccess*/
	BQ_FG_REG_MAC_DATA,	/* MACData*/
	BQ_FG_REG_MAC_CHKSUM,	/* MACChecksum */
	BQ_FG_REG_MAC_DATA_LEN,	/* MACDataLen */
	NVT_FG_REG_OVER_PEAK,	/*over peak flag*/
	NVT_FG_REG_CUR_DEV,		/*current deviation*/
	NVT_FG_REG_POW_DEV,		/*power deviation*/
	NVT_FG_AVE_CUR,		/*10s average current*/
	NVT_FG_AVE_TEMP,		/*10s average tempature*/
	NVT_FG_REG_SOA_L,               /* soa alert */
	NVT_FG_REG_SOA_H,               /* soa alert */
	NVT_FG_REG_ISC,          /* isc alert */
	NVT_FG_REG_START_LEARNING,	/*start learning*/
	NVT_FG_REG_STOP_LEARNING,	/*stop learning*/
	NVT_FG_REG_EST_POWER,	/*Estimated power input*/
	NVT_FG_REG_ACT_POWER,	/*Actual power output*/
	NVT_FG_REG_POWER_DEV,	/*Estimated power-Actual power output*/
	NVT_FG_REG_TIME_DEV,	/*Estimated power time-Actual power time output*/
	NVT_FG_REG_CONST_POWER,	/*Constant power value*/
	NVT_FG_REG_CONST_POWER_TM,	/*Constant power value time*/
	NVT_FG_REG_REF_POWER,	/*Recommended reference power*/
	NVT_FG_REG_REF_CURRENT,	/*nvt recommended discharge current*/
	NVT_FG_REG_NVT_REF_CURRENT,	/*nvt recommended discharge power*/
	NVT_FG_REG_START_LEARNING_B,	/*start learning B*/
	NVT_FG_REG_STOP_LEARNING_B,	/*stop learning B*/
	NVT_FG_REG_EST_POWER_B,	/*Estimated power input B*/
	NVT_FG_REG_ACT_POWER_B,	/*Actual power output B*/
	NVT_FG_REG_POWER_DEV_B,	/*Estimated power-Actual power output B*/
	NUM_REGS,
};
_Static_assert(NUM_REGS == 46, "stock bq27z561_regs is 46 bytes");
_Static_assert(NVT_FG_REG_OVER_PEAK == 22 && NVT_FG_REG_POWER_DEV_B == 45, "stock enum indexes");

static u8 bq27z561_regs[NUM_REGS] = {
	0x00,	/* CONTROL */
	0x06,	/* TEMP */
	0x08,	/* VOLT */
	0x0C,	/* CURRENT NOW */
	0x14,	/* AVG CURRENT */
	0x0A,	/* FLAGS */
	0x0E,	/* PACK_BATT (stock table index 6) */
	0x1E,	/* ORIGINAL_TEMP (stock table index 7) */
	0x16,	/* Time to empty */
	0x18,	/* Time to full */
	0x12,	/* Full charge capacity */
	0x10,	/* Remaining Capacity */
	0x2A,	/* CycleCount */
	0x2C,	/* State of Charge */
	0x2E,	/* State of Health */
	0x30,	/* Charging Voltage*/
	0x32,	/* Charging Current*/
	0x3C,	/* Design Capacity */
	0x3E,	/* AltManufacturerAccess*/
	0x40,	/* MACData*/
	0x60,	/* MACChecksum */
	0x61,	/* MACDataLen */
	0x78,	/*over peak flag*/
	0x79,	/*current deviation*/
	0x7B,	/*power deviation*/
	0x81,	/*10s average current*/
	0x83,	/*10s average tempature*/
	0x70,   /*soa Alert */
	0x71,   /*soa Alert */
	0x72,   /* isc Alert level */
	0x85,	/*start learning*/
	0x86,	/*stop learning*/
	0x87,	/*Estimated power input*/
	0x89,	/*Actual power output*/
	0x8B,	/*Estimated power-Actual power output*/
	0x8D,	/*Estimated power time-Actual power time output*/
	0xA1,	/*Constant power value*/
	0xA3,	/*Constant power value time*/
	0xA5,	/*Recommended reference power*/
	0xA7,	/*nvt recommended discharge current*/
	0xA9,	/*nvt recommended discharge power*/
	0x93,	/*start learning B*/
	0x94,	/*stop learning B*/
	0x95,	/*Estimated power input B*/
	0x97,	/*Actual power output B*/
	0x99,	/*Estimated power-Actual power output B*/
};

enum bq_fg_mac_cmd {
	FG_MAC_CMD_CTRL_STATUS	= 0x0000,
	FG_MAC_CMD_DEV_TYPE	= 0x0001,
	FG_MAC_CMD_FW_VER	= 0x0002,
	FG_MAC_CMD_HW_VER	= 0x0003,
	FG_MAC_CMD_IF_SIG	= 0x0004,
	FG_MCA_CMD_DF	=	0x0005,
	FG_MAC_CMD_CHEM_ID	= 0x0006,
	FG_MAC_CMD_GAUGING	= 0x0021,
	FG_MAC_CMD_SEAL		= 0x0030,
	FG_MAC_CMD_FASTCHARGE_EN = 0x003E,
	FG_MAC_CMD_FASTCHARGE_DIS = 0x003F,
	FG_MAC_CMD_DEV_RESET	= 0x0041,
	FG_MAC_CMD_DEVICE_NAME	= 0x004A,
	FG_MAC_CMD_CHEM_NAME	= 0x004B,
	FG_MAC_CMD_MANU_NAME	= 0x004C,
	FG_MAC_CMD_CUTOFF_VOLTAGE	= 0x0050,
	FG_MCA_CMD_SEAL_STATE	= 0x0054,
	FG_MAC_CMD_CHARGING_STATUS = 0x0055,
	FG_MAC_CMD_GAGUE_STATUS = 0x0056,
	FG_MAC_CMD_LIFETIME1	= 0x0060,
	FG_MAC_CMD_LIFETIME2	= 0x0061,
	FG_MAC_CMD_LIFETIME3	= 0x0062,
	FG_MAC_CMD_CALC_RVALUE = 0x0066,
	FG_MAC_CMD_OVER_VOL_DURATION = 0x0067,
	FG_MAC_CMD_REL_SOH = 0x0069,
	FG_MAC_CMD_BATT_SN = 0x0070,
	FG_MAC_CMD_QMAX_CYCLECOUNT	= 0x0071,
	FG_MAC_CMD_ITSTATUS1	= 0x0073,
	FG_MAC_CMD_QMAX		= 0x0075,
	FG_MAC_CMD_FCC_SOH	= 0x0077,
	FG_MAC_CMD_UI_SOH = 0x007B,
	FG_MAC_CMD_CELL_QMAX = 0x0080,
	FG_MAC_CMD_SREMC_SFCC = 0x0083,
	FG_MAC_CMD_EIS_SOH = 0x009D,
	FG_MAC_CMD_CO_CTRL = 0x00B2,
	FG_MAC_CMD_ENABLE_CO = 0x00C0,
	FG_MAC_CMD_CLOSE_CO = 0x00C9,
	FG_MAC_CMD_OPEN_CO = 0x00CC,
	FG_MAC_CMD_CALIBRATION_INFO_1 = 0x00D1,
	FG_MAC_CMD_CALIBRATION_INFO_2 = 0x00D2,
	FG_MAC_CMD_CALIBRATION_INFO_3 = 0x00D3,
	FG_MAC_CMD_CALIBRATION_INFO_4 = 0x00D4,
	FG_MAC_CMD_CLEAR_COUNT_DATA = 0x1054,
	FG_MAC_CMD_MIXDATARECORD1 = 0x2001,
	FG_MAC_CMD_RA_TABLE	= 0x40C0,
	FG_MAC_CMD_FEATURE_INFO = 0x40E0,
	FG_MAC_CMD_FW_RUNTIME	= 0x4441,
	FG_MAC_CMD_TERM_CURRENTS = 0x456E,
	FG_MAC_CMD_RECORD_VOLTAGE_LEVEL = 0x448A,
	FG_MAC_CMD_UPDATE_VERSION = 0x440C,
};

enum fg_seal_status {
	SEAL_STATE_RSVED,
	SEAL_STATE_FA,
	SEAL_STATE_UNSEALED,
	SEAL_STATE_SEALED,
};

enum bq_fg_device {
	BQ27Z561_MASTER = 0,
	BQ27Z561_SLAVE,
	BQ28Z610,
};

enum fg_device_role {
	FG_MASTER,
	FG_SLAVE,
	FG_THIRD,
	FG_MAX,
};

enum fg_fw_ver {
	FW_DEFAULT,
	FW_R0,
	FW_R1,
};

enum fg_cell_volt {
	CELL_VOLTAGE1,
	CELL_VOLTAGE2,
};

enum fg_cell_sremc_sfcc {
	CELL_SREMC1,
	CELL_SREMC2,
	CELL_SFCC1,
	CELL_SFCC2,
};

enum fg_cell_qmax {
	CELL_QMAX1,
	CELL_QMAX2,
};

#define BQ_ROLE_MASTER		0
#define BQ_ROLE_SLAVE		1

static int bq_mode_data[] = {
	[BQ27Z561_MASTER] = BQ_ROLE_MASTER,
	[BQ27Z561_SLAVE] = BQ_ROLE_SLAVE,
	[BQ28Z610] = BQ_ROLE_MASTER,
};

static const unsigned char *device2str[] = {
	"bq27z561_master",
	"bq27z561_slave",
	"bq27z561",
	"bq28z610",
};
struct cold_thermal {
	int index;
	int temp_l;
	int temp_h;
	int curr_th;
};

#define STEP_TABLE_MAX 3
struct step_config {
	int volt_lim;
	int curr_lim;
};

static struct step_config cc_cv_step_config[STEP_TABLE_MAX] = {
	{4200-3, 8820},
	{4300-3, 7350},
	{4400-3, 5880},
};

enum fg_vendor {
	BQ27Z561 = 0,  // TI
	NFG1000A,  // NVT-eastsoft
	NFG1000B,  // NVT-chipsea
	MPC8011B,
	MPC7012,
	MPC7021,
	BQ30Z55,
	BQ40Z50,
	BQ27Z746,
	TIBQ28Z610,
	MAX1789,
	RAA241200,
	SC58561,
	SC58511,
	SC58520,
	VENDOR_INVALID,
};

#define VOLTAGE_COL_MAX 6
#define VOLTAGE_ROW_MAX 1
struct soh_compesation {
	int soh_delta;
	int soh_comp;
};
_Static_assert(sizeof(struct soh_compesation) == 8, "soh_compesation size");

struct bq_fg_chip {   /* G13l: layout from the stock BTF (1856 bytes) */
	struct device *dev;	/* +0 */
	struct device *sysfs_dev;	/* +8 */
	struct i2c_client *client;	/* +16 */
	struct regmap *regmap;	/* +24 */
	struct device_node *regmap_node;	/* +32 */
	struct platform_device *pdev;	/* +40 */
	struct delayed_work term_voltage_update_work;	/* +48 */
	unsigned int dev_role;	/* +184 */
	int batt_dc;	/* +188 */
	int soh_th;	/* +192 */
	int cycle_th;	/* +196 */
	int cycle_step;	/* +200 */
	int resistance_id;	/* +204 */
	int *dec_rate_seq;	/* +208 */
	int dec_rate_len;	/* +216 */
	int support_nvt1000_ota;	/* +220 */
	struct mutex i2c_rw_lock;	/* +224 */
	struct mutex data_lock;	/* +272 */
	atomic_t pm_suspend;	/* +320 */
	atomic_t digest_in_process;	/* +324 */
	int fw_ver;	/* +328 */
	int df_ver;	/* +332 */
	u8 chip_ok;	/* +336 */
	u8 regs[46];	/* stock BTF: 46 bytes (template NUM_REGS differs) */	/* +337 */
	char device_name[8];	/* +383 */
	char fake_device_name[8];	/* +391 */
	char eeprom_version;	/* +399 */
	bool batt_fc;	/* +400 */
	bool batt_fd;	/* +401 */
	bool batt_dsg;	/* +402 */
	bool batt_rca;	/* +403 */
	bool batt_fc_1;	/* +404 */
	bool batt_fd_1;	/* +405 */
	bool batt_tc_1;	/* +406 */
	bool batt_td_1;	/* +407 */
	bool support_voltage_record_level;	/* +408 */
	bool support_batt_manager_4p0;	/* +409 */
	bool support_version_compatible;	/* +410 */
	u8 version_number;	/* +411 */
	u8 start_byte_address;	/* +412 */
	u8 byte_length;	/* +413 */
	u8 *record_voltage;	/* +416 */
	int seal_state;	/* +424 */
	int last_soc;	/* +428 */
	int batt_id;	/* +432 */
	int Qmax_old;	/* +436 */
	int batt_temp;	/* +440 */
	int batt_volt;	/* +444 */
	int batt_curr;	/* +448 */
	int batt_rm;	/* +452 */
	int batt_fcc;	/* +456 */
	int batt_rsoc;	/* +460 */
	int batt_cycle_count;	/* +464 */
	int batt_soh;	/* +468 */
	int skip_reads;	/* +472 */
	int skip_writes;	/* +476 */
	u8 digest[BATTERY_DIGEST_LEN];	/* +480 */
	bool verify_digest_success;	/* +512 */
	int batt_auth;	/* +516 */
	int constant_charge_current_max;	/* +520 */
	u8 key[BATTERY_UNSEAL_KEY_LEN];	/* +524 */
	bool ignore_digest_for_debug;	/* +528 */
	bool old_hw;	/* +529 */
	bool fg_error;	/* +530 */
	bool fake_usage_date;	/* +531 */
	bool enable_fcc_default;	/* +532 */
	struct delayed_work clound_update_work;	/* +536 */
	u8 clound_fg_fw_update[256];	/* GUESS: type T2112, 256 bytes */	/* +672 */
	struct cold_thermal *cold_thermal_seq;	/* +928 */
	int cold_thermal_len;	/* +936 */
	bool fast_mode;	/* +940 */
	int cell1_max;	/* +944 */
	int max_charge_current;	/* +948 */
	int max_discharge_current;	/* +952 */
	int max_temp_cell;	/* +956 */
	int min_temp_cell;	/* +960 */
	int total_fw_runtime;	/* +964 */
	int time_spent_in_lt;	/* +968 */
	int time_spent_in_ht;	/* +972 */
	int time_spent_in_ot;	/* +976 */
	const char *cell_name;	/* +984 */
	int fg_vendor;	/* +992 */
	int pack_vendor;	/* +996 */
	int fake_cycle;	/* +1000 */
	int fake_temp;	/* +1004 */
	int fake_isc;	/* +1008 */
	int fake_rm;	/* +1012 */
	int fake_fcc;	/* +1016 */
	int adapt_power;	/* +1020 */
	int aged_flag;	/* +1024 */
	unsigned int ui_soh;	/* +1028 */
	int count_level1;	/* +1032 */
	int count_level2;	/* +1036 */
	int count_level3;	/* +1040 */
	int count_lowtemp;	/* +1044 */
	int fake_count_level1;	/* +1048 */
	int fake_count_level2;	/* +1052 */
	int fake_count_level3;	/* +1056 */
	int fake_count_lowtemp;	/* +1060 */
	int update_fw_status;	/* +1064 */
	int fake_soh;	/* +1068 */
	int max_dsg_current;	/* +1072 */
	int min_temp;	/* +1076 */
	int min_voltage;	/* +1080 */
	int cuv_count;	/* +1084 */
	int cuv_last_cycle;	/* +1088 */
	int hcuv_count;	/* +1092 */
	int hcuv_last_cycle;	/* +1096 */
	int ocd_count;	/* +1100 */
	int ocd_last_cycle;	/* +1104 */
	int hocd_count;	/* +1108 */
	int hocd_last_cycle;	/* +1112 */
	int hscd_count;	/* +1116 */
	int hscd_last_cycle;	/* +1120 */
	int cov_count;	/* +1124 */
	int cov_last_cycle;	/* +1128 */
	int hcov_count;	/* +1132 */
	int hcov_last_cycle;	/* +1136 */
	int occ_count;	/* +1140 */
	int occ_last_cycle;	/* +1144 */
	int hocc_count;	/* +1148 */
	int hocc_last_cycle;	/* +1152 */
	bool ota_updating;	/* +1156 */
	unsigned int unk_1160;	/* GUESS-NAME (BTF ?str) */	/* +1160 */
	unsigned int unk_1164;	/* GUESS-NAME (BTF ?str) */	/* +1164 */
	unsigned int update_erase_flag;	/* +1168 */
	unsigned int back_up_data_flag;	/* +1172 */
	u8 backupdata[512];	/* GUESS: type T33075, 512 bytes */	/* +1176 */
	int batt_ori_temp;	/* +1688 */
	int fake_v_siloss;	/* +1692 */
	int fake_i_aver;	/* +1696 */
	int co_by_gpio;	/* +1700 */
	int ctl_co_gpio;	/* +1704 */
	int base_termv;	/* +1708 */
	int base_termv_gl;	/* +1712 */
	int soh_comp_cnt;	/* +1716 */
	struct soh_compesation soh_delta_comp[16];	/* +1720 */
	int fg_device_name;	/* +1848 */
	bool support_new_soc_smooth;	/* +1852 */
	bool is_G2_new_version;	/* +1853 */
};
_Static_assert(sizeof(struct bq_fg_chip) == 1856, "bq_fg_chip size");
_Static_assert(offsetof(struct bq_fg_chip, dev) == 0, "dev");
_Static_assert(offsetof(struct bq_fg_chip, sysfs_dev) == 8, "sysfs_dev");
_Static_assert(offsetof(struct bq_fg_chip, client) == 16, "client");
_Static_assert(offsetof(struct bq_fg_chip, regmap) == 24, "regmap");
_Static_assert(offsetof(struct bq_fg_chip, regmap_node) == 32, "regmap_node");
_Static_assert(offsetof(struct bq_fg_chip, pdev) == 40, "pdev");
_Static_assert(offsetof(struct bq_fg_chip, term_voltage_update_work) == 48, "term_voltage_update_work");
_Static_assert(offsetof(struct bq_fg_chip, dev_role) == 184, "dev_role");
_Static_assert(offsetof(struct bq_fg_chip, batt_dc) == 188, "batt_dc");
_Static_assert(offsetof(struct bq_fg_chip, soh_th) == 192, "soh_th");
_Static_assert(offsetof(struct bq_fg_chip, cycle_th) == 196, "cycle_th");
_Static_assert(offsetof(struct bq_fg_chip, cycle_step) == 200, "cycle_step");
_Static_assert(offsetof(struct bq_fg_chip, resistance_id) == 204, "resistance_id");
_Static_assert(offsetof(struct bq_fg_chip, dec_rate_seq) == 208, "dec_rate_seq");
_Static_assert(offsetof(struct bq_fg_chip, dec_rate_len) == 216, "dec_rate_len");
_Static_assert(offsetof(struct bq_fg_chip, support_nvt1000_ota) == 220, "support_nvt1000_ota");
_Static_assert(offsetof(struct bq_fg_chip, i2c_rw_lock) == 224, "i2c_rw_lock");
_Static_assert(offsetof(struct bq_fg_chip, data_lock) == 272, "data_lock");
_Static_assert(offsetof(struct bq_fg_chip, pm_suspend) == 320, "pm_suspend");
_Static_assert(offsetof(struct bq_fg_chip, digest_in_process) == 324, "digest_in_process");
_Static_assert(offsetof(struct bq_fg_chip, fw_ver) == 328, "fw_ver");
_Static_assert(offsetof(struct bq_fg_chip, df_ver) == 332, "df_ver");
_Static_assert(offsetof(struct bq_fg_chip, chip_ok) == 336, "chip_ok");
_Static_assert(offsetof(struct bq_fg_chip, regs) == 337, "regs");
_Static_assert(offsetof(struct bq_fg_chip, device_name) == 383, "device_name");
_Static_assert(offsetof(struct bq_fg_chip, fake_device_name) == 391, "fake_device_name");
_Static_assert(offsetof(struct bq_fg_chip, eeprom_version) == 399, "eeprom_version");
_Static_assert(offsetof(struct bq_fg_chip, batt_fc) == 400, "batt_fc");
_Static_assert(offsetof(struct bq_fg_chip, batt_fd) == 401, "batt_fd");
_Static_assert(offsetof(struct bq_fg_chip, batt_dsg) == 402, "batt_dsg");
_Static_assert(offsetof(struct bq_fg_chip, batt_rca) == 403, "batt_rca");
_Static_assert(offsetof(struct bq_fg_chip, batt_fc_1) == 404, "batt_fc_1");
_Static_assert(offsetof(struct bq_fg_chip, batt_fd_1) == 405, "batt_fd_1");
_Static_assert(offsetof(struct bq_fg_chip, batt_tc_1) == 406, "batt_tc_1");
_Static_assert(offsetof(struct bq_fg_chip, batt_td_1) == 407, "batt_td_1");
_Static_assert(offsetof(struct bq_fg_chip, support_voltage_record_level) == 408, "support_voltage_record_level");
_Static_assert(offsetof(struct bq_fg_chip, support_batt_manager_4p0) == 409, "support_batt_manager_4p0");
_Static_assert(offsetof(struct bq_fg_chip, support_version_compatible) == 410, "support_version_compatible");
_Static_assert(offsetof(struct bq_fg_chip, version_number) == 411, "version_number");
_Static_assert(offsetof(struct bq_fg_chip, start_byte_address) == 412, "start_byte_address");
_Static_assert(offsetof(struct bq_fg_chip, byte_length) == 413, "byte_length");
_Static_assert(offsetof(struct bq_fg_chip, record_voltage) == 416, "record_voltage");
_Static_assert(offsetof(struct bq_fg_chip, seal_state) == 424, "seal_state");
_Static_assert(offsetof(struct bq_fg_chip, last_soc) == 428, "last_soc");
_Static_assert(offsetof(struct bq_fg_chip, batt_id) == 432, "batt_id");
_Static_assert(offsetof(struct bq_fg_chip, Qmax_old) == 436, "Qmax_old");
_Static_assert(offsetof(struct bq_fg_chip, batt_temp) == 440, "batt_temp");
_Static_assert(offsetof(struct bq_fg_chip, batt_volt) == 444, "batt_volt");
_Static_assert(offsetof(struct bq_fg_chip, batt_curr) == 448, "batt_curr");
_Static_assert(offsetof(struct bq_fg_chip, batt_rm) == 452, "batt_rm");
_Static_assert(offsetof(struct bq_fg_chip, batt_fcc) == 456, "batt_fcc");
_Static_assert(offsetof(struct bq_fg_chip, batt_rsoc) == 460, "batt_rsoc");
_Static_assert(offsetof(struct bq_fg_chip, batt_cycle_count) == 464, "batt_cycle_count");
_Static_assert(offsetof(struct bq_fg_chip, batt_soh) == 468, "batt_soh");
_Static_assert(offsetof(struct bq_fg_chip, skip_reads) == 472, "skip_reads");
_Static_assert(offsetof(struct bq_fg_chip, skip_writes) == 476, "skip_writes");
_Static_assert(offsetof(struct bq_fg_chip, digest) == 480, "digest");
_Static_assert(offsetof(struct bq_fg_chip, verify_digest_success) == 512, "verify_digest_success");
_Static_assert(offsetof(struct bq_fg_chip, batt_auth) == 516, "batt_auth");
_Static_assert(offsetof(struct bq_fg_chip, constant_charge_current_max) == 520, "constant_charge_current_max");
_Static_assert(offsetof(struct bq_fg_chip, key) == 524, "key");
_Static_assert(offsetof(struct bq_fg_chip, ignore_digest_for_debug) == 528, "ignore_digest_for_debug");
_Static_assert(offsetof(struct bq_fg_chip, old_hw) == 529, "old_hw");
_Static_assert(offsetof(struct bq_fg_chip, fg_error) == 530, "fg_error");
_Static_assert(offsetof(struct bq_fg_chip, fake_usage_date) == 531, "fake_usage_date");
_Static_assert(offsetof(struct bq_fg_chip, enable_fcc_default) == 532, "enable_fcc_default");
_Static_assert(offsetof(struct bq_fg_chip, clound_update_work) == 536, "clound_update_work");
_Static_assert(offsetof(struct bq_fg_chip, clound_fg_fw_update) == 672, "clound_fg_fw_update");
_Static_assert(offsetof(struct bq_fg_chip, cold_thermal_seq) == 928, "cold_thermal_seq");
_Static_assert(offsetof(struct bq_fg_chip, cold_thermal_len) == 936, "cold_thermal_len");
_Static_assert(offsetof(struct bq_fg_chip, fast_mode) == 940, "fast_mode");
_Static_assert(offsetof(struct bq_fg_chip, cell1_max) == 944, "cell1_max");
_Static_assert(offsetof(struct bq_fg_chip, max_charge_current) == 948, "max_charge_current");
_Static_assert(offsetof(struct bq_fg_chip, max_discharge_current) == 952, "max_discharge_current");
_Static_assert(offsetof(struct bq_fg_chip, max_temp_cell) == 956, "max_temp_cell");
_Static_assert(offsetof(struct bq_fg_chip, min_temp_cell) == 960, "min_temp_cell");
_Static_assert(offsetof(struct bq_fg_chip, total_fw_runtime) == 964, "total_fw_runtime");
_Static_assert(offsetof(struct bq_fg_chip, time_spent_in_lt) == 968, "time_spent_in_lt");
_Static_assert(offsetof(struct bq_fg_chip, time_spent_in_ht) == 972, "time_spent_in_ht");
_Static_assert(offsetof(struct bq_fg_chip, time_spent_in_ot) == 976, "time_spent_in_ot");
_Static_assert(offsetof(struct bq_fg_chip, cell_name) == 984, "cell_name");
_Static_assert(offsetof(struct bq_fg_chip, fg_vendor) == 992, "fg_vendor");
_Static_assert(offsetof(struct bq_fg_chip, pack_vendor) == 996, "pack_vendor");
_Static_assert(offsetof(struct bq_fg_chip, fake_cycle) == 1000, "fake_cycle");
_Static_assert(offsetof(struct bq_fg_chip, fake_temp) == 1004, "fake_temp");
_Static_assert(offsetof(struct bq_fg_chip, fake_isc) == 1008, "fake_isc");
_Static_assert(offsetof(struct bq_fg_chip, fake_rm) == 1012, "fake_rm");
_Static_assert(offsetof(struct bq_fg_chip, fake_fcc) == 1016, "fake_fcc");
_Static_assert(offsetof(struct bq_fg_chip, adapt_power) == 1020, "adapt_power");
_Static_assert(offsetof(struct bq_fg_chip, aged_flag) == 1024, "aged_flag");
_Static_assert(offsetof(struct bq_fg_chip, ui_soh) == 1028, "ui_soh");
_Static_assert(offsetof(struct bq_fg_chip, count_level1) == 1032, "count_level1");
_Static_assert(offsetof(struct bq_fg_chip, count_level2) == 1036, "count_level2");
_Static_assert(offsetof(struct bq_fg_chip, count_level3) == 1040, "count_level3");
_Static_assert(offsetof(struct bq_fg_chip, count_lowtemp) == 1044, "count_lowtemp");
_Static_assert(offsetof(struct bq_fg_chip, fake_count_level1) == 1048, "fake_count_level1");
_Static_assert(offsetof(struct bq_fg_chip, fake_count_level2) == 1052, "fake_count_level2");
_Static_assert(offsetof(struct bq_fg_chip, fake_count_level3) == 1056, "fake_count_level3");
_Static_assert(offsetof(struct bq_fg_chip, fake_count_lowtemp) == 1060, "fake_count_lowtemp");
_Static_assert(offsetof(struct bq_fg_chip, update_fw_status) == 1064, "update_fw_status");
_Static_assert(offsetof(struct bq_fg_chip, fake_soh) == 1068, "fake_soh");
_Static_assert(offsetof(struct bq_fg_chip, max_dsg_current) == 1072, "max_dsg_current");
_Static_assert(offsetof(struct bq_fg_chip, min_temp) == 1076, "min_temp");
_Static_assert(offsetof(struct bq_fg_chip, min_voltage) == 1080, "min_voltage");
_Static_assert(offsetof(struct bq_fg_chip, cuv_count) == 1084, "cuv_count");
_Static_assert(offsetof(struct bq_fg_chip, cuv_last_cycle) == 1088, "cuv_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, hcuv_count) == 1092, "hcuv_count");
_Static_assert(offsetof(struct bq_fg_chip, hcuv_last_cycle) == 1096, "hcuv_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, ocd_count) == 1100, "ocd_count");
_Static_assert(offsetof(struct bq_fg_chip, ocd_last_cycle) == 1104, "ocd_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, hocd_count) == 1108, "hocd_count");
_Static_assert(offsetof(struct bq_fg_chip, hocd_last_cycle) == 1112, "hocd_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, hscd_count) == 1116, "hscd_count");
_Static_assert(offsetof(struct bq_fg_chip, hscd_last_cycle) == 1120, "hscd_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, cov_count) == 1124, "cov_count");
_Static_assert(offsetof(struct bq_fg_chip, cov_last_cycle) == 1128, "cov_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, hcov_count) == 1132, "hcov_count");
_Static_assert(offsetof(struct bq_fg_chip, hcov_last_cycle) == 1136, "hcov_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, occ_count) == 1140, "occ_count");
_Static_assert(offsetof(struct bq_fg_chip, occ_last_cycle) == 1144, "occ_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, hocc_count) == 1148, "hocc_count");
_Static_assert(offsetof(struct bq_fg_chip, hocc_last_cycle) == 1152, "hocc_last_cycle");
_Static_assert(offsetof(struct bq_fg_chip, ota_updating) == 1156, "ota_updating");
_Static_assert(offsetof(struct bq_fg_chip, unk_1160) == 1160, "unk_1160");
_Static_assert(offsetof(struct bq_fg_chip, unk_1164) == 1164, "unk_1164");
_Static_assert(offsetof(struct bq_fg_chip, update_erase_flag) == 1168, "update_erase_flag");
_Static_assert(offsetof(struct bq_fg_chip, back_up_data_flag) == 1172, "back_up_data_flag");
_Static_assert(offsetof(struct bq_fg_chip, backupdata) == 1176, "backupdata");
_Static_assert(offsetof(struct bq_fg_chip, batt_ori_temp) == 1688, "batt_ori_temp");
_Static_assert(offsetof(struct bq_fg_chip, fake_v_siloss) == 1692, "fake_v_siloss");
_Static_assert(offsetof(struct bq_fg_chip, fake_i_aver) == 1696, "fake_i_aver");
_Static_assert(offsetof(struct bq_fg_chip, co_by_gpio) == 1700, "co_by_gpio");
_Static_assert(offsetof(struct bq_fg_chip, ctl_co_gpio) == 1704, "ctl_co_gpio");
_Static_assert(offsetof(struct bq_fg_chip, base_termv) == 1708, "base_termv");
_Static_assert(offsetof(struct bq_fg_chip, base_termv_gl) == 1712, "base_termv_gl");
_Static_assert(offsetof(struct bq_fg_chip, soh_comp_cnt) == 1716, "soh_comp_cnt");
_Static_assert(offsetof(struct bq_fg_chip, soh_delta_comp) == 1720, "soh_delta_comp");
_Static_assert(offsetof(struct bq_fg_chip, fg_device_name) == 1848, "fg_device_name");
_Static_assert(offsetof(struct bq_fg_chip, support_new_soc_smooth) == 1852, "support_new_soc_smooth");
_Static_assert(offsetof(struct bq_fg_chip, is_G2_new_version) == 1853, "is_G2_new_version");

enum manu_macro {
	TERMINATION = 0,
	RECHARGE_VOL,
	FFC_TERMINATION,
	MANU_NAME,
	MANU_DATA_LEN,
};

enum fw_status_elem {
	FG_FW_INIT,
	FG_FW_ERROR,
	FG_FW_SUCCESS,
	FG_FW_INVALID,

};

#define TERMINATION_BYTE	6
#define TERMINATION_BASE	30
#define TERMINATION_STEP	5

#define RECHARGE_VOL_BYTE	7
#define RECHARGE_VOL_BASE	4200
#define RECHARGE_VOL_STEP	5

#define FFC_TERMINATION_BYTE	8
#define FFC_TERMINATION_BASE	400
#define FFC_TERMINATION_STEP	20

#define MANU_NAME_BYTE		3
#define MANU_NAME_BASE		0x0C
#define MANU_NAME_STEP		1

struct manu_data {
	int byte;
	int base;
	int step;
	int data;
};

static struct manu_data manu_info[MANU_DATA_LEN] = {
	{TERMINATION_BYTE, TERMINATION_BASE, TERMINATION_STEP},
	{RECHARGE_VOL_BYTE, RECHARGE_VOL_BASE, RECHARGE_VOL_STEP},
	{FFC_TERMINATION_BYTE, FFC_TERMINATION_BASE, FFC_TERMINATION_STEP},
	{MANU_NAME, MANU_NAME_BASE, MANU_NAME_STEP},
};

#define SHUTDOWN_DELAY_VOL	3300
#define BQ_RESUME_UPDATE_TIME	600

static const u8 fg_dump_regs[] = {
	0x00, 0x02, 0x04, 0x06,
	0x08, 0x0A, 0x0C, 0x0E,
	0x10, 0x16, 0x18, 0x1A,
	0x1C, 0x1E, 0x20, 0x28,
	0x2A, 0x2C, 0x2E, 0x30,
	0x66, 0x68, 0x6C, 0x6E,
};

struct fg_batt_info {
	int curr;
	int volt;
	int vcell_max;
	int temp;
	int rsoc;
	int rm;
	int fcc;
	int cycle_count;
	int soh;
};

#endif /* __BQ27Z561_H */

