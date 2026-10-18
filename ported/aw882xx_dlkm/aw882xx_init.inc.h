// SPDX-License-Identifier: GPL-2.0
/* aw_init.c   aw882xx codec module
 *
 *
 * Copyright (c) 2019 AWINIC Technology CO., LTD
 *
 * Author: Nick Li <liweilei@awinic.com.cn>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/* #define DEBUG */

#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/syscalls.h>
#include <sound/control.h>
#include <linux/uaccess.h>

#include "aw882xx.h"
#include "aw882xx_pid_1852_reg.h"
#include "aw882xx_pid_2013_reg.h"
#include "aw882xx_pid_2032_reg.h"
#include "aw882xx_pid_2055a_reg.h"
#include "aw882xx_pid_2055_reg.h"
#include "aw882xx_pid_2071_reg.h"
#include "aw882xx_pid_2113_reg.h"
#include "aw882xx_pid_2308_reg.h"
#include "aw882xx_pid_2116_reg.h"
#include "aw882xx_log.h"

/*
 * g3u: aw_pid_2013_efver_check as the stock inlines it: the profile / boost registers are stored together with the block (before
 * the read), a part with EFVER L or H == 0 switches them off again (reg = 0xff); only EFVER A keeps them.
 */
static __always_inline void aw_pid_2013_efver_check(struct aw_device *aw_dev)
{
	unsigned int reg_val = 0;
	unsigned int efverh;
	unsigned int efverl;

	aw_dev->ops.aw_i2c_read(aw_dev->i2c, 0x7a, &reg_val);
	efverh = ((reg_val >> 13) & 7) ^ 7;
	efverl = ((reg_val & 0x1c00) >> 10) ^ 7;

	if (efverh && efverl) {
		aw_dev_info(aw_dev->dev, "A2013 EFVER A");
	} else {
		aw_dev->profctrl_desc.reg = 0xff;
		aw_dev->bstctrl_desc.reg = 0xff;
		if (efverh && !efverl)
			aw_dev_info(aw_dev->dev, "A2013 EFVER B");
		else
			aw_dev_info(aw_dev->dev, "unsupport A2013 EFVER");
	}
}

/* g3u: stock block of chip 0x1852, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_1852_dev_init(struct aw_device *aw_dev)
{
	aw_dev->reg_max = 0x7d;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_1852_monitor.bin", 28);
	aw_dev->vol_step = 0xc;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P5_reg_val_to_db /* .text+0x11a34 */;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P5_db_val_to_reg /* .text+0x11a4c */;
	aw_dev->vcalb_desc.icalk_value_factor = 0x1;
	aw_dev->mute_desc.reg = 0x5;
	aw_dev->mute_desc.mask = 0xffffffef;
	aw_dev->mute_desc.enable = 0x10;
	aw_dev->mute_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x7;
	aw_dev->txen_desc.mask = 0xfffffffe;
	aw_dev->txen_desc.enable = 0x1;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->vcalb_desc.vcalb_reg = 0x56;
	aw_dev->vcalb_desc.vcal_factor = 0x2000;
	aw_dev->vcalb_desc.cabl_base_value = 0x3e8;
	aw_dev->vcalb_desc.icalk_reg = 0x7a;
	aw_dev->vcalb_desc.icalk_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.vcalk_value_factor = 0x1;
	aw_dev->vcalb_desc.icalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.icalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.vcalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_reg = 0x78;
	aw_dev->vcalb_desc.vcalk_reg_mask = 0xfffffc00;
	aw_dev->cco_mux_desc.reg = 0x66;
	aw_dev->cco_mux_desc.mask = 0xffffbfff;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x4000;
	aw_dev->voltage_desc.reg = 0x12;
	aw_dev->temp_desc.reg = 0x13;
	aw_dev->ipeak_desc.reg = 0x5;
	aw_dev->ipeak_desc.mask = 0xfffffff0;
	aw_dev->mute_desc.name = "btn 1852 hmue" /* .rodata+0x134f */;
	aw_dev->uls_hmute_desc.reg = 0xff;
	aw_dev->volume_desc.reg = 0xc;
	aw_dev->volume_desc.mask = 0xffff00ff;
	aw_dev->volume_desc.shift = 0x8;
	aw_dev->volume_desc.mute_volume = 0xb4;
	aw_dev->volume_desc.ctl_volume = 0x0;
	aw_dev->vcalb_desc.icalkl_reg = 0xff;
	aw_dev->vcalb_desc.vcalkl_reg = 0xff;
	aw_dev->dither_desc.reg = 0x71;
	aw_dev->dither_desc.mask = 0xffffff7f;
	aw_dev->bop_desc.reg = 0xff;
	aw_dev->dither_desc.enable = 0x80;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->dither_desc.name = "btn 1852 dither" /* .rodata+0x5bcc */;
	aw_dev->noise_gate_desc.reg = 0xff;
	aw_dev->ef_lock_desc.num = 0x1;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
}

