/* SPDX-License-Identifier: GPL-2.0
 * aw882xx_device.h
 *
 * Copyright (c) 2020 AWINIC Technology CO., LTD
 *
 * Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __AW882XX_DEVICE_FILE_H__
#define __AW882XX_DEVICE_FILE_H__
#include "aw882xx_data_type.h"
#include "aw882xx_calib.h"
#include "aw882xx_monitor.h"
#include "aw882xx_dsp.h"

#define AW_VOLUME_STEP_DB	(6 * 2)
#define AW_REG_NONE		(0xFF)
#define AW_NAME_MAX		(50)
#define ALGO_VERSION_MAX	(80)

#define AW_GET_MIN_VALUE(value1, value2) \
	((value1) > (value2) ? (value2) : (value1))

#define AW_GET_MAX_VALUE(value1, value2)  \
	((value1) > (value2) ? (value1) : (value2))

extern int g_algo_auth_st;

enum {
	AW_1000_US = 1000,
	AW_2000_US = 2000,
	AW_3000_US = 3000,
	AW_4000_US = 4000,
	AW_5000_US = 5000,
	AW_10000_US = 10000,
	AW_32000_US = 32000,
	AW_70000_US = 70000,
	AW_100000_US = 100000,
};

struct aw_device;

enum {
	AW_DEV_TYPE_NONE = 0,
	AW_DEV_TYPE_OK,
};

enum {
	AW_EF_AND_CHECK = 0,
	AW_EF_OR_CHECK,
};

enum {
	AW_DEV_CH_PRI_L = 0,
	AW_DEV_CH_PRI_R = 1,
	AW_DEV_CH_SEC_L = 2,
	AW_DEV_CH_SEC_R = 3,
	AW_DEV_CH_TERT_L = 4,
	AW_DEV_CH_TERT_R = 5,
	AW_DEV_CH_QUAT_L = 6,
	AW_DEV_CH_QUAT_R = 7,
	AW_DEV_CH_MAX,
};

enum AW_DEV_INIT {
	AW_DEV_INIT_ST = 0,
	AW_DEV_INIT_OK = 1,
	AW_DEV_INIT_NG = 2,
};

enum AW_DEV_STATUS {
	AW_DEV_PW_OFF = 0,
	AW_DEV_PW_ON,
};

enum AW_DEV_FW_STATUS {
	AW_DEV_FW_FAILED = 0,
	AW_DEV_FW_OK,
};


enum {
	AW_EXT_DSP_WRITE_NONE = 0,
	AW_EXT_DSP_WRITE,
};

enum AW_SPIN_KCONTROL_STATUS {
	AW_SPIN_KCONTROL_DISABLE = 0,
	AW_SPIN_KCONTROL_ENABLE,
};

enum AW_ALGO_AUTH_MODE {
	AW_ALGO_AUTH_DISABLE = 0,
	AW_ALGO_AUTH_MODE_MAGIC_ID,
	AW_ALGO_AUTH_MODE_REG_CRC,
};

enum AW_ALGO_AUTH_ID {
	AW_ALGO_AUTH_MAGIC_ID = 0x4157,
};

enum AW_ALGO_AUTH_STATUS {
	AW_ALGO_AUTH_WAIT = 0,
	AW_ALGO_AUTH_OK = 1,
};

struct aw_device_ops {
	int (*aw_get_version)(char *buf, int size);
	int (*aw_get_dev_num)(void);
	int (*aw_i2c_write)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int reg_data);
	int (*aw_i2c_read)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int *reg_data);
	int (*aw_i2c_write_bits)(struct i2c_client *i2c, unsigned char reg_addr, unsigned int mask, unsigned int reg_data);
	unsigned int (*aw_reg_val_to_db)(unsigned int value);
	unsigned int (*aw_db_val_to_reg)(unsigned int value);
	unsigned int (*aw_get_irq_type)(struct aw_device *aw_dev, unsigned int value);
	void (*aw_reg_force_set)(struct aw_device *aw_dev);
	int (*aw_frcset_check)(struct aw_device *aw_dev);
	int (*aw_get_voltage_offset)(struct aw_device *aw_dev, int *offset);
};

/* g3s: stock bit-field descriptor {name, reg, mask, enable, disable} (0x18 bytes), see aw_dev_switch() */
struct aw_btn_desc {
	const char *name;
	unsigned int reg;
	unsigned int mask;
	unsigned int enable;
	unsigned int disable;
};

