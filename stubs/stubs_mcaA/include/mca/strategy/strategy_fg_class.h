/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strategy_fg_class.h - fuel gauge strategy class API (provider: mca_strategy_fg_class.ko).
 * Prototypes from the XRING mca_strategy_fg_class.c, verified against the stock BTF/CRCs.
 * Only the functions needed by the reconstructed modules are declared; extend as needed.
 */
#ifndef __STRATEGY_FG_CLASS_H__
#define __STRATEGY_FG_CLASS_H__

#include <linux/types.h>

int strategy_class_fg_ops_is_init_ok(void);
int strategy_class_fg_ops_get_soc(void);
int strategy_class_fg_ops_get_temperature(int *temp);
int strategy_class_fg_ops_get_thermal_temperature(int *temp);
int strategy_class_fg_ops_get_current(int *curr);
int strategy_class_fg_ops_get_voltage(int *volt);
int strategy_class_fg_ops_get_cyclecount(int *cycle);
bool strategy_class_fg_ops_get_charging_done(void);
int strategy_class_fg_get_model_name(const char **model_name);
int strategy_class_fg_get_dc(int *dc);
int strategy_class_fg_get_rm(int *rm);
int strategy_class_fg_get_fcc(int *fcc);
int strategy_class_fg_get_health(int *health);
int strategy_class_fg_ops_get_soc_decimal(int *soc_decimal, int *rate);
int strategy_class_fg_get_soh(int *soh);

#endif /* __STRATEGY_FG_CLASS_H__ */
