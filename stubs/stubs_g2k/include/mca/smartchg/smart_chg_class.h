/* SPDX-License-Identifier: GPL-2.0 */
/* smart_chg_class.h - reconstructed API of mca_smart_charge.ko; OS4 (HyperOS 4.0) layout of struct mca_smartchg_if_ops from the stock BTF (88 bytes):
 * +80 is_support_pps_100w (new in OS4; the only implementation, strategy_buckchg_is_support_pps_100w, is `bool f(void)`, kCFI 0x36b1c5a6).
 * type values 0..5 (mca_smartchg_if_ops_register() accepts type < 6); the order is the one stored in the OS4 stock .data of the registrants (G2n, 18.10.2026: first word of each ops struct: strategy_buckchg 0, basic_wireless 1, strategy_quickchg 2, quick_wireless 3, buckchg_jeita 4, charger_thermal 5). */
#ifndef __MCA_SMART_CHG_CLASS_H__
#define __MCA_SMART_CHG_CLASS_H__

#include <linux/types.h>

enum mca_smartchg_if_chg_type {
	MCA_SMARTCHG_IF_CHG_TYPE_BUCK = 0,	/* mca_strategy_buckchg */
	MCA_SMARTCHG_IF_CHG_TYPE_WL_BUCK = 1,	/* mca_basic_wireless */
	MCA_SMARTCHG_IF_CHG_TYPE_QC = 2,	/* mca_strategy_quickchg */
	MCA_SMARTCHG_IF_CHG_TYPE_WL_QC = 3,	/* mca_quick_wireless */
	MCA_SMARTCHG_IF_CHG_TYPE_JEITA = 4,	/* mca_buckchg_jeita */
	MCA_SMARTCHG_IF_CHG_TYPE_THERMAL = 5,	/* mca_charger_thermal */
	MCA_SMARTCHG_IF_CHG_TYPE_END,		/* 6 */
};

struct mca_smartchg_if_ops {
	int type;
	void *data;
	int (*set_delta_fv)(void *data, int value);
	int (*set_delta_ichg)(void *data, int value);
	int (*set_fcc)(void *data, int value);
	int (*set_soc_limit_sts)(void *data, int value);
	int (*set_pwr_boost_sts)(void *data, int value);
	int (*set_wls_super_sts)(void *data, int value);
	int (*set_wls_quiet_sts)(void *data, int value);
	int (*update_baa_para)(void *data, char *name, int value1, int value2);
	bool (*is_support_pps_100w)(void);
};

/* OS4: baa (battery aging) jeita terminal parameters, layout from the BTF of mca_buckchg_jeita (size 20) */
struct smart_batt_temp_range {
	int idx;
	int min;
	int max;
};

struct smart_batt_jeita_term_para {
	struct smart_batt_temp_range t_range;
	int vterm;
	int iterm;
};

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops);
void mca_smartchg_set_scene(int scene);
int mca_smartchg_get_scene(void);
void mca_smartchg_set_board_temp(int board_temp);
int mca_smartchg_get_board_temp(void);
int mca_smartchg_get_limit_soc(void);
int mca_smartchg_is_extreme_cold_enabled(void);

#endif
