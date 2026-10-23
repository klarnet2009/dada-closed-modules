/*
 * Copyright (C) 2018, SI-IN, Yun Shi (yun.shi@si-in.com).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifndef _SIPA_COMMOMN_H
#define _SIPA_COMMOMN_H

#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/gameport.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>


#define SIPA_DRIVER_VERSION					("3.1.11-250813")
#define SIPA_MAX_CHANNEL_SUPPORT			(8)

struct sipa_err {
	unsigned long owi_set_mode_cnt;
	unsigned long owi_set_mode_err_cnt;
	unsigned long owi_write_err_cnt;
	unsigned long owi_polarity_err_cnt;
	unsigned long owi_max_retry_cnt;
	unsigned long owi_max_gap;
	unsigned long owi_max_deviation;
	unsigned long owi_write_data_err_cnt;
	unsigned long owi_write_data_cnt;
};

typedef struct sipa_dev_s {
	char name[32];
	unsigned int chip_type;
	struct platform_device *pdev;
	struct i2c_client *client;

	struct workqueue_struct *sia91xx_wq;
	struct delayed_work interrupt_work;
	struct delayed_work monitor_work;
	struct delayed_work fw_load_work;
	int disable_pin;
	int rst_pin;
	int owi_pin;
	int irq_pin;
	int boost_pin;			/* stock +0x1e8 (si,si_pa_boost, chip type 20 only) */
	int id_pin;			/* stock +0x1ec (si,si_pa_id) */

	unsigned int owi_delay_us;	/* stock +0x1f0 (read by sipa_owi_write_mode, pending_actions stores 10) */
	uint32_t owi_mode[6];		/* stock +0x1f4..0x208: owi mode per scene (resume: owi_write_mode(owi_mode[scene])) */
	unsigned int owi_polarity;	/* stock +0x20c: read by sipa_owi_write_mode, pending_actions stores 0 */
	uint32_t g3p_unk210;		/* stock +0x210 [GUESS: 4 bytes, no access found] */
	spinlock_t owi_lock;		/* stock +0x214 (x26 = si_pa + 0x214 in sipa_owi_write_mode) */

	struct regmap *regmap;		/* stock +0x218 */
	unsigned int scene;		/* stock +0x220 */

	uint32_t channel_num;		/* stock +0x224 */
	uint32_t g3l_en_compat;		/* stock +0x228 (en_compatible_type_func) */
	uint32_t g3l_en_irq;		/* stock +0x22c (extra_cfg.dyn_ud_vdd_port, cmd_show prints it as dynamic_updata_vdd_port) */
	uint32_t en_dyn_id;		/* stock +0x230 */
	uint8_t status;			/* stock +0x234 ("Sipa Status") */
	uint8_t fw_load_retry;		/* stock +0x235: retry counter of sipa_fw_load_work_delay (G11) */
	uint8_t g3p_pad236[2];
	uint8_t g3p_pad238[16];		/* stock +0x238..0x247: the template list_head, unused (the stock has no list) */
	uint8_t mute;			/* stock +0x248 ("Sipa Mute") */
	struct sipa_err err_info;	/* stock +0x250 (9 words) */
	uint8_t power_mode;		/* stock +0x298 */
	int spksw_gpio;			/* stock +0x29c */
	int spksw_level;		/* stock +0x2a0 */
	/* sizeof == 0x2a8 as in the stock (kmalloc size in sipa_probe) */
} sipa_dev_t;

/* g3t: layout of the stock object (offsets from the instruction offsets of the stock functions, the module has no BTF) */
#include <linux/stddef.h>
#include <linux/build_bug.h>
static_assert(offsetof(sipa_dev_t, chip_type) == 0x20);
static_assert(offsetof(sipa_dev_t, pdev) == 0x28);
static_assert(offsetof(sipa_dev_t, client) == 0x30);
static_assert(offsetof(sipa_dev_t, sia91xx_wq) == 0x38);
static_assert(offsetof(sipa_dev_t, interrupt_work) == 0x40);
static_assert(offsetof(sipa_dev_t, monitor_work) == 0xc8);
static_assert(offsetof(sipa_dev_t, fw_load_work) == 0x150);
static_assert(offsetof(sipa_dev_t, disable_pin) == 0x1d8);
static_assert(offsetof(sipa_dev_t, boost_pin) == 0x1e8);
static_assert(offsetof(sipa_dev_t, id_pin) == 0x1ec);
static_assert(offsetof(sipa_dev_t, owi_delay_us) == 0x1f0);
static_assert(offsetof(sipa_dev_t, owi_mode) == 0x1f4);
static_assert(offsetof(sipa_dev_t, owi_polarity) == 0x20c);
static_assert(offsetof(sipa_dev_t, owi_lock) == 0x214);
static_assert(offsetof(sipa_dev_t, regmap) == 0x218);
static_assert(offsetof(sipa_dev_t, scene) == 0x220);
static_assert(offsetof(sipa_dev_t, channel_num) == 0x224);
static_assert(offsetof(sipa_dev_t, status) == 0x234);
static_assert(offsetof(sipa_dev_t, mute) == 0x248);
static_assert(offsetof(sipa_dev_t, err_info) == 0x250);
static_assert(offsetof(sipa_dev_t, power_mode) == 0x298);
static_assert(offsetof(sipa_dev_t, spksw_gpio) == 0x29c);
static_assert(offsetof(sipa_dev_t, spksw_level) == 0x2a0);
static_assert(sizeof(sipa_dev_t) == 0x2a8);

