// SPDX-License-Identifier: GPL-2.0
/* generated stub provider: only the export CRCs matter */
#include <linux/module.h>
#include <mca/strategy/strategy_fg_class.h>

int strategy_class_fg_ops_register(void *data, struct strategy_fg_class_ops *ops) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_register);
int strategy_class_fg_ops_is_init_ok(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_is_init_ok);
int strategy_class_fg_ops_get_rsoc(int *rsoc) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_rsoc);
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
int strategy_class_fg_get_voltage_mean(int *vol_mean) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_voltage_mean);
int strategy_class_fg_ops_get_soc_decimal(int *soc_decimal, int *rate) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_soc_decimal);
bool strategy_class_fg_ops_get_charging_done(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_charging_done);
int strategy_class_fg_ops_set_charging_done(bool charging_done) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_set_charging_done);
int strategy_class_fg_get_model_name(const char **model_name) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_model_name);
int strategy_class_fg_set_fastcharge(bool en) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_set_fastcharge);
int strategy_class_fg_get_fastcharge(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_fastcharge);
int strategy_class_fg_get_authentic(bool *authentic) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_authentic);
int strategy_class_fg_get_dc(int *dc) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_dc);
int strategy_class_fg_get_rm(int *rm) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_rm);
int strategy_class_fg_get_fcc(int *fcc) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_fcc);
int strategy_class_fg_is_chip_ok(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_is_chip_ok);
int strategy_class_fg_dual_is_chip_ok(int fg_index) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_dual_is_chip_ok);
int strategy_class_fg_get_health(int *health) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_health);
int strategy_class_fg_get_soh(int *soh) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_soh);
int strategy_class_fg_get_first_termination(int *first_termination) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_first_termination);
int strategy_class_fg_get_pack_vendor_id(int *pack_vendor_id) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_pack_vendor_id);
int strategy_class_fg_get_temp_offset_flag(int *temp_offset_flag) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_get_temp_offset_flag);

MODULE_LICENSE("GPL");
