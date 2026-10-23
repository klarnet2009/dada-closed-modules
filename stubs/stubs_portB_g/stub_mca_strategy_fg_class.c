// SPDX-License-Identifier: GPL-2.0
/* Stub provider of mca_strategy_fg_class (only the export CRCs matter; the include set is chosen so that genksyms reproduces the stock CRCs). */
#include <linux/module.h>
#include <mca/strategy/strategy_fg_class.h>

int strategy_class_fg_ops_register(void *data, struct strategy_fg_class_ops *ops) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_register);
int strategy_class_fg_is_chip_ok(void) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_is_chip_ok);
int strategy_class_fg_ops_get_temperature(int *temp) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_temperature);
int strategy_class_fg_ops_set_charging_done(bool charging_done) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_set_charging_done);
int strategy_class_fg_set_fastcharge(bool en) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_set_fastcharge);
int strategy_class_fg_ops_get_current(int *curr) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_current);
int strategy_class_fg_ops_get_voltage(int *volt) { return 0; }
EXPORT_SYMBOL(strategy_class_fg_ops_get_voltage);