/* g3u: stock block of chip 0x2013, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2013_dev_init(struct aw_device *aw_dev)
{
	aw_dev->reg_max = 0x7d;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2013_monitor.bin", 28);
	aw_dev->vol_step = 0x30;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db /* .text+0x11a70 */;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg /* .text+0x11a88 */;
	aw_dev->dither_desc.enable = 0x4000;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x6;
	aw_dev->txen_desc.mask = 0xffffbfff;
	aw_dev->txen_desc.enable = 0x4000;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->vcalb_desc.vcalb_reg = 0x50;
	aw_dev->vcalb_desc.cabl_base_value = 0x3e8;
	aw_dev->vcalb_desc.vcal_factor = 0x2000;
	aw_dev->vcalb_desc.icalk_value_factor = 0x1;
	aw_dev->vcalb_desc.vcalk_value_factor = 0x1;
	aw_dev->vcalb_desc.icalk_reg = 0x78;
	aw_dev->vcalb_desc.icalk_reg_mask = 0xfffffc00;
	aw_dev->profctrl_desc.reg = 0x4;
	aw_dev->profctrl_desc.mask = 0xffffff7f;
	aw_dev->vcalb_desc.icalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.icalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.vcalk_neg_mask = 0xffff03ff;
	aw_dev->bstctrl_desc.reg = 0x61;
	aw_dev->bstctrl_desc.mask = 0xffffffcf;
	aw_dev->vcalb_desc.vcalk_reg = 0x79;
	aw_dev->vcalb_desc.vcalk_reg_mask = 0xfffffc00;
	aw_dev->bstctrl_desc.frc_bst = 0x10;
	aw_dev->bstctrl_desc.tsp_type = 0x0;
	aw_dev->cco_mux_desc.reg = 0x55;
	aw_dev->cco_mux_desc.mask = 0xfffffffb;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x4;
	aw_dev->volume_desc.mute_volume = 0x2d0;
	aw_dev->volume_desc.ctl_volume = 0x0;
	aw_dev->uls_hmute_desc.reg = 0xff;
	aw_dev->vcalb_desc.icalkl_reg = 0xff;
	aw_dev->dither_desc.reg = 0x12;
	aw_dev->dither_desc.mask = 0xffffbfff;
	aw_dev->dither_desc.name = "btn 2013 dither" /* .rodata+0x1e25 */;
	aw_dev->vcalb_desc.vcalkl_reg = 0xff;
	aw_dev->ef_lock_desc.num = 0x1;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
	aw_dev->profctrl_desc.spk_mode = 0x0;
	aw_dev->bop_desc.reg = 0xff;
	aw_dev->noise_gate_desc.reg = 0xff;
aw_pid_2013_efver_check(aw_dev);
}

