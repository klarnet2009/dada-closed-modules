// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_strategy_fg_class.ko exports. */
#include <linux/module.h>
#include <mca/strategy/strategy_fg_class.h>

int strategy_class_fg_ops_is_init_ok(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_is_init_ok);
int strategy_class_fg_ops_get_soc(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc);
int strategy_class_fg_ops_get_temperature(int *temp) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_temperature);
int strategy_class_fg_ops_get_thermal_temperature(int *temp) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_thermal_temperature);
int strategy_class_fg_ops_get_current(int *curr) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_current);
int strategy_class_fg_ops_get_voltage(int *volt) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_voltage);
int strategy_class_fg_ops_get_cyclecount(int *cycle) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_cyclecount);
bool strategy_class_fg_ops_get_charging_done(void) { return false; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_charging_done);
int strategy_class_fg_get_model_name(const char **model_name) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_model_name);
int strategy_class_fg_get_dc(int *dc) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_dc);
int strategy_class_fg_get_rm(int *rm) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_rm);
int strategy_class_fg_get_fcc(int *fcc) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_fcc);
int strategy_class_fg_get_health(int *health) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_health);
int strategy_class_fg_ops_get_soc_decimal(int *soc_decimal, int *rate) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc_decimal);
int strategy_class_fg_get_soh(int *soh) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_soh);
MODULE_LICENSE("GPL");