struct aw_int_desc {
	unsigned int mask_reg;			/*interrupt mask reg*/
	unsigned int st_reg;			/*interrupt status reg*/
	unsigned int mask_default;		/*default mask close all*/
	unsigned int int_mask;			/*set mask*/
};

struct aw_work_mode {
	unsigned int reg;
	unsigned int mask;
	unsigned int spk_val;
	unsigned int rcv_val;
};

struct aw_soft_rst {
	int reg;
	int reg_value;
};

#define aw_pwd_desc aw_btn_desc

#define aw_amppd_desc aw_btn_desc

#define aw_bop_desc aw_btn_desc

struct aw_vcalb_desc {
	unsigned int icalk_reg;
	unsigned int icalk_reg_mask;
	unsigned int icalk_shift;
	unsigned int icalkl_reg;
	unsigned int icalkl_reg_mask;
	unsigned int icalkl_shift;
	unsigned int icalk_sign_mask;
	unsigned int icalk_neg_mask;
	int icalk_value_factor;

	unsigned int vcalk_reg;
	unsigned int vcalk_reg_mask;
	unsigned int vcalk_shift;
	unsigned int vcalkl_reg;
	unsigned int vcalkl_reg_mask;
	unsigned int vcalkl_shift;
	unsigned int vcalk_sign_mask;
	unsigned int vcalk_neg_mask;
	int vcalk_value_factor;

	unsigned int vcalb_reg;
	int cabl_base_value;
	int vcal_factor;
};

#define aw_mute_desc aw_btn_desc

#define aw_uls_hmute_desc aw_btn_desc

struct aw_txen_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int enable;
	unsigned int disable;
	unsigned int reserve_val;
};

struct aw_sysst_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int st_check;
	unsigned int st_sws_check;
	unsigned int pll_check;
};

struct aw_profctrl_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int spk_mode;
	unsigned int cfg_prof_mode;
};

struct aw_bstctrl_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int frc_bst;
	unsigned int tsp_type;
	unsigned int cfg_bst_type;
};

struct aw_cco_mux_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int divided_val;
	unsigned int bypass_val;
};

struct aw_volume_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int shift;
	int init_volume;
	int mute_volume;
	int ctl_volume;
	int monitor_volume;
};

struct aw_voltage_desc {
	unsigned int reg;
	unsigned int vbat_range;
	unsigned int int_bit;
};

struct aw_temperature_desc {
	unsigned int reg;
	unsigned int sign_mask;
	unsigned int neg_mask;
};

struct aw_ipeak_desc {
	unsigned int reg;
	unsigned int mask;
};

struct aw_spin_ch {
	uint16_t rx_val;
};

struct aw_reg_ch {
	unsigned int reg;
	unsigned int mask;
	unsigned int left_val;
	unsigned int right_val;
};

struct aw_spin_desc {
	int aw_spin_kcontrol_st;
	struct aw_spin_ch spin_table[AW_SPIN_MAX];
	struct aw_reg_ch rx_desc;
};

struct aw_efcheck_desc {
	unsigned int reg;
	unsigned int mask;
	unsigned int and_val;
	unsigned int or_val;
};

/* g3l: "ef lock" check of the stock (aw882xx_dev_check_ef_lock): up to 2 {reg, mask, value} items */
struct aw_ef_lock_item {
	unsigned int reg;
	unsigned int mask;
	unsigned int value;
};

struct aw_ef_lock_desc {
	unsigned int num;
	struct aw_ef_lock_item item[2];
};

#define aw_dither_desc aw_btn_desc

#define aw_noise_gate_desc aw_btn_desc

#define aw_psm_desc aw_btn_desc

#define aw_mpd_desc aw_btn_desc

#define aw_dsmzth_desc aw_btn_desc

struct aw_auth_desc {
	uint8_t reg_in;
	uint8_t reg_out;
	int32_t auth_mode;
	int32_t reg_crc;
	int32_t random;
	int32_t chip_id;
	int32_t check_result;
};

struct algo_auth_data {
	int32_t auth_mode;  /* 0: disable  1 : chip ID  2 : reg crc */
	int32_t reg_crc;
	int32_t random;
	int32_t chip_id;
	int32_t check_result;
};

#define AW_IOCTL_MAGIC_S			'w'
#define AW_IOCTL_GET_ALGO_AUTH			_IOWR(AW_IOCTL_MAGIC_S, 1, struct algo_auth_data)
#define AW_IOCTL_SET_ALGO_AUTH			_IOWR(AW_IOCTL_MAGIC_S, 2, struct algo_auth_data)


