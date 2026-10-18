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

/* g3r: ef lock defaults of aw_dev_init_cmm (+0xa0..+0xbc): 2 items {0x78, 0x8000} and {0x7c, 0x8000}, mask ~0x8000 */
static void aw_ef_lock_defaults(struct aw_device *aw_dev)
{
	aw_dev->ef_lock_desc.num = 2;
	aw_dev->ef_lock_desc.item[0].reg = 0x78;
	aw_dev->ef_lock_desc.item[0].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[0].value = 0x8000;
	aw_dev->ef_lock_desc.item[1].reg = 0x7c;
	aw_dev->ef_lock_desc.item[1].mask = 0xffff7fff;
	aw_dev->ef_lock_desc.item[1].value = 0x8000;
}

static int aw_pid_1852_dev_init(struct aw882xx *aw882xx)
{
	int ret;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/* call aw device init func */
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_1852_MONITOR_FILE, strlen(AW_PID_1852_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_1852_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P5_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P5_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_1852_HAGCCFG4_REG;
	aw_pa->vol_shift = 8;
	aw_pa->vol_mask = 0xff00;
	aw_pa->reg_max = AW_PID_1852_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_1852_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_1852_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_1852_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_1852_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_1852_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_1852_RCV_MODE_MASK;
	aw_pa->work_mode.spk_val = AW_PID_1852_RCV_MODE_SPEAKER_MODE_VCOM13PVDD_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_1852_RCV_MODE_RECEIVER_MODE_VCOM12PVDD_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_1852_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_1852_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_1852_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_1852_PWDN_NORMAL_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_1852_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_1852_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_1852_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_1852_AMPPD_NORMAL_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_1852_SYSCTRL2_REG;
	aw_pa->mute_desc.mask = AW_PID_1852_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_1852_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_1852_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_1852_I2SCFG1_REG;
	aw_pa->txen_desc.mask = AW_PID_1852_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_1852_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_1852_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_PID_1852_VTMCTRL3_REG;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_1852_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_1852_CABL_BASE_VALUE;

	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_1852_ICABLK_FACTOR;
	aw_pa->vcalb_desc.icalk_reg = AW_PID_1852_EFRM1_REG;
	aw_pa->vcalb_desc.icalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_1852_EF_ISN_GESLP_MASK;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_1852_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_1852_EF_ISN_GESLP_NEG;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_1852_EFRH_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_1852_EF_VSN_GESLP_MASK;
	aw_pa->vcalb_desc.vcalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_1852_EF_VSN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_1852_EF_VSN_GESLP_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_1852_VCABLK_FACTOR;

	aw_pa->sysst_desc.reg = AW_PID_1852_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_1852_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_1852_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_1852_IIS_CHECK;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_1852_I2SCTRL_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_1852_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_1852_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_1852_CHSEL_RIGHT_VALUE;

	aw_pa->cco_mux_desc.reg = AW_PID_1852_PLLCTRL1_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_1852_I2S_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_1852_I2S_CCO_MUX_8_16_32KHZ_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_1852_I2S_CCO_MUX_EXC_8_16_32KHZ_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_1852_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_1852_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_1852_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_1852_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_1852_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_1852_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_1852_SYSCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_1852_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_1852_HAGCCFG4_REG;
	aw_pa->volume_desc.mask = AW_PID_1852_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_1852_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_1852_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_1852_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_REG_NONE;
	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->dither_desc.reg = AW_PID_1852_TESTCTRL2_REG;
	aw_pa->dither_desc.mask = AW_PID_1852_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_1852_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_1852_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_1852_CRCIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_1852_CRCOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

/******************************************************
 *
 * A2013 init
 *
 ******************************************************/
static void aw_pid_2013_efver_check(struct aw_device *aw_dev)
{
	unsigned int reg_val = 0;
	unsigned int efverh = 0;
	unsigned int efverl = 0;

	aw_dev->ops.aw_i2c_read(aw_dev->i2c,
			AW_PID_2013_EFRM1_REG, &reg_val);

	efverh = (((reg_val & (~AW_PID_2013_EFVERH_MASK)) >>
			AW_PID_2013_EFVERH_START_BIT) ^
			AW_PID_2013_EFVER_CHECK);
	efverl = (((reg_val & (~AW_PID_2013_EFVERL_MASK)) >>
			AW_PID_2013_EFVERL_START_BIT) ^
			AW_PID_2013_EFVER_CHECK);

	aw_dev_dbg(aw_dev->dev, "efverh: 0x%0x, efverl: 0x%0x", efverh, efverl);

	if (efverh && efverl) {
		aw_dev->bstcfg_enable = AW_BSTCFG_ENABLE;
		aw_dev_info(aw_dev->dev, "A2013 EFVER A");
	} else if (efverh && !efverl) {
		aw_dev->bstcfg_enable = AW_BSTCFG_DISABLE;
		aw_dev_info(aw_dev->dev, "A2013 EFVER B");
	} else {
		aw_dev->bstcfg_enable = AW_BSTCFG_DISABLE;
		aw_dev_info(aw_dev->dev, "unsupport A2013 EFVER");
	}
	aw_dev_info(aw_dev->dev, "bstcfg enable: %d", aw_dev->bstcfg_enable);
}

static int aw_pid_2013_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2013_MONITOR_FILE, strlen(AW_PID_2013_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2013_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2013_SYSCTRL2_REG;
	aw_pa->vol_shift = 0;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2013_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_2013_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2013_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2013_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2013_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_2013_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2013_EN_TRAN_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2013_EN_TRAN_SPK_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2013_EN_TRAN_RCV_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2013_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2013_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2013_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2013_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2013_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2013_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2013_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2013_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2013_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2013_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2013_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2013_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2013_I2SCTRL1_REG;
	aw_pa->txen_desc.mask = AW_PID_2013_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2013_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2013_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_PID_2013_VSNTM1_REG;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_2013_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_2013_CABL_BASE_VALUE;

	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2013_ICABLK_FACTOR;
	aw_pa->vcalb_desc.icalk_reg = AW_PID_2013_EFRH_REG;
	aw_pa->vcalb_desc.icalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_2013_EF_ISN_GESLP_MASK;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2013_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_2013_EF_ISN_GESLP_NEG;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_2013_EFRM2_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_2013_EF_VSN_GESLP_MASK;
	aw_pa->vcalb_desc.vcalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2013_EF_ISN_GESLP2_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_2013_EF_ISN_GESLP2_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2013_VCABLK_FACTOR;

	aw_pa->sysst_desc.reg = AW_PID_2013_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2013_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2013_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2013_IIS_CHECK;

	aw_pa->profctrl_desc.reg = AW_PID_2013_SYSCTRL_REG;
	aw_pa->profctrl_desc.mask = AW_PID_2013_EN_TRAN_MASK;
	aw_pa->profctrl_desc.spk_mode = AW_PID_2013_EN_TRAN_SPK_VALUE;

	aw_pa->bstctrl_desc.reg = AW_PID_2013_BSTCTRL2_REG;
	aw_pa->bstctrl_desc.mask = AW_PID_2013_BST_MODE_MASK;
	aw_pa->bstctrl_desc.frc_bst = AW_PID_2013_BST_MODE_FORCE_BOOST_VALUE;
	aw_pa->bstctrl_desc.tsp_type = AW_PID_2013_BST_MODE_TRANSPARENT_VALUE;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2013_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2013_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2013_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2013_CHSEL_RIGHT_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2013_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2013_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2013_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2013_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2013_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2013_MONITOR_TEMP_SIGN_MASK;

	aw_pa->cco_mux_desc.reg = AW_PID_2013_PLLCTRL3_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2013_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2013_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2013_CCO_MUX_BYPASS_VALUE;

	aw_pa->ipeak_desc.reg = AW_PID_2013_BSTCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2013_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2013_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2013_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2013_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2013_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2013_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_REG_NONE;
	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->dither_desc.reg = AW_PID_2013_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2013_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2013_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2013_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2013_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2013_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	aw_pid_2013_efver_check(aw_pa);
	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

/******************************************************
 *
 * A2032 init
 *
 ******************************************************/

static int aw_pid_2032_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/* call aw device init func */
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2032_MONITOR_FILE, strlen(AW_PID_2032_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2032_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2032_SYSCTRL2_REG;
	aw_pa->vol_shift = 6;
	aw_pa->vol_mask = 0xffc0;
	aw_pa->reg_max = AW_PID_2032_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_2032_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2032_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2032_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2032_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_2032_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2032_RCV_MODE_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2032_RCV_MODE_SPEAKER_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2032_RCV_MODE_RECEIVER_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2032_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2032_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2032_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2032_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2032_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2032_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2032_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2032_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2032_SYSCTRL2_REG;
	aw_pa->mute_desc.mask = AW_PID_2032_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2032_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2032_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2032_I2SCFG1_REG;
	aw_pa->txen_desc.mask = AW_PID_2032_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2032_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2032_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_PID_2032_VTMCTRL3_REG;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_2032_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_2032_CABL_BASE_VALUE;

	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2032_ICABLK_FACTOR;
	aw_pa->vcalb_desc.icalk_reg = AW_PID_2032_EFRM1_REG;
	aw_pa->vcalb_desc.icalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_2032_EF_ISN_GESLP_MASK;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2032_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_2032_EF_ISN_GESLP_NEG;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_2032_EFRH_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_2032_EF_VSN_GESLP_MASK;
	aw_pa->vcalb_desc.vcalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2032_EF_VSN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_2032_EF_VSN_GESLP_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2032_VCABLK_FACTOR;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2032_I2SCTRL_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2032_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2032_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2032_CHSEL_RIGHT_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2032_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2032_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2032_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2032_IIS_CHECK;

	aw_pa->cco_mux_desc.reg = AW_PID_2032_PLLCTRL1_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2032_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2032_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2032_CCO_MUX_BYPASS_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2032_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2032_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2032_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2032_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2032_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2032_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2032_SYSCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2032_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2032_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2032_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2032_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2032_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2032_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_REG_NONE;
	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->dither_desc.reg = AW_PID_2032_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2032_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2032_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2032_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2032_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2032_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

static int aw_pid_2055a_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	aw_pa->chip_id = PID_2055A_ID;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2055A_MONITOR_FILE, strlen(AW_PID_2055A_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2055A_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2055A_SYSCTRL2_REG;
	aw_pa->vol_shift = 0;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2055A_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_2055A_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2055A_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2055A_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2055A_SYSINT_REG;

	aw_pa->pwd_desc.reg = AW_PID_2055A_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2055A_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2055A_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2055A_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2055A_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2055A_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2055A_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2055A_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2055A_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2055A_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2055A_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2055A_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2055A_I2SCTRL1_REG;
	aw_pa->txen_desc.mask = AW_PID_2055A_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2055A_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2055A_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg = AW_REG_NONE;

	aw_pa->sysst_desc.reg = AW_PID_2055A_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2055A_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2055A_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2055A_IIS_CHECK;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2055A_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2055A_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2055A_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2055A_CHSEL_RIGHT_VALUE;

	aw_pa->voltage_desc.reg = AW_REG_NONE;
	aw_pa->temp_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2055A_I2SCTRL1_REG;
	aw_pa->txen_desc.mask = AW_PID_2055A_I2STXEN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2055A_BSTCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2055A_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2055A_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2055A_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2055A_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2055A_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2055A_VOL_DEFAULT_VALUE;

	aw_pa->cco_mux_desc.reg = AW_PID_2055A_PLLCTRL3_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2055A_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2055A_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2055A_CCO_MUX_BYPASS_VALUE;

	aw_pa->bop_desc.reg = AW_REG_NONE;
	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->dither_desc.reg = AW_PID_2055A_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2055A_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2055A_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2055A_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2055A_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2055A_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	usleep_range(AW_2000_US, AW_2000_US + 10);
	aw_pa->ops.aw_i2c_write(aw_pa->i2c, AW_PID_2055A_INIT_CHECK_REG,
					AW_PID_2055A_INIT_CHECK_VALUE);
	usleep_range(AW_3000_US, AW_3000_US + 10);

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}


static int aw_pid_2055_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2055_MONITOR_FILE, strlen(AW_PID_2055_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2055_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2055_SYSCTRL2_REG;
	aw_pa->vol_shift = 0;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2055_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_2055_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2055_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2055_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2055_SYSINT_REG;

	aw_pa->pwd_desc.reg = AW_PID_2055_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2055_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2055_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2055_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2055_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2055_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2055_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2055_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2055_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2055_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2055_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2055_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->work_mode.reg = AW_PID_2055_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2055_RCV_MODE_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2055_RCV_MODE_SPEAKER_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2055_RCV_MODE_RECEIVER_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2055_I2SCTRL1_REG;
	aw_pa->txen_desc.mask = AW_PID_2055_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2055_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2055_I2STXEN_DISABLE_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2055_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2055_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2055_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2055_IIS_CHECK;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2055_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2055_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2055_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2055_CHSEL_RIGHT_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2055_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2055_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2055_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2055_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2055_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2055_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2055_BSTCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2055_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2055_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2055_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2055_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2055_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2055_VOL_DEFAULT_VALUE;

	aw_pa->cco_mux_desc.reg = AW_PID_2055_PLLCTRL3_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2055_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2055_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2055_CCO_MUX_BYPASS_VALUE;

	aw_pa->bop_desc.reg = AW_PID_2055_SADCCTRL3_REG;
	aw_pa->bop_desc.mask = AW_PID_2055_BOP_EN_MASK;
	aw_pa->bop_desc.enable = AW_PID_2055_BOP_EN_ENABLE_VALUE;
	aw_pa->bop_desc.disbale = AW_PID_2055_BOP_EN_DISABLE_VALUE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->dither_desc.reg = AW_PID_2055_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2055_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2055_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2055_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2055_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2055_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	usleep_range(AW_2000_US, AW_2000_US + 10);
	aw_pa->ops.aw_i2c_write(aw_pa->i2c, AW_PID_2055_INIT_CHECK_REG,
					AW_PID_2055_INIT_CHECK_VALUE);
	usleep_range(AW_3000_US, AW_3000_US + 10);

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

static int aw_pid_2055_dev_check(struct aw882xx *aw882xx)
{
	unsigned int reg_data = 0;

	aw882xx_i2c_write(aw882xx, AW882XX_SOFT_RESET_REG, AW882XX_SOFT_RESET_VALUE);
	usleep_range(AW_1000_US, AW_1000_US + 100);

	aw882xx_i2c_read(aw882xx, AW_PID_2055_VERSION_DIFF_REG, &reg_data);
	if (reg_data == AW_PID_2055A_VERSION_VALUE)
		return aw_pid_2055a_dev_init(aw882xx);
	else if (reg_data == AW_PID_2055_VERSION_VALUE)
		return aw_pid_2055_dev_init(aw882xx);

	aw_dev_err(aw882xx->dev, "unsupported 2055 verison, 0x%04x", reg_data);

	return -EINVAL;
}

static int aw_pid_2071_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2071_MONITOR_FILE, strlen(AW_PID_2071_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2071_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2071_SYSCTRL2_REG;
	aw_pa->vol_shift = 6;
	aw_pa->vol_mask = 0xffc0;
	aw_pa->reg_max = AW_PID_2071_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;

	aw_pa->int_desc.mask_reg = AW_PID_2071_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2071_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2071_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2071_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_2071_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2071_RCV_MODE_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2071_RCV_MODE_SPEAKER_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2071_RCV_MODE_RECEIVER_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2071_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2071_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2071_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2071_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2071_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2071_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2071_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2071_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2071_SYSCTRL2_REG;
	aw_pa->mute_desc.mask = AW_PID_2071_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2071_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2071_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_REG_NONE;

	aw_pa->txen_desc.reg = AW_PID_2071_I2SCFG1_REG;
	aw_pa->txen_desc.mask = AW_PID_2071_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2071_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2071_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_PID_2071_VTMCTRL3_REG;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_2071_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_2071_CABL_BASE_VALUE;

	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2071_ICABLK_FACTOR;
	aw_pa->vcalb_desc.icalk_reg = AW_PID_2071_EFRH_REG;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_2071_EF_VSN_OFFSET_MASK;
	aw_pa->vcalb_desc.icalk_shift = AW_PID_2071_ICALK_SHIFT;
	aw_pa->vcalb_desc.icalkl_reg = AW_PID_2071_EFRM1_REG;
	aw_pa->vcalb_desc.icalkl_reg_mask = AW_PID_2071_EF_ISN_OFFSET_MASK;
	aw_pa->vcalb_desc.icalkl_shift = AW_PID_2071_ICALKL_SHIFT;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2071_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_2071_EF_ISN_GESLP_NEG;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_2071_EFRH_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_2071_EF_VSN_GESLP_MASK;
	aw_pa->vcalb_desc.vcalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2071_EF_VSN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_2071_EF_VSN_GESLP_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2071_VCABLK_FACTOR;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2071_I2SCTRL_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2071_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2071_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2071_CHSEL_RIGHT_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2071_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2071_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2071_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2071_IIS_CHECK;

	aw_pa->cco_mux_desc.reg = AW_PID_2071_PLLCTRL1_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2071_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2071_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2071_CCO_MUX_BYPASS_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2071_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2071_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2071_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2071_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2071_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2071_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2071_SYSCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2071_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2071_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2071_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2071_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2071_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2071_VOL_DEFAULT_VALUE;

	aw_pa->efcheck_desc.reg = AW_REG_NONE;
	aw_pa->efuse_check = AW_EF_OR_CHECK;

	aw_pa->bop_desc.reg = AW_REG_NONE;

	aw_pa->dither_desc.reg = AW_PID_2071_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2071_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2071_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2071_DITHER_DISABLE_VALUE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2071_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2071_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
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

static __always_inline int aw_pid_2113_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2113_MONITOR_FILE, strlen(AW_PID_2113_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2113_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;
	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_6_0P125_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_6_0P125_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2113_SYSCTRL2_REG;
	aw_pa->vol_shift = 0;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2113_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;
	aw_pa->ops.aw_reg_force_set = aw_pid_2113_reg_force_set;
	aw_pa->ops.aw_frcset_check = aw_pid_2113_frcset_check;

	aw_pa->int_desc.mask_reg = AW_PID_2113_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2113_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2113_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2113_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2113_EN_TRAN_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2113_EN_TRAN_SPK_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2113_EN_TRAN_RCV_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2113_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2113_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2113_PWDN_WORKING_VALUE;

	aw_pa->work_mode.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2113_EN_TRAN_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2113_EN_TRAN_SPK_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2113_EN_TRAN_RCV_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2113_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2113_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2113_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2113_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2113_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2113_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_PID_2113_SYSCTRL_REG;
	aw_pa->uls_hmute_desc.mask = AW_PID_2113_ULS_HMUTE_MASK;
	aw_pa->uls_hmute_desc.enable = AW_PID_2113_ULS_HMUTE_ENABLE_VALUE;
	aw_pa->uls_hmute_desc.disable = AW_PID_2113_ULS_HMUTE_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_PID_2113_VSNTM1_REG;
	aw_pa->vcalb_desc.vcal_factor = AW_PID_2113_VCAL_FACTOR;
	aw_pa->vcalb_desc.cabl_base_value = AW_PID_2113_CABL_BASE_VALUE;

	aw_pa->txen_desc.reg = AW_PID_2113_I2SCTRL3_REG;
	aw_pa->txen_desc.mask = AW_PID_2113_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2113_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2113_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.icalk_reg = AW_PID_2113_EFRH4_REG;
	aw_pa->vcalb_desc.icalk_reg_mask = AW_PID_2113_EF_ISN_GESLP_H_MASK;
	aw_pa->vcalb_desc.icalk_shift = AW_PID_2113_ICALK_SHIFT;
	aw_pa->vcalb_desc.icalkl_reg = AW_PID_2113_EFRL4_REG;
	aw_pa->vcalb_desc.icalkl_reg_mask = AW_PID_2113_EF_ISN_GESLP_L_MASK;
	aw_pa->vcalb_desc.icalkl_shift = AW_PID_2113_ICALKL_SHIFT;
	aw_pa->vcalb_desc.icalk_sign_mask = AW_PID_2113_EF_ISN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.icalk_neg_mask = AW_PID_2113_EF_ISN_GESLP_NEG;
	aw_pa->vcalb_desc.icalk_value_factor = AW_PID_2113_ICABLK_FACTOR;

	aw_pa->vcalb_desc.vcalk_reg = AW_PID_2113_EFRH3_REG;
	aw_pa->vcalb_desc.vcalk_reg_mask = AW_PID_2113_EF_VSN_GESLP_H_MASK;
	aw_pa->vcalb_desc.vcalk_shift = AW_PID_2113_VCALK_SHIFT;
	aw_pa->vcalb_desc.vcalkl_reg = AW_PID_2113_EFRL3_REG;
	aw_pa->vcalb_desc.vcalkl_reg_mask = AW_PID_2113_EF_VSN_GESLP_L_MASK;
	aw_pa->vcalb_desc.vcalkl_shift = AW_PID_2113_VCALKL_SHIFT;
	aw_pa->vcalb_desc.vcalk_sign_mask = AW_PID_2113_EF_VSN_GESLP_SIGN_MASK;
	aw_pa->vcalb_desc.vcalk_neg_mask = AW_PID_2113_EF_VSN_GESLP_NEG;
	aw_pa->vcalb_desc.vcalk_value_factor = AW_PID_2113_VCABLK_FACTOR;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2113_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2113_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2113_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2113_CHSEL_RIGHT_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2113_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2113_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2113_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2113_IIS_CHECK;

	aw_pa->cco_mux_desc.reg = AW_PID_2113_PLLCTRL1_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2113_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2113_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2113_CCO_MUX_BYPASS_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2113_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2113_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2113_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2113_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2113_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2113_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2113_BSTCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2113_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2113_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2113_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2113_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2113_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2113_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_PID_2113_SADCCTRL3_REG;
	aw_pa->bop_desc.mask = AW_PID_2113_BOP_EN_MASK;
	aw_pa->bop_desc.enable = AW_PID_2113_BOP_EN_ENABLE_VALUE;
	aw_pa->bop_desc.disbale = AW_PID_2113_BOP_EN_DISABLE_VALUE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	usleep_range(AW_2000_US, AW_2000_US + 10);
	aw_pa->ops.aw_i2c_write(aw_pa->i2c, AW_PID_2113_INIT_CHECK_REG,
					AW_PID_2113_INIT_CHECK_VALUE);
	usleep_range(AW_3000_US, AW_3000_US + 10);

	aw_pa->efcheck_desc.reg = AW_PID_2113_DBGCTRL_REG;
	aw_pa->efcheck_desc.mask = AW_PID_2113_EF_DBMD_MASK;
	aw_pa->efcheck_desc.and_val = AW_PID_2113_AND_VALUE;
	aw_pa->efcheck_desc.or_val = AW_PID_2113_OR_VALUE;

	aw_pa->dither_desc.reg = AW_PID_2113_DBGCTRL_REG;
	aw_pa->dither_desc.mask = AW_PID_2113_DITHER_MASK;
	aw_pa->dither_desc.enable = AW_PID_2113_DITHER_ENABLE_VALUE;
	aw_pa->dither_desc.disable = AW_PID_2113_DITHER_DISABLE_VALUE;

	aw_pa->noise_gate_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2113_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2113_TESTOUT_REG;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

/* 9bit signed int val */
static int aw_pid_2308_get_voltage_offset(struct aw_device *aw_dev,
														int32_t *offset) {
	int ret = 0;
	signed char val = -1;
	int32_t temp = -1;
	unsigned int reg_value = 0;
	struct aw882xx *aw882xx = (struct aw882xx *)aw_dev->private_data;

	ret = aw882xx_i2c_read(aw882xx, AW_PID_2308_EFRH2_REG, &reg_value);
	aw_dev_dbg(aw_dev->dev, "get addr %x val 0x%x",
						AW_PID_2308_EFRH2_REG, reg_value);

	val = (signed char)(reg_value & 0x00ff);
	temp = val;
	*offset = temp << 1;
	aw_dev_info(aw_dev->dev, "get vol offset val %d", *offset);

	return ret;
}

static int aw_pid_2308_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/*call aw device init func*/
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);
	memcpy(aw_pa->monitor_name, AW_PID_2308_MONITOR_FILE, strlen(AW_PID_2308_MONITOR_FILE));

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2308_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;

	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_direct_reg_val_to_db;
	aw_pa->ops.aw_db_val_to_reg = aw_direct_db_val_to_reg;
	aw_pa->vol_reg = AW_PID_2308_SYSCTRL2_REG;
	aw_pa->vol_shift = 0;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2308_REG_MAX;
	aw_pa->ops.aw_get_irq_type = aw_get_irq_type;
	aw_pa->ops.aw_get_voltage_offset = aw_pid_2308_get_voltage_offset;

	aw_pa->int_desc.mask_reg = AW_PID_2308_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2308_SYSINTM_DEFAULT;
	aw_pa->int_desc.int_mask = AW_PID_2308_SYSINTM_DEFAULT;
	aw_pa->int_desc.st_reg = AW_PID_2308_SYSINT_REG;

	aw_pa->work_mode.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->work_mode.mask = AW_PID_2308_RCV_MODE_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2308_RCV_MODE_SPEAKER_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2308_RCV_MODE_RECEIVER_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2308_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2308_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2308_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->amppd_desc.mask = AW_PID_2308_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2308_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2308_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2308_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2308_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2308_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->uls_hmute_desc.mask = AW_PID_2308_ULS_HMUTE_MASK;
	aw_pa->uls_hmute_desc.enable = AW_PID_2308_ULS_HMUTE_MUTE_VALUE;
	aw_pa->uls_hmute_desc.disable = AW_PID_2308_ULS_HMUTE_NORMAL_VALUE;

	aw_pa->txen_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->txen_desc.mask = AW_PID_2308_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2308_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2308_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_reg = AW_REG_NONE;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2308_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2308_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2308_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2308_CHSEL_RIGHT_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2308_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2308_SYSST_CHECK_MASK;
	aw_pa->sysst_desc.st_check = AW_PID_2308_NO_SWS_SYSST_CHECK;
	aw_pa->sysst_desc.st_sws_check = AW_PID_2308_SWS_SYSST_CHECK;
	aw_pa->sysst_desc.pll_check = AW_PID_2308_IIS_CHECK;

	aw_pa->cco_mux_desc.reg = AW_PID_2308_DBGCTRL_REG;
	aw_pa->cco_mux_desc.mask = AW_PID_2308_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2308_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2308_CCO_MUX_BYPASS_VALUE;

	aw_pa->voltage_desc.reg = AW_PID_2308_VBAT_REG;
	aw_pa->voltage_desc.int_bit = AW_PID_2308_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2308_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_PID_2308_TEMP_REG;
	aw_pa->temp_desc.neg_mask = AW_PID_2308_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2308_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_PID_2308_BSTCTRL2_REG;
	aw_pa->ipeak_desc.mask = AW_PID_2308_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2308_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2308_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2308_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2308_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2308_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_PID_2308_SYSCTRL_REG;
	aw_pa->bop_desc.mask = AW_PID_2308_BOP_EN_MASK;
	aw_pa->bop_desc.enable = AW_PID_2308_BOP_EN_ENABLE_VALUE;
	aw_pa->bop_desc.disbale = AW_PID_2308_BOP_EN_DISABLE_VALUE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->efcheck_desc.reg = AW_REG_NONE;

	aw_pa->dither_desc.reg = AW_REG_NONE;

	aw_pa->auth_desc.reg_in = AW_PID_2308_TESTIN_REG;
	aw_pa->auth_desc.reg_out = AW_PID_2308_TESTOUT_REG;

	aw_pa->noise_gate_desc.reg = AW_PID_2308_NGCTRL3_REG;
	aw_pa->noise_gate_desc.mask = AW_PID_2308_NOISE_GATE_EN_MASK;

	aw_pa->psm_desc.reg = AW_PID_2308_SYSCTRL2_REG;
	aw_pa->psm_desc.mask = AW_PID_2308_PSM_EN_MASK;
	aw_pa->psm_desc.enable = AW_PID_2308_PSM_EN_ENABLE_VALUE;
	aw_pa->psm_desc.disable = AW_PID_2308_PSM_EN_DISABLE_VALUE;

	aw_pa->mpd_desc.reg = AW_PID_2308_SYSCTRL2_REG;
	aw_pa->mpd_desc.mask = AW_PID_2308_EN_MPD_MASK;
	aw_pa->mpd_desc.enable = AW_PID_2308_EN_MPD_ENABLE_VALUE;
	aw_pa->mpd_desc.disable = AW_PID_2308_EN_MPD_DISABLE_VALUE;

	aw_pa->dsmzth_desc.reg = AW_PID_2308_NGCTRL3_REG;
	aw_pa->dsmzth_desc.mask = AW_PID_2308_DSMZTH_MASK;
	aw_pa->dsmzth_desc.enable = AW_PID_2308_DSMZTH_21P33MS_VALUE;
	aw_pa->dsmzth_desc.disable = AW_PID_2308_DSMZTH_NO_RESET_VALUE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

/*
 * g3r: chip 0x2116 ("aw882xx 2116 detected" branch of the stock aw882xx_init, .text+0x11660, 65 insns). The stock block stores
 * only the DIFFERENCES to the defaults of aw_dev_init_cmm (field list below with the stock offsets); in our (template) layout every
 * field is set explicitly: stock value if the block stores it, else the cmm default. 2116 has no monitor file, no reg_force_set /
 * frcset_check, no receiver mode, no vbat / temp / ipeak / efcheck / dither registers, no vcalb registers (all AW_REG_NONE), ef lock off.
 */
static int aw_pid_2116_dev_init(struct aw882xx *aw882xx)
{
	int ret = 0;
	struct aw_device *aw_pa = aw882xx->aw_pa;

	/* no monitor file: the stock leaves the name empty */
	memset(aw_pa->monitor_name, 0, AW_NAME_MAX);

	aw_pa->prof_info.prof_desc = NULL;
	aw_pa->prof_info.count = 0;
	aw_pa->channel = 0;
	aw_pa->bstcfg_enable = AW_BSTCFG_DISABLE;
	aw_pa->bop_en = AW_BOP_DISABLE;
	aw_pa->vol_step = AW_PID_2116_VOL_STEP;

	aw_pa->private_data = (void *)aw882xx;
	aw_pa->dev = aw882xx->dev;
	aw_pa->i2c = aw882xx->i2c;

	aw_pa->ops.aw_get_version = aw882xx_get_version;
	aw_pa->ops.aw_get_dev_num = aw882xx_get_dev_num;
	aw_pa->ops.aw_i2c_read = aw882xx_i2c_read;
	aw_pa->ops.aw_i2c_write = aw882xx_i2c_write;
	aw_pa->ops.aw_i2c_write_bits = aw882xx_i2c_write_bits;
	aw_pa->ops.aw_reg_val_to_db = aw_direct_reg_val_to_db;	/* +0x480 cmm default */
	aw_pa->ops.aw_db_val_to_reg = aw_direct_db_val_to_reg;	/* +0x488 cmm default */
	aw_pa->ops.aw_get_irq_type = aw_get_irq_nobst_type;	/* +0x490: stored by the 2116 block */
	aw_pa->vol_reg = AW_PID_2116_SYSCTRL2_REG;
	aw_pa->vol_shift = AW_PID_2116_VOL_START_BIT;
	aw_pa->vol_mask = 0x03ff;
	aw_pa->reg_max = AW_PID_2116_REG_MAX;			/* +0x1c = 0x72 */

	aw_pa->int_desc.mask_reg = AW_PID_2116_SYSINTM_REG;
	aw_pa->int_desc.mask_default = AW_PID_2116_SYSINTM_DEFAULT;	/* +0xf8 = 0x61ff */
	aw_pa->int_desc.int_mask = AW_PID_2116_SYSINTM_DEFAULT;		/* +0xfc = 0x61ff */
	aw_pa->int_desc.st_reg = AW_PID_2116_SYSINT_REG;

	aw_pa->work_mode.reg = AW_REG_NONE;			/* +0x100 = 0xff */
	aw_pa->work_mode.mask = AW_PID_2116_EN_TRAN_MASK;
	aw_pa->work_mode.spk_val = AW_PID_2116_EN_TRAN_SPK_VALUE;
	aw_pa->work_mode.rcv_val = AW_PID_2116_EN_TRAN_RCV_VALUE;

	aw_pa->pwd_desc.reg = AW_PID_2116_SYSCTRL_REG;
	aw_pa->pwd_desc.mask = AW_PID_2116_PWDN_MASK;
	aw_pa->pwd_desc.enable = AW_PID_2116_PWDN_POWER_DOWN_VALUE;
	aw_pa->pwd_desc.disable = AW_PID_2116_PWDN_WORKING_VALUE;

	aw_pa->amppd_desc.reg = AW_PID_2116_SYSCTRL_REG;	/* +0x128 "btn 2116 amppd", +0x130/+0x138 */
	aw_pa->amppd_desc.mask = AW_PID_2116_AMPPD_MASK;
	aw_pa->amppd_desc.enable = AW_PID_2116_AMPPD_POWER_DOWN_VALUE;
	aw_pa->amppd_desc.disable = AW_PID_2116_AMPPD_WORKING_VALUE;

	aw_pa->mute_desc.reg = AW_PID_2116_SYSCTRL_REG;
	aw_pa->mute_desc.mask = AW_PID_2116_HMUTE_MASK;
	aw_pa->mute_desc.enable = AW_PID_2116_HMUTE_ENABLE_VALUE;
	aw_pa->mute_desc.disable = AW_PID_2116_HMUTE_DISABLE_VALUE;

	aw_pa->uls_hmute_desc.reg = AW_PID_2116_SYSCTRL_REG;	/* +0x158 "btn 2116 uls hmute", +0x160/+0x168 */
	aw_pa->uls_hmute_desc.mask = AW_PID_2116_ULS_HMUTE_MASK;
	aw_pa->uls_hmute_desc.enable = AW_PID_2116_ULS_HMUTE_ENABLE_VALUE;
	aw_pa->uls_hmute_desc.disable = AW_PID_2116_ULS_HMUTE_DISABLE_VALUE;

	aw_pa->txen_desc.reg = AW_PID_2116_I2SCTRL3_REG;	/* +0x170/+0x178 */
	aw_pa->txen_desc.mask = AW_PID_2116_I2STXEN_MASK;
	aw_pa->txen_desc.enable = AW_PID_2116_I2STXEN_ENABLE_VALUE;
	aw_pa->txen_desc.disable = AW_PID_2116_I2STXEN_DISABLE_VALUE;

	aw_pa->vcalb_desc.vcalb_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalk_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.icalkl_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalk_reg = AW_REG_NONE;
	aw_pa->vcalb_desc.vcalkl_reg = AW_REG_NONE;

	aw_pa->spin_desc.rx_desc.reg = AW_PID_2116_I2SCTRL1_REG;
	aw_pa->spin_desc.rx_desc.mask = AW_PID_2116_CHSEL_MASK;
	aw_pa->spin_desc.rx_desc.left_val = AW_PID_2116_CHSEL_LEFT_VALUE;
	aw_pa->spin_desc.rx_desc.right_val = AW_PID_2116_CHSEL_RIGHT_VALUE;

	aw_pa->sysst_desc.reg = AW_PID_2116_SYSST_REG;
	aw_pa->sysst_desc.mask = AW_PID_2116_SYSST_CHECK_MASK;	/* +0x1dc = 0xffffbec4 */
	aw_pa->sysst_desc.st_check = AW_PID_2116_SYSST_CHECK;	/* +0x1e0 = 0x11 */
	aw_pa->sysst_desc.st_sws_check = AW_PID_2116_SWS_SYSST_CHECK;	/* +0x1e4 = 0x111 */
	aw_pa->sysst_desc.pll_check = AW_PID_2116_IIS_CHECK;

	aw_pa->cco_mux_desc.reg = AW_PID_2116_PLLCTRL1_REG;	/* +0x1ec = 0x54 */
	aw_pa->cco_mux_desc.mask = AW_PID_2116_CCO_MUX_MASK;
	aw_pa->cco_mux_desc.divided_val = AW_PID_2116_CCO_MUX_DIVIDED_VALUE;
	aw_pa->cco_mux_desc.bypass_val = AW_PID_2116_CCO_MUX_BYPASS_VALUE;

	aw_pa->voltage_desc.reg = AW_REG_NONE;			/* +0x1fc = 0xff */
	aw_pa->voltage_desc.int_bit = AW_PID_2116_MONITOR_INT_10BIT;
	aw_pa->voltage_desc.vbat_range = AW_PID_2116_MONITOR_VBAT_RANGE;

	aw_pa->temp_desc.reg = AW_REG_NONE;			/* +0x208 = 0xff */
	aw_pa->temp_desc.neg_mask = AW_PID_2116_MONITOR_TEMP_NEG_MASK;
	aw_pa->temp_desc.sign_mask = AW_PID_2116_MONITOR_TEMP_SIGN_MASK;

	aw_pa->ipeak_desc.reg = AW_REG_NONE;			/* +0x214 = 0xff */
	aw_pa->ipeak_desc.mask = AW_PID_2116_BST_IPEAK_MASK;

	aw_pa->volume_desc.reg = AW_PID_2116_SYSCTRL2_REG;
	aw_pa->volume_desc.mask = AW_PID_2116_VOL_MASK;
	aw_pa->volume_desc.shift = AW_PID_2116_VOL_START_BIT;
	aw_pa->volume_desc.mute_volume = AW_PID_2116_MUTE_VOL;
	aw_pa->volume_desc.ctl_volume = AW_PID_2116_VOL_DEFAULT_VALUE;

	aw_pa->bop_desc.reg = AW_PID_2116_SYSCTRL2_REG;	/* +0x3b0 "btn 2116 bop", +0x3b8/+0x3c0 */
	aw_pa->bop_desc.mask = AW_PID_2116_BOP_EN_MASK;
	aw_pa->bop_desc.enable = AW_PID_2116_BOP_EN_ENABLE_VALUE;
	aw_pa->bop_desc.disbale = AW_PID_2116_BOP_EN_DISABLE_VALUE;

	aw_pa->soft_rst.reg = AW882XX_SOFT_RESET_REG;
	aw_pa->soft_rst.reg_value = AW882XX_SOFT_RESET_VALUE;

	aw_pa->efcheck_desc.reg = AW_REG_NONE;
	aw_pa->dither_desc.reg = AW_REG_NONE;

	/* the stock block does not touch the auth register bytes (+0x440/+0x441 stay 0) */
	aw_pa->auth_desc.reg_in = 0;
	aw_pa->auth_desc.reg_out = 0;

	aw_pa->noise_gate_desc.reg = AW_PID_2116_DACCFG10_REG;	/* +0x3e0 "btn 2116 noise gate", +0x3e8/+0x3f0 */
	aw_pa->noise_gate_desc.mask = AW_PID_2116_NOISE_GATE_EN_MASK;

	aw_pa->psm_desc.reg = AW_REG_NONE;
	aw_pa->mpd_desc.reg = AW_REG_NONE;
	aw_pa->dsmzth_desc.reg = AW_REG_NONE;

	ret = 0;

	aw882xx->aw_pa = aw_pa;
	return ret;
}

#include <asm/unaligned.h>
void aw_dev_init_cmm(struct aw_device *aw_dev);
#include "g3n_cmm.inc.h"

void aw_dev_init_cmm(struct aw_device *aw_dev);
int aw882xx_init(struct aw_device *aw_dev)
{
	struct aw882xx *aw882xx = (struct aw882xx *)aw_dev->private_data;
	int ret;

	/* g3n: the stock calls aw_dev_init_cmm first */
	aw_dev_init_cmm(aw_dev);

	/* g3r: ef lock: default 2 items (cmm), the 2013 / 2031 / 2032 blocks use 1, the 2116 block 0 (stock +0xa0) */
	aw_ef_lock_defaults(aw_dev);
	if (aw_dev->chip_id == PID_2013_ID || aw_dev->chip_id == 0x2031 || aw_dev->chip_id == PID_2032_ID)
		aw_dev->ef_lock_desc.num = 1;
	else if (aw_dev->chip_id == 0x2116)
		aw_dev->ef_lock_desc.num = 0;

	switch (aw_dev->chip_id) {
	case PID_1852_ID:
		ret = aw_pid_1852_dev_init(aw882xx);
		break;
	case PID_2013_ID:
		ret = aw_pid_2013_dev_init(aw882xx);
		break;
	case 0x2031:	/* g3p: ids found in the stock dispatch (cmp chain) that the template lacks; same init as 0x2032 [GUESS] */
	case PID_2032_ID:
		ret = aw_pid_2032_dev_init(aw882xx);
		break;
	case PID_2055_ID:
		ret = aw_pid_2055_dev_check(aw882xx);
		break;
	case 0x2070:	/* g3p: stock dispatch: 0x2070, 0x2115, 0x2071 share one branch [GUESS: the 2071 init] */
	case 0x2115:
	case PID_2071_ID:
		ret = aw_pid_2071_dev_init(aw882xx);
		break;
	case 0x2116:	/* g3r: own branch of the stock (.text+0x11660), see aw_pid_2116_dev_init */
		ret = aw_pid_2116_dev_init(aw882xx);
		break;
	case PID_2113_ID:
		ret = aw_pid_2113_dev_init(aw882xx);
		break;
	case PID_2308_ID:
		ret = aw_pid_2308_dev_init(aw882xx);
		break;
	default:
		aw_dev_err(aw882xx->dev, "unsupported chip id 0x%04x", aw882xx->aw_pa->chip_id);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* g3r: common tail of the stock: ef lock check, then the device probe */
	ret = aw882xx_dev_check_ef_lock(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw882xx->dev, "ef has not been locked");
		return ret;
	}

	return aw882xx_dev_probe(aw_dev);
}

#include "g3k_aw_init.inc.h"