/* g3u: stock block of chip 0x2032, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2032_dev_init(struct aw_device *aw_dev)
{
	aw_dev->reg_max = 0x7e;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2032_monitor.bin", 28);
	aw_dev->vcalb_desc.cabl_base_value = 0x3e8;
	aw_dev->vcalb_desc.icalk_reg = 0x7a;
	aw_dev->vcalb_desc.icalk_reg_mask = 0xfffffc00;
	aw_dev->vol_step = 0x30;
	aw_dev->vcalb_desc.icalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.icalk_neg_mask = 0xffff03ff;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db /* .text+0x11a70 */;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg /* .text+0x11a88 */;
	aw_dev->vcalb_desc.vcalk_reg = 0x78;
	aw_dev->vcalb_desc.vcalk_reg_mask = 0xfffffc00;
	aw_dev->mute_desc.reg = 0x5;
	aw_dev->mute_desc.mask = 0xffffffef;
	aw_dev->mute_desc.enable = 0x10;
	aw_dev->mute_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x7;
	aw_dev->txen_desc.mask = 0xfffffffe;
	aw_dev->txen_desc.enable = 0x1;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->vcalb_desc.icalk_value_factor = 0x1;
	aw_dev->vcalb_desc.vcalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.vcalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_value_factor = 0x1;
	aw_dev->cco_mux_desc.reg = 0x66;
	aw_dev->cco_mux_desc.mask = 0xffffbfff;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x4000;
	aw_dev->voltage_desc.reg = 0x12;
	aw_dev->temp_desc.reg = 0x13;
	aw_dev->ipeak_desc.reg = 0x5;
	aw_dev->ipeak_desc.mask = 0xfffffff0;
	aw_dev->vcalb_desc.vcalb_reg = 0x56;
	aw_dev->mute_desc.name = "btn 2032 hmute" /* .rodata+0x5bdc */;
	aw_dev->volume_desc.reg = 0x5;
	aw_dev->volume_desc.mask = 0xffff003f;
	aw_dev->volume_desc.shift = 0x6;
	aw_dev->volume_desc.mute_volume = 0x2d0;
	aw_dev->volume_desc.ctl_volume = 0x0;
	aw_dev->vcalb_desc.vcal_factor = 0x2000;
	aw_dev->dither_desc.enable = 0x2000;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->uls_hmute_desc.reg = 0xff;
	aw_dev->vcalb_desc.icalkl_reg = 0xff;
	aw_dev->vcalb_desc.vcalkl_reg = 0xff;
	aw_dev->bop_desc.reg = 0xff;
	aw_dev->dither_desc.reg = 0x20;
	aw_dev->dither_desc.mask = 0xffffdfff;
	aw_dev->dither_desc.name = "btn 2032 dither" /* .rodata+0x5beb */;
	aw_dev->noise_gate_desc.reg = 0xff;
	aw_dev->ef_lock_desc.num = 0x1;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
}