struct aw_device {
	int status;				/* +0x00 */
	unsigned int chip_id;			/* +0x04 */
	unsigned int monitor_start;		/* +0x08 */
	int frcset_en;				/* +0x0c */
	int bop_en;				/* +0x10 */
	int efuse_check;			/* +0x14 */
	int fade_en;				/* +0x18 */
	int reg_max;				/* +0x1c */
	unsigned int mute_st;			/* +0x20 */
	unsigned int amppd_st;			/* +0x24 */
	unsigned int dither_st;			/* +0x28 */
	unsigned int txen_st;			/* +0x2c */

	unsigned char cur_prof;			/* +0x30 current profile index */
	unsigned char set_prof;			/* +0x31 set profile index */
	unsigned int channel;			/* +0x34 pa channel select */
	unsigned int vol_step;			/* +0x38 */
	unsigned int re_max;			/* +0x3c */
	unsigned int re_min;			/* +0x40 */
	int voltage_offset_debug;		/* +0x44 */
	int32_t voltage_offset;			/* +0x48 */
	bool pll_check_disable;			/* +0x4c g3r: DT "pll-check-disable" */

	struct device *dev;			/* +0x50 */
	struct i2c_client *i2c;			/* +0x58 */
	char monitor_name[AW_NAME_MAX];		/* +0x60 */
	struct workqueue_struct *work_queue;	/* +0x98: g3u: the stock keeps the (monitor/fw) workqueue here, set in codec_probe */

	struct aw_ef_lock_desc ef_lock_desc;	/* +0xa0 */
	struct aw_efcheck_desc efcheck_desc;	/* +0xbc */
	struct aw_profctrl_desc profctrl_desc;	/* +0xcc */
	struct aw_bstctrl_desc bstctrl_desc;	/* +0xdc */
	struct aw_int_desc int_desc;		/* +0xf0 */
	struct aw_work_mode work_mode;		/* +0x100 */
	struct aw_pwd_desc pwd_desc;		/* +0x110 */
	struct aw_amppd_desc amppd_desc;	/* +0x128 */
	struct aw_mute_desc mute_desc;		/* +0x140 (hmute) */
	struct aw_uls_hmute_desc uls_hmute_desc;/* +0x158 */
	struct aw_txen_desc txen_desc;		/* +0x170 */
	struct aw_vcalb_desc vcalb_desc;	/* +0x184 */
	struct aw_sysst_desc sysst_desc;	/* +0x1d8 */
	struct aw_cco_mux_desc cco_mux_desc;	/* +0x1ec */
	struct aw_voltage_desc voltage_desc;	/* +0x1fc */
	struct aw_temperature_desc temp_desc;	/* +0x208 */
	struct aw_ipeak_desc ipeak_desc;	/* +0x214 */
	struct aw_volume_desc volume_desc;	/* +0x21c */
	struct aw_prof_info prof_info;		/* +0x238 */
	struct aw_cali_desc cali_desc;		/* +0x250 */
	struct aw_monitor_desc monitor_desc;	/* +0x268 */
	struct aw_soft_rst soft_rst;		/* +0x388 */
	struct aw_spin_desc spin_desc;		/* +0x390 */
	struct aw_bop_desc bop_desc;		/* +0x3b0 */
	struct aw_dither_desc dither_desc;	/* +0x3c8 */
	struct aw_noise_gate_desc noise_gate_desc;	/* +0x3e0 */
	struct aw_psm_desc psm_desc;		/* +0x3f8 */
	struct aw_mpd_desc mpd_desc;		/* +0x410 */
	struct aw_dsmzth_desc dsmzth_desc;	/* +0x428 */
	struct aw_auth_desc auth_desc;		/* +0x440 */
	struct aw_device_ops ops;		/* +0x458 */
	struct list_head list_node;		/* +0x4b0 */
};