struct sipa_chip_compat {
	const uint32_t sub_type;
	struct {
		const uint32_t *chips;
		const uint32_t num;
	};
};

enum {
	SIPA_CHANNEL_0 = 0,
	SIPA_CHANNEL_1,
	SIPA_CHANNEL_2,
	SIPA_CHANNEL_3,
	SIPA_CHANNEL_NUM
};


enum {
	AUDIO_SCENE_PLAYBACK = 0,
	AUDIO_SCENE_VOICE,
	AUDIO_SCENE_VOIP,
	AUDIO_SCENE_RECEIVER,
	AUDIO_SCENE_FACTORY,
	AUDIO_SCENE_FM,
	AUDIO_SCENE_NUM
};

/* g3p: chip type numbering of the stock (index of support_chip_type_name_table, 26 types) */
enum {
	CHIP_TYPE_SIA8101 = 0,
	CHIP_TYPE_SIA8109,
	CHIP_TYPE_SIA8152,
	CHIP_TYPE_SIA8152S,
	CHIP_TYPE_SIA8159,
	CHIP_TYPE_SIA8159A,
	CHIP_TYPE_SIA9195,
	CHIP_TYPE_SIA9196,
	CHIP_TYPE_SIA9175,
	CHIP_TYPE_SIA8001,
	CHIP_TYPE_SIA8102,
	CHIP_TYPE_SIA9177,
	CHIP_TYPE_SIA9255,
	CHIP_TYPE_SIA8100X,
	CHIP_TYPE_SIA81X9,
	CHIP_TYPE_SIA8152X,
	CHIP_TYPE_SIA917X,
	CHIP_TYPE_SIA8150,
	CHIP_TYPE_SIA815T,
	CHIP_TYPE_SIA9187,
	CHIP_TYPE_SIA5118,
	CHIP_TYPE_SIA9197X,
	CHIP_TYPE_SIA8132,
	CHIP_TYPE_SIA5118S,
	CHIP_TYPE_SIA5118X,
	CHIP_TYPE_SIA9306,
	CHIP_TYPE_UNKNOWN = 200,
	CHIP_TYPE_INVALID
};

/* chip type t is <= max and in the bit mask (stock: cmp t,max; lsl 1,t; tst mask) */
#define SIPA_TYPE_IN(t, max, mask)	((t) <= (max) && ((1u << (t)) & (mask)))

#define SIPA_MAX_REG_ADDR					(0xFF)

#define SIA81XX_REG_R_O						(0x00000001)
#define SIA81XX_REG_W_O						(0x00000001 << 1)
#define SIA81XX_REG_RW						(SIA81XX_REG_R_O | SIA81XX_REG_W_O)

#define SIPA_CAL_SPK_DEFAULT_VAL			(0xFFFFFFFF)
#define SIPA_CAL_SPK_OK						(1)
#define SIPA_CAL_SPK_FAIL					(0)


/* error list */
/* pulse width time out */
#define EPTOUT								(100)
/* pulse electrical level opposite with the polarity */
#define EPOLAR								(101)
#define EEXEC								(102)
#define EOUTR								(103)

#define sipa_timer_usr_id_set_vdd(ch)			((ch))
#define sipa_timer_usr_id_spk_cal(ch)			((ch) + SIPA_CHANNEL_NUM)
#define sipa_timer_usr_id_cal_r0(ch)            ((ch) + 2*SIPA_CHANNEL_NUM)
#define sipa_timer_usr_id_set_spk_model(ch)     ((ch) + 3*SIPA_CHANNEL_NUM)
#define sipa_timer_usr_id_get_spk_model(ch)     ((ch) + 4*SIPA_CHANNEL_NUM)
#define sipa_timer_usr_id_close_f0_temp(ch)     ((ch) + 5*SIPA_CHANNEL_NUM)



# if 0
#if defined(SIA91XX_TYPE) && !defined(PLATFORM_TYPE_MTK)
typedef  unsigned short uint16_t;
typedef  unsigned int   uint32_t;
typedef  unsigned char  uint8_t;
#endif
#endif

/* g3l: functions added from the stock disassembly */
int mievent_report(unsigned int eventid, const char *value, struct device *dev);
bool check_smartpa_type(const char *name);
int set_smartpa_type(const char *name, unsigned int type);
int get_smartpa_type(void);
void set_smartpa_codec(struct device *dev, const char *dai_name);
struct snd_kcontrol;
struct snd_ctl_elem_value;
int sipa_read_reg(struct regmap *regmap, unsigned char reg, unsigned int *val);
int sipa_write_reg(struct regmap *regmap, unsigned char reg, unsigned int val);

#endif /* _SIPA_COMMOMN_H */
