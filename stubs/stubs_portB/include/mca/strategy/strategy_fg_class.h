/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strategy_fg_class.h - fuel gauge strategy class API (provider: mca_strategy_fg_class.ko).
 * struct strategy_fg_class_ops from the stock BTF (member order/types), prototypes from the XRING mca_strategy_fg_class.c.
 */
#ifndef __STRATEGY_FG_CLASS_H__
#define __STRATEGY_FG_CLASS_H__

#include <linux/types.h>

struct strategy_fg_class_ops {
	int (*strategy_fg_is_init_ok)(void *data);
	int (*strategy_fg_is_chip_ok)(void *data);
	int (*strategy_fg_dual_is_chip_ok)(void *data, int fg_index);
	int (*strategy_fg_get_rsoc)(void *data, int *rsoc);
	int (*strategy_fg_get_soc)(void *data);
	int (*strategy_fg_get_temp)(void *data, int *temp);
	int (*strategy_fg_get_thermal_temp)(void *data, int *temp);
	int (*strategy_fg_get_current)(void *data, int *curr);
	int (*strategy_fg_get_voltage)(void *data, int *volt);
	int (*strategy_fg_get_cycle)(void *data, int *cycle);
	int (*strategy_fg_get_rm)(void *data, int *rm);
	int (*strategy_fg_get_voltage_mean)(void *data, int *vol_mean);
	int (*strategy_fg_get_soc_decimal_info)(void *data, int *soc_decimal, int *rate);
	bool (*strategy_fg_get_charging_done)(void *data);
	int (*strategy_fg_set_charging_done)(void *data, bool charging_done);
	int (*strategy_fg_get_model_name)(void *data, const char **model_name);
	int (*strategy_fg_set_fastcharge)(void *data, bool en);
	int (*strategy_fg_get_fastcharge)(void *data);
	int (*strategy_fg_get_authentic)(void *data, bool *authentic);
	int (*strategy_fg_get_dc)(void *data, int *dc);
	int (*strategy_fg_get_fcc)(void *data, int *fcc);
	int (*strategy_fg_get_health)(void *data, int *health);
	int (*strategy_fg_get_soh)(void *data, int *soh);
	int (*strategy_fg_get_first_termiation)(void *data, int *first_termination);
	int (*strategy_fg_get_pack_vendor_id)(void *data, int *pack_vendor_id);
	int (*strategy_fg_get_temp_offset_flag)(void *data, int *temp_offset_flag);
};

int strategy_class_fg_ops_register(void *data, struct strategy_fg_class_ops *ops);
int strategy_class_fg_is_chip_ok(void);
int strategy_class_fg_ops_get_temperature(int *temp);
int strategy_class_fg_ops_set_charging_done(bool charging_done);
int strategy_class_fg_set_fastcharge(bool en);
int strategy_class_fg_ops_get_current(int *curr);
int strategy_class_fg_ops_get_voltage(int *volt);

#endif /* __STRATEGY_FG_CLASS_H__ */
