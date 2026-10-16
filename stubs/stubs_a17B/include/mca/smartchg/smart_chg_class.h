/* SPDX-License-Identifier: GPL-2.0 */
/* smart_chg_class.h - reconstructed API of mca_smart_charge.ko (if_ops layout from the HyperOS 3 stock BTF; OS4 may have changed it) */
#ifndef __MCA_SMART_CHG_CLASS_H__
#define __MCA_SMART_CHG_CLASS_H__

#include <linux/types.h>

enum mca_smartchg_if_chg_type {
	MCA_SMARTCHG_IF_CHG_TYPE_BUCK = 0,
	MCA_SMARTCHG_IF_CHG_TYPE_QC,
	MCA_SMARTCHG_IF_CHG_TYPE_WL_QC,
	MCA_SMARTCHG_IF_CHG_TYPE_JEITA,
	MCA_SMARTCHG_IF_CHG_TYPE_END,
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
};

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops);
int mca_smartchg_set_scene(int scene);
int mca_smartchg_get_scene(void);
void mca_smartchg_set_board_temp(int board_temp);
int mca_smartchg_get_board_temp(void);
int mca_smartchg_get_limit_soc(void);
int mca_smartchg_is_extreme_cold_enabled(void);

#endif