/* g3u: stock block of chip 0x2071, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2071_dev_init(struct aw_device *aw_dev)
{
	aw_dev->vcalb_desc.cabl_base_value = 0x3e8;
	aw_dev->vcalb_desc.icalk_reg = 0x78;
	aw_dev->vcalb_desc.icalk_reg_mask = 0xffff83ff;
	aw_dev->vcalb_desc.icalk_shift = 0xa;
	aw_dev->vcalb_desc.icalkl_reg = 0x7a;
	aw_dev->reg_max = 0x7e;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2071_monitor.bin", 28);
	aw_dev->vcalb_desc.icalkl_reg_mask = 0xffff83ff;
	aw_dev->vcalb_desc.icalkl_shift = 0xa;
	aw_dev->vcalb_desc.icalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.icalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_reg = 0x78;
	aw_dev->vcalb_desc.vcalk_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.vcalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.vcalk_neg_mask = 0xffff03ff;
	aw_dev->cco_mux_desc.reg = 0x66;
	aw_dev->cco_mux_desc.mask = 0xffffbfff;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x4000;
	aw_dev->vol_step = 0x30;
	aw_dev->voltage_desc.reg = 0x12;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db /* .text+0x11a70 */;
	aw_dev->temp_desc.reg = 0x13;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg /* .text+0x11a88 */;
	aw_dev->ipeak_desc.reg = 0x5;
	aw_dev->ipeak_desc.mask = 0xfffffff0;
	aw_dev->mute_desc.reg = 0x5;
	aw_dev->mute_desc.mask = 0xffffffef;
	aw_dev->mute_desc.enable = 0x10;
	aw_dev->mute_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x7;
	aw_dev->txen_desc.mask = 0xfffffffe;
	aw_dev->txen_desc.enable = 0x1;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->vcalb_desc.icalk_value_factor = 0x1;
	aw_dev->vcalb_desc.vcalk_value_factor = 0x1;
	aw_dev->efuse_check = 0x1;
	aw_dev->volume_desc.reg = 0x5;
	aw_dev->volume_desc.mask = 0xffff003f;
	aw_dev->mute_desc.name = "btn 2071 hmute" /* .rodata+0x8d1 */;
	aw_dev->vcalb_desc.vcalb_reg = 0x56;
	aw_dev->volume_desc.shift = 0x6;
	aw_dev->uls_hmute_desc.reg = 0xff;
	aw_dev->vcalb_desc.vcal_factor = 0x2000;
	aw_dev->vcalb_desc.vcalkl_reg = 0xff;
	aw_dev->volume_desc.mute_volume = 0x2d0;
	aw_dev->volume_desc.ctl_volume = 0x0;
	aw_dev->bop_desc.reg = 0xff;
	aw_dev->dither_desc.reg = 0x20;
	aw_dev->dither_desc.mask = 0xffffdfff;
	aw_dev->dither_desc.enable = 0x2000;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->dither_desc.name = "btn 2071 dither" /* .rodata+0x2f5f */;
	aw_dev->noise_gate_desc.reg = 0xff;
	aw_dev->ef_lock_desc.num = 0x1;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
}

static int aw_pid_2113_frcset_check(struct aw_device *aw_dev)
{
	unsigned int reg_val = 0;
	uint16_t temh = 0;
	uint16_t teml = 0;
	uint16_t tem = 0;

	aw_dev->ops.aw_i2c_read(aw_dev->i2c,
			AW_PID_2113_EFRH3_REG, &reg_val);
	temh = ((uint16_t)reg_val & (~AW_PID_2113_TEMH_MASK));

	aw_dev->ops.aw_i2c_read(aw_dev->i2c,
			AW_PID_2113_EFRL3_REG, &reg_val);
	teml = ((uint16_t)reg_val & (~AW_PID_2113_TEML_MASK));

	if (aw_dev->efuse_check == AW_EF_OR_CHECK)
		tem = (temh | teml);
	else
		tem = (temh & teml);

	if (tem == AW_PID_2113_DEFAULT_CFG)
		aw_dev->frcset_en = AW_FRCSET_ENABLE;
	else
		aw_dev->frcset_en = AW_FRCSET_DISABLE;

	aw_dev_info(aw_dev->dev, "tem is 0x%04x, frcset_en is %d",
						tem, aw_dev->frcset_en);
	return 0;
}

