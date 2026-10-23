/* SPDX-License-Identifier: GPL-2.0 */
/* mca_charge_interface.h - reconstructed from the stock mca_charge_interface BTF (struct mca_charge_if_ops, 112 bytes) and the XRING source */
#ifndef __MCA_CHARGE_INTERFACE_H__
#define __MCA_CHARGE_INTERFACE_H__

#include <linux/types.h>

#define MCA_CHARGE_IF_MAX_VALUE_BUFF	128

enum mca_charge_if_chg_type {
	MCA_CHARGE_IF_CHG_TYPE_BUCK = 0,
	MCA_CHARGE_IF_CHG_TYPE_MAIN_BUCK,
	MCA_CHARGE_IF_CHG_TYPE_AUX_BUCK,
	MCA_CHARGE_IF_CHG_TYPE_QC,
	MCA_CHARGE_IF_CHG_TYPE_QC_MAIN_PATH,
	MCA_CHARGE_IF_CHG_TYPE_QC_AUX_PATH,
	MCA_CHARGE_IF_CHG_TYPE_QC_DIV1,
	MCA_CHARGE_IF_CHG_TYPE_QC_DIV2,
	MCA_CHARGE_IF_CHG_TYPE_QC_DIV4,
	MCA_CHARGE_IF_CHG_TYPE_WL_BUCK,
	MCA_CHARGE_IF_CHG_TYPE_WL_MAIN_BUCK,
	MCA_CHARGE_IF_CHG_TYPE_WL_AUX_BUCK,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC_MAIN_PATH,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC_AUX_PATH,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV1,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV2,
	MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV4,
	MCA_CHARGE_IF_CHG_TYPE_ALL,
	MCA_CHARGE_IF_CHG_TYPE_END,
};

struct mca_charge_if_ops {
	const char *type_name;
	void *data;
	int (*set_input_suspend)(const char *user, char *value, void *data);
	int (*get_input_suspend)(char *value, void *data);
	int (*set_charge_enable)(const char *user, unsigned int value, void *data);
	int (*get_charge_enable)(char *value, void *data);
	int (*set_input_current_limit)(const char *user, char *value, void *data);
	int (*get_input_current_limit)(char *value, void *data);
	int (*set_charge_current_limit)(const char *user, char *value, void *data);
	int (*get_charge_current_limit)(char *value, void *data);
	int (*set_charge_power_limit)(const char *user, unsigned int value, void *data);
	int (*get_charge_power_limit)(char *value, void *data);
	int (*set_ship_mode_en)(const char *user, unsigned int value, void *data);
	int (*get_ship_mode_status)(bool *value, void *data);
};

int mca_charge_if_ops_register(struct mca_charge_if_ops *ops);

#endif /* __MCA_CHARGE_INTERFACE_H__ */
