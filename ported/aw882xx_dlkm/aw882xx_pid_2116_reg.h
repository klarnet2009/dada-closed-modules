/* SPDX-License-Identifier: GPL-2.0
 * aw882xx_pid_2116_reg.h
 *
 * g3r (18.10.2026): NOT an AWINIC header. Chip 0x2116 has no public header; this file holds only the
 * constants that the OS4 stock aw882xx_init stores for chip id 0x2116 (the branch at .text+0x11660, 65 insns)
 * on top of the defaults of aw_dev_init_cmm. Names follow the 2113 header (same register numbers where they
 * coincide); the register map of the chip is otherwise unknown (no reg table: the stock has none either, it
 * dumps registers 0 .. REG_MAX-1).
 */

#ifndef __AW882XX_PID_2116_REG_H__
#define __AW882XX_PID_2116_REG_H__

/* stock stores 0x72 in the field that holds REG_MAX (+0x1c; 0x80 for 2113 and 2308) */
#define AW_PID_2116_REG_MAX		(0x72)

#define AW_PID_2116_SYSST_REG		(0x01)
#define AW_PID_2116_SYSINT_REG		(0x02)
#define AW_PID_2116_SYSINTM_REG		(0x03)
#define AW_PID_2116_SYSCTRL_REG		(0x04)
#define AW_PID_2116_SYSCTRL2_REG	(0x05)
#define AW_PID_2116_I2SCTRL1_REG	(0x06)
#define AW_PID_2116_I2SCTRL3_REG	(0x08)
#define AW_PID_2116_DACCFG10_REG	(0x16)	/* noise gate enable bit (13) lives here */
#define AW_PID_2116_VBAT_REG		(0x21)	/* not used by 2116: voltage_desc.reg = AW_REG_NONE */
#define AW_PID_2116_PLLCTRL1_REG	(0x54)

/* interrupt mask: stock writes 0x61ff into both mask_default and int_mask (default of the common init is 0xffff) */
#define AW_PID_2116_SYSINTM_DEFAULT	(0x61FF)

/* sysst: mask ~0x413b, check values */
#define AW_PID_2116_SYSST_CHECK_MASK	(~(0x413B))
#define AW_PID_2116_SYSST_CHECK		(0x0011)
#define AW_PID_2116_SWS_SYSST_CHECK	(0x0111)
#define AW_PID_2116_IIS_CHECK		(0x0011)	/* common default, not stored by the 2116 branch */

/* amppd: polarity inverted against the common default (stock: enable 0, disable 2) */
#define AW_PID_2116_AMPPD_MASK			(~(0x2))
#define AW_PID_2116_AMPPD_POWER_DOWN_VALUE	(0x0)
#define AW_PID_2116_AMPPD_WORKING_VALUE		(0x2)

/* common defaults of aw_dev_init_cmm (2116 does not override them) */
#define AW_PID_2116_PWDN_MASK			(~(0x1))
#define AW_PID_2116_PWDN_POWER_DOWN_VALUE	(0x1)
#define AW_PID_2116_PWDN_WORKING_VALUE		(0x0)
#define AW_PID_2116_EN_TRAN_MASK		(~(0x80))
#define AW_PID_2116_EN_TRAN_SPK_VALUE		(0x0)
#define AW_PID_2116_EN_TRAN_RCV_VALUE		(0x80)
#define AW_PID_2116_HMUTE_MASK			(~(0x100))
#define AW_PID_2116_HMUTE_ENABLE_VALUE		(0x100)
#define AW_PID_2116_HMUTE_DISABLE_VALUE		(0x0)

/* uls hmute: SYSCTRL bit 14 */
#define AW_PID_2116_ULS_HMUTE_MASK		(~(0x4000))
#define AW_PID_2116_ULS_HMUTE_ENABLE_VALUE	(0x4000)
#define AW_PID_2116_ULS_HMUTE_DISABLE_VALUE	(0x0)

/* tx enable: I2SCTRL3 bit 6 */
#define AW_PID_2116_I2STXEN_MASK		(~(0x40))
#define AW_PID_2116_I2STXEN_ENABLE_VALUE	(0x40)
#define AW_PID_2116_I2STXEN_DISABLE_VALUE	(0x0)

/* i2s channel select: common default */
#define AW_PID_2116_CHSEL_MASK			(~(0xC00))
#define AW_PID_2116_CHSEL_LEFT_VALUE		(0x400)
#define AW_PID_2116_CHSEL_RIGHT_VALUE		(0x800)

/* cco mux: PLLCTRL1 bit 13 */
#define AW_PID_2116_CCO_MUX_MASK		(~(0x2000))
#define AW_PID_2116_CCO_MUX_DIVIDED_VALUE	(0x0)
#define AW_PID_2116_CCO_MUX_BYPASS_VALUE	(0x2000)

/* monitor: common defaults (2116 has no vbat / temp / ipeak register: reg = AW_REG_NONE) */
#define AW_PID_2116_MONITOR_VBAT_RANGE		(6025)
#define AW_PID_2116_MONITOR_INT_10BIT		(1023)
#define AW_PID_2116_MONITOR_TEMP_SIGN_MASK	(~(1<<9))
#define AW_PID_2116_MONITOR_TEMP_NEG_MASK	(0xFC00)
#define AW_PID_2116_BST_IPEAK_MASK		(~(0xF000))

/* volume: SYSCTRL2 [9:0], mute value 1023 (common default) */
#define AW_PID_2116_VOL_MASK			(~(0x3FF))
#define AW_PID_2116_VOL_START_BIT		(0)
#define AW_PID_2116_MUTE_VOL			(1023)
#define AW_PID_2116_VOL_DEFAULT_VALUE		(0)
#define AW_PID_2116_VOL_STEP			(64)	/* common default 0x40 at +0x38 */

/* bop: SYSCTRL2 bit 14 (reg 5, not reg 4 / bit 12 as the common default) */
#define AW_PID_2116_BOP_EN_MASK			(~(0x4000))
#define AW_PID_2116_BOP_EN_ENABLE_VALUE		(0x4000)
#define AW_PID_2116_BOP_EN_DISABLE_VALUE	(0x0)

/* noise gate: DACCFG10 bit 13 */
#define AW_PID_2116_NOISE_GATE_EN_MASK		(~(0x2000))

#endif /* __AW882XX_PID_2116_REG_H__ */