static void aw_pid_2113_reg_force_set(struct aw_device *aw_dev)
{
	aw_dev_dbg(aw_dev->dev, "enter");

	if (aw_dev->frcset_en == AW_FRCSET_ENABLE) {
		/*set FORCE_PWM*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL3_REG,
				AW_PID_2113_FORCE_PWM_MASK, AW_PID_2113_FORCE_PWM_FORCEMINUS_PWM_VALUE);
		/*set BOOST_OS_WIDTH*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL5_REG,
				AW_PID_2113_BST_OS_WIDTH_MASK, AW_PID_2113_BST_OS_WIDTH_50NS_VALUE);
		/*set BURST_LOOPR*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL6_REG,
				AW_PID_2113_BST_LOOPR_MASK, AW_PID_2113_BST_LOOPR_340K_VALUE);
		/*set RSQN_DLY*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL7_REG,
				AW_PID_2113_RSQN_DLY_MASK, AW_PID_2113_RSQN_DLY_35NS_VALUE);
		/*set BURST_SSMODE*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL8_REG,
				AW_PID_2113_BURST_SSMODE_MASK, AW_PID_2113_BURST_SSMODE_FAST_VALUE);
		/*set BST_BURST*/
		aw_dev->ops.aw_i2c_write_bits(aw_dev->i2c, AW_PID_2113_BSTCTRL9_REG,
				AW_PID_2113_BST_BURST_MASK, AW_PID_2113_BST_BURST_30MA_VALUE);
		aw_dev_dbg(aw_dev->dev, "force set reg done!");
	} else {
		aw_dev_info(aw_dev->dev, "needn't set reg value");
	}
}

/* g3u: stock block of chip 0x2113, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2113_dev_init(struct aw_device *aw_dev)
{
	aw_dev->reg_max = 0x80;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2113_monitor.bin", 28);
	aw_dev->vol_step = 0x30;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db /* .text+0x11a70 */;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg /* .text+0x11a88 */;
	aw_dev->ops.aw_reg_force_set = aw_pid_2113_reg_force_set /* .text+0x11aac */;
	aw_dev->ops.aw_frcset_check = aw_pid_2113_frcset_check /* .text+0x11c28 */;
	aw_dev->uls_hmute_desc.name = "btn 2113 uls hmute" /* .rodata+0x4467 */;
	aw_dev->uls_hmute_desc.reg = 0x4;
	aw_dev->uls_hmute_desc.mask = 0xffffbfff;
	aw_dev->vcalb_desc.vcalb_reg = 0x50;
	aw_dev->vcalb_desc.vcal_factor = 0x2000;
	aw_dev->vcalb_desc.cabl_base_value = 0x3e8;
	aw_dev->uls_hmute_desc.enable = 0x4000;
	aw_dev->uls_hmute_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x8;
	aw_dev->txen_desc.mask = 0xffffffbf;
	aw_dev->txen_desc.enable = 0x40;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->vcalb_desc.icalk_reg = 0x78;
	aw_dev->vcalb_desc.icalk_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.icalk_shift = 0x0;
	aw_dev->vcalb_desc.icalkl_reg = 0x7c;
	aw_dev->vcalb_desc.icalkl_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.icalkl_shift = 0x0;
	aw_dev->vcalb_desc.icalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.icalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.icalk_value_factor = 0x1;
	aw_dev->vcalb_desc.vcalk_reg = 0x79;
	aw_dev->vcalb_desc.vcalk_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.vcalk_shift = 0x0;
	aw_dev->vcalb_desc.vcalkl_reg = 0x7d;
	aw_dev->vcalb_desc.vcalkl_reg_mask = 0xfffffc00;
	aw_dev->vcalb_desc.vcalkl_shift = 0x0;
	aw_dev->vcalb_desc.vcalk_sign_mask = 0xfffffdff;
	aw_dev->vcalb_desc.vcalk_neg_mask = 0xffff03ff;
	aw_dev->vcalb_desc.vcalk_value_factor = 0x1;
	aw_dev->cco_mux_desc.reg = 0x54;
	aw_dev->cco_mux_desc.mask = 0xffffffbf;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x40;
	aw_dev->volume_desc.mute_volume = 0x2d0;
	aw_dev->bop_desc.enable = 0x20;
	aw_dev->bop_desc.disable = 0x0;
	aw_dev->bop_desc.reg = 0x3c;
	aw_dev->bop_desc.mask = 0xffffffdf;
	aw_dev->bop_desc.name = "btn 2113 bop" /* .rodata+0x68ea */;
aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x73, 0x1d40);
usleep_range(3000, 3010);
	aw_dev->dither_desc.enable = 0x4000;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->efcheck_desc.and_val = 0x0;
	aw_dev->efcheck_desc.or_val = 0x8;
	aw_dev->efcheck_desc.reg = 0x14;
	aw_dev->efcheck_desc.mask = 0xfff7;
	aw_dev->dither_desc.reg = 0x14;
	aw_dev->dither_desc.mask = 0xffffbfff;
	aw_dev->dither_desc.name = "btn 2113 dither" /* .rodata+0x3516 */;
	aw_dev->noise_gate_desc.reg = 0xff;
}

/* 9bit signed int val */
static int aw_pid_2308_get_voltage_offset(struct aw_device *aw_dev,
														int32_t *offset) {
	int ret = 0;
	signed char val = -1;
	int32_t temp = -1;
	unsigned int reg_value = 0;

	ret = aw_dev->ops.aw_i2c_read(aw_dev->i2c, AW_PID_2308_EFRH2_REG, &reg_value);
	aw_dev_dbg(aw_dev->dev, "get addr %x val 0x%x",
						AW_PID_2308_EFRH2_REG, reg_value);

	val = (signed char)(reg_value & 0x00ff);
	temp = val;
	*offset = temp << 1;
	aw_dev_info(aw_dev->dev, "get vol offset val %d", *offset);

	return ret;
}

/* g3u: stock block of chip 0x2308, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2308_dev_init(struct aw_device *aw_dev)
{
	aw_dev->reg_max = 0x80;
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2308_monitor.bin", 28);
	aw_dev->ops.aw_get_voltage_offset = aw_pid_2308_get_voltage_offset /* .text+0x11e2c */;
	aw_dev->ops.aw_reg_val_to_db = aw_direct_reg_val_to_db /* .text+0x10af8 */;
	aw_dev->ops.aw_db_val_to_reg = aw_direct_db_val_to_reg /* .text+0x10b00 */;
	aw_dev->auth_desc.reg_in = 0x38;
	aw_dev->auth_desc.reg_out = 0x2e;
	aw_dev->psm_desc.reg = 0x5;
	aw_dev->psm_desc.mask = 0xffffbfff;
	aw_dev->psm_desc.enable = 0x4000;
	aw_dev->psm_desc.disable = 0x0;
	aw_dev->psm_desc.name = "btn 2308 psm" /* .rodata+0x6914 */;
	aw_dev->mpd_desc.reg = 0x5;
	aw_dev->mpd_desc.mask = 0xffff7fff;
	aw_dev->mpd_desc.enable = 0x8000;
	aw_dev->mpd_desc.disable = 0x0;
	aw_dev->mpd_desc.name = "btn 2308 mpd" /* .rodata+0x3ff5 */;
	aw_dev->dsmzth_desc.reg = 0x5e;
	aw_dev->dsmzth_desc.mask = 0xffffff0f;
	aw_dev->dsmzth_desc.enable = 0x50;
	aw_dev->dsmzth_desc.disable = 0x0;
	aw_dev->dsmzth_desc.name = "btn 2308 dsmzth" /* .rodata+0x5eb3 */;
}

