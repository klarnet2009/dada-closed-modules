/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sc8581.h
 *
 * charge-pump ic driver
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

#ifndef __SC8581_H__
#define __SC8581_H__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/version.h>
#include <linux/regmap.h>

#define SC8581_ROLE_MASTER		0
#define SC8581_ROLE_SLAVE		1
#define ERROR_RECOVERY_COUNT	5

#define USB_OVP_TH_4TO1	 22000
#define USB_OVP_TH_2TO1	 14000
#define USB_OVP_TH_1TO1	 6500
#define BUS_OVP_TH_1TO1	 6000
#define BAT_OCP_TH 8000
#define WPC_OVP_TH  22000
#define OUT_OVP_TH 5000
#define PMID2OUT_UVP_TH 100
#define PMID2OUT_OVP_TH 600
#define BAT_OVP_TH 4500
#define BAT_OVP_ALARM_TH 4400
#define CP_DEFAULT_FSW 600

enum  sc8581_adc_ch {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VUSB,
	ADC_VWPC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TBAT,
	ADC_TDIE,
	ADC_MAX_NUM,
};

enum cp_number {
	SC8581_MASTER,
	SC8581_SLAVE,
};

enum cp_model {
	SC85XX_UNKNOWN,
	SC8561,
	SC8581,
	SC8585,
};

enum chg_mode {
	CP_MODE_DIV4,
	CP_MODE_DIV2,
	CP_MODE_DIV1,
	CP_MODE_DIV_MAX,
};

struct sc8581_cfg {
	unsigned int bat_ovp_th;
	unsigned int bat_ovp_alarm_th;
	unsigned int wpc_ovp_th;
	unsigned int out_ovp_th;
	unsigned int pmid2out_uvp_th;
	unsigned int pmid2out_ovp_th;
	unsigned int bus_ovp_th[CP_MODE_DIV_MAX];
	unsigned int usb_ovp_th[CP_MODE_DIV_MAX];
	unsigned int bus_ocp_th[CP_MODE_DIV_MAX];

};

struct sc8581_fsw_cfg {
	int max;
	int min;
	int step;
};

struct sc8581_device {
	struct i2c_client *client;
	struct device *dev;
	struct device *sysfs_dev;
	struct charger_device *chg_dev;
	struct power_supply *cp_psy;
	struct power_supply_desc psy_desc;
	struct regmap *regmap;
	struct sc8581_cfg cfg;
	struct sc8581_fsw_cfg fsw_cfg;
	bool chip_ok;
	char log_tag[25];
	int work_mode;
	int operation_mode;
	int chip_vendor;
	u8 adc_mode;
	unsigned int revision;
	unsigned int product_cfg;
	int cp_role;

	struct delayed_work irq_handle_work;
	int irq_gpio;
	int irq;
	int nlpm_gpio;
	bool ovpgate_en;
	bool i2c_is_working;
	bool vout2out_uvp_fix;
};

union cp_propval {
	unsigned int uintval;
	int intval;
	char strval[PAGE_SIZE];
};

extern int subpmic_dev_notify(int event, bool plug);
extern int register_subpmic_device_notifier(struct notifier_block *nb);
#endif /* __SC8581_H__ */