#define AW_DEV_OFF_CHECK(f, o) static_assert(offsetof(struct aw_device, f) == (o), "aw_device." #f)
AW_DEV_OFF_CHECK(pll_check_disable, 0x4c);
AW_DEV_OFF_CHECK(monitor_name, 0x60);
AW_DEV_OFF_CHECK(ef_lock_desc, 0xa0);
AW_DEV_OFF_CHECK(profctrl_desc, 0xcc);
AW_DEV_OFF_CHECK(bstctrl_desc, 0xdc);
AW_DEV_OFF_CHECK(int_desc, 0xf0);
AW_DEV_OFF_CHECK(pwd_desc, 0x110);
AW_DEV_OFF_CHECK(txen_desc, 0x170);
AW_DEV_OFF_CHECK(vcalb_desc, 0x184);
AW_DEV_OFF_CHECK(sysst_desc, 0x1d8);
AW_DEV_OFF_CHECK(cco_mux_desc, 0x1ec);
AW_DEV_OFF_CHECK(volume_desc, 0x21c);
AW_DEV_OFF_CHECK(prof_info, 0x238);
AW_DEV_OFF_CHECK(cali_desc, 0x250);
AW_DEV_OFF_CHECK(monitor_desc, 0x268);
AW_DEV_OFF_CHECK(soft_rst, 0x388);
AW_DEV_OFF_CHECK(spin_desc, 0x390);
AW_DEV_OFF_CHECK(bop_desc, 0x3b0);
AW_DEV_OFF_CHECK(dsmzth_desc, 0x428);
AW_DEV_OFF_CHECK(auth_desc, 0x440);
AW_DEV_OFF_CHECK(ops, 0x458);
AW_DEV_OFF_CHECK(list_node, 0x4b0);
static_assert(sizeof(struct aw_device) == 0x4c0, "sizeof(struct aw_device)");

#define AW_REG_RD_ACCESS	(1 << 0)
#define AW_REG_WR_ACCESS	(1 << 1)

static inline bool aw_dev_reg_rd_access(struct aw_device *aw_dev, int reg)
{
	if (reg >= aw_dev->reg_max)
		return false;

	return true;	/* g3r: the stock has no access tables, it reads registers 0 .. reg_max - 1 */
}



void aw882xx_dev_deinit(struct aw_device *aw_dev);
int aw882xx_dev_init(struct aw_device *aw_dev, struct aw_container *aw_cfg);
int aw882xx_device_start(struct aw_device *aw_dev);
int aw882xx_device_stop(struct aw_device *aw_dev);
int aw882xx_dev_reg_update(struct aw_device *aw_dev, bool force);
int aw882xx_dev_irq_reinit(struct aw_device *aw_dev);


/*re*/
int aw882xx_dev_get_cali_re(struct aw_device *aw_dev, int32_t *cali_re);
int aw882xx_dev_init_cali_re(struct aw_device *aw_dev);
int aw882xx_dev_dc_status(struct aw_device *aw_dev);

/*interrupt*/
int aw882xx_dev_status(struct aw_device *aw_dev);
int aw882xx_dev_get_int_status(struct aw_device *aw_dev, uint16_t *int_status);
void aw882xx_dev_clear_int_status(struct aw_device *aw_dev);
int aw882xx_dev_set_intmask(struct aw_device *aw_dev, bool flag);

/*fade int / out*/
void aw882xx_dev_set_fade_vol_step(struct aw_device *aw_dev, unsigned int step);
int aw882xx_dev_get_fade_vol_step(struct aw_device *aw_dev);
void aw882xx_dev_get_fade_time(unsigned int *time, bool fade_in);
void aw882xx_dev_set_fade_time(unsigned int time, bool fade_in);

/*dsp kcontrol*/

int aw882xx_dev_probe(struct aw_device *aw_dev);
int aw882xx_dev_remove(struct aw_device *aw_dev);
int aw882xx_dev_get_list_head(struct list_head **head);

int aw882xx_dev_set_volume(struct aw_device *aw_dev, unsigned int set_vol);
int aw882xx_dev_get_volume(struct aw_device *aw_dev, unsigned int *get_vol);
void aw882xx_dev_mute(struct aw_device *aw_dev, bool mute);


void aw882xx_dev_monitor_hal_get_time(struct aw_device *aw_dev, uint32_t *time);
void aw882xx_dev_monitor_hal_work(struct aw_device *aw_dev, uint32_t *vmax);

int aw882xx_dev_algo_auth_set(struct aw_device *aw_dev, struct algo_auth_data *algo_data);
int aw882xx_dev_algo_auth_get(struct aw_device *aw_dev, struct algo_auth_data *algo_data);
int aw882xx_dev_check_ef_lock(struct aw_device *aw_dev);
void aw882xx_dev_iv_forbidden_output(struct aw_device *aw_dev, bool power_waste);

#endif