/* g3u: stock block of chip 0x2116, stores generated from the disassembly (gd.py --genc)  */
static __always_inline void aw_pid_2116_dev_init(struct aw_device *aw_dev)
{
	aw_dev->ops.aw_get_irq_type = aw_get_irq_nobst_type /* .text+0x11d30 */;
	aw_dev->int_desc.mask_default = 0x61ff;
	aw_dev->int_desc.int_mask = 0x61ff;
	aw_dev->sysst_desc.mask = 0xffffbec4;
	aw_dev->sysst_desc.st_check = 0x11;
	aw_dev->reg_max = 0x72;
	aw_dev->sysst_desc.st_sws_check = 0x111;
	aw_dev->work_mode.reg = 0xff;
	aw_dev->amppd_desc.reg = 0x4;
	aw_dev->amppd_desc.mask = 0xfffffffd;
	aw_dev->amppd_desc.enable = 0x0;
	aw_dev->amppd_desc.disable = 0x2;
	aw_dev->voltage_desc.reg = 0xff;
	aw_dev->temp_desc.reg = 0xff;
	aw_dev->ipeak_desc.reg = 0xff;
	aw_dev->amppd_desc.name = "btn 2116 amppd" /* .rodata+0x298a */;
	aw_dev->ef_lock_desc.num = 0x0;
	aw_dev->bop_desc.reg = 0x5;
	aw_dev->bop_desc.mask = 0xffffbfff;
	aw_dev->uls_hmute_desc.name = "btn 2116 uls hmute" /* .rodata+0x2f6f */;
	aw_dev->uls_hmute_desc.reg = 0x4;
	aw_dev->uls_hmute_desc.mask = 0xffffbfff;
	aw_dev->bop_desc.name = "btn 2116 bop" /* .rodata+0xe0c */;
	aw_dev->uls_hmute_desc.enable = 0x4000;
	aw_dev->uls_hmute_desc.disable = 0x0;
	aw_dev->txen_desc.reg = 0x8;
	aw_dev->txen_desc.mask = 0xffffffbf;
	aw_dev->noise_gate_desc.reg = 0x16;
	aw_dev->noise_gate_desc.mask = 0xffffdfff;
	aw_dev->txen_desc.enable = 0x40;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->noise_gate_desc.enable = 0x2000;
	aw_dev->noise_gate_desc.disable = 0x0;
	aw_dev->cco_mux_desc.reg = 0x54;
	aw_dev->cco_mux_desc.mask = 0xffffdfff;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x2000;
	aw_dev->bop_desc.enable = 0x4000;
	aw_dev->bop_desc.disable = 0x0;
	aw_dev->noise_gate_desc.name = "btn 2116 noise gate" /* .rodata+0x64ad */;
}

/* g3u: stock block of chip 0x2055, stores generated from the disassembly (gd.py --genc) (the 2055 stores; the stock repeats the block before and after the version read) */
static __always_inline void aw_pid_2055_fill(struct aw_device *aw_dev)
{
	memcpy(aw_dev->monitor_name, "aw882xx_pid_2055_monitor.bin", 28);
	aw_dev->vol_step = 0x30;
	aw_dev->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db /* .text+0x11a70 */;
	aw_dev->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg /* .text+0x11a88 */;
	aw_dev->reg_max = 0x7d;
	aw_dev->txen_desc.reg = 0x6;
	aw_dev->txen_desc.mask = 0xffffbfff;
	aw_dev->txen_desc.enable = 0x4000;
	aw_dev->txen_desc.disable = 0x0;
	aw_dev->volume_desc.mute_volume = 0x2d0;
	aw_dev->cco_mux_desc.reg = 0x52;
	aw_dev->cco_mux_desc.mask = 0xfffffffb;
	aw_dev->cco_mux_desc.divided_val = 0x0;
	aw_dev->cco_mux_desc.bypass_val = 0x4;
	aw_dev->uls_hmute_desc.reg = 0xff;
	aw_dev->bop_desc.reg = 0x3c;
	aw_dev->bop_desc.mask = 0xffffffef;
	aw_dev->bop_desc.enable = 0x10;
	aw_dev->bop_desc.disable = 0x0;
	aw_dev->bop_desc.name = "btn 2055 bop" /* .rodata+0x54a0 */;
	aw_dev->dither_desc.enable = 0x4000;
	aw_dev->dither_desc.disable = 0x0;
	aw_dev->dither_desc.name = "btn 2055 dither" /* .rodata+0x3783 */;
	aw_dev->dither_desc.reg = 0x17;
	aw_dev->dither_desc.mask = 0xffffbfff;
	aw_dev->noise_gate_desc.reg = 0xff;
	aw_dev->ef_lock_desc.num = 0x2;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
	aw_dev->ef_lock_desc.item[1].value = 0x8000;
	aw_dev->ef_lock_desc.item[1].reg = 0x79;
	aw_dev->ef_lock_desc.item[1].mask = 0xffff7fff;
}

