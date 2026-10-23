/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CHARGER_PARTITION_CLASS_H__
#define __CHARGER_PARTITION_CLASS_H__

#include <linux/types.h>

int charger_partition_alloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size);
int charger_partition_dealloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size);
int charger_partition_write(u8 charger_partition_host_type, u8 charger_partition_info_type, void *buf, uint32_t size);
int charger_partition_write_double85(int value);
int charger_partition_write_remove_temp_limit(int value);
int charger_partition_write_memory_test(int value);
int charger_partition_write_soc_limit(int value);
int charger_partition_get_eu_model(bool *is_eu_model);
int charger_partition_read_cuv_count(int *cuv_count, int *hcuv_count);
int charger_partition_read_hscd_count(int *hscd_count);
int charger_partition_read_ocd_count(int *ocd_count, int *hocd_count);
int charger_partition_write_cuv_count(int cuv_count, int hcuv_count);
int charger_partition_write_hcsd_count(int hscd_count);
int charger_partition_write_ocd_count(int ocd_count, int hocd_count);

/* GLOBAL OS4.0.0.7 only (parameter types from the stock BTF: read = pointer to 32-bit / 32-byte buffer, write = by value) */
int charger_partition_read_batt_safe_vlim(int *value);
int charger_partition_write_batt_safe_vlim(int value);
int charger_partition_read_batt_safe_ilim(int *value);
int charger_partition_write_batt_safe_ilim(int value);
int charger_partition_read_batt_emerg_sn(char *batt_emerg_sn);
int charger_partition_write_batt_emerg_sn(const char *batt_emerg_sn);

#endif