/*
 * g3u: chip 0x2055: the stock stores the 2055 block, resets the chip (reg 0 = 0x55aa), reads the version (reg 0x23) and stores the block
 * AGAIN (the stores cannot be merged across the indirect calls; no version error in the stock: any value but 0x6708 is a plain 2055); 2055A then changes chip_id to 0x2055a and rewrites the monitor name,
 * work_mode.reg, voltage and temp registers (0xff). Common tail: usleep 2 ms, write 0x73 = 0x1d40, usleep 3 ms.
 */
static __always_inline void aw_pid_2055_dev_check(struct aw_device *aw_dev)
{
	unsigned int reg_data = 0;

	aw_pid_2055_fill(aw_dev);
	aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x0, 0x55aa);
	usleep_range(1000, 1100);
	aw_dev->ops.aw_i2c_read(aw_dev->i2c, 0x23, &reg_data);
	aw_pid_2055_fill(aw_dev);
	if (reg_data == 0x6708) {
		aw_dev->chip_id = 0x2055a;
		aw_dev->work_mode.reg = 0xff;
		memcpy(aw_dev->monitor_name, "aw882xx_pid_2055a_monitor.bin", 29);
		aw_dev->voltage_desc.reg = 0xff;
		aw_dev->temp_desc.reg = 0xff;
	}

	usleep_range(2000, 2010);
	aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x73, 0x1d40);
	usleep_range(3000, 3010);
}

#include <asm/unaligned.h>
noinline void aw_dev_init_cmm(struct aw_device *aw_dev);
#include "g3n_cmm.inc.h"


noinline void aw_dev_init_cmm(struct aw_device *aw_dev);
int aw882xx_init(struct aw_device *aw_dev)
{
	int ret = 0;

	/* g3n: the stock calls aw_dev_init_cmm first */
	aw_dev_init_cmm(aw_dev);

	/* g3u: dispatch of the stock: 0x1852, 0x2013, 0x2032, 0x2055 (+A), 0x2071, 0x2113, 0x2116, 0x2308; every other id is rejected
	 * (0x2031, 0x2070 and 0x2115 are only the pivots of the stock's binary-search switch lowering, the earlier rounds took them for case values) */
	switch (aw_dev->chip_id) {
	case 0x1852:
		aw_pid_1852_dev_init(aw_dev);
		break;
	case 0x2013:
		aw_pid_2013_dev_init(aw_dev);
		break;
	case 0x2032:
		aw_pid_2032_dev_init(aw_dev);
		break;
	case 0x2055:
		aw_pid_2055_dev_check(aw_dev);
		break;
	case 0x2071:
		aw_pid_2071_dev_init(aw_dev);
		break;
	case 0x2113:
		aw_pid_2113_dev_init(aw_dev);
		break;
	case 0x2116:
		aw_pid_2116_dev_init(aw_dev);
		break;
	case 0x2308:
		aw_pid_2308_dev_init(aw_dev);
		break;
	default:
		aw_dev_err(aw_dev->dev, "unsupported chip id 0x%04x", aw_dev->chip_id);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* g3r: common tail of the stock: ef lock check, then the device probe */
	ret = aw882xx_dev_check_ef_lock(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "ef has not been locked");
		return ret;
	}

	return aw882xx_dev_probe(aw_dev);
}

#include "g3k_aw_init.inc.h"
